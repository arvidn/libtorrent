/*

Copyright (c) 2007-2018, Arvid Norberg, Steven Siloti
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

#include "libtorrent/config.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/aux_/alloca.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/platform_util.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/disk_buffer_pool.hpp"
#include "libtorrent/disk_io_job.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/alert_manager.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/aux_/scope_end.hpp"

#include <functional>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/variant/get.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#define DEBUG_DISK_THREAD 0

namespace libtorrent {
char const* job_name(job_action_t const job);
}

#if DEBUG_DISK_THREAD
#include <cstdarg> // for va_list
#include <sstream>
#include <cstdio> // for vsnprintf

#define DLOG(...) debug_log(__VA_ARGS__)
#else
#define DLOG(...) do {} while(false)
#endif

namespace libtorrent {

#if TORRENT_USE_ASSERTS

#define TORRENT_PIECE_ASSERT(cond, piece) \
	do { if (!(cond)) { assert_print_piece(piece); assert_fail(#cond, __LINE__, __FILE__, __func__, nullptr); } } TORRENT_WHILE_0

#define TORRENT_PIECE_ASSERT_FAIL(piece) \
	do { assert_print_piece(piece); assert_fail("<unconditional>", __LINE__, __FILE__, __func__, nullptr); } TORRENT_WHILE_0

#else
#define TORRENT_PIECE_ASSERT(cond, piece) do {} TORRENT_WHILE_0
#define TORRENT_PIECE_ASSERT_FAIL(piece) do {} TORRENT_WHILE_0
#endif // TORRENT_USE_ASSERTS


	namespace {

#if DEBUG_DISK_THREAD

	void debug_log(char const* fmt, ...)
	{
		static std::mutex log_mutex;
		static const time_point start = clock_type::now();
		// map thread IDs to low numbers
		static std::unordered_map<std::thread::id, int> thread_ids;

		std::thread::id const self = std::this_thread::get_id();

		std::unique_lock<std::mutex> l(log_mutex);
		auto it = thread_ids.insert({self, int(thread_ids.size())}).first;

		va_list v;
		va_start(v, fmt);

		char usr[2048];
		int len = std::vsnprintf(usr, sizeof(usr), fmt, v);

		static bool prepend_time = true;
		if (!prepend_time)
		{
			prepend_time = (usr[len-1] == '\n');
			fputs(usr, stderr);
			return;
		}
		va_end(v);
		char buf[2300];
		int const t = int(total_milliseconds(clock_type::now() - start));
		std::snprintf(buf, sizeof(buf), "\x1b[3%dm%05d: [%d] %s\x1b[0m"
			, (it->second % 7) + 1, t, it->second, usr);
		prepend_time = (usr[len-1] == '\n');
		fputs(buf, stderr);
	}

#endif // DEBUG_DISK_THREAD

	open_mode_t file_flags_for_job(disk_io_job* j
		, bool const coalesce_buffers)
	{
		open_mode_t ret = open_mode_t{};
		if (!(j->flags & disk_interface::sequential_access)) ret |= open_mode::random_access;
		if (coalesce_buffers) ret |= open_mode::coalesce_buffers;
		return ret;
	}

	// the do_* functions can return this to indicate the disk
	// job did not complete immediately, and shouldn't be posted yet
	constexpr status_t defer_handler = static_cast<status_t>(200);

	// the job cannot be completed right now, put it back in the
	// queue and try again later
	constexpr status_t retry_job = static_cast<status_t>(201);


	struct piece_refcount_holder
	{
		explicit piece_refcount_holder(cached_piece_entry* p) : m_pe(p)
		{ ++m_pe->piece_refcount; }
		~piece_refcount_holder()
		{
			if (!m_executed)
			{
				TORRENT_PIECE_ASSERT(m_pe->piece_refcount > 0, m_pe);
				--m_pe->piece_refcount;
			}
		}
		piece_refcount_holder(piece_refcount_holder const&) = delete;
		piece_refcount_holder& operator=(piece_refcount_holder const&) = delete;
		void release()
		{
			TORRENT_ASSERT(!m_executed);
			m_executed = true;
			TORRENT_PIECE_ASSERT(m_pe->piece_refcount > 0, m_pe);
			--m_pe->piece_refcount;
		}
	private:
		cached_piece_entry* m_pe;
		bool m_executed = false;
	};

	template <typename Lock>
	struct scoped_unlocker_impl
	{
		explicit scoped_unlocker_impl(Lock& l) : m_lock(&l) { m_lock->unlock(); }
		~scoped_unlocker_impl() { if (m_lock) m_lock->lock(); }
		scoped_unlocker_impl(scoped_unlocker_impl&& rhs) noexcept : m_lock(rhs.m_lock)
		{ rhs.m_lock = nullptr; }
		scoped_unlocker_impl& operator=(scoped_unlocker_impl&& rhs) noexcept
		{
			if (&rhs == this) return *this;
			if (m_lock) m_lock->lock();
			m_lock = rhs.m_lock;
			rhs.m_lock = nullptr;
			return *this;
		}
	private:
		Lock* m_lock;
	};

	template <typename Lock>
	scoped_unlocker_impl<Lock> scoped_unlock(Lock& l)
	{ return scoped_unlocker_impl<Lock>(l); }

	} // anonymous namespace

constexpr disk_job_flags_t disk_interface::force_copy;
constexpr disk_job_flags_t disk_interface::sequential_access;
constexpr disk_job_flags_t disk_interface::volatile_read;
constexpr disk_job_flags_t disk_interface::cache_hit;

// ------- disk_io_thread ------

	disk_io_thread::disk_io_thread(io_service& ios, aux::session_settings const& sett, counters& cnt)
		: m_generic_io_jobs(*this)
		, m_generic_threads(m_generic_io_jobs, ios)
		, m_hash_io_jobs(*this)
		, m_hash_threads(m_hash_io_jobs, ios)
		, m_settings(sett)
		, m_disk_cache(ios, std::bind(&disk_io_thread::trigger_cache_trim, this))
		, m_stats_counters(cnt)
		, m_ios(ios)
	{
		settings_updated();
	}

	storage_interface* disk_io_thread::get_torrent(storage_index_t const storage)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		return m_torrents[storage].get();
	}

	std::vector<open_file_state> disk_io_thread::get_status(storage_index_t const st) const
	{
		return m_file_pool.get_status(st);
	}

	storage_holder disk_io_thread::new_torrent(storage_constructor_type sc
		, storage_params p, std::shared_ptr<void> const& owner)
	{
		std::unique_ptr<storage_interface> storage(sc(p, m_file_pool));
		storage->set_owner(owner);

		TORRENT_ASSERT(storage);
		if (m_free_slots.empty())
		{
			// make sure there's always space in here to add another free slot.
			// stopping a torrent should never fail because it needs to allocate memory
			m_free_slots.reserve(m_torrents.size() + 1);
			storage_index_t const idx = m_torrents.end_index();
			m_torrents.emplace_back(std::move(storage));
			m_torrents.back()->set_storage_index(idx);
			return storage_holder(idx, *this);
		}
		else
		{
			storage_index_t const idx = m_free_slots.back();
			m_free_slots.pop_back();
			(m_torrents[idx] = std::move(storage))->set_storage_index(idx);
			return storage_holder(idx, *this);
		}
	}

	void disk_io_thread::remove_torrent(storage_index_t const idx)
	{
		auto& pos = m_torrents[idx];
		if (pos->dec_refcount() == 0)
		{
			pos.reset();
			m_free_slots.push_back(idx);
		}
	}

#if TORRENT_USE_ASSERTS
	disk_io_thread::~disk_io_thread()
	{
		DLOG("destructing disk_io_thread\n");

		TORRENT_ASSERT(m_magic == 0x1337);
		m_magic = 0xdead;
		TORRENT_ASSERT(m_generic_io_jobs.m_queued_jobs.empty());
		TORRENT_ASSERT(m_hash_io_jobs.m_queued_jobs.empty());
	}
#endif

	void disk_io_thread::abort(bool const wait)
	{
		DLOG("disk_io_thread::abort: (%d)\n", int(wait));

		// first make sure queued jobs have been submitted
		// otherwise the queue may not get processed
		submit_jobs();

		// abuse the job mutex to make setting m_abort and checking the thread count atomic
		// see also the comment in thread_fun
		std::unique_lock<std::mutex> l(m_job_mutex);
		if (m_abort.exchange(true)) return;
		bool const no_threads = m_generic_threads.num_threads() == 0
			&& m_hash_threads.num_threads() == 0;
		// abort outstanding jobs belonging to this torrent

		DLOG("aborting hash jobs\n");
		for (auto i = m_hash_io_jobs.m_queued_jobs.iterate(); i.get(); i.next())
			i.get()->flags |= disk_io_job::aborted;
		l.unlock();

		// if there are no disk threads, we can't wait for the jobs here, because
		// we'd stall indefinitely
		if (no_threads)
		{
			abort_jobs();
		}

		DLOG("aborting thread pools\n");
		// even if there are no threads it doesn't hurt to abort the pools
		// it prevents threads from being started after an abort which is a good
		// defensive programming measure
		m_generic_threads.abort(wait);
		m_hash_threads.abort(wait);
	}

	void disk_io_thread::reclaim_blocks(span<aux::block_cache_reference> refs)
	{
		TORRENT_ASSERT(m_magic == 0x1337);

		std::unique_lock<std::mutex> l(m_cache_mutex);
		for (auto ref : refs)
		{
			auto& pos = m_torrents[ref.storage];
			storage_interface* st = pos.get();
			TORRENT_ASSERT(st != nullptr);
			m_disk_cache.reclaim_block(st, ref);
			if (st->dec_refcount() == 0)
			{
				pos.reset();
				m_free_slots.push_back(ref.storage);
			}
		}
	}

	void disk_io_thread::settings_updated()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		std::unique_lock<std::mutex> l(m_cache_mutex);
		m_disk_cache.set_settings(m_settings);
		m_file_pool.resize(m_settings.get_int(settings_pack::file_pool_size));

		int const num_threads = m_settings.get_int(settings_pack::aio_threads);
		// add one hasher thread for every three generic threads
		int const num_hash_threads = num_threads / hasher_thread_divisor;

		DLOG("set_max_threads(%d, %d)\n", num_threads - num_hash_threads
			, num_hash_threads);
		m_generic_threads.set_max_threads(num_threads - num_hash_threads);
		m_hash_threads.set_max_threads(num_hash_threads);
	}

	// flush all blocks that are below p->hash.offset, since we've
	// already hashed those blocks, they won't cause any read-back
	int disk_io_thread::try_flush_hashed(cached_piece_entry* p, int const cont_block
		, jobqueue_t& completed_jobs, std::unique_lock<std::mutex>& l)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(l.owns_lock());
		TORRENT_ASSERT(cont_block > 0);
		if (p->hash == nullptr && !p->hashing_done)
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
		int end = p->hashing_done ? int(p->blocks_in_piece) : (p->hash->offset + default_block_size - 1) / default_block_size;

		// nothing has been hashed yet, don't flush anything
		if (end == 0 && !p->need_readback) return 0;

		// the number of contiguous blocks we need to be allowed to flush
		int block_limit = std::min(cont_block, int(p->blocks_in_piece));

		// if everything has been hashed, we might as well flush everything
		// regardless of the contiguous block restriction
		if (end == int(p->blocks_in_piece)) block_limit = 1;

		if (p->need_readback)
		{
			// if this piece needs a read-back already, don't
			// try to keep it from being flushed, since we'll
			// need to read it back regardless. Flushing will
			// save blocks that can be used to "save" other
			// pieces from being flushed prematurely
			end = int(p->blocks_in_piece);
		}

		TORRENT_ASSERT(end <= p->blocks_in_piece);

		// count number of blocks that would be flushed
		int num_blocks = 0;
		for (int i = end - 1; i >= 0; --i)
			num_blocks += (p->blocks[i].dirty && !p->blocks[i].pending);

		// we did not satisfy the block_limit requirement
		// i.e. too few blocks would be flushed at this point, put it off
		if (block_limit > num_blocks) return 0;

		// if the cache line size is larger than a whole piece, hold
		// off flushing this piece until enough adjacent pieces are
		// full as well.
		int cont_pieces = int(cont_block / p->blocks_in_piece);

		// at this point, we may enforce flushing full cache stripes even when
		// they span multiple pieces. This won't necessarily work in the general
		// case, because it assumes that the piece picker will have an affinity
		// to download whole stripes at a time. This is why this setting is turned
		// off by default, flushing only one piece at a time

		if (cont_pieces <= 1 || m_settings.get_bool(settings_pack::allow_partial_disk_writes))
		{
			DLOG("try_flush_hashed: (%d) blocks_in_piece: %d end: %d\n"
				, int(p->piece), int(p->blocks_in_piece), end);

			return flush_range(p, 0, end, completed_jobs, l);
		}

		// piece range
		piece_index_t const range_start((static_cast<int>(p->piece) / cont_pieces) * cont_pieces);
		piece_index_t const range_end(std::min(static_cast<int>(range_start)
			+ cont_pieces, p->storage->files().num_pieces()));

		// look through all the pieces in this range to see if
		// they are ready to be flushed. If so, flush them all,
		// otherwise, hold off
		bool range_full = true;

		cached_piece_entry* first_piece = nullptr;
		DLOG("try_flush_hashed: multi-piece: ");
		for (piece_index_t i = range_start; i != range_end; ++i)
		{
			if (i == p->piece)
			{
				if (i == range_start) first_piece = p;
				DLOG("[%d self] ", static_cast<int>(i));
				continue;
			}
			cached_piece_entry* pe = m_disk_cache.find_piece(p->storage.get(), i);
			if (pe == nullptr)
			{
				DLOG("[%d nullptr] ", static_cast<int>(i));
				range_full = false;
				break;
			}
			if (i == range_start) first_piece = pe;

			// if this is a read-cache piece, it has already been flushed
			if (pe->cache_state != cached_piece_entry::write_lru)
			{
				DLOG("[%d read-cache] ", static_cast<int>(i));
				continue;
			}
			int hash_cursor = pe->hash ? pe->hash->offset / default_block_size : 0;

			// if the piece has all blocks, and they're all dirty, and they've
			// all been hashed, then this piece is eligible for flushing
			if (pe->num_dirty == pe->blocks_in_piece
				&& (pe->hashing_done
					|| hash_cursor == pe->blocks_in_piece
					|| m_settings.get_bool(settings_pack::disable_hash_checks)))
			{
				DLOG("[%d hash-done] ", static_cast<int>(i));
				continue;
			}

#if DEBUG_DISK_THREAD
			if (pe->num_dirty < pe->blocks_in_piece)
			{
				DLOG("[%d dirty:%d] ", static_cast<int>(i), int(pe->num_dirty));
			}
			else if (pe->hashing_done == 0 && hash_cursor < pe->blocks_in_piece)
			{
				DLOG("[%d cursor:%d] ", static_cast<int>(i), hash_cursor);
			}
			else
			{
				DLOG("[%d xx] ", static_cast<int>(i));
			}
#endif

			// TODO: in this case, the piece should probably not be flushed yet. are there
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

		cont_pieces = static_cast<int>(range_end) - static_cast<int>(range_start);
		int const blocks_to_flush = int(p->blocks_in_piece * cont_pieces);
		TORRENT_ALLOCA(iov, iovec_t, blocks_to_flush);
		TORRENT_ALLOCA(flushing, int, blocks_to_flush);
		// this is the offset into iov and flushing for each piece
		TORRENT_ALLOCA(iovec_offset, int, cont_pieces + 1);
		int iov_len = 0;
		// this is the block index each piece starts at
		int block_start = 0;
		// keep track of the pieces that have had their refcount incremented
		// so we know to decrement them later
		TORRENT_ALLOCA(refcount_pieces, int, cont_pieces);
		piece_index_t piece = range_start;
		for (int i = 0; i < cont_pieces; ++i, ++piece)
		{
			cached_piece_entry* pe;
			if (piece == p->piece) pe = p;
			else pe = m_disk_cache.find_piece(p->storage.get(), piece);
			if (pe == nullptr
				|| pe->cache_state != cached_piece_entry::write_lru)
			{
				refcount_pieces[i] = 0;
				iovec_offset[i] = iov_len;
				block_start += int(p->blocks_in_piece);
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
				, iov.subspan(iov_len), flushing.subspan(iov_len), block_start);

			block_start += int(p->blocks_in_piece);
		}
		iovec_offset[cont_pieces] = iov_len;

		// ok, now we have one (or more, but hopefully one) contiguous
		// iovec array. Now, flush it to disk

		TORRENT_ASSERT(first_piece != nullptr);

		if (iov_len == 0)
		{
			// we may not exit here if we incremented any piece refcounters
			TORRENT_ASSERT(cont_pieces == 0);
			DLOG("  iov_len: 0 cont_pieces: %d range_start: %d range_end: %d\n"
				, cont_pieces, static_cast<int>(range_start), static_cast<int>(range_end));
			return 0;
		}

		storage_error error;
		{
			// unlock while we're performing the actual disk I/O
			// then lock again
			auto unlock = scoped_unlock(l);
			flush_iovec(first_piece, iov, flushing, iov_len, error);
		}

		block_start = 0;

		piece = range_start;
		for (int i = 0; i < cont_pieces; ++i, ++piece)
		{
			cached_piece_entry* pe;
			if (piece == p->piece) pe = p;
			else pe = m_disk_cache.find_piece(p->storage.get(), piece);
			if (pe == nullptr)
			{
				DLOG("iovec_flushed: piece %d gone!\n", static_cast<int>(piece));
				TORRENT_PIECE_ASSERT(refcount_pieces[i] == 0, pe);
				block_start += int(p->blocks_in_piece);
				continue;
			}
			if (refcount_pieces[i])
			{
				TORRENT_PIECE_ASSERT(pe->piece_refcount > 0, pe);
				--pe->piece_refcount;
				m_disk_cache.maybe_free_piece(pe);
			}
			const int block_diff = iovec_offset[i + 1] - iovec_offset[i];
			iovec_flushed(pe, flushing.subspan(iovec_offset[i]).data(), block_diff
				, block_start, error, completed_jobs);
			block_start += int(p->blocks_in_piece);
		}

		// if the cache is under high pressure, we need to evict
		// the blocks we just flushed to make room for more write pieces
		int const evict = m_disk_cache.num_to_evict(0);
		if (evict > 0) m_disk_cache.try_evict_blocks(evict);

		return iov_len;
	}

	// iov and flushing are expected to be arrays to at least pe->blocks_in_piece
	// items in them. Returns the number of iovecs written to the iov array.
	// The same number of block indices are written to the flushing array. These
	// are block indices that the respective iovec structure refers to, since
	// we might not be able to flush everything as a single contiguous block,
	// the block indices indicates where the block run is broken
	// the cache needs to be locked when calling this function
	// block_base_index is the offset added to every block index written to
	// the flushing array. This can be used when building iovecs spanning
	// multiple pieces, the subsequent pieces after the first one, must have
	// their block indices start where the previous one left off
	int disk_io_thread::build_iovec(cached_piece_entry* pe, int const start, int end
		, span<iovec_t> iov, span<int> flushing, int const block_base_index)
	{
		DLOG("build_iovec: piece=%d [%d, %d)\n"
			, int(pe->piece), start, end);
		TORRENT_PIECE_ASSERT(start >= 0, pe);
		TORRENT_PIECE_ASSERT(start < end, pe);
		end = std::min(end, int(pe->blocks_in_piece));

		int const piece_size = pe->storage->files().piece_size(pe->piece);
		TORRENT_PIECE_ASSERT(piece_size > 0, pe);

		int iov_len = 0;
		// the blocks we're flushing
		int num_flushing = 0;

#if DEBUG_DISK_THREAD
		DLOG("build_iov: piece: %d [", int(pe->piece));
		for (int i = 0; i < start; ++i) DLOG(".");
#endif

		int size_left = piece_size;
		for (int i = start; i < end; ++i, size_left -= default_block_size)
		{
			TORRENT_PIECE_ASSERT(size_left > 0, pe);
			// don't flush blocks that are empty (buf == 0), not dirty
			// (read cache blocks), or pending (already being written)
			if (pe->blocks[i].buf == nullptr
				|| pe->blocks[i].pending
				|| !pe->blocks[i].dirty)
			{
				DLOG("-");
				continue;
			}

			// if we fail to lock the block, it' no longer in the cache
			bool const locked = m_disk_cache.inc_block_refcount(pe, i, block_cache::ref_flushing);

			// it should always succeed, since it's a dirty block, and
			// should never have been marked as volatile
			TORRENT_ASSERT(locked);
			TORRENT_ASSERT(pe->cache_state != cached_piece_entry::volatile_read_lru);
			TORRENT_UNUSED(locked);

			flushing[num_flushing++] = i + block_base_index;
			iov[iov_len] = { pe->blocks[i].buf, std::min(default_block_size, size_left) };
			++iov_len;
			pe->blocks[i].pending = true;

			DLOG("x");
		}
		DLOG("]\n");

		TORRENT_PIECE_ASSERT(iov_len == num_flushing, pe);
		return aux::numeric_cast<int>(iov_len);
	}

	// does the actual writing to disk
	// the cached_piece_entry is supposed to point to the
	// first piece, if the iovec spans multiple pieces
	void disk_io_thread::flush_iovec(cached_piece_entry* pe
		, span<iovec_t const> iov, span<int const> flushing
		, int const num_blocks, storage_error& error)
	{
		TORRENT_PIECE_ASSERT(!error, pe);
		TORRENT_PIECE_ASSERT(num_blocks > 0, pe);
		m_stats_counters.inc_stats_counter(counters::num_writing_threads, 1);

		time_point const start_time = clock_type::now();

#if DEBUG_DISK_THREAD
		DLOG("flush_iovec: piece: %d [ ", int(pe->piece));
		for (int i = 0; i < num_blocks; ++i)
			DLOG("%d ", flushing[i]);
		DLOG("]\n");
#endif

		open_mode_t const file_flags = m_settings.get_bool(settings_pack::coalesce_writes)
			? open_mode::coalesce_buffers : open_mode_t{};

		// issue the actual write operation
		auto iov_start = iov;
		int flushing_start = 0;
		piece_index_t const piece = pe->piece;
		int const blocks_in_piece = int(pe->blocks_in_piece);
		bool failed = false;
		for (int i = 1; i <= num_blocks; ++i)
		{
			if (i < num_blocks && flushing[i] == flushing[i - 1] + 1) continue;
			int const ret = pe->storage->writev(
				iov_start.first(i - flushing_start)
				, piece_index_t(static_cast<int>(piece) + flushing[flushing_start] / blocks_in_piece)
				, (flushing[flushing_start] % blocks_in_piece) * default_block_size
				, file_flags, error);
			if (ret < 0 || error) failed = true;
			iov_start = iov.subspan(i);
			flushing_start = i;
		}

		m_stats_counters.inc_stats_counter(counters::num_writing_threads, -1);

		{
			std::lock_guard<std::mutex> l(m_need_tick_mutex);
			if (!pe->storage->set_need_tick())
				m_need_tick.emplace_back(aux::time_now() + minutes(2), pe->storage);
		}

		if (!failed)
		{
			TORRENT_PIECE_ASSERT(!error, pe);
			std::int64_t const write_time = total_microseconds(clock_type::now() - start_time);

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
	bool disk_io_thread::iovec_flushed(cached_piece_entry* pe
		, int* flushing, int const num_blocks, int const block_offset
		, storage_error const& error
		, jobqueue_t& completed_jobs)
	{
		for (int i = 0; i < num_blocks; ++i)
			flushing[i] -= block_offset;

#if DEBUG_DISK_THREAD
		DLOG("iovec_flushed: piece: %d block_offset: %d [ "
			, static_cast<int>(pe->piece), block_offset);
		for (int i = 0; i < num_blocks; ++i)
			DLOG("%d ", flushing[i]);
		DLOG("]\n");
#endif
		if (m_disk_cache.blocks_flushed(pe, flushing, num_blocks))
			return true;

		if (error)
		{
			fail_jobs_impl(error, pe->jobs, completed_jobs);
		}
		else
		{
			disk_io_job* j = pe->jobs.get_all();
			while (j)
			{
				disk_io_job* next = j->next;
				j->next = nullptr;
				TORRENT_PIECE_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage, pe);
				TORRENT_PIECE_ASSERT(j->piece == pe->piece, pe);
				if (j->completed(pe))
				{
					j->ret = status_t::no_error;
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

		return false;
	}

	// issues write operations for blocks in the given
	// range on the given piece.
	int disk_io_thread::flush_range(cached_piece_entry* pe, int const start, int const end
		, jobqueue_t& completed_jobs, std::unique_lock<std::mutex>& l)
	{
		TORRENT_ASSERT(l.owns_lock());

		DLOG("flush_range: piece=%d [%d, %d)\n"
			, static_cast<int>(pe->piece), start, end);
		TORRENT_PIECE_ASSERT(start >= 0, pe);
		TORRENT_PIECE_ASSERT(start < end, pe);

		TORRENT_ALLOCA(iov, iovec_t, pe->blocks_in_piece);
		TORRENT_ALLOCA(flushing, int, pe->blocks_in_piece);
		int const iov_len = build_iovec(pe, start, end, iov, flushing, 0);
		if (iov_len == 0) return 0;

		TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);
#if TORRENT_USE_ASSERTS
		pe->piece_log.push_back(piece_log_t(piece_log_t::flush_range, -1));
#endif

		storage_error error;
		{
			piece_refcount_holder refcount_holder(pe);
			auto unlocker = scoped_unlock(l);

			flush_iovec(pe, iov, flushing, iov_len, error);
		}

		if (!iovec_flushed(pe, flushing.data(), iov_len, 0, error, completed_jobs))
			m_disk_cache.maybe_free_piece(pe);

		// if the cache is under high pressure, we need to evict
		// the blocks we just flushed to make room for more write pieces
		int const evict = m_disk_cache.num_to_evict(0);
		if (evict > 0) m_disk_cache.try_evict_blocks(evict);

		return iov_len;
	}

	void disk_io_thread::fail_jobs(storage_error const& e, jobqueue_t& jobs_)
	{
		jobqueue_t jobs;
		fail_jobs_impl(e, jobs_, jobs);
		if (!jobs.empty()) add_completed_jobs(jobs);
	}

	void disk_io_thread::fail_jobs_impl(storage_error const& e, jobqueue_t& src, jobqueue_t& dst)
	{
		while (!src.empty())
		{
			disk_io_job* j = src.pop_front();
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
			j->ret = status_t::fatal_disk_error;
			j->error = e;
			dst.push_back(j);
		}
	}

	void disk_io_thread::flush_piece(cached_piece_entry* pe, std::uint32_t const flags
		, jobqueue_t& completed_jobs, std::unique_lock<std::mutex>& l)
	{
		TORRENT_ASSERT(l.owns_lock());
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
			flush_range(pe, 0, INT_MAX, completed_jobs, l);

			// if we're also flushing the read cache, this piece
			// should be removed as soon as all write jobs finishes
			// otherwise it will turn into a read piece
		}

		// mark_for_eviction may erase the piece from the cache, that's
		// why we don't have the 'i' iterator referencing it at this point
		if (flags & (flush_read_cache | flush_delete_cache))
		{
			fail_jobs_impl(storage_error(boost::asio::error::operation_aborted), pe->jobs, completed_jobs);
			// we're removing the torrent, don't keep any entries around in the
			// ghost list
			m_disk_cache.mark_for_eviction(pe, block_cache::disallow_ghost);
		}
	}

	void disk_io_thread::flush_cache(storage_interface* storage, std::uint32_t const flags
		, jobqueue_t& completed_jobs, std::unique_lock<std::mutex>& l)
	{
		if (storage != nullptr)
		{
			auto const& pieces = storage->cached_pieces();
			std::vector<piece_index_t> piece_index;
			piece_index.reserve(pieces.size());
			for (auto const& p : pieces)
			{
				TORRENT_ASSERT(p.get_storage() == storage);
				if (p.get_storage() != storage) continue;
				piece_index.push_back(p.piece);
			}

			for (auto idx : piece_index)
			{
				cached_piece_entry* pe = m_disk_cache.find_piece(storage, idx);
				if (pe == nullptr) continue;
				TORRENT_PIECE_ASSERT(pe->storage.get() == storage, pe);
				flush_piece(pe, flags, completed_jobs, l);
			}
#if TORRENT_USE_ASSERTS
			TORRENT_ASSERT(l.owns_lock());
			// if the user asked to delete the cache for this storage
			// we really should not have any pieces left. This is only called
			// from disk_io_thread::do_delete, which is a fence job and should
			// have any other jobs active, i.e. there should not be any references
			// keeping pieces or blocks alive
			if ((flags & flush_delete_cache) && (flags & flush_expect_clear))
			{
				auto const& storage_pieces = storage->cached_pieces();
				for (auto const& p : storage_pieces)
				{
					cached_piece_entry* pe = m_disk_cache.find_piece(storage, p.piece);
					TORRENT_PIECE_ASSERT(pe->num_dirty == 0, pe);
				}
			}
#endif
		}
		else
		{
			auto range = m_disk_cache.all_pieces();
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
	void disk_io_thread::try_flush_write_blocks(int num, jobqueue_t& completed_jobs
		, std::unique_lock<std::mutex>& l)
	{
		DLOG("try_flush_write_blocks: %d\n", num);

		auto const range = m_disk_cache.write_lru_pieces();
		aux::vector<std::pair<std::shared_ptr<storage_interface>, piece_index_t>> pieces;
		pieces.reserve(m_disk_cache.num_write_lru_pieces());

		for (auto p = range; p.get() && num > 0; p.next())
		{
			cached_piece_entry* e = p.get();
			if (e->num_dirty == 0) continue;
			pieces.emplace_back(e->storage, e->piece);
		}

		for (auto const& p : pieces)
		{
			// TODO: instead of doing a lookup each time through the loop, save
			// cached_piece_entry pointers with piece_refcount incremented to pin them
			cached_piece_entry* pe = m_disk_cache.find_piece(p.first.get(), p.second);
			if (pe == nullptr) continue;

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

			m_disk_cache.maybe_free_piece(pe);
		}

		// when the write cache is under high pressure, it is likely
		// counter productive to actually do this, since a piece may
		// not have had its flush_hashed job run on it
		// so only do it if no other thread is currently flushing

		if (num == 0 || m_stats_counters[counters::num_writing_threads] > 0) return;

		// if we still need to flush blocks, start over and flush
		// everything in LRU order (degrade to lru cache eviction)
		for (auto const& p : pieces)
		{
			cached_piece_entry* pe = m_disk_cache.find_piece(p.first.get(), p.second);
			if (pe == nullptr) continue;
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

			num -= flush_range(pe, 0, INT_MAX, completed_jobs, l);
			--pe->piece_refcount;

			m_disk_cache.maybe_free_piece(pe);
		}
	}

	void disk_io_thread::flush_expired_write_blocks(jobqueue_t& completed_jobs
		, std::unique_lock<std::mutex>& l)
	{
		DLOG("flush_expired_write_blocks\n");

		time_point const now = aux::time_now();
		time_duration const expiration_limit = seconds(m_settings.get_int(settings_pack::cache_expiry));

#if TORRENT_USE_ASSERTS
		time_point timeout = min_time();
#endif

		TORRENT_ALLOCA(to_flush, cached_piece_entry*, 200);
		int num_flush = 0;

		for (list_iterator<cached_piece_entry> p = m_disk_cache.write_lru_pieces(); p.get(); p.next())
		{
			cached_piece_entry* e = p.get();
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
			flush_range(to_flush[i], 0, INT_MAX, completed_jobs, l);
			TORRENT_ASSERT(to_flush[i]->piece_refcount > 0);
			--to_flush[i]->piece_refcount;
			m_disk_cache.maybe_free_piece(to_flush[i]);
		}
	}

	namespace {

	using disk_io_fun_t = status_t (disk_io_thread::*)(disk_io_job*, jobqueue_t&);

	// this is a jump-table for disk I/O jobs
	std::array<disk_io_fun_t, 15> const job_functions =
	{{
		&disk_io_thread::do_read,
		&disk_io_thread::do_write,
		&disk_io_thread::do_hash,
		&disk_io_thread::do_move_storage,
		&disk_io_thread::do_release_files,
		&disk_io_thread::do_delete_files,
		&disk_io_thread::do_check_fastresume,
		&disk_io_thread::do_rename_file,
		&disk_io_thread::do_stop_torrent,
		&disk_io_thread::do_flush_piece,
		&disk_io_thread::do_flush_hashed,
		&disk_io_thread::do_flush_storage,
		&disk_io_thread::do_trim_cache,
		&disk_io_thread::do_file_priority,
		&disk_io_thread::do_clear_piece
	}};

	} // anonymous namespace

	// evict and/or flush blocks if we're exceeding the cache size
	// or used to exceed it and haven't dropped below the low watermark yet
	// the low watermark is dynamic, based on the number of peers waiting
	// on buffers to free up. The more waiters, the lower the low watermark
	// is. Because of this, the target for flushing jobs may have dropped
	// below the number of blocks we flushed by the time we're done flushing
	// that's why we need to call this fairly often. Both before and after
	// a disk job is executed
	void disk_io_thread::check_cache_level(std::unique_lock<std::mutex>& l, jobqueue_t& completed_jobs)
	{
		// when the read cache is disabled, always try to evict all read cache
		// blocks
		if (!m_settings.get_bool(settings_pack::use_read_cache))
		{
			int const evict = m_disk_cache.read_cache_size();
			m_disk_cache.try_evict_blocks(evict);
		}

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

	void disk_io_thread::perform_job(disk_io_job* j, jobqueue_t& completed_jobs)
	{
		TORRENT_ASSERT(j->next == nullptr);
		TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

#if DEBUG_DISK_THREAD
		{
			std::unique_lock<std::mutex> l(m_cache_mutex);

			DLOG("perform_job job: %s ( %s) piece: %d offset: %d outstanding: %d\n"
				, job_name(j->action)
				, (j->flags & disk_io_job::fence) ? "fence ": ""
				, static_cast<int>(j->piece), j->d.io.offset
				, j->storage ? j->storage->num_outstanding_jobs() : -1);
		}
#endif

		std::shared_ptr<storage_interface> storage = j->storage;

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		if (j->storage)
		{
			std::unique_lock<std::mutex> l(m_cache_mutex);
			auto const& pieces = j->storage->cached_pieces();
			for (auto const& p : pieces)
				TORRENT_ASSERT(p.storage == j->storage);
		}
#endif
		// TODO: 4 instead of doing this. pass in the settings to each storage_interface
		// call. Each disk thread could hold its most recent understanding of the settings
		// in a shared_ptr, and update it every time it wakes up from a job. That way
		// each access to the settings won't require a std::mutex to be held.
		if (storage && storage->m_settings == nullptr)
			storage->m_settings = &m_settings;

		TORRENT_ASSERT(static_cast<int>(j->action) < int(job_functions.size()));

		m_stats_counters.inc_stats_counter(counters::num_running_disk_jobs, 1);

		// call disk function
		// TODO: in the future, propagate exceptions back to the handlers
		status_t ret = status_t::no_error;
		try
		{
			int const idx = static_cast<int>(j->action);
			ret = (this->*(job_functions[static_cast<std::size_t>(idx)]))(j, completed_jobs);
		}
		catch (boost::system::system_error const& err)
		{
			ret = status_t::fatal_disk_error;
			j->error.ec = err.code();
			j->error.operation = operation_t::exception;
		}
		catch (std::bad_alloc const&)
		{
			ret = status_t::fatal_disk_error;
			j->error.ec = errors::no_memory;
			j->error.operation = operation_t::exception;
		}
		catch (std::exception const&)
		{
			ret = status_t::fatal_disk_error;
			j->error.ec = boost::asio::error::fault;
			j->error.operation = operation_t::exception;
		}

		// note that -2 errors are OK
		TORRENT_ASSERT(ret != status_t::fatal_disk_error
			|| (j->error.ec && j->error.operation != operation_t::unknown));

		m_stats_counters.inc_stats_counter(counters::num_running_disk_jobs, -1);

		std::unique_lock<std::mutex> l(m_cache_mutex);
		if (m_cache_check_state == cache_check_idle)
		{
			m_cache_check_state = cache_check_active;
			while (m_cache_check_state != cache_check_idle)
			{
				check_cache_level(l, completed_jobs);
				TORRENT_ASSERT(l.owns_lock());
				--m_cache_check_state;
			}
		}
		else
		{
			m_cache_check_state = cache_check_reinvoke;
		}
		l.unlock();

		if (ret == retry_job)
		{
			job_queue& q = queue_for_job(j);

			std::unique_lock<std::mutex> l2(m_job_mutex);
			// to avoid busy looping here, give up
			// our quanta in case there aren't any other
			// jobs to run in between

			// TODO: a potentially more efficient solution would be to have a special
			// queue for retry jobs, that's only ever run when a job completes, in
			// any thread. It would only work if counters::num_running_disk_jobs > 0

			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

			bool const need_sleep = q.m_queued_jobs.empty();
			q.m_queued_jobs.push_back(j);
			l2.unlock();
			if (need_sleep) std::this_thread::yield();
			return;
		}

		if (ret == defer_handler) return;

		j->ret = ret;

		completed_jobs.push_back(j);
	}

	status_t disk_io_thread::do_uncached_read(disk_io_job* j)
	{
		j->argument = disk_buffer_holder(*this, m_disk_cache.allocate_buffer("send buffer"), 0x4000);
		auto& buffer = boost::get<disk_buffer_holder>(j->argument);
		if (buffer.get() == nullptr)
		{
			j->error.ec = error::no_memory;
			j->error.operation = operation_t::alloc_cache_piece;
			return status_t::fatal_disk_error;
		}

		time_point const start_time = clock_type::now();

		open_mode_t const file_flags = file_flags_for_job(j
			, m_settings.get_bool(settings_pack::coalesce_reads));
		iovec_t b = {buffer.get(), j->d.io.buffer_size};

		int const ret = j->storage->readv(b
			, j->piece, j->d.io.offset, file_flags, j->error);

		TORRENT_ASSERT(ret >= 0 || (j->error.ec && j->error.operation != operation_t::unknown));
		TORRENT_UNUSED(ret);

		if (!j->error.ec)
		{
			std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

			m_stats_counters.inc_stats_counter(counters::num_read_back);
			m_stats_counters.inc_stats_counter(counters::num_blocks_read);
			m_stats_counters.inc_stats_counter(counters::num_read_ops);
			m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
			m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
		}
		return status_t::no_error;
	}

	status_t disk_io_thread::do_read(disk_io_job* j, jobqueue_t& completed_jobs)
	{
		int const piece_size = j->storage->files().piece_size(j->piece);
		int const blocks_in_piece = (piece_size + default_block_size - 1) / default_block_size;
		int const iov_len = m_disk_cache.pad_job(j, blocks_in_piece
			, m_settings.get_int(settings_pack::read_cache_line_size));

		TORRENT_ALLOCA(iov, iovec_t, iov_len);

		std::unique_lock<std::mutex> l(m_cache_mutex);

		int const evict = m_disk_cache.num_to_evict(iov_len);
		if (evict > 0) m_disk_cache.try_evict_blocks(evict);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe == nullptr)
		{
			l.unlock();
			return do_uncached_read(j);
		}
		TORRENT_PIECE_ASSERT(pe->outstanding_read == 1, pe);

		l.unlock();

		// then we'll actually allocate the buffers
		int ret = m_disk_cache.allocate_iovec(iov);

		if (ret < 0)
		{
			status_t const s = do_uncached_read(j);

			std::unique_lock<std::mutex> l2(m_cache_mutex);
			pe = m_disk_cache.find_piece(j);
			if (pe != nullptr) maybe_issue_queued_read_jobs(pe, completed_jobs);
			return s;
		}

		// free buffers at the end of the scope
		auto iov_dealloc = aux::scope_end([&]{ m_disk_cache.free_iovec(iov); });

		// this is the offset that's aligned to block boundaries
		int const adjusted_offset = aux::numeric_cast<int>(j->d.io.offset & ~(default_block_size - 1));

		// if this is the last piece, adjust the size of the
		// last buffer to match up
		iov[iov_len - 1] = iov[iov_len - 1].first(
			std::min(piece_size - adjusted_offset - (iov_len - 1)
			* default_block_size, default_block_size));
		TORRENT_ASSERT(iov[iov_len - 1].size() > 0);

		// at this point, all the buffers are allocated and iov is initialized
		// and the blocks have their refcounters incremented, so no other thread
		// can remove them. We can now release the cache std::mutex and dive into the
		// disk operations.

		open_mode_t const file_flags = file_flags_for_job(j
			, m_settings.get_bool(settings_pack::coalesce_reads));
		time_point const start_time = clock_type::now();

		ret = j->storage->readv(iov
			, j->piece, int(adjusted_offset), file_flags, j->error);

		TORRENT_ASSERT(ret >= 0 || (j->error.ec && j->error.operation != operation_t::unknown));

		if (!j->error.ec)
		{
			std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

			m_stats_counters.inc_stats_counter(counters::num_blocks_read, iov_len);
			m_stats_counters.inc_stats_counter(counters::num_read_ops);
			m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
			m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
		}

		l.lock();

		if (ret < 0)
		{
			pe = m_disk_cache.find_piece(j);
			if (pe == nullptr)
			{
				// the piece is supposed to be allocated when the
				// disk job is allocated
				TORRENT_ASSERT_FAIL();
				return status_t::fatal_disk_error;
			}
			TORRENT_PIECE_ASSERT(pe->outstanding_read == 1, pe);

			if (!pe->read_jobs.empty())
				fail_jobs_impl(j->error, pe->read_jobs, completed_jobs);
			TORRENT_PIECE_ASSERT(pe->read_jobs.empty(), pe);
			pe->outstanding_read = 0;
#if TORRENT_USE_ASSERTS
			pe->piece_log.emplace_back(piece_log_t::clear_outstanding_jobs);
#endif
			m_disk_cache.maybe_free_piece(pe);
			return status_t::fatal_disk_error;
		}

		int block = j->d.io.offset / default_block_size;
#if TORRENT_USE_ASSERTS
		pe->piece_log.push_back(piece_log_t(j->action, block));
#endif

		// we want to hold on to the iov now
		iov_dealloc.disarm();

		// as soon we insert the blocks they may be evicted
		// (if using purgeable memory). In order to prevent that
		// until we can read from them, increment the refcounts
		m_disk_cache.insert_blocks(pe, block, iov, j, block_cache::blocks_inc_refcount);

		TORRENT_ASSERT(pe->blocks[block].buf);

		int const tmp = m_disk_cache.try_read(j, *this, true);

		// This should always succeed because we just checked to see there is a
		// buffer for this block
		TORRENT_ASSERT(tmp >= 0);
		TORRENT_UNUSED(tmp);

		maybe_issue_queued_read_jobs(pe, completed_jobs);

		for (int i = 0; i < iov_len; ++i, ++block)
			m_disk_cache.dec_block_refcount(pe, block, block_cache::ref_reading);

		return status_t::no_error;
	}

	void disk_io_thread::maybe_issue_queued_read_jobs(cached_piece_entry* pe
		, jobqueue_t& completed_jobs)
	{
		TORRENT_PIECE_ASSERT(pe->outstanding_read == 1, pe);

		// if we're shutting down, just cancel the jobs
		if (m_abort)
		{
			fail_jobs_impl(storage_error(boost::asio::error::operation_aborted)
				, pe->read_jobs, completed_jobs);
			TORRENT_PIECE_ASSERT(pe->read_jobs.empty(), pe);
			pe->outstanding_read = 0;
#if TORRENT_USE_ASSERTS
			pe->piece_log.emplace_back(piece_log_t::clear_outstanding_jobs);
#endif
			m_disk_cache.maybe_free_piece(pe);
			return;
		}

		// while we were reading, there may have been a few jobs
		// that got queued up also wanting to read from this piece.
		// Any job that is a cache hit now, complete it immediately.
		// Then, issue the first non-cache-hit job. Once it complete
		// it will keep working off this list
		jobqueue_t stalled_jobs;
		pe->read_jobs.swap(stalled_jobs);

		// the next job to issue (i.e. this is a cache-miss)
		disk_io_job* next_job = nullptr;

		while (!stalled_jobs.empty())
		{
			disk_io_job* j = stalled_jobs.pop_front();
			TORRENT_ASSERT(j->flags & disk_io_job::in_progress);

			int ret = m_disk_cache.try_read(j, *this);
			if (ret >= 0)
			{
				// cache-hit
				m_stats_counters.inc_stats_counter(counters::num_blocks_cache_hits);
				DLOG("do_read: cache hit\n");
				j->flags |= disk_interface::cache_hit;
				j->ret = status_t::no_error;
				completed_jobs.push_back(j);
			}
			else if (ret == -2)
			{
				// error
				j->ret = status_t::fatal_disk_error;
				completed_jobs.push_back(j);
			}
			else
			{
				// cache-miss, issue the first one
				// put back the rest
				if (next_job == nullptr)
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
			add_job(next_job, false);
		}
		else
		{
			TORRENT_PIECE_ASSERT(pe->read_jobs.empty(), pe);
			pe->outstanding_read = 0;
#if TORRENT_USE_ASSERTS
			pe->piece_log.emplace_back(piece_log_t::clear_outstanding_jobs);
#endif
			m_disk_cache.maybe_free_piece(pe);
		}
	}

	status_t disk_io_thread::do_uncached_write(disk_io_job* j)
	{
		time_point const start_time = clock_type::now();
		auto buffer = std::move(boost::get<disk_buffer_holder>(j->argument));

		iovec_t const b = { buffer.get(), j->d.io.buffer_size};
		open_mode_t const file_flags = file_flags_for_job(j
			, m_settings.get_bool(settings_pack::coalesce_writes));

		m_stats_counters.inc_stats_counter(counters::num_writing_threads, 1);

		// the actual write operation
		int const ret = j->storage->writev(b
			, j->piece, j->d.io.offset, file_flags, j->error);

		TORRENT_ASSERT(ret >= 0 || (j->error.ec && j->error.operation != operation_t::unknown));

		m_stats_counters.inc_stats_counter(counters::num_writing_threads, -1);

		if (!j->error.ec)
		{
			std::int64_t const write_time = total_microseconds(clock_type::now() - start_time);

			m_stats_counters.inc_stats_counter(counters::num_blocks_written);
			m_stats_counters.inc_stats_counter(counters::num_write_ops);
			m_stats_counters.inc_stats_counter(counters::disk_write_time, write_time);
			m_stats_counters.inc_stats_counter(counters::disk_job_time, write_time);
		}

		{
			std::lock_guard<std::mutex> l(m_need_tick_mutex);
			if (!j->storage->set_need_tick())
				m_need_tick.emplace_back(aux::time_now() + minutes(2), j->storage);
		}

		return ret != j->d.io.buffer_size
			? status_t::fatal_disk_error : status_t::no_error;
	}

	status_t disk_io_thread::do_write(disk_io_job* j, jobqueue_t& completed_jobs)
	{
		TORRENT_ASSERT(j->d.io.buffer_size <= default_block_size);

		std::unique_lock<std::mutex> l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe != nullptr && pe->hashing_done)
		{
#if TORRENT_USE_ASSERTS
			print_piece_log(pe->piece_log);
#endif
			TORRENT_ASSERT(pe->blocks[j->d.io.offset / 16 / 1024].buf
				!= boost::get<disk_buffer_holder>(j->argument).get());
			TORRENT_ASSERT(pe->blocks[j->d.io.offset / 16 / 1024].buf != nullptr);
			j->error.ec = error::operation_aborted;
			j->error.operation = operation_t::file_write;
			return status_t::fatal_disk_error;
		}

		pe = m_disk_cache.add_dirty_block(j
			, !m_settings.get_bool(settings_pack::disable_hash_checks));

		if (pe)
		{
#if TORRENT_USE_ASSERTS
			pe->piece_log.push_back(piece_log_t(j->action, j->d.io.offset / 0x4000));
#endif

			if (!pe->hashing_done
				&& pe->hash == nullptr
				&& !m_settings.get_bool(settings_pack::disable_hash_checks))
			{
				pe->hash.reset(new partial_hash);
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
			try_flush_hashed(pe, m_settings.get_int(
				settings_pack::write_cache_line_size), completed_jobs, l);

			--pe->piece_refcount;
			m_disk_cache.maybe_free_piece(pe);

			return defer_handler;
		}

		// ok, we should just perform this job right now.
		return do_uncached_write(j);
	}

	void disk_io_thread::async_read(storage_index_t storage, peer_request const& r
		, std::function<void(disk_buffer_holder block, disk_job_flags_t const flags
		, storage_error const& se)> handler, disk_job_flags_t const flags)
	{
		TORRENT_ASSERT(r.length <= default_block_size);

		DLOG("async_read piece: %d block: %d\n", static_cast<int>(r.piece)
			, r.start / default_block_size);

		disk_io_job* j = allocate_job(job_action_t::read);
		j->storage = m_torrents[storage]->shared_from_this();
		j->piece = r.piece;
		j->d.io.offset = r.start;
		j->d.io.buffer_size = std::uint16_t(r.length);
		j->argument = disk_buffer_holder(*this, nullptr, 0);
		j->flags = flags;
		j->callback = std::move(handler);

		TORRENT_ASSERT(static_cast<int>(r.piece) * static_cast<std::int64_t>(j->storage->files().piece_length())
			+ r.start + r.length <= j->storage->files().total_size());

		std::unique_lock<std::mutex> l(m_cache_mutex);
		int const ret = prep_read_job_impl(j);
		l.unlock();

		switch (ret)
		{
			case 0:
				j->call_callback();
				free_job(j);
				break;
			case 1:
				add_job(j);
				break;
		}
	}

	// this function checks to see if a read job is a cache hit,
	// and if it doesn't have a piece allocated, it allocates
	// one and it sets outstanding_read flag and possibly queues
	// up the job in the piece read job list
	// the cache std::mutex must be held when calling this
	//
	// returns 0 if the job succeeded immediately
	// 1 if it needs to be added to the job queue
	// 2 if it was deferred and will be performed later (no need to
	//   add it to the queue)
	int disk_io_thread::prep_read_job_impl(disk_io_job* j, bool const check_fence)
	{
		TORRENT_ASSERT(j->action == job_action_t::read);

		int const ret = m_disk_cache.try_read(j, *this);
		if (ret >= 0)
		{
			m_stats_counters.inc_stats_counter(counters::num_blocks_cache_hits);
			DLOG("do_read: cache hit\n");
			j->flags |= disk_interface::cache_hit;
			j->ret = status_t::no_error;
			return 0;
		}
		else if (ret == -2)
		{
			j->error.ec = error::no_memory;
			j->error.operation = operation_t::alloc_cache_piece;
			j->ret = status_t::fatal_disk_error;
			return 0;
		}

		if (check_fence && j->storage->is_blocked(j))
		{
			// this means the job was queued up inside storage
			m_stats_counters.inc_stats_counter(counters::blocked_disk_jobs);
			DLOG("blocked job: %s (torrent: %d total: %d)\n"
				, job_name(j->action), j->storage ? j->storage->num_blocked() : 0
				, int(m_stats_counters[counters::blocked_disk_jobs]));
			return 2;
		}

		if (!m_settings.get_bool(settings_pack::use_read_cache)
			|| m_settings.get_int(settings_pack::cache_size) == 0)
		{
			// if the read cache is disabled then we can skip going through the cache
			// but only if there is no existing piece entry. Otherwise there may be a
			// partial hit on one-or-more dirty buffers so we must use the cache
			// to avoid reading bogus data from storage
			if (m_disk_cache.find_piece(j) == nullptr)
				return 1;
		}

		cached_piece_entry* pe = m_disk_cache.allocate_piece(j, cached_piece_entry::read_lru1);

		if (pe == nullptr)
		{
			j->ret = status_t::fatal_disk_error;
			j->error.ec = error::no_memory;
			j->error.operation = operation_t::file_read;
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

		return 1;
	}

	bool disk_io_thread::async_write(storage_index_t const storage, peer_request const& r
		, char const* buf, std::shared_ptr<disk_observer> o
		, std::function<void(storage_error const&)> handler
		, disk_job_flags_t const flags)
	{
		TORRENT_ASSERT(r.length <= default_block_size);
		TORRENT_ASSERT(r.length <= 16 * 1024);
		TORRENT_ASSERT(buf != nullptr);

		bool exceeded = false;
		disk_buffer_holder buffer(*this, m_disk_cache.allocate_buffer(exceeded, o, "receive buffer"), 0x4000);
		if (!buffer) aux::throw_ex<std::bad_alloc>();
		std::memcpy(buffer.get(), buf, aux::numeric_cast<std::size_t>(r.length));

		disk_io_job* j = allocate_job(job_action_t::write);
		j->storage = m_torrents[storage]->shared_from_this();
		j->piece = r.piece;
		j->d.io.offset = r.start;
		j->d.io.buffer_size = std::uint16_t(r.length);
		j->argument = std::move(buffer);
		j->callback = std::move(handler);
		j->flags = flags;

#if TORRENT_USE_ASSERTS
		std::unique_lock<std::mutex> l3_(m_cache_mutex);
		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe)
		{
			// we should never add a new dirty block to a piece
			// whose hash we have calculated. The piece needs
			// to be cleared first, (async_clear_piece).
			TORRENT_ASSERT(pe->hashing_done == 0);

			TORRENT_ASSERT(pe->blocks[r.start / 0x4000].refcount == 0 || pe->blocks[r.start / 0x4000].buf == nullptr);
		}
		l3_.unlock();
#endif

#if TORRENT_USE_ASSERTS && defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		std::unique_lock<std::mutex> l2_(m_cache_mutex);
		auto range = m_disk_cache.all_pieces();
		for (auto i = range.first; i != range.second; ++i)
		{
			cached_piece_entry const& p = *i;
			int const piece_size = p.storage->files().piece_size(p.piece);
			int const blocks_in_piece = (piece_size + default_block_size - 1) / default_block_size;
			for (int k = 0; k < blocks_in_piece; ++k)
				TORRENT_PIECE_ASSERT(p.blocks[k].buf != boost::get<disk_buffer_holder>(j->argument).get(), &p);
		}
		l2_.unlock();
#endif

		TORRENT_ASSERT((r.start % default_block_size) == 0);

		if (j->storage->is_blocked(j))
		{
			// this means the job was queued up inside storage
			m_stats_counters.inc_stats_counter(counters::blocked_disk_jobs);
			DLOG("blocked job: %s (torrent: %d total: %d)\n"
				, job_name(j->action), j->storage ? j->storage->num_blocked() : 0
				, int(m_stats_counters[counters::blocked_disk_jobs]));
			return exceeded;
		}

		std::unique_lock<std::mutex> l(m_cache_mutex);
		// if we succeed in adding the block to the cache, the job will
		// be added along with it. we may not free j if so
		cached_piece_entry* dpe = m_disk_cache.add_dirty_block(j
			, !m_settings.get_bool(settings_pack::disable_hash_checks));

		if (dpe != nullptr)
		{
			if (dpe->outstanding_flush == 0)
			{
				dpe->outstanding_flush = 1;
				l.unlock();

				// the block and write job were successfully inserted
				// into the cache. Now, see if we should trigger a flush
				j = allocate_job(job_action_t::flush_hashed);
				j->storage = m_torrents[storage]->shared_from_this();
				j->piece = r.piece;
				j->flags = flags;
				add_job(j);
			}

			// if we added the block (regardless of whether we also
			// issued a flush job or not), we're done.
			return exceeded;
		}
		l.unlock();

		add_job(j);
		return exceeded;
	}

	void disk_io_thread::async_hash(storage_index_t const storage
		, piece_index_t const piece, disk_job_flags_t const flags
		, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler)
	{
		disk_io_job* j = allocate_job(job_action_t::hash);
		j->storage = m_torrents[storage]->shared_from_this();
		j->piece = piece;
		j->callback = std::move(handler);
		j->flags = flags;

		int const piece_size = j->storage->files().piece_size(piece);

		// first check to see if the hashing is already done
		std::unique_lock<std::mutex> l(m_cache_mutex);
		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe != nullptr && !pe->hashing && pe->hash && pe->hash->offset == piece_size)
		{
			j->d.piece_hash = pe->hash->h.final();

			pe->hash.reset();

			if (pe->cache_state != cached_piece_entry::volatile_read_lru)
				pe->hashing_done = 1;

#if TORRENT_USE_ASSERTS
			++pe->hash_passes;
#endif

			l.unlock();
			j->call_callback();
			free_job(j);
			return;
		}
		l.unlock();
		add_job(j);
	}

	void disk_io_thread::async_move_storage(storage_index_t const storage
		, std::string p, move_flags_t const flags
		, std::function<void(status_t, std::string const&, storage_error const&)> handler)
	{
		disk_io_job* j = allocate_job(job_action_t::move_storage);
		j->storage = m_torrents[storage]->shared_from_this();
		j->argument = std::move(p);
		j->callback = std::move(handler);
		j->move_flags = flags;

		add_fence_job(j);
	}

	void disk_io_thread::async_release_files(storage_index_t const storage
		, std::function<void()> handler)
	{
		disk_io_job* j = allocate_job(job_action_t::release_files);
		j->storage = m_torrents[storage]->shared_from_this();
		j->callback = std::move(handler);

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		{
			std::unique_lock<std::mutex> l(m_cache_mutex);
			auto const& pieces = j->storage->cached_pieces();
			for (auto const& p : pieces)
				TORRENT_ASSERT(p.storage == j->storage);
		}
#endif
		add_fence_job(j);
	}

	void disk_io_thread::abort_hash_jobs(storage_index_t const storage)
	{
		// abort outstanding hash jobs belonging to this torrent
		std::unique_lock<std::mutex> l(m_job_mutex);

		std::shared_ptr<storage_interface> st
			= m_torrents[storage]->shared_from_this();
		// hash jobs
		for (auto i = m_hash_io_jobs.m_queued_jobs.iterate(); i.get(); i.next())
		{
			disk_io_job *j = i.get();
			if (j->storage != st) continue;
			// only cancel volatile-read jobs. This means only full checking
			// jobs. These jobs are likely to have a pretty deep queue and
			// really gain from being cancelled. They can also be restarted
			// easily.
			if (!(j->flags & disk_interface::volatile_read)) continue;
			j->flags |= disk_io_job::aborted;
		}
	}

	void disk_io_thread::async_delete_files(storage_index_t const storage
		, remove_flags_t const options
		, std::function<void(storage_error const&)> handler)
	{
		abort_hash_jobs(storage);
		disk_io_job* j = allocate_job(job_action_t::delete_files);
		j->storage = m_torrents[storage]->shared_from_this();
		j->callback = std::move(handler);
		j->argument = options;
		add_fence_job(j);
	}

	void disk_io_thread::async_check_files(storage_index_t const storage
		, add_torrent_params const* resume_data
		, aux::vector<std::string, file_index_t>& links
		, std::function<void(status_t, storage_error const&)> handler)
	{
		auto links_vector = new aux::vector<std::string, file_index_t>();
		links_vector->swap(links);

		disk_io_job* j = allocate_job(job_action_t::check_fastresume);
		j->storage = m_torrents[storage]->shared_from_this();
		j->argument = resume_data;
		j->d.links = links_vector;
		j->callback = std::move(handler);

		add_fence_job(j);
	}

	void disk_io_thread::async_rename_file(storage_index_t const storage
		, file_index_t const index, std::string name
		, std::function<void(std::string const&, file_index_t, storage_error const&)> handler)
	{
		disk_io_job* j = allocate_job(job_action_t::rename_file);
		j->storage = m_torrents[storage]->shared_from_this();
		j->file_index = index;
		j->argument = std::move(name);
		j->callback = std::move(handler);
		add_fence_job(j);
	}

	void disk_io_thread::async_stop_torrent(storage_index_t const storage
		, std::function<void()> handler)
	{
		abort_hash_jobs(storage);
		disk_io_job* j = allocate_job(job_action_t::stop_torrent);
		j->storage = m_torrents[storage]->shared_from_this();
		j->callback = std::move(handler);
		add_fence_job(j);
	}

	void disk_io_thread::async_flush_piece(storage_index_t const storage
		, piece_index_t const piece
		, std::function<void()> handler)
	{
		disk_io_job* j = allocate_job(job_action_t::flush_piece);
		j->storage = m_torrents[storage]->shared_from_this();
		j->piece = piece;
		j->callback = std::move(handler);

		if (m_abort)
		{
			j->error.ec = boost::asio::error::operation_aborted;
			j->call_callback();
			free_job(j);
			return;
		}

		add_job(j);
	}

	void disk_io_thread::async_set_file_priority(storage_index_t const storage
		, aux::vector<download_priority_t, file_index_t> prios
		, std::function<void(storage_error const&, aux::vector<download_priority_t, file_index_t>)> handler)
	{
		disk_io_job* j = allocate_job(job_action_t::file_priority);
		j->storage = m_torrents[storage]->shared_from_this();
		j->argument = std::move(prios);
		j->callback = std::move(handler);

		add_fence_job(j);
	}

	void disk_io_thread::async_clear_piece(storage_index_t const storage
		, piece_index_t const index, std::function<void(piece_index_t)> handler)
	{
		disk_io_job* j = allocate_job(job_action_t::clear_piece);
		j->storage = m_torrents[storage]->shared_from_this();
		j->piece = index;
		j->callback = std::move(handler);

		// regular jobs are not guaranteed to be executed in-order
		// since clear piece must guarantee that all write jobs that
		// have been issued finish before the clear piece job completes

		// TODO: this is potentially very expensive. One way to solve
		// it would be to have a fence for just this one piece.
		add_fence_job(j);
	}

	void disk_io_thread::clear_piece(storage_index_t const storage
		, piece_index_t const index)
	{

		storage_interface* st = m_torrents[storage].get();
		std::unique_lock<std::mutex> l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(st, index);
		if (pe == nullptr) return;
		TORRENT_PIECE_ASSERT(pe->hashing == false, pe);
		pe->hashing_done = 0;
		pe->hash.reset();

		// evict_piece returns true if the piece was in fact
		// evicted. A piece may fail to be evicted if there
		// are still outstanding operations on it, which should
		// never be the case when this function is used
		// in fact, no jobs should really be hung on this piece
		// at this point
		jobqueue_t jobs;
		bool const ok = m_disk_cache.evict_piece(pe, jobs, block_cache::allow_ghost);
		TORRENT_PIECE_ASSERT(ok, pe);
		TORRENT_UNUSED(ok);
		fail_jobs(storage_error(boost::asio::error::operation_aborted), jobs);
	}

	void disk_io_thread::kick_hasher(cached_piece_entry* pe, std::unique_lock<std::mutex>& l)
	{
		if (!pe->hash) return;
		if (pe->hashing) return;

		int const piece_size = pe->storage->files().piece_size(pe->piece);
		partial_hash* ph = pe->hash.get();

		// are we already done?
		if (ph->offset >= piece_size) return;

		int const cursor = ph->offset / default_block_size;
		int end = cursor;
		TORRENT_PIECE_ASSERT(ph->offset % default_block_size == 0, pe);

		for (int i = cursor; i < pe->blocks_in_piece; ++i)
		{
			cached_block_entry& bl = pe->blocks[i];
			if (bl.buf == nullptr) break;

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

		// save a local copy of offset to avoid concurrent access
		int offset = ph->offset;
#if TORRENT_USE_ASSERTS
		int old_offset = offset;
#endif

		l.unlock();

		time_point const start_time = clock_type::now();

		for (int i = cursor; i < end; ++i)
		{
			cached_block_entry& bl = pe->blocks[i];
			int const size = std::min(default_block_size, piece_size - offset);
			ph->h.update(bl.buf, size);
			offset += size;
		}

		std::int64_t const hash_time = total_microseconds(clock_type::now() - start_time);

		l.lock();

		TORRENT_ASSERT(old_offset == ph->offset);
		ph->offset = offset;

		TORRENT_PIECE_ASSERT(pe->hashing, pe);
		TORRENT_PIECE_ASSERT(pe->hash, pe);

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
		disk_io_job* j = pe->jobs.get_all();
		jobqueue_t hash_jobs;
		while (j)
		{
			TORRENT_PIECE_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage, pe);
			disk_io_job* next = j->next;
			j->next = nullptr;
			TORRENT_PIECE_ASSERT(j->piece == pe->piece, pe);
			if (j->action == job_action_t::hash) hash_jobs.push_back(j);
			else pe->jobs.push_back(j);
			j = next;
		}
		if (!hash_jobs.empty())
		{
			sha1_hash const result = pe->hash->h.final();

			for (auto i = hash_jobs.iterate(); i.get(); i.next())
			{
				disk_io_job* hj = i.get();
				hj->d.piece_hash = result;
				hj->ret = status_t::no_error;
			}

			pe->hash.reset();
			if (pe->cache_state != cached_piece_entry::volatile_read_lru)
				pe->hashing_done = 1;
#if TORRENT_USE_ASSERTS
			++pe->hash_passes;
#endif
			add_completed_jobs(hash_jobs);
		}
	}

	status_t disk_io_thread::do_uncached_hash(disk_io_job* j)
	{
		// we're not using a cache. This is the simple path
		// just read straight from the file
		TORRENT_ASSERT(m_magic == 0x1337);

		int const piece_size = j->storage->files().piece_size(j->piece);
		int const blocks_in_piece = (piece_size + default_block_size - 1) / default_block_size;
		open_mode_t const file_flags = file_flags_for_job(j
			, m_settings.get_bool(settings_pack::coalesce_reads));

		iovec_t iov = { m_disk_cache.allocate_buffer("hashing")
			, static_cast<std::size_t>(default_block_size) };

		// free at the end of the scope
		auto iov_dealloc = aux::scope_end([&]{ m_disk_cache.free_buffer(iov.data()); });

		hasher h;
		int ret = 0;
		int offset = 0;
		for (int i = 0; i < blocks_in_piece; ++i)
		{
			DLOG("do_hash: (uncached) reading (piece: %d block: %d)\n"
				, int(j->piece), i);

			time_point const start_time = clock_type::now();

			iov = iov.first(std::min(default_block_size, piece_size - offset));
			ret = j->storage->readv(iov, j->piece, offset, file_flags, j->error);
			if (ret <= 0) break;
			iov = iov.first(ret);

			if (!j->error.ec)
			{
				std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

				m_stats_counters.inc_stats_counter(counters::num_blocks_read);
				m_stats_counters.inc_stats_counter(counters::num_read_ops);
				m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
				m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
			}

			offset += default_block_size;
			h.update(iov);
		}

		j->d.piece_hash = h.final();
		return ret >= 0 ? status_t::no_error : status_t::fatal_disk_error;
	}

	status_t disk_io_thread::do_hash(disk_io_job* j, jobqueue_t& /* completed_jobs */ )
	{
		if (m_settings.get_bool(settings_pack::disable_hash_checks))
			return status_t::no_error;

		int const piece_size = j->storage->files().piece_size(j->piece);
		open_mode_t const file_flags = file_flags_for_job(j
			, m_settings.get_bool(settings_pack::coalesce_reads));

		std::unique_lock<std::mutex> l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe != nullptr)
		{
			TORRENT_ASSERT(pe->in_use);
#if TORRENT_USE_ASSERTS
			pe->piece_log.push_back(piece_log_t(j->action));
#endif
			m_disk_cache.cache_hit(pe, j->d.io.offset / default_block_size
				, bool(j->flags & disk_interface::volatile_read));

			TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);
			{
				piece_refcount_holder h(pe);
				kick_hasher(pe, l);
			}

			TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);

			// are we already done hashing?
			if (pe->hash && !pe->hashing && pe->hash->offset == piece_size)
			{
				DLOG("do_hash: (%d) (already done)\n", int(pe->piece));
				j->d.piece_hash = pe->hash->h.final();
				pe->hash.reset();
				if (pe->cache_state != cached_piece_entry::volatile_read_lru)
					pe->hashing_done = 1;
#if TORRENT_USE_ASSERTS
				++pe->hash_passes;
#endif
				m_disk_cache.update_cache_state(pe);
				m_disk_cache.maybe_free_piece(pe);
				return status_t::no_error;
			}
		}
		else if (m_settings.get_bool(settings_pack::use_read_cache) == false)
		{
			return do_uncached_hash(j);
		}

		if (pe == nullptr)
		{
			std::uint16_t const cache_state = std::uint16_t((j->flags & disk_interface::volatile_read)
				? cached_piece_entry::volatile_read_lru
				: cached_piece_entry::read_lru1);
			pe = m_disk_cache.allocate_piece(j, cache_state);
		}
		if (pe == nullptr)
		{
			j->error.ec = error::no_memory;
			j->error.operation = operation_t::alloc_cache_piece;
			return status_t::fatal_disk_error;
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

		piece_refcount_holder refcount_holder(pe);

		if (!pe->hash)
		{
			pe->hashing_done = 0;
			pe->hash.reset(new partial_hash);
		}
		partial_hash* ph = pe->hash.get();

		int const blocks_in_piece = (piece_size + default_block_size - 1) / default_block_size;

		// we don't care about anything to the left of ph->offset
		// since those blocks have already been hashed.
		// we just care about [firs_block, first_block + blocks_left]
		int const first_block = ph->offset / default_block_size;
		int const blocks_left = blocks_in_piece - first_block;

		//   ph->offset
		//           |  first_block
		//           |   |
		//           v   v
		// +---+---+---+---+---+---+
		// |   |   |   |   |   |   |
		// +---+---+---+---+---+---+
		//
		//             \-----------/
		//               blocks_left
		//
		// \-----------------------/
		//     blocks_in_piece

		// keep track of which blocks we have locked by incrementing
		// their refcounts. This is used to decrement only these blocks
		// later.
		TORRENT_ALLOCA(locked_blocks, int, blocks_in_piece);
		std::fill(locked_blocks.begin(), locked_blocks.end(), 0);
		int num_locked_blocks = 0;

		// increment the refcounts of all
		// blocks up front, and then hash them without holding the lock
		TORRENT_PIECE_ASSERT(ph->offset % default_block_size == 0, pe);
		for (int i = 0; i < blocks_left; ++i)
		{
			// is the block not in the cache?
			if (pe->blocks[first_block + i].buf == nullptr) continue;

			// if we fail to lock the block, it's no longer in the cache
			if (m_disk_cache.inc_block_refcount(pe, first_block + i, block_cache::ref_hashing) == false)
				continue;

			locked_blocks[num_locked_blocks++] = i;
		}

		// to keep the cache footprint low, try to evict a volatile piece
		m_disk_cache.try_evict_one_volatile();

		// save a local copy of offset to avoid concurrent access
		int offset = ph->offset;
#if TORRENT_USE_ASSERTS
		int old_offset = offset;
#endif

		l.unlock();

		bool slow_path = true;

		if (num_locked_blocks == 0)
		{
			// this is the fast path where we don't have any blocks in the cache.
			// We'll need to read all (remaining blocks) from disk
			TORRENT_ALLOCA(iov, iovec_t, blocks_left);
			if (m_disk_cache.allocate_iovec(iov) >= 0)
			{
				// free buffers at the end of the scope
				auto iov_dealloc = aux::scope_end([&]{ m_disk_cache.free_iovec(iov); });

				// if this is the last piece, adjust the size of the
				// last buffer to match up
				iov[blocks_left - 1] = iov[blocks_left - 1].first(
					piece_size - (blocks_in_piece - 1) * default_block_size);
				TORRENT_ASSERT(iov[blocks_left - 1].size() > 0);
				TORRENT_ASSERT(iov[blocks_left - 1].size() <= default_block_size);

				time_point const start_time = clock_type::now();
				int const read_ret = j->storage->readv(iov
					, j->piece, offset, file_flags, j->error);

				if (read_ret == piece_size - offset)
				{
					std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

					m_stats_counters.inc_stats_counter(counters::num_blocks_hashed, blocks_left);
					m_stats_counters.inc_stats_counter(counters::num_read_back, blocks_left);
					m_stats_counters.inc_stats_counter(counters::num_blocks_read, blocks_left);
					m_stats_counters.inc_stats_counter(counters::num_read_ops);
					m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
					m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);

					for (auto const& v : iov)
					{
						offset += int(v.size());
						ph->h.update(v);
					}

					slow_path = false;

					TORRENT_ASSERT(offset == piece_size);

					// we want to hold on to the buffers now, to insert them in the
					// cache
					iov_dealloc.disarm();
					l.lock();
					m_disk_cache.insert_blocks(pe, first_block, iov, j);
					l.unlock();
				}
			}
		}

		status_t ret = status_t::no_error;
		if (slow_path)
		{
			int next_locked_block = 0;
			for (int i = 0; i < blocks_left; ++i)
			{
				if (next_locked_block < num_locked_blocks
					&& locked_blocks[next_locked_block] == i)
				{
					int const len = std::min(default_block_size, piece_size - offset);
					++next_locked_block;
					TORRENT_PIECE_ASSERT(pe->blocks[first_block + i].buf, pe);
					TORRENT_PIECE_ASSERT(offset == (first_block + i) * default_block_size, pe);
					offset += len;
					ph->h.update({pe->blocks[first_block + i].buf, len});
				}
				else
				{
					iovec_t const iov = { m_disk_cache.allocate_buffer("hashing")
						, std::min(default_block_size, piece_size - offset)};

					if (iov.data() == nullptr)
					{
						l.lock();
						// decrement the refcounts of the blocks we just hashed
						for (int k = 0; k < num_locked_blocks; ++k)
							m_disk_cache.dec_block_refcount(pe, first_block + locked_blocks[k], block_cache::ref_hashing);

						refcount_holder.release();
						pe->hashing = false;
						pe->hash.reset();
						m_disk_cache.maybe_free_piece(pe);

						j->error.ec = errors::no_memory;
						j->error.operation = operation_t::alloc_cache_piece;
						return status_t::fatal_disk_error;
					}

					// free buffers at the end of the scope
					auto iov_dealloc = aux::scope_end([&]{ m_disk_cache.free_buffer(iov.data()); });

					DLOG("do_hash: reading (piece: %d block: %d)\n"
						, static_cast<int>(pe->piece), first_block + i);

					time_point const start_time = clock_type::now();
					TORRENT_PIECE_ASSERT(offset == (first_block + i) * default_block_size, pe);
					int const read_ret = j->storage->readv(iov, j->piece
						, offset, file_flags, j->error);

					if (read_ret < 0)
					{
						ret = status_t::fatal_disk_error;
						TORRENT_ASSERT(j->error.ec && j->error.operation != operation_t::unknown);
						break;
					}

					// treat a short read as an error. The hash will be invalid, the
					// block cannot be cached and the main thread should skip the rest
					// of this file
					if (read_ret != int(iov.size()))
					{
						ret = status_t::fatal_disk_error;
						j->error.ec = boost::asio::error::eof;
						j->error.operation = operation_t::file_read;
						break;
					}

					if (!j->error.ec)
					{
						std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

						m_stats_counters.inc_stats_counter(counters::num_read_back);
						m_stats_counters.inc_stats_counter(counters::num_blocks_read);
						m_stats_counters.inc_stats_counter(counters::num_read_ops);
						m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
						m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
					}

					TORRENT_PIECE_ASSERT(offset == (first_block + i) * default_block_size, pe);
					offset += int(iov.size());
					ph->h.update(iov);

					iov_dealloc.disarm();
					l.lock();
					m_disk_cache.insert_blocks(pe, first_block + i, iov, j);
					l.unlock();
				}
			}
		}

		l.lock();

		TORRENT_ASSERT(old_offset == ph->offset);
		ph->offset = offset;

		// decrement the refcounts of the blocks we just hashed
		for (int i = 0; i < num_locked_blocks; ++i)
			m_disk_cache.dec_block_refcount(pe, first_block + locked_blocks[i], block_cache::ref_hashing);

		refcount_holder.release();

		pe->hashing = 0;

		if (ret == status_t::no_error)
		{
			j->d.piece_hash = ph->h.final();

			pe->hash.reset();
			if (pe->cache_state != cached_piece_entry::volatile_read_lru)
				pe->hashing_done = 1;
#if TORRENT_USE_ASSERTS
			++pe->hash_passes;
#endif
			m_disk_cache.update_cache_state(pe);
		}

		m_disk_cache.maybe_free_piece(pe);

		TORRENT_ASSERT(ret == status_t::no_error || (j->error.ec && j->error.operation != operation_t::unknown));

		return ret;
	}

	status_t disk_io_thread::do_move_storage(disk_io_job* j, jobqueue_t& /* completed_jobs */ )
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// if files have to be closed, that's the storage's responsibility
		return j->storage->move_storage(boost::get<std::string>(j->argument)
			, j->move_flags, j->error);
	}

	status_t disk_io_thread::do_release_files(disk_io_job* j, jobqueue_t& completed_jobs)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		std::unique_lock<std::mutex> l(m_cache_mutex);
		flush_cache(j->storage.get(), flush_write_cache, completed_jobs, l);
		l.unlock();

		j->storage->release_files(j->error);
		return j->error ? status_t::fatal_disk_error : status_t::no_error;
	}

	status_t disk_io_thread::do_delete_files(disk_io_job* j, jobqueue_t& completed_jobs)
	{
		TORRENT_ASSERT(boost::get<remove_flags_t>(j->argument));

		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		std::unique_lock<std::mutex> l(m_cache_mutex);

		flush_cache(j->storage.get()
			, flush_read_cache | flush_delete_cache | flush_expect_clear
			, completed_jobs, l);
		l.unlock();

		j->storage->delete_files(boost::get<remove_flags_t>(j->argument), j->error);
		return j->error ? status_t::fatal_disk_error : status_t::no_error;
	}

	status_t disk_io_thread::do_check_fastresume(disk_io_job* j, jobqueue_t& /* completed_jobs */ )
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		add_torrent_params const* rd = boost::get<add_torrent_params const*>(j->argument);
		add_torrent_params tmp;
		if (rd == nullptr) rd = &tmp;

		std::unique_ptr<aux::vector<std::string, file_index_t>> links(j->d.links);
		// check if the fastresume data is up to date
		// if it is, use it and return true. If it
		// isn't return false and the full check
		// will be run. If the links pointer is non-empty, it has the same number
		// of elements as there are files. Each element is either empty or contains
		// the absolute path to a file identical to the corresponding file in this
		// torrent. The storage must create hard links (or copy) those files. If
		// any file does not exist or is inaccessible, the disk job must fail.

		TORRENT_ASSERT(j->storage->files().piece_length() > 0);

		bool const verify_success = j->storage->verify_resume_data(*rd
			, links ? *links : aux::vector<std::string, file_index_t>(), j->error);

		// if we don't have any resume data, return
		// or if error is set and return value is 'no_error' or 'need_full_check'
		// the error message indicates that the fast resume data was rejected
		// if 'fatal_disk_error' is returned, the error message indicates what
		// when wrong in the disk access
		if ((rd->have_pieces.empty() || !verify_success)
			&& !m_settings.get_bool(settings_pack::no_recheck_incomplete_resume))
		{
			// j->error may have been set at this point, by verify_resume_data()
			// it's important to not have it cleared out subsequent calls, as long
			// as they succeed.
			storage_error ignore;
			if (j->storage->has_any_file(ignore))
			{
				// always initialize the storage
				storage_error se;
				j->storage->initialize(se);
				if (se)
				{
					j->error = se;
					return status_t::fatal_disk_error;
				}
				return status_t::need_full_check;
			}
		}

		storage_error se;
		j->storage->initialize(se);
		if (se)
		{
			j->error = se;
			return status_t::fatal_disk_error;
		}
		return status_t::no_error;
	}

	status_t disk_io_thread::do_rename_file(disk_io_job* j, jobqueue_t& /* completed_jobs */ )
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// if files need to be closed, that's the storage's responsibility
		j->storage->rename_file(j->file_index, boost::get<std::string>(j->argument)
			, j->error);
		return j->error ? status_t::fatal_disk_error : status_t::no_error;
	}

	status_t disk_io_thread::do_stop_torrent(disk_io_job* j, jobqueue_t& completed_jobs)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// issue write commands for all dirty blocks
		// and clear all read jobs
		std::unique_lock<std::mutex> l(m_cache_mutex);
		flush_cache(j->storage.get(), flush_read_cache | flush_write_cache
			, completed_jobs, l);
		l.unlock();

		j->storage->release_files(j->error);
		return j->error ? status_t::fatal_disk_error : status_t::no_error;
	}

	namespace {

	void get_cache_info_impl(cached_piece_info& info, cached_piece_entry const* i)
	{
		info.piece = i->piece;
		info.storage = i->storage.get();
		info.last_use = i->expire;
		info.need_readback = i->need_readback;
		info.next_to_hash = i->hash == nullptr ? -1 : (i->hash->offset + default_block_size - 1) / default_block_size;
		info.kind = i->cache_state == cached_piece_entry::write_lru
			? cached_piece_info::write_cache
			: i->cache_state == cached_piece_entry::volatile_read_lru
			? cached_piece_info::volatile_read_cache
			: cached_piece_info::read_cache;
		int const blocks_in_piece = i->blocks_in_piece;
		info.blocks.resize(aux::numeric_cast<std::size_t>(blocks_in_piece));
		for (int b = 0; b < blocks_in_piece; ++b)
			info.blocks[std::size_t(b)] = i->blocks[b].buf != nullptr;
	}

	} // anonymous namespace

	void disk_io_thread::update_stats_counters(counters& c) const
	{
		// These are atomic_counts, so it's safe to access them from
		// a different thread
		std::unique_lock<std::mutex> jl(m_job_mutex);

		c.set_value(counters::num_read_jobs, read_jobs_in_use());
		c.set_value(counters::num_write_jobs, write_jobs_in_use());
		c.set_value(counters::num_jobs, jobs_in_use());
		c.set_value(counters::queued_disk_jobs, m_generic_io_jobs.m_queued_jobs.size()
			+ m_hash_io_jobs.m_queued_jobs.size());

		jl.unlock();

		std::unique_lock<std::mutex> l(m_cache_mutex);

		// gauges
		c.set_value(counters::disk_blocks_in_use, m_disk_cache.in_use());

		m_disk_cache.update_stats_counters(c);
	}

	void disk_io_thread::get_cache_info(cache_status* ret, storage_index_t const st
		, bool const no_pieces, bool const session) const
	{
		std::unique_lock<std::mutex> l(m_cache_mutex);

#if TORRENT_ABI_VERSION == 1
		ret->total_used_buffers = m_disk_cache.in_use();

		ret->blocks_read_hit = int(m_stats_counters[counters::num_blocks_cache_hits]);
		ret->blocks_read = int(m_stats_counters[counters::num_blocks_read]);
		ret->blocks_written = int(m_stats_counters[counters::num_blocks_written]);
		ret->writes = int(m_stats_counters[counters::num_write_ops]);
		ret->reads = int(m_stats_counters[counters::num_read_ops]);

		int num_read_jobs = int(std::max(std::int64_t(1)
			, m_stats_counters[counters::num_read_ops]));
		int num_write_jobs = int(std::max(std::int64_t(1)
			, m_stats_counters[counters::num_write_ops]));
		int num_hash_jobs = int(std::max(std::int64_t(1)
			, m_stats_counters[counters::num_blocks_hashed]));

		ret->average_read_time = int(m_stats_counters[counters::disk_read_time] / num_read_jobs);
		ret->average_write_time = int(m_stats_counters[counters::disk_write_time] / num_write_jobs);
		ret->average_hash_time = int(m_stats_counters[counters::disk_hash_time] / num_hash_jobs);
		ret->average_job_time = int(m_stats_counters[counters::disk_job_time]
			/ (num_read_jobs + num_write_jobs + num_hash_jobs));
		ret->cumulative_job_time = int(m_stats_counters[counters::disk_job_time]);
		ret->cumulative_read_time = int(m_stats_counters[counters::disk_read_time]);
		ret->cumulative_write_time = int(m_stats_counters[counters::disk_write_time]);
		ret->cumulative_hash_time = int(m_stats_counters[counters::disk_hash_time]);
		ret->total_read_back = int(m_stats_counters[counters::num_read_back]);

		ret->blocked_jobs = int(m_stats_counters[counters::blocked_disk_jobs]);

		ret->num_jobs = jobs_in_use();
		ret->num_read_jobs = read_jobs_in_use();
		ret->read_queue_size = read_jobs_in_use();
		ret->num_write_jobs = write_jobs_in_use();
		ret->pending_jobs = int(m_stats_counters[counters::num_running_disk_jobs]);
		ret->num_writing_threads = int(m_stats_counters[counters::num_writing_threads]);

		for (int i = 0; i < static_cast<int>(job_action_t::num_job_ids); ++i)
			ret->num_fence_jobs[i] = int(m_stats_counters[counters::num_fenced_read + i]);

		m_disk_cache.get_stats(ret);

#endif

		ret->pieces.clear();

		if (no_pieces == false)
		{
			if (!session)
			{
				std::shared_ptr<storage_interface> storage = m_torrents[st];
				TORRENT_ASSERT(storage);
				ret->pieces.reserve(aux::numeric_cast<std::size_t>(storage->num_pieces()));

				for (auto const& pe : storage->cached_pieces())
				{
					TORRENT_ASSERT(pe.storage.get() == storage.get());

					if (pe.cache_state == cached_piece_entry::read_lru2_ghost
						|| pe.cache_state == cached_piece_entry::read_lru1_ghost)
						continue;
					ret->pieces.emplace_back();
					get_cache_info_impl(ret->pieces.back(), &pe);
				}
			}
			else
			{
				ret->pieces.reserve(aux::numeric_cast<std::size_t>(m_disk_cache.num_pieces()));

				auto range = m_disk_cache.all_pieces();
				for (auto i = range.first; i != range.second; ++i)
				{
					if (i->cache_state == cached_piece_entry::read_lru2_ghost
						|| i->cache_state == cached_piece_entry::read_lru1_ghost)
						continue;
					ret->pieces.emplace_back();
					get_cache_info_impl(ret->pieces.back(), &*i);
				}
			}
		}

		l.unlock();

#if TORRENT_ABI_VERSION == 1
		std::unique_lock<std::mutex> jl(m_job_mutex);
		ret->queued_jobs = m_generic_io_jobs.m_queued_jobs.size() + m_hash_io_jobs.m_queued_jobs.size();
		jl.unlock();
#endif
	}

	status_t disk_io_thread::do_flush_piece(disk_io_job* j, jobqueue_t& completed_jobs)
	{
		std::unique_lock<std::mutex> l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe == nullptr) return status_t::no_error;

#if TORRENT_USE_ASSERTS
		pe->piece_log.emplace_back(j->action);
#endif
		try_flush_hashed(pe, m_settings.get_int(
			settings_pack::write_cache_line_size), completed_jobs, l);

		return status_t::no_error;
	}

	// this is triggered every time we insert a new dirty block in a piece
	// by the time this gets executed, the block may already have been flushed
	// triggered by another mechanism.
	status_t disk_io_thread::do_flush_hashed(disk_io_job* j, jobqueue_t& completed_jobs)
	{
		std::unique_lock<std::mutex> l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);

		if (pe == nullptr) return status_t::no_error;

		pe->outstanding_flush = 0;

		if (pe->num_dirty == 0) return status_t::no_error;

		// if multiple threads are flushing this piece, this assert may fire
		// this happens if the cache is running full and pieces are started to
		// get flushed
//		TORRENT_PIECE_ASSERT(pe->outstanding_flush == 1, pe);

#if TORRENT_USE_ASSERTS
		pe->piece_log.emplace_back(j->action);
#endif
		TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1
			|| pe->cache_state == cached_piece_entry::read_lru2, pe);

		piece_refcount_holder refcount_holder(pe);

		if (!pe->hashing_done)
		{
			if (pe->hash == nullptr && !m_settings.get_bool(settings_pack::disable_hash_checks))
			{
				pe->hash.reset(new partial_hash);
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
		try_flush_hashed(pe, m_settings.get_int(
			settings_pack::write_cache_line_size), completed_jobs, l);

		TORRENT_ASSERT(l.owns_lock());

		refcount_holder.release();

		m_disk_cache.maybe_free_piece(pe);

		return status_t::no_error;
	}

	status_t disk_io_thread::do_flush_storage(disk_io_job* j, jobqueue_t& completed_jobs)
	{
		std::unique_lock<std::mutex> l(m_cache_mutex);
		flush_cache(j->storage.get(), flush_write_cache, completed_jobs, l);
		return status_t::no_error;
	}

	status_t disk_io_thread::do_trim_cache(disk_io_job*, jobqueue_t& /* completed_jobs */)
	{
//#error implement
		return status_t::no_error;
	}

	status_t disk_io_thread::do_file_priority(disk_io_job* j, jobqueue_t& /* completed_jobs */ )
	{
		j->storage->set_file_priority(
			boost::get<aux::vector<download_priority_t, file_index_t>>(j->argument)
			, j->error);
		return status_t::no_error;
	}

	// this job won't return until all outstanding jobs on this
	// piece are completed or cancelled and the buffers for it
	// have been evicted
	status_t disk_io_thread::do_clear_piece(disk_io_job* j, jobqueue_t& completed_jobs)
	{
		std::unique_lock<std::mutex> l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe == nullptr) return status_t::no_error;
		TORRENT_PIECE_ASSERT(pe->hashing == false, pe);
		pe->hashing_done = 0;
		pe->hash.reset();
		pe->hashing_done = false;

#if TORRENT_USE_ASSERTS
		pe->piece_log.emplace_back(j->action);
#endif

		// evict_piece returns true if the piece was in fact
		// evicted. A piece may fail to be evicted if there
		// are still outstanding operations on it, in which case
		// try again later
		jobqueue_t jobs;
		if (m_disk_cache.evict_piece(pe, jobs, block_cache::allow_ghost))
		{
			fail_jobs_impl(storage_error(boost::asio::error::operation_aborted)
				, jobs, completed_jobs);
			return status_t::no_error;
		}

		m_disk_cache.mark_for_eviction(pe, block_cache::allow_ghost);
		if (pe->num_blocks == 0) return status_t::no_error;

		// we should always be able to evict the piece, since
		// this is a fence job
		TORRENT_PIECE_ASSERT_FAIL(pe);
		return retry_job;
	}

	void disk_io_thread::add_fence_job(disk_io_job* j, bool const user_add)
	{
		// if this happens, it means we started to shut down
		// the disk threads too early. We have to post all jobs
		// before the disk threads are shut down
		TORRENT_ASSERT(!m_abort);

		DLOG("add_fence:job: %s (outstanding: %d)\n"
			, job_name(j->action)
			, j->storage->num_outstanding_jobs());

		m_stats_counters.inc_stats_counter(counters::num_fenced_read + static_cast<int>(j->action));

		disk_io_job* fj = allocate_job(job_action_t::flush_storage);
		fj->storage = j->storage;
		TORRENT_ASSERT(fj->flags == disk_job_flags_t{});

		int ret = j->storage->raise_fence(j, fj, m_stats_counters);
		if (ret == aux::disk_job_fence::fence_post_fence)
		{
			std::unique_lock<std::mutex> l(m_job_mutex);
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
			m_generic_io_jobs.m_queued_jobs.push_back(j);
			l.unlock();

			// discard the flush job
			free_job(fj);

			if (num_threads() == 0 && user_add)
				immediate_execute();

			return;
		}

		if (ret == aux::disk_job_fence::fence_post_flush)
		{
			// now, we have to make sure that all outstanding jobs on this
			// storage actually get flushed, in order for the fence job to
			// be executed
			std::unique_lock<std::mutex> l(m_job_mutex);
			TORRENT_ASSERT((fj->flags & disk_io_job::in_progress) || !fj->storage);

			m_generic_io_jobs.m_queued_jobs.push_front(fj);
		}
		else
		{
			TORRENT_ASSERT(!(fj->flags & disk_io_job::in_progress));
			TORRENT_ASSERT(fj->blocked);
		}

		if (num_threads() == 0 && user_add)
			immediate_execute();
	}

	void disk_io_thread::add_job(disk_io_job* j, bool const user_add)
	{
		TORRENT_ASSERT(m_magic == 0x1337);

		TORRENT_ASSERT(!j->storage || j->storage->files().is_valid());
		TORRENT_ASSERT(j->next == nullptr);
		// if this happens, it means we started to shut down
		// the disk threads too early. We have to post all jobs
		// before the disk threads are shut down
		TORRENT_ASSERT(!m_abort
			|| j->action == job_action_t::flush_piece
			|| j->action == job_action_t::trim_cache);

		// this happens for read jobs that get hung on pieces in the
		// block cache, and then get issued
		if (j->flags & disk_io_job::in_progress)
		{
			std::unique_lock<std::mutex> l(m_job_mutex);
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
			m_generic_io_jobs.m_queued_jobs.push_back(j);

			// if we literally have 0 disk threads, we have to execute the jobs
			// immediately. If add job is called internally by the disk_io_thread,
			// we need to defer executing it. We only want the top level to loop
			// over the job queue (as is done below)
			if (num_threads() == 0 && user_add)
			{
				l.unlock();
				immediate_execute();
			}
			return;
		}

		DLOG("add_job: %s (outstanding: %d)\n"
			, job_name(j->action)
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
				, job_name(j->action), j->storage ? j->storage->num_blocked() : 0
				, int(m_stats_counters[counters::blocked_disk_jobs]));
			return;
		}

		std::unique_lock<std::mutex> l(m_job_mutex);

		TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

		job_queue& q = queue_for_job(j);
		q.m_queued_jobs.push_back(j);
		// if we literally have 0 disk threads, we have to execute the jobs
		// immediately. If add job is called internally by the disk_io_thread,
		// we need to defer executing it. We only want the top level to loop
		// over the job queue (as is done below)
		if (pool_for_job(j).max_threads() == 0 && user_add)
		{
			l.unlock();
			immediate_execute();
		}
	}

	void disk_io_thread::immediate_execute()
	{
		while (!m_generic_io_jobs.m_queued_jobs.empty())
		{
			disk_io_job* j = m_generic_io_jobs.m_queued_jobs.pop_front();
			maybe_flush_write_blocks();
			execute_job(j);
		}
	}

	void disk_io_thread::submit_jobs()
	{
		std::unique_lock<std::mutex> l(m_job_mutex);
		if (!m_generic_io_jobs.m_queued_jobs.empty())
		{
			m_generic_io_jobs.m_job_cond.notify_all();
			m_generic_threads.job_queued(m_generic_io_jobs.m_queued_jobs.size());
		}
		if (!m_hash_io_jobs.m_queued_jobs.empty())
		{
			m_hash_io_jobs.m_job_cond.notify_all();
			m_hash_threads.job_queued(m_hash_io_jobs.m_queued_jobs.size());
		}
	}

	void disk_io_thread::maybe_flush_write_blocks()
	{
		time_point const now = clock_type::now();
		if (now <= m_last_cache_expiry + seconds(5)) return;

		std::unique_lock<std::mutex> l(m_cache_mutex);
		DLOG("blocked_jobs: %d queued_jobs: %d num_threads %d\n"
			, int(m_stats_counters[counters::blocked_disk_jobs])
			, m_generic_io_jobs.m_queued_jobs.size(), num_threads());
		m_last_cache_expiry = now;
		jobqueue_t completed_jobs;
		flush_expired_write_blocks(completed_jobs, l);
		l.unlock();
		if (!completed_jobs.empty())
			add_completed_jobs(completed_jobs);
	}

	void disk_io_thread::execute_job(disk_io_job* j)
	{
		jobqueue_t completed_jobs;
		if (j->flags & disk_io_job::aborted)
		{
			j->ret = status_t::fatal_disk_error;
			j->error = storage_error(boost::asio::error::operation_aborted);
			completed_jobs.push_back(j);
			add_completed_jobs(completed_jobs);
			return;
		}

		perform_job(j, completed_jobs);
		if (!completed_jobs.empty())
			add_completed_jobs(completed_jobs);
	}

	bool disk_io_thread::wait_for_job(job_queue& jobq, disk_io_thread_pool& threads
		, std::unique_lock<std::mutex>& l)
	{
		TORRENT_ASSERT(l.owns_lock());

		// the thread should only go active if it is exiting or there is work to do
		// if the thread goes active on every wakeup it causes the minimum idle thread
		// count to be lower than it should be
		// for performance reasons we also want to avoid going idle and active again
		// if there is already work to do
		if (jobq.m_queued_jobs.empty())
		{
			threads.thread_idle();

			do
			{
				// if the number of wanted threads is decreased,
				// we may stop this thread
				// when we're terminating the last thread, make sure
				// we finish up all queued jobs first
				if (threads.should_exit()
					&& (jobq.m_queued_jobs.empty()
						|| threads.num_threads() > 1)
					// try_thread_exit must be the last condition
					&& threads.try_thread_exit(std::this_thread::get_id()))
				{
					// time to exit this thread.
					threads.thread_active();
					return true;
				}

				jobq.m_job_cond.wait(l);
			} while (jobq.m_queued_jobs.empty());

			threads.thread_active();
		}

		return false;
	}

	void disk_io_thread::thread_fun(job_queue& queue
		, disk_io_thread_pool& pool)
	{
		std::thread::id const thread_id = std::this_thread::get_id();

		DLOG("started disk thread\n");

		std::unique_lock<std::mutex> l(m_job_mutex);

		++m_num_running_threads;
		m_stats_counters.inc_stats_counter(counters::num_running_threads, 1);

		for (;;)
		{
			disk_io_job* j = nullptr;
			bool const should_exit = wait_for_job(queue, pool, l);
			if (should_exit) break;
			j = queue.m_queued_jobs.pop_front();
			l.unlock();

			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

			if (&pool == &m_generic_threads && thread_id == pool.first_thread_id())
			{
				// there's no need for all threads to be doing this
				maybe_flush_write_blocks();

				time_point const now = aux::time_now();
				{
					std::unique_lock<std::mutex> l2(m_need_tick_mutex);
					while (!m_need_tick.empty() && m_need_tick.front().first < now)
					{
						std::shared_ptr<storage_interface> st = m_need_tick.front().second.lock();
						m_need_tick.erase(m_need_tick.begin());
						if (st)
						{
							l2.unlock();
							st->tick();
							l2.lock();
						}
					}
				}

				if (now > m_next_close_oldest_file)
				{
					seconds const interval(m_settings.get_int(settings_pack::close_file_interval));
					if (interval <= seconds(0))
					{
						m_next_close_oldest_file = max_time();
					}
					else
					{
						m_next_close_oldest_file = now + interval;
						m_file_pool.close_oldest();
					}
				}
			}

			execute_job(j);

			l.lock();
		}

		// do cleanup in the last running thread
		// if we're not aborting, that means we just configured the thread pool to
		// not have any threads (i.e. perform all disk operations in the network
		// thread). In this case, the cleanup will happen in abort().
		m_stats_counters.inc_stats_counter(counters::num_running_threads, -1);
		if (--m_num_running_threads > 0 || !m_abort)
		{
			DLOG("exiting disk thread. num_threads: %d aborting: %d\n"
				, num_threads(), int(m_abort));
			TORRENT_ASSERT(m_magic == 0x1337);
			return;
		}

		// it is important to hold the job mutex while calling try_thread_exit()
		// and continue to hold it until checking m_abort above so that abort()
		// doesn't inadvertently trigger the code below when it thinks there are no
		// more disk I/O threads running
		l.unlock();

		// at this point, there are no queued jobs left. However, main
		// thread is still running and may still have peer_connections
		// that haven't fully destructed yet, reclaiming their references
		// to read blocks in the disk cache. We need to wait until all
		// references are removed from other threads before we can go
		// ahead with the cleanup.
		// This is not supposed to happen because the disk thread is now scheduled
		// for shut down after all peers have shut down (see
		// session_impl::abort_stage2()).
		std::unique_lock<std::mutex> l2(m_cache_mutex);
		TORRENT_ASSERT_VAL(m_disk_cache.pinned_blocks() == 0
			, m_disk_cache.pinned_blocks());
		while (m_disk_cache.pinned_blocks() > 0)
		{
			l2.unlock();
			std::this_thread::sleep_for(milliseconds(100));
			l2.lock();
		}
		l2.unlock();

		DLOG("the last disk thread alive. cleaning up\n");

		abort_jobs();

		TORRENT_ASSERT(m_magic == 0x1337);
	}

	void disk_io_thread::abort_jobs()
	{
		DLOG("disk_io_thread::abort_jobs\n");

		TORRENT_ASSERT(m_magic == 0x1337);
		if (m_jobs_aborted.test_and_set()) return;

		jobqueue_t jobs;
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
		auto pieces = m_disk_cache.all_pieces();
		TORRENT_ASSERT(pieces.first == pieces.second);
#endif

		TORRENT_ASSERT(m_magic == 0x1337);
	}

	int disk_io_thread::num_threads() const
	{
		return m_generic_threads.max_threads() + m_hash_threads.max_threads();
	}

	disk_io_thread::job_queue& disk_io_thread::queue_for_job(disk_io_job* j)
	{
		if (m_hash_threads.max_threads() > 0 && j->action == job_action_t::hash)
			return m_hash_io_jobs;
		else
			return m_generic_io_jobs;
	}

	disk_io_thread_pool& disk_io_thread::pool_for_job(disk_io_job* j)
	{
		if (m_hash_threads.max_threads() > 0 && j->action == job_action_t::hash)
			return m_hash_threads;
		else
			return m_generic_threads;
	}

	// this is a callback called by the block_cache when
	// it's exceeding the disk cache size.
	void disk_io_thread::trigger_cache_trim()
	{
		// we just exceeded the cache size limit. Trigger a trim job
		disk_io_job* j = allocate_job(job_action_t::trim_cache);
		add_job(j, false);
		submit_jobs();
	}

	void disk_io_thread::add_completed_jobs(jobqueue_t& jobs)
	{
		jobqueue_t new_completed_jobs;
		do
		{
			// when a job completes, it's possible for it to cause
			// a fence to be lowered, issuing the jobs queued up
			// behind the fence. It's also possible for some of these
			// jobs to be cache-hits, completing immediately. Those
			// jobs are added to the new_completed_jobs queue and
			// we need to re-issue those
			add_completed_jobs_impl(jobs, new_completed_jobs);
			TORRENT_ASSERT(jobs.empty());
			jobs.swap(new_completed_jobs);
		} while (!jobs.empty());
	}

	void disk_io_thread::add_completed_jobs_impl(jobqueue_t& jobs
		, jobqueue_t& completed_jobs)
	{
		jobqueue_t new_jobs;
		int ret = 0;
		for (auto i = jobs.iterate(); i.get(); i.next())
		{
			disk_io_job* j = i.get();
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

//			DLOG("job_complete %s outstanding: %d\n"
//				, job_name(j->action), j->storage ? j->storage->num_outstanding_jobs() : 0);

			if (j->storage)
			{
				if (j->flags & disk_io_job::fence)
				{
					m_stats_counters.inc_stats_counter(
						counters::num_fenced_read + static_cast<int>(j->action), -1);
				}

				ret += j->storage->job_complete(j, new_jobs);
			}
			TORRENT_ASSERT(ret == new_jobs.size());
			TORRENT_ASSERT(!(j->flags & disk_io_job::in_progress));
#if TORRENT_USE_ASSERTS
			TORRENT_ASSERT(j->job_posted == false);
			j->job_posted = true;
#endif
		}

		if (ret)
		{
			DLOG("unblocked %d jobs (%d left)\n", ret
				, int(m_stats_counters[counters::blocked_disk_jobs]) - ret);
		}

		m_stats_counters.inc_stats_counter(counters::blocked_disk_jobs, -ret);
		TORRENT_ASSERT(int(m_stats_counters[counters::blocked_disk_jobs]) >= 0);

		if (!new_jobs.empty())
		{
#if TORRENT_USE_ASSERTS
			for (auto i = new_jobs.iterate(); i.get(); i.next())
			{
				disk_io_job const* j = static_cast<disk_io_job const*>(i.get());
				TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

				if (j->action != job_action_t::write) continue;

				std::unique_lock<std::mutex> l(m_cache_mutex);
				cached_piece_entry* pe = m_disk_cache.find_piece(j);
				if (!pe) continue;

				TORRENT_ASSERT(pe->blocks[j->d.io.offset / 16 / 1024].buf
					!= boost::get<disk_buffer_holder>(j->argument).get());
				TORRENT_ASSERT(pe->blocks[j->d.io.offset / 16 / 1024].buf == nullptr);
				TORRENT_ASSERT(!pe->hashing_done);
			}
#endif
			jobqueue_t other_jobs;
			jobqueue_t flush_jobs;
			std::unique_lock<std::mutex> l_(m_cache_mutex);
			while (!new_jobs.empty())
			{
				disk_io_job* j = new_jobs.pop_front();

				if (j->action == job_action_t::read)
				{
					int const state = prep_read_job_impl(j, false);
					switch (state)
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
				if (j->action != job_action_t::write)
				{
					other_jobs.push_back(j);
					continue;
				}

				cached_piece_entry* pe = m_disk_cache.add_dirty_block(j
					, !m_settings.get_bool(settings_pack::disable_hash_checks));

				if (pe == nullptr)
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
					&& pe->hash == nullptr
					&& !m_settings.get_bool(settings_pack::disable_hash_checks))
				{
					pe->hash.reset(new partial_hash);
					m_disk_cache.update_cache_state(pe);
				}

				TORRENT_PIECE_ASSERT(pe->cache_state <= cached_piece_entry::read_lru1 || pe->cache_state == cached_piece_entry::read_lru2, pe);

				if (pe->outstanding_flush == 0)
				{
					pe->outstanding_flush = 1;

					// the block and write job were successfully inserted
					// into the cache. Now, see if we should trigger a flush
					disk_io_job* fj = allocate_job(job_action_t::flush_hashed);
					fj->storage = j->storage;
					fj->piece = j->piece;
					flush_jobs.push_back(fj);
				}
			}
			l_.unlock();

			{
				std::lock_guard<std::mutex> l(m_job_mutex);
				m_generic_io_jobs.m_queued_jobs.append(other_jobs);
			}

			while (!flush_jobs.empty())
			{
				disk_io_job* j = flush_jobs.pop_front();
				add_job(j, false);
			}

			{
				std::lock_guard<std::mutex> l(m_job_mutex);
				m_generic_io_jobs.m_job_cond.notify_all();
				m_generic_threads.job_queued(m_generic_io_jobs.m_queued_jobs.size());
			}
		}

		std::lock_guard<std::mutex> l(m_completed_jobs_mutex);
		m_completed_jobs.append(jobs);

		if (!m_job_completions_in_flight)
		{
			// we take this lock just to make the logging prettier (non-interleaved)
			DLOG("posting job handlers (%d)\n", m_completed_jobs.size());

			m_ios.post(std::bind(&disk_io_thread::call_job_handlers, this));
			m_job_completions_in_flight = true;
		}
	}

	// This is run in the network thread
	void disk_io_thread::call_job_handlers()
	{
		m_stats_counters.inc_stats_counter(counters::on_disk_counter);
		std::unique_lock<std::mutex> l(m_completed_jobs_mutex);

		DLOG("call_job_handlers (%d)\n", m_completed_jobs.size());

		TORRENT_ASSERT(m_job_completions_in_flight);
		m_job_completions_in_flight = false;

		disk_io_job* j = m_completed_jobs.get_all();
		l.unlock();

		aux::array<disk_io_job*, 64> to_delete;
		int cnt = 0;

		while (j)
		{
			TORRENT_ASSERT(j->job_posted == true);
			TORRENT_ASSERT(j->callback_called == false);
//			DLOG("   callback: %s\n", job_name(j->action));
			disk_io_job* next = j->next;

#if TORRENT_USE_ASSERTS
			j->callback_called = true;
#endif
			j->call_callback();
			to_delete[cnt++] = j;
			j = next;
			if (cnt == int(to_delete.size()))
			{
				cnt = 0;
				free_jobs(to_delete.data(), int(to_delete.size()));
			}
		}

		if (cnt > 0) free_jobs(to_delete.data(), cnt);
	}
}
