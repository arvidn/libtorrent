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

#ifndef TORRENT_DISK_IO_THREAD
#define TORRENT_DISK_IO_THREAD

#include "libtorrent/config.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/disk_io_thread_pool.hpp"
#include "libtorrent/disk_io_job.hpp"
#include "libtorrent/disk_job_pool.hpp"
#include "libtorrent/block_cache.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/session_settings.hpp"

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <vector>

namespace libtorrent {

	struct counters;
	class alert_manager;

namespace aux { struct block_cache_reference; }

	struct cached_piece_info
	{
		storage_interface* storage;

		// holds one entry for each block in this piece. ``true`` represents
		// the data for that block being in the disk cache and ``false`` means it's not.
		std::vector<bool> blocks;

		// the time when a block was last written to this piece. The older
		// a piece is, the more likely it is to be flushed to disk.
		time_point last_use;

		// The index of the next block that needs to be hashed.
		// Blocks are hashed as they are downloaded in order to not
		// have to re-read them from disk once the piece is complete, to
		// compare its hash against the hashes in the .torrent file.
		int next_to_hash;

		// the piece index for this cache entry.
		piece_index_t piece;

		enum kind_t { read_cache = 0, write_cache = 1, volatile_read_cache = 2 };

		// specifies if this piece is part of the read cache or the write cache.
		kind_t kind;

		bool need_readback;
	};

	using jobqueue_t = tailqueue<disk_io_job>;

	// this struct holds a number of statistics counters
	// relevant for the disk io thread and disk cache.
	struct TORRENT_EXPORT cache_status
	{
		// initializes all counters to 0
		cache_status()
			: pieces()
#if TORRENT_ABI_VERSION == 1
			, blocks_written(0)
			, writes(0)
			, blocks_read(0)
			, blocks_read_hit(0)
			, reads(0)
			, queued_bytes(0)
			, cache_size(0)
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
			, num_writing_threads(0)
#endif
		{
#if TORRENT_ABI_VERSION == 1
			std::memset(num_fence_jobs, 0, sizeof(num_fence_jobs));
#endif
		}

		std::vector<cached_piece_info> pieces;

#if TORRENT_ABI_VERSION == 1
		// the total number of 16 KiB blocks written to disk
		// since this session was started.
		int blocks_written;

		// the total number of write operations performed since this
		// session was started.
		//
		// The ratio (``blocks_written`` - ``writes``) / ``blocks_written`` represents
		// the number of saved write operations per total write operations. i.e. a kind
		// of cache hit ratio for the write cahe.
		int writes;

		// the number of blocks that were requested from the
		// bittorrent engine (from peers), that were served from disk or cache.
		int blocks_read;

		// the number of blocks that was just copied from the read cache
		//
		// The ratio ``blocks_read_hit`` / ``blocks_read`` is the cache hit ratio
		// for the read cache.
		int blocks_read_hit;

		// the number of read operations used
		int reads;

		// the number of bytes queued for writing, including bytes
		// submitted to the OS for writing, but not yet complete
		mutable std::int64_t queued_bytes;

		// the number of 16 KiB blocks currently in the disk cache (both read and write).
		// This includes both read and write cache.
		int cache_size;

		// the number of blocks in the cache used for write cache
		int write_cache_size;

		// the number of 16KiB blocks in the read cache.
		int read_cache_size;

		// the number of blocks with a refcount > 0, i.e.
		// they may not be evicted
		int pinned_blocks;

		// the total number of buffers currently in use.
		// This includes the read/write disk cache as well as send and receive buffers
		// used in peer connections.
		// deprecated, use session_stats_metrics "disk.disk_blocks_in_use"
		mutable int total_used_buffers;

		// the number of microseconds an average disk I/O job
		// has to wait in the job queue before it get processed.

		// the time read jobs takes on average to complete
		// (not including the time in the queue), in microseconds. This only measures
		// read cache misses.
		int average_read_time;

		// the time write jobs takes to complete, on average,
		// in microseconds. This does not include the time the job sits in the disk job
		// queue or in the write cache, only blocks that are flushed to disk.
		int average_write_time;

		// the time hash jobs takes to complete on average, in
		// microseconds. Hash jobs include running SHA-1 on the data (which for the most
		// part is done incrementally) and sometimes reading back parts of the piece. It
		// also includes checking files without valid resume data.
		int average_hash_time;
		int average_job_time;

		// the number of milliseconds spent in all disk jobs, and specific ones
		// since the start of the session. Times are specified in milliseconds
		int cumulative_job_time;
		int cumulative_read_time;
		int cumulative_write_time;
		int cumulative_hash_time;

		// the number of blocks that had to be read back from disk because
		// they were flushed before the SHA-1 hash got to hash them. If this
		// is large, a larger cache could significantly improve performance
		int total_read_back;

		// number of read jobs in the disk job queue
		int read_queue_size;

		// number of jobs blocked because of a fence
		int blocked_jobs;

		// number of jobs waiting to be issued (m_to_issue)
		// average over 30 seconds
		// deprecated, use session_stats_metrics "disk.queued_disk_jobs"
		int queued_jobs;

		// largest ever seen number of queued jobs
		int peak_queued;

		// number of jobs waiting to complete (m_pending)
		// average over 30 seconds
		int pending_jobs;

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

		// the number of threads currently writing to disk
		int num_writing_threads;

		// counts only fence jobs that are currently blocking jobs
		// not fences that are themself blocked
		int num_fence_jobs[static_cast<int>(job_action_t::num_job_ids)];
#endif
	};

	// this is a singleton consisting of the thread and a queue
	// of disk io jobs
	struct TORRENT_EXTRA_EXPORT disk_io_thread final
		: disk_job_pool
		, disk_interface
		, buffer_allocator_interface
	{
		disk_io_thread(io_service& ios, aux::session_settings const&, counters&);
#if TORRENT_USE_ASSERTS
		~disk_io_thread();
#endif

		enum
		{
			// every 4:th thread is a hash thread
			hasher_thread_divisor = 4
		};

		void settings_updated();

		void abort(bool wait);

		storage_holder new_torrent(storage_constructor_type sc
			, storage_params p, std::shared_ptr<void> const&) override;
		void remove_torrent(storage_index_t) override;

		void async_read(storage_index_t storage, peer_request const& r
			, std::function<void(disk_buffer_holder block
				, disk_job_flags_t flags, storage_error const& se)> handler, disk_job_flags_t flags = {}) override;
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
		void async_flush_piece(storage_index_t storage, piece_index_t piece
			, std::function<void()> handler = std::function<void()>()) override;
		void async_set_file_priority(storage_index_t storage
			, aux::vector<download_priority_t, file_index_t> prio
			, std::function<void(storage_error const&, aux::vector<download_priority_t, file_index_t>)> handler) override;

		void async_clear_piece(storage_index_t storage, piece_index_t index
			, std::function<void(piece_index_t)> handler) override;
		// this is not asynchronous and requires that the piece does not
		// have any pending buffers. It's meant to be used for pieces that
		// were just read and hashed and failed the hash check.
		// there should be no read-operations left, and all buffers should
		// be discardable
		void clear_piece(storage_index_t storage, piece_index_t index) override;

		// implements buffer_allocator_interface
		void reclaim_blocks(span<aux::block_cache_reference> ref) override;
		void free_disk_buffer(char* buf) override { m_disk_cache.free_buffer(buf); }
		void trigger_cache_trim();
		void update_stats_counters(counters& c) const override;
		void get_cache_info(cache_status* ret, storage_index_t storage
			, bool no_pieces, bool session) const override;
		storage_interface* get_torrent(storage_index_t) override;

		std::vector<open_file_state> get_status(storage_index_t) const override;

		// this submits all queued up jobs to the thread
		void submit_jobs() override;

		block_cache* cache() { return &m_disk_cache; }

#if TORRENT_USE_ASSERTS
		bool is_disk_buffer(char* buffer) const override
		{ return m_disk_cache.is_disk_buffer(buffer); }
#endif

		int prep_read_job_impl(disk_io_job* j, bool check_fence = true);

		void maybe_issue_queued_read_jobs(cached_piece_entry* pe,
			jobqueue_t& completed_jobs);
		status_t do_read(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_uncached_read(disk_io_job* j);

		status_t do_write(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_uncached_write(disk_io_job* j);

		status_t do_hash(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_uncached_hash(disk_io_job* j);

		status_t do_move_storage(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_release_files(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_delete_files(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_check_fastresume(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_rename_file(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_stop_torrent(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_flush_piece(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_flush_hashed(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_flush_storage(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_trim_cache(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_file_priority(disk_io_job* j, jobqueue_t& completed_jobs);
		status_t do_clear_piece(disk_io_job* j, jobqueue_t& completed_jobs);

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
		void add_completed_jobs_impl(jobqueue_t& jobs
			, jobqueue_t& completed_jobs);

		void fail_jobs(storage_error const& e, jobqueue_t& jobs_);
		void fail_jobs_impl(storage_error const& e, jobqueue_t& src, jobqueue_t& dst);

		void check_cache_level(std::unique_lock<std::mutex>& l, jobqueue_t& completed_jobs);

		void perform_job(disk_io_job* j, jobqueue_t& completed_jobs);

		// this queues up another job to be submitted
		void add_job(disk_io_job* j, bool user_add = true);
		void add_fence_job(disk_io_job* j, bool user_add = true);

		// assumes l is locked (cache std::mutex).
		// writes out the blocks [start, end) (releases the lock
		// during the file operation)
		int flush_range(cached_piece_entry* p, int start, int end
			, jobqueue_t& completed_jobs, std::unique_lock<std::mutex>& l);

		// low level flush operations, used by flush_range
		int build_iovec(cached_piece_entry* pe, int start, int end
			, span<iovec_t> iov, span<int> flushing, int block_base_index = 0);
		void flush_iovec(cached_piece_entry* pe, span<iovec_t const> iov, span<int const> flushing
			, int num_blocks, storage_error& error);
		// returns true if the piece entry was freed
		bool iovec_flushed(cached_piece_entry* pe
			, int* flushing, int num_blocks, int block_offset
			, storage_error const& error
			, jobqueue_t& completed_jobs);

		// assumes l is locked (the cache std::mutex).
		// assumes pe->hash to be set.
		// If there are new blocks in piece 'pe' that have not been
		// hashed by the partial_hash object attached to this piece,
		// the piece will
		void kick_hasher(cached_piece_entry* pe, std::unique_lock<std::mutex>& l);

		// flags to pass in to flush_cache()
		enum flush_flags_t : std::uint32_t
		{
			// only flush read cache (this is cheap)
			flush_read_cache = 1,
			// flush read cache, and write cache
			flush_write_cache = 2,
			// flush read cache, delete write cache without flushing to disk
			flush_delete_cache = 4,
			// expect all pieces for the storage to have been
			// cleared when flush_cache() returns. This is only
			// used for asserts and only applies for fence jobs
			flush_expect_clear = 8
		};
		void flush_cache(storage_interface* storage, std::uint32_t flags, jobqueue_t& completed_jobs, std::unique_lock<std::mutex>& l);
		void flush_expired_write_blocks(jobqueue_t& completed_jobs, std::unique_lock<std::mutex>& l);
		void flush_piece(cached_piece_entry* pe, std::uint32_t flags, jobqueue_t& completed_jobs, std::unique_lock<std::mutex>& l);

		int try_flush_hashed(cached_piece_entry* p, int cont_blocks, jobqueue_t& completed_jobs, std::unique_lock<std::mutex>& l);

		void try_flush_write_blocks(int num, jobqueue_t& completed_jobs, std::unique_lock<std::mutex>& l);

		void maybe_flush_write_blocks();
		void execute_job(disk_io_job* j);
		void immediate_execute();
		void abort_jobs();
		void abort_hash_jobs(storage_index_t storage);

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

		aux::session_settings const& m_settings;

		// the last time we expired write blocks from the cache
		time_point m_last_cache_expiry = min_time();

		// we call close_oldest_file on the file_pool regularly. This is the next
		// time we should call it
		time_point m_next_close_oldest_file = min_time();

		// LRU cache of open files
		file_pool m_file_pool{40};

		// disk cache
		mutable std::mutex m_cache_mutex;
		block_cache m_disk_cache;
		enum
		{
			cache_check_idle,
			cache_check_active,
			cache_check_reinvoke
		};
		int m_cache_check_state = cache_check_idle;

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
		std::vector<std::pair<time_point, std::weak_ptr<storage_interface>>> m_need_tick;
		std::mutex m_need_tick_mutex;

		// this is protected by the completed_jobs_mutex. It's true whenever
		// there's a call_job_handlers message in-flight to the network thread. We
		// only ever keep one such message in flight at a time, and coalesce
		// completion callbacks in m_completed jobs
		bool m_job_completions_in_flight = false;

		aux::vector<std::shared_ptr<storage_interface>, storage_index_t> m_torrents;

		// indices into m_torrents to empty slots
		std::vector<storage_index_t> m_free_slots;

		std::atomic_flag m_jobs_aborted = ATOMIC_FLAG_INIT;

#if TORRENT_USE_ASSERTS
		int m_magic = 0x1337;
#endif
	};
}

#endif
