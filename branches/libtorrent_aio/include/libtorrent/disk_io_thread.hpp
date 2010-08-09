/*

Copyright (c) 2007-2010, Arvid Norberg
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

#ifndef TORRENT_DISK_IO_THREAD
#define TORRENT_DISK_IO_THREAD

#if defined TORRENT_DISK_STATS || defined TORRENT_STATS
#include <fstream>
#endif

#include "libtorrent/storage.hpp"
#include "libtorrent/allocator.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/sliding_average.hpp"
#include "libtorrent/disk_io_job.hpp"
#include "libtorrent/block_cache.hpp"
#include "libtorrent/file_pool.hpp"

#include <boost/function/function0.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_array.hpp>
#include <list>
#include "libtorrent/config.hpp"
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
#include <boost/pool/pool.hpp>
#endif
#include "libtorrent/session_settings.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/disk_buffer_pool.hpp"
#include "libtorrent/disk_io_job.hpp"

#include <boost/intrusive_ptr.hpp> // atomic_count

namespace libtorrent
{
	struct cached_piece_info
	{
		int piece;
		std::vector<bool> blocks;
		ptime last_use;
		enum kind_t { read_cache = 0, write_cache = 1 };
		kind_t kind;
	};
	
	struct cache_status
	{
		cache_status()
			: blocks_written(0)
			, writes(0)
			, blocks_read(0)
			, blocks_read_hit(0)
			, reads(0)
			, cache_size(0)
			, read_cache_size(0)
			, total_used_buffers(0)
			, average_queue_time(0)
			, average_read_time(0)
			, job_queue_length(0)
		{}

		std::vector<cached_piece_info> pieces;

		// the number of 16kB blocks written
		size_type blocks_written;
		// the number of write operations used
		size_type writes;
		// (blocks_written - writes) / blocks_written represents the
		// "cache hit" ratio in the write cache
		// the number of blocks read

		// the number of blocks passed back to the bittorrent engine
		size_type blocks_read;
		// the number of blocks that was just copied from the read cache
		size_type blocks_read_hit;
		// the number of read operations used
		size_type reads;

		mutable size_type queued_bytes;

		// the number of blocks in the cache (both read and write)
		int cache_size;

		// the number of blocks in the cache used for read cache
		int read_cache_size;

		// the total number of blocks that are currently in use
		// this includes send and receive buffers
		mutable int total_used_buffers;

		// times in microseconds
		int average_queue_time;
		int average_read_time;
		int job_queue_length;
	};
	
	// this is a singleton consisting of the thread and a queue
	// of disk io jobs
	struct TORRENT_EXPORT disk_io_thread : disk_buffer_pool
	{
		disk_io_thread(io_service& ios
			, int block_size = 16 * 1024);
		~disk_io_thread();

		void abort();
		void join();

		// aborts read operations
		void stop(boost::intrusive_ptr<piece_manager> s);
		void add_job(disk_io_job const& j);

		void thread_fun();

		file_pool& files() { return m_file_pool; }

		enum return_code_t
		{
			// the error is stored in disk_io_job::error
			disk_operation_failed = -1,
			// don't post the handler yet, this operation
			// is async and will be completed later
			defer_handler = -100
		};

		int do_read(disk_io_job& j);
		int do_write(disk_io_job& j);
		int do_hash(disk_io_job& j);
		int do_move_storage(disk_io_job& j);
		int do_release_files(disk_io_job& j);
		int do_delete_files(disk_io_job& j);
		int do_check_fastresume(disk_io_job& j);
		int do_check_files(disk_io_job& j);
		int do_save_resume_data(disk_io_job& j);
		int do_rename_file(disk_io_job& j);
		int do_abort_thread(disk_io_job& j);
		int do_clear_read_cache(disk_io_job& j);
		int do_abort_torrent(disk_io_job& j);
		int do_update_settings(disk_io_job& j);
		int do_read_and_hash(disk_io_job& j);
		int do_cache_piece(disk_io_job& j);
		int do_finalize_file(disk_io_job& j);
		int do_get_cache_info(disk_io_job& j);

#ifdef TORRENT_DEBUG
		void check_invariant() const;
#endif
		
	private:

		void perform_async_job(disk_io_job j);

		void uncork_jobs();
		void on_disk_write(block_cache::iterator p, int begin
			, int end, int to_write, error_code const& ec);
		void on_disk_read(block_cache::iterator p, int begin
			, int end, error_code const& ec);

		enum op_t
		{
			op_read = 0,
			op_write = 1
		};

		int io_range(block_cache::iterator p, int start, int end, int readwrite);

		enum flush_flags_t { flush_read_cache = 1, flush_write_cache = 2, flush_delete_cache = 4 };
		int flush_cache(disk_io_job const& j, boost::uint32_t flags);

		void on_write_one_buffer(error_code const& ec, size_t bytes_transferred
			, disk_io_job j);

		void on_read_one_buffer(error_code const& ec, size_t bytes_transferred
			, disk_io_job j);

		int try_flush(block_cache::iterator p);

		bool m_abort;

		// this is the number of bytes we're waiting for to be written
		size_type m_queue_buffer_size;

		ptime m_last_file_check;

		// LRU cache of open files
		file_pool m_file_pool;

		// disk cache
		block_cache m_disk_cache;

		// keeps average queue time for disk jobs (in microseconds)
		// #error both of these sliding windows should be moved to the main thread
		sliding_average<512> m_queue_time;

		// average read time for cache misses (in microseconds)
		sliding_average<512> m_read_time;

		// number of write operations issued
		boost::uint64_t m_write_calls;
		boost::uint64_t m_read_calls;
		boost::uint64_t m_write_blocks;
		boost::uint64_t m_read_blocks;

		// these are async I/O operations that have been issued
		// and we are waiting to complete
		file::aiocb_t* m_in_progress;

		// these are async operations that we've accumulated
		// during this round and will be issued
		file::aiocb_t* m_to_issue;

#ifdef TORRENT_DISK_STATS
		std::ofstream m_log;
#endif

#if TORRENT_USE_AIO
		static void signal_handler(int signal, siginfo_t* si, void*);
#endif

		// the total number of outstanding jobs. This is used to
		// limit the number of jobs issued in parallel. It also creates
		// an opportunity to sort the jobs by physical offset before
		// issued to the AIO subsystem
		int m_outstanding_jobs;

		// once we have reached our max number of outstanding jobs,
		// they start getting queued up in this list, sorted by their
		// physical offset on disk
//		typedef std::multimap<size_type, disk_io_job> deferred_jobs_t;
//		deferred_jobs_t m_deferred_jobs;

		// the iterator of the current position in the deferred jobs
		// list.
//		deferred_jobs_t::iterator m_elevator_job_pos;

		// this is set to true when the job pos iterator is
		// invalid. This happens every time the deferred jobs
		// list gets emptied
//		bool m_invalid_elevator_pos;

		// the direction of the elevator. -1 means down and
		// 1 means up
//		int m_elevator_direction;

		// the physical offset of the last job consumed out
		// of the deferred jobs list
//		size_type m_last_phys_off;

		// the amount of physical ram in the machine
		boost::uint64_t m_physical_ram;

		// this is the main thread io_service. Callbacks are
		// posted on this in order to have them execute in
		// the main thread.
		io_service& m_ios;

		// Jobs that are blocked by the fence are put in this
		// list. Each time a storage is taken out of the fence,
		// this list is gone through and jobs belonging to the
		// storage are issued.
		std::list<disk_io_job> m_blocked_jobs;

		// this keeps the io_service::run() call blocked from
		// returning. When shutting down, it's possible that
		// the event queue is drained before the disk_io_thread
		// has posted its last callback. When this happens, the
		// io_service will have a pending callback from the
		// disk_io_thread, but the event loop is not running.
		// this means that the event is destructed after the
		// disk_io_thread. If the event refers to a disk buffer
		// it will try to free it, but the buffer pool won't
		// exist anymore, and crash. This prevents that.
		boost::optional<io_service::work> m_work;

		std::list<disk_io_job> m_queued_jobs;

		// mutex to protect the m_queued_jobs list
		mutex m_job_mutex;

		// used to wake up the thread
		semaphore m_job_sem;

		// incremented in signal handler
		// for each job that's completed
		mutable boost::detail::atomic_count m_completed_aios;

		// thread for performing blocking disk io operations
		thread m_disk_io_thread;
	};

}

#endif

