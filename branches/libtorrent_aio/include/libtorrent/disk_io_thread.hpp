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
#include "libtorrent/disk_job_pool.hpp"
#include "libtorrent/block_cache.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/disk_interface.hpp"

#include <boost/function/function0.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_array.hpp>
#include <deque>
#include "libtorrent/config.hpp"
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
#include <boost/pool/pool.hpp>
#endif
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/thread.hpp"

#include <boost/intrusive_ptr.hpp> // atomic_count

namespace libtorrent
{
	class alert;
	struct alert_dispatcher;

	struct cached_piece_info
	{
		int piece;
		std::vector<bool> blocks;
		ptime last_use;
		bool need_readback;
		int next_to_hash;
		enum kind_t { read_cache = 0, write_cache = 1, volatile_read_cache = 2 };
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
			, queued_bytes(0)
#ifndef TORRENT_NO_DEPRECATE
			, cache_size(0)
#endif
			, write_cache_size(0)
			, read_cache_size(0)
			, pinned_blocks(0)
			, total_used_buffers(0)
			, average_read_time(0)
			, average_write_time(0)
			, average_hash_time(0)
			, average_job_time(0)
			, cumulative_job_time(0)
			, cumulative_read_time(0)
			, cumulative_write_time(0)
			, cumulative_hash_time(0)
			, total_read_back(0)
			, read_queue_size(0)
			, blocked_jobs(0)
			, queued_jobs(0)
			, peak_queued(0)
			, pending_jobs(0)
			, num_jobs(0)
			, num_read_jobs(0)
			, num_write_jobs(0)
			, arc_mru_size(0)
			, arc_mru_ghost_size(0)
			, arc_mfu_size(0)
			, arc_mfu_ghost_size(0)
			, arc_write_size(0)
			, arc_volatile_size(0)
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

		// the number of bytes queued for writing, including bytes
		// submitted to the OS for writing, but not yet complete
		mutable size_type queued_bytes;

#ifndef TORRENT_NO_DEPRECATE
		// this is the sum of write_cache_size and read_cache_size
		int cache_size;
#endif
		// the number of blocks in the cache used for write cache
		int write_cache_size;

		// the number of blocks in the cache used for read cache
		int read_cache_size;

		// the number of blocks with a refcount > 0, i.e.
		// they may not be evicted
		int pinned_blocks;

		// the total number of blocks that are currently in use
		// this includes send and receive buffers
		mutable int total_used_buffers;

		// times in microseconds
		int average_read_time;
		int average_write_time;
		int average_hash_time;
		int average_job_time;

		boost::uint32_t cumulative_job_time;
		boost::uint32_t cumulative_read_time;
		boost::uint32_t cumulative_write_time;
		boost::uint32_t cumulative_hash_time;

		// number of blocks we've read back from disk
		// because they were evicted before
		int total_read_back;
		int read_queue_size;
	
		// number of jobs blocked because of a fence
		int blocked_jobs;

		// number of jobs waiting to be issued (m_to_issue)
		// average over 30 seconds
		int queued_jobs;
		// largest ever seen number of queued jobs
		int peak_queued;
		// number of jobs waiting to complete (m_pending)
		// average over 30 seconds
		int pending_jobs;
		// largest ever seen number of pending jobs
		int peak_pending;

		// total number of disk job objects allocated right now
		int num_jobs;

		// total number of disk read job objects allocated right now
		int num_read_jobs;

		// total number of disk write job objects allocated right now
		int num_write_jobs;

		// ARC cache stats. All of these counters are in number of pieces
		// not blocks. A piece does not necessarily correspond to a certain
		// number of blocks. The pieces in the ghost list never have any
		// blocks in them
		int arc_mru_size;
		int arc_mru_ghost_size;
		int arc_mfu_size;
		int arc_mfu_ghost_size;
		int arc_write_size;
		int arc_volatile_size;
	};
	
	// this is a singleton consisting of the thread and a queue
	// of disk io jobs
	struct TORRENT_EXTRA_EXPORT disk_io_thread
		: disk_job_pool
		, disk_interface
		, buffer_allocator_interface
	{
		disk_io_thread(io_service& ios
			, alert_dispatcher* alert_disp
			, void* userdata
			, int block_size = 16 * 1024);
		~disk_io_thread();

		void set_settings(settings_pack* sett);
		void set_num_threads(int i, bool wait = true);

		void async_read(piece_manager* storage, peer_request const& r
			, boost::function<void(disk_io_job const*)> const& handler, void* requester
			, int flags = 0);
		void async_write(piece_manager* storage, peer_request const& r
			, disk_buffer_holder& buffer
			, boost::function<void(disk_io_job const*)> const& handler
			, int flags = 0);
		void async_hash(piece_manager* storage, int piece, int flags
			, boost::function<void(disk_io_job const*)> const& handler, void* requester);
		void async_move_storage(piece_manager* storage, std::string const& p
			, boost::function<void(disk_io_job const*)> const& handler);
		void async_release_files(piece_manager* storage
			, boost::function<void(disk_io_job const*)> const& handler
			= boost::function<void(disk_io_job const*)>());
		void async_check_fastresume(piece_manager* storage
			, lazy_entry const* resume_data
			, boost::function<void(disk_io_job const*)> const& handler);
		void async_finalize_file(piece_manager* storage, int file
			, boost::function<void(disk_io_job const*)> const& handler
			= boost::function<void(disk_io_job const*)>());
		void async_flush_piece(piece_manager* storage, int piece
			, boost::function<void(disk_io_job const*)> const& handler
			= boost::function<void(disk_io_job const*)>());
		void async_cache_piece(piece_manager* storage, int piece
			, boost::function<void(disk_io_job const*)> const& handler);
		void async_stop_torrent(piece_manager* storage
			, boost::function<void(disk_io_job const*)> const& handler);
		void async_rename_file(piece_manager* storage, int index, std::string const& name
			, boost::function<void(disk_io_job const*)> const& handler);
		void async_delete_files(piece_manager* storage
			, boost::function<void(disk_io_job const*)> const& handler);
		void async_save_resume_data(piece_manager* storage
			, boost::function<void(disk_io_job const*)> const& handler);
		void async_set_file_priority(piece_manager* storage
			, std::vector<boost::uint8_t> const& prio
			, boost::function<void(disk_io_job const*)> const& handler);

		void clear_read_cache(piece_manager* storage);
		void clear_piece(piece_manager* storage, int index);

		void subscribe_to_disk(disk_observer* o)
		{ m_disk_cache.subscribe_to_disk(o); }

		// implements buffer_allocator_interface
		void reclaim_block(block_cache_reference ref);
		void free_disk_buffer(char* buf) { m_disk_cache.free_buffer(buf); }
		char* allocate_disk_buffer(char const* category)
		{ return m_disk_cache.allocate_buffer(category); }
		char* allocate_disk_buffer(bool& exceeded, disk_observer* o
			, char const* category);

		bool exceeded_cache_use() const
		{ return m_disk_cache.exceeded_max_size(); }

		void get_cache_info(cache_status* ret, bool no_pieces = true
			, piece_manager const* storage = 0);

		// this submits all queued up jobs to the thread
		void submit_jobs();

		block_cache* cache() { return &m_disk_cache; }

		enum thread_type_t {
			generic_thread,
			hasher_thread
		};

		void thread_fun(int thread_id, thread_type_t type);

		file_pool& files() { return m_file_pool; }

		io_service& get_io_service() { return m_ios; }

#ifdef TORRENT_DEBUG
		void check_invariant() const;
#endif

		int do_read(disk_io_job* j);
		int do_uncached_read(disk_io_job* j);

		int do_write(disk_io_job* j);
		int do_uncached_write(disk_io_job* j);

		int do_hash(disk_io_job* j);
		int do_uncached_hash(disk_io_job* j);

		int do_move_storage(disk_io_job* j);
		int do_release_files(disk_io_job* j);
		int do_delete_files(disk_io_job* j);
		int do_check_fastresume(disk_io_job* j);
		int do_save_resume_data(disk_io_job* j);
		int do_rename_file(disk_io_job* j);
		int do_stop_torrent(disk_io_job* j);
		int do_read_and_hash(disk_io_job* j);
		int do_cache_piece(disk_io_job* j);
		int do_finalize_file(disk_io_job* j);
		int do_flush_piece(disk_io_job* j);
		int do_flush_hashed(disk_io_job* j);
		int do_flush_storage(disk_io_job* j);
		int do_trim_cache(disk_io_job* j);
		int do_file_priority(disk_io_job* j);

		void call_job_handlers(void* userdata);

	private:

		enum return_value_t
		{
			// the do_* functions can return this to indicate the disk
			// job did not complete immediately, and shouldn't be posted yet
			defer_handler = -200,

			// the job cannot be completed right now, put it back in the
			// queue and try again later
			retry_job = -201,
		};

		void add_completed_job(disk_io_job* j);
		void add_completed_jobs(tailqueue& jobs);
		void add_completed_job_impl(disk_io_job* j);

		void perform_async_job(disk_io_job* j);

		// this queues up another job to be submitted
		void add_job(disk_io_job* j, bool ignore_fence = false);
		void add_fence_job(piece_manager* storage, disk_io_job* j);

		// assumes l is locked (cache mutex).
		// writes out the blocks [start, end) (releases the lock
		// during the file operation)
		int flush_range(cached_piece_entry* p, int start, int end
			, int flags, mutex::scoped_lock& l);

		// assumes l is locked (the cache mutex).
		// assumes pe->hash to be set.
		// If there are new blocks in piece 'pe' that have not been
		// hashed by the partial_hash object attached to this piece,
		// the piece will
		void kick_hasher(cached_piece_entry* pe, mutex::scoped_lock& l);

		void abort_jobs(tailqueue& jobs_);

		enum flush_flags_t { flush_read_cache = 1, flush_write_cache = 2, flush_delete_cache = 4 };
		void flush_cache(piece_manager* storage, boost::uint32_t flags, mutex::scoped_lock& l);
		void flush_expired_write_blocks(mutex::scoped_lock& l);
		void flush_piece(cached_piece_entry* pe, int flags, mutex::scoped_lock& l);

		int try_flush_hashed(cached_piece_entry* p, int cont_blocks, mutex::scoped_lock& l);

		void try_flush_write_blocks(int num, mutex::scoped_lock& l);

		// this is a counter which is atomically incremented
		// by each thread as it's started up, in order to
		// assign a unique id to each thread
		boost::detail::atomic_count m_num_threads;

		// this is a counter of how many threads are currently running.
		// it's used to identify the last thread still running while
		// shutting down. This last thread is responsible for cleanup
		boost::detail::atomic_count m_num_running_threads;

		// the actual threads running disk jobs
		std::vector<boost::shared_ptr<thread> > m_threads;

		aux::session_settings m_settings;

		// userdata pointer for the complete_job function, which
		// is posted to the network thread when jobs complete
		void* m_userdata;

		// the last time we expired write blocks from the cache
		ptime m_last_cache_expiry;

		ptime m_last_file_check;

		// LRU cache of open files
		file_pool m_file_pool;

		// disk cache
		mutex m_cache_mutex;
		block_cache m_disk_cache;

		void flip_stats();

		// total number of blocks in use by both the read
		// and the write cache. This is not supposed to
		// exceed m_cache_size
		cache_status m_cache_stats;

		// average read time for cache misses (in microseconds)
		average_accumulator m_read_time;

		// average write time (in microseconds)
		average_accumulator m_write_time;

		// average hash time (in microseconds)
		average_accumulator m_hash_time;

		// average time to serve a job (any job) in microseconds
		average_accumulator m_job_time;

		// the last time we reset the average time and store the
		// latest value in m_cache_stats
		ptime m_last_stats_flip;

#ifdef TORRENT_DISK_STATS
		std::ofstream m_log;
#endif

		// the total number of outstanding jobs. This is used to
		// limit the number of jobs issued in parallel. It also creates
		// an opportunity to sort the jobs by physical offset before
		// issued to the AIO subsystem
		atomic_count m_outstanding_jobs;

		// the amount of physical ram in the machine
		boost::uint64_t m_physical_ram;

		// this is the main thread io_service. Callbacks are
		// posted on this in order to have them execute in
		// the main thread.
		io_service& m_ios;

		// the number of jobs that have been blocked by a fence. These
		// jobs are queued up in their respective storage, waiting for
		// the fence to be lowered. This counter is just used to know
		// when it's OK to exit the main loop of the disk thread
		atomic_count m_num_blocked_jobs;

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

		// used to wake up the disk IO thread when there are new
		// jobs on the job queue (m_queued_jobs)
		condition m_job_cond;

		// mutex to protect the m_queued_jobs list
		mutex m_job_mutex;
		// jobs queued for servicing
		tailqueue m_queued_jobs;
		// when using more than 2 threads, this is
		// used for just hashing jobs, just for threads
		// dedicated to od hashing
		condition m_hash_job_cond;
		tailqueue m_queued_hash_jobs;
		
		// used to rate limit disk performance warnings
		ptime m_last_disk_aio_performance_warning;

		// function to be posted to the network thread to post
		// an alert (used for performance warnings)
		alert_dispatcher* m_post_alert;

		// jobs that are completed are put on this queue
		// whenever the queue size grows from 0 to 1
		// a message is posted to the network thread, which
		// will then drain the queue and execute the jobs'
		// handler functions
		mutex m_completed_jobs_mutex;
		tailqueue m_completed_jobs;
	};
}

#endif

