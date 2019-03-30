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

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE

#include "libtorrent/storage.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/disk_buffer_pool.hpp"
#include "libtorrent/disk_io_job.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/disk_job_pool.hpp"
#include "libtorrent/disk_io_thread_pool.hpp"
#include "libtorrent/aux_/store_buffer.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/file_view_pool.hpp"
#include "libtorrent/aux_/scope_end.hpp"

#include <functional>
#include <condition_variable>

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

	aux::open_mode_t file_flags_for_job(disk_io_job* j)
	{
		aux::open_mode_t ret = aux::open_mode::read_only;
		if (!(j->flags & disk_interface::sequential_access)) ret |= aux::open_mode::random_access;
		return ret;
	}

	storage_index_t pop(std::vector<storage_index_t>& q)
	{
		TORRENT_ASSERT(!q.empty());
		storage_index_t const ret = q.back();
		q.pop_back();
		return ret;
	}
} // anonymous namespace

using jobqueue_t = tailqueue<disk_io_job>;

// this is a singleton consisting of the thread and a queue
// of disk io jobs
struct TORRENT_EXTRA_EXPORT disk_io_thread final
	: disk_job_pool
	, disk_interface
	, buffer_allocator_interface
{
	disk_io_thread(io_context& ios, counters& cnt);
#if TORRENT_USE_ASSERTS
	~disk_io_thread() override;
#endif

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
		, aux::vector<std::string, file_index_t> links
		, std::function<void(status_t, storage_error const&)> handler) override;
	void async_rename_file(storage_index_t storage, file_index_t index, std::string name
		, std::function<void(std::string const&, file_index_t, storage_error const&)> handler) override;
	void async_stop_torrent(storage_index_t storage
		, std::function<void()> handler) override;
	void async_set_file_priority(storage_index_t storage
		, aux::vector<download_priority_t, file_index_t> prio
		, std::function<void(storage_error const&
			, aux::vector<download_priority_t, file_index_t>)> handler) override;

	void async_clear_piece(storage_index_t storage, piece_index_t index
		, std::function<void(piece_index_t)> handler) override;

	// implements buffer_allocator_interface
	void free_disk_buffer(char* b) override
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

		void thread_fun(disk_io_thread_pool& pool, executor_work_guard<io_context::executor_type> work) override
		{
			ADD_OUTSTANDING_ASYNC("disk_io_thread::work");
			m_owner.thread_fun(*this, pool);

			// w's dtor releases the io_context to allow the run() call to return
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

	void fail_jobs_impl(storage_error const& e, jobqueue_t& src, jobqueue_t& dst);

	void perform_job(disk_io_job* j, jobqueue_t& completed_jobs);

	// this queues up another job to be submitted
	void add_job(disk_io_job* j, bool user_add = true);
	void add_fence_job(disk_io_job* j, bool user_add = true);

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

	// this is the main thread io_context. Callbacks are
	// posted on this in order to have them execute in
	// the main thread.
	io_context& m_ios;

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

	std::atomic_flag m_jobs_aborted = ATOMIC_FLAG_INIT;

#if TORRENT_USE_ASSERTS
	int m_magic = 0x1337;
#endif
};

TORRENT_EXPORT std::unique_ptr<disk_interface> mmap_disk_io_constructor(
	io_context& ios, counters& cnt)
{
	return std::make_unique<disk_io_thread>(ios, cnt);
}

// ------- disk_io_thread ------

	disk_io_thread::disk_io_thread(io_context& ios, counters& cnt)
		: m_generic_io_jobs(*this)
		, m_generic_threads(m_generic_io_jobs, ios)
		, m_hash_io_jobs(*this)
		, m_hash_threads(m_hash_io_jobs, ios)
		, m_buffer_pool(ios)
		, m_stats_counters(cnt)
		, m_ios(ios)
	{
		m_buffer_pool.set_settings(m_settings);
	}

	std::vector<open_file_state> disk_io_thread::get_status(storage_index_t const st) const
	{
		return m_file_pool.get_status(st);
	}

	storage_holder disk_io_thread::new_torrent(storage_params params
		, std::shared_ptr<void> const& owner)
	{
		storage_index_t const idx = m_free_slots.empty()
			? m_torrents.end_index()
			: pop(m_free_slots);
		auto storage = std::make_shared<default_storage>(std::move(params), m_file_pool);
		storage->set_storage_index(idx);
		storage->set_owner(owner);
		if (idx == m_torrents.end_index())
		{
			// make sure there's always space in here to add another free slot.
			// stopping a torrent should never fail because it needs to allocate memory
			m_free_slots.reserve(m_torrents.size() + 1);
			m_torrents.emplace_back(std::move(storage));
		}
		else m_torrents[idx] = std::move(storage);
		return storage_holder(idx, *this);
	}

	void disk_io_thread::remove_torrent(storage_index_t const idx)
	{
		m_torrents[idx].reset();
		m_free_slots.push_back(idx);
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

	void disk_io_thread::set_settings(settings_pack const* pack)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		apply_pack(pack, m_settings);
		m_buffer_pool.set_settings(m_settings);
		m_file_pool.resize(m_settings.get_int(settings_pack::file_pool_size));

		int const num_threads = m_settings.get_int(settings_pack::aio_threads);
		// add one hasher thread for every three generic threads
		int const num_hash_threads = num_threads / hasher_thread_divisor;

		DLOG("set_max_threads(%d, %d)\n", num_threads - num_hash_threads
			, num_hash_threads);
		m_generic_threads.set_max_threads(num_threads - num_hash_threads);
		m_hash_threads.set_max_threads(num_hash_threads);
	}

	void disk_io_thread::fail_jobs_impl(storage_error const& e, jobqueue_t& src, jobqueue_t& dst)
	{
		while (!src.empty())
		{
			disk_io_job* j = src.pop_front();
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

			if (j->action == job_action_t::write)
			{
				m_store_buffer.erase({j->storage->storage_index(), j->piece, j->d.io.offset});
			}
			j->ret = status_t::fatal_disk_error;
			j->error = e;
			dst.push_back(j);
		}
	}

	namespace {

	typedef status_t (disk_io_thread::*disk_io_fun_t)(disk_io_job* j);

	// this is a jump-table for disk I/O jobs
	std::array<disk_io_fun_t, 11> const job_functions =
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
		&disk_io_thread::do_file_priority,
		&disk_io_thread::do_clear_piece
	}};

	} // anonymous namespace

	void disk_io_thread::perform_job(disk_io_job* j, jobqueue_t& completed_jobs)
	{
		TORRENT_ASSERT(j->next == nullptr);
		TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

#if DEBUG_DISK_THREAD
		{
			std::unique_lock<std::mutex> l(m_job_mutex);

			DLOG("perform_job job: %s ( %s%s) piece: %d offset: %d outstanding: %d\n"
				, job_action_name[j->action]
				, (j->flags & disk_io_job::fence) ? "fence ": ""
				, (j->flags & disk_io_job::force_copy) ? "force_copy ": ""
				, static_cast<int>(j->piece), j->d.io.offset
				, j->storage ? j->storage->num_outstanding_jobs() : -1);
		}
#endif

		std::shared_ptr<default_storage> storage = j->storage;

		TORRENT_ASSERT(static_cast<int>(j->action) < int(job_functions.size()));

		m_stats_counters.inc_stats_counter(counters::num_running_disk_jobs, 1);

		// call disk function
		// TODO: in the future, propagate exceptions back to the handlers
		status_t ret = status_t::no_error;
		try
		{
			int const idx = static_cast<int>(j->action);
			ret = (this->*(job_functions[static_cast<std::size_t>(idx)]))(j);
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

		j->ret = ret;

		completed_jobs.push_back(j);
	}

	status_t disk_io_thread::do_read(disk_io_job* j)
	{
		j->argument = disk_buffer_holder(*this, m_buffer_pool.allocate_buffer("send buffer"), default_block_size);
		auto& buffer = boost::get<disk_buffer_holder>(j->argument);
		if (!buffer)
		{
			j->error.ec = error::no_memory;
			j->error.operation = operation_t::alloc_cache_piece;
			return status_t::fatal_disk_error;
		}

		time_point const start_time = clock_type::now();

		aux::open_mode_t const file_flags = file_flags_for_job(j);
		iovec_t b = {buffer.data(), j->d.io.buffer_size};

		int const ret = j->storage->readv(m_settings, b
			, j->piece, j->d.io.offset, file_flags, j->error);

		TORRENT_ASSERT(ret >= 0 || j->error.ec);
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

	status_t disk_io_thread::do_write(disk_io_job* j)
	{
		time_point const start_time = clock_type::now();
		auto buffer = std::move(boost::get<disk_buffer_holder>(j->argument));

		iovec_t const b = { buffer.data(), j->d.io.buffer_size};
		aux::open_mode_t const file_flags = file_flags_for_job(j);

		m_stats_counters.inc_stats_counter(counters::num_writing_threads, 1);

		// the actual write operation
		int const ret = j->storage->writev(m_settings, b
			, j->piece, j->d.io.offset, file_flags, j->error);

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
				m_need_tick.push_back({aux::time_now() + minutes(2), j->storage});
		}

		m_store_buffer.erase({j->storage->storage_index(), j->piece, j->d.io.offset});

		return ret != j->d.io.buffer_size
			? status_t::fatal_disk_error : status_t::no_error;
	}

	void disk_io_thread::async_read(storage_index_t storage, peer_request const& r
		, std::function<void(disk_buffer_holder, storage_error const&)> handler
		, disk_job_flags_t const flags)
	{
		TORRENT_ASSERT(r.length <= default_block_size);

		// in case r.start is not aligned to a block, calculate that offset,
		// since that's how the store_buffer is indexed
		int const block_offset = r.start - (r.start % default_block_size);
		// this is the offset into the block that we're reading from
		int const read_offset = r.start - block_offset;

		storage_error ec;
		if (read_offset + r.length > default_block_size || r.length <= 0)
		{
			// this is an invalid read request. We don't support read requests
			// spanning multiple blocks
			ec.ec = errors::invalid_request;
			ec.operation = operation_t::file_read;
			handler(disk_buffer_holder{}, ec);
			return;
		}

		DLOG("async_read piece: %d block: %d\n", static_cast<int>(r.piece)
			, block_offset / default_block_size);

		disk_buffer_holder buffer;

		if (m_store_buffer.get({ storage, r.piece, block_offset }, [&](char* buf)
		{
			buffer = disk_buffer_holder(*this, m_buffer_pool.allocate_buffer("send buffer"), r.length);
			if (!buffer)
			{
				ec.ec = error::no_memory;
				ec.operation = operation_t::alloc_cache_piece;
				return;
			}

			std::memcpy(buffer.data(), buf + read_offset, std::size_t(r.length));
		}))
		{
			handler(std::move(buffer), ec);
			return;
		}

		disk_io_job* j = allocate_job(job_action_t::read);
		j->storage = m_torrents[storage]->shared_from_this();
		j->piece = r.piece;
		j->d.io.offset = r.start;
		j->d.io.buffer_size = std::uint16_t(r.length);
		j->flags = flags;
		j->callback = std::move(handler);

		if (j->storage->is_blocked(j))
		{
			// this means the job was queued up inside storage
			m_stats_counters.inc_stats_counter(counters::blocked_disk_jobs);
			DLOG("blocked job: %s (torrent: %d total: %d)\n"
				, job_name(j->action), j->storage ? j->storage->num_blocked() : 0
				, int(m_stats_counters[counters::blocked_disk_jobs]));
		}
		else
		{
			add_job(j);
		}
	}

	bool disk_io_thread::async_write(storage_index_t const storage, peer_request const& r
		, char const* buf, std::shared_ptr<disk_observer> o
		, std::function<void(storage_error const&)> handler
		, disk_job_flags_t const flags)
	{
		bool exceeded = false;
		disk_buffer_holder buffer(*this, m_buffer_pool.allocate_buffer(
			exceeded, o, "receive buffer"), default_block_size);
		if (!buffer) aux::throw_ex<std::bad_alloc>();
		std::memcpy(buffer.data(), buf, aux::numeric_cast<std::size_t>(r.length));

		disk_io_job* j = allocate_job(job_action_t::write);
		j->storage = m_torrents[storage]->shared_from_this();
		j->piece = r.piece;
		j->d.io.offset = r.start;
		j->d.io.buffer_size = std::uint16_t(r.length);
		j->argument = std::move(buffer);
		j->callback = std::move(handler);
		j->flags = flags;

		TORRENT_ASSERT((r.start % default_block_size) == 0);

		m_store_buffer.insert({j->storage->storage_index(), j->piece, j->d.io.offset}
			, boost::get<disk_buffer_holder>(j->argument).data());

		if (j->storage->is_blocked(j))
		{
			// this means the job was queued up inside storage
			m_stats_counters.inc_stats_counter(counters::blocked_disk_jobs);
			DLOG("blocked job: %s (torrent: %d total: %d)\n"
				, job_name(j->action), j->storage ? j->storage->num_blocked() : 0
				, int(m_stats_counters[counters::blocked_disk_jobs]));
			return exceeded;
		}

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

		add_fence_job(j);
	}

	void disk_io_thread::abort_hash_jobs(storage_index_t const storage)
	{
		// abort outstanding hash jobs belonging to this torrent
		std::unique_lock<std::mutex> l(m_job_mutex);

		auto st = m_torrents[storage]->shared_from_this();
		// hash jobs
		for (auto i = m_hash_io_jobs.m_queued_jobs.iterate(); i.get(); i.next())
		{
			disk_io_job* j = i.get();
			if (j->storage != st) continue;
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
		, aux::vector<std::string, file_index_t> links
		, std::function<void(status_t, storage_error const&)> handler)
	{
		disk_io_job* j = allocate_job(job_action_t::check_fastresume);
		j->storage = m_torrents[storage]->shared_from_this();
		j->argument = resume_data;
		j->callback = std::move(handler);

		aux::vector<std::string, file_index_t>* links_vector = nullptr;
		if (!links.empty()) links_vector = new aux::vector<std::string, file_index_t>(std::move(links));
		j->d.links = links_vector;

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
		auto st = m_torrents[storage]->shared_from_this();
		abort_hash_jobs(storage);

		disk_io_job* j = allocate_job(job_action_t::stop_torrent);
		j->storage = st;
		j->callback = std::move(handler);
		add_fence_job(j);
	}

	void disk_io_thread::async_set_file_priority(storage_index_t const storage
		, aux::vector<download_priority_t, file_index_t> prios
		, std::function<void(storage_error const&
			, aux::vector<download_priority_t, file_index_t>)> handler)
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
		// but it hardly seems worth the complexity and cost just for the edge
		// case of receiving a corrupt piece
		add_fence_job(j);
	}

	status_t disk_io_thread::do_hash(disk_io_job* j)
	{
		// we're not using a cache. This is the simple path
		// just read straight from the file
		TORRENT_ASSERT(m_magic == 0x1337);

		int const piece_size = j->storage->files().piece_size(j->piece);
		int const blocks_in_piece = (piece_size + default_block_size - 1) / default_block_size;
		aux::open_mode_t const file_flags = file_flags_for_job(j);

		hasher h;
		int ret = 0;
		int offset = 0;
		for (int i = 0; i < blocks_in_piece; ++i)
		{
			DLOG("do_hash: reading (piece: %d block: %d)\n", int(j->piece), i);

			time_point const start_time = clock_type::now();

			std::ptrdiff_t const len = std::min(default_block_size, piece_size - offset);

			if (!m_store_buffer.get({j->storage->storage_index(), j->piece, offset}
				, [&](char* buf)
				{
					h.update({buf, len});
					ret = static_cast<int>(len);
				}))
			{
				ret = j->storage->hashv(m_settings, h, len, j->piece, offset, file_flags, j->error);
				if (ret < 0) break;
			}

			if (!j->error.ec)
			{
				std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

				m_stats_counters.inc_stats_counter(counters::num_blocks_read);
				m_stats_counters.inc_stats_counter(counters::num_read_ops);
				m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
				m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
			}

			if (ret == 0) break;

			offset += default_block_size;
		}

		j->d.piece_hash = h.final();
		return ret >= 0 ? status_t::no_error : status_t::fatal_disk_error;
	}

	status_t disk_io_thread::do_move_storage(disk_io_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// if files have to be closed, that's the storage's responsibility
		status_t ret;
		std::string p;
		std::tie(ret, p) = j->storage->move_storage(boost::get<std::string>(j->argument)
			, j->move_flags, j->error);

		boost::get<std::string>(j->argument) = p;
		return ret;
	}

	status_t disk_io_thread::do_release_files(disk_io_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);
		j->storage->release_files(j->error);
		return j->error ? status_t::fatal_disk_error : status_t::no_error;
	}

	status_t disk_io_thread::do_delete_files(disk_io_job* j)
	{
		TORRENT_ASSERT(boost::get<remove_flags_t>(j->argument));

		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);
		j->storage->delete_files(boost::get<remove_flags_t>(j->argument), j->error);
		return j->error ? status_t::fatal_disk_error : status_t::no_error;
	}

	status_t disk_io_thread::do_check_fastresume(disk_io_job* j)
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
				j->storage->initialize(m_settings, se);
				if (se)
				{
					j->error = se;
					return status_t::fatal_disk_error;
				}
				return status_t::need_full_check;
			}
		}

		storage_error se;
		j->storage->initialize(m_settings, se);
		if (se)
		{
			j->error = se;
			return status_t::fatal_disk_error;
		}
		return status_t::no_error;
	}

	status_t disk_io_thread::do_rename_file(disk_io_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// if files need to be closed, that's the storage's responsibility
		j->storage->rename_file(j->file_index, boost::get<std::string>(j->argument)
			, j->error);
		return j->error ? status_t::fatal_disk_error : status_t::no_error;
	}

	status_t disk_io_thread::do_stop_torrent(disk_io_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		j->storage->release_files(j->error);
		return j->error ? status_t::fatal_disk_error : status_t::no_error;
	}

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

		// gauges
		c.set_value(counters::disk_blocks_in_use, m_buffer_pool.in_use());
	}

	status_t disk_io_thread::do_file_priority(disk_io_job* j)
	{
		j->storage->set_file_priority(m_settings
			, boost::get<aux::vector<download_priority_t, file_index_t>>(j->argument)
			, j->error);
		return status_t::no_error;
	}

	// this job won't return until all outstanding jobs on this
	// piece are completed or cancelled and the buffers for it
	// have been evicted
	status_t disk_io_thread::do_clear_piece(disk_io_job*)
	{
		// there's nothing to do here, by the time this is called the jobs for
		// this storage has been completed since this is a fence job
		return status_t::no_error;
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

		int ret = j->storage->raise_fence(j, m_stats_counters);
		if (ret == aux::disk_job_fence::fence_post_fence)
		{
			std::unique_lock<std::mutex> l(m_job_mutex);
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);
			m_generic_io_jobs.m_queued_jobs.push_back(j);
			l.unlock();

			if (num_threads() == 0 && user_add)
				immediate_execute();

			return;
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
		TORRENT_ASSERT(!m_abort);

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
				time_point const now = aux::time_now();
				{
					std::unique_lock<std::mutex> l2(m_need_tick_mutex);
					while (!m_need_tick.empty() && m_need_tick.front().first < now)
					{
						std::shared_ptr<default_storage> st = m_need_tick.front().second.lock();
						m_need_tick.erase(m_need_tick.begin());
						if (st)
						{
							l2.unlock();
							st->tick();
							l2.lock();
						}
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

		DLOG("the last disk thread alive. cleaning up\n");

		abort_jobs();

		TORRENT_ASSERT(m_magic == 0x1337);
	}

	void disk_io_thread::abort_jobs()
	{
		DLOG("disk_io_thread::abort_jobs\n");

		TORRENT_ASSERT(m_magic == 0x1337);
		if (m_jobs_aborted.test_and_set()) return;

		// close all files. This may take a long
		// time on certain OSes (i.e. Mac OS)
		// that's why it's important to do this in
		// the disk thread in parallel with stopping
		// trackers.
		m_file_pool.release();
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

	void disk_io_thread::add_completed_jobs(jobqueue_t& jobs)
	{
		do
		{
			// when a job completes, it's possible for it to cause
			// a fence to be lowered, issuing the jobs queued up
			// behind the fence
			add_completed_jobs_impl(jobs);
			TORRENT_ASSERT(jobs.empty());
		} while (!jobs.empty());
	}

	void disk_io_thread::add_completed_jobs_impl(jobqueue_t& jobs)
	{
		jobqueue_t new_jobs;
		int ret = 0;
		for (auto i = jobs.iterate(); i.get(); i.next())
		{
			disk_io_job* j = i.get();
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) || !j->storage);

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
			{
				std::lock_guard<std::mutex> l(m_job_mutex);
				m_generic_io_jobs.m_queued_jobs.append(new_jobs);
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

			post(m_ios, [this] { this->call_job_handlers(); });
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

#endif // HAVE_MMAP || HAVE_MAP_VIEW_OF_FILE
