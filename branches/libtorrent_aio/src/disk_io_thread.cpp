/*

Copyright (c) 2007, Arvid Norberg
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

#if TORRENT_USE_RLIMIT
#include <sys/resource.h>
#endif

#ifdef TORRENT_BSD
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#define DEBUG_DISK_THREAD 0

#define DLOG if (DEBUG_DISK_THREAD) fprintf

namespace libtorrent
{
	// this is posted to the network thread and run from there
	static void alert_callback(alert_dispatcher* disp, alert* a)
	{
		if (disp && disp->post_alert(a)) return;
		delete a;
	}

// ------- disk_io_thread ------

	disk_io_thread::disk_io_thread(io_service& ios
		, alert_dispatcher* alert_disp
		, void* userdata
		, int block_size)
		: m_num_threads(0)
		, m_num_running_threads(0)
		, m_userdata(userdata)
		, m_last_cache_expiry(min_time())
		, m_last_file_check(time_now_hires())
		, m_file_pool(40)
		, m_disk_cache(block_size, ios, alert_disp)
		, m_last_stats_flip(time_now())
		, m_outstanding_jobs(0)
		, m_physical_ram(0)
		, m_ios(ios)
		, m_num_blocked_jobs(0)
		, m_work(io_service::work(m_ios))
		, m_last_disk_aio_performance_warning(min_time())
		, m_post_alert(alert_disp)
	{
		m_disk_cache.set_settings(m_settings);

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

		// figure out how much physical RAM there is in
		// this machine. This is used for automatically
		// sizing the disk cache size when it's set to
		// automatic.
#ifdef TORRENT_BSD
#ifdef HW_MEMSIZE
		int mib[2] = { CTL_HW, HW_MEMSIZE };
#else
		// not entirely sure this sysctl supports 64
		// bit return values, but it's probably better
		// than not building
		int mib[2] = { CTL_HW, HW_PHYSMEM };
#endif
		size_t len = sizeof(m_physical_ram);
		if (sysctl(mib, 2, &m_physical_ram, &len, NULL, 0) != 0)
			m_physical_ram = 0;
#elif defined TORRENT_WINDOWS
		MEMORYSTATUSEX ms;
		ms.dwLength = sizeof(MEMORYSTATUSEX);
		if (GlobalMemoryStatusEx(&ms))
			m_physical_ram = ms.ullTotalPhys;
		else
			m_physical_ram = 0;
#elif defined TORRENT_LINUX
		m_physical_ram = sysconf(_SC_PHYS_PAGES);
		m_physical_ram *= sysconf(_SC_PAGESIZE);
#elif defined TORRENT_AMIGA
		m_physical_ram = AvailMem(MEMF_PUBLIC);
#endif

#if TORRENT_USE_RLIMIT
		if (m_physical_ram > 0)
		{
			struct rlimit r;
			if (getrlimit(RLIMIT_AS, &r) == 0 && r.rlim_cur != RLIM_INFINITY)
			{
				if (m_physical_ram > r.rlim_cur)
					m_physical_ram = r.rlim_cur;
			}
		}
#endif

		set_num_threads(1);
	}

	disk_io_thread::~disk_io_thread()
	{
		DLOG(stderr, "[%p] destructing disk_io_thread\n", this);

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		// by now, all pieces should have been evicted
		std::pair<block_cache::iterator, block_cache::iterator> pieces
			= m_disk_cache.all_pieces();
		TORRENT_ASSERT(pieces.first == pieces.second);
#endif
	}

	void disk_io_thread::set_num_threads(int i, bool wait)
	{
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
			m_job_cond.signal_all(l);
			m_hash_job_cond.signal_all(l);
			l.unlock();
			if (wait) for (int i = m_num_threads; i < m_threads.size(); ++i) m_threads[i]->join();
			// this will detach the threads
			m_threads.resize(m_num_threads);
		}
	}

	void disk_io_thread::reclaim_block(block_cache_reference ref)
	{
		TORRENT_ASSERT(ref.storage);
		mutex::scoped_lock l(m_cache_mutex);
		m_disk_cache.reclaim_block(ref);
	}

	// disk_io_thread takes over ownership of the settings_pack
	void disk_io_thread::set_settings(settings_pack* pack)
	{
		mutex::scoped_lock l(m_cache_mutex);
		apply_pack(pack, m_settings);
		m_disk_cache.set_settings(m_settings);
	}

	// flush all blocks that are below p->hash.offset, since we've
	// already hashed those blocks, they won't cause any read-back
	int disk_io_thread::try_flush_hashed(cached_piece_entry* p, int cont_block
		, mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(cont_block > 0);
		if (p->hash == 0)
		{
			DLOG(stderr, "[%p] try_flush_hashed: (%d) no hash\n", this, int(p->piece));
			return 0;
		}

		// end is one past the end
		// round offset up to include the last block, which might
		// have an odd size
		int block_size = m_disk_cache.block_size();
		int end = (p->hash->offset + block_size - 1) / block_size;

		// nothing has been hashed yet, don't flush anything
		if (end == 0 && !p->need_readback) return 0;

		// the number of contiguous blocks we need to be allowed to flush
		cont_block = (std::min)(cont_block, int(p->blocks_in_piece));

		// if everything has been hashed, we might as well flush everything
		// regardless of the contiguous block restriction
		if (end == int(p->blocks_in_piece)) cont_block = 1;

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

		// we did not satisfy the cont_block requirement
		// i.e. too few blocks would be flushed at this point, put it off
		if (cont_block > num_blocks) return 0;

		DLOG(stderr, "[%p] try_flush_hashed: (%d) blocks_in_piece: %d end: %d\n"
			, this, int(p->piece), int(p->blocks_in_piece), end);

		return flush_range(p, 0, end, 0, l);
	}

	// issues write operations for blocks in the given
	// range on the given piece.
	int disk_io_thread::flush_range(cached_piece_entry* pe, int start, int end
		, int flags, mutex::scoped_lock& l)
	{
		INVARIANT_CHECK;

		DLOG(stderr, "[%p] flush_range: piece=%d [%d, %d)\n"
			, this, int(pe->piece), start, end);
		TORRENT_ASSERT(start >= 0);
		TORRENT_ASSERT(start < end);
		end = (std::min)(end, int(pe->blocks_in_piece));

		int piece_size = pe->storage->files()->piece_size(pe->piece);
		TORRENT_ASSERT(piece_size > 0);
		
		file::iovec_t* iov = TORRENT_ALLOCA(file::iovec_t, pe->blocks_in_piece);
		int iov_len = 0;
		// the blocks we're flushing
		int* flushing = TORRENT_ALLOCA(int, pe->blocks_in_piece);
		int num_flushing = 0;

#ifdef DEBUG_STORAGE
		DLOG(stderr, "[%p] flush_range: piece: %d [", this, int(pe->piece));
		for (int i = 0; i < start; ++i) DLOG(stderr, ".");
#endif

		int block_size = m_disk_cache.block_size();
		int size_left = piece_size;
		for (int i = start; i < end; ++i, size_left -= block_size)
		{
			TORRENT_ASSERT(size_left > 0);
			// don't flush blocks that are empty (buf == 0), not dirty
			// (read cache blocks), or pending (already being written)
			if (pe->blocks[i].buf == NULL
				|| pe->blocks[i].pending
				|| !pe->blocks[i].dirty)
			{
				DLOG(stderr, "-");
				continue;
			}

			flushing[num_flushing++] = i;
			iov[iov_len].iov_base = pe->blocks[i].buf;
			iov[iov_len].iov_len = (std::min)(block_size, size_left);
			++iov_len;
			pe->blocks[i].pending = true;
			m_disk_cache.inc_block_refcount(pe, i);

			DLOG(stderr, "x");
		}

		if (num_flushing == 0) return 0;

		l.unlock();

		ptime start_time = time_now_hires();

		// issue the actual write operation
		file::iovec_t* iov_start = iov;
		int flushing_start = 0;
		storage_error error;
		bool failed = false;
		for (int i = 1; i <= num_flushing; ++i)
		{
			if (i < num_flushing && flushing[i] == flushing[i-1]+1) continue;
			int ret = pe->storage->get_storage_impl()->writev(iov_start, i - flushing_start
				, pe->piece, flushing[flushing_start] * block_size, 0, error);
			if (ret < 0) failed = true;
			iov_start = &iov[i];
			flushing_start = i;
		}

		if (!failed)
		{
			boost::uint32_t write_time = total_microseconds(time_now_hires() - start_time);
			m_write_time.add_sample(write_time / num_flushing);
			m_cache_stats.cumulative_write_time += write_time;
			m_cache_stats.cumulative_job_time += write_time;
			m_cache_stats.blocks_written += num_flushing;
		}

		l.lock();

		m_disk_cache.blocks_flushed(pe, flushing, num_flushing);

		// if the cache is under high pressure, we need to evict
		// the blocks we just flushed to make room for more write pieces
		int evict = m_disk_cache.num_to_evict(0);
		if (evict > 0) m_disk_cache.try_evict_blocks(evict);

		tailqueue jobs;
		if (failed)
		{
			jobs.swap(pe->jobs);

			for (tailqueue_iterator i = jobs.iterate(); i .get(); i.next())
			{
				disk_io_job* j = (disk_io_job*)i.get();
				TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
				j->ret = -1;
				j->error = error;
			}
		}
		else
		{
			disk_io_job* j = (disk_io_job*)pe->jobs.get_all();
			while (j)
			{
				disk_io_job* next = (disk_io_job*)j->next;
				j->next = NULL;
				TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
				if (j->completed(pe, block_size))
				{
					j->ret = j->d.io.buffer_size;
					j->error = error;
					jobs.push_back(j);
				}
				else
					pe->jobs.push_back(j);
				j = next;
			}
		}
		add_completed_jobs(jobs);

		return num_flushing;
	}

	void disk_io_thread::abort_jobs(tailqueue& jobs_)
	{
		tailqueue jobs;
		jobs.swap(jobs_);
		for (tailqueue_iterator i = jobs.iterate(); i.get(); i.next())
		{
			disk_io_job* j = (disk_io_job*)i.get();
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
			j->ret = -1;
			j->error.ec = asio::error::operation_aborted;
		}
		if (jobs.size()) add_completed_jobs(jobs);
	}

	void disk_io_thread::flush_piece(cached_piece_entry* pe, int flags, mutex::scoped_lock& l)
	{
		if (flags & flush_delete_cache)
		{
			// delete dirty blocks and post handlers with
			// operation_aborted error code
			abort_jobs(pe->jobs);
			m_disk_cache.abort_dirty(pe);
		}
		else if ((flags & flush_write_cache) && pe->num_dirty > 0)
		{
			// issue write commands
			flush_range(pe, 0, INT_MAX, 0, l);

			// if we're also flushing the read cache, this piece
			// should be removed as soon as all write jobs finishes
			// otherwise it will turn into a read piece
		}

		// mark_for_deletion may erase the piece from the cache, that's
		// why we don't have the 'i' iterator referencing it at this point
		if (flags & (flush_read_cache | flush_delete_cache))
		{
			abort_jobs(pe->jobs);
			m_disk_cache.mark_for_deletion(pe);
		}
	}

	// returns the number of outstanding jobs on the pieces. If this is 0
	// it indicates that files can be closed without interrupting any operation
	void disk_io_thread::flush_cache(piece_manager* storage, boost::uint32_t flags
		, mutex::scoped_lock& l)
	{
		if (storage)
		{
			// iterate over all blocks and issue writes for the ones
			// that have dirty blocks (i.e. needs to be written)
			for (boost::unordered_set<cached_piece_entry*>::iterator i
				= storage->cached_pieces().begin(), end(storage->cached_pieces().end()); i != end;)
			{
				cached_piece_entry* pe = *i;
				++i;
				TORRENT_ASSERT(pe->storage == storage);
				flush_piece(pe, flags, l);
			}
		}
		else
		{
			std::pair<block_cache::iterator, block_cache::iterator> range = m_disk_cache.all_pieces();

			for (block_cache::iterator i = range.first; i != range.second;)
			{
				cached_piece_entry* pe = const_cast<cached_piece_entry*>(&*i);
				++i;
				flush_piece(pe, flags, l);
			}
		}
	}

	// this is called if we're exceeding (or about to exceed) the cache
	// size limit. This means we should not restrict ourselves to contiguous
	// blocks of write cache line size, but try to flush all old blocks
	// this is why we pass in 1 as cont_block to the flushing functions
	void disk_io_thread::try_flush_write_blocks(int num
		, mutex::scoped_lock& l)
	{
		DLOG(stderr, "[%p] try_flush_write_blocks: %d\n", this, num);

		list_iterator range = m_disk_cache.write_lru_pieces();

		for (list_iterator p = range; p.get() && num > 0; p.next())
		{
			cached_piece_entry* e = (cached_piece_entry*)p.get();
			if (e->num_dirty == 0) continue;

			++e->piece_refcount;
			kick_hasher(e, l);
			num -= try_flush_hashed(e, 1, l);

			--e->piece_refcount;
		}

		// when the write cache is under high pressure, it is likely
		// counter productive to actually do this, since a piece may
		// not have had its flush_hashed job run on it 

/*
		// if we still need to flush blocks, start over and flush
		// everything in LRU order (degrade to lru cache eviction)
		if (num > 0)
		{
			for (list_iterator p = range; p.get() && num > 0; p.next())
			{
				cached_piece_entry* e = (cached_piece_entry*)p.get();
				// don't flush blocks that are being hashed by another thread
				if (e->num_dirty == 0 || e->hashing) continue;

				num -= flush_range(e, 0, INT_MAX, 0, l);
			}
		}
 */
	}

	void disk_io_thread::flush_expired_write_blocks(mutex::scoped_lock& l)
	{
		DLOG(stderr, "[%p] flush_expired_write_blocks\n", this);

		ptime now = time_now();
		time_duration expiration_limit = seconds(m_settings.get_int(settings_pack::cache_expiry));

#ifdef TORRENT_DEBUG
		ptime timeout = min_time();
#endif

		for (list_iterator p = m_disk_cache.write_lru_pieces(); p.get(); p.next())
		{
			cached_piece_entry* e = (cached_piece_entry*)p.get();
#ifdef TORRENT_DEBUG
			TORRENT_ASSERT(e->expire >= timeout);
			timeout = e->expire;
#endif

			// since we're iterating in order of last use, if this piece
			// shouldn't be evicted, none of the following ones will either
			if (now - e->expire < expiration_limit) break;
			if (e->num_dirty == 0) continue;

			flush_range(e, 0, INT_MAX, 0, l);
		}
	}

	typedef int (disk_io_thread::*disk_io_fun_t)(disk_io_job* j);

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
		&disk_io_thread::do_finalize_file,
		&disk_io_thread::do_flush_piece,
		&disk_io_thread::do_flush_hashed,
		&disk_io_thread::do_flush_storage,
		&disk_io_thread::do_trim_cache,
		&disk_io_thread::do_file_priority,
		&disk_io_thread::do_load_torrent,
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
	};

	void disk_io_thread::perform_async_job(disk_io_job* j)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(j->next == 0);
		TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

		mutex::scoped_lock l(m_cache_mutex);

		int evict = m_disk_cache.num_to_evict(0);
		if (evict > 0)
		{
			evict = m_disk_cache.try_evict_blocks(evict);
			if (evict > 0) try_flush_write_blocks(evict, l);
		}

		l.unlock();

		DLOG(stderr, "[%p] perform_async_job job: %s ( %s%s) piece: %d offset: %d outstanding: %d\n"
			, this, job_action_name[j->action]
			, (j->flags & disk_io_job::fence) ? "fence ": ""
			, (j->flags & disk_io_job::force_copy) ? "force_copy ": ""
			, j->piece, j->d.io.offset
			, j->storage->num_outstanding_jobs());

		boost::intrusive_ptr<piece_manager> storage = j->storage;

		// TODO: instead of doing this. pass in the settings to each storage_interface
		// call. Each disk thread could hold its most recent understanding of the settings
		// in a shared_ptr, and update it every time it wakes up from a job. That way
		// each access to the settings won't require a mutex to be held.
		if (storage && storage->get_storage_impl()->m_settings == 0)
			storage->get_storage_impl()->m_settings = &m_settings;

		TORRENT_ASSERT(j->action < sizeof(job_functions)/sizeof(job_functions[0]));

		// TODO: hold disk_io_thread mutex here!
		if (time_now() > m_last_stats_flip + seconds(1)) flip_stats();

		ptime start_time = time_now_hires();

		++m_outstanding_jobs;

		// call disk function
		int ret = (this->*(job_functions[j->action]))(j);

		--m_outstanding_jobs;

		if (ret == retry_job)
		{
			mutex::scoped_lock l(m_job_mutex);
			// to avoid busy looping here, give up
			// our quanta in case there aren't any other
			// jobs to run in between

			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
	
			bool need_sleep = m_queued_jobs.empty();
			m_queued_jobs.push_back(j);
			l.unlock();
			if (need_sleep) sleep(0);
			return;
		}

		if (ret == defer_handler) return;
		j->ret = ret;

		ptime now = time_now_hires();
		m_job_time.add_sample(total_microseconds(now - start_time));

		DLOG(stderr, "[%p]   return: %d error: %s\n"
			, this, ret, j->error ? j->error.ec.message().c_str() : "");

		TORRENT_ASSERT(j->next == 0);
		DLOG(stderr, "[%p]   posting callback j->buffer: %p\n", this, j->buffer);

		add_completed_job(j);
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

		ptime start_time = time_now_hires();

		file::iovec_t b = { j->buffer, j->d.io.buffer_size };
   
		// the actual read operation
		int ret = j->storage->get_storage_impl()->readv(&b, 1
			, j->piece, j->d.io.offset, j->flags, j->error);
   
		if (!j->error.ec)
		{
			boost::uint32_t read_time = total_microseconds(time_now_hires() - start_time);
			m_read_time.add_sample(read_time);
			m_cache_stats.cumulative_read_time += read_time;
			m_cache_stats.cumulative_job_time += read_time;
			m_cache_stats.total_read_back += b.iov_len;
			++m_cache_stats.blocks_read;
		}
		return ret;
	}

	int disk_io_thread::do_read(disk_io_job* j)
	{
		if (!m_settings.get_bool(settings_pack::use_read_cache)
			|| m_settings.get_int(settings_pack::cache_size) == 0)
		{
			// we're not using a cache. This is the simple path
			// just read straight from the file
			return do_uncached_read(j);
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

		l.unlock();

		// then we'll actually allocate the buffers
		int ret = m_disk_cache.allocate_iovec(iov, iov_len);

		if (ret < 0) return do_uncached_read(j);

		// this is the offset that's aligned to block boundaries
		size_type adjusted_offset = j->d.io.offset & ~(block_size-1);

		// if this is the last piece, adjust the size of the
		// last buffer to match up
		iov[iov_len-1].iov_len = (std::min)(int(piece_size - adjusted_offset)
			- (iov_len-1) * block_size, block_size);
		TORRENT_ASSERT(iov[iov_len-1].iov_len > 0);

		// at this point, all the buffers are allocated and iov is initizalied
		// and the blocks have their refcounters incremented, so no other thread
		// can remove them. We can now release the cache mutex and dive into the
		// disk operations.

		ptime start_time = time_now_hires();

		ret = j->storage->get_storage_impl()->readv(iov, iov_len
			, j->piece, adjusted_offset, j->flags, j->error);

		if (!j->error.ec)
		{
			boost::uint32_t read_time = total_microseconds(time_now_hires() - start_time);
			m_read_time.add_sample(read_time / iov_len);
			m_cache_stats.cumulative_read_time += read_time;
			m_cache_stats.cumulative_job_time += read_time;
			m_cache_stats.blocks_read += iov_len;
		}

		l.lock();

		if (ret < 0)
		{
			// read failed. free buffers and return error
			m_disk_cache.free_iovec(iov, iov_len);
			return ret;
		}

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
			m_disk_cache.free_iovec(iov, iov_len);
			return -1;
		}

		int block = j->d.io.offset / block_size;
		m_disk_cache.insert_blocks(pe, block, iov, iov_len, j);

		int tmp = m_disk_cache.try_read(j);
		TORRENT_ASSERT(tmp >= 0);

		return j->d.io.buffer_size;
	}

	int disk_io_thread::do_uncached_write(disk_io_job* j)
	{
		ptime start_time = time_now_hires();

		file::iovec_t b = { j->buffer, j->d.io.buffer_size };
   
		// the actual write operation
		int ret = j->storage->get_storage_impl()->writev(&b, 1
			, j->piece, j->d.io.offset, j->flags, j->error);
   
		if (!j->error.ec)
		{
			boost::uint32_t write_time = total_microseconds(time_now_hires() - start_time);
			m_write_time.add_sample(write_time);
			m_cache_stats.cumulative_write_time += write_time;
			m_cache_stats.cumulative_job_time += write_time;
			++m_cache_stats.blocks_written;
		}

		m_disk_cache.free_buffer(j->buffer);
		j->buffer = NULL;

		return ret;
	}

	int disk_io_thread::do_write(disk_io_job* j)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(j->d.io.buffer_size <= m_disk_cache.block_size());
		int block_size = m_disk_cache.block_size();

		// should we put this write job in the cache?
		// if we don't use the cache we shouldn't.
		if (m_settings.get_bool(settings_pack::use_write_cache)
				&& m_settings.get_int(settings_pack::cache_size) > 0)
		{
			mutex::scoped_lock l(m_cache_mutex);

			cached_piece_entry* pe = m_disk_cache.add_dirty_block(j);

			if (pe)
			{
				if (pe->hash == 0 && !m_settings.get_bool(settings_pack::disable_hash_checks))
				{
					pe->hash = new partial_hash;
					m_disk_cache.update_cache_state(pe);
				}

				++pe->piece_refcount;

				// see if we can progress the hash cursor with this new block
				kick_hasher(pe, l);

				// flushes the piece to disk in case
				// it satisfies the condition for a write
				// piece to be flushed
				try_flush_hashed(pe, m_settings.get_int(settings_pack::write_cache_line_size), l);

				// if we have more blocks in the cache than allowed by
				// the cache size limit, flush some dirty blocks
				int current_size = m_disk_cache.in_use();
				if (m_settings.get_int(settings_pack::cache_size) <= current_size)
				{
					int left = current_size - m_settings.get_int(settings_pack::cache_size);
					left = m_disk_cache.try_evict_blocks(left);
					if (left > 0 && !m_settings.get_bool(settings_pack::dont_flush_write_cache))
						try_flush_write_blocks(left, l);
				}

				--pe->piece_refcount;
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

		TORRENT_ASSERT(r.length <= m_disk_cache.block_size());
		TORRENT_ASSERT(r.length <= 16 * 1024);

		int block_size = m_disk_cache.block_size();
		DLOG(stderr, "[%p] do_read piece: %d block: %d\n", this, r.piece, r.start / block_size);

		disk_io_job* j = allocate_job(disk_io_job::read);
		j->storage = storage;
		j->piece = r.piece;
		j->d.io.offset = r.start;
		j->d.io.buffer_size = r.length;
		j->buffer = 0;
		j->flags = flags;
		j->requester = requester;
		j->callback = handler;

		if (m_settings.get_bool(settings_pack::use_read_cache)
			&& m_settings.get_int(settings_pack::cache_size) > 0)
		{
			mutex::scoped_lock l(m_cache_mutex);
			int ret = m_disk_cache.try_read(j);
			if (ret >= 0)
			{
				l.unlock();
				DLOG(stderr, "[%p] do_read: cache hit\n", this);
				j->flags |= disk_io_job::cache_hit;
				j->ret = ret;
				handler(j);
				free_job(j);
				return;
			}
			else if (ret == -2)
			{
				l.unlock();
				j->error.ec = error::no_memory;
				j->error.operation = storage_error::alloc_cache_piece;
				j->ret = disk_io_job::operation_failed;
				handler(j);
				free_job(j);
				return;
			}
		}

		add_job(j);
	}

	void disk_io_thread::async_cache_piece(piece_manager* storage, int piece
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		disk_io_job* j = allocate_job(disk_io_job::cache_piece);
		j->storage = storage;
		j->piece = piece;
		j->callback = handler;

		add_job(j);
	}

	void disk_io_thread::async_write(piece_manager* storage, peer_request const& r
		, disk_buffer_holder& buffer
		, boost::function<void(disk_io_job const*)> const& handler
		, int flags)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(r.length <= m_disk_cache.block_size());
		TORRENT_ASSERT(r.length <= 16 * 1024);

		disk_io_job* j = allocate_job(disk_io_job::write);
		j->storage = storage;
		j->piece = r.piece;
		j->d.io.offset = r.start;
		j->d.io.buffer_size = r.length;
		j->buffer = buffer.get();
		j->callback = handler;
		j->flags = flags;

#if (defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS) && defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		mutex::scoped_lock l2_(m_cache_mutex);
		std::pair<block_cache::iterator, block_cache::iterator> range = m_disk_cache.all_pieces();
		for (block_cache::iterator i = range.first; i != range.second; ++i)
		{
			cached_piece_entry const& p = *i;
			int bs = m_disk_cache.block_size();
			int piece_size = p.storage->files()->piece_size(p.piece);
			int blocks_in_piece = (piece_size + bs - 1) / bs;
			for (int k = 0; k < blocks_in_piece; ++k)
				TORRENT_ASSERT(p.blocks[k].buf != j->buffer);
		}
		l2_.unlock();
#endif

#if !defined TORRENT_DISABLE_POOL_ALLOCATOR
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
				++m_num_blocked_jobs;
				DLOG(stderr, "[%p] blocked job: %s (torrent: %d total: %d)\n"
					, this, job_action_name[j->action], j->storage ? j->storage->num_blocked() : 0
					, int(m_num_blocked_jobs));
				buffer.release();
				return;
			}

			mutex::scoped_lock l(m_cache_mutex);
			cached_piece_entry* pe = m_disk_cache.add_dirty_block(j);
			l.unlock();

			if (pe)
			{
				buffer.release();

				// the block and write job were successfully inserted
				// into the cache. Now, see if we should trigger a flush
				j = allocate_job(disk_io_job::flush_hashed);
				j->storage = storage;
				j->piece = r.piece;
				j->flags = flags;
				add_job(j);
				return;
			}
		}

		add_job(j);
		buffer.release();
	}

	void disk_io_thread::async_hash(piece_manager* storage, int piece, int flags
		, boost::function<void(disk_io_job const*)> const& handler, void* requester)
	{
		disk_io_job* j = allocate_job(disk_io_job::hash);
		j->storage = storage;
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
	
			l.unlock();
			handler(j);
			free_job(j);
			return;
		}

		add_job(j);
	}

	void disk_io_thread::async_move_storage(piece_manager* storage, std::string const& p
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		disk_io_job* j = allocate_job(disk_io_job::move_storage);
		j->storage = storage;
		j->buffer = strdup(p.c_str());
		j->callback = handler;

		add_fence_job(storage, j);
	}

	void disk_io_thread::async_release_files(piece_manager* storage
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		disk_io_job* j = allocate_job(disk_io_job::release_files);
		j->storage = storage;
		j->callback = handler;

		add_fence_job(storage, j);
	}

	void disk_io_thread::async_check_fastresume(piece_manager* storage
		, lazy_entry const* resume_data
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		TORRENT_ASSERT(resume_data != 0);
		disk_io_job* j = allocate_job(disk_io_job::check_fastresume);
		j->storage = storage;
		j->buffer = (char*)resume_data;
		j->callback = handler;

		add_fence_job(storage, j);
	}

	void disk_io_thread::async_set_file_priority(piece_manager* storage
		, std::vector<boost::uint8_t> const& prios
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		std::vector<boost::uint8_t>* p = new std::vector<boost::uint8_t>(prios);

		disk_io_job* j = allocate_job(disk_io_job::file_priority);
		j->storage = storage;
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

	void disk_io_thread::async_save_resume_data(piece_manager* storage
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		disk_io_job* j = allocate_job(disk_io_job::save_resume_data);
		j->storage = storage;
		j->buffer = NULL;
		j->callback = handler;

		add_fence_job(storage, j);
	}

	void disk_io_thread::async_finalize_file(piece_manager* storage, int file
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		disk_io_job* j = allocate_job(disk_io_job::finalize_file);
		j->storage = storage;
		j->piece = file;
		j->callback = handler;

		add_job(j);
	}

	void disk_io_thread::async_flush_piece(piece_manager* storage, int piece
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		disk_io_job* j = allocate_job(disk_io_job::flush_piece);
		j->storage = storage;
		j->piece = piece;
		j->callback = handler;

		if (m_num_threads == 0)
		{
			j->error.ec = asio::error::operation_aborted;
			handler(j);
			free_job(j);
		}

		add_job(j);
	}

	void disk_io_thread::clear_read_cache(piece_manager* storage)
	{
		mutex::scoped_lock l(m_cache_mutex);

		boost::unordered_set<cached_piece_entry*> const& cache = storage->cached_pieces();
		for (boost::unordered_set<cached_piece_entry*>::const_iterator i = cache.begin()
			, end(cache.end()); i != end; ++i)
		{
			m_disk_cache.evict_piece(*(i++));
		}
	}

	void disk_io_thread::clear_piece(piece_manager* storage, int index)
	{
		mutex::scoped_lock l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(storage, index);
		if (pe == 0) return;

		m_disk_cache.evict_piece(pe);
	}

	void disk_io_thread::async_stop_torrent(piece_manager* storage
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		disk_io_job* j = allocate_job(disk_io_job::stop_torrent);
		j->storage = storage;
		j->callback = handler;
		add_fence_job(storage, j);
	}

	void disk_io_thread::async_rename_file(piece_manager* storage, int index, std::string const& name
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		disk_io_job* j = allocate_job(disk_io_job::rename_file);
		j->storage = storage;
		j->piece = index;
		j->buffer = strdup(name.c_str());
		j->callback = handler;
		add_fence_job(storage, j);
	}

	void disk_io_thread::async_delete_files(piece_manager* storage
		, boost::function<void(disk_io_job const*)> const& handler)
	{
		disk_io_job* j = allocate_job(disk_io_job::delete_files);
		j->storage = storage;
		j->callback = handler;
		add_fence_job(storage, j);
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
		TORRENT_ASSERT(ph->offset % block_size == 0);

		for (int i = cursor; i < pe->blocks_in_piece; ++i)
		{
			cached_block_entry& bl = pe->blocks[i];
			if (bl.buf == 0) break;
			m_disk_cache.inc_block_refcount(pe, i);
			++end;
		}

		// no blocks to hash?
		if (end == cursor) return;

		pe->hashing = 1;

		l.unlock();

		DLOG(stderr, "[%p] kick_hasher: %d - %d (piece: %d offset: %d)\n"
			, this, cursor, end, int(pe->piece), ph->offset);

		ptime start_time = time_now_hires();

		for (int i = cursor; i < end; ++i)
		{
			cached_block_entry& bl = pe->blocks[i];
			int size = (std::min)(block_size, piece_size - ph->offset);
			ph->h.update(bl.buf, size);
			ph->offset += size;
		}

		boost::uint64_t hash_time = total_microseconds(time_now_hires() - start_time);

		l.lock();

		TORRENT_ASSERT(pe->hashing);
		TORRENT_ASSERT(pe->hash);

		m_hash_time.add_sample(hash_time / (end - cursor));
		m_cache_stats.cumulative_hash_time += hash_time;
		m_cache_stats.cumulative_job_time += hash_time;

		pe->hashing = 0;

		// decrement the block refcounters
		for (int i = cursor; i < end; ++i)
			m_disk_cache.dec_block_refcount(pe, i);

		// did we complete the hash?
		if (pe->hash->offset != piece_size) return;

		// if there are any hash-jobs hanging off of this piece
		// we should post them now
		disk_io_job* j = (disk_io_job*)pe->jobs.get_all();
		tailqueue hash_jobs;
		while (j)
		{
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
			disk_io_job* next = (disk_io_job*)j->next;
			j->next = NULL;
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
			add_completed_jobs(hash_jobs);
		}
	}

	int disk_io_thread::do_uncached_hash(disk_io_job* j)
	{
		// we're not using a cache. This is the simple path
		// just read straight from the file

		int piece_size = j->storage->files()->piece_size(j->piece);
		int block_size = m_disk_cache.block_size();
		int blocks_in_piece = (piece_size + block_size - 1) / block_size;

		file::iovec_t iov;
		iov.iov_base = m_disk_cache.allocate_buffer("hashing");
		hasher h;
		int ret = 0;
		int offset = 0;
		for (int i = 0; i < blocks_in_piece; ++i)
		{
			DLOG(stderr, "[%p] do_hash: (uncached) reading (piece: %d block: %d)\n", this
				, int(j->piece), i);

			ptime start_time = time_now_hires();

			iov.iov_len = (std::min)(block_size, piece_size - offset);
			ret = j->storage->get_storage_impl()->readv(&iov, 1, j->piece
				, offset, j->flags, j->error);
			if (ret < 0) break;

			if (!j->error.ec)
			{
				boost::uint32_t read_time = total_microseconds(time_now_hires() - start_time);
				m_read_time.add_sample(read_time);
				m_cache_stats.cumulative_read_time += read_time;
				m_cache_stats.cumulative_job_time += read_time;
				++m_cache_stats.blocks_read;
			}

			offset += block_size;
			h.update((char const*)iov.iov_base, iov.iov_len);
		}

		m_disk_cache.free_buffer((char*)iov.iov_base);

		sha1_hash piece_hash = h.final();
		memcpy(j->d.piece_hash, &piece_hash[0], 20);
		return ret >= 0 ? 0 : -1;
	}

	int disk_io_thread::do_hash(disk_io_job* j)
	{
		INVARIANT_CHECK;

		if (m_settings.get_int(settings_pack::cache_size) == 0)
			return do_uncached_hash(j);

		int piece_size = j->storage->files()->piece_size(j->piece);

		mutex::scoped_lock l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe)
		{
			m_disk_cache.cache_hit(pe, j->requester, j->flags & disk_io_job::volatile_read);

			++pe->piece_refcount;
			kick_hasher(pe, l);
			--pe->piece_refcount;

			// are we already done hashing?
			if (pe->hash && !pe->hashing && pe->hash->offset == piece_size)
			{
				DLOG(stderr, "[%p] do_hash: (%d) (already done)\n", this, int(pe->piece));
				sha1_hash piece_hash = pe->hash->h.final();
				memcpy(j->d.piece_hash, &piece_hash[0], 20);
				delete pe->hash;
				pe->hash = NULL;
				m_disk_cache.update_cache_state(pe);
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
			TORRENT_ASSERT(pe->hash);
			// another thread is hashing this piece right now
			// try again in a little bit
			DLOG(stderr, "[%p] do_hash: retry\n", this);
			// TODO: we should probably just hang the job on the piece and make sure the hasher gets kicked
			return retry_job;
		}

		pe->hashing = 1;

		++pe->piece_refcount;

		if (pe->hash == NULL)
			pe->hash = new partial_hash;
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

		// TODO: it would be nice to not have to lock the mutex every
		// turn through this loop. i.e. increment the refcounts of all
		// blocks up front, and then hash them
		TORRENT_ASSERT(ph->offset % block_size == 0);
		for (int i = ph->offset / block_size; i < blocks_in_piece; ++i)
		{
			iov.iov_len = (std::min)(block_size, piece_size - ph->offset);

			// is the block already in the cache?
			if (pe->blocks[i].buf)
			{
				m_disk_cache.inc_block_refcount(pe, i);
				locked_blocks[num_locked_blocks++] = i;
			}
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
				TORRENT_ASSERT(pe->blocks[i].buf);
				TORRENT_ASSERT(ph->offset == i * block_size);
				ph->offset += iov.iov_len;
				ph->h.update(pe->blocks[i].buf, iov.iov_len);
			}
			else
			{
				iov.iov_base = m_disk_cache.allocate_buffer("hashing");

				if (iov.iov_base == NULL)
				{
					l.lock();
					//#error introduce a holder class that automatically increments and decrements the piece_refcount
					--pe->piece_refcount;
					pe->hashing = false;
					delete pe->hash;
					pe->hash = NULL;

					j->error.ec = errors::no_memory;
					j->error.operation = storage_error::alloc_cache_piece;
					return -1;
				}

				DLOG(stderr, "[%p] do_hash: reading (piece: %d block: %d)\n", this, int(pe->piece), i);

				ptime start_time = time_now_hires();

				TORRENT_ASSERT(ph->offset == i * block_size);
				ret = j->storage->get_storage_impl()->readv(&iov, 1, j->piece
						, ph->offset, j->flags, j->error);

				if (ret < 0)
				{
					m_disk_cache.free_buffer((char*)iov.iov_base);
					l.lock();
					break;
				}

				if (!j->error.ec)
				{
					boost::uint32_t read_time = total_microseconds(time_now_hires() - start_time);
					m_read_time.add_sample(read_time);
					m_cache_stats.cumulative_read_time += read_time;
					m_cache_stats.cumulative_job_time += read_time;
					m_cache_stats.total_read_back += iov.iov_len;
					++m_cache_stats.blocks_read;
				}

				TORRENT_ASSERT(ph->offset == i * block_size);
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
			m_disk_cache.dec_block_refcount(pe, locked_blocks[i]);

		--pe->piece_refcount;

		pe->hashing = 0;

		if (ret >= 0)
		{
			sha1_hash piece_hash = ph->h.final();
			memcpy(j->d.piece_hash, &piece_hash[0], 20);

			delete pe->hash;
			pe->hash = NULL;
			m_disk_cache.update_cache_state(pe);
		}
		return ret < 0 ? ret : 0;
	}

	int disk_io_thread::do_move_storage(disk_io_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// if files have to be closed, that's the storage's responsibility
		j->storage->get_storage_impl()->move_storage(j->buffer, j->error);
		return j->error ? -1 : 0;
	}

	int disk_io_thread::do_release_files(disk_io_job* j)
	{
		INVARIANT_CHECK;

		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		mutex::scoped_lock l(m_cache_mutex);
		flush_cache(j->storage.get(), flush_write_cache, l);
		l.unlock();

		j->storage->get_storage_impl()->release_files(j->error);
		return j->error ? -1 : 0;
	}

	int disk_io_thread::do_delete_files(disk_io_job* j)
	{
		TORRENT_ASSERT(j->buffer == 0);
		INVARIANT_CHECK;

		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		mutex::scoped_lock l(m_cache_mutex);
		flush_cache(j->storage.get(), flush_delete_cache, l);
		l.unlock();

		j->storage->get_storage_impl()->delete_files(j->error);
		return j->error ? -1 : 0;
	}

	int disk_io_thread::do_check_fastresume(disk_io_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		lazy_entry const* rd = (lazy_entry const*)j->buffer;
		TORRENT_ASSERT(rd != 0);

		return j->storage->check_fastresume(*rd, j->error);
	}

	int disk_io_thread::do_save_resume_data(disk_io_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		mutex::scoped_lock l(m_cache_mutex);
		flush_cache(j->storage.get(), flush_write_cache, l);
		l.unlock();

		entry* resume_data = new entry(entry::dictionary_t);
		j->storage->get_storage_impl()->write_resume_data(*resume_data, j->error);
		TORRENT_ASSERT(j->buffer == 0);
		j->buffer = (char*)resume_data;
		return j->error ? -1 : 0;
	}

	int disk_io_thread::do_rename_file(disk_io_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// if files need to be closed, that's the storage's responsibility
		j->storage->get_storage_impl()->rename_file(j->piece, j->buffer, j->error);
		return j->error ? -1 : 0;
	}

	int disk_io_thread::do_stop_torrent(disk_io_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// issue write commands for all dirty blocks
		// and clear all read jobs
		mutex::scoped_lock l(m_cache_mutex);
		flush_cache(j->storage.get(), flush_read_cache | flush_write_cache, l);
		m_disk_cache.release_memory();

		return 0;
	}

	int disk_io_thread::do_cache_piece(disk_io_job* j)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(j->buffer == 0);
		
		if (m_settings.get_int(settings_pack::cache_size) == 0
			|| m_settings.get_bool(settings_pack::use_read_cache) == false)
			return 0;


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
				j->error.ec = errors::no_memory;
				j->error.operation = storage_error::alloc_cache_piece;
				return -1;
			}

			DLOG(stderr, "[%p] do_cache_piece: reading (piece: %d block: %d)\n"
				, this, int(pe->piece), i);

			ptime start_time = time_now_hires();

			ret = j->storage->get_storage_impl()->readv(&iov, 1, j->piece
				, offset, j->flags, j->error);

			if (ret < 0)
			{
				l.lock();
				break;
			}

			if (!j->error.ec)
			{
				boost::uint32_t read_time = total_microseconds(time_now_hires() - start_time);
				m_read_time.add_sample(read_time);
				m_cache_stats.cumulative_read_time += read_time;
				m_cache_stats.cumulative_job_time += read_time;
				++m_cache_stats.blocks_read;
			}

			offset += block_size;

			l.lock();
			m_disk_cache.insert_blocks(pe, i, &iov, 1, j);
		}

		--pe->piece_refcount;
		return 0;
	}

	int disk_io_thread::do_finalize_file(disk_io_job* j)
	{
		j->storage->get_storage_impl()->finalize_file(j->piece, j->error);
		return j->error ? -1 : 0;
	}

	void disk_io_thread::flip_stats()
	{
		// calling mean() will actually reset the accumulators
		m_cache_stats.average_read_time = m_read_time.mean();
		m_cache_stats.average_write_time = m_write_time.mean();
		m_cache_stats.average_hash_time = m_hash_time.mean();
		m_cache_stats.average_job_time = m_job_time.mean();
		m_last_stats_flip = time_now();
	}

	void get_cache_info_impl(cached_piece_info& info, cached_piece_entry const* i, int block_size)
	{
		info.piece = i->piece;
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

	void disk_io_thread::get_cache_info(cache_status* ret, bool no_pieces
		, piece_manager const* storage)
	{
		mutex::scoped_lock l(m_cache_mutex);
		*ret = m_cache_stats;
		ret->total_used_buffers = m_disk_cache.in_use();
		ret->blocked_jobs = m_num_blocked_jobs;
		ret->queued_jobs = m_queued_jobs.size();
		ret->pending_jobs = m_outstanding_jobs;
		ret->num_jobs = jobs_in_use();
		ret->num_read_jobs = read_jobs_in_use();
		ret->num_write_jobs = write_jobs_in_use();

		m_disk_cache.get_stats(ret);

		if (no_pieces) return;

		int block_size = m_disk_cache.block_size();

		if (storage)
		{
			ret->pieces.reserve(storage->num_pieces());

			for (boost::unordered_set<cached_piece_entry*>::iterator i
				= storage->cached_pieces().begin(), end(storage->cached_pieces().end());
				i != end; ++i)
			{
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

	int disk_io_thread::do_flush_piece(disk_io_job* j)
	{
		mutex::scoped_lock l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);
		if (pe == NULL) return 0;

		flush_piece(pe, flush_write_cache, l);
		return 0;
	}

	// this is triggered every time we insert a new dirty block in a piece
	// by the time this gets executed, the block may already have been flushed
	// triggered by another mechanism.
	int disk_io_thread::do_flush_hashed(disk_io_job* j)
	{
		mutex::scoped_lock l(m_cache_mutex);

		cached_piece_entry* pe = m_disk_cache.find_piece(j);

		if (pe == NULL) return 0;
		if (pe->num_dirty == 0) return 0;
		if (pe->hash == 0 && !m_settings.get_bool(settings_pack::disable_hash_checks))
		{
			pe->hash = new partial_hash;
			m_disk_cache.update_cache_state(pe);
		}

		++pe->piece_refcount;

		// see if we can progress the hash cursor with this new block
		kick_hasher(pe, l);

		// flushes the piece to disk in case
		// it satisfies the condition for a write
		// piece to be flushed
		// #error if hash checks are disabled, always just flush
		try_flush_hashed(pe, m_settings.get_int(settings_pack::write_cache_line_size), l);

		// if we have more blocks in the cache than allowed by
		// the cache size limit, flush some dirty blocks
		int current_size = m_disk_cache.in_use();
		if (m_settings.get_int(settings_pack::cache_size) < current_size)
		{
			int left = current_size - m_settings.get_int(settings_pack::cache_size);
			left = m_disk_cache.try_evict_blocks(left);
			if (left > 0 && !m_settings.get_bool(settings_pack::dont_flush_write_cache))
				try_flush_write_blocks(left, l);
		}

		--pe->piece_refcount;
		return 0;
	}

	int disk_io_thread::do_flush_storage(disk_io_job* j)
	{
		mutex::scoped_lock l(m_cache_mutex);
		flush_cache(j->storage.get(), flush_write_cache, l);
		return 0;
	}

	int disk_io_thread::do_trim_cache(disk_io_job* j)
	{
//#error implement
		return 0;
	}

	int disk_io_thread::do_file_priority(disk_io_job* j)
	{
		std::vector<boost::uint8_t>* p = reinterpret_cast<std::vector<boost::uint8_t>*>(j->buffer);
		j->storage->get_storage_impl()->set_file_priority(*p, j->error);
		delete p;
		return 0;
	}

	int disk_io_thread::do_load_torrent(disk_io_job* j)
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
			j->buffer = (char*)t;
		}

		return 0;
	}

	void disk_io_thread::add_fence_job(piece_manager* storage, disk_io_job* j)
	{
		// if this happens, it means we started to shut down
		// the disk threads too early. We have to post all jobs
		// before the disk threads are shut down
		TORRENT_ASSERT(m_num_threads > 0);

		DLOG(stderr, "[%p] add_fence:job: %s (outstanding: %d)\n", this
			, job_action_name[j->action]
			, j->storage->num_outstanding_jobs());

		disk_io_job* fj = allocate_job(disk_io_job::flush_storage);
		fj->storage = j->storage;

		int ret = storage->raise_fence(j, fj, &m_num_blocked_jobs);
		if (ret == disk_job_fence::fence_post_fence)
		{
			mutex::scoped_lock l(m_job_mutex);
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
			m_queued_jobs.push_back(j);
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
			m_queued_jobs.push_back(fj);
		}
		else
		{
			TORRENT_ASSERT((fj->flags & disk_io_job::in_progress) == 0);
			TORRENT_ASSERT(fj->blocked);
		}
	}

	void disk_io_thread::add_job(disk_io_job* j, bool ignore_fence)
	{
		// if this happens, it means we started to shut down
		// the disk threads too early. We have to post all jobs
		// before the disk threads are shut down
		TORRENT_ASSERT(m_num_threads > 0);

		DLOG(stderr, "[%p] add_job: %s (ignore_fence: %d outstanding: %d)\n", this
			, job_action_name[j->action], ignore_fence
			, j->storage ? j->storage->num_outstanding_jobs() : 0);

		// is the fence up for this storage?
		// jobs that are instantaneous are not affected by the fence, is_blocked()
		// will take ownership of the job and queue it up, in case the fence is up
		// if the fence flag is set, this job just raised the fence on the storage
		// and should be scheduled
		if (j->storage && j->storage->is_blocked(j, ignore_fence))
		{
			++m_num_blocked_jobs;
			DLOG(stderr, "[%p] blocked job: %s (torrent: %d total: %d)\n"
				, this, job_action_name[j->action], j->storage ? j->storage->num_blocked() : 0
				, int(m_num_blocked_jobs));
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
			m_job_cond.signal_all(l);
		if (!m_queued_hash_jobs.empty())
			m_hash_job_cond.signal_all(l);
	}

	void disk_io_thread::thread_fun(int thread_id, thread_type_t type)
	{
		DLOG(stderr, "[%p] started disk thread %d\n", this, int(thread_id));

		++m_num_running_threads;

		mutex::scoped_lock l(m_job_mutex);
		for (;;)
		{
			
			disk_io_job* j = 0;
			if (type == generic_thread)
			{
				while (m_queued_jobs.empty() && thread_id < m_num_threads) m_job_cond.wait(l);

				// if the number of wanted thread is decreased,
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
				while (m_queued_hash_jobs.empty() && thread_id < m_num_threads) m_hash_job_cond.wait(l);
				if (m_queued_hash_jobs.empty() && thread_id >= m_num_threads) break;
				j = (disk_io_job*)m_queued_hash_jobs.pop_front();
			}

			l.unlock();

			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

			if (thread_id == 0)
			{
				// there's no need for all threads to be doing this
				ptime now = time_now_hires();
				if (now > m_last_cache_expiry + seconds(5))
				{
					mutex::scoped_lock l2(m_cache_mutex);
					DLOG(stderr, "[%p] blocked_jobs: %d queued_jobs: %d num_threads %d\n"
						, this, int(m_num_blocked_jobs), m_queued_jobs.size(), int(m_num_threads));
					m_last_cache_expiry = now;
					flush_expired_write_blocks(l2);
				}
			}

			perform_async_job(j);

			mutex::scoped_lock l2(m_cache_mutex);

			int evict = m_disk_cache.num_to_evict(0);
			if (evict > 0)
			{
				evict = m_disk_cache.try_evict_blocks(evict);
				if (evict > 0) try_flush_write_blocks(evict, l2);
			}

			l.lock();
		}

		// do cleanup in the last running thread 
		if (--m_num_running_threads > 0)
		{
			DLOG(stderr, "[%p] exiting disk thread %d. num_threads: %d\n", this, thread_id, int(m_num_threads));
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

		DLOG(stderr, "[%p] disk thread %d is the last one alive. cleaning up\n", this, thread_id);

		tailqueue jobs;

		m_disk_cache.clear(jobs);
		abort_jobs(jobs);

		// close all files. This may take a long
		// time on certain OSes (i.e. Mac OS)
		// that's why it's important to do this in
		// the disk thread in parallel with stopping
		// trackers.
		m_file_pool.release();

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		// by now, all pieces should have been evicted
		std::pair<block_cache::iterator, block_cache::iterator> pieces
			= m_disk_cache.all_pieces();
		TORRENT_ASSERT(pieces.first == pieces.second);
#endif
		// release the io_service to allow the run() call to return
		// we do this once we stop posting new callbacks to it.
		m_work.reset();
	}

	char* disk_io_thread::allocate_disk_buffer(bool& exceeded
		, disk_observer* o
		, char const* category)
	{
		bool trigger_trim = false;
		char* ret = m_disk_cache.allocate_buffer(exceeded, trigger_trim, o, category);
		if (trigger_trim)
		{
			// we just exceeded the cache size limit. Trigger a trim job
			disk_io_job* j = allocate_job(disk_io_job::trim_cache);
			add_job(j);
		}
		return ret;
	}

	void disk_io_thread::add_completed_job_impl(disk_io_job* j)
	{
		mutex::scoped_lock l(m_completed_jobs_mutex);

		bool need_post = m_completed_jobs.size() == 0;
		m_completed_jobs.push_back(j);
		l.unlock();

		if (need_post)
			m_ios.post(boost::bind(&disk_io_thread::call_job_handlers, this, m_userdata));
	}

	void disk_io_thread::add_completed_job(disk_io_job* j)
	{
		tailqueue tmp;
		tmp.push_back(j);
		add_completed_jobs(tmp);
	}

	void disk_io_thread::add_completed_jobs(tailqueue& jobs)
	{
		tailqueue new_jobs;
		int ret = 0;
		for (tailqueue_iterator i = jobs.iterate(); i.get(); i.next())
		{
			disk_io_job* j = (disk_io_job*)i.get();
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

			DLOG(stderr, "[%p] job_complete %s outstanding: %d\n", this
				, job_action_name[j->action], j->storage ? j->storage->num_outstanding_jobs() : 0);

			ret += j->storage ? j->storage->job_complete(j, new_jobs) : 0;
			TORRENT_ASSERT(ret == new_jobs.size());
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) == 0);
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			TORRENT_ASSERT(j->job_posted == false);
			j->job_posted = true;
#endif
		}

		if (ret) DLOG(stderr, "[%p] unblocked %d jobs (%d left)\n", this, ret
			, int(m_num_blocked_jobs) - ret);
		TORRENT_ASSERT(m_num_blocked_jobs >= ret);

		m_num_blocked_jobs -= ret;

		if (new_jobs.size() > 0)
		{
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			for (tailqueue_iterator i = new_jobs.iterate(); i.get(); i.next())
			{
				disk_io_job const* j = static_cast<disk_io_job const*>(i.get());
				TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
			}
#endif
			mutex::scoped_lock l(m_job_mutex);
			m_queued_jobs.append(new_jobs);
			m_job_cond.signal_all(l);
		}

		mutex::scoped_lock l(m_completed_jobs_mutex);

		bool need_post = m_completed_jobs.size() == 0;
		m_completed_jobs.append(jobs);
		l.unlock();

		if (need_post)
		{
			DLOG(stderr, "[%p] posting job handlers (%d)\n", this, m_completed_jobs.size());
			m_ios.post(boost::bind(&disk_io_thread::call_job_handlers, this, m_userdata));
		}
	}

	// This is run in the network thread
	void disk_io_thread::call_job_handlers(void* userdata)
	{
		mutex::scoped_lock l(m_completed_jobs_mutex);
		DLOG(stderr, "[%p] call_job_handlers (%d)\n", this, m_completed_jobs.size());
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
			DLOG(stderr, "[%p]   callback: %s\n", this, job_action_name[j->action]);
			disk_io_job* next = (disk_io_job*)j->next;

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			j->callback_called = true;
#endif
			if (j->callback) j->callback(j);
			to_delete.push_back(j);
			j = next;
		}

		free_jobs(&to_delete[0], to_delete.size());

		// uncork all peers who received a disk event. This is
		// to coalesce all the socket writes caused by the events.
		if (uncork) uncork->do_delayed_uncork();
	}

#ifdef TORRENT_DEBUG
	void disk_io_thread::check_invariant() const
	{
	}
#endif
		
}

