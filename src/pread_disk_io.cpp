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
#include "libtorrent/aux_/disk_cache.hpp"
#include "libtorrent/aux_/visit_block_iovecs.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/storage_array.hpp"
#include "libtorrent/aux_/disk_completed_queue.hpp"
#include "libtorrent/aux_/debug_disk_thread.hpp"
#include "libtorrent/aux_/scope_end.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace libtorrent {
namespace {

aux::open_mode_t file_mode_for_job(aux::pread_disk_job* j)
{
	aux::open_mode_t ret = aux::open_mode::read_only;
	if (j->flags & disk_interface::sequential_access) ret |= aux::open_mode::sequential_access;
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

// Copy SHA-256 block hashes that the v2 hash queue precomputed for this
// piece into block_hashes, filling only slots that are still all-zero.
void fill_precomputed_v2(
	span<sha256_hash> block_hashes, aux::vector<sha256_hash> const& pc, int const blocks_in_piece2)
{
	int const to_copy = std::min({int(pc.size()), int(block_hashes.size()), blocks_in_piece2});
	for (int i = 0; i < to_copy; ++i)
		if (block_hashes[i].is_all_zeros()) block_hashes[i] = pc[i];
}

template <typename Fun>
status_t translate_error(aux::disk_job* j, Fun f)
{
	try
	{
		return f();
	}
	catch (boost::system::system_error const& err)
	{
		j->error.ec = err.code();
		j->error.operation = operation_t::exception;
		return disk_status::fatal_disk_error;
	}
	catch (std::bad_alloc const&)
	{
		j->error.ec = errors::no_memory;
		j->error.operation = operation_t::exception;
		return disk_status::fatal_disk_error;
	}
	catch (std::exception const&)
	{
		j->error.ec = boost::asio::error::fault;
		j->error.operation = operation_t::exception;
		return disk_status::fatal_disk_error;
	}
}

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

	// insert a write job's block into the cache. Returns the insert flags
	// (need_hasher_kick / exceeded_limit). Does NOT kick the hasher or flush --
	// that re-enters the fence (add_job/add_completed_jobs), so it must run with
	// no fence mutex held. The repost callback in add_completed_jobs_impl() calls
	// this while a fence mutex is held and defers kick_write_hashers() to after.
	aux::insert_result_flags insert_write(aux::pread_disk_job* j, std::shared_ptr<disk_observer> o);

	// wake the hasher for blocks just inserted (and, with no hash threads, drain
	// the v2 hash queue inline). Must run with no fence mutex held.
	void kick_write_hashers();

	// inserts a write job's block into the cache and performs the hasher kick and
	// flush bookkeeping. Used by async_write() (inline, no fence). Returns true if
	// the disk buffer pool is over its limit (back-pressure).
	bool add_write_to_cache(aux::pread_disk_job* j, std::shared_ptr<disk_observer> o);

	// the single place a read job consults the cache and is turned into queued
	// work. It runs once the job is past the fence gate -- in add_job() with the
	// fence down, or in the repost callback when a fence lowers -- so it always
	// sees the current cache (do_job(read) reads straight from disk, unlike
	// do_job(hash)/do_job(hash2), which already consult the cache).
	//
	//  - the whole request is in the cache: fills the job's buffer and returns
	//    true (the caller completes it).
	//  - an unaligned, block-spanning request has only the block on one side in
	//    the cache: copies that side into a fresh buffer and transforms j into a
	//    job::partial_read for the missing side, then returns false. The
	//    partial_read therefore reads only from disk and never re-consults the
	//    cache.
	//  - nothing is in the cache: leaves j a job::read and returns false (read
	//    from disk).
	//
	// Only does m_cache.get/get2 -- no completion -- so it is safe to call with a
	// fence mutex held.
	bool prepare_read(aux::pread_disk_job* j);

	// flush the cache if it is over its watermark (or schedule a flush). Called
	// after add_write_to_cache(); separated because a synchronous flush can
	// complete and free the write job.
	void schedule_flush();

	void perform_job(aux::pread_disk_job* j, jobqueue_t& completed_jobs);

	// this queues up another job to be submitted
	void add_job(aux::pread_disk_job* j, bool user_add = true);
	void add_fence_job(aux::pread_disk_job* j, bool user_add = true);

	void execute_job(aux::pread_disk_job* j);
	void immediate_execute();
	void abort_jobs();
	void abort_hash_jobs(storage_index_t storage);

	void try_flush_cache(int target_cache_size
		, bool optimistic
		, std::unique_lock<std::mutex>& l);
	void flush_storage(std::shared_ptr<aux::pread_storage> const& storage);

	// flush any storages queued in m_fence_flush (a fence is waiting on their
	// outstanding writes). m_job_mutex is held on entry and exit; it is released
	// while flushing.
	void flush_fenced_storages(std::unique_lock<std::mutex>& l);

	int flush_cache_blocks(
		bitfield& flushed, span<aux::disk_job* const> blocks, jobqueue_t& completed_jobs);
	void clear_piece_jobs(jobqueue_t aborted, aux::disk_job* clear, jobqueue_t& completed);

	aux::disk_io_thread_pool& pool_for_job(aux::pread_disk_job* j);

	// set to true once we start shutting down. Guarded by m_job_mutex: it's
	// written, and read by the disk threads, under the mutex. The lock-free
	// reads (in add_job(), add_fence_job() and the destructor) only run on the
	// network thread (the only writer's thread) or after the disk threads have
	// exited, so they don't race with the write.
	bool m_abort = false;

	// this is a counter of how many threads are currently running.
	// it's used to identify the last thread still running while
	// shutting down. This last thread is responsible for cleanup
	// must hold the job mutex to access
	int m_num_running_threads = 0;

	// std::mutex to protect the m_generic_threads and m_hash_threads lists
	mutable std::mutex m_job_mutex;

	// when set, it means we're trying to flush the disk cache down to this size
	// it's a signal to generic disk threads to start flushing. Once flushing
	// starts, m_flush_target is cleared.
	std::optional<int> m_flush_target = std::nullopt;

	settings_interface const& m_settings;

	// LRU cache of open files
	aux::file_pool m_file_pool;

	// disk cache
	aux::disk_buffer_pool m_buffer_pool;

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

	// storages that have a fence queued behind their outstanding cache writes.
	// Those writes won't flush on their own (they can be partial pieces the
	// optimistic pass skips, with the cache under its watermark), so the fence
	// would wait forever. A generic disk thread flushes each so the writes drain
	// and the fence can run. Guarded by m_job_mutex.
	// this must be declared (and so destructed) after m_file_pool, since a
	// pread_storage held alive only by this vector will call back into
	// m_file_pool from its destructor.
	std::vector<std::shared_ptr<aux::pread_storage>> m_fence_flush;

	std::atomic_flag m_jobs_aborted = ATOMIC_FLAG_INIT;

	// every write job is inserted into this map while it is in the job queue.
	// It is removed after the write completes. This will let subsequent reads
	// pull the buffers straight out of the queue instead of having to
	// synchronize with the writing thread(s)
	aux::disk_cache m_cache;

	// most jobs are posted to m_generic_io_jobs
	// but hash jobs are posted to m_hash_io_jobs if m_hash_threads
	// has a non-zero maximum thread count
	aux::disk_io_thread_pool m_generic_threads;
	aux::disk_io_thread_pool m_hash_threads;

	// the jobs must be destructed first, since they will likely depend on the
	// disk cache to free buffers and file_pool to close files
	aux::disk_job_pool<aux::pread_disk_job> m_job_pool;
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
	, m_stats_counters(cnt)
	, m_ios(ios)
	, m_completed_jobs([&](aux::disk_job** j, int const n) {
		m_job_pool.free_jobs(reinterpret_cast<aux::pread_disk_job**>(j), n);
		}, cnt)
	, m_cache(ios)
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

	// m_fence_flush holds at most one entry per storage (deduped via
	// pread_storage::in_fence_flush()), so reserving a slot per storage here means
	// pushing to it never allocates -- it happens on the job-completion path, which
	// must not throw. m_fence_flush is guarded by m_job_mutex and touched by the
	// disk threads, so the (possibly reallocating) reserve has to hold the lock.
	std::lock_guard<std::mutex> l(m_job_mutex);
	m_fence_flush.reserve(static_cast<std::size_t>(m_torrents.num_slots() + 1));

	auto storage = std::make_shared<aux::pread_storage>(params, m_file_pool);
	storage->set_owner(owner);
	storage_index_t const idx = m_torrents.add(std::move(storage));
	return {idx, *this};
}

void pread_disk_io::remove_torrent(storage_index_t const idx)
{
	// purge the cache so stale entries can't collide with a future torrent
	// that reuses this index. The torrent is fully torn down by now, so no
	// jobs should still be attached -- abort any defensively.
	jobqueue_t aborted;
	m_cache.remove_storage(idx, aborted);
	TORRENT_ASSERT(aborted.empty());
	m_completed_jobs.abort_jobs(m_ios, std::move(aborted));
	m_torrents.remove(idx);
}

#if TORRENT_USE_ASSERTS
pread_disk_io::~pread_disk_io()
{
	DLOG("destructing pread_disk_io\n");

	// abort should have been triggered
	TORRENT_ASSERT(m_abort);

	// there are not supposed to be any writes in-flight by now
	TORRENT_ASSERT(m_cache.size() == 0);

	// all torrents are supposed to have been removed by now
	TORRENT_ASSERT(m_torrents.empty());

	// every fenced storage is supposed to have been flushed by now. If one
	// were left, destroying m_fence_flush here would drop the last reference
	// to its pread_storage, which would call back into the (already
	// destructed) m_file_pool.
	TORRENT_ASSERT(m_fence_flush.empty());
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
	if (m_abort) return;
	m_abort = true;
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
	m_cache.set_max_size(m_settings.get_int(settings_pack::max_queued_disk_bytes) / default_block_size);
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
	auto se = aux::scope_end([&] {
		m_stats_counters.inc_stats_counter(counters::num_running_disk_jobs, -1);
	});


	status_t const ret = translate_error(j, [&] {
		return std::visit([this, j](auto& a) { return this->do_job(a, j); }, j->action);
	});

	if (ret & disk_status::job_deferred) return;

	j->ret = ret;

	// note that -2 errors are OK
	TORRENT_ASSERT(j->ret != disk_status::fatal_disk_error
		|| (j->error.ec && j->error.operation != operation_t::unknown));

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

		m_stats_counters.inc_stats_counter(counters::num_blocks_read);
		m_stats_counters.inc_stats_counter(counters::num_read_ops);
		m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
		m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
	}

	TORRENT_ASSERT((j->flags & aux::disk_job::in_progress) || !j->storage);
	return status_t{};
}

status_t pread_disk_io::do_job(aux::job::read& a, aux::pread_disk_job* j)
{
	a.buf = disk_buffer_holder(m_buffer_pool, m_buffer_pool.allocate_buffer("send buffer"));
	if (!a.buf)
	{
		j->error.ec = error::no_memory;
		j->error.operation = operation_t::alloc_cache_piece;
		return disk_status::fatal_disk_error;
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

		m_stats_counters.inc_stats_counter(counters::num_blocks_read);
		m_stats_counters.inc_stats_counter(counters::num_read_ops);
		m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
		m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
	}
	TORRENT_ASSERT((j->flags & aux::disk_job::in_progress) || !j->storage);
	return status_t{};
}

status_t pread_disk_io::do_job(aux::job::write&, aux::pread_disk_job*)
{
	// write jobs never run through the generic job path: a write queued behind
	// a fence is inserted directly into the cache by the repost callback in
	// add_completed_jobs_impl() when the fence lowers, and an un-fenced write is
	// inserted inline in async_write(). So this is never reached.
	TORRENT_ASSERT_FAIL();
	return {};
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

	// async_read() does not consult the cache itself. It posts a read job that
	// carries the request verbatim -- including an unaligned start that spans two
	// blocks -- and lets add_job() funnel it. A job arriving while a fence is up
	// is parked, and the cache is consulted only once it is unblocked, so a read
	// can never bypass the fence and read stale data ahead of a queued write.
	// prepare_read() is the single point where the cache decides whether a read is
	// served from the cache, turned into a partial_read, or read from disk.
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

bool pread_disk_io::prepare_read(aux::pread_disk_job* j)
{
	auto& a = std::get<aux::job::read>(j->action);
	aux::piece_location const loc{j->storage->storage_index(), a.piece};
	// in case a.offset is not aligned to a block, calculate that offset, since
	// that's how the disk_cache is indexed. block_offset is the aligned offset to
	// the first block this read touches; read_offset is the offset into it.
	int const block_offset = a.offset - (a.offset % default_block_size);
	int const block_idx = a.offset / default_block_size;
	int const read_offset = a.offset - block_offset;

	disk_buffer_holder buffer;

	if (read_offset + int(a.buffer_size) > default_block_size)
	{
		// an unaligned request spanning two blocks. Either, both, or neither may
		// be in the cache.
		std::ptrdiff_t const len1 = default_block_size - read_offset;
		// the callback returns -1 if the buffer could not be allocated, else a
		// bitmask of which sides were found: 2 = first block, 1 = second block.
		int const ret = m_cache.get2(loc, block_idx, [&](char const* buf1, char const* buf2) {
			buffer =
				disk_buffer_holder(m_buffer_pool, m_buffer_pool.allocate_buffer("send buffer"));
			if (!buffer) return -1;
			if (buf1) std::memcpy(buffer.data(), buf1 + read_offset, std::size_t(len1));
			if (buf2) std::memcpy(buffer.data() + len1, buf2, std::size_t(a.buffer_size - len1));
			return (buf1 ? 2 : 0) | (buf2 ? 1 : 0);
		});

		// neither side was in the cache; read the whole span from disk.
		if (ret == 0) return false;

		if (ret == -1)
		{
			j->error.ec = error::no_memory;
			j->error.operation = operation_t::alloc_cache_piece;
			j->ret = disk_status::fatal_disk_error;
			return true;
		}

		if (ret == 3)
		{
			// both sides were in the cache; the request is served.
			a.buf = std::move(buffer);
			j->error = storage_error{};
			j->ret = status_t{};
			return true;
		}

		// only one side was in the cache (copied into buffer above). Transform j
		// into a partial_read that reads the missing side from disk. This is the
		// only place partial_read jobs are created, and it runs past the fence
		// gate against the current cache, so the cached side is never stale and
		// the partial_read never needs to re-consult the cache.
		TORRENT_ASSERT(ret == 1 || ret == 2);
		auto handler = std::move(a.handler);
		piece_index_t const piece = a.piece;
		// ret == 1: only the second block was cached, so the first (len1 bytes at
		// buffer offset 0, disk offset a.offset) is missing. ret == 2: only the
		// first block was cached, so the second (the remainder at buffer offset
		// len1, disk offset block_offset + default_block_size) is missing.
		auto const buffer_offset = std::uint16_t((ret == 1) ? 0 : len1);
		auto const missing_size = std::uint16_t((ret == 1) ? len1 : a.buffer_size - len1);
		std::int32_t const missing_offset =
			(ret == 1) ? a.offset : block_offset + default_block_size;
		j->action = aux::job::partial_read{std::move(handler),
			std::move(buffer),
			buffer_offset,
			missing_size,
			piece,
			missing_offset};
		return false;
	}

	// an aligned read request for one block.
	if (!m_cache.get(loc, block_idx, [&](span<char const> buf) {
			TORRENT_ASSERT_VAL(read_offset + int(a.buffer_size) <= buf.size(), read_offset);
			buffer =
				disk_buffer_holder(m_buffer_pool, m_buffer_pool.allocate_buffer("send buffer"));
			if (!buffer) return;
			std::memcpy(buffer.data(), buf.data() + read_offset, std::size_t(a.buffer_size));
		}))
		return false;

	if (!buffer)
	{
		j->error.ec = error::no_memory;
		j->error.operation = operation_t::alloc_cache_piece;
		j->ret = disk_status::fatal_disk_error;
		return true;
	}

	a.buf = std::move(buffer);
	j->error = storage_error{};
	j->ret = status_t{};
	return true;
}

bool pread_disk_io::async_write(storage_index_t const storage, peer_request const& r
	, char const* buf, std::shared_ptr<disk_observer> o
	, std::function<void(storage_error const&)> handler
	, disk_job_flags_t const flags)
{
	TORRENT_ASSERT(valid_flags(flags));

	auto storage_ptr = m_torrents[storage]->shared_from_this();

	disk_buffer_holder buffer(m_buffer_pool, m_buffer_pool.allocate_buffer("receive buffer"));
	if (!buffer) aux::throw_ex<std::bad_alloc>();
	std::memcpy(buffer.data(), buf, aux::numeric_cast<std::size_t>(r.length));

	TORRENT_ASSERT(r.start % default_block_size == 0);
	TORRENT_ASSERT(r.length <= default_block_size);

	aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::write>(flags,
		std::move(storage_ptr),
		std::move(handler),
		std::move(buffer),
		r.piece,
		r.start,
		std::uint16_t(r.length));

	// during shutdown a counted write would never be flushed and would leak the
	// storage's outstanding-job count. is_blocked() has not run, so j is not
	// in_progress and must not be job_complete()d -- abort it directly.
	if (m_abort)
	{
		m_completed_jobs.abort_job(m_ios, j);
		return false;
	}

	// a write counts as an outstanding job on the storage (like a read or hash),
	// so a fence waits for buffered writes and their in-flight flush before it
	// runs, rather than tearing the storage down under a flush. This does not
	// deadlock the fence: add_fence_job() hands a storage with outstanding writes
	// to a generic thread to flush, draining the writes the fence waits on.
	//
	// when a fence is up, is_blocked() parks the job in the fence's blocked queue
	// to be reposted into the cache once the fence lowers (add_completed_jobs_impl).
	// Inserting now would race teardown: for clear_piece the cpe is about to be
	// reset, for storage-wide fences the storage is about to go away. Parking
	// keeps the block "writing" in the picker's view, so the piece isn't reported
	// finished and its hash isn't requested before the block has been inserted.
	// TODO: back-pressure (the disk_observer o) is dropped on the blocked path.
	// async_write returns false (keep writing) but the cache's back-pressure
	// (driven off m_blocks) can't see the parked writes, so a long-lived fence
	// (e.g. move_storage) on a busy torrent can grow m_blocked_jobs unbounded.
	// Carry the observer through to throttle the peer while its writes are parked.
	if (j->storage->is_blocked(j, m_stats_counters))
	{
		return false;
	}

	// is_blocked() marked j in_progress and counted it as outstanding.
	bool const exceeded = add_write_to_cache(j, std::move(o));
	schedule_flush();
	return exceeded;
}

aux::insert_result_flags pread_disk_io::insert_write(
	aux::pread_disk_job* j, std::shared_ptr<disk_observer> o)
{
	auto const& a = std::get<aux::job::write>(j->action);
	piece_index_t const piece = a.piece;
	int const offset = a.offset;

	DLOG("async_write: piece: %d offset: %d flags: %x\n",
		int(piece),
		int(offset),
		static_cast<std::uint8_t>(j->flags));
	bool const force_flush = bool(j->flags & flush_piece);
	file_storage const& fs = j->storage->files();
	// in order to compute v1 hashes, we need the full piece, including pad
	// files. Even though v2 torrents guarantee that they are zero.
	int const piece_size = j->storage->v1() ? fs.piece_size(piece) : fs.piece_size2(piece);
	TORRENT_ASSERT(a.buffer_size == std::min(piece_size - offset, default_block_size));
	aux::disk_cache::piece_entry_params const piece_params{
		fs.piece_size2(piece), piece_size, j->storage->v1(), j->storage->v2(), j->storage};
	return m_cache.insert({j->storage->storage_index(), piece},
		offset / default_block_size,
		force_flush,
		std::move(o),
		j,
		piece_params);
}

void pread_disk_io::kick_write_hashers()
{
	m_hash_threads.interrupt();
	// no thread available to process the interrupt -- drain inline so the v2
	// hash queue can't grow without bound (and so destruction sees an empty
	// cache).
	if (m_hash_threads.max_threads() != 0) return;

	jobqueue_t completed;
	jobqueue_t retry;
	m_cache.kick_pending_hashers(completed, retry);
	m_cache.drain_v2_hash_queue(
		[](std::shared_ptr<aux::pread_storage> const& st,
			piece_index_t const p,
			int const block,
			sha256_hash const& h) {
			if (st) st->store_precomputed_v2(p, block, h);
		},
		retry,
		[&](jobqueue_t aborted, aux::disk_job* clear) {
			clear_piece_jobs(std::move(aborted), clear, completed);
		});
	if (!completed.empty()) add_completed_jobs(std::move(completed));
	while (!retry.empty())
	{
		auto* j = static_cast<aux::pread_disk_job*>(retry.pop_front());
		// a retry hash job that already went through is_blocked() (in_progress is
		// set, e.g. it arrived from the fence backlog) must not be re-registered
		// with the fence -- run it directly. With no hash threads everything runs
		// inline here anyway. Otherwise it's a fresh job; add_job() handles it.
		if (j->flags & aux::disk_job::in_progress)
			execute_job(j);
		else
			add_job(j);
	}
}

bool pread_disk_io::add_write_to_cache(aux::pread_disk_job* j, std::shared_ptr<disk_observer> o)
{
	auto const result = insert_write(j, std::move(o));

	// v1 wake-up signal comes from the cache; for v2 the insert may have
	// pushed a queue entry the cache has no way to flag.
	if ((result & aux::disk_cache::need_hasher_kick) || j->storage->v2()) kick_write_hashers();

	return bool(result & aux::disk_cache::exceeded_limit);
}

// schedule (or, with no generic threads, perform) a cache flush if the cache is
// over its watermark. Kept separate from add_write_to_cache() so do_job(write)
// can run it only after the write job's fence accounting is settled -- a
// synchronous flush here can complete (and free) the write job.
void pread_disk_io::schedule_flush()
{
	std::unique_lock<std::mutex> l(m_job_mutex);
	if (!m_flush_target)
	{
		// if the disk buffer wants to free up blocks, notify the thread
		// pool that we may need to flush blocks
		auto req = m_cache.flush_request();
		if (req)
		{
			m_flush_target = int(*req);
			DLOG("async_write: set flush_target: %d\n", *m_flush_target);
			// wake up a thread
			m_generic_threads.interrupt();
		}
	}

	// if no threads exist to process the interrupt, flush synchronously
	if (m_generic_threads.max_threads() == 0)
	{
		// also flush any completed (force-flush) pieces
		if (m_flush_target)
		{
			int const target = *std::exchange(m_flush_target, std::nullopt);
			DLOG("try_flush_cache(%d)\n", target);
			try_flush_cache(target, false, l);
		}
		else
		{
			try_flush_cache(0, true, l);
		}
	}
}

void pread_disk_io::async_hash(storage_index_t const storage
	, piece_index_t const piece, span<sha256_hash> const v2, disk_job_flags_t const flags
	, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler)
{
	TORRENT_ASSERT(valid_flags(flags));

	// the v2 span may arrive with stale bytes (e.g. callers that chain pieces
	// through the same buffer). do_job(hash) uses non-zero entries as a marker
	// that a block's hash is already known and skips re-hashing it, so wipe
	// the span here to keep that contract internal to the disk_io.
	for (auto& h : v2)
		h.clear();

	aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::hash>(
		flags,
		m_torrents[storage]->shared_from_this(),
		std::move(handler),
		piece,
		v2,
		sha1_hash{}
	);

	aux::disk_cache::hash_result const ret = m_cache.try_hash_piece({j->storage->storage_index(), piece}, j);

	// if we have already computed the piece hash, just post the completion
	// immediately
	if (ret == aux::disk_cache::job_completed)
	{
		// any v2 hash missing from storage means we have to fall back to
		// do_job(hash), which can read the block from disk.
		auto& a = std::get<aux::job::hash>(j->action);
		bool need_disk_fallback = false;
		if (!a.block_hashes.empty())
		{
			int const blocks_in_piece2 = j->storage->files().blocks_in_piece2(piece);
			fill_precomputed_v2(
				a.block_hashes, j->storage->take_precomputed_v2(piece), blocks_in_piece2);

			for (int i = 0; i < blocks_in_piece2; ++i)
			{
				if (i >= int(a.block_hashes.size()) || a.block_hashes[i].is_all_zeros())
				{
					need_disk_fallback = true;
					break;
				}
			}
		}

		if (need_disk_fallback)
		{
			DLOG("async_hash: v2 hashes incomplete, falling back to do_job(hash)\n");
			add_job(j);
			return;
		}

		// TODO: we may not need to do this, the cache could tell us
		// this piece should be flushed to disk now.
		m_generic_threads.interrupt();

		jobqueue_t jobs;
		jobs.push_back(j);
		add_completed_jobs(std::move(jobs));
		return;
	}

	// In this case the job has been queued on the piece, and will be posted
	// once the hashing completes
	if (ret == aux::disk_cache::job_queued)
		return;

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

	// In theory, we could check the cache for this block hash, but we
	// only retain cached_piece_entries until the main piece hash has been
	// returned, asking for individual blocks may not be available
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

// Called by the bittorrent layer in two distinct scenarios:
//
// 1. Hash check failed: peer_connection / torrent calls this after the piece
//    hash was returned and didn't match. By this point hashing has finished
//    on the piece (the hash was delivered to the caller before they could
//    react), so when do_job(clear_piece) runs the cpe has no hashing_flag.
//
// 2. Disk write failed: peer_connection::on_disk_write_complete calls this
//    when async_write reports a non-aborted error. The write failure is
//    unrelated to hashing -- earlier blocks of the same piece may still be
//    going through kick_hasher when the clear is dispatched, so try_clear_piece
//    sees hashing_flag set and parks the clear on the cpe (see disk_cache.cpp).
//
// Don't add an assertion in disk_cache::try_clear_piece that hashing_flag is
// clear; scenario 2 will fire it. The deferral branches are intentional.
void pread_disk_io::async_clear_piece(storage_index_t const storage
	, piece_index_t const index, std::function<void(piece_index_t)> handler)
{
	aux::pread_disk_job* j = m_job_pool.allocate_job<aux::job::clear_piece>(
		{},
		m_torrents[storage]->shared_from_this(),
		std::move(handler),
		index
	);

	DLOG("async_clear_piece: piece: %d\n", int(index));
	// clear_piece is a fence job: raise the per-storage fence so any
	// async_write issued from the network thread after this point is
	// aborted instead of racing with the cpe reset (see async_write).
	// The fence stays up until the clear job itself completes -- if
	// try_clear_piece defers (flushing/hashing/v2_pending), do_job
	// returns job_deferred and the fence is only lowered when the
	// deferred path eventually posts the completion.
	add_fence_job(j);
}

status_t pread_disk_io::do_job(aux::job::hash& a, aux::pread_disk_job* j)
{
	bool const v1 = bool(j->flags & disk_interface::v1_hash);
	bool const v2 = !a.block_hashes.empty();

	int const piece_size = v1 ? j->storage->files().piece_size(a.piece) : 0;
	int const piece_size2 = v2 ? j->storage->files().piece_size2(a.piece) : 0;
	int const blocks_in_piece = v1 ? (piece_size + default_block_size - 1) / default_block_size : 0;
	int const blocks_in_piece2 = v2 ? j->storage->files().blocks_in_piece2(a.piece) : 0;
	aux::open_mode_t const file_mode = file_mode_for_job(j);

	TORRENT_ASSERT(!v2 || int(a.block_hashes.size()) >= blocks_in_piece2);
	TORRENT_ASSERT(v1 || v2);

	int const blocks_to_read = std::max(blocks_in_piece, blocks_in_piece2);

	// async_hash's fast path may have partially populated a.block_hashes
	// already; preserve those entries (only fill all-zero slots).
	if (v2)
	{
		fill_precomputed_v2(
			a.block_hashes, j->storage->take_precomputed_v2(a.piece), blocks_in_piece2);

		// If any v2 block is still missing, rendezvous with the hasher
		// queue rather than reading the block back from disk. The block
		// is sitting in m_v2_hash_queue waiting to be hashed; doing it
		// here too would duplicate the hash work and leak the second copy
		// into pread_storage::m_precomputed_v2 (nobody else will consume
		// it). Only worth waiting when there's actually a hasher thread
		// that can drain the queue.
		if (m_hash_threads.max_threads() > 0)
		{
			bool any_missing = false;
			for (int i = 0; i < blocks_in_piece2; ++i)
			{
				if (a.block_hashes[i].is_all_zeros())
				{
					any_missing = true;
					break;
				}
			}
			if (any_missing && m_cache.wait_for_v2_queue({j->storage->storage_index(), a.piece}, j))
			{
				m_hash_threads.interrupt();
				return disk_status::job_deferred;
			}
		}
	}

	// hash_piece() callback, for a piece that's at least partially in the
	// cache. See the not_in_cache branch below for the not-in-cache fallback.
	auto hash_partial_piece = [&](lt::aux::piece_hasher* ph,
								  int const hasher_cursor,
								  span<char const*> const blocks) {
		time_point const start_time = clock_type::now();

		// v1 hashing is contiguous from hasher_cursor; v2 may need any
		// earlier block that's still missing its hash.
		int start = hasher_cursor;
		if (v2)
		{
			for (int i = 0; i < blocks_in_piece2; ++i)
			{
				if (a.block_hashes[i].is_all_zeros())
				{
					start = std::min(start, i);
					break;
				}
			}
		}

		int offset = start * default_block_size;
		int blocks_read_from_disk = 0;
		for (int i = start; i < blocks_to_read; ++i)
		{
			bool const v2_block = i < blocks_in_piece2;
			bool const v2_done = v2_block && !a.block_hashes[i].is_all_zeros();
			bool const v1_done = i < hasher_cursor;

			std::ptrdiff_t const len =
				(v1 && !v1_done) ? std::min(default_block_size, piece_size - offset) : 0;
			std::ptrdiff_t const len2 = (v2_block && !v2_done) ? std::min(default_block_size, piece_size2 - offset) : 0;

			if (len == 0 && len2 == 0)
			{
				offset += default_block_size;
				continue;
			}

			hasher256 ph2;
			char const* buf = (i < int(blocks.size())) ? blocks[i] : nullptr;
			if (buf == nullptr)
			{
				DLOG("do_hash: reading (piece: %d block: %d)\n", int(a.piece), i);

				j->error.ec.clear();

				if (len > 0)
				{
					TORRENT_ASSERT(ph);
					auto const flags =
						v2_block && !v2_done ? (j->flags & ~disk_interface::flush_piece) : j->flags;

					j->storage->hash(m_settings, ph->ctx(), len, a.piece
						, offset, file_mode, flags, j->error);
					++blocks_read_from_disk;
				}
				if (len2 > 0)
				{
					j->storage->hash2(m_settings, ph2, len2, a.piece, offset
						, file_mode, j->flags, j->error);
					++blocks_read_from_disk;
				}
				if (j->error) break;
			}
			else
			{
				if (len > 0)
				{
					TORRENT_ASSERT(ph);
					ph->update({ buf, len });
				}
				if (len2 > 0) ph2.update({buf, len2});
			}
			offset += default_block_size;

			if (len2 > 0) a.block_hashes[i] = ph2.final();
		}

		if (v1)
		{
			TORRENT_ASSERT(ph);
			a.piece_hash = ph->final_hash();
		}

		if (!j->error.ec)
		{
			std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

			m_stats_counters.inc_stats_counter(counters::num_read_back, blocks_read_from_disk);
			m_stats_counters.inc_stats_counter(counters::num_read_ops, blocks_read_from_disk);
			m_stats_counters.inc_stats_counter(counters::disk_hash_time, read_time);
			m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
		}
	};

	auto const hpr =
		m_cache.hash_piece({j->storage->storage_index(), a.piece}, j, hash_partial_piece);

	if (hpr == aux::disk_cache::hash_piece_result::deferred) return disk_status::job_deferred;

	// Fast path for a piece that isn't in the cache at all: read the whole
	// piece from disk in a single I/O operation, rather than one read per
	// 16 kiB block. v1's addressing spans whatever files/pad-files the piece
	// touches and zero-fills any pad-file range (see storage->read()); v2's
	// real data is always a prefix of that same span, since a pad file only
	// ever follows the real bytes of the file it aligns, never precedes or
	// overlaps them. So when v1 is also being hashed, one read of the
	// v1-sized span serves both: the v1 SHA-1 is hashed over the whole
	// buffer, and the v2 per-block SHA-256 hashes are computed from its
	// leading piece_size2 bytes. When only v2 is being hashed, the read is
	// exactly piece_size2 bytes. Either way, all hashing happens in memory,
	// with no further I/O.
	if (hpr == aux::disk_cache::hash_piece_result::not_in_cache)
	{
		time_point const start_time = clock_type::now();

		int const read_len = v1 ? piece_size : piece_size2;
		std::unique_ptr<char[]> buf(new char[std::size_t(read_len)]);
		span<char> const buf_span{buf.get(), read_len};

		j->error.ec.clear();
		j->storage->read(m_settings, buf_span, a.piece, 0, file_mode, j->flags, j->error);

		if (!j->error.ec)
		{
			if (v1)
			{
				hasher h;
				h.update(buf_span);
				a.piece_hash = h.final();
			}

			if (v2)
			{
				int offset = 0;
				for (int i = 0; i < blocks_in_piece2; ++i)
				{
					std::ptrdiff_t const len2 = std::min(default_block_size, piece_size2 - offset);
					if (a.block_hashes[i].is_all_zeros())
					{
						hasher256 h2;
						h2.update({buf.get() + offset, len2});
						a.block_hashes[i] = h2.final();
					}
					offset += default_block_size;
				}
			}

			std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

			m_stats_counters.inc_stats_counter(counters::num_read_back, blocks_to_read);
			m_stats_counters.inc_stats_counter(counters::num_read_ops, 1);
			m_stats_counters.inc_stats_counter(counters::disk_hash_time, read_time);
			m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
		}
	}

	return j->error ? disk_status::fatal_disk_error : status_t{};
}

status_t pread_disk_io::do_job(aux::job::hash2& a, aux::pread_disk_job* j)
{
	int const piece_size = j->storage->files().piece_size2(a.piece);
	aux::open_mode_t const file_mode = file_mode_for_job(j);

	DLOG("do_hash2: reading (piece: %d offset: %d)\n", int(a.piece), int(a.offset));

	time_point const start_time = clock_type::now();

	TORRENT_ASSERT(piece_size > a.offset);
	std::ptrdiff_t const len = std::min(default_block_size, piece_size - a.offset);

	// fast path: SHA-256 was already computed by drain_v2_hash_queue() while
	// the block was held in the v2 hash queue, and stashed on the storage.
	int const blk = a.offset / default_block_size;
	if (auto pre = j->storage->take_precomputed_v2_block(a.piece, blk))
	{
		a.piece_hash2 = *pre;
		std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);
		m_stats_counters.inc_stats_counter(counters::disk_hash_time, read_time);
		m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
		return {};
	}

	int ret = 0;
	a.piece_hash2 = m_cache.hash2({j->storage->storage_index(), a.piece}, blk, [&] {
		hasher256 h;
		ret = j->storage->hash2(m_settings, h, len, a.piece, a.offset
			, file_mode, j->flags, j->error);
		return h.final();
	});

	if (!j->error.ec)
	{
		std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

		m_stats_counters.inc_stats_counter(counters::num_blocks_read);
		m_stats_counters.inc_stats_counter(counters::num_read_ops);
		m_stats_counters.inc_stats_counter(counters::disk_hash_time, read_time);
		m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
	}

	return ret >= 0 ? status_t{} : disk_status::fatal_disk_error;
}

status_t pread_disk_io::do_job(aux::job::move_storage& a, aux::pread_disk_job* j)
{
	// if this assert fails, something's wrong with the fence logic
	TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);
	flush_storage(j->storage);

	// if files have to be closed, that's the storage's responsibility
	auto const [ret, p] = j->storage->move_storage(std::move(a.path), a.move_flags, j->error);

	a.path = std::move(p);
	return ret;
}

status_t pread_disk_io::do_job(aux::job::release_files&, aux::pread_disk_job* j)
{
	// if this assert fails, something's wrong with the fence logic
	TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);
	flush_storage(j->storage);
	j->storage->release_files(j->error);
	return j->error ? disk_status::fatal_disk_error : status_t{};
}

status_t pread_disk_io::do_job(aux::job::delete_files& a, aux::pread_disk_job* j)
{
	TORRENT_ASSERT(a.flags);

	// if this assert fails, something's wrong with the fence logic
	TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

	// TODO: maybe we don't need to write to files we're about to delete
	flush_storage(j->storage);

	j->storage->delete_files(a.flags, j->error);
	return j->error ? disk_status::fatal_disk_error : status_t{};
}

status_t pread_disk_io::do_job(aux::job::check_fastresume& a, aux::pread_disk_job* j)
{
	// if this assert fails, something's wrong with the fence logic
	TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);
	flush_storage(j->storage);
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
	if (j->error) return disk_status::fatal_disk_error | ret_flag;

	// we must call verify_resume_data() unconditionally of the setting below, in
	// order to set up the links (if present)
	bool const verify_success = j->storage->verify_resume_data(*rd
		, links ? *links : aux::vector<std::string, file_index_t>(), j->error);

	// j->error may have been set at this point, by verify_resume_data()
	// it's important to not have it cleared out subsequent calls, as long
	// as they succeed.

	if (m_settings.get_bool(settings_pack::no_recheck_incomplete_resume))
		return status_t{} | ret_flag;

	if (!aux::contains_resume_data(*rd))
	{
		// if we don't have any resume data, we still may need to trigger a
		// full re-check, if there are *any* files.
		storage_error ignore;
		return ((j->storage->has_any_file(ignore))
			? disk_status::need_full_check
			: status_t{})
			| ret_flag;
	}

	return (verify_success
		? status_t{}
		: disk_status::need_full_check)
		| ret_flag;
}

status_t pread_disk_io::do_job(aux::job::rename_file& a, aux::pread_disk_job* j)
{
	// if this assert fails, something's wrong with the fence logic
	TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);

	// if files need to be closed, that's the storage's responsibility
	j->storage->rename_file(a.file_index, a.name, j->error);
	return j->error ? disk_status::fatal_disk_error : status_t{};
}

status_t pread_disk_io::do_job(aux::job::stop_torrent&, aux::pread_disk_job* j)
{
	// if this assert fails, something's wrong with the fence logic
	TORRENT_ASSERT(j->storage->num_outstanding_jobs() == 1);
	flush_storage(j->storage);
	j->storage->release_files(j->error);
	return j->error ? disk_status::fatal_disk_error : status_t{};
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
	auto const [cache_size, num_unhashed] = m_cache.stats();
	c.set_value(counters::cached_blocks, cache_size);
	c.set_value(counters::num_unhashed, num_unhashed);
	auto const [hits, misses, stalls, races, num_files] = m_file_pool.stats_counters();
	c.set_value(counters::file_pool_hits, hits);
	c.set_value(counters::file_pool_misses, misses);
	c.set_value(counters::file_pool_thread_stall, stalls);
	c.set_value(counters::file_pool_race, races);
	c.set_value(counters::file_pool_size, num_files);
}

status_t pread_disk_io::do_job(aux::job::file_priority& a, aux::pread_disk_job* j)
{
	j->storage->set_file_priority(m_settings
		, a.prio
		, j->error);
	return status_t{};
}

status_t pread_disk_io::do_job(aux::job::clear_piece& a, aux::pread_disk_job* j)
{
	// raise_fence ensured the previous outstanding jobs for this storage all
	// completed before we got here, and the fence keeps new ones blocked.
	// async_write checks has_fence() and aborts itself, so no fresh writes
	// can race with the cpe reset either.
	jobqueue_t aborted;
	bool const immediate =
		m_cache.try_clear_piece({j->storage->storage_index(), a.piece}, j, aborted);

	if (!aborted.empty()) m_completed_jobs.abort_jobs(m_ios, std::move(aborted));

	if (!immediate)
	{
		// try_clear_piece parked j on the cpe waiting for flushing_flag or
		// v2_pending to clear. The deferred path will dispatch the
		// completion via clear_piece_jobs -> add_completed_jobs, which runs
		// job_complete and lowers the fence.
		DLOG("do_job(clear_piece): piece: %d deferred\n", int(a.piece));
		return disk_status::job_deferred;
	}

	DLOG("do_job(clear_piece): piece: %d immediate\n", int(a.piece));
	// In the hash-failure caller take_precomputed_v2() in async_hash's path
	// has already drained the map and this is a no-op; in the write-failure
	// caller the v2 drain typically completed before the write error
	// surfaced and the entries it stored are removed here.
	j->storage->drop_precomputed_v2(a.piece);
	return {};
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

	int const ret = j->storage->raise_fence(j, m_stats_counters);

	{
		std::unique_lock<std::mutex> l(m_job_mutex);
		if (ret == aux::disk_job_fence::fence_post_fence)
		{
			TORRENT_ASSERT((j->flags & aux::disk_job::in_progress) || !j->storage);
			m_generic_threads.push_back(j);
		}

		// the fence was queued behind outstanding jobs -- typically cache writes that
		// haven't flushed yet. Nothing else will flush them while the fence blocks new
		// jobs (a partial piece's completing hash is itself blocked here), so the
		// fence would wait forever. Hand the storage to a generic thread to flush.
		// (fence_post_fence has nothing outstanding to flush; a stacked fence's writes
		// land later, when the fence ahead lowers -- add_completed_jobs_impl() re-arms.)
		if (ret != aux::disk_job_fence::fence_post_fence && !j->storage->in_fence_flush())
		{
			j->storage->set_in_fence_flush(true);
			m_fence_flush.push_back(j->storage);
		}
		m_generic_threads.interrupt();
	}

	// m_fence_flush is only serviced by the generic pool (thread_fun, or
	// immediate_execute() below). Gate on the generic pool being empty, not on
	// num_threads(): with hash threads but no generic threads, interrupt() above
	// is a no-op and nothing would ever flush the storage, deadlocking the fence.
	if (m_generic_threads.max_threads() == 0 && user_add) immediate_execute();
}

void pread_disk_io::add_job(aux::pread_disk_job* j, bool const user_add)
{
	TORRENT_ASSERT(!j->storage || j->storage->files().is_valid());
	TORRENT_ASSERT(j->next == nullptr);

#if TORRENT_DISK_LATENCY_STATS
	// stamp the job on the network thread, where add_job runs. The matching
	// measurement happens when the completion handler runs (also on the
	// network thread), so the latency includes both disk queues.
	j->start_time = clock_type::now();
#endif
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
	if (j->storage && j->storage->is_blocked(j, m_stats_counters))
	{
		DLOG("blocked job: %s (torrent: %d total: %d)\n"
			, print_job(*j).c_str(), j->storage ? j->storage->num_blocked() : 0
			, int(m_stats_counters[counters::blocked_disk_jobs]));
		return;
	}

	// is_blocked() returned false, so the fence (if there was one) is down. The
	// fence is only lowered after job_complete() has reposted every blocked job
	// -- inserting all blocked writes into the cache -- and it does that holding
	// the same mutex is_blocked() takes, so a fence observed down here guarantees
	// those writes are already in the cache. This is where a read consults the
	// cache (async_read() does not), so we don't post a disk read for a block whose
	// write is sitting un-flushed in the cache; do_job(read) reads disk
	// unconditionally, so this must happen here. prepare_read() may instead turn j
	// into a partial_read, which falls through to the pool.
	if (j->storage && std::holds_alternative<aux::job::read>(j->action) && prepare_read(j))
	{
		jobqueue_t completed;
		completed.push_back(j);
		add_completed_jobs(std::move(completed));
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
	if (q.max_threads() == 0 && user_add)
		immediate_execute();
}

void pread_disk_io::immediate_execute()
{
	// Hash threads can lower fences and touch the generic queue while
	// there are no generic disk threads.
	for (;;)
	{
		std::unique_lock<std::mutex> l(m_job_mutex);
		if (m_generic_threads.empty()) break;
		auto* j = static_cast<aux::pread_disk_job*>(m_generic_threads.pop_front());
		l.unlock();
		execute_job(j);
	}
	// mirror what thread_fun does: flush force-flush pieces, flush storages with
	// a waiting fence, and handle any pending cache flush target
	std::unique_lock<std::mutex> l(m_job_mutex);
	flush_fenced_storages(l);
	if (m_flush_target)
	{
		int const target = *std::exchange(m_flush_target, std::nullopt);
		DLOG("try_flush_cache(%d)\n", target);
		try_flush_cache(target, false, l);
	}
	else
	{
		try_flush_cache(0, true, l);
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
		j->ret = disk_status::fatal_disk_error;
		j->error = storage_error(boost::asio::error::operation_aborted);
		completed_jobs.push_back(j);
		add_completed_jobs(std::move(completed_jobs));
		return;
	}

	perform_job(j, completed_jobs);
	if (!completed_jobs.empty())
		add_completed_jobs(std::move(completed_jobs));
}

int pread_disk_io::flush_cache_blocks(
	bitfield& flushed, span<aux::disk_job* const> blocks, jobqueue_t& completed_jobs)
{
	if (blocks.empty()) return 0;

#if DEBUG_DISK_THREAD
	{
		auto piece = piece_index_t(-1);
		std::string blocks_str;
		blocks_str.reserve(blocks.size());
		for (auto* wj : blocks)
		{
			blocks_str += wj ? '*' : ' ';
			if (wj) piece = std::get<aux::job::write>(wj->action).piece;
		}
		// If this assert fires, it means we were asked to flush a piece
		// that doesn't have any jobs to flush
		TORRENT_ASSERT(piece != piece_index_t(-1));
		DLOG("flush_cache_blocks: piece: %d blocks: [%s]\n", int(piece), blocks_str.c_str());
	}
#endif

	// blocks may be sparse. We need to skip any entry where the job is null
	m_stats_counters.inc_stats_counter(counters::num_running_disk_jobs, 1);
	m_stats_counters.inc_stats_counter(counters::num_writing_threads, 1);

	auto se = aux::scope_end([&] {
		m_stats_counters.inc_stats_counter(counters::num_writing_threads, -1);
		m_stats_counters.inc_stats_counter(counters::num_running_disk_jobs, -1);
	});

	time_point const start_time = clock_type::now();

	bool failed = false;

	// the total number of blocks we ended up flushing to disk
	int ret = 0;

	visit_block_iovecs(blocks, [&](span<span<char const>> iovec, int const start_idx) {
		auto* j = blocks[start_idx];
		TORRENT_ASSERT(j->get_type() == aux::job_action_t::write);
		auto& a = std::get<aux::job::write>(j->action);
		auto* pj = static_cast<aux::pread_disk_job*>(j);
		aux::open_mode_t const file_mode = file_mode_for_job(pj);
		aux::pread_storage* storage = pj->storage.get();

		TORRENT_ASSERT(a.piece != piece_index_t(-1));
		int const count = static_cast<int>(iovec.size());
		DLOG("write: blocks: %d (piece: %d)\n", count, int(a.piece));

		storage_error error;
		storage->write(m_settings, iovec
			, a.piece, a.offset, file_mode, j->flags, error);

		int i = start_idx;
		for (auto* j2 : blocks.subspan(start_idx, count))
		{
			TORRENT_ASSERT(j2);
			TORRENT_ASSERT(j2->get_type() == aux::job_action_t::write);
			j2->error = error;
			flushed.set_bit(i);
			completed_jobs.push_back(j2);
			++i;
		}

		ret += count;

		if (error)
		{
			TORRENT_ASSERT(i == start_idx + count);
			// if there was a failure, fail the remaining jobs as well
			for (auto* j2 : blocks.subspan(start_idx + count))
			{
				if (j2)
				{
					j2->error = error;
					flushed.set_bit(i);
					completed_jobs.push_back(j2);
					++ret;
				}
				++i;
			}
			failed = true;
		}
		return failed;
	});

	if (!failed)
	{
		std::int64_t const write_time = total_microseconds(clock_type::now() - start_time);

		m_stats_counters.inc_stats_counter(counters::num_blocks_written, blocks.size());
		m_stats_counters.inc_stats_counter(counters::num_write_ops);
		m_stats_counters.inc_stats_counter(counters::disk_write_time, write_time);
		m_stats_counters.inc_stats_counter(counters::disk_job_time, write_time);
	}

	return ret;
}

void pread_disk_io::clear_piece_jobs(
	jobqueue_t aborted, aux::disk_job* clear, jobqueue_t& completed)
{
	m_completed_jobs.abort_jobs(m_ios, std::move(aborted));
	// drop any precomputed v2 hashes for the cleared piece. We hold off on
	// this drop until m_cache has confirmed v2_pending == 0 for the piece;
	// otherwise an in-flight drain batch could call store_precomputed_v2()
	// after we drop, leaving a stale hash to be consumed by a later
	// hash/hash2 on the re-downloaded piece.
	if (clear)
	{
		auto* j = static_cast<aux::pread_disk_job*>(clear);
		auto const& a = std::get<aux::job::clear_piece>(j->action);
		j->storage->drop_precomputed_v2(a.piece);
		// don't complete the clear here: this runs under the cache mutex (the
		// callback fires from inside flush_to_disk / drain_v2_hash_queue) and
		// completing it re-enters the cache (add_completed_jobs ->
		// schedule_flush -> flush_request), self-deadlocking. The caller drains
		// `completed` once the cache mutex is released.
		completed.push_back(clear);
	}
}

void pread_disk_io::try_flush_cache(int const target_cache_size
	, bool const optimistic
	, std::unique_lock<std::mutex>& l)
{
	DLOG("flushing, cache target: %d (current size: %d)\n", target_cache_size, m_cache.size());
	l.unlock();
	jobqueue_t completed_jobs;
	m_cache.flush_to_disk(
		[&](bitfield& flushed, span<aux::disk_job* const> blocks) {
			return flush_cache_blocks(flushed, blocks, completed_jobs);
		},
		target_cache_size,
		[&](jobqueue_t aborted, aux::disk_job* clear) {
			clear_piece_jobs(std::move(aborted), clear, completed_jobs);
		},
		optimistic);
	// complete the flushed write jobs with m_job_mutex unlocked: a completed
	// write can lower a fence and unblock its backlog, and that path takes
	// m_job_mutex (add_completed_jobs_impl). Re-acquire before returning to keep
	// the caller's "lock held on return" contract.
	DLOG("flushed blocks (%d blocks left), return to disk loop\n", m_cache.size());
	if (!completed_jobs.empty())
		add_completed_jobs(std::move(completed_jobs));
	l.lock();
}

void pread_disk_io::flush_storage(std::shared_ptr<aux::pread_storage> const& storage)
{
	storage_index_t const torrent = storage->storage_index();
	DLOG("flush_storage (%d)\n", torrent);
	jobqueue_t completed_jobs;
	m_cache.flush_storage(
		[&](bitfield& flushed, span<aux::disk_job* const> blocks) {
			return flush_cache_blocks(flushed, blocks, completed_jobs);
		},
		torrent,
		[&](jobqueue_t aborted, aux::disk_job* clear) {
			clear_piece_jobs(std::move(aborted), clear, completed_jobs);
		});
	DLOG("flush_storage - done (%d left)\n", m_cache.size());
	if (!completed_jobs.empty())
		add_completed_jobs(std::move(completed_jobs));
}

void pread_disk_io::flush_fenced_storages(std::unique_lock<std::mutex>& l)
{
	TORRENT_ASSERT(l.owns_lock());
	// drain in place: pop_back keeps the reserved capacity (a swap would hand it
	// to a fresh local and force the next push to reallocate), so this allocates
	// nothing -- it is reachable from the no-throw job-completion path. flushing
	// drains the storage's outstanding writes; once they complete the queued
	// fence's wait for m_outstanding_jobs == 0 can finish and it runs.
	while (!m_fence_flush.empty())
	{
		auto const st = std::move(m_fence_flush.back());
		m_fence_flush.pop_back();
		st->set_in_fence_flush(false);
		l.unlock();
		flush_storage(st);
		l.lock();
	}
}

void pread_disk_io::thread_fun(aux::disk_io_thread_pool& pool
	, executor_work_guard<io_context::executor_type> work)
{
	// work is used to keep the io_context alive
	TORRENT_UNUSED(work);

	ADD_OUTSTANDING_ASYNC("pread_disk_io::work");
	std::thread::id const thread_id = std::this_thread::get_id();

	aux::set_thread_name("libtorrent-disk-thread");

	DLOG("started disk thread\n");

	std::unique_lock<std::mutex> l(m_job_mutex);

	++m_num_running_threads;
	m_stats_counters.inc_stats_counter(counters::num_running_threads, 1);

	// we call close_oldest_file on the file_pool regularly. This is the next
	// time we should call it
	time_point next_close_oldest_file = min_time();

	for (;;)
	{
		// before going to sleep, always flush blocks from the cache that need flushing
		if (&pool == &m_generic_threads)
		{
			// flush storages whose fence is waiting on their outstanding writes
			flush_fenced_storages(l);

			// if we need to flush the cache, let one of the generic threads do
			// that
			if (m_flush_target)
			{
				int const target_cache_size = *std::exchange(m_flush_target, std::nullopt);
				DLOG("try_flush_cache(%d)\n", target_cache_size);
				try_flush_cache(target_cache_size, false, l);
			}
			else
			{
				try_flush_cache(0, true, l);
			}
		}

		auto const res = pool.wait_for_job(l);

		// if there are pieces waiting for a hasher kick, process them now
		if (&pool == &m_hash_threads)
		{
			l.unlock();
			jobqueue_t completed;
			jobqueue_t retry;
			bool const needs_flush = m_cache.kick_pending_hashers(completed, retry);

			m_cache.drain_v2_hash_queue(
				[](std::shared_ptr<aux::pread_storage> const& st,
					piece_index_t const piece,
					int const block,
					sha256_hash const& h) {
					if (st) st->store_precomputed_v2(piece, block, h);
				},
				retry,
				[&](jobqueue_t aborted, aux::disk_job* clear) {
					clear_piece_jobs(std::move(aborted), clear, completed);
				});
			add_completed_jobs(std::move(completed));

			l.lock();
			bool const submit = !retry.empty();
			while (!retry.empty())
			{
				auto j = static_cast<aux::pread_disk_job*>(retry.pop_front());
				TORRENT_ASSERT(&pool_for_job(j) == &m_hash_threads);

				DLOG("retry_job: %s (outstanding: %d)\n"
					, print_job(*j).c_str()
					, j->storage ? j->storage->num_outstanding_jobs() : 0);

				// Only register with the fence if this job hasn't already gone
				// through is_blocked() (i.e. it arrived via try_hash_piece() ->
				// job_queued, not via hash_piece() -> deferred where in_progress
				// is already set). Calling is_blocked() on a job that already has
				// in_progress set would double-increment m_outstanding_jobs and
				// would deadlock any pending fence.
				if (j->storage && !(j->flags & aux::disk_job::in_progress)
					&& j->storage->is_blocked(j, m_stats_counters))
				{
					DLOG("blocked job: %s (torrent: %d total: %d)\n"
						, print_job(*j).c_str(), j->storage ? j->storage->num_blocked() : 0
						, int(m_stats_counters[counters::blocked_disk_jobs]));
					continue;
				}
				m_hash_threads.push_back(j);
			}

			if (submit) m_hash_threads.submit_jobs();
			// In case there are no other disk jobs triggering a flush, we need
			// to wake up a generic thread.
			if (needs_flush)
				m_generic_threads.interrupt();
			// for interrupt, there's no job to pop; for new_job, the queue may
			// have raced to empty. In both cases loop back. For exit_thread,
			// fall through so the thread actually exits.
			if (res == aux::wait_result::interrupt
				|| (res == aux::wait_result::new_job && pool.empty()))
				continue;
		}

		if (res == aux::wait_result::exit_thread)
		{
			DLOG("exit disk loop\n");
			break;
		}

		if (res != aux::wait_result::new_job)
		{
			DLOG("continue disk loop\n");
			continue;
		}

		auto* j = static_cast<aux::pread_disk_job*>(pool.pop_front());

		bool const is_flush_piece = (&pool == &m_hash_threads)
			&& bool(j->flags & disk_interface::flush_piece);

		if (is_flush_piece)
		{
			// This hash job will (or already did) set force_flush_flag.
			// Interrupt a generic thread to do the actual flush.
			m_generic_threads.interrupt();
		}

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

		// If a hash job ran on the hash thread, hash_piece() may have set
		// force_flush_flag on the piece. Wake a generic thread to flush those blocks.
		if (is_flush_piece)
			m_generic_threads.interrupt();

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

	// flush everything before exiting this thread
	try_flush_cache(0, false, l);

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

	// flush any storages still queued behind a fence's outstanding writes.
	// Otherwise they would sit in m_fence_flush, keeping their pread_storage
	// alive until m_fence_flush is destructed, which happens after
	// m_file_pool -- and pread_storage::~pread_storage() calls back into
	// m_file_pool.
	{
		std::unique_lock<std::mutex> l(m_job_mutex);
		flush_fenced_storages(l);
	}

	// close all files. This may take a long
	// time on certain OSes (i.e. Mac OS)
	// that's why it's important to do this in
	// the disk thread in parallel with stopping
	// trackers.
	m_file_pool.release();
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
	// When completing a job lowers a fence, job_complete() calls this repost
	// callback -- still holding the fence's mutex -- once for each job that was
	// blocked behind it, in order. So a job's resume happens under the fence and
	// a job arriving on the network thread concurrently is ordered behind the
	// backlog. The callback only does the part that can't re-enter the fence:
	//  - a write is inserted into the cache, like async_write (the write completes
	//    later, when flushed -- never here). The hasher kick is deferred.
	//  - a read consults the cache, like async_read. The block was written while
	//    the fence was up and is in the cache above, so it must be served from
	//    there; do_job(read) would read it (stale) from disk. The completion is
	//    deferred (it re-enters the fence via job_complete()).
	//  - everything else (reads that missed, hashes, a stacked fence) is queued
	//    for dispatch to its thread pool, deferred too.
	// passing repost to job_complete must not allocate (the job-completion path must
	// not throw), so it has to fit std::function's small-object buffer -- hence a
	// single captured pointer.
	struct repost_state
	{
		pread_disk_io* self;
		jobqueue_t to_pool;
		jobqueue_t cache_hits;
		bool need_kick;
	} ctx{this, {}, {}, false};

	// set when a completed fence re-raised a stacked fence and its storage was
	// queued in m_fence_flush in the loop below; after the loop we drain it (or
	// interrupt a generic thread to). See the push site for the rationale.
	bool need_fence_flush = false;

	auto const repost = [&ctx](aux::disk_job* job) {
		auto* j = static_cast<aux::pread_disk_job*>(job);
		if (std::holds_alternative<aux::job::write>(j->action))
		{
			auto const result = ctx.self->insert_write(j, {});
			if ((result & aux::disk_cache::need_hasher_kick) || j->storage->v2())
				ctx.need_kick = true;
		}
		else if (std::holds_alternative<aux::job::read>(j->action) && ctx.self->prepare_read(j))
			ctx.cache_hits.push_back(j);
		else
			ctx.to_pool.push_back(j);
	};

	int ret = 0;
	for (auto i = jobs.iterate(); i.get(); i.next())
	{
		auto* j = static_cast<aux::pread_disk_job*>(i.get());

		if (j->flags & aux::disk_job::fence)
		{
			m_stats_counters.inc_stats_counter(
				counters::num_fenced_read + static_cast<int>(j->get_type()), -1);
		}

		if (j->flags & aux::disk_job::in_progress)
		{
			TORRENT_ASSERT(j->storage);
			if (j->storage) ret += j->storage->job_complete(j, repost);
		}

		// a fence completed but the storage still has a fence up: a stacked fence
		// was re-raised over the writes the repost callback just inserted. (the fence
		// flag survives job_complete; only in_progress is cleared.) The repost
		// inserted those writes into the cache; nothing else will flush them while
		// the re-raised fence blocks new jobs, so the fence (now waiting on those
		// very writes) would wait forever. Queue the storage in m_fence_flush so a
		// generic thread drains them, the same way add_fence_job() does when a fence
		// is first raised over outstanding writes.
		if ((j->flags & aux::disk_job::fence) && j->storage && j->storage->has_fence())
		{
			std::lock_guard<std::mutex> l(m_job_mutex);
			if (!j->storage->in_fence_flush())
			{
				j->storage->set_in_fence_flush(true);
				m_fence_flush.push_back(j->storage);
			}
			need_fence_flush = true;
		}

		TORRENT_ASSERT(!(j->flags & aux::disk_job::in_progress));
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(j->job_posted == false);
		j->job_posted = true;
#endif
	}

	m_stats_counters.inc_stats_counter(counters::blocked_disk_jobs, -ret);
	TORRENT_ASSERT(int(m_stats_counters[counters::blocked_disk_jobs]) >= 0);

	m_completed_jobs.append(m_ios, std::move(jobs));

	// --- deferred work, now that no fence mutex is held ---

	// wake the hasher for the blocks the repost callback inserted (drains the v2
	// queue inline when there are no hash threads).
	if (ctx.need_kick) kick_write_hashers();

	// every reposted write is in the cache now; flush if over the watermark.
	schedule_flush();

	// complete the reads we served from the cache, the same way the disk threads
	// would have. This can lower further fences (re-entering via job_complete).
	if (!ctx.cache_hits.empty()) add_completed_jobs(std::move(ctx.cache_hits));

	// drain the storages queued in the loop above. With no generic threads the
	// interrupt is a no-op and nothing else would drain m_fence_flush before the
	// next user job, so flush inline here.
	if (need_fence_flush)
	{
		std::unique_lock<std::mutex> l(m_job_mutex);
		if (m_generic_threads.max_threads() == 0)
			flush_fenced_storages(l);
		else
			m_generic_threads.interrupt();
	}

	if (ctx.to_pool.empty()) return;

	// dispatch the remaining unblocked jobs to their pools. The m_abort check is
	// atomic with abort() under m_job_mutex, so a stop_torrent fence's backlog
	// can't be queued to a pool that's being torn down (its handler would never
	// run); fail those jobs instead.
	jobqueue_t run_now;
	{
		std::lock_guard<std::mutex> l(m_job_mutex);
		if (m_abort)
		{
			while (!ctx.to_pool.empty())
			{
				auto* j = static_cast<aux::pread_disk_job*>(ctx.to_pool.pop_front());
				TORRENT_ASSERT((j->flags & aux::disk_job::in_progress) || !j->storage);
				j->ret = disk_status::fatal_disk_error;
				j->error = storage_error(boost::asio::error::operation_aborted);
				completed.push_back(j);
			}
			return;
		}

		bool queue_generic = false;
		bool queue_hash = false;
		while (!ctx.to_pool.empty())
		{
			auto* j = static_cast<aux::pread_disk_job*>(ctx.to_pool.pop_front());
			aux::disk_io_thread_pool& pool = pool_for_job(j);
			if (pool.max_threads() == 0)
			{
				run_now.push_back(j);
				continue;
			}
			pool.push_back(j);
			if (&pool == &m_hash_threads)
				queue_hash = true;
			else
				queue_generic = true;
		}
		if (queue_generic) m_generic_threads.submit_jobs();
		if (queue_hash) m_hash_threads.submit_jobs();
	}

	// a pool with no threads has to run inline, like add_job()->immediate_execute()
	while (!run_now.empty())
		execute_job(static_cast<aux::pread_disk_job*>(run_now.pop_front()));
}

}
