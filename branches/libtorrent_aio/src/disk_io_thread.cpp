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

/*
	Disk queue elevator patch by Morten Husveit
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

#include "libtorrent/time.hpp"
#include "libtorrent/disk_buffer_pool.hpp"
#include "libtorrent/disk_io_job.hpp"
#include "libtorrent/alert_types.hpp"

#ifdef TORRENT_BSD
#include <sys/sysctl.h>
#endif

#if TORRENT_USE_RLIMIT
#include <sys/resource.h>
#endif

#define DEBUG_STORAGE 0

#define DLOG if (DEBUG_STORAGE) fprintf

namespace libtorrent
{

	bool same_sign(int a, int b) { return ((a < 0) == (b < 0)) || (a == 0) || (b == 0); }

	bool between(size_type v, size_type b1, size_type b2)
	{
		return (b2 <= b1 && v <= b1 && v >= b2)
			|| (b2 >= b1 && v >= b1 && v <= b2);
	}

	bool elevator_ordered(size_type v, size_type next, size_type prev, int elevator)
	{
		// if the point is in between prev and next, we should always sort it in
		// between them, i.e. we're in the right place.
		if (between(v, prev, next)) return true;

		// if the point is in the elevator direction from prev (and not
		// in between prev and next) and the next point is not in the
		// elevator direction, we've found the right spot as well
		if (same_sign(v - prev, elevator) && !same_sign(next - prev, elevator)) return true;

		// otherwise we need to keep iterating forward looking for the
		// right insertion point
		return false;
	}

	// free function to prepend a chain of aios to a list
	// elevator direction determines how the new items are sorted
	// if it's 0, they are just prepended without any insertion sort
	// if it's -1, the direction from the first element is going down
	// towards lower offsets. If the element being inserted is higher,
	// it's inserted close to the end where the elevator has turned back.
	// if it's lower it's inserted early, as the offset would pass it.
	// a positive elevator direction has the same semantics but oposite order
	void prepend_aios(file::aiocb_t*& list, file::aiocb_t* aios, int elevator_direction)
	{
		if (aios == 0) return;
		if (elevator_direction == 0)
		{
			file::aiocb_t* last = aios;
			while (last->next) last = last->next;
			last->next = list;
			list = aios;
			return;
		}

		// insert each aio ordered by phys_offset
		// according to elevator_direction
		while (aios)
		{
			// pop the first element from aios into i
			file::aiocb_t* i = aios;
			aios = aios->next;
			i->next = 0;

			// find the right place in the current list to insert i
			// since the local elevator direction may change during
			// this scan, use a local copy
			// we want the ordering to look something like this:
			//
			// \            or like this:      ^
			//  \         (depending on the   /  \
			//   \   /     elevator          /    \
			//    \ /      direction)       /
			//     V                       /
			//
			// the knee is where the elevator direction changes. We never
			// want to insert an element before the first one, since that
			// might make the drive head move backwards
			int elevator = elevator_direction;
			file::aiocb_t** last = &list;
			file::aiocb_t* j = list;
			size_type last_offset = j ? j->phys_offset : 0;
			// this will keep iterating as long as j->phys_offset < i->phys_offset
			// for negative elevator dir, and as long as j->phys_offset > i->phys_offset
			// for positive elevator dir.
			// never insert in front of the first element (j == list), since
			// that's the one that determines where the current head is
			while (j
				&& (!elevator_ordered(i->phys_offset, j->phys_offset, last_offset, elevator)
					|| j == list))
			{
				if (!same_sign(j->phys_offset - last_offset, elevator))
				{
					// the elevator direction changed
					elevator *= -1;
				}

				last_offset = j->phys_offset;
				last = &j->next;
				j = j->next;
			}
			*last = i;
			i->next = j;
		}
	}

#if TORRENT_USE_AIO
	// global pointer to the disk_io_thread
	// so it can be accessed from within the signal handler
	// TODO: could this be a TLS pointer? That would make it
	// less intrusive when running multiple instances of
	// libtorrent
	disk_io_thread* g_disk_io_thread = 0;
#endif

// ------- disk_io_thread ------

	disk_io_thread::disk_io_thread(io_service& ios
		, boost::function<void(alert*)> const& post_alert
		, int block_size)
		: disk_buffer_pool(block_size)
		, m_abort(false)
		, m_queue_buffer_size(0)
		, m_last_file_check(time_now_hires())
		, m_file_pool(40)
		, m_disk_cache(*this)
		, m_write_calls(0)
		, m_read_calls(0)
		, m_write_blocks(0)
		, m_read_blocks(0)
		, m_in_progress(0)
		, m_to_issue(0)
		, m_outstanding_jobs(0)
//		, m_elevator_job_pos(m_deferred_jobs.begin())
//		, m_invalid_elevator_pos(false)
		, m_elevator_direction(1)
		, m_last_phys_off(0)
		, m_physical_ram(0)
		, m_ios(ios)
		, m_work(io_service::work(m_ios))
		, m_completed_aios(0)
		, m_post_alert(post_alert)
		, m_disk_io_thread(boost::bind(&disk_io_thread::thread_fun, this))
	{
#if TORRENT_USE_AIO
		g_disk_io_thread = this;
#endif

#ifdef TORRENT_DISK_STATS
		m_log.open("disk_io_thread.log", std::ios::trunc);
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

		// figure out how much physical RAM there is in
		// this machine. This is used for automatically
		// sizing the disk cache size when it's set to
		// automatic.
#ifdef TORRENT_BSD
		int mib[2] = { CTL_HW, HW_MEMSIZE };
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
	}

	disk_io_thread::~disk_io_thread()
	{
		DLOG(stderr, "destructing disk_io_thread [%p]\n", this);
#if TORRENT_USE_AIO
		sigset_t mask;
		sigemptyset(&mask);
		sigaddset(&mask, TORRENT_AIO_SIGNAL);

		if (pthread_sigmask(SIG_BLOCK, &mask, 0) == -1)
		{
			TORRENT_ASSERT(false);
		}
#endif

		TORRENT_ASSERT(m_abort == true);
	}

	void disk_io_thread::abort()
	{
		disk_io_job j;
		j.action = disk_io_job::abort_thread;
		add_job(j);
	}

	void disk_io_thread::join()
	{
		DLOG(stderr, "waiting for disk_io_thread [%p]\n", this);
		m_disk_io_thread.join();
		TORRENT_ASSERT(m_abort == true);
	}

	// aborts read operations
	void disk_io_thread::stop(boost::intrusive_ptr<piece_manager> s)
	{
		disk_io_job j;
		j.action = disk_io_job::abort_torrent;
		j.storage = s;
		add_job(j);
	}

	int disk_io_thread::try_flush(block_cache::iterator p, int limit)
	{
		DLOG(stderr, "[%p] try_flush: %d\n", this, p->piece);
		int start_of_run = 0;
		int i = 0;
		limit = (std::min)(limit, int(p->blocks_in_piece));
		int ret = 0;

		for (; i < p->blocks_in_piece; ++i)
		{
			if (p->blocks[i].dirty && !p->blocks[i].pending) continue;

			if (start_of_run == i
				|| i - start_of_run < limit)
			{
				start_of_run = i + 1;
				continue;
			}

			// we should flush start_of_run - i.
			ret += io_range(p, start_of_run, i, op_write);
			start_of_run = i + 1;
		}

		if (i - start_of_run >= limit)
		{
			// we should flush start_of_run - i.
			ret += io_range(p, start_of_run, i, op_write);
			start_of_run = i + 1;
		}
		return ret;
	}

	int disk_io_thread::io_range(block_cache::iterator p, int start, int end, int readwrite)
	{
		INVARIANT_CHECK;

		DLOG(stderr, "[%p] io_range: readwrite=%d piece=%d [%d, %d)\n", this, readwrite, p->piece, start, end);
		TORRENT_ASSERT(p != m_disk_cache.end());
		TORRENT_ASSERT(start >= 0);
		TORRENT_ASSERT(start < end);
		end = (std::min)(end, int(p->blocks_in_piece));

		int piece_size = p->storage->info()->piece_size(p->piece);
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " flushing " << piece_size << std::endl;
#endif
		TORRENT_ASSERT(piece_size > 0);
		
		int buffer_size = 0;

		file::iovec_t* iov = TORRENT_ALLOCA(file::iovec_t, p->blocks_in_piece);
		int iov_counter = 0;
		int ret = 0;

		end = (std::min)(end, int(p->blocks_in_piece));

		// the termination condition is deliberately <= end here
		// so that we get one extra loop where we can issue the last
		// async write operation
		for (int i = start; i <= end; ++i)
		{
			// don't flush blocks that are empty (buf == 0), not dirty
			// (read cache blocks), or pending (already being written)
			if (i == end
				|| p->blocks[i].buf == 0
				// if we're writing and the block is already pending, it
				// means we're already writing it, skip it!
				|| p->blocks[i].pending
				|| (!p->blocks[i].dirty && readwrite == op_write)
				|| (!p->blocks[i].uninitialized && readwrite == op_read))
			{
				DLOG(stderr, "[%p] io_range: skipping block=%d end: %d buf=%p pending=%d dirty=%d\n"
					, this, i, end, p->blocks[i].buf, p->blocks[i].pending, p->blocks[i].dirty);
				if (buffer_size == 0) continue;

				TORRENT_ASSERT(buffer_size <= i * m_block_size);
				int to_write = (std::min)(i * m_block_size, piece_size) - buffer_size;
				int range_start = i - (buffer_size + m_block_size - 1) / m_block_size;
				if (readwrite == op_write)
				{
					DLOG(stderr, "[%p] io_range: write piece=%d start_block=%d end_block=%d\n"
						, this, p->piece, range_start, i);
					m_queue_buffer_size += to_write;
					file::aiocb_t* aios = p->storage->write_async_impl(iov
						, p->piece, to_write, iov_counter
						, boost::bind(&disk_io_thread::on_disk_write, this, p
							, range_start, i, to_write, _1));
					m_write_blocks += i - range_start;
					++m_write_calls;
					DLOG(stderr, "prepending aios (%p) from write_async_impl to "
						"m_to_issue (%p) elevator=%d\n"
						, aios, m_to_issue, m_elevator_direction);

					prepend_aios(m_to_issue, aios, m_settings.allow_reordered_disk_operations
						? m_elevator_direction : 0);

					for (file::aiocb_t* j = m_to_issue; j; j = j->next)
						DLOG(stderr, "  %"PRId64, j->phys_offset);
					DLOG(stderr, "\n");
				}
				else
				{
					DLOG(stderr, "[%p] io_range: read piece=%d start_block=%d end_block=%d\n"
						, this, p->piece, range_start, i);
					++m_outstanding_jobs;
					file::aiocb_t* aios = p->storage->read_async_impl(iov, p->piece
						, range_start * m_block_size, iov_counter
						, boost::bind(&disk_io_thread::on_disk_read, this, p
							, range_start, i, _1));
					m_read_blocks += i - range_start;
					++m_read_calls;
					DLOG(stderr, "prepending aios (%p) from read_async_impl to m_to_issue (%p)\n"
						, aios, m_to_issue);

					prepend_aios(m_to_issue, aios, m_settings.allow_reordered_disk_operations
						? m_elevator_direction : 0);

					for (file::aiocb_t* j = m_to_issue; j; j = j->next)
						DLOG(stderr, "  %"PRId64, j->phys_offset);
					DLOG(stderr, "\n");
				}
				iov_counter = 0;
				buffer_size = 0;
				continue;
			}
			int block_size = (std::min)(piece_size - i * m_block_size, m_block_size);
			iov[iov_counter].iov_base = p->blocks[i].buf;
			iov[iov_counter].iov_len = block_size;
#ifdef TORRENT_DEBUG
			if (readwrite == op_write)
				TORRENT_ASSERT(p->blocks[i].dirty == true);
			else
				TORRENT_ASSERT(p->blocks[i].dirty == false);
#endif
			TORRENT_ASSERT(p->blocks[i].pending == false);
			p->blocks[i].uninitialized = false;
			TORRENT_ASSERT(!p->blocks[i].pending);
			if (!p->blocks[i].pending)
			{
				p->blocks[i].pending = true;
				TORRENT_ASSERT(p->blocks[i].refcount == 0);
				++p->blocks[i].refcount;
				TORRENT_ASSERT(p->blocks[i].refcount == 1);
				++const_cast<block_cache::cached_piece_entry&>(*p).refcount;
			}
			++iov_counter;
			++ret;
			buffer_size += block_size;
		}
		return ret;
	}

	void disk_io_thread::on_disk_write(block_cache::iterator p, int begin
		, int end, int to_write, error_code const& ec)
	{
		TORRENT_ASSERT(m_queue_buffer_size >= to_write);
		m_queue_buffer_size -= to_write;
		DLOG(stderr, "[%p] on_disk_write piece: %d start: %d end: %d\n", this, p->piece, begin, end);
		m_disk_cache.mark_as_done(p, begin, end, m_ios, m_queue_buffer_size, ec);
	}

	void disk_io_thread::on_disk_read(block_cache::iterator p, int begin
		, int end, error_code const& ec)
	{
		DLOG(stderr, "[%p] on_disk_read piece: %d start: %d end: %d\n", this, p->piece, begin, end);
		m_disk_cache.mark_as_done(p, begin, end, m_ios, m_queue_buffer_size, ec);

		TORRENT_ASSERT(m_outstanding_jobs > 0);
		--m_outstanding_jobs;
	}

	// returns the number of outstanding jobs on the pieces. If this is 0
	// it indicates that files can be closed without interrupting any operation
	int disk_io_thread::flush_cache(disk_io_job const& j, boost::uint32_t flags)
	{
		int ret = 0;

		void* storage = j.storage.get();

		std::pair<block_cache::iterator, block_cache::iterator> range;

		if (storage)
			range = m_disk_cache.pieces_for_storage(j.storage.get());
		else
			range = m_disk_cache.all_pieces();

		// range is now all of the pieces belonging to this storage.
		// iterate over all blocks and issue writes for the ones
		// that have dirty blocks (i.e. needs to be written)
		for (block_cache::iterator i = range.first; i != range.second;)
		{
			block_cache::iterator p = i++;
			TORRENT_ASSERT(storage == 0 || p->storage == j.storage);

			if (flags & flush_delete_cache)
			{
				// delete dirty blocks and post handlers with
				// operation_aborted error code
				m_disk_cache.abort_dirty(p, m_ios);
			}
			else if (flags & flush_write_cache && p->num_dirty > 0)
			{
				// issue write commands
				io_range(p, 0, INT_MAX, op_write);

				// if we're also flushing the read cache, this piece
				// should be removed as soon as all write jobs finishes
				// otherwise it will turn into a read piece
			}

			// we need to count read jobs as well
			// because we can't close files with
			// any outstanding jobs
			ret += p->jobs.size();

			// mark_for_deletion may erase the piece from the cache, that's
			// why we don't have the 'i' iterator referencing it at this point
			if (flags & (flush_read_cache | flush_delete_cache))
				m_disk_cache.mark_for_deletion(p);

		}
		return ret;
	}

	void disk_io_thread::uncork_jobs()
	{
	}

	void disk_io_thread::try_flush_write_blocks(int num)
	{
		DLOG(stderr, "[%p] try_flush_write_blocks: %d\n", this, num);

		std::pair<block_cache::lru_iterator, block_cache::lru_iterator> range
			= m_disk_cache.all_lru_pieces();

		// flush write cache in LRU order
		for (block_cache::lru_iterator p = range.first;
			p != range.second && num > 0; ++p)
		{
			if (p->num_dirty == 0) continue;

			try_flush(m_disk_cache.map_iterator(p), 1);
		}
	}

	typedef int (disk_io_thread::*disk_io_fun_t)(disk_io_job& j);

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
		&disk_io_thread::do_check_files,
		&disk_io_thread::do_save_resume_data,
		&disk_io_thread::do_rename_file,
		&disk_io_thread::do_abort_thread,
		&disk_io_thread::do_clear_read_cache,
		&disk_io_thread::do_abort_torrent,
		&disk_io_thread::do_update_settings,
		&disk_io_thread::do_read_and_hash,
		&disk_io_thread::do_cache_piece,
		&disk_io_thread::do_finalize_file,
		&disk_io_thread::do_get_cache_info,
	};

	static const char* job_action_name[] =
	{
		"read",
		"write",
		"hash",
		"move_storage",
		"release_files",
		"delete_files",
		"check_fastresume",
		"check_files",
		"save_resume_data",
		"rename_file",
		"abort_thread",
		"clear_read_cache",
		"abort_torrent",
		"update_settings",
		"read_and_hash",
		"cache_piece",
		"finalize_file",
		"get_cache_info"
	};

	void disk_io_thread::perform_async_job(disk_io_job j)
	{
		DLOG(stderr, "[%p] perform_async_job job: %s piece: %d offset: %d\n"
			, this, job_action_name[j.action], j.piece, j.offset);
		if (j.storage && j.storage->get_storage_impl()->m_settings == 0)
			j.storage->get_storage_impl()->m_settings = &m_settings;

		TORRENT_ASSERT(j.action >= 0 && j.action < sizeof(job_functions)/sizeof(job_functions[0]));

		// is the fence up for this storage?
		if (j.storage && j.storage->has_fence())
		{
			DLOG(stderr, "[%p]   perform_async_job: blocked\n", this);
			// Yes it is! We're not allowed
			// to issue this job. Queue it up
			m_blocked_jobs.push_back(j);
			return;
		}

		// call disk function
		int ret = (this->*(job_functions[j.action]))(j);

		DLOG(stderr, "[%p]   return: %d error: %s\n"
			, this, ret, j.error ? j.error.message().c_str() : "");

		j.outstanding_writes = m_queue_buffer_size;
		if (ret != defer_handler && j.callback)
		{
			DLOG(stderr, "[%p]   posting callback j.buffer: %p\n", this, j.buffer);
			m_ios.post(boost::bind(j.callback, ret, j));
		}

		// if this job actually completed (as opposed to deferred the handler)
		// and it's a job that raises the fence (like move storage, release
		// files, etc.), we may have to uncork the jobs that were blocked by it.
		if (ret != defer_handler && (j.flags & disk_io_job::need_uncork))
		{
			DLOG(stderr, "[%p]   uncorking\n", this);
			std::list<disk_io_job> jobs;
			m_blocked_jobs.swap(jobs);
			// we should only uncork if the storage doesn't
			// have a fence up anymore
			TORRENT_ASSERT(!j.storage->has_fence());

			while (!jobs.empty())
			{
				perform_async_job(jobs.front());
				jobs.pop_front();
			}
		}
	}

	int disk_io_thread::do_read(disk_io_job& j)
	{
#ifdef TORRENT_DISK_STATS
		m_log << log_time();
#endif
		DLOG(stderr, "[%p] do_read\n", this);
		INVARIANT_CHECK;

		TORRENT_ASSERT(j.buffer_size <= m_block_size);

		if (m_settings.use_read_cache && !m_settings.explicit_read_cache)
		{
			int ret = m_disk_cache.try_read(j);
			if (ret >= 0)
			{
				DLOG(stderr, "[%p] do_read: cache hit\n", this);
				j.flags |= disk_io_job::cache_hit;
#ifdef TORRENT_DISK_STATS
				m_log << " read-cache-hit " << j.buffer_size << std::endl;
#endif
				return ret;
			}
			else if (ret == -2)
			{
				j.error = error::no_memory;
				return disk_operation_failed;
			}

			// cache the piece, unless we're using an explicit cache
			if (!m_settings.explicit_read_cache)
			{
				block_cache::iterator p = m_disk_cache.allocate_piece(j);
				if (p != m_disk_cache.end())
				{
					int start_block = j.offset / m_block_size;
					int end_block = (std::min)(int(p->blocks_in_piece)
							, start_block + m_settings.read_cache_line_size);
					// this will also add the job to the pending job list in this piece
					// unless it fails and returns -1
					int ret = m_disk_cache.allocate_pending(p, start_block, end_block, j);
					DLOG(stderr, "[%p] do_read: allocate_pending ret=%d start_block=%d end_block=%d\n"
							, this, ret, start_block, end_block);

					if (ret > 0)
					{
						// some blocks were allocated
						io_range(p, start_block, end_block, op_read);

						DLOG(stderr, "[%p] do_read: cache miss\n", this);
#ifdef TORRENT_DISK_STATS
						m_log << " read " << j.buffer_size << std::endl;
#endif
						return defer_handler;
					}
					else if (ret == -1)
					{
						// allocation failed
						m_disk_cache.mark_for_deletion(p);
#ifdef TORRENT_DISK_STATS
						m_log << " read 0" << std::endl;
#endif
						j.buffer = 0;
						j.error = error::no_memory;
						j.str.clear();
						return disk_operation_failed;
					}

					// we get here if allocate_pending failed with
					// an error other than -1. This happens for instance
					// if the cache is full. Then fall through and issue the
					// read circumventing the cache

					m_disk_cache.mark_for_deletion(p);
				}
			}
		}

#ifdef TORRENT_DISK_STATS
		m_log << " read " << j.buffer_size << std::endl;
#endif

		j.buffer = allocate_buffer("send buffer");
		if (j.buffer == 0)
		{
			j.error = error::no_memory;
			return disk_operation_failed;
		}

		DLOG(stderr, "[%p] do_read: async\n", this);
		++m_outstanding_jobs;
		file::iovec_t b = { j.buffer, j.buffer_size };
		file::aiocb_t* aios = j.storage->read_async_impl(&b, j.piece, j.offset, 1
			, boost::bind(&disk_io_thread::on_read_one_buffer, this, _1, _2, j));
		DLOG(stderr, "prepending aios (%p) from read_async_impl to m_to_issue (%p)\n"
			, aios, m_to_issue);

		prepend_aios(m_to_issue, aios, m_settings.allow_reordered_disk_operations
			? m_elevator_direction : 0);

		for (file::aiocb_t* j = m_to_issue; j; j = j->next)
			DLOG(stderr, "  %"PRId64, j->phys_offset);
		DLOG(stderr, "\n");
		return defer_handler;
	}

	int disk_io_thread::do_write(disk_io_job& j)
	{
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " write " << j.buffer_size << std::endl;
#endif
		INVARIANT_CHECK;
		TORRENT_ASSERT(j.buffer != 0);
		TORRENT_ASSERT(j.buffer_size <= m_block_size);

		if (m_settings.cache_size > 0)
		{
			block_cache::iterator p = m_disk_cache.add_dirty_block(j);

			if (p != m_disk_cache.end())
			{
				// flushes the piece to disk in case
				// it satisfies the condition for a write
				// piece to be flushed
				try_flush(p, m_settings.write_cache_line_size);

				// if we have more blocks in the cache than allowed by
				// the cache size limit, flush some dirty blocks
				if (m_settings.cache_size <= m_disk_cache.size())
				{
					try_flush_write_blocks(m_settings.cache_size - m_disk_cache.size());
				}

				// the handler will be called when the block
				// is flushed to disk
				return defer_handler;
			}

			// #error this is a serious error, we should return ENOMEM
			TORRENT_ASSERT(false);
		}

		// #error this won't work with the hash operation, since it can't synchronize with when the blocks are written

		file::iovec_t b = { j.buffer, j.buffer_size };
		m_queue_buffer_size += j.buffer_size;
		file::aiocb_t* aios = j.storage->write_async_impl(&b, j.piece, j.offset, 1
			, boost::bind(&disk_io_thread::on_write_one_buffer, this, _1, _2, j));
		DLOG(stderr, "prepending aios (%p) from write_async_impl to m_to_issue (%p)\n"
			, aios, m_to_issue);

		prepend_aios(m_to_issue, aios, m_settings.allow_reordered_disk_operations
			? m_elevator_direction : 0);

		for (file::aiocb_t* j = m_to_issue; j; j = j->next)
			DLOG(stderr, "  %"PRId64, j->phys_offset);
		DLOG(stderr, "\n");
		return defer_handler;
	}

	int disk_io_thread::do_hash(disk_io_job& j)
	{
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " hash" << std::endl;
#endif
		INVARIANT_CHECK;

		block_cache::iterator p = m_disk_cache.find_piece(j);

		// flush the write jobs for this piece
		if (p != m_disk_cache.end() && p->num_dirty > 0)
		{
			// issue write commands
			io_range(p, 0, INT_MAX, op_write);
			block_cache::cached_piece_entry* pe = const_cast<block_cache::cached_piece_entry*>(&*p);
			pe->jobs.push_back(j);
			return defer_handler;
		}
		else
		{
			// #error merge this piece of code with what's in block_cache. The best way
			// to do this is probably to make the block cache call some function back
			// into the disk_io_thread for completed jobs and let the job take the same
			// path again but finish the second time instead of issuing async jobs
			if (m_settings.disable_hash_checks)
				return 0;

			// #error replace this with an asynchronous call which uses a worker thread
			// to do the hashing. This would make better use of parallel systems
			sha1_hash h = j.storage->hash_for_piece_impl(j.piece, j.error);
			if (j.error)
			{
				j.storage->mark_failed(j.piece);
				return disk_operation_failed;
			}

			int ret = (j.storage->info()->hash_for_piece(j.piece) == h)?0:-2;
			if (ret == -2) j.storage->mark_failed(j.piece);

			return ret;
		}
	}

	int disk_io_thread::do_move_storage(disk_io_job& j)
	{
		TORRENT_ASSERT(j.buffer == 0);
//#error do we have to close all files on windows?
		j.storage->move_storage_impl(j.str, j.error);
		if (!j.error) j.str = j.storage->save_path();
		return j.error ? disk_operation_failed : 0;
	}

	int disk_io_thread::do_release_files(disk_io_job& j)
	{
		TORRENT_ASSERT(j.buffer == 0);
		INVARIANT_CHECK;

		int ret = flush_cache(j, flush_write_cache);
		if (ret == 0)
		{
			// this means there are no outstanding requests
			// to this piece. We can go ahead and close the
			// files immediately without interfering with
			// any async operations
			j.storage->release_files_impl(j.error);
			return j.error ? disk_operation_failed : 0;
		}

//#error this fence would only have to block write operations and let read operations through
//#error maybe not. If blocks are reference counted, even read operation would force cache pieces to linger

		// raise the fence to block new async. operations
		j.flags |= disk_io_job::need_uncork;
		DLOG(stderr, "[%p] raising fence ret: %d\n", this, ret);
		j.storage->raise_fence(boost::bind(&disk_io_thread::perform_async_job, this, j));
		return defer_handler;
	}

	int disk_io_thread::do_delete_files(disk_io_job& j)
	{
		TORRENT_ASSERT(j.buffer == 0);
		INVARIANT_CHECK;

		int ret = flush_cache(j, flush_delete_cache);
		if (ret == 0)
		{
			// this means there are no outstanding requests
			// to this piece. We can go ahead and delete the
			// files immediately without interfering with
			// any async operations
			j.storage->delete_files_impl(j.error);
			return j.error ? disk_operation_failed : 0;
		}

		// raise the fence to block new async. operations
		j.flags |= disk_io_job::need_uncork;
		DLOG(stderr, "[%p] raising fence ret: %d\n", this, ret);
		j.storage->raise_fence(boost::bind(&disk_io_thread::perform_async_job, this, j));
		return defer_handler;
	}

	int disk_io_thread::do_check_fastresume(disk_io_job& j)
	{
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " check_fastresume" << std::endl;
#endif
		lazy_entry const* rd = (lazy_entry const*)j.buffer;
		TORRENT_ASSERT(rd != 0);
		return j.storage->check_fastresume(*rd, j.error);
	}

	int disk_io_thread::do_check_files(disk_io_job& j)
	{
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " check_files" << std::endl;
#endif
		int piece_size = j.storage->info()->piece_length();
		int ret = 0;
		for (int processed = 0; processed < 4 * 1024 * 1024; processed += piece_size)
		{
			ptime now = time_now_hires();
			TORRENT_ASSERT(now >= m_last_file_check);
			if (now - m_last_file_check < milliseconds(m_settings.file_checks_delay_per_block))
			{
				int sleep_time = m_settings.file_checks_delay_per_block
					* (piece_size / (16 * 1024))
					- total_milliseconds(now - m_last_file_check);
				if (sleep_time < 0) sleep_time = 0;
				TORRENT_ASSERT(sleep_time < 5 * 1000);

				sleep(sleep_time);
			}
			m_last_file_check = time_now_hires();

			if (m_abort)
			{
				j.error = error::operation_aborted;
				return disk_operation_failed;
			}

			// #error it would be nice to check files with async operations too
			ret = j.storage->check_files(j.piece, j.offset, j.error);
			DLOG(stderr, "check_files() ret=%d j.piece=%d j.offset=%d j.error=%s\n"
				, ret, j.piece, j.offset, j.error.message().c_str());

			if (j.error) return disk_operation_failed;

			if (ret == piece_manager::need_full_check && j.callback)
				m_ios.post(boost::bind(j.callback, ret, j));
			if (ret != piece_manager::need_full_check)
				return ret;
		}

		// if the check is not done, add it at the end of the job queue
		if (ret == piece_manager::need_full_check)
		{
			// offset needs to be reset to 0 so that the disk
			// job sorting can be done correctly
			j.offset = 0;
			add_job(j);
			return defer_handler;
		}
		return ret;
	}
/*
	void disk_io_thread::on_check()
	{
	
	}
*/
	int disk_io_thread::do_save_resume_data(disk_io_job& j)
	{
		int ret = flush_cache(j, flush_write_cache);
		if (ret == 0)
		{
			// this means there are no outstanding requests
			// to this piece. We can go ahead and close the
			// files immediately without interfering with
			// any async operations
			j.resume_data.reset(new entry(entry::dictionary_t));
			j.storage->write_resume_data(*j.resume_data, j.error);
			return j.error ? disk_operation_failed : 0;
		}

//#error this fence would only have to block write operations and could let read operations through

		// raise the fence to block new
		j.flags |= disk_io_job::need_uncork;
		DLOG(stderr, "[%p] raising fence\n", this);
		j.storage->raise_fence(boost::bind(&disk_io_thread::perform_async_job, this, j));
		return defer_handler;
	}

	int disk_io_thread::do_rename_file(disk_io_job& j)
	{
		TORRENT_ASSERT(j.buffer == 0);
//#error do we have to close the file on windows?
		j.storage->rename_file_impl(j.piece, j.str, j.error);
		return j.error ? disk_operation_failed : 0;
	}

	int disk_io_thread::do_abort_thread(disk_io_job& j)
	{
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " abort_thread " << std::endl;
#endif

		// issue write commands for all dirty blocks
		// and clear all read jobs
		flush_cache(j, flush_read_cache | flush_write_cache);
		m_abort = true;

		// we're aborting. Cancel all jobs that are blocked or
		// have been deferred as well
		while (!m_blocked_jobs.empty())
		{
			disk_io_job& j = m_blocked_jobs.back();
//			TORRENT_ASSERT(!j.storage->has_fence());
			j.error = error::operation_aborted;
			m_ios.post(boost::bind(j.callback, -1, j));
			m_blocked_jobs.pop_back();
		}
/*
		for (deferred_jobs_t::iterator i = m_deferred_jobs.begin();
			i != m_deferred_jobs.end();)
		{
			disk_io_job& j = i->second;
			TORRENT_ASSERT(!j.storage->has_fence());
			j.error = error::operation_aborted;
			m_ios.post(boost::bind(j.callback, -1, j));
			if (m_elevator_job_pos == i) ++m_elevator_job_pos;
			m_deferred_jobs.erase(i++);
		}
*/
		// #error, if there is a storage that has a fence up
		// it's going to get left hanging here.

//		m_self_work.reset();
		return 0;
	}

	int disk_io_thread::do_clear_read_cache(disk_io_job& j)
	{
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " clear_read_cache " << std::endl;
#endif
		flush_cache(j, flush_read_cache);
		return 0;
	}

	int disk_io_thread::do_abort_torrent(disk_io_job& j)
	{
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " abort_torrent " << std::endl;
#endif

		// issue write commands for all dirty blocks
		// and clear all read jobs
		flush_cache(j, flush_read_cache | flush_write_cache);

		// we're aborting. Cancel all jobs that are blocked or
		// have been deferred as well
		for (std::list<disk_io_job>::iterator i = m_blocked_jobs.begin();
			i != m_blocked_jobs.end();)
		{
			if (i->storage != j.storage)
			{
				++i;
				continue;
			}

			disk_io_job& j = *i;
			j.error = error::operation_aborted;
			m_ios.post(boost::bind(j.callback, -1, j));
			i = m_blocked_jobs.erase(i);
		}
/*
		for (deferred_jobs_t::iterator i = m_deferred_jobs.begin();
			i != m_deferred_jobs.end();)
		{
			if (i->second.storage != j.storage)
			{
				++i;
				continue;
			}

			disk_io_job& k = i->second;
			k.error = error::operation_aborted;
			m_ios.post(boost::bind(k.callback, -1, k));
			if (m_elevator_job_pos == i) ++m_elevator_job_pos;
			m_deferred_jobs.erase(i++);
		}
*/
		release_memory();
		return 0;
	}

	int disk_io_thread::do_update_settings(disk_io_job& j)
	{
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " update_settings " << std::endl;
#endif
		TORRENT_ASSERT(j.buffer);
		session_settings const& s = *((session_settings*)j.buffer);
		TORRENT_ASSERT(s.cache_size >= 0);
		TORRENT_ASSERT(s.cache_expiry > 0);

#if defined TORRENT_WINDOWS
		if (m_settings.low_prio_disk != s.low_prio_disk)
		{
			m_file_pool.set_low_prio_io(s.low_prio_disk);
			// we need to close all files, since the prio
			// only takes affect when files are opened
			m_file_pool.release(0);
		}
#endif
		m_settings = s;
		m_file_pool.resize(m_settings.file_pool_size);
#if defined __APPLE__ && defined __MACH__ && MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
		setiopolicy_np(IOPOL_TYPE_DISK, IOPOL_SCOPE_THREAD
				, m_settings.low_prio_disk ? IOPOL_THROTTLE : IOPOL_DEFAULT);
#endif
		if (m_settings.cache_size == -1)
		{
			// the cache size is set to automatic. Make it
			// depend on the amount of physical RAM
			// if we don't know how much RAM we have, just set the
			// cache size to 16 MiB (1024 blocks)
			if (m_physical_ram == 0)
				m_settings.cache_size = 1024;
			else
				m_settings.cache_size = m_physical_ram / 8 / m_block_size;
		}

		m_disk_cache.set_max_size(m_settings.cache_size);
		if (m_disk_cache.size() > m_settings.cache_size)
			m_disk_cache.try_evict_blocks(m_disk_cache.size() - m_settings.cache_size, 0, m_disk_cache.end());

		return 0;
	}

	int disk_io_thread::do_read_and_hash(disk_io_job& j)
	{
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " read_and_hash " << j.buffer_size << std::endl;
#endif
		DLOG(stderr, "[%p] do_read_and_hash\n", this);
		INVARIANT_CHECK;
		TORRENT_ASSERT(j.buffer == 0);

		// read the entire piece and verify the piece hash
		// since we need to check the hash, this function
		// will ignore the cache size limit (at least for
		// reading and hashing, not for keeping it around)
		block_cache::iterator p = m_disk_cache.allocate_piece(j);
		if (p == m_disk_cache.end())
		{
			TORRENT_ASSERT(j.buffer == 0);
			j.error = error::no_memory;
			j.str.clear();
			return disk_operation_failed;
		}

		int ret = m_disk_cache.allocate_pending(p, 0, p->blocks_in_piece, j, 2);
		DLOG(stderr, "[%p] do_read_and_hash: allocate_pending ret=%d\n", this, ret);

		if (ret > 0)
		{
			// some blocks were allocated
			io_range(p, 0, p->blocks_in_piece, op_read);
			return defer_handler;
		}
		else if (ret == -1)
		{
			// allocation failed
			m_disk_cache.mark_for_deletion(p);
#ifdef TORRENT_DISK_STATS
			m_log << " read 0" << std::endl;
#endif
			TORRENT_ASSERT(j.buffer == 0);
			j.error = error::no_memory;
			j.str.clear();
			return disk_operation_failed;
		}
		else if (ret < -1)
		{
			m_disk_cache.mark_for_deletion(p);
			//#error handle the case where there wasn't enough cache space
		}

		// we get here if all the blocks we want are already
		// in the cache

		ret = m_disk_cache.try_read(j);
		if (ret == -2)
		{
			// allocation failed
			TORRENT_ASSERT(j.buffer == 0);
			j.error = error::no_memory;
			j.str.clear();
			return disk_operation_failed;
		}
		TORRENT_ASSERT(ret == j.buffer_size);
		j.flags |= disk_io_job::cache_hit;

#if TORRENT_DISK_STATS
		rename_buffer(j.buffer, "released send buffer");
#endif
		if (m_settings.disable_hash_checks) return ret;

		// #error do this in a hasher thread!
		hasher sha1;
		int size = j.storage->info()->piece_size(p->piece);
		for (int i = 0; i < p->blocks_in_piece; ++i)
		{
			TORRENT_ASSERT(size > 0);
			sha1.update(p->blocks[i].buf, (std::min)(m_block_size, size));
			size -= m_block_size;
		}
		sha1_hash h = sha1.final();
		ret = (j.storage->info()->hash_for_piece(j.piece) == h)?ret:-3;

		if (ret == -3)
		{
			j.storage->mark_failed(j.piece);
			j.error = errors::failed_hash_check;
			j.str.clear();
			free_buffer(j.buffer);
			j.buffer = 0;
		}
		return ret;
	}

	int disk_io_thread::do_cache_piece(disk_io_job& j)
	{
#ifdef TORRENT_DISK_STATS
		m_log << log_time() << " cache " << j.piece << std::endl;
#endif
		INVARIANT_CHECK;
		TORRENT_ASSERT(j.buffer == 0);

		block_cache::iterator p = m_disk_cache.allocate_piece(j);
		if (p == m_disk_cache.end())
		{
			j.error = errors::no_memory;
			return disk_operation_failed;
		}

		int ret = m_disk_cache.allocate_pending(p, 0, p->blocks_in_piece, j);
		if (ret > 0)
		{
			io_range(p, 0, INT_MAX, op_read);
			return defer_handler;
		}
		else if (ret == -1)
		{
			TORRENT_ASSERT(j.buffer == 0);
			free_buffer(j.buffer);
			j.buffer = 0;
			j.error = error::no_memory;
			j.str.clear();
			return disk_operation_failed;
		}
		// the piece is already in the cache
		return 0;
	}

	int disk_io_thread::do_finalize_file(disk_io_job& j)
	{
		j.storage->finalize_file(j.piece, j.error);
		return j.error ? disk_operation_failed : 0;
	}

	int disk_io_thread::do_get_cache_info(disk_io_job& j)
	{
		std::pair<block_cache::iterator, block_cache::iterator> range
			= m_disk_cache.pieces_for_storage(j.storage.get());

		cache_status* ret = (cache_status*)j.buffer;

		// #error add these stats
		ret->total_used_buffers = in_use();
//		ret->current_async_jobs = m_outstanding_jobs;
//		ret->elevator_turns= m_elevator_turns;
		ret->queued_bytes = m_queue_buffer_size;

		ret->average_queue_time = m_queue_time.mean();
		ret->average_read_time = m_read_time.mean();
		ret->job_queue_length = m_blocked_jobs.size();
		ret->blocks_written = m_write_blocks;
		ret->blocks_read = m_read_blocks;
		ret->writes = m_write_calls;
		ret->reads = m_read_calls;

		m_disk_cache.get_stats(ret);

		time_t now_time_t = time(0);
		ptime now = time_now();

		for (block_cache::iterator i = range.first; i != range.second; ++i)
		{
			ret->pieces.push_back(cached_piece_info());
			cached_piece_info& info = ret->pieces.back();
			info.piece = i->piece;
			info.last_use = now - seconds(now_time_t - i->expire);
			info.kind = i->num_dirty ? cached_piece_info::write_cache : cached_piece_info::read_cache;
			int blocks_in_piece = i->blocks_in_piece;
			info.blocks.resize(blocks_in_piece);
			for (int b = 0; b < blocks_in_piece; ++b)
				info.blocks[b] = i->blocks[b].buf != 0;
		}
		return 0;
	}

	void disk_io_thread::on_write_one_buffer(error_code const& ec, size_t bytes_transferred
		, disk_io_job j)
	{
		int ret = j.buffer_size;
		TORRENT_ASSERT(ec || bytes_transferred == j.buffer_size);

		TORRENT_ASSERT(m_queue_buffer_size >= j.buffer_size);
		m_queue_buffer_size -= j.buffer_size;

		DLOG(stderr, "[%p] on_write_one_buffer piece=%d offset=%d error=%s\n", this, j.piece, j.offset, ec.message().c_str());
		if (ec)
		{
			free_buffer(j.buffer);
			j.buffer = 0;
			j.error = ec;
			j.error_file.clear();
			j.str.clear();
			ret = -1;
		}

		++m_write_blocks;
		if (j.callback)
			m_ios.post(boost::bind(j.callback, ret, j));
	}

	void disk_io_thread::on_read_one_buffer(error_code const& ec, size_t bytes_transferred
		, disk_io_job j)
	{
		TORRENT_ASSERT(m_outstanding_jobs > 0);
		--m_outstanding_jobs;
		DLOG(stderr, "[%p] on_read_one_buffer piece=%d offset=%d error=%s\n", this, j.piece, j.offset, ec.message().c_str());
		int ret = j.buffer_size;
		j.error = ec;
		if (!ec && bytes_transferred != j.buffer_size)
			j.error = errors::file_too_short;

		if (j.error)
		{
			TORRENT_ASSERT(j.buffer == 0);
			j.error_file.clear();
			j.str.clear();
			ret = -1;
		}

		++m_read_blocks;
		if (j.callback)
			m_ios.post(boost::bind(j.callback, ret, j));
	}

	// This is sometimes called from an outside thread!
	void disk_io_thread::add_job(disk_io_job const& j)
	{
		TORRENT_ASSERT(!m_abort);
		// post a message to make sure perform_async_job always
		// is run in the disk thread
		mutex::scoped_lock l (m_job_mutex);
		m_queued_jobs.push_back(j);
		// wake up the disk thread to issue this new job
		m_job_sem.signal();
	}

#if TORRENT_USE_AIO

	void disk_io_thread::signal_handler(int signal, siginfo_t* si, void*)
	{
		if (signal != TORRENT_AIO_SIGNAL) return;
		if (g_disk_io_thread == 0) return;

		++g_disk_io_thread->m_completed_aios;
		// wake up the disk thread to
		// make it handle these completed jobs
		g_disk_io_thread->m_job_sem.signal();
	}
#endif

	void disk_io_thread::thread_fun()
	{
		m_disk_cache.set_max_size(m_settings.cache_size);

#if TORRENT_USE_AIO
		// if we have posix aio, assume we have pthreads as well
		sigset_t mask;
		sigemptyset(&mask);
		sigaddset(&mask, TORRENT_AIO_SIGNAL);

		if (pthread_sigmask(SIG_UNBLOCK, &mask, 0) == -1)
		{
			TORRENT_ASSERT(false);
		}

		struct sigaction sa;

		sa.sa_flags = SA_SIGINFO | SA_RESTART;
		sa.sa_sigaction = &disk_io_thread::signal_handler;
		sigemptyset(&sa.sa_mask);

		if (sigaction(TORRENT_AIO_SIGNAL, &sa, 0) == -1)
		{
			TORRENT_ASSERT(false);
		}
#endif
		int last_completed_aios = 0;

		do
		{
			DLOG(stderr, "sem_wait() [%p]\n", this);
			m_job_sem.wait();
			DLOG(stderr, "sem_wait() returned [%p]\n", this);

			// more jobs might complete as we go through
			// the list. In which case m_completed_aios
			// would have incremented again. It's incremented
			// in the aio signal handler
			int complete_aios = m_completed_aios;
			DLOG(stderr, "m_completed_aios %d last_completed_aios: %d\n", complete_aios, last_completed_aios);
			while (complete_aios != last_completed_aios)
			{
				// this needs to be atomic for the signal handler
				last_completed_aios = complete_aios;
				// go through all outstanding disk operations
				// and potentially dispatch ones that are complete
				DLOG(stderr, "reap in progress aios (%p)\n", m_in_progress);
				m_in_progress = reap_aios(m_in_progress);
				DLOG(stderr, "new in progress aios (%p)\n", m_in_progress);
				complete_aios = m_completed_aios;
			}

			// keep the mutex locked for as short as possible
			// while we swap out all the jobs in the queue
			// we can then go through the queue without having
			// to block the mutex
			std::list<disk_io_job> jobs;
			mutex::scoped_lock l(m_job_mutex);
			jobs.swap(m_queued_jobs);
			l.unlock();

			// go through list of newly submitted jobs
			// and perform the appropriate action
			while (!jobs.empty())
			{
				perform_async_job(jobs.front());
				jobs.pop_front();
			}

// #error add a setting to determine whether or not to sort jobs before issuing them

			// tell the kernel about the async disk I/O jobs we want to perform

			// if we're on a system that doesn't do async. I/O, we should only perform
			// one at a time in case new jobs are issued that should take priority (such
			// as asking for stats)
			if (m_to_issue)
			{
				if (!same_sign(m_to_issue->phys_offset - m_last_phys_off, m_elevator_direction))
					m_elevator_direction *= -1;

				m_last_phys_off = m_to_issue->phys_offset;

				DLOG(stderr, "issue aios (%p) phys_offset=%"PRId64" elevator=%d\n"
					, m_to_issue, m_to_issue->phys_offset, m_elevator_direction);
				file::aiocb_t* pending;
				boost::tie(pending, m_to_issue) = issue_aios(m_to_issue);
				DLOG(stderr, "prepend aios (%p) to m_in_progress (%p)\n", pending, m_in_progress);
				prepend_aios(m_in_progress, pending, 0);

#if TORRENT_USE_AIO || TORRENT_USE_OVERLAPPED
				if (m_to_issue)
				{
					// there were some jobs that couldn't be posted
					// the the kernel. This limits the performance of
					// the disk throughput, issue a performance warning
					m_ios.post(boost::bind(m_post_alert, new performance_alert(
						torrent_handle(), performance_alert::aio_limit_reached)));
				}
#endif
			}

			// now, we may have received the abort thread
			// message, and m_abort may have been set to
			// true, but we still need to wait for the outstanding
			// jobs, that's why we'll keep looping while m_in_progress
			// is has jobs in it as well
		
		} while (!m_abort || m_in_progress);

// #error assert there are no outstanding jobs in the cache

		// release the io_service to allow the run() call to return
		// we do this once we stop posting new callbacks to it.
		m_work.reset();
		DLOG(stderr, "exiting disk thread [%p]\n", this);
	}

#ifdef TORRENT_DEBUG
	void disk_io_thread::check_invariant() const
	{
	
	}
#endif
		
}

