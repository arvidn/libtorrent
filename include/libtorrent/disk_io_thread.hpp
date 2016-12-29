/*

Copyright (c) 2007-2016, Arvid Norberg, Steven Siloti
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

#include "libtorrent/config.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/allocator.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/sliding_average.hpp"
#include "libtorrent/disk_io_thread_pool.hpp"
#include "libtorrent/disk_io_job.hpp"
#include "libtorrent/disk_job_pool.hpp"
#include "libtorrent/disk_buffer_pool.hpp"
#include "libtorrent/aux_/file_view_pool.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/block_cache_reference.hpp"
#include "libtorrent/aux_/store_buffer.hpp"
#include "libtorrent/debug.hpp"

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <vector>

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE

namespace libtorrent {

	struct add_torrent_params;
	struct counters;

	using jobqueue_t = tailqueue<disk_io_job>;

	// this is a singleton consisting of the thread and a queue
	// of disk io jobs
	struct TORRENT_EXTRA_EXPORT disk_io_thread final
		: disk_job_pool
		, disk_interface
		, buffer_allocator_interface
	{
		disk_io_thread(io_service& ios
			, counters& cnt
			, int block_size = 16 * 1024);
		~disk_io_thread() override;

		void set_settings(settings_pack const* sett) override;
		storage_holder new_torrent(storage_params params
			, std::shared_ptr<void> const& torrent) override;
		void remove_torrent(storage_index_t) override;

		void abort(bool wait) override;

		void async_read(storage_index_t storage, peer_request const& r
			, std::function<void(disk_buffer_holder, storage_error const&)> handler
			, disk_job_flags_t flags = {}) override;
		bool async_write(storage_index_t storage, peer_request const& r
			, char const* buf, std::shared_ptr<disk_observer> o
			, std::function<void(storage_error const&)> handler
			, disk_job_flags_t flags = {}) override;
		void async_hash(storage_index_t storage, piece_index_t piece, disk_job_flags_t flags
			, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler) override;
		void async_move_storage(storage_index_t storage, std::string p, move_flags_t flags
			, std::function<void(status_t, std::string const&, storage_error const&)> handler) override;
		void async_release_files(storage_index_t storage
			, std::function<void()> handler = std::function<void()>()) override;
		void async_delete_files(storage_index_t storage, remove_flags_t options
			, std::function<void(storage_error const&)> handler) override;
		void async_check_files(storage_index_t storage
			, add_torrent_params const* resume_data
			, aux::vector<std::string, file_index_t>& links
			, std::function<void(status_t, storage_error const&)> handler) override;
		void async_rename_file(storage_index_t storage, file_index_t index, std::string name
			, std::function<void(std::string const&, file_index_t, storage_error const&)> handler) override;
		void async_stop_torrent(storage_index_t storage
			, std::function<void()> handler) override;
		void async_set_file_priority(storage_index_t storage
			, aux::vector<std::uint8_t, file_index_t> prio
			, std::function<void(storage_error const&)> handler) override;

		void async_clear_piece(storage_index_t storage, piece_index_t index
			, std::function<void(piece_index_t)> handler) override;

		// implements buffer_allocator_interface
		void free_disk_buffer(char* b, aux::block_cache_reference const&) override
		{ m_buffer_pool.free_buffer(b); }

		void update_stats_counters(counters& c) const override;

		std::vector<open_file_state> get_status(storage_index_t) const override;

		// this submits all queued up jobs to the thread
		void submit_jobs() override;

		status_t do_read(disk_io_job* j);
		status_t do_write(disk_io_job* j);
		status_t do_hash(disk_io_job* j);

		status_t do_move_storage(disk_io_job* j);
		status_t do_release_files(disk_io_job* j);
		status_t do_delete_files(disk_io_job* j);
		status_t do_check_fastresume(disk_io_job* j);
		status_t do_rename_file(disk_io_job* j);
		status_t do_stop_torrent(disk_io_job* j);
		status_t do_read_and_hash(disk_io_job* j);
		status_t do_file_priority(disk_io_job* j);
		status_t do_clear_piece(disk_io_job* j);

		void call_job_handlers();

	private:

		struct job_queue : pool_thread_interface
		{
			explicit job_queue(disk_io_thread& owner) : m_owner(owner) {}

			void notify_all() override
			{
				m_job_cond.notify_all();
			}

			void thread_fun(disk_io_thread_pool& pool, io_service::work work) override
			{
				ADD_OUTSTANDING_ASYNC("disk_io_thread::work");
				m_owner.thread_fun(*this, pool);

				// w's dtor releases the io_service to allow the run() call to return
				// we do this once we stop posting new callbacks to it.
				// after the dtor has been called, the disk_io_thread object may be destructed
				TORRENT_UNUSED(work);
				COMPLETE_ASYNC("disk_io_thread::work");
			}

			disk_io_thread& m_owner;

			// used to wake up the disk IO thread when there are new
			// jobs on the job queue (m_queued_jobs)
			std::condition_variable m_job_cond;

			// jobs queued for servicing
			jobqueue_t m_queued_jobs;
		};

		void thread_fun(job_queue& queue, disk_io_thread_pool& pool);

		// returns true if the thread should exit
		static bool wait_for_job(job_queue& jobq, disk_io_thread_pool& threads
			, std::unique_lock<std::mutex>& l);

		void add_completed_jobs(jobqueue_t& jobs);
		void add_completed_jobs_impl(jobqueue_t& jobs);

		void fail_jobs(storage_error const& e, jobqueue_t& jobs_);
		void fail_jobs_impl(storage_error const& e, jobqueue_t& src, jobqueue_t& dst);

		void perform_job(disk_io_job* j, jobqueue_t& completed_jobs);

		// this queues up another job to be submitted
		void add_job(disk_io_job* j, bool user_add = true);
		void add_fence_job(disk_io_job* j, bool user_add = true);

		void execute_job(disk_io_job* j);
		void immediate_execute();
		void abort_jobs();

		// returns the maximum number of threads
		// the actual number of threads may be less
		int num_threads() const;
		job_queue& queue_for_job(disk_io_job* j);
		disk_io_thread_pool& pool_for_job(disk_io_job* j);

		// set to true once we start shutting down
		std::atomic<bool> m_abort{false};

		// this is a counter of how many threads are currently running.
		// it's used to identify the last thread still running while
		// shutting down. This last thread is responsible for cleanup
		// must hold the job mutex to access
		int m_num_running_threads = 0;

		// std::mutex to protect the m_generic_io_jobs and m_hash_io_jobs lists
		mutable std::mutex m_job_mutex;

		// most jobs are posted to m_generic_io_jobs
		// but hash jobs are posted to m_hash_io_jobs if m_hash_threads
		// has a non-zero maximum thread count
		job_queue m_generic_io_jobs;
		disk_io_thread_pool m_generic_threads;
		job_queue m_hash_io_jobs;
		disk_io_thread_pool m_hash_threads;

		// every write job is inserted into this map while it is in the job queue.
		// It is removed after the write completes. This will let subsequent reads
		// pull the buffers straight out of the queue instead of having to
		// synchronize with the writing thread(s)
		aux::store_buffer m_store_buffer;

		aux::session_settings m_settings;

		// LRU cache of open files
		aux::file_view_pool m_file_pool{40};

		// disk cache
		disk_buffer_pool m_buffer_pool;
		enum
		{
			cache_check_idle,
			cache_check_active,
			cache_check_reinvoke
		};

		// total number of blocks in use by both the read
		// and the write cache. This is not supposed to
		// exceed m_cache_size

		counters& m_stats_counters;

		// this is the main thread io_service. Callbacks are
		// posted on this in order to have them execute in
		// the main thread.
		io_service& m_ios;

		// jobs that are completed are put on this queue
		// whenever the queue size grows from 0 to 1
		// a message is posted to the network thread, which
		// will then drain the queue and execute the jobs'
		// handler functions
		std::mutex m_completed_jobs_mutex;
		jobqueue_t m_completed_jobs;

		// storages that have had write activity recently and will get ticked
		// soon, for deferred actions (say, flushing partfile metadata)
		std::vector<std::pair<time_point, std::weak_ptr<default_storage>>> m_need_tick;
		std::mutex m_need_tick_mutex;

		// this is protected by the completed_jobs_mutex. It's true whenever
		// there's a call_job_handlers message in-flight to the network thread. We
		// only ever keep one such message in flight at a time, and coalesce
		// completion callbacks in m_completed jobs
		bool m_job_completions_in_flight = false;

		aux::vector<std::shared_ptr<default_storage>, storage_index_t> m_torrents;

		// indices into m_torrents to empty slots
		std::vector<storage_index_t> m_free_slots;

#if TORRENT_USE_ASSERTS
		int m_magic = 0x1337;
		std::atomic<bool> m_jobs_aborted{false};
#endif
	};
}

#endif // HAVE_MMAP || HAVE_MAP_VIEW_OF_FILE

#endif // TORRENT_DISK_IO_THREAD
