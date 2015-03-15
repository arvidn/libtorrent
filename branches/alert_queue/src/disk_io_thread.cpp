/*

Copyright (c) 2007-2014, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/storage.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/file_pool.hpp"
#include <boost/scoped_array.hpp>
#include <boost/bind.hpp>
#include <boost/tuple/tuple.hpp>
#include <set>
#include <vector>

#include "libtorrent/time.hpp"
#include "libtorrent/disk_buffer_pool.hpp"
#include "libtorrent/disk_io_job.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/alert_dispatcher.hpp"
#include "libtorrent/uncork_interface.hpp"
#include "libtorrent/performance_counters.hpp"

#include "libtorrent/debug.hpp"

#if TORRENT_USE_RLIMIT
#include <sys/resource.h>
#endif

#define DEBUG_DISK_THREAD 0

#define DLOG if (DEBUG_DISK_THREAD) debug_log

namespace libtorrent
{

#ifdef TORRENT_DISK_STATS
	// this is defined and used in storage.cpp
	extern FILE* g_access_log;
#endif

#if TORRENT_USE_ASSERTS
	char const* job_name(int j);

	void assert_print_piece(cached_piece_entry const* pe)
	{
		static const char* const cache_state[] =
		{
			"write", "volatile-read", "read-lru", "read-lru-ghost", "read-lfu", "read-lfu-ghost"
		};

		if (pe == NULL)
		{
			assert_print("piece: NULL\n");
		}
		else
		{
			assert_print("piece: %d\nrefcount: %d\npiece_refcount: %d\n"
				"num_blocks: %d\nhashing: %d\n\nhash: %p\nhash_offset: %d\n"
				"cache_state: (%d) %s\noutstanding_flush: %d\npiece: %d\n"
				"num_dirty: %d\nnum_blocks: %d\nblocks_in_piece: %d\n"
				"hashing_done: %d\nmarked_for_deletion: %d\nneed_readback: %d\n"
				"hash_passed: %d\nread_jobs: %d\njobs: %d\n"
				"piece_log:\n"
				, int(pe->piece), pe->refcount, pe->piece_refcount, pe->num_blocks
				, int(pe->hashing), pe->hash, pe->hash ? pe->hash->offset : -1
				, int(pe->cache_state), pe->cache_state >= 0 && pe->cache_state
					< cached_piece_entry::num_lrus ? cache_state[pe->cache_state] : ""
				, int(pe->outstanding_flush), int(pe->piece), int(pe->num_dirty)
				, int(pe->num_blocks), int(pe->blocks_in_piece), int(pe->hashing_done)
				, int(pe->marked_for_deletion), int(pe->need_readback), pe->hash_passes
				, int(pe->read_jobs.size()), int(pe->jobs.size()));
			for (int i = 0; i < pe->piece_log.size(); ++i)
			{
				assert_print(&", %s (%d)"[i==0], job_name(pe->piece_log[i].job), pe->piece_log[i].block);
			}
		}
		assert_print("\n");
	}

#define TORRENT_PIECE_ASSERT(cond, piece) \
	do { if (!(cond)) { assert_print_piece(piece); assert_fail(#cond, __LINE__, __FILE__, TORRENT_FUNCTION, 0); } } while(false)

#else
#define TORRENT_PIECE_ASSERT(cond, piece) do {} while(false)
#endif

	void debug_log(char const* fmt, ...)
	{
#if DEBUG_DISK_THREAD
		static mutex log_mutex;
		va_list v;	
		va_start(v, fmt);

		char usr[2048];
		int len = vsnprintf(usr, sizeof(usr), fmt, v);

		static bool prepend_time = true;
		if (!prepend_time)
		{
			prepend_time = (usr[len-1] == '\n');
			mutex::scoped_lock l(log_mutex);
			fputs(usr, stderr);
			return;
		}
		va_end(v);
		char buf[2300];
		snprintf(buf, sizeof(buf), "%s: [%p] %s", time_now_string(), pthread_self(), usr);
		prepend_time = (usr[len-1] == '\n');
		mutex::scoped_lock l(log_mutex);
		fputs(buf, stderr);
#endif
	}

	static int file_flags_for_job(disk_io_job* j)
	{
		int ret = 0;

		if (!(j->flags & disk_io_job::sequential_access)) ret |= file::random_access;
		if (j->flags & disk_io_job::coalesce_buffers) ret |= file::coalesce_buffers;
		return ret;
	}

// ------- disk_io_thread ------

	disk_io_thread::disk_io_thread(io_service& ios
		, alert_dispatcher* alert_disp
		, counters& cnt
		, void* userdata
		, int block_size)
		: m_num_threads(0)
		, m_num_running_threads(0)
		, m_userdata(userdata)
		, m_last_cache_expiry(min_time())
		, m_last_file_check(clock_type::now())
		, m_file_pool(40)
		, m_disk_cache(block_size, ios, boost::bind(&disk_io_thread::trigger_cache_trim, this), alert_disp)
		, m_stats_counters(cnt)
		, m_ios(ios)
		, m_work(io_service::work(m_ios))
		, m_last_disk_aio_performance_warning(min_time())
		, m_post_alert(alert_disp)
		, m_outstanding_reclaim_message(false)
#if TORRENT_USE_ASSERTS
		, m_magic(0x1337)
#endif
	{
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("disk_io_thread::work");
#endif
		m_disk_cache.set_settings(m_settings);

#ifdef TORRENT_DISK_STATS
		if (g_access_log == NULL) g_access_log = fopen("file_access.log", "a+");
#endif

#if TORRENT_USE_RLIMIT
		// ---- auto-cap open files ----

		struct rlimit rl;
		if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
		{
			// deduct some margin for epoll/kqueue, log files,
			// futexes, shared objects etc.
			rl.rlim_cur -= 20;

			// 80% of the available file descriptors should go to connections
			// 20% goes towards regular files
			m_file_pool.resize((std::min)(m_file_pool.size_limit(), int(rl.rlim_cur * 2 / 10)));
		}
#endif // TORRENT_USE_RLIMIT

		set_num_threads(1);
	}

	disk_io_thread::~disk_io_thread()
	{
		DLOG("destructing disk_io_thread\n");

#if TORRENT_USE_ASSERTS
		// by now, all pieces should have been evicted
		std::pair<block_cache::iterator, block_cache::iterator> pieces
			= m_disk_cache.all_pieces();
		TORRENT_ASSERT(pieces.first == pieces.second);
#endif

#ifdef TORRENT_DISK_STATS
		if (g_access_log)
		{
			FILE* f = g_access_log;
			g_access_log = NULL;
			fclose(f);
		}
#endif

		TORRENT_ASSERT(m_magic == 0x1337);
#if TORRENT_USE_ASSERTS
		m_magic = 0xdead;
#endif
	}

	// TODO: 1 it would be nice to have the number of threads be set dynamically
	void disk_io_thread::set_num_threads(int i, bool wait)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		if (i == m_num_threads) return;

		if (i > m_num_threads)
		{
			while (m_num_threads < i)
			{
				int thread_id = (++m_num_threads) - 1;
				thread_type_t type = generic_thread;

				// the magic number 3 is also used in add_job()
				// every 4:th thread is a hasher thread
				if ((thread_id & 0x3) == 3) type = hasher_thread;
				m_threads.push_back(boost::shared_ptr<thread>(
					new thread(boost::bind(&disk_io_thread::thread_fun, this, thread_id, type))));
			}
		}
		else
		{
			while (m_num_threads > i) { --m_num_threads; }
			mutex::scoped_lock l(m_job_mutex);
			m_job_cond.notify_all();
			m_hash_job_cond.notify_all();
			l.unlock();
			if (wait) for (int i = m_num_threads; i < m_threads.size(); ++i) m_threads[i]->join();
			// this will detach the threads
			m_threads.resize(m_num_threads);
		}
	}

	char* disk_io_thread::async_allocate_disk_buffer(char const* category
		, boost::function<void(char*)> const& handler)
	{ return m_disk_cache.async_allocate_buffer(category, handler); }

	void disk_io_thread::reclaim_block(block_cache_reference ref)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(ref.storage);
		m_blocks_to_reclaim.push_back(ref);
		if (m_outstanding_reclaim_message) return;

		m_ios.post(boost::bind(&disk_io_thread::commit_reclaimed_blocks, this));
		m_outstanding_reclaim_message = true;
	}

	void disk_io_thread::commit_reclaimed_blocks()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(m_outstanding_reclaim_message);
		m_outstanding_reclaim_message = false;
		mutex::scoped_lock l(m_cache_mutex);
		for (int i = 0; i < m_blocks_to_reclaim.size(); ++i)
			m_disk_cache.reclaim_block(m_blocks_to_reclaim[i]);
		m_blocks_to_reclaim.clear();
	}

	void disk_io_thread::set_settings(settings_pack* pack)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		mutex::scoped_lock l(m_cache_mutex);
		apply_pack(pack, m_settings);
		m_disk_cache.set_settings(m_settings);
	}

	// flush all blocks that are below p->hash.offset, since we've
	// already hashed those blocks, they won't cause any read-back
	int disk_io_thread::try_flush_hashed(cached_piece_entry* p, int cont_block
		, tailqueue& completed_jobs, mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(l.locked());
		TORRENT_ASSERT(cont_block > 0);
		if (p->hash == 0 && !p->hashing_done)
		{
			DLOG("try_flush_hashed: (%d) no hash\n", int(p->piece));
			return 0;
		}

		if (p->num_dirty == 0)
		{
			DLOG("try_flush_hashed: no dirty blocks\n");
			return 0;
		}

		// end is one past the end
		// round offset up to include the last block, which might
		// have an odd size
		int block_size = m_disk_cache.block_size();
		int end = p->hashing_done ? p->blocks_in_piece : (p->hash->offset + block_size - 1) / block_size;

		// nothing has been hashed yet, don't flush anything
		if (end == 0 && !p->need_readback) return 0;

		// the number of contiguous blocks we need to be allowed to flush
		int block_limit = (std::min)(cont_block, int(p->blocks_in_piece));

		// if everything has been hashed, we might as well flush everything
		// regardless of the contiguous block restriction
		if (end == int(p->blocks_in_piece)) block_limit = 1;

		if (p->need_readback)
		{
			// if this piece needs a read-back already, don't
			// try to keep it from being flushed, since we'll
			// need to read it back regardless. Flushing will
			// save blocks that can be used to "save" other
			// pieces from being fllushed prematurely
			end = int(p->blocks_in_piece);
		}

		// count number of blocks that would be flushed
		int num_blocks = 0;
		for (int i = end-1; i >= 0; --i)
			num_blocks += (p->blocks[i].dirty && !p->blocks[i].pending);

		// we did not satisfy the block_limit requirement
		// i.e. too few blocks would be flushed at this point, put it off
		if (block_limit > num_blocks) return 0;

		// if the cache line size is larger than a whole piece, hold
		// off flushing this piece until enough adjacent pieces are
		// full as well.
		int cont_pieces = cont_block / p->blocks_in_piece;

		// at this point, we may enforce flushing full cache stripes even when
		// they span multiple pieces. This won't necessarily work in the general
		// case, because it assumes that the piece picker will have an affinity
		// to download whole stripes at a time. This is why this setting is turned
		// off by default, flushing only one piece at a time

		if (cont_pieces <= 1 || m_settings.get_bool(settings_pack::allow_partial_disk_writes))
		{
			DLOG("try_flush_hashed: (%d) blocks_in_piece: %d end: %d\n"
				, int(p->piece), int(p->blocks_in_piece), end);

			return flush_range(p, 0, end, 0, completed_jobs, l);
		}

		// piece range
		int range_start = (p->piece / cont_pieces) * cont_pieces;
		int range_end = (std::min)(range_start + cont_pieces, p->storage->files()->num_pieces());

		// look through all the pieces in this range to see if
		// they are ready to be flushed. If so, flush them all,
		// otherwise, hold off
		bool range_full = true;
		
		cached_piece_entry* first_piece = NULL;
		DLOG("try_flush_hashed: multi-piece: ");
		for (int i = range_start; i < range_end; ++i)
		{
			if (i == p->piece)
			{
				if (i == range_start) first_piece = p;
				DLOG("[%d self] ", i);
				continue;
			}
			cached_piece_entry* pe = m_disk_cache.find_piece(p->storage.get(), i);
			if (pe == NULL)
			{
				DLOG("[%d NULL] ", i);
				range_full = false;
				break;
			}
			if (i == range_start) first_piece = pe;

			// if this is a read-cache piece, it has already been flushed
			if (pe->cache_state != cached_piece_entry::write_lru)
			{
				DLOG("[%d read-cache] ", i);
				continue;
			}
			int hash_cursor = pe->hash ? pe->hash->offset / block_size : 0;

			// if the piece has all blocks, and they're all dirty, and they've
			// all been hashed, then this piece is eligible for flushing
			if (pe->num_dirty == pe->blocks_in_piece
				&& (pe->hashing_done
					|| hash_cursor == pe->blocks_in_piece
					|| m_settings.get_bool(settings_pack::disable_hash_checks)))
			{
				DLOG("[%d hash-done] ", i);
				continue;
			}

			if (pe->num_dirty < pe->blocks_in_piece)
			{
				DLOG("[%d dirty:%d] ", i, int(pe->num_dirty));
			}
			else if (pe->hashing_done == 0 && hash_cursor < pe->blocks_in_piece)
			{
				DLOG("[%d cursor:%d] ", i, hash_cursor);
			}
			else
			{
				DLOG("[%d xx] ", i);
			}

			// TOOD: in this case, the piece should probably not be flushed yet. are there
			// any more cases where it should?

			range_full = false;
			break;
		}

		if (!range_full)
		{
			DLOG("not flushing\n");
			return 0;
		}
		DLOG("\n");

		// now, build a iovec for all pieces that we want to flush, so that they
		// can be flushed in a single atomic operation. This is especially important
		// when there are more than 1 disk thread, to make sure they don't
		// interleave in undesired places.
		// in order to remember where each piece boundary ended up in the iovec,
		// we keep the indices in the iovec_offset array

		cont_pieces = range_end - range_start;

		file::iovec_t* iov = TORRENT_ALLOCA(file::iovec_t, p->blocks_in_piece * cont_pieces);
		int* flushing = TORRENT_ALLOCA(int, p->blocks_in_piece * cont_pieces);
		// this is the offset into iov and flushing for each piece
		int* iovec_offset = TORRENT_ALLOCA(int, cont_pieces + 1);
		int iov_len = 0;
		// this is the block index each piece starts at
		int block_start = 0;
		// keep track of the pieces that have had their refcount incremented
		// so we know to decrement them later
		int* refcount_pieces = TORRENT_ALLOCA(int, cont_pieces);
		for (int i = 0; i < cont_pieces; ++i)
		{
			cached_piece_entry* pe;
			if (i == p->piece) pe = p;
			else pe = m_disk_cache.find_piece(p->storage.get(), range_start + i);
			if (pe == NULL
				|| pe->cache_state != cached_piece_entry::write_lru)
			{
				refcount_pieces[i] = 0;
				iovec_offset[i] = iov_len;
				block_start += p->blocks_in_piece;
				continue;
			}

			iovec_offset[i] = iov_len;
			refcount_pieces[i] = 1;
			TORRENT_ASSERT_VAL(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);
#if TORRENT_USE_ASSERTS
			pe->piece_log.push_back(piece_log_t(piece_log_t::flushing, -1));
#endif
			++pe->piece_refcount;

			iov_len += build_iovec(pe, 0, p->blocks_in_piece
				, iov + iov_len, flushing + iov_len, block_start);

			block_start += p->blocks_in_piece;
		}
		iovec_offset[cont_pieces] = iov_len;

		// ok, now we have one (or more, but hopefully one) contiguous
		// iovec array. Now, flush it to disk

		TORRENT_ASSERT(first_piece != NULL);

		if (iov_len == 0)
		{
			// we may not exit here if we incremented any piece refcounters
			TORRENT_ASSERT(cont_pieces == 0);
			DLOG("  iov_len: 0 cont_pieces: %d range_start: %d range_end: %d\n"
				, cont_pieces, range_start, range_end);
			return 0;
		}

		l.unlock();

		storage_error error;
		flush_iovec(first_piece, iov, flushing, iov_len, error);

		l.lock();

		block_start = 0;
		for (int i = 0; i < cont_pieces; ++i)
		{
			cached_piece_entry* pe;
			if (i == p->piece) pe = p;
			else pe = m_disk_cache.find_piece(p->storage.get(), range_start + i);
			if (pe == NULL)
			{
				DLOG("iovec_flushed: piece %d gone!\n", range_start + i);
				TORRENT_PIECE_ASSERT(refcount_pieces[i] == 0, pe);
				block_start += p->blocks_in_piece;
				continue;
			}
			if (refcount_pieces[i])
			{
				TORRENT_PIECE_ASSERT(pe->piece_refcount > 0, pe);
				--pe->piece_refcount;
				m_disk_cache.maybe_free_piece(pe);
			}
			int num_blocks = iovec_offset[i+1] - iovec_offset[i];
			iovec_flushed(pe, flushing + iovec_offset[i], num_blocks
				, block_start, error, completed_jobs);
			block_start += p->blocks_in_piece;
		}

		// if the cache is under high pressure, we need to evict
		// the blocks we just flushed to make room for more write pieces
		int evict = m_disk_cache.num_to_evict(0);
		if (evict > 0) m_disk_cache.try_evict_blocks(evict);

		return iov_len;
	}

	// iov and flushing are expected to be arrays to at least pe->blocks_in_piece
	// items in them. Returns the numner of iovecs written to the iov array.
	// The same number of block indices are written to the flushing array. These
	// are block indices that the respecivec iovec structure refers to, since
	// we might not be able to flush everything as a single contiguous block,
	// the block indices indicates where the block run is broken
	// the cache needs to be locked when calling this function
	// block_base_index is the offset added to every block index written to
	// the flushing array. This can be used when building iovecs spanning
	// multiple pieces, the subsequent pieces after the first one, must have
	// their block indices start where the previous one left off
	int disk_io_thread::build_iovec(cached_piece_entry* pe, int start, int end
		, file::iovec_t* iov, int* flushing, int block_base_index)
	{
		INVARIANT_CHECK;

		DLOG("build_iovec: piece=%d [%d, %d)\n"
			, int(pe->piece), start, end);
		TORRENT_PIECE_ASSERT(start >= 0, pe);
		TORRENT_PIECE_ASSERT(start < end, pe);
		end = (std::min)(end, int(pe->blocks_in_piece));

		int piece_size = pe->storage->files()->piece_size(pe->piece);
		TORRENT_PIECE_ASSERT(piece_size > 0, pe);
		
		int iov_len = 0;
		// the blocks we're flushing
		int num_flushing = 0;

#if DEBUG_DISK_THREAD
		DLOG("build_iov: piece: %d [", int(pe->piece));
		for (int i = 0; i < start; ++i) DLOG(".");
#endif

		int block_size = m_disk_cache.block_size();
		int size_left = piece_size;
		for (int i = start; i < end; ++i, size_left -= block_size)
		{
			TORRENT_PIECE_ASSERT(size_left > 0, pe);
			// don't flush blocks that are empty (buf == 0), not dirty
			// (read cache blocks), or pending (already being written)
			if (pe->blocks[i].buf == NULL
				|| pe->blocks[i].pending
				|| !pe->blocks[i].dirty)
			{
				DLOG("-");
				continue;
			}

			// if we fail to lock the block, it' no longer in the cache
			bool locked = m_disk_cache.inc_block_refcount(pe, i, block_cache::ref_flushing);

			// it should always suceed, since it's a dirty block, and
			// should never have been marked as volatile
			TORRENT_ASSERT(locked);

			flushing[num_flushing++] = i + block_base_index;
			iov[iov_len].iov_base = pe->blocks[i].buf;
			iov[iov_len].iov_len = (std::min)(block_size, size_left);
			++iov_len;
			pe->blocks[i].pending = true;

			DLOG("x");
		}
		DLOG("]\n");

		TORRENT_PIECE_ASSERT(iov_len == num_flushing, pe);
		return iov_len;
	}

	// does the actual writing to disk
	// the cached_piece_entry is supposed to point to the
	// first piece, if the iovec spans multiple pieces
	void disk_io_thread::flush_iovec(cached_piece_entry* pe
		, file::iovec_t const* iov, int const* flushing
		, int num_blocks, storage_error& error)
	{
		TORRENT_PIECE_ASSERT(!error, pe);
		TORRENT_PIECE_ASSERT(num_blocks > 0, pe);
		m_stats_counters.inc_stats_counter(counters::num_writing_threads, 1);

		time_point start_time = clock_type::now();
		int block_size = m_disk_cache.block_size();

#if DEBUG_DISK_THREAD
		DLOG("flush_iovec: piece: %d [ ", int(pe->piece));
		for (int i = 0; i < num_blocks; ++i)
			DLOG("%d ", flushing[i]);
		DLOG("]\n");
#endif

		// issue the actual write operation
		file::iovec_t const* iov_start = iov;
		int flushing_start = 0;
		int piece = pe->piece;
		int blocks_in_piece = pe->blocks_in_piece;
		bool failed = false;
		for (int i = 1; i <= num_blocks; ++i)
		{
			if (i < num_blocks && flushing[i] == flushing[i-1]+1) continue;
			int ret = pe->storage->get_storage_impl()->writev(iov_start
				, i - flushing_start
				, piece + flushing[flushing_start] / blocks_in_piece
				, (flushing[flushing_start] % blocks_in_piece) * block_size
				, 0, error);
			if (ret < 0 || error) failed = true;
			iov_start = &iov[i];
			flushing_start = i;
		}

		m_stats_counters.inc_stats_counter(counters::num_writing_threads, -1);

		if (!failed)
		{
			TORRENT_PIECE_ASSERT(!error, pe);
			boost::uint32_t write_time = total_microseconds(clock_type::now() - start_time);
			m_write_time.add_sample(write_time / num_blocks);

			m_stats_counters.inc_stats_counter(counters::num_blocks_written, num_blocks);
			m_stats_counters.inc_stats_counter(counters::num_write_ops);
			m_stats_counters.inc_stats_counter(counters::disk_write_time, write_time);
			m_stats_counters.inc_stats_counter(counters::disk_job_time, write_time);
#if DEBUG_DISK_THREAD
			DLOG("flush_iovec: %d\n", num_blocks);
#endif
		}
#if DEBUG_DISK_THREAD
		else
		{
			DLOG("flush_iovec: error: (%d) %s\n"
				, error.ec.value(), error.ec.message().c_str());
		}
#endif
	}

	// It is necessary to call this function with the blocks produced by
	// build_iovec, to reset their state to not being flushed anymore
	// the cache needs to be locked when calling this function
	void disk_io_thread::iovec_flushed(cached_piece_entry* pe
		, int* flushing, int num_blocks, int block_offset
		, storage_error const& error
		, tailqueue& completed_jobs)
	{
		for (int i = 0; i < num_blocks; ++i)
			flushing[i] -= block_offset;

#if DEBUG_DISK_THREAD
		DLOG("iovec_flushed: piece: %d block_offset: %d [ "
			, int(pe->piece), block_offset);
		for (int i = 0; i < num_blocks; ++i)
			DLOG("%d ", flushing[i]);
		DLOG("]\n");
#endif
		m_disk_cache.blocks_flushed(pe, flushing, num_blocks);

		int block_size = m_disk_cache.block_size();

		if (error)
		{
			fail_jobs_impl(error, pe->jobs, completed_jobs);
		}
		else
		{
			disk_io_job* j = (disk_io_job*)pe->jobs.get_all();
			while (j)
			{
				disk_io_job* next = (disk_io_job*)j->next;
				j->next = NULL;
				TORRENT_PIECE_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage, pe);
				TORRENT_PIECE_ASSERT(j->piece == pe->piece, pe);
				if (j->completed(pe, block_size))
				{
					j->ret = j->d.io.buffer_size;
					j->error = error;
					completed_jobs.push_back(j);
				}
				else
				{
					pe->jobs.push_back(j);
				}
				j = next;
			}
		}
	}

	// issues write operations for blocks in the given
	// range on the given piece.
	int disk_io_thread::flush_range(cached_piece_entry* pe, int start, int end
		, int flags, tailqueue& completed_jobs, mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		INVARIANT_CHECK;

		DLOG("flush_range: piece=%d [%d, %d)\n"
			, int(pe->piece), start, end);
		TORRENT_PIECE_ASSERT(start >= 0, pe);
		TORRENT_PIECE_ASSERT(start < end, pe);
		
		file::iovec_t* iov = TORRENT_ALLOCA(file::iovec_t, pe->blocks_in_piece);
		int* flushing = TORRENT_ALLOCA(int, pe->blocks_in_piece);
		int iov_len = build_iovec(pe, start, end, iov, flushing, 0);
		if (iov_len == 0) return 0;

		TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);
#if TORRENT_USE_ASSERTS
		pe->piece_log.push_back(piece_log_t(piece_log_t::flush_range, -1));
#endif
		++pe->piece_refcount;

		l.unlock();

		storage_error error;
		flush_iovec(pe, iov, flushing, iov_len, error);

		l.lock();

		TORRENT_PIECE_ASSERT(pe->piece_refcount > 0, pe);
		--pe->piece_refcount;
		iovec_flushed(pe, flushing, iov_len, 0, error, completed_jobs);

		// if the cache is under high pressure, we need to evict
		// the blocks we just flushed to make room for more write pieces
		int evict = m_disk_cache.num_to_evict(0);
		if (evict > 0) m_disk_cache.try_evict_blocks(evict);

		m_disk_cache.maybe_free_piece(pe);

		return iov_len;
	}

	void disk_io_thread::fail_jobs(storage_error const& e, tailqueue& jobs_)
	{
		tailqueue jobs;
		fail_jobs_impl(e, jobs_, jobs);
		if (jobs.size()) add_completed_jobs(jobs);
	}

	void disk_io_thread::fail_jobs_impl(storage_error const& e, tailqueue& src, tailqueue& dst)
	{
		while (src.size())
		{
			disk_io_job* j = (disk_io_job*)src.pop_front();
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
			j->ret = -1;
			j->error = e;
			dst.push_back(j);
		}
	}

	void disk_io_thread::flush_piece(cached_piece_entry* pe, int flags
		, tailqueue& completed_jobs, mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		if (flags & flush_delete_cache)
		{
			// delete dirty blocks and post handlers with
			// operation_aborted error code
			fail_jobs_impl(storage_error(boost::asio::error::operation_aborted)
				, pe->jobs, completed_jobs);
			fail_jobs_impl(storage_error(boost::asio::error::operation_aborted)
				, pe->read_jobs, completed_jobs);
			m_disk_cache.abort_dirty(pe);
		}
		else if ((flags & flush_write_cache) && pe->num_dirty > 0)
		{
			// issue write commands
			flush_range(pe, 0, INT_MAX, 0, completed_jobs, l);

			// if we're also flushing the read cache, this piece
			// should be removed as soon as all write jobs finishes
			// otherwise it will turn into a read piece
		}

		// mark_for_deletion may erase the piece from the cache, that's
		// why we don't have the 'i' iterator referencing it at this point
		if (flags & (flush_read_cache | flush_delete_cache))
		{
			fail_jobs_impl(storage_error(boost::asio::error::operation_aborted), pe->jobs, completed_jobs);
			m_disk_cache.mark_for_deletion(pe);
		}
	}

	void disk_io_thread::flush_cache(piece_manager* storage, boost::uint32_t flags
		, tailqueue& completed_jobs, mutex::scoped_lock& l)
	{
		if (storage)
		{
			boost::unordered_set<cached_piece_entry*> const& pieces = storage->cached_pieces();
			std::vector<int> piece_index;
			piece_index.reserve(pieces.size());
			for (boost::unordered_set<cached_piece_entry*>::const_iterator i = pieces.begin()
				, end(pieces.end()); i != end; ++i)
			{
				if ((*i)->get_storage() != storage) continue;
				piece_index.push_back((*i)->piece);
			}

			for (std::vector<int>::iterator i = piece_index.begin()
				, end(piece_index.end()); i != end; ++i)
			{
				cached_piece_entry* pe = m_disk_cache.find_piece(storage, *i);
				if (pe == NULL) continue;
				TORRENT_PIECE_ASSERT(pe->storage.get() == storage, pe);
				flush_piece(pe, flags, completed_jobs, l);
			}
#if TORRENT_USE_ASSERTS
			TORRENT_ASSERT(l.locked());
			// if the user asked to delete the cache for this storage
			// we really should not have any pieces left. This is only called
			// from disk_io_thread::do_delete, which is a fence job and should
			// have any other jobs active, i.e. there should not be any references
			// keeping pieces or blocks alive
			if ((flags & flush_delete_cache) && (flags & flush_expect_clear))
			{
				boost::unordered_set<cached_piece_entry*> const& storage_pieces = storage->cached_pieces();
				for (boost::unordered_set<cached_piece_entry*>::const_iterator i = storage_pieces.begin()
					, end(storage_pieces.end()); i != end; ++i)
				{
					cached_piece_entry* pe = m_disk_cache.find_piece(storage, (*i)->piece);
					TORRENT_PIECE_ASSERT(pe->num_dirty == 0, pe);
				}
			}
#endif
		}
		else
		{
			std::pair<block_cache::iterator, block_cache::iterator> range = m_disk_cache.all_pieces();
			while (range.first != range.second)
			{
				// TODO: it would be nice to optimize this by having the cache
				// pieces also ordered by
				if ((flags & (flush_read_cache | flush_delete_cache)) == 0)
				{
					// if we're not flushing the read cache, and not deleting the
					// cache, skip pieces with no dirty blocks, i.e. read cache
					// pieces
					while (range.first->num_dirty == 0)
					{
						++range.first;
						if (range.first == range.second) return;
					}
				}
				cached_piece_entry* pe = const_cast<cached_piece_entry*>(&*range.first);
				flush_piece(pe, flags, completed_jobs, l);
				range = m_disk_cache.all_pieces();
			}
		}
	}

	// this is called if we're exceeding (or about to exceed) the cache
	// size limit. This means we should not restrict ourselves to contiguous
	// blocks of write cache line size, but try to flush all old blocks
	// this is why we pass in 1 as cont_block to the flushing functions
	void disk_io_thread::try_flush_write_blocks(int num, tailqueue& completed_jobs
		, mutex::scoped_lock& l)
	{
		DLOG("try_flush_write_blocks: %d\n", num);

		list_iterator range = m_disk_cache.write_lru_pieces();
		std::vector<std::pair<piece_manager*, int> > pieces;
		pieces.reserve(m_disk_cache.num_write_lru_pieces());

		for (list_iterator p = range; p.get() && num > 0; p.next())
		{
			cached_piece_entry* e = (cached_piece_entry*)p.get();
			if (e->num_dirty == 0) continue;
			pieces.push_back(std::make_pair(e->storage.get(), int(e->piece)));
		}

		for (std::vector<std::pair<piece_manager*, int> >::iterator i = pieces.begin()
			, end(pieces.end()); i != end; ++i)
		{
			// TODO: instead of doing a lookup each time through the loop, save
			// cached_piece_entry pointers with piece_refcount incremented to pin them
			cached_piece_entry* pe = m_disk_cache.find_piece(i->first, i->second);
			if (pe == NULL) continue;

			// another thread may flush this piece while we're looping and
			// evict it into a read piece and then also evict it to ghost
			if (pe->cache_state != cached_piece_entry::write_lru) continue;

#if TORRENT_USE_ASSERTS
			pe->piece_log.push_back(piece_log_t(piece_log_t::try_flush_write_blocks, -1));
#endif
			++pe->piece_refcount;
			kick_hasher(pe, l);
			num -= try_flush_hashed(pe, 1, completed_jobs, l);
			--pe->piece_refcount;
		}

		// when the write cache is under high pressure, it is likely
		// counter productive to actually do this, since a piece may
		// not have had its flush_hashed job run on it 
		// so only do it if no other thread is currently flushing

		if (num == 0 || m_stats_counters[counters::num_writing_threads] > 0) return;

		// if we still need to flush blocks, start over and flush
		// everything in LRU order (degrade to lru cache eviction)
		for (std::vector<std::pair<piece_manager*, int> >::iterator i = pieces.begin()
			, end(pieces.end()); i != end; ++i)
		{
			cached_piece_entry* pe = m_disk_cache.find_piece(i->first, i->second);
			if (pe == NULL) continue;
			if (pe->num_dirty == 0) continue;

			// another thread may flush this piece while we're looping and
			// evict it into a read piece and then also evict it to ghost
			if (pe->cache_state != cached_piece_entry::write_lru) continue;

			// don't flush blocks that are being hashed by another thread
			if (pe->num_dirty == 0 || pe->hashing) continue;

#if TORRENT_USE_ASSERTS
			pe->piece_log.push_back(piece_log_t(piece_log_t::try_flush_write_blocks2, -1));
#endif
			++pe->piece_refcount;

			num -= flush_range(pe, 0, INT_MAX, 0, completed_jobs, l);
			--pe->piece_refcount;

			m_disk_cache.maybe_free_piece(pe);
		}
	}

	void disk_io_thread::flush_expired_write_blocks(tailqueue& completed_jobs, mutex::scoped_lock& l)
	{
		DLOG("flush_expired_write_blocks\n");

		time_point now = aux::time_now();
		time_duration expiration_limit = seconds(m_settings.get_int(settings_pack::cache_expiry));

#if TORRENT_USE_ASSERTS
		time_point timeout = min_time();
#endif

		cached_piece_entry** to_flush = TORRENT_ALLOCA(cached_piece_entry*, 200);
		int num_flush = 0;

		for (list_iterator p = m_disk_cache.write_lru_pieces(); p.get(); p.next())
		{
			cached_piece_entry* e = (cached_piece_entry*)p.get();
#if TORRENT_USE_ASSERTS
			TORRENT_PIECE_ASSERT(e->expire >= timeout, e);
			timeout = e->expire;
#endif

			// since we're iterating in order of last use, if this piece
			// shouldn't be evicted, none of the following ones will either
			if (now - e->expire < expiration_limit) break;
			if (e->num_dirty == 0) continue;

			TORRENT_PIECE_ASSERT(e->cache_state <= cached_piece_entry::read_lru1 || e->cache_state == cached_piece_entry::read_lru2, e);
#if TORRENT_USE_ASSERTS
			e->piece_log.push_back(piece_log_t(piece_log_t::flush_expired, -1));
#endif
			++e->piece_refcount;
			// We can rely on the piece entry not being removed by
			// incrementing the piece_refcount
			to_flush[num_flush++] = e;
			if (num_flush == 200) break;
		}

		for (int i = 0; i < num_flush; ++i)
		{
			flush_range(to_flush[i], 0, INT_MAX, 0, completed_jobs, l);
			TORRENT_ASSERT(to_flush[i]->piece_refcount > 0);
			--to_flush[i]->piece_refcount;
			m_disk_cache.maybe_free_piece(to_flush[i]);
		}
	}

	typedef int (disk_io_thread::*disk_io_fun_t)(disk_io_job* j, tailqueue& completed_jobs);

	// this is a jump-table for disk I/O jobs
	static const disk_io_fun_t job_functions[] =
	{
		&disk_io_thread::do_read,
		&disk_io_thread::do_write,
		&disk_io_thread::do_hash,
		&disk_io_thread::do_move_storage,
		&disk_io_thread::do_release_files,
		&disk_io_thread::do_delete_files,
		&disk_io_thread::do_check_fastresume,
		&disk_io_thread::do_save_resume_data,
		&disk_io_thread::do_rename_file,
		&disk_io_thread::do_stop_torrent,
		&disk_io_thread::do_cache_piece,
#ifndef TORRENT_NO_DEPRECATE
		&disk_io_thread::do_finalize_file,
#endif
		&disk_io_thread::do_flush_piece,
		&disk_io_thread::do_flush_hashed,
		&disk_io_thread::do_flush_storage,
		&disk_io_thread::do_trim_cache,
		&disk_io_thread::do_file_priority,
		&disk_io_thread::do_load_torrent,
		&disk_io_thread::do_clear_piece,
		&disk_io_thread::do_tick,
	};

	const char* job_action_name[] =
	{
		"read",
		"write",
		"hash",
		"move_storage",
		"release_files",
		"delete_files",
		"check_fastresume",
		"save_resume_data",
		"rename_file",
		"stop_torrent",
		"cache_piece",
		"finalize_file",
		"flush_piece",
		"flush_hashed",
		"flush_storage",
		"trim_cache",
		"set_file_priority",
		"load_torrent",
		"clear_piece",
		"tick_storage",
	};

#if TORRENT_USE_ASSERTS || DEBUG_DISK_THREAD
	char const* job_name(int j)
	{
		if (j < 0 || j >= piece_log_t::last_job)
			return "unknown";

		if (j < piece_log_t::flushing)
			return job_action_name[j];
		return piece_log_t::job_names[j - piece_log_t::flushing];
	}

#endif

	// evict and/or flush blocks if we're exceeding the cache size
	// or used to exceed it and haven't dropped below the low watermark yet
	// the low watermark is dynamic, based on the number of peers waiting
	// on buffers to free up. The more waiters, the lower the low watermark
	// is. Because of this, the target for flushing jobs may have dropped
	// below the number of blocks we flushed by the time we're done flushing
	// that's why we need to call this fairly often. Both before and after
	// a disk job is executed
	void disk_io_thread::check_cache_level(mutex::scoped_lock& l, tailqueue& completed_jobs)
	{
		int evict = m_disk_cache.num_to_evict(0);
		if (evict > 0)
		{
			evict = m_disk_cache.try_evict_blocks(evict);
			// don't evict write jobs if at least one other thread
			// is flushing right now. Doing so could result in
			// unnecessary flushing of the wrong pieces
			if (evict > 0 && m_stats_counters[counters::num_writing_threads] == 0)
			{
				try_flush_write_blocks(evict, completed_jobs, l);
			}
		}
	}

	void disk_io_thread::perform_job(disk_io_job* j, tailqueue& completed_jobs)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(j->next == 0);
		TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

		mutex::scoped_lock l(m_cache_mutex);

		check_cache_level(l, completed_jobs);

		DLOG("perform_job job: %s ( %s%s) piece: %d offset: %d outstanding: %d\n"
			, job_action_name[j->action]
			, (j->flags & disk_io_job::fence) ? "fence ": ""
			, (j->flags & disk_io_job::force_copy) ? "force_copy ": ""
			, j->piece, j->d.io.offset
			, j->storage ? j->storage->num_outstanding_jobs() : -1);

		l.unlock();

		boost::shared_ptr<piece_manager> storage = j->storage;

		// TODO: instead of doing this. pass in the settings to each storage_interface
		// call. Each disk thread could hold its most recent understanding of the settings
		// in a shared_ptr, and update it every time it wakes up from a job. That way
		// each access to the settings won't require a mutex to be held.
		if (storage && storage->get_storage_impl()->m_settings == 0)
			storage->get_storage_impl()->m_settings = &m_settings;

		TORRENT_ASSERT(j->action < sizeof(job_functions)/sizeof(job_functions[0]));

		time_point start_time = clock_type::now();

		m_stats_counters.inc_stats_counter(counters::num_running_disk_jobs, 1);

		// call disk function
		int ret = (this->*(job_functions[j->action]))(j, completed_jobs);

		// note that -2 erros are OK
		TORRENT_ASSERT(ret != -1 || (j->error.ec && j->error.operation != 0));

		m_stats_counters.inc_stats_counter(counters::num_running_disk_jobs, -1);

		if (ret == retry_job)
		{
			mutex::scoped_lock l(m_job_mutex);
			// to avoid busy looping here, give up
			// our quanta in case there aren't any other
			// jobs to run in between

			// TODO: a potentially more efficient solution would be to have a special
			// queue for retry jobs, that's only ever run when a job completes, in
			// any thread. It would only work if counters::num_running_disk_jobs > 0
			
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
	
			bool need_sleep = m_queued_jobs.empty();
			m_queued_jobs.push_back(j);
			l.unlock();
			if (need_sleep) sleep(0);
			return;
		}

#if TORRENT_USE_ASSERT
		// TODO: it should clear the hash state even when there's an error, right?
		if (j->action == disk_io_job::hash && !j->error.ec)
		{
			// a hash job should never return without clearing pe->hash
			l.lock();
			cached_piece_entry* pe = m_disk_cache.find_piece(j);
			if (pe != NULL)
			{
				TORRENT_PIECE_ASSERT(pe->hash == NULL, pe);
			}
			l.unlock();
		}
#endif

		if (ret == defer_handler) return;

		j->ret = ret;

		time_point now = clock_type::now();
		m_job_time.add_sample(total_microseconds(now - start_time));
		completed_jobs.push_back(j);
	}

	int disk_io_thread::do_uncached_read(disk_io_job* j)
	{
		j->buffer = m_disk_cache.allocate_buffer("send buffer");
		if (j->buffer == 0)
		{
			j->error.ec = error::no_memory;
			j->error.operation = storage_error::alloc_cache_piece;
			return -1;
		}

		time_point start_time = clock_type::now();

		int file_flags = file_flags_for_job(j);
		file::iovec_t b = { j->buffer, size_t(j->d.io.buffer_size) };
   
		int ret = j->storage->get_storage_impl()->readv(&b, 1
			, j->piece, j->d.io.offset, file_flags, j->error);
   
		TORRENT_ASSERT(ret >= 0 || j->error.ec);

		if (!j->error.ec)
		{
			boost::uint32_t read_time = total_microseconds(clock_type::now() - start_time);
			m_read_time.add_sample(read_time);

			m_stats_counters.inc_stats_counter(counters::num_read_back);
			m_stats_counters.inc_stats_counter(counters::num_blocks_read);
			m_stats_counters.inc_stats_counter(counters::num_read_ops);
			m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
			m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
		}
		return ret;
	}

	int disk_io_thread::do_read(disk_io_job* j, tailqueue& completed_jobs)
	{
		if (!m_settings.get_bool(settings_pack::use_read_cache)
			|| m_settings.get_int(settings_pack::cache_size) == 0)
		{
			// we're not using a cache. This is the simple path
			// just read straight from the file
			int ret = do_uncached_read(j);

			mutex::scoped_lock l(m_cache_mutex);
			cached_piece_entry* pe = m_disk_cache.find_piece(j);
			if (pe) maybe_issue_queued_read_jobs(pe, completed_jobs);
			return ret;
		}

		int block_size = m_disk_cache.block_size();
		int piece_size = j->storage->files()->piece_size(j->piece);
		int blocks_in_piece = (piece_size + block_size - 1) / block_size;
		int iov_len = m_disk_cache.pad_job(j, blocks_in_piece
			, m_settings.get_int(settings_pack::read_cache_line_size));

		file::iovec_t* iov = TORRENT_ALLOCA(file::iovec_t, iov_len);

		mutex::scoped_lock l(m_cache_mutex);

		int evict = m_disk_cache.num_to_evict(iov_len);
		if (evict > 0) m_disk_cache.try_evict_blocks(evict);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe == NULL)
		{
			// this isn't supposed to happen. The piece is supposed
			// to be allocated when the read job is posted to the
			// queue, and have 'outstanding_read' set to 1
			TORRENT_ASSERT(false);

			int cache_state = (j->flags & disk_io_job::volatile_read)
				? cached_piece_entry::volatile_read_lru
				: cached_piece_entry::read_lru1;
			pe = m_disk_cache.allocate_piece(j, cache_state);
			if (pe == NULL)
			{
				j->error.ec = error::no_memory;
				j->error.operation = storage_error::alloc_cache_piece;
				m_disk_cache.free_iovec(iov, iov_len);
				return -1;
			}
#if TORRENT_USE_ASSERTS
			pe->piece_log.push_back(piece_log_t(piece_log_t::set_outstanding_jobs));
#endif
			pe->outstanding_read = 1;
		}
		TORRENT_PIECE_ASSERT(pe->outstanding_read == 1, pe);

		l.unlock();

		// then we'll actually allocate the buffers
		int ret = m_disk_cache.allocate_iovec(iov, iov_len);

		if (ret < 0)
		{
			ret = do_uncached_read(j);

			mutex::scoped_lock l(m_cache_mutex);
			cached_piece_entry* pe = m_disk_cache.find_piece(j);
			if (pe) maybe_issue_queued_read_jobs(pe, completed_jobs);
			return ret;
		}

		// this is the offset that's aligned to block boundaries
		boost::int64_t adjusted_offset = j->d.io.offset & ~(block_size-1);

		// if this is the last piece, adjust the size of the
		// last buffer to match up
		iov[iov_len-1].iov_len = (std::min)(int(piece_size - adjusted_offset)
			- (iov_len-1) * block_size, block_size);
		TORRENT_ASSERT(iov[iov_len-1].iov_len > 0);

		// at this point, all the buffers are allocated and iov is initizalied
		// and the blocks have their refcounters incremented, so no other thread
		// can remove them. We can now release the cache mutex and dive into the
		// disk operations.

		int file_flags = file_flags_for_job(j);
		time_point start_time = clock_type::now();

		ret = j->storage->get_storage_impl()->readv(iov, iov_len
			, j->piece, adjusted_offset, file_flags, j->error);

		if (!j->error.ec)
		{
			boost::uint32_t read_time = total_microseconds(clock_type::now() - start_time);
			m_read_time.add_sample(read_time / iov_len);

			m_stats_counters.inc_stats_counter(counters::num_blocks_read, iov_len);
			m_stats_counters.inc_stats_counter(counters::num_read_ops);
			m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
			m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
		}

		l.lock();

		if (ret < 0)
		{
			// read failed. free buffers and return error
			m_disk_cache.free_iovec(iov, iov_len);

			cached_piece_entry* pe = m_disk_cache.find_piece(j);
			if (pe == NULL)
			{
				// the piece is supposed to be allocated when the
				// disk job is allocated
				TORRENT_ASSERT(false);
				return ret;
			}
			TORRENT_PIECE_ASSERT(pe->outstanding_read == 1, pe);

			if (pe->read_jobs.size() > 0)
				fail_jobs_impl(j->error, pe->read_jobs, completed_jobs);
			TORRENT_PIECE_ASSERT(pe->read_jobs.size() == 0, pe);
			pe->outstanding_read = 0;
#if TORRENT_USE_ASSERTS
			pe->piece_log.push_back(piece_log_t(piece_log_t::clear_outstanding_jobs));
#endif
			m_disk_cache.maybe_free_piece(pe);
			return ret;
		}

		int block = j->d.io.offset / block_size;
#if TORRENT_USE_ASSERT
		pe->piece_log.push_back(piece_log_t(j->action, block));
#endif
		// as soon we insert the blocks they may be evicted
		// (if using purgeable memory). In order to prevent that
		// until we can read from them, increment the refcounts
		m_disk_cache.insert_blocks(pe, block, iov, iov_len, j, block_cache::blocks_inc_refcount);

		TORRENT_ASSERT(pe->blocks[block].buf);

		int tmp = m_disk_cache.try_read(j, true);
		TORRENT_ASSERT(tmp >= 0);

		maybe_issue_queued_read_jobs(pe, completed_jobs);

		for (int i = 0; i < iov_len; ++i, ++block)
			m_disk_cache.dec_block_refcount(pe, block, block_cache::ref_reading);

		return j->d.io.buffer_size;
	}

	void disk_io_thread::maybe_issue_queued_read_jobs(cached_piece_entry* pe, tailqueue& completed_jobs)
	{
		TORRENT_PIECE_ASSERT(pe->outstanding_read == 1, pe);

		// if we're shutting down, just cancel the jobs
		if (m_num_threads == 0)
		{
			fail_jobs_impl(storage_error(boost::asio::error::operation_aborted), pe->read_jobs, completed_jobs);
			TORRENT_PIECE_ASSERT(pe->read_jobs.size() == 0, pe);
			pe->outstanding_read = 0;
#if TORRENT_USE_ASSERTS
			pe->piece_log.push_back(piece_log_t(piece_log_t::clear_outstanding_jobs));
#endif
			m_disk_cache.maybe_free_piece(pe);
			return;
		}

		// while we were reading, there may have been a few jobs
		// that got queued up also wanting to read from this piece.
		// Any job that is a cache hit now, complete it immediately.
		// Then, issue the first non-cache-hit job. Once it complete
		// it will keep working off this list
		tailqueue stalled_jobs;
		pe->read_jobs.swap(stalled_jobs);

		// the next job to issue (i.e. this is a cache-miss)
		disk_io_job* next_job = NULL;

		while (stalled_jobs.size() > 0)
		{
			disk_io_job* j = (disk_io_job*)stalled_jobs.pop_front();
			TORRENT_ASSERT(j->flags & disk_io_job::in_progress);

			int ret = m_disk_cache.try_read(j);
			if (ret >= 0)
			{
				// cache-hit
				m_stats_counters.inc_stats_counter(counters::num_blocks_cache_hits);
				DLOG("do_read: cache hit\n");
				j->flags |= disk_io_job::cache_hit;
				j->ret = ret;
				completed_jobs.push_back(j);
			}
			else if (ret == -2)
			{
				// error
				j->ret = disk_io_job::operation_failed;
				completed_jobs.push_back(j);
			}
			else
			{
				// cache-miss, issue the first one
				// put back the rest
				if (next_job == NULL)
				{
					next_job = j;
				}
				else
				{
					TORRENT_PIECE_ASSERT(j->piece == pe->piece, pe);
					pe->read_jobs.push_back(j);
				}
			}
		}

		if (next_job)
		{
			add_job(next_job);
		}
		else
		{
			TORRENT_PIECE_ASSERT(pe->read_jobs.size() == 0, pe);
			pe->outstanding_read = 0;
#if TORRENT_USE_ASSERTS
			pe->piece_log.push_back(piece_log_t(piece_log_t::clear_outstanding_jobs));
#endif
			m_disk_cache.maybe_free_piece(pe);
		}
	}

	int disk_io_thread::do_uncached_write(disk_io_job* j)
	{
		time_point start_time = clock_type::now();

		file::iovec_t b = { j->buffer, size_t(j->d.io.buffer_size) };
		int file_flags = file_flags_for_job(j);
   
		m_stats_counters.inc_stats_counter(counters::num_writing_threads, 1);

		// the actual write operation
		int ret = j->storage->get_storage_impl()->writev(&b, 1
			, j->piece, j->d.io.offset, file_flags, j->error);
   
		m_stats_counters.inc_stats_counter(counters::num_writing_threads, -1);

		if (!j->error.ec)
		{
			boost::uint32_t write_time = total_microseconds(clock_type::now() - start_time);
			m_write_time.add_sample(write_time);

			m_stats_counters.inc_stats_counter(counters::num_blocks_written);
			m_stats_counters.inc_stats_counter(counters::num_write_ops);
			m_stats_counters.inc_stats_counter(counters::disk_write_time, write_time);
			m_stats_counters.inc_stats_counter(counters::disk_job_time, write_time);
		}

		m_disk_cache.free_buffer(j->buffer);
		j->buffer = NULL;

		return ret;
	}

	int disk_io_thread::do_write(disk_io_job* j, tailqueue& completed_jobs)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(j->d.io.buffer_size <= m_disk_cache.block_size());

		// should we put this write job in the cache?
		// if we don't use the cache we shouldn't.
		if (m_settings.get_bool(settings_pack::use_write_cache)
				&& m_settings.get_int(settings_pack::cache_size) > 0)
		{
			mutex::scoped_lock l(m_cache_mutex);

			cached_piece_entry* pe = m_disk_cache.find_piece(j);
			if (pe && pe->hashing_done)
			{
#if TORRENT_USE_ASSERT
				print_piece_log(pe->piece_log);
#endif
				TORRENT_ASSERT(pe->blocks[j->d.io.offset / 16 / 1024].buf != j->buffer);
				TORRENT_ASSERT(pe->blocks[j->d.io.offset / 16 / 1024].buf != NULL);
				j->error.ec = error::operation_aborted;
				j->error.operation = storage_error::write;
				return -1;
			}

			pe = m_disk_cache.add_dirty_block(j);

			if (pe)
			{
#if TORRENT_USE_ASSERT
				pe->piece_log.push_back(piece_log_t(j->action, j->d.io.offset / 0x4000));
#endif

				if (!pe->hashing_done
					&& pe->hash == 0
					&& !m_settings.get_bool(settings_pack::disable_hash_checks))
				{
					pe->hash = new partial_hash;
					m_disk_cache.update_cache_state(pe);
				}

				TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);
				++pe->piece_refcount;

				// see if we can progress the hash cursor with this new block
				kick_hasher(pe, l);

				TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);

				// flushes the piece to disk in case
				// it satisfies the condition for a write
				// piece to be flushed
				try_flush_hashed(pe, m_settings.get_int(settings_pack::write_cache_line_size), completed_jobs, l);

				--pe->piece_refcount;
				m_disk_cache.maybe_free_piece(pe);

				return defer_handler;
			}
		}

		// ok, we should just perform this job right now.
		return do_uncached_write(j);
	}

	void disk_io_thread::async_read(piece_manager* storage, peer_request const& r
		, boost::function<void(disk_io_job const*)> const& handler, void* requester
		, int flags)
	{
		INVARIANT_CHECK;

#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		TORRENT_ASSERT(r.length <= m_disk_cache.block_size());
		TORRENT_ASSERT(r.length <= 16 * 1024);

		DLOG("do_read piece: %d block: %d\n", r.piece, r.start / m_disk_cache.block_size());

		disk_io_job* j = allocate_job(disk_io_job::read);
		j->storage = storage->shared_from_this();
		j->piece = r.piece;
		j->d.io.offset = r.start;
		j->d.io.buffer_size = r.length;
		j->buffer = 0;
		j->flags = flags;
		j->requester = requester;
		j->callback = handler;

		mutex::scoped_lock l(m_cache_mutex);
		int ret = prep_read_job_impl(j);
		l.unlock();

		switch (ret)
		{
			case 0:
				if (handler) handler(j);
				free_job(j);
				break;
			case 1:
				add_job(j);
				break;
		}
	}

	// this function checks to see if a read job is a cache hit,
	// and if it doesn't have a picece allocated, it allocates
	// one and it sets outstanding_read flag and possibly queues
	// up the job in the piece read job list
	// the cache mutex must be held when calling this
	// 
	// returns 0 if the job succeeded immediately
	// 1 if it needs to be added to the job queue
	// 2 if it was deferred and will be performed later (no need to
	//   add it to the queue)
	int disk_io_thread::prep_read_job_impl(disk_io_job* j, bool check_fence)
	{
		TORRENT_ASSERT(j->action == disk_io_job::read);

		if (m_settings.get_bool(settings_pack::use_read_cache)
			&& m_settings.get_int(settings_pack::cache_size) > 0)
		{
			int ret = m_disk_cache.try_read(j);
			if (ret >= 0)
			{
				m_stats_counters.inc_stats_counter(counters::num_blocks_cache_hits);
				DLOG("do_read: cache hit\n");
				j->flags |= disk_io_job::cache_hit;
				j->ret = ret;
				return 0;
			}
			else if (ret == -2)
			{
				j->error.ec = error::no_memory;
				j->error.operation = storage_error::alloc_cache_piece;
				j->ret = disk_io_job::operation_failed;
				return 0;
			}

			if (check_fence && j->storage->is_blocked(j))
			{
				// this means the job was queued up inside storage
				m_stats_counters.inc_stats_counter(counters::blocked_disk_jobs);
				DLOG("blocked job: %s (torrent: %d total: %d)\n"
					, job_action_name[j->action], j->storage ? j->storage->num_blocked() : 0
					, int(m_stats_counters[counters::blocked_disk_jobs]));
				return 2;
			}

			cached_piece_entry* pe = m_disk_cache.allocate_piece(j, cached_piece_entry::read_lru1);
			if (pe == NULL)
			{
				j->ret = -1;
				j->error.ec = error::no_memory;
				j->error.operation = storage_error::read;
				return 0;
			}

			if (pe->outstanding_read)
			{
				TORRENT_PIECE_ASSERT(j->piece == pe->piece, pe);
				pe->read_jobs.push_back(j);
				return 2;
			}

#if TORRENT_USE_ASSERTS
			pe->piece_log.push_back(piece_log_t(piece_log_t::set_outstanding_jobs));
#endif
			pe->outstanding_read = 1;
		}
		return 1;
	}

	void disk_io_thread::async_write(piece_manager* storage, peer_request const& r
		, disk_buffer_holder& buffer
		, boost::function<void(disk_io_job const*)> const& handler
		, int flags)
	{
		INVARIANT_CHECK;

#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		TORRENT_ASSERT(r.length <= m_disk_cache.block_size());
		TORRENT_ASSERT(r.length <= 16 * 1024);

		disk_io_job* j = allocate_job(disk_io_job::write);
		j->storage = storage->shared_from_this();
		j->piece = r.piece;
		j->d.io.offset = r.start;
		j->d.io.buffer_size = r.length;
		j->buffer = buffer.get();
		j->callback = handler;
		j->flags = flags;

#if TORRENT_USE_ASSERT
		mutex::scoped_lock l3_(m_cache_mutex);
		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe)
		{
			// we should never add a new dirty block to a piece
			// whose hash we have calculated. The piece needs
			// to be cleared first, (async_clear_piece).
			TORRENT_ASSERT(pe->hashing_done == 0);

			TORRENT_ASSERT(pe->blocks[r.start / 0x4000].refcount == 0 || pe->blocks[r.start / 0x4000].buf == NULL);
		}
		l3_.unlock();
#endif

#if TORRENT_USE_ASSERT && defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		mutex::scoped_lock l2_(m_cache_mutex);
		std::pair<block_cache::iterator, block_cache::iterator> range = m_disk_cache.all_pieces();
		for (block_cache::iterator i = range.first; i != range.second; ++i)
		{
			cached_piece_entry const& p = *i;
			int bs = m_disk_cache.block_size();
			int piece_size = p.storage->files()->piece_size(p.piece);
			int blocks_in_piece = (piece_size + bs - 1) / bs;
			for (int k = 0; k < blocks_in_piece; ++k)
				TORRENT_PIECE_ASSERT(p.blocks[k].buf != j->buffer, &p);
		}
		l2_.unlock();
#endif

#if !defined TORRENT_DISABLE_POOL_ALLOCATOR && TORRENT_USE_ASSERTS
		mutex::scoped_lock l_(m_cache_mutex);
		TORRENT_ASSERT(m_disk_cache.is_disk_buffer(j->buffer));
		l_.unlock();
#endif
		if (m_settings.get_int(settings_pack::cache_size) > 0
			&& m_settings.get_bool(settings_pack::use_write_cache))
		{
			int block_size = m_disk_cache.block_size();

			TORRENT_ASSERT((r.start % block_size) == 0);

			if (storage->is_blocked(j))
			{
				// this means the job was queued up inside storage
				m_stats_counters.inc_stats_counter(counters::blocked_disk_jobs);
				DLOG("blocked job: %s (torrent: %d total: %d)\n"
					, job_action_name[j->action], j->storage ? j->storage->num_blocked() : 0
					, int(m_stats_counters[counters::blocked_disk_jobs]));
				// make the holder give up ownership of the buffer
				// since the job was successfully queued up
				buffer.release();
				return;
			}

			mutex::scoped_lock l(m_cache_mutex);
			// if we succeed in adding the block to the cache, the job will
			// be added along with it. we may not free j if so
			cached_piece_entry* pe = m_disk_cache.add_dirty_block(j);

			// if the buffer was successfully added to the cache
			// our holder should no longer own it
			if (pe) buffer.release();

			if (pe && pe->outstanding_flush == 0)
			{
				pe->outstanding_flush = 1;
				l.unlock();

				// the block and write job were successfully inserted
				// into the cache. Now, see if we should trigger a flush
				j = allocate_job(disk_io_job::flush_hashed);
				j->storage = storage->shared_from_this();
				j->piece = r.piece;
				j->flags = flags;
				add_job(j);
			}
			// if we added the block (regardless of whether we also
			// issued a flush job or not), we're done.
			if (pe) return;
			l.unlock();
		}

		add_job(j);
		buffer.release();
	}

	void disk_io_thread::async_hash(piece_manager* storage, int piece, int flags
		, boost::function<void(disk_io_job const*)> const& handler, void* requester)
	{
#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		disk_io_job* j = allocate_job(disk_io_job::hash);
		j->storage = storage->shared_from_this();
		j->piece = piece;
		j->callback = handler;
		j->flags = flags;
		j->requester = requester;

		int piece_size = storage->files()->piece_size(piece);

		// first check to see if the hashing is already done
		mutex::scoped_lock l(m_cache_mutex);
		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe && !pe->hashing && pe->hash && pe->hash->offset == piece_size)
		{
			sha1_hash result = pe->hash->h.final();
			memcpy(j->d.piece_hash, &result[0], 20);

			delete pe->hash;
			pe->hash = NULL;

			if (pe->cache_state != cached_piece_entry::volatile_read_lru)
				pe->hashing_done = 1;

#if TORRENT_USE_ASSERTS
			++pe->hash_passes;
#endif
	
			l.unlock();
			if (handler) handler(j);
			free_job(j);
			return;
		}

		add_job(j);
	}

	void disk_io_thread::async_move_storage(piece_manager* storage, std::string const& p, int flags
		, boost::function<void(disk_io_job const*)> const& handler)
	{
#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		disk_io_job* j = allocate_job(disk_io_job::move_storage);
		j->storage = storage->shared_from_this();
		j->buffer = strdup(p.c_str());
		j->callback = handler;
		j->flags = flags;

		add_fence_job(storage, j);
	}

	void disk_io_thread::async_release_files(piece_manager* storage
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		disk_io_job* j = allocate_job(disk_io_job::release_files);
		j->storage = storage->shared_from_this();
		j->callback = handler;

		add_fence_job(storage, j);
	}

	void disk_io_thread::async_delete_files(piece_manager* storage
		, boost::function<void(disk_io_job const*)> const& handler)
	{
#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		// remove cache blocks belonging to this torrent
		tailqueue completed_jobs;

		// remove outstanding jobs belonging to this torrent
		mutex::scoped_lock l2(m_job_mutex);

		// TODO: maybe the tailqueue_iterator should contain a pointer-pointer
		// instead and have an unlink function
		disk_io_job* qj = (disk_io_job*)m_queued_jobs.get_all();
		tailqueue to_abort;

		while (qj)
		{
			disk_io_job* next = (disk_io_job*)qj->next;
#if TORRENT_USE_ASSERTS
			qj->next = NULL;
#endif
			if (qj->storage.get() == storage)
				to_abort.push_back(qj);
			else
				m_queued_jobs.push_back(qj);
			qj = next;
		}
		l2.unlock();

		mutex::scoped_lock l(m_cache_mutex);
		flush_cache(storage, flush_delete_cache, completed_jobs, l);
		l.unlock();

		disk_io_job* j = allocate_job(disk_io_job::delete_files);
		j->storage = storage->shared_from_this();
		j->callback = handler;
		add_fence_job(storage, j);

		fail_jobs_impl(storage_error(boost::asio::error::operation_aborted), to_abort, completed_jobs);

		if (completed_jobs.size())
			add_completed_jobs(completed_jobs);
	}

	void disk_io_thread::async_check_fastresume(piece_manager* storage
		, bdecode_node const* resume_data
		, boost::function<void(disk_io_job const*)> const& handler)
	{
#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		disk_io_job* j = allocate_job(disk_io_job::check_fastresume);
		j->storage = storage->shared_from_this();
		j->buffer = (char*)resume_data;
		j->callback = handler;

		add_fence_job(storage, j);
	}

	void disk_io_thread::async_save_resume_data(piece_manager* storage
		, boost::function<void(disk_io_job const*)> const& handler)
	{
#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		disk_io_job* j = allocate_job(disk_io_job::save_resume_data);
		j->storage = storage->shared_from_this();
		j->buffer = NULL;
		j->callback = handler;

		add_fence_job(storage, j);
	}

	void disk_io_thread::async_rename_file(piece_manager* storage, int index, std::string const& name
		, boost::function<void(disk_io_job const*)> const& handler)
	{
#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		disk_io_job* j = allocate_job(disk_io_job::rename_file);
		j->storage = storage->shared_from_this();
		j->piece = index;
		j->buffer = strdup(name.c_str());
		j->callback = handler;
		add_fence_job(storage, j);
	}

	void disk_io_thread::async_stop_torrent(piece_manager* storage
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		// remove outstanding hash jobs belonging to this torrent
		mutex::scoped_lock l2(m_job_mutex);

		disk_io_job* qj = (disk_io_job*)m_queued_hash_jobs.get_all();
		tailqueue to_abort;

		while (qj)
		{
			disk_io_job* next = (disk_io_job*)qj->next;
#if TORRENT_USE_ASSERTS
			qj->next = NULL;
#endif
			if (qj->storage.get() == storage)
				to_abort.push_back(qj);
			else
				m_queued_hash_jobs.push_back(qj);
			qj = next;
		}
		l2.unlock();

		disk_io_job* j = allocate_job(disk_io_job::stop_torrent);
		j->storage = storage->shared_from_this();
		j->callback = handler;
		add_fence_job(storage, j);

		tailqueue completed_jobs;
		fail_jobs_impl(storage_error(boost::asio::error::operation_aborted), to_abort, completed_jobs);
		if (completed_jobs.size())
			add_completed_jobs(completed_jobs);
	}

	void disk_io_thread::async_cache_piece(piece_manager* storage, int piece
		, boost::function<void(disk_io_job const*)> const& handler)
	{
#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		disk_io_job* j = allocate_job(disk_io_job::cache_piece);
		j->storage = storage->shared_from_this();
		j->piece = piece;
		j->callback = handler;

		add_job(j);
	}

#ifndef TORRENT_NO_DEPRECATE
	void disk_io_thread::async_finalize_file(piece_manager* storage, int file
		, boost::function<void(disk_io_job const*)> const& handler)
	{
#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		disk_io_job* j = allocate_job(disk_io_job::finalize_file);
		j->storage = storage->shared_from_this();
		j->piece = file;
		j->callback = handler;

		add_job(j);
	}
#endif

	void disk_io_thread::async_flush_piece(piece_manager* storage, int piece
		, boost::function<void(disk_io_job const*)> const& handler)
	{
#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		disk_io_job* j = allocate_job(disk_io_job::flush_piece);
		j->storage = storage->shared_from_this();
		j->piece = piece;
		j->callback = handler;

		if (m_num_threads == 0)
		{
			j->error.ec = asio::error::operation_aborted;
			if (handler) handler(j);
			free_job(j);
			return;
		}

		add_job(j);
	}

	void disk_io_thread::async_set_file_priority(piece_manager* storage
		, std::vector<boost::uint8_t> const& prios
		, boost::function<void(disk_io_job const*)> const& handler)
	{
#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		std::vector<boost::uint8_t>* p = new std::vector<boost::uint8_t>(prios);

		disk_io_job* j = allocate_job(disk_io_job::file_priority);
		j->storage = storage->shared_from_this();
		j->buffer = (char*)p;
		j->callback = handler;

		add_fence_job(storage, j);
	}

	void disk_io_thread::async_load_torrent(add_torrent_params* params
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		disk_io_job* j = allocate_job(disk_io_job::load_torrent);
		j->requester = (char*)params;
		j->callback = handler;

		add_job(j);
	}

	void disk_io_thread::async_tick_torrent(piece_manager* storage
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		disk_io_job* j = allocate_job(disk_io_job::tick_storage);
		j->storage = storage->shared_from_this();
		j->callback = handler;

		add_job(j);
	}

	void disk_io_thread::clear_read_cache(piece_manager* storage)
	{
		mutex::scoped_lock l(m_cache_mutex);

		tailqueue jobs;
		boost::unordered_set<cached_piece_entry*> const& cache = storage->cached_pieces();
		for (boost::unordered_set<cached_piece_entry*>::const_iterator i = cache.begin()
			, end(cache.end()); i != end; ++i)
		{
			tailqueue temp;
			m_disk_cache.evict_piece(*(i++), temp);
			jobs.append(temp);
		}
		fail_jobs(storage_error(boost::asio::error::operation_aborted), jobs);
	}

	void disk_io_thread::async_clear_piece(piece_manager* storage, int index
		, boost::function<void(disk_io_job const*)> const& handler)
	{
#ifdef TORRENT_DEBUG
		// the caller must increment the torrent refcount before
		// issuing an async disk request
		storage->assert_torrent_refcount();
#endif

		disk_io_job* j = allocate_job(disk_io_job::clear_piece);
		j->storage = storage->shared_from_this();
		j->piece = index;
		j->callback = handler;

		// regular jobs are not guaranteed to be executed in-order
		// since clear piece must guarantee that all write jobs that
		// have been issued finish before the clear piece job completes

		// TODO: this is potentially very expensive. One way to solve
		// it would be to have a fence for just this one piece.
		add_fence_job(storage, j);
	}

	void disk_io_thread::clear_piece(piece_manager* storage, int index)	
	{
		mutex::scoped_lock l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(storage, index);
		if (pe == 0) return;
		TORRENT_PIECE_ASSERT(pe->hashing == false, pe);
		pe->hashing_done = 0;
		delete pe->hash;
		pe->hash = NULL;

		// evict_piece returns true if the piece was in fact
		// evicted. A piece may fail to be evicted if there
		// are still outstanding operations on it, which should
		// never be the case when this function is used
		// in fact, no jobs should really be hung on this piece
		// at this point
		tailqueue jobs;
		bool ok = m_disk_cache.evict_piece(pe, jobs);
		TORRENT_PIECE_ASSERT(ok, pe);
		fail_jobs(storage_error(boost::asio::error::operation_aborted), jobs);
	}

	void disk_io_thread::kick_hasher(cached_piece_entry* pe, mutex::scoped_lock& l)
	{
		if (!pe->hash) return;
		if (pe->hashing) return;

		int piece_size = pe->storage.get()->files()->piece_size(pe->piece);
		partial_hash* ph = pe->hash;

		// are we already done?
		if (ph->offset >= piece_size) return;

		int block_size = m_disk_cache.block_size();
		int cursor = ph->offset / block_size;
		int end = cursor;
		TORRENT_PIECE_ASSERT(ph->offset % block_size == 0, pe);

		for (int i = cursor; i < pe->blocks_in_piece; ++i)
		{
			cached_block_entry& bl = pe->blocks[i];
			if (bl.buf == 0) break;

			// if we fail to lock the block, it' no longer in the cache
			if (m_disk_cache.inc_block_refcount(pe, i, block_cache::ref_hashing) == false)
				break;

			++end;
		}

		// no blocks to hash?
		if (end == cursor) return;

		pe->hashing = 1;

		DLOG("kick_hasher: %d - %d (piece: %d offset: %d)\n"
			, cursor, end, int(pe->piece), ph->offset);

		l.unlock();

		time_point start_time = clock_type::now();

		for (int i = cursor; i < end; ++i)
		{
			cached_block_entry& bl = pe->blocks[i];
			int size = (std::min)(block_size, piece_size - ph->offset);
			ph->h.update(bl.buf, size);
			ph->offset += size;
		}

		boost::uint64_t hash_time = total_microseconds(clock_type::now() - start_time);

		l.lock();

		TORRENT_PIECE_ASSERT(pe->hashing, pe);
		TORRENT_PIECE_ASSERT(pe->hash, pe);

		m_hash_time.add_sample(hash_time / (end - cursor));

		m_stats_counters.inc_stats_counter(counters::num_blocks_hashed, end - cursor);
		m_stats_counters.inc_stats_counter(counters::disk_hash_time, hash_time);
		m_stats_counters.inc_stats_counter(counters::disk_job_time, hash_time);

		pe->hashing = 0;

		// decrement the block refcounters
		for (int i = cursor; i < end; ++i)
			m_disk_cache.dec_block_refcount(pe, i, block_cache::ref_hashing);

		// did we complete the hash?
		if (pe->hash->offset != piece_size) return;

		// if there are any hash-jobs hanging off of this piece
		// we should post them now
		disk_io_job* j = (disk_io_job*)pe->jobs.get_all();
		tailqueue hash_jobs;
		while (j)
		{
			TORRENT_PIECE_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage, pe);
			disk_io_job* next = (disk_io_job*)j->next;
			j->next = NULL;
			TORRENT_PIECE_ASSERT(j->piece == pe->piece, pe);
			if (j->action == disk_io_job::hash) hash_jobs.push_back(j);
			else pe->jobs.push_back(j);
			j = next;
		}
		if (hash_jobs.size())
		{
			sha1_hash result = pe->hash->h.final();

			for (tailqueue_iterator i = hash_jobs.iterate(); i.get(); i.next())
			{
				disk_io_job* j = (disk_io_job*)i.get();
				memcpy(j->d.piece_hash, &result[0], 20);
				j->ret = 0;
			}

			delete pe->hash;
			pe->hash = NULL;
			if (pe->cache_state != cached_piece_entry::volatile_read_lru)
				pe->hashing_done = 1;
#if TORRENT_USE_ASSERTS
			++pe->hash_passes;
#endif
			add_completed_jobs(hash_jobs);
		}
	}

	int disk_io_thread::do_uncached_hash(disk_io_job* j)
	{
		// we're not using a cache. This is the simple path
		// just read straight from the file
		TORRENT_ASSERT(m_magic == 0x1337);

		int piece_size = j->storage->files()->piece_size(j->piece);
		int block_size = m_disk_cache.block_size();
		int blocks_in_piece = (piece_size + block_size - 1) / block_size;
		int file_flags = file_flags_for_job(j);

		file::iovec_t iov;
		iov.iov_base = m_disk_cache.allocate_buffer("hashing");
		hasher h;
		int ret = 0;
		int offset = 0;
		for (int i = 0; i < blocks_in_piece; ++i)
		{
			DLOG("do_hash: (uncached) reading (piece: %d block: %d)\n"
				, int(j->piece), i);

			time_point start_time = clock_type::now();

			iov.iov_len = (std::min)(block_size, piece_size - offset);
			ret = j->storage->get_storage_impl()->readv(&iov, 1, j->piece
				, offset, file_flags, j->error);
			if (ret < 0) break;

			if (!j->error.ec)
			{
				boost::uint32_t read_time = total_microseconds(clock_type::now() - start_time);
				m_read_time.add_sample(read_time);

				m_stats_counters.inc_stats_counter(counters::num_blocks_read);
				m_stats_counters.inc_stats_counter(counters::num_read_ops);
				m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
				m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
			}

			offset += block_size;
			h.update((char const*)iov.iov_base, iov.iov_len);
		}

		m_disk_cache.free_buffer((char*)iov.iov_base);

		sha1_hash piece_hash = h.final();
		memcpy(j->d.piece_hash, &piece_hash[0], 20);
		return ret >= 0 ? 0 : -1;
	}

	int disk_io_thread::do_hash(disk_io_job* j, tailqueue& completed_jobs)
	{
		INVARIANT_CHECK;

		if (m_settings.get_int(settings_pack::cache_size) == 0)
			return do_uncached_hash(j);

		int piece_size = j->storage->files()->piece_size(j->piece);
		int file_flags = file_flags_for_job(j);

		mutex::scoped_lock l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe)
		{
			TORRENT_ASSERT(pe->in_use);
#if TORRENT_USE_ASSERTS
			pe->piece_log.push_back(piece_log_t(j->action));
#endif
			m_disk_cache.cache_hit(pe, j->requester, j->flags & disk_io_job::volatile_read);

			TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);
			++pe->piece_refcount;
			kick_hasher(pe, l);
			--pe->piece_refcount;

			TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);

			// are we already done hashing?
			if (pe->hash && !pe->hashing && pe->hash->offset == piece_size)
			{
				DLOG("do_hash: (%d) (already done)\n", int(pe->piece));
				sha1_hash piece_hash = pe->hash->h.final();
				memcpy(j->d.piece_hash, &piece_hash[0], 20);
				delete pe->hash;
				pe->hash = NULL;
				if (pe->cache_state != cached_piece_entry::volatile_read_lru)
					pe->hashing_done = 1;
#if TORRENT_USE_ASSERTS
				++pe->hash_passes;
#endif
				m_disk_cache.update_cache_state(pe);
				m_disk_cache.maybe_free_piece(pe);
				return 0;
			}
		}
	
		if (pe == NULL && !m_settings.get_bool(settings_pack::use_read_cache))
		{
			l.unlock();
			// if there's no piece in the cache, and the read cache is disabled
			// it means it's already been flushed to disk, and there's no point
			// in reading it into the cache, since we're not using read cache
			// so just use the uncached version
			return do_uncached_hash(j);
		}

		if (pe == NULL)
		{
			int cache_state = (j->flags & disk_io_job::volatile_read)
				? cached_piece_entry::volatile_read_lru
				: cached_piece_entry::read_lru1;
			pe = m_disk_cache.allocate_piece(j, cache_state);
		}
		if (pe == NULL)
		{
			j->error.ec = error::no_memory;
			j->error.operation = storage_error::alloc_cache_piece;
			return -1;
		}

		if (pe->hashing)
		{
			TORRENT_PIECE_ASSERT(pe->hash, pe);
			// another thread is hashing this piece right now
			// try again in a little bit
			DLOG("do_hash: retry\n");
			// TODO: we should probably just hang the job on the piece and make sure the hasher gets kicked
			return retry_job;
		}

		pe->hashing = 1;

		TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1
			|| pe->cache_state == cached_piece_entry::read_lru2, pe);
		++pe->piece_refcount;

		if (pe->hash == NULL)
		{
			pe->hashing_done = 0;
			pe->hash = new partial_hash;
		}
		partial_hash* ph = pe->hash;

		int block_size = m_disk_cache.block_size();
		int blocks_in_piece = (piece_size + block_size - 1) / block_size;
		
		file::iovec_t iov;
		int ret = 0;

		// keep track of which blocks we have locked by incrementing
		// their refcounts. This is used to decrement only these blocks
		// later.
		int* locked_blocks = TORRENT_ALLOCA(int, blocks_in_piece);
		memset(locked_blocks, 0, blocks_in_piece * sizeof(int));
		int num_locked_blocks = 0;

		// increment the refcounts of all
		// blocks up front, and then hash them without holding the lock
		TORRENT_PIECE_ASSERT(ph->offset % block_size == 0, pe);
		for (int i = ph->offset / block_size; i < blocks_in_piece; ++i)
		{
			iov.iov_len = (std::min)(block_size, piece_size - ph->offset);

			// is the block not in the cache?
			if (pe->blocks[i].buf == NULL) continue;

			// if we fail to lock the block, it' no longer in the cache
			if (m_disk_cache.inc_block_refcount(pe, i, block_cache::ref_hashing) == false)
				continue;

			locked_blocks[num_locked_blocks++] = i;
		}

		l.unlock();

		int next_locked_block = 0;
		for (int i = ph->offset / block_size; i < blocks_in_piece; ++i)
		{
			iov.iov_len = (std::min)(block_size, piece_size - ph->offset);

			if (next_locked_block < num_locked_blocks
				&& locked_blocks[next_locked_block] == i)
			{
				++next_locked_block;
				TORRENT_PIECE_ASSERT(pe->blocks[i].buf, pe);
				TORRENT_PIECE_ASSERT(ph->offset == i * block_size, pe);
				ph->offset += iov.iov_len;
				ph->h.update(pe->blocks[i].buf, iov.iov_len);
			}
			else
			{
				iov.iov_base = m_disk_cache.allocate_buffer("hashing");

				if (iov.iov_base == NULL)
				{
					l.lock();
					// TODO: introduce a holder class that automatically increments
					// and decrements the piece_refcount

					// decrement the refcounts of the blocks we just hashed
					for (int i = 0; i < num_locked_blocks; ++i)
						m_disk_cache.dec_block_refcount(pe, locked_blocks[i], block_cache::ref_hashing);

					--pe->piece_refcount;
					pe->hashing = false;
					delete pe->hash;
					pe->hash = NULL;

					m_disk_cache.maybe_free_piece(pe);

					j->error.ec = errors::no_memory;
					j->error.operation = storage_error::alloc_cache_piece;
					return -1;
				}

				DLOG("do_hash: reading (piece: %d block: %d)\n", int(pe->piece), i);

				time_point start_time = clock_type::now();

				TORRENT_PIECE_ASSERT(ph->offset == i * block_size, pe);
				ret = j->storage->get_storage_impl()->readv(&iov, 1, j->piece
						, ph->offset, file_flags, j->error);

				if (ret < 0)
				{
					m_disk_cache.free_buffer((char*)iov.iov_base);
					l.lock();
					break;
				}

				// treat a short read as an error. The hash will be invalid, the
				// block cannot be cached and the main thread should skip the rest
				// of this file
				if (ret != iov.iov_len)
				{
					ret = -1;
					j->error.ec.assign(boost::asio::error::eof
						, boost::asio::error::get_misc_category());
					m_disk_cache.free_buffer((char*)iov.iov_base);
					l.lock();
					break;
				}

				if (!j->error.ec)
				{
					boost::uint32_t read_time = total_microseconds(clock_type::now() - start_time);
					m_read_time.add_sample(read_time);

					m_stats_counters.inc_stats_counter(counters::num_read_back);
					m_stats_counters.inc_stats_counter(counters::num_blocks_read);
					m_stats_counters.inc_stats_counter(counters::num_read_ops);
					m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
					m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
				}

				TORRENT_PIECE_ASSERT(ph->offset == i * block_size, pe);
				ph->offset += iov.iov_len;
				ph->h.update((char const*)iov.iov_base, iov.iov_len);

				l.lock();
				m_disk_cache.insert_blocks(pe, i, &iov, 1, j);
				l.unlock();
			}
		}

		l.lock();

		// decrement the refcounts of the blocks we just hashed
		for (int i = 0; i < num_locked_blocks; ++i)
			m_disk_cache.dec_block_refcount(pe, locked_blocks[i], block_cache::ref_hashing);

		--pe->piece_refcount;

		pe->hashing = 0;

		if (ret >= 0)
		{
			sha1_hash piece_hash = ph->h.final();
			memcpy(j->d.piece_hash, &piece_hash[0], 20);

			delete pe->hash;
			pe->hash = NULL;
			if (pe->cache_state != cached_piece_entry::volatile_read_lru)
				pe->hashing_done = 1;
#if TORRENT_USE_ASSERTS
			++pe->hash_passes;
#endif
			m_disk_cache.update_cache_state(pe);
		}

		m_disk_cache.maybe_free_piece(pe);

		TORRENT_ASSERT(ret >= 0 || (j->error.ec && j->error.operation != 0));

		return ret < 0 ? ret : 0;
	}

	int disk_io_thread::do_move_storage(disk_io_job* j, tailqueue& completed_jobs)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// if files have to be closed, that's the storage's responsibility
		return j->storage->get_storage_impl()->move_storage(j->buffer, j->flags, j->error);
	}

	int disk_io_thread::do_release_files(disk_io_job* j, tailqueue& completed_jobs)
	{
		INVARIANT_CHECK;

		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		mutex::scoped_lock l(m_cache_mutex);
		flush_cache(j->storage.get(), flush_write_cache, completed_jobs, l);
		l.unlock();

		j->storage->get_storage_impl()->release_files(j->error);
		return j->error ? -1 : 0;
	}

	int disk_io_thread::do_delete_files(disk_io_job* j, tailqueue& completed_jobs)
	{
		TORRENT_ASSERT(j->buffer == 0);
		INVARIANT_CHECK;

		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		mutex::scoped_lock l(m_cache_mutex);
#if TORRENT_USE_ASSERTS
		m_disk_cache.mark_deleted(*j->storage->files());
#endif
		
		flush_cache(j->storage.get(), flush_delete_cache | flush_expect_clear, completed_jobs, l);
		l.unlock();

		j->storage->get_storage_impl()->delete_files(j->error);
		return j->error ? -1 : 0;
	}

	int disk_io_thread::do_check_fastresume(disk_io_job* j, tailqueue& completed_jobs)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		bdecode_node const* rd = (bdecode_node const*)j->buffer;
		bdecode_node tmp;
		if (rd == NULL) rd = &tmp;

		return j->storage->check_fastresume(*rd, j->error);
	}

	int disk_io_thread::do_save_resume_data(disk_io_job* j, tailqueue& completed_jobs)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		mutex::scoped_lock l(m_cache_mutex);
		flush_cache(j->storage.get(), flush_write_cache, completed_jobs, l);
		l.unlock();

		entry* resume_data = new entry(entry::dictionary_t);
		j->storage->get_storage_impl()->write_resume_data(*resume_data, j->error);
		TORRENT_ASSERT(j->buffer == 0);
		j->buffer = (char*)resume_data;
		return j->error ? -1 : 0;
	}

	int disk_io_thread::do_rename_file(disk_io_job* j, tailqueue& completed_jobs)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// if files need to be closed, that's the storage's responsibility
		j->storage->get_storage_impl()->rename_file(j->piece, j->buffer, j->error);
		return j->error ? -1 : 0;
	}

	int disk_io_thread::do_stop_torrent(disk_io_job* j, tailqueue& completed_jobs)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// issue write commands for all dirty blocks
		// and clear all read jobs
		mutex::scoped_lock l(m_cache_mutex);
		flush_cache(j->storage.get(), flush_read_cache | flush_write_cache, completed_jobs, l);
		l.unlock();

		m_disk_cache.release_memory();

		j->storage->get_storage_impl()->release_files(j->error);
		return j->error ? -1 : 0;
	}

	int disk_io_thread::do_cache_piece(disk_io_job* j, tailqueue& completed_jobs)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(j->buffer == 0);
		
		if (m_settings.get_int(settings_pack::cache_size) == 0
			|| m_settings.get_bool(settings_pack::use_read_cache) == false)
			return 0;

		int file_flags = file_flags_for_job(j);

		mutex::scoped_lock l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe == NULL)
		{
			int cache_state = (j->flags & disk_io_job::volatile_read)
				? cached_piece_entry::volatile_read_lru
				: cached_piece_entry::read_lru1;
			pe = m_disk_cache.allocate_piece(j, cache_state);
		}
		if (pe == NULL)
		{
			j->error.ec = error::no_memory;
			j->error.operation = storage_error::alloc_cache_piece;
			return -1;
		}

#if TORRENT_USE_ASSERTS
		pe->piece_log.push_back(piece_log_t(j->action));
#endif
		TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);
		++pe->piece_refcount;

		int block_size = m_disk_cache.block_size();
		int piece_size = j->storage->files()->piece_size(j->piece);
		int blocks_in_piece = (piece_size + block_size - 1) / block_size;
		
		file::iovec_t iov;
		int ret = 0;
		int offset = 0;

		// TODO: it would be nice to not have to lock the mutex every
		// turn through this loop
		for (int i = 0; i < blocks_in_piece; ++i)
		{
			iov.iov_len = (std::min)(block_size, piece_size - offset);

			// is the block already in the cache?
			if (pe->blocks[i].buf) continue;
			l.unlock();

			iov.iov_base = m_disk_cache.allocate_buffer("read cache");

			if (iov.iov_base == NULL)
			{
				//#error introduce a holder class that automatically increments and decrements the piece_refcount
				--pe->piece_refcount;
				m_disk_cache.maybe_free_piece(pe);
				j->error.ec = errors::no_memory;
				j->error.operation = storage_error::alloc_cache_piece;
				return -1;
			}

			DLOG("do_cache_piece: reading (piece: %d block: %d)\n"
				, int(pe->piece), i);

			time_point start_time = clock_type::now();

			ret = j->storage->get_storage_impl()->readv(&iov, 1, j->piece
				, offset, file_flags, j->error);

			if (ret < 0)
			{
				l.lock();
				break;
			}

			if (!j->error.ec)
			{
				boost::uint32_t read_time = total_microseconds(clock_type::now() - start_time);
				m_read_time.add_sample(read_time);

				m_stats_counters.inc_stats_counter(counters::num_blocks_read);
				m_stats_counters.inc_stats_counter(counters::num_read_ops);
				m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
				m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
			}

			offset += block_size;

			l.lock();
			m_disk_cache.insert_blocks(pe, i, &iov, 1, j);
		}

		--pe->piece_refcount;
		m_disk_cache.maybe_free_piece(pe);
		return 0;
	}

#ifndef TORRENT_NO_DEPRECATE
	int disk_io_thread::do_finalize_file(disk_io_job* j, tailqueue& completed_jobs)
	{
		j->storage->get_storage_impl()->finalize_file(j->piece, j->error);
		return j->error ? -1 : 0;
	}
#endif

	void get_cache_info_impl(cached_piece_info& info, cached_piece_entry const* i, int block_size)
	{
		info.piece = i->piece;
		info.storage = i->storage.get();
		info.last_use = i->expire;
		info.need_readback = i->need_readback;
		info.next_to_hash = i->hash == 0 ? -1 : (i->hash->offset + block_size - 1) / block_size;
		info.kind = i->cache_state == cached_piece_entry::write_lru
			? cached_piece_info::write_cache
			: i->cache_state == cached_piece_entry::volatile_read_lru
			? cached_piece_info::volatile_read_cache
			: cached_piece_info::read_cache;
		int blocks_in_piece = i->blocks_in_piece;
		info.blocks.resize(blocks_in_piece);
		for (int b = 0; b < blocks_in_piece; ++b)
			info.blocks[b] = i->blocks[b].buf != 0;
	}

	void disk_io_thread::update_stats_counters(counters& c) const
	{
		// These are atomic_counts, so it's safe to access them from
		// a different thread
		mutex::scoped_lock jl(m_job_mutex);

		c.set_value(counters::num_read_jobs, read_jobs_in_use());
		c.set_value(counters::num_write_jobs, write_jobs_in_use());
		c.set_value(counters::num_jobs, jobs_in_use());
		c.set_value(counters::queued_disk_jobs, m_queued_jobs.size()
			+ m_queued_hash_jobs.size());

		jl.unlock();

		mutex::scoped_lock l(m_cache_mutex);

		// gauges
		c.set_value(counters::disk_blocks_in_use, m_disk_cache.in_use());

		m_disk_cache.update_stats_counters(c);
	}

	void disk_io_thread::get_cache_info(cache_status* ret, bool no_pieces
		, piece_manager const* storage) const
	{
		mutex::scoped_lock l(m_cache_mutex);

#ifndef TORRENT_NO_DEPRECATE
		ret->total_used_buffers = m_disk_cache.in_use();

		ret->blocks_read_hit = m_stats_counters[counters::num_blocks_cache_hits];
		ret->blocks_read = m_stats_counters[counters::num_blocks_read];
		ret->blocks_written = m_stats_counters[counters::num_blocks_written];
		ret->writes = m_stats_counters[counters::num_write_ops];
		ret->reads = m_stats_counters[counters::num_read_ops];

		int num_read_jobs = (std::max)(boost::int64_t(1)
			, m_stats_counters[counters::num_read_ops]);
		int num_write_jobs = (std::max)(boost::int64_t(1)
			, m_stats_counters[counters::num_write_ops]);
		int num_hash_jobs = (std::max)(boost::int64_t(1)
			, m_stats_counters[counters::num_blocks_hashed]);

		ret->average_read_time = m_stats_counters[counters::disk_read_time] / num_read_jobs;
		ret->average_write_time = m_stats_counters[counters::disk_write_time] / num_write_jobs;
		ret->average_hash_time = m_stats_counters[counters::disk_hash_time] / num_hash_jobs;
		ret->average_job_time = m_stats_counters[counters::disk_job_time]
			/ (num_read_jobs + num_write_jobs + num_hash_jobs);
		ret->cumulative_job_time = m_stats_counters[counters::disk_job_time];
		ret->cumulative_read_time = m_stats_counters[counters::disk_read_time];
		ret->cumulative_write_time = m_stats_counters[counters::disk_write_time];
		ret->cumulative_hash_time = m_stats_counters[counters::disk_hash_time];
		ret->total_read_back = m_stats_counters[counters::num_read_back];

		ret->blocked_jobs = m_stats_counters[counters::blocked_disk_jobs];

		ret->num_jobs = jobs_in_use();
		ret->num_read_jobs = read_jobs_in_use();
		ret->read_queue_size = read_jobs_in_use();
		ret->num_write_jobs = write_jobs_in_use();
		ret->pending_jobs = m_stats_counters[counters::num_running_disk_jobs];
		ret->num_writing_threads = m_stats_counters[counters::num_writing_threads];

		for (int i = 0; i < disk_io_job::num_job_ids; ++i)
			ret->num_fence_jobs[i] = m_stats_counters[counters::num_fenced_read + i];

		m_disk_cache.get_stats(ret);

#endif

		ret->pieces.clear();

		if (no_pieces == false)
		{
			int block_size = m_disk_cache.block_size();
   
			if (storage)
			{
				ret->pieces.reserve(storage->num_pieces());
   
				for (boost::unordered_set<cached_piece_entry*>::iterator i
					= storage->cached_pieces().begin(), end(storage->cached_pieces().end());
					i != end; ++i)
				{
					TORRENT_ASSERT((*i)->storage.get() == storage);
   
					if ((*i)->cache_state == cached_piece_entry::read_lru2_ghost
						|| (*i)->cache_state == cached_piece_entry::read_lru1_ghost)
						continue;
					ret->pieces.push_back(cached_piece_info());
					get_cache_info_impl(ret->pieces.back(), *i, block_size);
				}
			}
			else
			{
				ret->pieces.reserve(m_disk_cache.num_pieces());
   
				std::pair<block_cache::iterator, block_cache::iterator> range
					= m_disk_cache.all_pieces();
   
				for (block_cache::iterator i = range.first; i != range.second; ++i)
				{
					if (i->cache_state == cached_piece_entry::read_lru2_ghost
						|| i->cache_state == cached_piece_entry::read_lru1_ghost)
						continue;
					ret->pieces.push_back(cached_piece_info());
					get_cache_info_impl(ret->pieces.back(), &*i, block_size);
				}
			}
		}

		l.unlock();

#ifndef TORRENT_NO_DEPRECATE
		mutex::scoped_lock jl(m_job_mutex);
		ret->queued_jobs = m_queued_jobs.size() + m_queued_hash_jobs.size();
		jl.unlock();
#endif
	}

	int disk_io_thread::do_flush_piece(disk_io_job* j, tailqueue& completed_jobs)
	{
		mutex::scoped_lock l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe == NULL) return 0;

#if TORRENT_USE_ASSERTS
		pe->piece_log.push_back(piece_log_t(j->action));
#endif
		try_flush_hashed(pe, m_settings.get_int(settings_pack::write_cache_line_size), completed_jobs, l);

		return 0;
	}

	// this is triggered every time we insert a new dirty block in a piece
	// by the time this gets executed, the block may already have been flushed
	// triggered by another mechanism.
	int disk_io_thread::do_flush_hashed(disk_io_job* j, tailqueue& completed_jobs)
	{
		mutex::scoped_lock l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);

		if (pe == NULL) return 0;
		if (pe->num_dirty == 0) return 0;

		// if multiple threads are flushing this piece, this assert may fire
		// this happens if the cache is running full and pieces are started to
		// get flushed
//		TORRENT_PIECE_ASSERT(pe->outstanding_flush == 1, pe);

#if TORRENT_USE_ASSERTS
		pe->piece_log.push_back(piece_log_t(j->action));
#endif
		TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1
			|| pe->cache_state == cached_piece_entry::read_lru2, pe);
		++pe->piece_refcount;

		if (!pe->hashing_done)
		{
			if (pe->hash == 0 && !m_settings.get_bool(settings_pack::disable_hash_checks))
			{
				pe->hash = new partial_hash;
				m_disk_cache.update_cache_state(pe);
			}

			// see if we can progress the hash cursor with this new block
			kick_hasher(pe, l);

			TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);
		}

		// flushes the piece to disk in case
		// it satisfies the condition for a write
		// piece to be flushed
		// #error if hash checks are disabled, always just flush
		try_flush_hashed(pe, m_settings.get_int(settings_pack::write_cache_line_size), completed_jobs, l);

		TORRENT_ASSERT(l.locked());

//		TORRENT_PIECE_ASSERT(pe->outstanding_flush == 1, pe);
		pe->outstanding_flush = 0;
		--pe->piece_refcount;

		m_disk_cache.maybe_free_piece(pe);

		return 0;
	}

	int disk_io_thread::do_flush_storage(disk_io_job* j, tailqueue& completed_jobs)
	{
		mutex::scoped_lock l(m_cache_mutex);
		flush_cache(j->storage.get(), flush_write_cache, completed_jobs, l);
		return 0;
	}

	int disk_io_thread::do_trim_cache(disk_io_job* j, tailqueue& completed_jobs)
	{
//#error implement
		return 0;
	}

	int disk_io_thread::do_file_priority(disk_io_job* j, tailqueue& completed_jobs)
	{
		std::vector<boost::uint8_t>* p = reinterpret_cast<std::vector<boost::uint8_t>*>(j->buffer);
		j->storage->get_storage_impl()->set_file_priority(*p, j->error);
		delete p;
		return 0;
	}

	int disk_io_thread::do_load_torrent(disk_io_job* j, tailqueue& completed_jobs)
	{
		add_torrent_params* params = (add_torrent_params*)j->requester;

		std::string filename = resolve_file_url(params->url);
		torrent_info* t = new torrent_info(filename, j->error.ec);
		if (j->error.ec)
		{
			j->buffer = NULL;
			delete t;
		}
		else
		{
			// do this to trigger parsing of the info-dict here. It's better
			// than to have it be done in the network thread. It has enough to
			// do as it is.
			std::string cert = t->ssl_cert();
			j->buffer = (char*)t;
		}

		return 0;
	}

	// this job won't return until all outstanding jobs on this
	// piece are completed or cancelled and the buffers for it
	// have been evicted
	int disk_io_thread::do_clear_piece(disk_io_job* j, tailqueue& completed_jobs)
	{
		mutex::scoped_lock l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe == 0) return 0;
		TORRENT_PIECE_ASSERT(pe->hashing == false, pe);
		pe->hashing_done = 0;
		delete pe->hash;
		pe->hash = NULL;
		pe->hashing_done = false;

#if TORRENT_USE_ASSERTS
		pe->piece_log.push_back(piece_log_t(j->action));
#endif

		// evict_piece returns true if the piece was in fact
		// evicted. A piece may fail to be evicted if there
		// are still outstanding operations on it, in which case
		// try again later
		tailqueue jobs;
		if (m_disk_cache.evict_piece(pe, jobs))
		{
			fail_jobs_impl(storage_error(boost::asio::error::operation_aborted), jobs, completed_jobs);
			return 0;
		}

		m_disk_cache.mark_for_deletion(pe);
		if (pe->num_blocks == 0) return 0;

		// we should always be able to evict the piece, since
		// this is a fence job
		TORRENT_PIECE_ASSERT(false, pe);
		return retry_job;
	}

	int disk_io_thread::do_tick(disk_io_job* j, tailqueue& completed_jobs)
	{
		// true means this storage wants more ticks, false
		// disables ticking (until it's enabled again)
		return j->storage->get_storage_impl()->tick();
	}

	void disk_io_thread::add_fence_job(piece_manager* storage, disk_io_job* j)
	{
		// if this happens, it means we started to shut down
		// the disk threads too early. We have to post all jobs
		// before the disk threads are shut down
		TORRENT_ASSERT(m_num_threads > 0);

		DLOG("add_fence:job: %s (outstanding: %d)\n"
			, job_action_name[j->action]
			, j->storage->num_outstanding_jobs());

		m_stats_counters.inc_stats_counter(counters::num_fenced_read + j->action);

		disk_io_job* fj = allocate_job(disk_io_job::flush_storage);
		fj->storage = j->storage;

		int ret = storage->raise_fence(j, fj, m_stats_counters);
		if (ret == disk_job_fence::fence_post_fence)
		{
			mutex::scoped_lock l(m_job_mutex);
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
			// prioritize fence jobs since they're blocking other jobs
			m_queued_jobs.push_front(j);
			l.unlock();

			// discard the flush job
			free_job(fj);
			return;
		}

		// in this case, we can't run the fence job right now, because there
		// are other jobs outstanding on this storage. We need to trigger a
		// flush of all those jobs now. Only write jobs linger, those are the
		// jobs that needs to be kicked
		TORRENT_ASSERT(j->blocked);
		
		if (ret == disk_job_fence::fence_post_flush)
		{
			// now, we have to make sure that all outstanding jobs on this
			// storage actually get flushed, in order for the fence job to
			// be executed
			mutex::scoped_lock l(m_job_mutex);
			TORRENT_ASSERT((fj->flags & disk_io_job::in_progress) || !fj->storage);

			m_queued_jobs.push_front(fj);
		}
		else
		{
			TORRENT_ASSERT((fj->flags & disk_io_job::in_progress) == 0);
			TORRENT_ASSERT(fj->blocked);
		}
	}

	void disk_io_thread::add_job(disk_io_job* j)
	{
		TORRENT_ASSERT(m_magic == 0x1337);

		TORRENT_ASSERT(!j->storage || j->storage->files()->is_valid());
		TORRENT_ASSERT(j->next == NULL);
		// if this happens, it means we started to shut down
		// the disk threads too early. We have to post all jobs
		// before the disk threads are shut down
		TORRENT_ASSERT(m_num_threads > 0
			|| j->action == disk_io_job::flush_piece
			|| j->action == disk_io_job::trim_cache);

		// this happens for read jobs that get hung on pieces in the
		// block cache, and then get issued
		if (j->flags & disk_io_job::in_progress)
		{
			mutex::scoped_lock l(m_job_mutex);
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
			m_queued_jobs.push_back(j);
			return;
		}

		DLOG("add_job: %s (outstanding: %d)\n"
			, job_action_name[j->action]
			, j->storage ? j->storage->num_outstanding_jobs() : 0);

		// is the fence up for this storage?
		// jobs that are instantaneous are not affected by the fence, is_blocked()
		// will take ownership of the job and queue it up, in case the fence is up
		// if the fence flag is set, this job just raised the fence on the storage
		// and should be scheduled
		if (j->storage && j->storage->is_blocked(j))
		{
			m_stats_counters.inc_stats_counter(counters::blocked_disk_jobs);
			DLOG("blocked job: %s (torrent: %d total: %d)\n"
				, job_action_name[j->action], j->storage ? j->storage->num_blocked() : 0
				, int(m_stats_counters[counters::blocked_disk_jobs]));
			return;
		}

		mutex::scoped_lock l(m_job_mutex);

		TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
		
		// if there are at least 3 threads, there's a hasher thread
		// and the hash jobs go into a separate queue
		// see set_num_threads()
		if (m_num_threads > 3 && j->action == disk_io_job::hash)
			m_queued_hash_jobs.push_back(j);
		else
			m_queued_jobs.push_back(j);
	}

	void disk_io_thread::submit_jobs()
	{
		mutex::scoped_lock l(m_job_mutex);
		if (!m_queued_jobs.empty())
			m_job_cond.notify_all();
		if (!m_queued_hash_jobs.empty())
			m_hash_job_cond.notify_all();
	}

	void disk_io_thread::thread_fun(int thread_id, thread_type_t type)
	{
		DLOG("started disk thread %d\n", int(thread_id));

		++m_num_running_threads;
		m_stats_counters.inc_stats_counter(counters::num_running_threads, 1);

		mutex::scoped_lock l(m_job_mutex);
		for (;;)
		{
			disk_io_job* j = 0;
			if (type == generic_thread)
			{
				TORRENT_ASSERT(l.locked());
				while (m_queued_jobs.empty() && thread_id < m_num_threads) m_job_cond.wait(l);

				// if the number of wanted threads is decreased,
				// we may stop this thread
				// when we're terminating the last thread (id=0), make sure
				// we finish up all queued jobs first
				if (thread_id >= m_num_threads && !(thread_id == 0 && m_queued_jobs.size() > 0))
				{
					// time to exit this thread.
					break;
				}

				j = (disk_io_job*)m_queued_jobs.pop_front();
			}
			else if (type == hasher_thread)
			{
				TORRENT_ASSERT(l.locked());
				while (m_queued_hash_jobs.empty() && thread_id < m_num_threads) m_hash_job_cond.wait(l);
				if (m_queued_hash_jobs.empty() && thread_id >= m_num_threads) break;
				j = (disk_io_job*)m_queued_hash_jobs.pop_front();
			}

			l.unlock();

			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

			if (thread_id == 0)
			{
				// there's no need for all threads to be doing this
				time_point now = clock_type::now();
				if (now > m_last_cache_expiry + seconds(5))
				{
					mutex::scoped_lock l2(m_cache_mutex);
					DLOG("blocked_jobs: %d queued_jobs: %d num_threads %d\n"
						, int(m_stats_counters[counters::blocked_disk_jobs])
						, m_queued_jobs.size(), int(m_num_threads));
					m_last_cache_expiry = now;
					tailqueue completed_jobs;
					flush_expired_write_blocks(completed_jobs, l2);
					l2.unlock();
					if (completed_jobs.size())
						add_completed_jobs(completed_jobs);
				}
			}

			tailqueue completed_jobs;
			perform_job(j, completed_jobs);

			mutex::scoped_lock l2(m_cache_mutex);
			check_cache_level(l2, completed_jobs);
			l2.unlock();

			if (completed_jobs.size())
				add_completed_jobs(completed_jobs);

			l.lock();
		}
		l.unlock();

		// do cleanup in the last running thread 
		m_stats_counters.inc_stats_counter(counters::num_running_threads, -1);
		if (--m_num_running_threads > 0)
		{
			DLOG("exiting disk thread %d. num_threads: %d\n", thread_id, int(m_num_threads));
			TORRENT_ASSERT(m_magic == 0x1337);
			return;
		}

		// at this point, there are no queued jobs left. However, main
		// thread is still running and may still have peer_connections
		// that haven't fully destructed yet, reclaiming their references
		// to read blocks in the disk cache. We need to wait until all
		// references are removed from other threads before we can go
		// ahead with the cleanup.
		mutex::scoped_lock l2(m_cache_mutex);
		while (m_disk_cache.pinned_blocks() > 0)
		{
			l2.unlock();
			sleep(100);
			l2.lock();
		}
		l2.unlock();

		DLOG("disk thread %d is the last one alive. cleaning up\n", thread_id);

		tailqueue jobs;

		m_disk_cache.clear(jobs);
		fail_jobs(storage_error(boost::asio::error::operation_aborted), jobs);

		// close all files. This may take a long
		// time on certain OSes (i.e. Mac OS)
		// that's why it's important to do this in
		// the disk thread in parallel with stopping
		// trackers.
		m_file_pool.release();

#if TORRENT_USE_ASSERTS
		// by now, all pieces should have been evicted
		std::pair<block_cache::iterator, block_cache::iterator> pieces
			= m_disk_cache.all_pieces();
		TORRENT_ASSERT(pieces.first == pieces.second);
#endif
		// release the io_service to allow the run() call to return
		// we do this once we stop posting new callbacks to it.
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("disk_io_thread::work");
#endif
		m_work.reset();
		TORRENT_ASSERT(m_magic == 0x1337);
	}

	// this is a callback called by the block_cache when
	// it's exceeding the disk cache size.
	void disk_io_thread::trigger_cache_trim()
	{
		// we just exceeded the cache size limit. Trigger a trim job
		disk_io_job* j = allocate_job(disk_io_job::trim_cache);
		add_job(j);
		submit_jobs();
	}

	char* disk_io_thread::allocate_disk_buffer(bool& exceeded
		, boost::shared_ptr<disk_observer> o
		, char const* category)
	{
		char* ret = m_disk_cache.allocate_buffer(exceeded, o, category);
		return ret;
	}

	void disk_io_thread::add_completed_job(disk_io_job* j)
	{
		tailqueue tmp;
		tmp.push_back(j);
		add_completed_jobs(tmp);
	}

	void disk_io_thread::add_completed_jobs(tailqueue& jobs)
	{
		tailqueue new_completed_jobs;
		do
		{
			// when a job completes, it's possible for it to cause
			// a fence to be lowered, issuing the jobs queued up
			// behind the fence. It's also possible for some of these
			// jobs to be cache-hits, completing immediately. Those
			// jobs are added to the new_completed_jobs queue and
			// we need to re-issue those
			add_completed_jobs_impl(jobs, new_completed_jobs);
			TORRENT_ASSERT(jobs.size() == 0);
			jobs.swap(new_completed_jobs);
		} while (jobs.size() > 0);
	}

	void disk_io_thread::add_completed_jobs_impl(tailqueue& jobs
		, tailqueue& completed_jobs)
	{
		tailqueue new_jobs;
		int ret = 0;
		for (tailqueue_iterator i = jobs.iterate(); i.get(); i.next())
		{
			disk_io_job* j = (disk_io_job*)i.get();
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

//			DLOG("job_complete %s outstanding: %d\n"
//				, job_action_name[j->action], j->storage ? j->storage->num_outstanding_jobs() : 0);

			if (j->storage)
			{
				if (j->flags & disk_io_job::fence)
				{
					m_stats_counters.inc_stats_counter(
						counters::num_fenced_read + j->action, -1);
				}

				ret += j->storage->job_complete(j, new_jobs);
			}
			TORRENT_ASSERT(ret == new_jobs.size());
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) == 0);
#if TORRENT_USE_ASSERTS
			TORRENT_ASSERT(j->job_posted == false);
			j->job_posted = true;
#endif
		}

#if DEBUG_DISK_THREAD
		if (ret) DLOG("unblocked %d jobs (%d left)\n", ret
			, int(m_stats_counters[counters::blocked_disk_jobs]) - ret);
#endif

		m_stats_counters.inc_stats_counter(counters::blocked_disk_jobs, -ret);
		TORRENT_ASSERT(int(m_stats_counters[counters::blocked_disk_jobs]) >= 0);

		if (new_jobs.size() > 0)
		{
#if TORRENT_USE_ASSERTS
			for (tailqueue_iterator i = new_jobs.iterate(); i.get(); i.next())
			{
				disk_io_job const* j = static_cast<disk_io_job const*>(i.get());
				TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

				if (j->action == disk_io_job::write)
				{
					mutex::scoped_lock l(m_cache_mutex);
					cached_piece_entry* pe = m_disk_cache.find_piece(j);
					if (pe)
					{
						TORRENT_ASSERT(pe->blocks[j->d.io.offset / 16 / 1024].buf != j->buffer);
						TORRENT_ASSERT(pe->blocks[j->d.io.offset / 16 / 1024].buf == NULL);
						TORRENT_ASSERT(!pe->hashing_done);
					}
				}
			}
#endif
			tailqueue other_jobs;
			tailqueue flush_jobs;
			mutex::scoped_lock l_(m_cache_mutex);
			while (new_jobs.size() > 0)
			{
				disk_io_job* j = (disk_io_job*)new_jobs.pop_front();

				if (j->action == disk_io_job::read
					&& m_settings.get_bool(settings_pack::use_read_cache)
					&& m_settings.get_int(settings_pack::cache_size) > 0)
				{
					int ret = prep_read_job_impl(j, false);
					switch (ret)
					{
						case 0:
							completed_jobs.push_back(j);
							break;
						case 1:
							other_jobs.push_back(j);
							break;
					}
					continue;
				}

				// write jobs should be put straight into the cache
				if (j->action != disk_io_job::write)
				{
					other_jobs.push_back(j);
					continue;
				}

				cached_piece_entry* pe = m_disk_cache.find_piece(j);
				pe = m_disk_cache.add_dirty_block(j);

				if (pe == NULL)
				{
					// this isn't correct, since jobs in the jobs
					// queue aren't ordered
					other_jobs.push_back(j);
					continue;
				}

#if TORRENT_USE_ASSERTS
				pe->piece_log.push_back(piece_log_t(j->action, j->d.io.offset / 0x4000));
#endif

				if (!pe->hashing_done
					&& pe->hash == 0
					&& !m_settings.get_bool(settings_pack::disable_hash_checks))
				{
					pe->hash = new partial_hash;
					m_disk_cache.update_cache_state(pe);
				}

				TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);

				if (pe->outstanding_flush == 0)
				{
					pe->outstanding_flush = 1;

					// the block and write job were successfully inserted
					// into the cache. Now, see if we should trigger a flush
					disk_io_job* fj = allocate_job(disk_io_job::flush_hashed);
					fj->storage = j->storage;
					fj->piece = j->piece;
					flush_jobs.push_back(fj);
				}
			}
			l_.unlock();

			mutex::scoped_lock l(m_job_mutex);
			m_queued_jobs.append(other_jobs);
			l.unlock();

			while (flush_jobs.size() > 0)
			{
				disk_io_job* j = (disk_io_job*)flush_jobs.pop_front();
				add_job(j);
			}

			m_job_cond.notify_all();
		}

		mutex::scoped_lock l(m_completed_jobs_mutex);

		bool need_post = m_completed_jobs.size() == 0;
		m_completed_jobs.append(jobs);
		l.unlock();

		if (need_post)
		{
#if DEBUG_DISK_THREAD
			// we take this lock just to make the logging prettier (non-interleaved)
			DLOG("posting job handlers (%d)\n", m_completed_jobs.size());
#endif
			m_ios.post(boost::bind(&disk_io_thread::call_job_handlers, this, m_userdata));
		}
	}

	// This is run in the network thread
	void disk_io_thread::call_job_handlers(void* userdata)
	{
		mutex::scoped_lock l(m_completed_jobs_mutex);

#if DEBUG_DISK_THREAD
		DLOG("call_job_handlers (%d)\n", m_completed_jobs.size());
#endif

		int num_jobs = m_completed_jobs.size();
		disk_io_job* j = (disk_io_job*)m_completed_jobs.get_all();
		l.unlock();

		uncork_interface* uncork = (uncork_interface*)userdata;
		std::vector<disk_io_job*> to_delete;
		to_delete.reserve(num_jobs);

		while (j)
		{
			TORRENT_ASSERT(j->job_posted == true);
			TORRENT_ASSERT(j->callback_called == false);
//			DLOG("   callback: %s\n", job_action_name[j->action]);
			disk_io_job* next = (disk_io_job*)j->next;

#if TORRENT_USE_ASSERTS
			j->callback_called = true;
#endif
			if (j->callback) j->callback(j);
			to_delete.push_back(j);
			j = next;
		}

		if (!to_delete.empty())
			free_jobs(&to_delete[0], to_delete.size());

		// uncork all peers who received a disk event. This is
		// to coalesce all the socket writes caused by the events.
		if (uncork) uncork->do_delayed_uncork();
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void disk_io_thread::check_invariant() const
	{
	}
#endif
		
}

