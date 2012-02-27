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
#include "libtorrent/aiocb_pool.hpp"
#include "libtorrent/hash_thread.hpp"

#include <boost/function/function0.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_array.hpp>
#include <deque>
#include "libtorrent/config.hpp"
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
#include <boost/pool/pool.hpp>
#endif
#include "libtorrent/session_settings.hpp"
#include "libtorrent/thread.hpp"

#include <boost/intrusive_ptr.hpp> // atomic_count

#if TORRENT_USE_IOSUBMIT
#include <libaio.h>
#endif

namespace libtorrent
{
	class alert;

	struct cached_piece_info
	{
		int piece;
		std::vector<bool> blocks;
		ptime last_use;
		bool need_readback;
		int next_to_hash;
		enum kind_t { read_cache = 0, write_cache = 1 };
		kind_t kind;
		int num_jobs[disk_io_job::num_job_ids];
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
			, elevator_turns(0)
			, total_used_buffers(0)
			, average_queue_time(0)
			, average_read_time(0)
			, average_write_time(0)
			, average_hash_time(0)
			, average_job_time(0)
			, average_sort_time(0)
			, average_issue_time(0)
			, cumulative_job_time(0)
			, cumulative_read_time(0)
			, cumulative_write_time(0)
			, cumulative_hash_time(0)
			, cumulative_sort_time(0)
			, cumulative_issue_time(0)
			, total_read_back(0)
			, read_queue_size(0)
			, blocked_jobs(0)
			, queued_jobs(0)
			, peak_queued(0)
			, pending_jobs(0)
			, peak_pending(0)
			, num_aiocb(0)
			, peak_aiocb(0)
			, cumulative_completed_aiocbs(0)
			, num_jobs(0)
			, num_read_jobs(0)
			, num_write_jobs(0)
			, arc_mru_size(0)
			, arc_mru_ghost_size(0)
			, arc_mfu_size(0)
			, arc_mfu_ghost_size(0)
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

		// the number of times we've changed elevator direction
		int elevator_turns;

		// the total number of blocks that are currently in use
		// this includes send and receive buffers
		mutable int total_used_buffers;

		// times in microseconds
		int average_queue_time;
		int average_read_time;
		int average_write_time;
		int average_hash_time;
		int average_job_time;
		int average_sort_time;
		int average_issue_time;

		boost::uint32_t cumulative_job_time;
		boost::uint32_t cumulative_read_time;
		boost::uint32_t cumulative_write_time;
		boost::uint32_t cumulative_hash_time;
		boost::uint32_t cumulative_sort_time;
		boost::uint32_t cumulative_issue_time;

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

		// the number of aiocb_t structures that are in use
		// right now
		int num_aiocb;

		// the peak number of aiocb_t structures in use
		int peak_aiocb;

		// counter of the number of aiocbs that have
		// been completed
		size_type cumulative_completed_aiocbs;

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
	};
	
#if TORRENT_USE_SUBMIT_THREADS
	// since linux' io_submit() isn't really asychronous, there's
	// an option to create 3 worker threads to submit the disk jobs (iocbs)
	struct submit_queue
	{
		submit_queue(aiocb_pool* p)
			: m_abort(false), m_pool(p)
			, m_thread1(boost::bind(&submit_queue::worker_fun, this))
			, m_thread2(boost::bind(&submit_queue::worker_fun, this))
			, m_thread3(boost::bind(&submit_queue::worker_fun, this))
		{}

		mutable mutex m_mutex;
		condition m_cond;
		std::vector<iocb*> m_queue;
		bool m_abort;
		aiocb_pool* m_pool;
		thread m_thread1;
		thread m_thread2;
		thread m_thread3;

		int submit(file::aiocb_t* chain)
		{
			int count = 0;
			for (file::aiocb_t* i = chain; i != 0; i = i->next) ++count;
			mutex::scoped_lock l(m_mutex);
			int index = m_queue.size();
			m_queue.resize(m_queue.size() + count);
			for (file::aiocb_t* i = chain; i != 0; (i = i->next), ++index) m_queue[index] = &i->cb;
			m_cond.signal_all(l);
			return count;
		}

		void kick()
		{
			mutex::scoped_lock l(m_mutex);
			if (m_queue.empty()) return;
			m_cond.signal_all(l);
		}

		~submit_queue()
		{
			mutex::scoped_lock l(m_mutex);
			m_abort = true;
			m_cond.signal_all(l);
			l.unlock();

			m_thread1.join();
			m_thread2.join();
			m_thread3.join();
		}

		void worker_fun()
		{
			mutex::scoped_lock l(m_mutex);
			while (!m_abort || !m_queue.empty())
			{
				while (m_queue.empty() && !m_abort) m_cond.wait(l);
				if (m_queue.empty()) continue;

				const int submit_batch_size = 256;
				int num_to_submit = (std::min)(submit_batch_size, int(m_queue.size()));
				iocb* to_submit[submit_batch_size];
				memcpy(to_submit, &m_queue[0], num_to_submit * sizeof(iocb*));
				m_queue.erase(m_queue.begin(), m_queue.begin() + num_to_submit);
				l.unlock();

				int r = 0;
				iocb** start = to_submit;
				r = io_submit(m_pool->io_queue, num_to_submit, start);

				int num_to_put_back = 0;
				if (r < 0) num_to_put_back = num_to_submit;
				else
				{
					num_to_put_back = num_to_submit - r;
					start += r;
				}
				l.lock();

				if (num_to_put_back)
				{
					m_queue.insert(m_queue.begin(), start, start + num_to_put_back);
					// wait to be kicked, no point in re-trying immediately
					m_cond.wait(l);
				}
			}
		}
	};

#endif // TORRENT_USE_SUBMIT_THREADS

	// this is a singleton consisting of the thread and a queue
	// of disk io jobs
	struct TORRENT_EXPORT disk_io_thread
	{
		friend TORRENT_EXPORT int append_aios(file::aiocb_t*& list_start, file::aiocb_t*& list_end
			, file::aiocb_t* aios, int elevator_direction, disk_io_thread* io);

		disk_io_thread(io_service& ios
			, boost::function<void(alert*)> const& post_alert
			, void* userdata
			, int block_size = 16 * 1024);
		~disk_io_thread();

		void set_settings(session_settings* sett);
		void reclaim_block(block_cache_reference ref);
		void abort();
		void join();

		void subscribe_to_disk(boost::function<void()> const& cb)
		{ m_disk_cache.subscribe_to_disk(cb); }
		void free_buffer(char* buf) { m_disk_cache.free_buffer(buf); }
		char* allocate_buffer(bool& exceeded, boost::function<void()> const& cb
			, char const* category);
		char* allocate_buffer(char const* category)
		{ return m_disk_cache.allocate_buffer(category); }
		bool exceeded_cache_use() const
		{ return m_disk_cache.exceeded_max_size(); }

		// this queues up another job to be submitted
		void add_job(disk_io_job* j, bool high_priority = false);

		// this submits all queued up jobs to the thread
		void submit_jobs();

		aiocb_pool* aiocbs() { return &m_aiocb_pool; }
		block_cache* cache() { return &m_disk_cache; }
		void thread_fun();

		file_pool& files() { return m_file_pool; }

		io_service& get_io_service() { return m_ios; }

		void get_disk_metrics(cache_status& ret) const;
#ifdef TORRENT_DEBUG
		void check_invariant() const;
#endif
		
		void pinned_change(int diff) { m_disk_cache.pinned_change(diff); }

		enum return_code_t
		{
			// the error is stored in disk_io_job::error
			disk_operation_failed = -1,
			// don't post the handler yet, this operation
			// is async and will be completed later
			defer_handler = -100
		};

		int do_read(disk_io_job* j);
		int do_write(disk_io_job* j);
		int do_hash(disk_io_job* j);
		int do_move_storage(disk_io_job* j);
		int do_release_files(disk_io_job* j);
		int do_delete_files(disk_io_job* j);
		int do_check_fastresume(disk_io_job* j);
		int do_save_resume_data(disk_io_job* j);
		int do_rename_file(disk_io_job* j);
		int do_abort_thread(disk_io_job* j);
		int do_clear_read_cache(disk_io_job* j);
		int do_abort_torrent(disk_io_job* j);
		int do_update_settings(disk_io_job* j);
		int do_read_and_hash(disk_io_job* j);
		int do_cache_piece(disk_io_job* j);
		int do_finalize_file(disk_io_job* j);
		int do_get_cache_info(disk_io_job* j);
		int do_hashing_done(disk_io_job* j);
		int do_file_status(disk_io_job* j);
		int do_reclaim_block(disk_io_job* j);
		int do_clear_piece(disk_io_job* j);
		int do_sync_piece(disk_io_job* j);
		int do_flush_piece(disk_io_job* j);
		int do_trim_cache(disk_io_job* j);
		int do_aiocb_complete(disk_io_job* j);

	private:

		void perform_async_job(disk_io_job* j);
		void submit_jobs_impl();

		void on_disk_write(cached_piece_entry* p, int begin
			, int end, int to_write, async_handler* handler);
		void on_disk_read(cached_piece_entry* p, int begin
			, int end, async_handler* handler);

		enum op_t
		{
			op_read = 0,
			op_write = 1
		};

		int io_range(cached_piece_entry* p, int start, int end, int readwrite, int flags);

		enum flush_flags_t { flush_read_cache = 1, flush_write_cache = 2, flush_delete_cache = 4 };
		int flush_cache(disk_io_job* j, boost::uint32_t flags);
		void flush_expired_write_blocks();
		void flush_piece(cached_piece_entry* pe, int flags, int& ret);

		void on_write_one_buffer(async_handler* handler, disk_io_job* j);
		void on_read_one_buffer(async_handler* handler, disk_io_job* j);

		int try_flush_contiguous(cached_piece_entry* p, int cont_blocks, int num = INT_MAX);
		int try_flush_hashed(cached_piece_entry* p, int cont_blocks, int num = INT_MAX);
		void try_flush_write_blocks(int num);

		bool m_abort;

		session_settings m_settings;

		// userdata pointer for the complete_job function, which
		// is posted to the network thread when jobs complete
		void* m_userdata;

		// the last time we expired write blocks from the cache
		ptime m_last_cache_expiry;

		// this is the number of bytes we're waiting for to be written
		size_type m_pending_buffer_size;

		// the number of bytes waiting in write jobs in m_jobs
		size_type m_queue_buffer_size;

		ptime m_last_file_check;

		// LRU cache of open files
		file_pool m_file_pool;

		// this is a thread pool for doing SHA-1 hashing
		hash_thread m_hash_thread;

		// disk cache
		block_cache m_disk_cache;

		void flip_stats();

		// total number of blocks in use by both the read
		// and the write cache. This is not supposed to
		// exceed m_cache_size
		cache_status m_cache_stats;

		// keeps average queue time for disk jobs (in microseconds)
		average_accumulator m_queue_time;

		// average read time for cache misses (in microseconds)
		average_accumulator m_read_time;

		// average write time (in microseconds)
		average_accumulator m_write_time;

		// average hash time (in microseconds)
		average_accumulator m_hash_time;

		// average time to serve a job (any job) in microseconds
		average_accumulator m_job_time;

		// average time to ask for physical offset on disk
		// and insert into queue
		average_accumulator m_sort_time;

		// average time to issue jobs
		average_accumulator m_issue_time;

		// the last time we reset the average time and store the
		// latest value in m_cache_stats
		ptime m_last_stats_flip;

		// these are async I/O operations that have been issued
		// and we are waiting to complete
		file::aiocb_t* m_in_progress;

		// these are async operations that we've accumulated
		// during this round and will be issued
		file::aiocb_t* m_to_issue;
		// the last element in the to-issue chain
		file::aiocb_t* m_to_issue_end;

		// the number of jobs waiting to be issued in m_to_issue
		int m_num_to_issue;
		int m_peak_num_to_issue;

#ifdef TORRENT_DISK_STATS
		std::ofstream m_log;
#endif

#if TORRENT_USE_AIO && !TORRENT_USE_AIO_SIGNALFD && !TORRENT_USE_AIO_PORTS && !TORRENT_USE_AIO_KQUEUE
		static void signal_handler(int signal, siginfo_t* si, void*);
#endif

		// the total number of outstanding jobs. This is used to
		// limit the number of jobs issued in parallel. It also creates
		// an opportunity to sort the jobs by physical offset before
		// issued to the AIO subsystem
		int m_outstanding_jobs;
		int m_peak_outstanding;

#if TORRENT_USE_SYNCIO
		// the direction of the elevator. -1 means down and
		// 1 means up
		int m_elevator_direction;

		// the number of times we've switched elevator direction
		// (only useful for non-aio builds with physical disk offset support)
		boost::uint64_t m_elevator_turns;

		// the physical offset of the last job consumed out
		// of the deferred jobs list
		size_type m_last_phys_off;
#endif

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
		tailqueue m_blocked_jobs;

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

		// jobs queued for servicing
		tailqueue m_queued_jobs;
		
		// jobs that have been completed waiting
		// to be posted back to the network thread
		tailqueue m_completed_jobs;

		// mutex to protect the m_queued_jobs list
		mutex m_job_mutex;

		// used to rate limit disk performance warnings
		ptime m_last_disk_aio_performance_warning;

		// function to be posted to the network thread to post
		// an alert (used for performance warnings)
		boost::function<void(alert*)> m_post_alert;

		// pool used to allocate the aiocb_t elements
		// used by the async operations on files
		aiocb_pool m_aiocb_pool;

#if TORRENT_USE_SYNCIO
		disk_worker_pool m_worker_thread;
#endif

#if TORRENT_USE_SUBMIT_THREADS
		// used to run io_submit() in separate threads
		submit_queue m_submit_queue;
#endif

#if TORRENT_USE_OVERLAPPED
		// this is used to feed events of completed disk I/O
		// operations to the disk thread
		HANDLE m_completion_port;
#endif

#if TORRENT_USE_AIO_PORTS
		// on solaris we can get AIO completions over ports
		// which is a lot nicer than signals. This is the
		// port used for notifications
		int m_port;
#endif

#if TORRENT_USE_AIO_KQUEUE
		// when using kqueue for aio completion notifications
		// this is the queue events are posted to
		int m_queue;

		// this is a pipe that's used to interrupt the disk thread
		// waiting in the kevent() call. A single byte is written
		// to the pipe and the kqueue has an event triggered by
		// the pipe becoming readable
		int m_job_pipe[2];
#endif

#if TORRENT_USE_AIO_SIGNALFD
		// if we're using a signalfd instead of a signal handler
		// this is its file descriptor
		// this is fucked up. If we only have a single signalfd, in
		// the disk thread itself, it will only catch signals specifically
		// posted to that thread, and for some reason the aio implementation
		// sometimes send signals to other threads. Now we have two signalfds,
		// one created from the network thread and one from the disk thread.
		// this seems to work, but there are other threads not covered by a
		// signalfd. So it seems this is still broken.
		// the symptom of lost signals is block writes being issued but
		// never completed, so pieces will get stuck flushing and the usable
		// portion of the disk cache will be smaller and smaller over time
		int m_signal_fd[2];

		// this is an eventfd used to signal the disk thread that
		// there are new jobs in its in-queue
		int m_job_event_fd;
#endif

#if TORRENT_USE_IOSUBMIT
		// this is used to feed events of completed disk I/O
		// operations to the disk thread
		io_context_t m_io_queue;

		// these two event fds are used to signal
		// each disk job that completes and each disk
		// job that's queued
		int m_disk_event_fd;
		int m_job_event_fd;
#endif

		// thread for performing blocking disk io operations
		thread m_disk_io_thread;
	};

	struct deferred_submit_jobs
	{
		deferred_submit_jobs(disk_io_thread& dt): m_disk_thread(dt) {}
		~deferred_submit_jobs() { m_disk_thread.submit_jobs(); }
		disk_io_thread& m_disk_thread;
	};
}

#endif

