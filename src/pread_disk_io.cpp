/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/pread_storage.hpp"
#include "libtorrent/pread_disk_io.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/aux_/disk_buffer_pool.hpp"
#include "libtorrent/aux_/pread_disk_job.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/debug.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/platform_util.hpp" // for set_thread_name
#include "libtorrent/aux_/disk_job_pool.hpp"
#include "libtorrent/aux_/disk_io_thread_pool.hpp"
#include "libtorrent/aux_/store_buffer.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/storage_array.hpp"
#include "libtorrent/aux_/disk_completed_queue.hpp"
#include "libtorrent/aux_/debug_disk_thread.hpp"

#include <functional>

namespace libtorrent {
namespace {

	aux::open_mode_t file_mode_for_job(aux::pread_disk_job* j)
	{
		aux::open_mode_t ret = aux::open_mode::read_only;
		if (!(j->flags & disk_interface::sequential_access)) ret |= aux::open_mode::random_access;
		return ret;
	}

#if TORRENT_USE_ASSERTS
	bool valid_flags(disk_job_flags_t const flags)
	{
		return (flags & ~(disk_interface::force_copy
				| disk_interface::sequential_access
				| disk_interface::volatile_read
				| disk_interface::v1_hash
				| disk_interface::flush_piece))
			== disk_job_flags_t{};
	}
#endif
} // anonymous namespace

// this is a singleton consisting of the thread and a queue
// of disk io jobs
struct TORRENT_EXTRA_EXPORT pread_disk_io final
	: disk_interface
{
	pread_disk_io(io_context& ios, settings_interface const&, counters& cnt);
#if TORRENT_USE_ASSERTS
	~pread_disk_io() override;
#endif

	void settings_updated() override;
	storage_holder new_torrent(storage_params const& params
		, std::shared_ptr<void> const& owner) override;
	void remove_torrent(storage_index_t) override;

	void abort(bool wait) override;

	void async_read(storage_index_t storage, peer_request const& r
		, std::function<void(disk_buffer_holder, storage_error const&)> handler
		, disk_job_flags_t flags = {}) override;
	bool async_write(storage_index_t storage, peer_request const& r
		, char const* buf, std::shared_ptr<disk_observer> o
		, std::function<void(storage_error const&)> handler
		, disk_job_flags_t flags = {}) override;
	void async_hash(storage_index_t storage, piece_index_t piece, span<sha256_hash> v2
		, disk_job_flags_t flags
		, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler) override;
	void async_hash2(storage_index_t storage, piece_index_t piece, int offset, disk_job_flags_t flags
		, std::function<void(piece_index_t, sha256_hash const&, storage_error const&)> handler) override;
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

	void update_stats_counters(counters& c) const override;

	std::vector<open_file_state> get_status(storage_index_t) const override;

	// this submits all queued up jobs to the thread
	void submit_jobs() override;

	status_t do_job(aux::job::partial_read& a, aux::pread_disk_job* j);
	status_t do_job(aux::job::read& a, aux::pread_disk_job* j);
	status_t do_job(aux::job::write& a, aux::pread_disk_job* j);
	status_t do_job(aux::job::hash& a, aux::pread_disk_job* j);
	status_t do_job(aux::job::hash2& a, aux::pread_disk_job* j);

	status_t do_job(aux::job::move_storage& a, aux::pread_disk_job* j);
	status_t do_job(aux::job::release_files& a, aux::pread_disk_job* j);
	status_t do_job(aux::job::delete_files& a, aux::pread_disk_job* j);
	status_t do_job(aux::job::check_fastresume& a, aux::pread_disk_job* j);
	status_t do_job(aux::job::rename_file& a, aux::pread_disk_job* j);
	status_t do_job(aux::job::stop_torrent& a, aux::pread_disk_job* j);
	status_t do_job(aux::job::file_priority& a, aux::pread_disk_job* j);
	status_t do_job(aux::job::clear_piece& a, aux::pread_disk_job* j);

private:

	void thread_fun(aux::disk_io_thread_pool& pool
		, executor_work_guard<io_context::executor_type> work);

	void add_completed_jobs(jobqueue_t jobs);
	void add_completed_jobs_impl(jobqueue_t jobs, jobqueue_t& completed);

	void perform_job(aux::pread_disk_job* j, jobqueue_t& completed_jobs);

	// this queues up another job to be submitted
	void add_job(aux::pread_disk_job* j, bool user_add = true);
	void add_fence_job(aux::pread_disk_job* j, bool user_add = true);

	void execute_job(aux::pread_disk_job* j);
	void immediate_execute();
	void abort_jobs();
	void abort_hash_jobs(storage_index_t storage);

	// returns the maximum number of threads
	// the actual number of threads may be less
	int num_threads() const;
	aux::disk_io_thread_pool& pool_for_job(aux::pread_disk_job* j);

	// set to true once we start shutting down
	std::atomic<bool> m_abort{false};

	// this is a counter of how many threads are currently running.
	// it's used to identify the last thread still running while
	// shutting down. This last thread is responsible for cleanup
	// must hold the job mutex to access
	int m_num_running_threads = 0;

	aux::disk_job_pool<aux::pread_disk_job> m_job_pool;

	// std::mutex to protect the m_generic_threads and m_hash_threads lists
	mutable std::mutex m_job_mutex;

	// every write job is inserted into this map while it is in the job queue.
	// It is removed after the write completes. This will let subsequent reads
	// pull the buffers straight out of the queue instead of having to
	// synchronize with the writing thread(s)
	aux::store_buffer m_store_buffer;

	settings_interface const& m_settings;

	// LRU cache of open files
	aux::file_pool m_file_pool;

	// disk cache
	aux::disk_buffer_pool m_buffer_pool;

	// total number of blocks in use by both the read
	// and the write cache. This is not supposed to
	// exceed m_cache_size

	counters& m_stats_counters;

	// this is the main thread io_context. Callbacks are
	// posted on this in order to have them execute in
	// the main thread.
	io_context& m_ios;

	aux::disk_completed_queue m_completed_jobs;

	// storages that have had write activity recently and will get ticked
	// soon, for deferred actions (say, flushing partfile metadata)
	std::vector<std::pair<time_point, std::weak_ptr<aux::pread_storage>>> m_need_tick;
	std::mutex m_need_tick_mutex;

	aux::storage_array<aux::pread_storage> m_torrents;

	std::atomic_flag m_jobs_aborted = ATOMIC_FLAG_INIT;

	// most jobs are posted to m_generic_io_jobs
	// but hash jobs are posted to m_hash_io_jobs if m_hash_threads
	// has a non-zero maximum thread count
	aux::disk_io_thread_pool m_generic_threads;
	aux::disk_io_thread_pool m_hash_threads;
};

TORRENT_EXPORT std::unique_ptr<disk_interface> pread_disk_io_constructor(
	io_context& ios, settings_interface const& sett, counters& cnt)
{
	return std::make_unique<pread_disk_io>(ios, sett, cnt);
}

// ------- pread_disk_io ------

	// for _1 and _2
	using namespace std::placeholders;

	pread_disk_io::pread_disk_io(io_context& ios, settings_interface const& sett, counters& cnt)
		: m_settings(sett)
		, m_file_pool(sett.get_int(settings_pack::file_pool_size))
		, m_buffer_pool(ios)
		, m_stats_counters(cnt)
		, m_ios(ios)
		, m_completed_jobs([&](aux::disk_job** j, int const n) {
			m_job_pool.free_jobs(reinterpret_cast<aux::pread_disk_job**>(j), n);
			}, cnt)
		, m_generic_threads(std::bind(&pread_disk_io::thread_fun, this, _1, _2), ios)
		, m_hash_threads(std::bind(&pread_disk_io::thread_fun, this, _1, _2), ios)
	{
		settings_updated();
	}

	std::vector<open_file_state> pread_disk_io::get_status(storage_index_t const st) const
	{
		return m_file_pool.get_status(st);
	}

	storage_holder pread_disk_io::new_torrent(storage_params const& params
		, std::shared_ptr<void> const& owner)
	{
		TORRENT_ASSERT(params.files.is_valid());

		auto storage = std::make_shared<aux::pread_storage>(params, m_file_pool);
		storage->set_owner(owner);
		storage_index_t const idx = m_torrents.add(std::move(storage));
		return storage_holder(idx, *this);
	}

	void pread_disk_io::remove_torrent(storage_index_t const idx)
	{
		m_torrents.remove(idx);
	}

#if TORRENT_USE_ASSERTS
	pread_disk_io::~pread_disk_io()
	{
		DLOG("destructing pread_disk_io\n");

		// abort should have been triggered
		TORRENT_ASSERT(m_abort);

		// there are not supposed to be any writes in-flight by now
		TORRENT_ASSERT(m_store_buffer.size() == 0);

		// all torrents are supposed to have been removed by now
		TORRENT_ASSERT(m_torrents.empty());
	}
#endif

	void pread_disk_io::abort(bool const wait)
	{
		DLOG("pread_disk_io::abort: (wait: %d)\n", int(wait));

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
		m_hash_threads.visit_jobs([](aux::disk_job* j)
		{
			j->flags |= aux::disk_job::aborted;
		});
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

	void pread_disk_io::settings_updated()
	{
		m_buffer_pool.set_settings(m_settings);
		m_file_pool.resize(m_settings.get_int(settings_pack::file_pool_size));

		int const num_threads = m_settings.get_int(settings_pack::aio_threads);
		int const num_hash_threads = m_settings.get_int(settings_pack::hashing_threads);
		DLOG("set max threads(%d, %d)\n", num_threads, num_hash_threads);

		m_generic_threads.set_max_threads(num_threads);
		m_hash_threads.set_max_threads(num_hash_threads);
	}

	void pread_disk_io::perform_job(aux::pread_disk_job* j, jobqueue_t& completed_jobs)
	{
		TORRENT_ASSERT(j->next == nullptr);
		TORRENT_ASSERT((j->flags & aux::disk_job::in_progress) || !j->storage);

#if DEBUG_DISK_THREAD
		{
			std::unique_lock<std::mutex> l(m_job_mutex);

			DLOG("perform_job job: %s outstanding: %d\n"
				, print_job(*j).c_str()
				, j->storage ? j->storage->num_outstanding_jobs() : -1);
		}
#endif

		std::shared_ptr<aux::pread_storage> storage = j->storage;

		m_stats_counters.inc_stats_counter(counters::num_running_disk_jobs, 1);

		// call disk function
		// TODO: in the future, propagate exceptions back to the handlers
		status_t ret = status_t::no_error;
		try
		{
			ret = std::visit([this, j](auto& a) { return this->do_job(a, j); }, j->action);
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

	status_t pread_disk_io::do_job(aux::job::partial_read& a, aux::pread_disk_job* j)
	{
		TORRENT_ASSERT(a.buf);
		time_point const start_time = clock_type::now();

		span<char> const b = {a.buf.data() + a.buffer_offset, a.buffer_size};

		int const ret = j->storage->read(m_settings, b
			, a.piece, a.offset, file_mode_for_job(j), j->flags, j->error);

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

	status_t pread_disk_io::do_job(aux::job::read& a, aux::pread_disk_job* j)
	{
		a.buf = disk_buffer_holder(m_buffer_pool, m_buffer_pool.allocate_buffer("send buffer"), default_block_size);
		if (!a.buf)
		{
			j->error.ec = error::no_memory;
			j->error.operation = operation_t::alloc_cache_piece;
			return status_t::fatal_disk_error;
		}

		time_point const start_time = clock_type::now();

		aux::open_mode_t const file_mode = file_mode_for_job(j);
		span<char> const b = {a.buf.data(), a.buffer_size};

		int const ret = j->storage->read(m_settings, b
			, a.piece, a.offset, file_mode, j->flags, j->error);

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

	status_t pread_disk_io::do_job(aux::job::write& a, aux::pread_disk_job* j)
	{
		time_point const start_time = clock_type::now();
		auto buffer = std::move(a.buf);

		span<char> const b = { buffer.data(), a.buffer_size};
		aux::open_mode_t const file_mode = file_mode_for_job(j);

		m_stats_counters.inc_stats_counter(counters::num_writing_threads, 1);

		// the actual write operation
		int const ret = j->storage->write(m_settings, b
			, a.piece, a.offset, file_mode, j->flags, j->error);

		m_stats_counters.inc_stats_counter(counters::num_writing_threads, -1);

		if (!j->error.ec)
		{
			std::int64_t const write_time = total_microseconds(clock_type::now() - start_time);

			m_stats_counters.inc_stats_counter(counters::num_blocks_written);
			m_stats_counters.inc_stats_counter(counters::num_write_ops);
			m_stats_counters.inc_stats_counter(counters::disk_write_time, write_time);
			m_stats_counters.inc_stats_counter(counters::disk_job_time, write_time);
		}

		m_store_buffer.erase({j->storage->storage_index(), a.piece, a.offset});

		{
			std::lock_guard<std::mutex> l(m_need_tick_mutex);
			if (!j->storage->set_need_tick())
				m_need_tick.push_back({aux::time_now() + minutes(2), j->storage});
		}

		return ret != a.buffer_size
			? status_t::fatal_disk_error : status_t::no_error;
	}

	void pread_disk_io::async_read(storage_index_t storage, peer_request const& r
		, std::function<void(disk_buffer_holder, storage_error const&)> handler
		, disk_job_flags_t const flags)
	{
		TORRENT_ASSERT(valid_flags(flags));
		TORRENT_ASSERT(r.length <= default_block_size);
		TORRENT_ASSERT(r.length > 0);
		TORRENT_ASSERT(r.start >= 0);

		storage_error ec;
		if (r.length <= 0 || r.start < 0)
		{
			// this is an invalid read request.
			ec.ec = errors::invalid_request;
			ec.operation = operation_t::file_read;
			handler(disk_buffer_holder{}, ec);
			return;
		}

		// in case r.start is not aligned to a block, calculate that offset,
		// since that's how the store_buffer is indexed. block_offset is the
		// aligned offset to the first block this read touches. In the case the
		// request is aligned, it's the same as r.start
		int const block_offset = r.start - (r.start % default_block_size);
		// this is the offset into the block that we're reading from
		int const read_offset = r.start - block_offset;

		DLOG("async_read piece: %d block: %d (read-offset: %d)\n", static_cast<int>(r.piece)
			, block_offset / default_block_size, read_offset);

		disk_buffer_holder buffer;

		if (read_offset + r.length > default_block_size)
		{
			// This is an unaligned request spanning two blocks. One of the two
			// blocks may be in the store buffer, or neither.
			// If neither is in the store buffer, we can just issue a normal
			// read job for the unaligned request.

			aux::torrent_location const loc1{storage, r.piece, block_offset};
			aux::torrent_location const loc2{storage, r.piece, block_offset + default_block_size};
			std::ptrdiff_t const len1 = default_block_size - read_offset;

			TORRENT_ASSERT(r.length > len1);

			int const ret = m_store_buffer.get2(loc1, loc2, [&](char const* buf1, char const* buf2)
			{
				buffer = disk_buffer_holder(m_buffer_pool
					, m_buffer_pool.allocate_buffer("send buffer")
					, r.length);
				if (!buffer)
				{
					ec.ec = error::no_memory;
					ec.operation = operation_t::alloc_cache_piece;
					return 3;
				}

				if (buf1)
					std::memcpy(buffer.data(), buf1 + read_offset, std::size_t(len1));
				if (buf2)
					std::memcpy(buffer.data() + len1, buf2, std::size_t(r.length - len1));
				return (buf1 ? 2 : 0) | (buf2 ? 1 : 0);
			});

			if (ret == 3)
			{
				// both sides were found in the store buffer and the read request
				// was satisfied immediately
				handler(std::move(buffer), ec);
				return;
			}

			if (ret != 0)
			{
				TORRENT_ASSERT(ret == 1 || ret == 2);
				// only one side of the read request was found in the store
				// buffer, and we need to issue a partial read for the remaining
				// bytes
				aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::partial_read>(
					flags,
					m_torrents[storage]->shared_from_this(),
					std::move(handler),
					std::move(buffer),
					std::uint16_t((ret == 1) ? 0 : len1), // buffer_offset
					std::uint16_t((ret == 1) ? len1 : r.length - len1), // buffer_size
					r.piece,
					(ret == 1) ? r.start : block_offset + default_block_size // offset
				);

				add_job(j);
				return;
			}

			// if we couldn't find any block in the store buffer, just post it
			// as a normal read job
		}
		else
		{
			if (m_store_buffer.get({ storage, r.piece, block_offset }, [&](char const* buf)
			{
				buffer = disk_buffer_holder(m_buffer_pool, m_buffer_pool.allocate_buffer("send buffer"), r.length);
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
		}

		aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::read>(
			flags,
			m_torrents[storage]->shared_from_this(),
			std::move(handler),
			disk_buffer_holder{},
			std::uint16_t(r.length), // buffer_size
			r.piece,
			r.start // offset
		);

		add_job(j);
	}

	bool pread_disk_io::async_write(storage_index_t const storage, peer_request const& r
		, char const* buf, std::shared_ptr<disk_observer> o
		, std::function<void(storage_error const&)> handler
		, disk_job_flags_t const flags)
	{
		TORRENT_ASSERT(valid_flags(flags));
		bool exceeded = false;
		disk_buffer_holder buffer(m_buffer_pool, m_buffer_pool.allocate_buffer(
			exceeded, o, "receive buffer"), default_block_size);
		if (!buffer) aux::throw_ex<std::bad_alloc>();
		std::memcpy(buffer.data(), buf, aux::numeric_cast<std::size_t>(r.length));

		TORRENT_ASSERT(r.start % default_block_size == 0);
		TORRENT_ASSERT(r.length <= default_block_size);

		auto data_ptr = buffer.data();

		aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::write>(
			flags,
			m_torrents[storage]->shared_from_this(),
			std::move(handler),
			std::move(buffer),
			r.piece,
			r.start,
			std::uint16_t(r.length)
		);

		m_store_buffer.insert({j->storage->storage_index(), r.piece, r.start}, data_ptr);

		add_job(j);
		return exceeded;
	}

	void pread_disk_io::async_hash(storage_index_t const storage
		, piece_index_t const piece, span<sha256_hash> const v2, disk_job_flags_t const flags
		, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler)
	{
		TORRENT_ASSERT(valid_flags(flags));
		aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::hash>(
			flags,
			m_torrents[storage]->shared_from_this(),
			std::move(handler),
			piece,
			v2,
			sha1_hash{}
		);
		add_job(j);
	}

	void pread_disk_io::async_hash2(storage_index_t const storage
		, piece_index_t const piece, int const offset, disk_job_flags_t const flags
		, std::function<void(piece_index_t, sha256_hash const&, storage_error const&)> handler)
	{
		TORRENT_ASSERT(valid_flags(flags));
		aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::hash2>(
			flags,
			m_torrents[storage]->shared_from_this(),
			std::move(handler),
			piece,
			offset,
			sha256_hash{}
		);
		add_job(j);
	}

	void pread_disk_io::async_move_storage(storage_index_t const storage
		, std::string p, move_flags_t const flags
		, std::function<void(status_t, std::string const&, storage_error const&)> handler)
	{
		aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::move_storage>(
			{},
			m_torrents[storage]->shared_from_this(),
			std::move(handler),
			std::move(p), // path
			flags
		);

		add_fence_job(j);
	}

	void pread_disk_io::async_release_files(storage_index_t const storage
		, std::function<void()> handler)
	{
		aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::release_files>(
			{},
			m_torrents[storage]->shared_from_this(),
			std::move(handler)
		);

		add_fence_job(j);
	}

	void pread_disk_io::abort_hash_jobs(storage_index_t const storage)
	{
		// abort outstanding hash jobs belonging to this torrent
		std::unique_lock<std::mutex> l(m_job_mutex);

		auto st = m_torrents[storage]->shared_from_this();
		// hash jobs
		m_hash_threads.visit_jobs([&](aux::disk_job* gj)
		{
			auto* j = static_cast<aux::pread_disk_job*>(gj);
			if (j->storage != st) return;
			// only cancel volatile-read jobs. This means only full checking
			// jobs. These jobs are likely to have a pretty deep queue and
			// really gain from being cancelled. They can also be restarted
			// easily.
			if (j->flags & disk_interface::volatile_read)
				j->flags |= aux::disk_job::aborted;
		});
	}

	void pread_disk_io::async_delete_files(storage_index_t const storage
		, remove_flags_t const options
		, std::function<void(storage_error const&)> handler)
	{
		abort_hash_jobs(storage);
		aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::delete_files>(
			{},
			m_torrents[storage]->shared_from_this(),
			std::move(handler),
			options
		);
		add_fence_job(j);
	}

	void pread_disk_io::async_check_files(storage_index_t const storage
		, add_torrent_params const* resume_data
		, aux::vector<std::string, file_index_t> links
		, std::function<void(status_t, storage_error const&)> handler)
	{
		aux::vector<std::string, file_index_t>* links_vector = nullptr;
		if (!links.empty()) links_vector = new aux::vector<std::string, file_index_t>(std::move(links));

		aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::check_fastresume>(
			{},
			m_torrents[storage]->shared_from_this(),
			std::move(handler),
			links_vector,
			resume_data
		);

		add_fence_job(j);
	}

	void pread_disk_io::async_rename_file(storage_index_t const storage
		, file_index_t const index, std::string name
		, std::function<void(std::string const&, file_index_t, storage_error const&)> handler)
	{
		aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::rename_file>(
			{},
			m_torrents[storage]->shared_from_this(),
			std::move(handler),
			index,
			std::move(name)
		);
		add_fence_job(j);
	}

	void pread_disk_io::async_stop_torrent(storage_index_t const storage
		, std::function<void()> handler)
	{
		auto st = m_torrents[storage]->shared_from_this();
		abort_hash_jobs(storage);

		aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::stop_torrent>(
			{},
			m_torrents[storage]->shared_from_this(),
			std::move(handler)
		);
		add_fence_job(j);
	}

	void pread_disk_io::async_set_file_priority(storage_index_t const storage
		, aux::vector<download_priority_t, file_index_t> prios
		, std::function<void(storage_error const&
			, aux::vector<download_priority_t, file_index_t>)> handler)
	{
		aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::file_priority>(
			{},
			m_torrents[storage]->shared_from_this(),
			std::move(handler),
			std::move(prios)
		);

		add_fence_job(j);
	}

	void pread_disk_io::async_clear_piece(storage_index_t const storage
		, piece_index_t const index, std::function<void(piece_index_t)> handler)
	{
		aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::clear_piece>(
			{},
			m_torrents[storage]->shared_from_this(),
			std::move(handler),
			index
		);

		// regular jobs are not guaranteed to be executed in-order
		// since clear piece must guarantee that all write jobs that
		// have been issued finish before the clear piece job completes

		// TODO: this is potentially very expensive. One way to solve
		// it would be to have a fence for just this one piece.
		// but it hardly seems worth the complexity and cost just for the edge
		// case of receiving a corrupt piece
		add_fence_job(j);
	}

	status_t pread_disk_io::do_job(aux::job::hash& a, aux::pread_disk_job* j)
	{
		// we're not using a cache. This is the simple path
		// just read straight from the file
		bool const v1 = bool(j->flags & disk_interface::v1_hash);
		bool const v2 = !a.block_hashes.empty();

		int const piece_size = v1 ? j->storage->files().piece_size(a.piece) : 0;
		int const piece_size2 = v2 ? j->storage->files().piece_size2(a.piece) : 0;
		int const blocks_in_piece = v1 ? (piece_size + default_block_size - 1) / default_block_size : 0;
		int const blocks_in_piece2 = v2 ? j->storage->files().blocks_in_piece2(a.piece) : 0;
		aux::open_mode_t const file_mode = file_mode_for_job(j);

		TORRENT_ASSERT(!v2 || int(a.block_hashes.size()) >= blocks_in_piece2);
		TORRENT_ASSERT(v1 || v2);

		hasher h;
		int ret = 0;
		int offset = 0;
		int const blocks_to_read = std::max(blocks_in_piece, blocks_in_piece2);

		// Since there may be outstanding write operations on this piece,
		// we have to check the store buffer for every block. However, if
		// there are no blocks being written, we would like to issue as
		// large of a read as possible (i.e. not just individual blocks).
		// so we defer calling hash() and hash2()
		int deferred_offset = 0;
		int deferred_length = 0;

		for (int i = 0; i < blocks_to_read; ++i)
		{
			bool const v2_block = i < blocks_in_piece2;

			DLOG("do_hash: reading (piece: %d block: %d)\n", int(a.piece), i);

			time_point const start_time = clock_type::now();

			std::ptrdiff_t const len = v1 ? std::min(default_block_size, piece_size - offset) : 0;
			std::ptrdiff_t const len2 = v2_block ? std::min(default_block_size, piece_size2 - offset) : 0;

			hasher256 h2;

			if (!m_store_buffer.get({ j->storage->storage_index(), a.piece, offset }
				, [&](char const* buf)
				{
					if (deferred_length > 0)
					{
						// if we will call hash2() in a bit, don't trigger a flush
						// just yet, let hash2() do it
						auto const flags = v2_block ? (j->flags & ~disk_interface::flush_piece) : j->flags;
						j->error.ec.clear();
						ret = j->storage->hash(m_settings, h, deferred_length, a.piece
							, deferred_offset, file_mode, flags, j->error);
						// TODO: handle errors here
						deferred_length = 0;
					}
					if (v1)
					{
						h.update({ buf, len });
						ret = int(len);
					}
					if (v2_block)
					{
						h2.update({ buf, len2 });
						ret = int(len2);
					}
					deferred_offset = offset + len;
				}))
			{
				if (v1)
				{
					deferred_length += len;
					ret = len;
				}

				if (v2_block)
				{
					j->error.ec.clear();
					ret = j->storage->hash2(m_settings, h2, len2, a.piece, offset
						, file_mode, j->flags, j->error);
					if (ret < 0) break;
				}
			}

			if (!j->error.ec)
			{
				std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

				m_stats_counters.inc_stats_counter(counters::num_blocks_read, blocks_to_read);
				m_stats_counters.inc_stats_counter(counters::num_read_ops);
				m_stats_counters.inc_stats_counter(counters::disk_hash_time, read_time);
				m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
			}

			if (v2_block)
				a.block_hashes[i] = h2.final();

			if (ret <= 0) break;

			offset += default_block_size;
		}

		if (deferred_length > 0)
		{
			j->error.ec.clear();
			ret = j->storage->hash(m_settings, h, deferred_length, a.piece
				, deferred_offset, file_mode, j->flags, j->error);
		}

		if (v1)
			a.piece_hash = h.final();
		return ret >= 0 ? status_t::no_error : status_t::fatal_disk_error;
	}

	status_t pread_disk_io::do_job(aux::job::hash2& a, aux::pread_disk_job* j)
	{
		int const piece_size = j->storage->files().piece_size2(a.piece);
		aux::open_mode_t const file_mode = file_mode_for_job(j);

		hasher256 h;
		int ret = 0;

		DLOG("do_hash2: reading (piece: %d offset: %d)\n", int(a.piece), int(a.offset));

		time_point const start_time = clock_type::now();

		TORRENT_ASSERT(piece_size > a.offset);
		std::ptrdiff_t const len = std::min(default_block_size, piece_size - a.offset);

		if (!m_store_buffer.get({ j->storage->storage_index(), a.piece, a.offset }
			, [&](char const* buf)
		{
			h.update({ buf, len });
			ret = int(len);
		}))
		{
			ret = j->storage->hash2(m_settings, h, len, a.piece, a.offset
				, file_mode, j->flags, j->error);
			if (ret < 0) return status_t::fatal_disk_error;
		}

		if (!j->error.ec)
		{
			std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

			m_stats_counters.inc_stats_counter(counters::num_blocks_read);
			m_stats_counters.inc_stats_counter(counters::num_read_ops);
			m_stats_counters.inc_stats_counter(counters::disk_hash_time, read_time);
			m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
		}

		a.piece_hash2 = h.final();
		return ret >= 0 ? status_t::no_error : status_t::fatal_disk_error;
	}

	status_t pread_disk_io::do_job(aux::job::move_storage& a, aux::pread_disk_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// if files have to be closed, that's the storage's responsibility
		auto const [ret, p] = j->storage->move_storage(std::move(a.path), a.move_flags, j->error);

		a.path = std::move(p);
		return ret;
	}

	status_t pread_disk_io::do_job(aux::job::release_files&, aux::pread_disk_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);
		j->storage->release_files(j->error);
		return j->error ? status_t::fatal_disk_error : status_t::no_error;
	}

	status_t pread_disk_io::do_job(aux::job::delete_files& a, aux::pread_disk_job* j)
	{
		TORRENT_ASSERT(a.flags);

		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);
		j->storage->delete_files(a.flags, j->error);
		return j->error ? status_t::fatal_disk_error : status_t::no_error;
	}

	status_t pread_disk_io::do_job(aux::job::check_fastresume& a, aux::pread_disk_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		add_torrent_params const* rd = a.resume_data;
		add_torrent_params tmp;
		if (rd == nullptr) rd = &tmp;

		std::unique_ptr<aux::vector<std::string, file_index_t>> links(a.links);
		// check if the fastresume data is up to date
		// if it is, use it and return true. If it
		// isn't return false and the full check
		// will be run. If the links pointer is non-empty, it has the same number
		// of elements as there are files. Each element is either empty or contains
		// the absolute path to a file identical to the corresponding file in this
		// torrent. The storage must create hard links (or copy) those files. If
		// any file does not exist or is inaccessible, the disk job must fail.

		TORRENT_ASSERT(j->storage->files().piece_length() > 0);

		// always initialize the storage
		auto const ret_flag = j->storage->initialize(m_settings, j->error);
		if (j->error) return status_t::fatal_disk_error | ret_flag;

		// we must call verify_resume() unconditionally of the setting below, in
		// order to set up the links (if present)
		bool const verify_success = j->storage->verify_resume_data(*rd
			, links ? *links : aux::vector<std::string, file_index_t>(), j->error);

		// j->error may have been set at this point, by verify_resume_data()
		// it's important to not have it cleared out subsequent calls, as long
		// as they succeed.

		if (m_settings.get_bool(settings_pack::no_recheck_incomplete_resume))
			return status_t::no_error | ret_flag;

		if (!aux::contains_resume_data(*rd))
		{
			// if we don't have any resume data, we still may need to trigger a
			// full re-check, if there are *any* files.
			storage_error ignore;
			return ((j->storage->has_any_file(ignore))
				? status_t::need_full_check
				: status_t::no_error)
				| ret_flag;
		}

		return (verify_success
			? status_t::no_error
			: status_t::need_full_check)
			| ret_flag;
	}

	status_t pread_disk_io::do_job(aux::job::rename_file& a, aux::pread_disk_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

		// if files need to be closed, that's the storage's responsibility
		j->storage->rename_file(a.file_index, a.name, j->error);
		return j->error ? status_t::fatal_disk_error : status_t::no_error;
	}

	status_t pread_disk_io::do_job(aux::job::stop_torrent&, aux::pread_disk_job* j)
	{
		// if this assert fails, something's wrong with the fence logic
		TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);
		j->storage->release_files(j->error);
		return j->error ? status_t::fatal_disk_error : status_t::no_error;
	}

	void pread_disk_io::update_stats_counters(counters& c) const
	{
		// These are atomic_counts, so it's safe to access them from
		// a different thread
		std::unique_lock<std::mutex> jl(m_job_mutex);

		c.set_value(counters::num_read_jobs, m_job_pool.read_jobs_in_use());
		c.set_value(counters::num_write_jobs, m_job_pool.write_jobs_in_use());
		c.set_value(counters::num_jobs, m_job_pool.jobs_in_use());
		c.set_value(counters::queued_disk_jobs, m_generic_threads.queue_size()
			+ m_hash_threads.queue_size());

		jl.unlock();

		// gauges
		c.set_value(counters::disk_blocks_in_use, m_buffer_pool.in_use());
	}

	status_t pread_disk_io::do_job(aux::job::file_priority& a, aux::pread_disk_job* j)
	{
		j->storage->set_file_priority(m_settings
			, a.prio
			, j->error);
		return status_t::no_error;
	}

	// this job won't return until all outstanding jobs on this
	// piece are completed or cancelled and the buffers for it
	// have been evicted
	status_t pread_disk_io::do_job(aux::job::clear_piece&, aux::pread_disk_job*)
	{
		// there's nothing to do here, by the time this is called the jobs for
		// this storage has been completed since this is a fence job
		return status_t::no_error;
	}

	void pread_disk_io::add_fence_job(aux::pread_disk_job* j, bool const user_add)
	{
		// if this happens, it means we started to shut down
		// the disk threads too early. We have to post all jobs
		// before the disk threads are shut down
		if (m_abort)
		{
			m_completed_jobs.abort_job(m_ios, j);
			return;
		}

		DLOG("add_fence:job: %s (outstanding: %d)\n"
			, print_job(*j).c_str()
			, j->storage->num_outstanding_jobs());

		TORRENT_ASSERT(j->storage);
		m_stats_counters.inc_stats_counter(counters::num_fenced_read + static_cast<int>(j->get_type()));

		int ret = j->storage->raise_fence(j, m_stats_counters);
		if (ret == aux::disk_job_fence::fence_post_fence)
		{
			std::unique_lock<std::mutex> l(m_job_mutex);
			TORRENT_ASSERT((j->flags & aux::disk_job::in_progress) || !j->storage);
			m_generic_threads.push_back(j);
			l.unlock();
		}

		if (num_threads() == 0 && user_add)
			immediate_execute();
	}

	void pread_disk_io::add_job(aux::pread_disk_job* j, bool const user_add)
	{
		TORRENT_ASSERT(!j->storage || j->storage->files().is_valid());
		TORRENT_ASSERT(j->next == nullptr);
		// if this happens, it means we started to shut down
		// the disk threads too early. We have to post all jobs
		// before the disk threads are shut down
		if (m_abort)
		{
			m_completed_jobs.abort_job(m_ios, j);
			return;
		}

		TORRENT_ASSERT(!(j->flags & aux::disk_job::in_progress));

		DLOG("add_job: %s (outstanding: %d)\n"
			, print_job(*j).c_str()
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
				, print_job(*j).c_str(), j->storage ? j->storage->num_blocked() : 0
				, int(m_stats_counters[counters::blocked_disk_jobs]));
			return;
		}

		std::unique_lock<std::mutex> l(m_job_mutex);

		TORRENT_ASSERT((j->flags & aux::disk_job::in_progress) || !j->storage);

		auto& q = pool_for_job(j);
		q.push_back(j);
		l.unlock();
		// if we literally have 0 disk threads, we have to execute the jobs
		// immediately. If add job is called internally by the pread_disk_io,
		// we need to defer executing it. We only want the top level to loop
		// over the job queue (as is done below)
		if (pool_for_job(j).max_threads() == 0 && user_add)
			immediate_execute();
	}

	void pread_disk_io::immediate_execute()
	{
		while (!m_generic_threads.empty())
		{
			auto* j = static_cast<aux::pread_disk_job*>(m_generic_threads.pop_front());
			execute_job(j);
		}
	}

	void pread_disk_io::submit_jobs()
	{
		std::unique_lock<std::mutex> l(m_job_mutex);
		m_generic_threads.submit_jobs();
		m_hash_threads.submit_jobs();
	}

	void pread_disk_io::execute_job(aux::pread_disk_job* j)
	{
		jobqueue_t completed_jobs;
		if (j->flags & aux::disk_job::aborted)
		{
			j->ret = status_t::fatal_disk_error;
			j->error = storage_error(boost::asio::error::operation_aborted);
			completed_jobs.push_back(j);
			add_completed_jobs(std::move(completed_jobs));
			return;
		}

		perform_job(j, completed_jobs);
		if (!completed_jobs.empty())
			add_completed_jobs(std::move(completed_jobs));
	}

	void pread_disk_io::thread_fun(aux::disk_io_thread_pool& pool
		, executor_work_guard<io_context::executor_type> work)
	{
		// work is used to keep the io_context alive
		TORRENT_UNUSED(work);

		ADD_OUTSTANDING_ASYNC("pread_disk_io::work");
		std::thread::id const thread_id = std::this_thread::get_id();

		aux::set_thread_name("Disk");

		DLOG("started disk thread\n");

		std::unique_lock<std::mutex> l(m_job_mutex);

		++m_num_running_threads;
		m_stats_counters.inc_stats_counter(counters::num_running_threads, 1);

		// we call close_oldest_file on the file_pool regularly. This is the next
		// time we should call it
		time_point next_close_oldest_file = min_time();

		for (;;)
		{
			aux::pread_disk_job* j = nullptr;
			bool const should_exit = pool.wait_for_job(l);
			if (should_exit) break;
			j = static_cast<aux::pread_disk_job*>(pool.pop_front());
			l.unlock();

			TORRENT_ASSERT((j->flags & aux::disk_job::in_progress) || !j->storage);

			if (&pool == &m_generic_threads && thread_id == pool.first_thread_id())
			{
				time_point const now = aux::time_now();
				{
					std::unique_lock<std::mutex> l2(m_need_tick_mutex);
					while (!m_need_tick.empty() && m_need_tick.front().first < now)
					{
						std::shared_ptr<aux::pread_storage> st = m_need_tick.front().second.lock();
						m_need_tick.erase(m_need_tick.begin());
						if (st)
						{
							l2.unlock();
							st->tick();
							l2.lock();
						}
					}
				}

				if (now > next_close_oldest_file)
				{
					seconds const interval(m_settings.get_int(settings_pack::close_file_interval));
					if (interval <= seconds(0))
					{
						// check again in one minute, in case the setting changed
						next_close_oldest_file = now + minutes(1);
					}
					else
					{
						next_close_oldest_file = now + interval;
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

		int const threads_left = --m_num_running_threads;
		if (threads_left > 0 || !m_abort)
		{
			DLOG("exiting disk thread. num_threads: %d aborting: %d\n"
				, threads_left, int(m_abort));
			m_stats_counters.inc_stats_counter(counters::num_running_threads, -1);
			COMPLETE_ASYNC("pread_disk_io::work");
			return;
		}

		DLOG("last thread alive. (left: %d) cleaning up. (generic-jobs: %d hash-jobs: %d)\n"
			, threads_left
			, m_generic_threads.queue_size()
			, m_hash_threads.queue_size());

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

		m_stats_counters.inc_stats_counter(counters::num_running_threads, -1);
		COMPLETE_ASYNC("pread_disk_io::work");
	}

	void pread_disk_io::abort_jobs()
	{
		DLOG("pread_disk_io::abort_jobs\n");

		if (m_jobs_aborted.test_and_set()) return;

		// close all files. This may take a long
		// time on certain OSes (i.e. Mac OS)
		// that's why it's important to do this in
		// the disk thread in parallel with stopping
		// trackers.
		m_file_pool.release();
	}

	int pread_disk_io::num_threads() const
	{
		return m_generic_threads.max_threads() + m_hash_threads.max_threads();
	}

	aux::disk_io_thread_pool& pread_disk_io::pool_for_job(aux::pread_disk_job* j)
	{
		if (m_hash_threads.max_threads() > 0
			&& (j->get_type() == aux::job_action_t::hash
				|| j->get_type() == aux::job_action_t::hash2))
			return m_hash_threads;
		else
			return m_generic_threads;
	}

	void pread_disk_io::add_completed_jobs(jobqueue_t jobs)
	{
		jobqueue_t completed = std::move(jobs);
		do
		{
			// when a job completes, it's possible for it to cause
			// a fence to be lowered, issuing the jobs queued up
			// behind the fence
			jobqueue_t new_jobs;
			add_completed_jobs_impl(std::move(completed), new_jobs);
			TORRENT_ASSERT(completed.empty());
			completed = std::move(new_jobs);
		} while (!completed.empty());
	}

	void pread_disk_io::add_completed_jobs_impl(jobqueue_t jobs, jobqueue_t& completed)
	{
		jobqueue_t new_jobs;
		int ret = 0;
		for (auto i = jobs.iterate(); i.get(); i.next())
		{
			auto* j = static_cast<aux::pread_disk_job*>(i.get());
			TORRENT_ASSERT((j->flags & aux::disk_job::in_progress) || !j->storage);

			if (j->flags & aux::disk_job::fence)
			{
				m_stats_counters.inc_stats_counter(
					counters::num_fenced_read + static_cast<int>(j->get_type()), -1);
			}

			TORRENT_ASSERT(j->storage);
			if (j->storage)
				ret += j->storage->job_complete(j, new_jobs);

			TORRENT_ASSERT(ret == new_jobs.size());
			TORRENT_ASSERT(!(j->flags & aux::disk_job::in_progress));
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

		if (m_abort.load())
		{
			while (!new_jobs.empty())
			{
				auto* j = static_cast<aux::pread_disk_job*>(new_jobs.pop_front());
				TORRENT_ASSERT((j->flags & aux::disk_job::in_progress) || !j->storage);
				j->ret = status_t::fatal_disk_error;
				j->error = storage_error(boost::asio::error::operation_aborted);
				completed.push_back(j);
			}
		}
		else
		{
			if (!new_jobs.empty())
			{
				{
					std::lock_guard<std::mutex> l(m_job_mutex);
					m_generic_threads.append(std::move(new_jobs));
				}

				{
					std::lock_guard<std::mutex> l(m_job_mutex);
					m_generic_threads.submit_jobs();
				}
			}
		}

		m_completed_jobs.append(m_ios, std::move(jobs));
	}
}
