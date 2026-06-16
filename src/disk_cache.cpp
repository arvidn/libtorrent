/*

Copyright (c) 2023, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/disk_cache.hpp"
#include "libtorrent/disk_observer.hpp"
#include "libtorrent/aux_/debug_disk_thread.hpp"
#include "libtorrent/bitfield.hpp"

#if TORRENT_USE_INVARIANT_CHECKS
#include <map>
#endif

namespace libtorrent::aux {

namespace mi = boost::multi_index;

namespace {
struct compare_storage
{
	bool operator()(piece_location const& lhs, storage_index_t const rhs) const
	{
		return lhs.torrent < rhs;
	}

	bool operator()(storage_index_t const lhs, piece_location const& rhs) const
	{
		return lhs < rhs.torrent;
	}
};

bool compute_force_flush(cached_piece_entry const& piece)
{
	// piece that are partial on startup won't have the flushed_cursor
	// updated to indicate what's on disk and what's in the cache. Once
	// the bittorrent engine asks for the piece hash, we know the piece is
	// supposed to be complete. After hashing, we should flush any remaining
	// blocks to disk
	return (piece.hasher_cursor == piece.blocks_in_piece()
		|| bool(piece.flags & cached_piece_entry::piece_hash_returned_flag));
}

std::uint16_t compute_flushed_cursor(span<const cached_block_entry> blocks)
{
	std::uint16_t ret = 0;
	for (auto const& b : blocks)
	{
		if (!b.is_flushed()) return ret;
		++ret;
	}
	return ret;
}

std::uint16_t count_jobs(span<const cached_block_entry> blocks)
{
	return static_cast<std::uint16_t>(std::count_if(blocks.begin(), blocks.end()
		, [](cached_block_entry const& b) { return b.get_write_job(); }));
}

}

char const* cached_block_entry::data() const noexcept
{
	if (auto const* ref = std::get_if<disk_buffer_ref>(&write_state))
		return ref->data();
	if (auto const* j = std::get_if<disk_job*>(&write_state))
	{
		TORRENT_ASSERT((*j)->get_type() == aux::job_action_t::write);
		return std::get<job::write>((*j)->action).borrowed_buf;
	}
	return nullptr;
}

span<char const> cached_block_entry::buf(int const block_size) const
{
	if (auto const* ref = std::get_if<disk_buffer_ref>(&write_state))
	{
		TORRENT_ASSERT(block_size > 0);
		return {ref->data(), ref->data() ? block_size : 0};
	}
	if (auto const* j = std::get_if<disk_job*>(&write_state))
	{
		TORRENT_ASSERT((*j)->get_type() == aux::job_action_t::write);
		auto const& job = std::get<job::write>((*j)->action);
		TORRENT_ASSERT(block_size == job.buffer_size);
		return {job.borrowed_buf, job.buffer_size};
	}
	return {nullptr, 0};
}

span<char const> cached_block_entry::write_buf() const
{
	if (auto const* j = std::get_if<disk_job*>(&write_state))
	{
		TORRENT_ASSERT((*j)->get_type() == aux::job_action_t::write);
		auto const& job = std::get<job::write>((*j)->action);
		return {job.borrowed_buf, job.borrowed_buf ? job.buffer_size : std::uint16_t{0}};
	}
	return {nullptr, 0};
}

bool cached_block_entry::has_buf() const noexcept
{
	auto const* ref = std::get_if<disk_buffer_ref>(&write_state);
	return ref != nullptr && ref->data() != nullptr;
}

bool cached_block_entry::is_flushed() const noexcept
{
	return std::holds_alternative<disk_buffer_ref>(write_state);
}

disk_job* cached_block_entry::get_write_job() const noexcept
{
	auto const* p = std::get_if<disk_job*>(&write_state);
	return p ? *p : nullptr;
}

disk_buffer_ref cached_block_entry::take_buf() noexcept
{
	TORRENT_ASSERT(has_buf());
	disk_buffer_ref r = std::move(std::get<disk_buffer_ref>(write_state));
	// leave as disk_buffer_ref{null} to preserve "was flushed" information
	write_state = disk_buffer_ref{};
	return r;
}

disk_job* cached_block_entry::take_write_job() noexcept
{
	TORRENT_ASSERT(std::holds_alternative<disk_job*>(write_state));
	disk_job* j = std::get<disk_job*>(write_state);
	write_state = std::monostate{};
	return j;
}

template <typename... Type>
struct overload : Type... {
	using Type::operator()...;
};
template<class... Type> overload(Type...) -> overload<Type...>;

sha1_hash piece_hasher::final_hash()
{
	sha1_hash ret;
	std::visit(overload{
		[&] (hasher& h) { ret = h.final(); ph = ret; },
		[&] (sha1_hash const& h) { ret = h; },
	}, ph);
	TORRENT_ASSERT(!ret.is_all_zeros());
	return ret;
}

void piece_hasher::update(span<char const> const buf)
{
	auto* ctx = std::get_if<hasher>(&ph);
	TORRENT_ASSERT(ctx != nullptr);
	ctx->update(buf);
}

lt::hasher& piece_hasher::ctx()
{
	auto* ctx = std::get_if<hasher>(&ph);
	TORRENT_ASSERT(ctx != nullptr);
	return *ctx;
}

cached_piece_entry::cached_piece_entry(piece_location const& loc,
	int const piece_size_v2,
	int const piece_size_arg,
	int const num_blocks,
	bool const v1)
	: piece(loc)
	, blocks(aux::make_unique<cached_block_entry[]>(num_blocks))
	, ph(v1 ? std::make_unique<piece_hasher>() : nullptr)
	, piece_size2(piece_size_v2)
	, piece_size(piece_size_arg)
	, flags(v1 ? v1_hashes_flag : cached_piece_flags{})
{
	TORRENT_ASSERT(piece_size_arg > 0);
	TORRENT_ASSERT(num_blocks == blocks_in_piece());
}

span<cached_block_entry> cached_piece_entry::get_blocks() const
{
	return {blocks.get(), blocks_in_piece()};
}

disk_cache::disk_cache(io_context& ios)
	: m_back_pressure(ios)
{}

// If the specified piece exists in the cache and is not in use by another
// thread, clear it: abort all write jobs (returned in `aborted`), drop any
// v2 hash queue entries, and reset cursors/flags. Returns true when the
// caller can post the clear_piece job as complete now. Returns false if the
// clear must be deferred -- the clear_piece job is parked on the cpe and
// will be dispatched by whichever path eventually releases the gate that
// blocked us (flush_piece_impl's scope_end for flushing_flag, or
// drain_v2_hash_queue for v2_pending).
bool disk_cache::try_clear_piece(piece_location const loc, disk_job* j, jobqueue_t& aborted)
{
	std::unique_lock<std::mutex> l(m_mutex);

	INVARIANT_CHECK;

	auto& view = m_pieces.template get<0>();
	auto i = view.find(loc);
	if (i == view.end()) return true;

	if (i->flags & cached_piece_entry::flushing_flag)
	{
		// postpone the clearing until we're done flushing
		view.modify(i, [&](cached_piece_entry& e) { e.clear_piece = j; });
		return false;
	}

	// clear_piece is also issued on a disk write failure (peer_connection.cpp's
	// on_disk_write_complete error path), and that can race with a hasher that
	// was already kicked for earlier blocks of the same piece. Park the clear;
	// flush_piece_impl's scope_end will dispatch it the next time a flush
	// touches this piece (the hasher's Path 2/3 exit sets force_flush_flag so
	// the next optimistic flush picks it up).
	if (i->flags & cached_piece_entry::hashing_flag)
	{
		view.modify(i, [&](cached_piece_entry& e) { e.clear_piece = j; });
		return false;
	}

	view.modify(i, [&](cached_piece_entry& e) {
		clear_piece_impl(e, aborted);
	});

	// In-flight drain batches still own entries for this piece -- their
	// store_precomputed_v2() calls land on pread_storage *after* we return.
	// Defer the completion (and the drop_precomputed_v2 the caller will
	// issue) until the drain decrements v2_pending to zero, so the late
	// store doesn't race the drop. The caller's per-storage fence keeps new
	// async_writes blocked across this wait; v2 hashing 16 KiB is much
	// faster than the disk I/O that drove the request, so this branch is
	// rarely taken.
	if (i->v2_pending > 0)
	{
		view.modify(i, [&](cached_piece_entry& e) { e.clear_piece = j; });
		return false;
	}
	return true;
}

// we allow allocating more blocks even after we exceed the max size,
// but communicate back to the allocator (typically the peer_connection)
// that we have exceeded the limit via the out-parameter "exceeded". The
// caller is expected to honor this by not allocating any more buffers
// until the disk_observer object (passed in as "o") is invoked, indicating
// that there's more room in the pool now. This caps the amount of over-
// allocation to one block per peer connection.
// returns true if this piece needs to have its hasher kicked
insert_result_flags disk_cache::insert(piece_location const loc
	, int const block_idx
	, bool const force_flush
	, std::shared_ptr<disk_observer> o
	, disk_job* write_job
	, piece_entry_params const& params)
{
	TORRENT_ASSERT(write_job != nullptr);
	std::unique_lock<std::mutex> l(m_mutex);

	INVARIANT_CHECK;

	auto& view = m_pieces.template get<0>();
	auto i = view.find(loc);
	if (i == view.end())
	{
		int const num_blocks = (params.piece_size + default_block_size - 1) / default_block_size;
		i = m_pieces.emplace(loc, params.piece_size2, params.piece_size, num_blocks, params.v1)
				.first;
	}

	TORRENT_ASSERT(!(i->flags & cached_piece_entry::piece_hash_returned_flag));

	cached_block_entry& blk = i->blocks[block_idx];
	DLOG("disk_cache.insert: piece: %d blk: %d flushed: %d write_job: %p flushed_cursor: %d hashed_cursor: %d\n"
		, static_cast<int>(i->piece.piece)
		, block_idx
		, blk.is_flushed()
		, blk.get_write_job()
		, i->flushed_cursor
		, i->hasher_cursor);
	TORRENT_ASSERT(!blk.has_buf());
	TORRENT_ASSERT(blk.get_write_job() == nullptr);
	TORRENT_ASSERT(block_idx >= i->flushed_cursor);
	TORRENT_ASSERT(block_idx >= i->hasher_cursor);

	TORRENT_ASSERT(write_job->get_type() == aux::job_action_t::write);
	auto& wjob = std::get<aux::job::write>(write_job->action);
	TORRENT_ASSERT(wjob.buffer_size
		== std::min(default_block_size, params.piece_size - block_idx * default_block_size));
	// All write jobs must use the same disk buffer allocator
	if (!m_allocator)
		m_allocator = wjob.buf.m_allocator;
	else
		TORRENT_ASSERT(m_allocator == wjob.buf.m_allocator);

	// captured while wjob.buf is in scope, before any move that may empty
	// it. flush reads through this without the cache mutex (see write_buf()).
	TORRENT_ASSERT(wjob.borrowed_buf == nullptr);
	wjob.borrowed_buf = wjob.buf.data();

	// move v2 blocks whose data falls inside piece_size2 onto the hash
	// queue. The queue owns the buffer; the cache reaches the bytes through
	// wjob.borrowed_buf.
	if (params.v2)
	{
		int const block_offset = block_idx * default_block_size;
		int const piece_size2 = params.piece_size2;
		int const v2_len = std::min(int(wjob.buffer_size), piece_size2 - block_offset);
		if (v2_len > 0)
		{
			m_v2_hash_queue.emplace_back(loc,
				params.storage,
				static_cast<std::uint16_t>(block_idx),
				static_cast<std::uint16_t>(v2_len),
				std::move(wjob.buf));
			view.modify(i, [](cached_piece_entry& e) { ++e.v2_pending; });
		}
	}

	blk.write_state = write_job;
	// queue-owned buffers contribute to the level via m_v2_hash_queue.size().
	if (wjob.buf) ++m_blocks;
	if (params.v1) ++m_num_unhashed;

	bool const effective_force_flush = force_flush || compute_force_flush(*i);
	view.modify(i, [effective_force_flush](cached_piece_entry& e) {
		if (effective_force_flush) e.flags |= cached_piece_entry::force_flush_flag;
		++e.num_jobs;
	});

	insert_result_flags ret{};

	if (m_back_pressure.has_back_pressure(m_blocks + int(m_v2_hash_queue.size()), std::move(o)))
		ret |= exceeded_limit;

	// need_hasher_kick covers v1 hasher progress only; the caller wakes the
	// hasher for v2 queue work itself (it knows storage->v2()).
	if (params.v1 && i->hasher_cursor == block_idx
		&& !(i->flags & cached_piece_entry::piece_hash_returned_flag)
		&& !(i->flags & cached_piece_entry::needs_hasher_kick_flag))
	{
		ret |= need_hasher_kick;
		view.modify(i, [](cached_piece_entry& e) { e.flags |= cached_piece_entry::needs_hasher_kick_flag; });
	}

	return ret;
}

void disk_cache::set_max_size(int const max_size)
{
	std::unique_lock<std::mutex> l(m_mutex);
	m_back_pressure.set_max_size(max_size);
}

std::optional<int> disk_cache::flush_request() const
{
	std::unique_lock<std::mutex> l(m_mutex);
	return m_back_pressure.should_flush(m_blocks + int(m_v2_hash_queue.size()));
}

// this call can have 3 outcomes:
// 1. the job is immediately satisfied and should be posted to the
//    completion queue
// 2. The piece is in the cache and currently hashing, but it's not done
//    yet. We hang the hash job on the piece itself so the hashing thread
//    can complete it when hashing finishes
// 3. The piece is not in the cache and should be posted to the disk thread
//    to read back the bytes.
disk_cache::hash_result disk_cache::try_hash_piece(piece_location const loc, disk_job* hash_job)
{
	std::unique_lock<std::mutex> l(m_mutex);

	INVARIANT_CHECK;

	auto& view = m_pieces.template get<0>();
	auto i = view.find(loc);
	if (i == view.end()) return hash_result::post_job;

	if (!(i->flags & cached_piece_entry::hashing_flag) && i->hasher_cursor == i->blocks_in_piece())
	{
		// v1 hash is computed. If the v2 drain still has entries for
		// this piece, async_hash's contract to deliver v1 + v2 hashes
		// together means we can't post yet -- hang and let
		// drain_v2_hash_queue post the job through the retry queue when
		// v2_pending hits zero. do_job(hash) -> hash_piece scope_end
		// will then set piece_hash_returned_flag and force_flush_flag.
		if (i->v2_pending > 0)
		{
			TORRENT_ASSERT(i->hash_job == nullptr);
			view.modify(i, [&](cached_piece_entry& e) { e.hash_job = hash_job; });
			return hash_result::job_queued;
		}

		view.modify(i, [&](cached_piece_entry& e) {
			// mark for flush so a generic thread will write any pending
			// write_jobs that are still in the cache for this piece
			e.flags |=
				cached_piece_entry::piece_hash_returned_flag | cached_piece_entry::force_flush_flag;

			auto& job = std::get<aux::job::hash>(hash_job->action);
			TORRENT_ASSERT(bool(e.ph) == bool(e.flags & cached_piece_entry::v1_hashes_flag));
			job.piece_hash = e.ph ? e.ph->final_hash() : sha1_hash{};
		});
		return hash_result::job_completed;
	}

	// v2-only with v2 still draining: same defer, just reached through a
	// different gate -- there's no v1 hasher cursor to wait on.
	if (!(i->flags & cached_piece_entry::v1_hashes_flag) && i->v2_pending > 0)
	{
		TORRENT_ASSERT(i->hash_job == nullptr);
		view.modify(i, [&](cached_piece_entry& e) { e.hash_job = hash_job; });
		return hash_result::job_queued;
	}

	if ((i->flags & cached_piece_entry::hashing_flag) && i->hasher_cursor < i->blocks_in_piece())
	{
		// We're not done hashing yet, let the hashing thread post the
		// completion once it's done

		// We don't expect to ever have simultaneous async_hash() requests
		// for the same piece
		TORRENT_ASSERT(i->hash_job == nullptr);
		view.modify(i, [&](cached_piece_entry& e) { e.hash_job = hash_job; });
		return hash_result::job_queued;
	}

	return hash_result::post_job;
}

bool disk_cache::wait_for_v2_queue(piece_location const loc, disk_job* hash_job)
{
	std::unique_lock<std::mutex> l(m_mutex);
	INVARIANT_CHECK;

	auto& view = m_pieces.template get<0>();
	auto pi = view.find(loc);
	if (pi == view.end()) return false;
	// hash_piece's deferred path will own hash_job in this case
	if (pi->flags & cached_piece_entry::hashing_flag) return false;
	if (pi->hash_job != nullptr) return false;
	if (pi->v2_pending == 0) return false;

	view.modify(pi, [hash_job](cached_piece_entry& e) { e.hash_job = hash_job; });
	return true;
}

// this should be called from a hasher thread, with m_mutex held.
// hashing_flag must already be set on the piece by the caller.
// Returns true if force_flush_flag was set on the piece.
bool disk_cache::kick_hasher(piece_container::nth_index<4>::type::iterator piece_iter
	, std::unique_lock<std::mutex>& l, jobqueue_t& completed_jobs
	, jobqueue_t& retry_jobs)
{
	INVARIANT_CHECK;
	TORRENT_ASSERT(l.owns_lock());

	std::uint16_t cursor = piece_iter->hasher_cursor;
	TORRENT_ASSERT(cursor <= piece_iter->blocks_in_piece());

	// NOTE: block_storage is indexed starting at cursor. We don't waste space
	// allocating a slot for every block, just the ones in front of the cursor
	TORRENT_ALLOCA(blocks_storage, span<char const>, piece_iter->blocks_in_piece() - int(cursor));

	// hashing_flag is already set by caller. We need to clear it before returning.
	TORRENT_ASSERT(piece_iter->flags & cached_piece_entry::hashing_flag);

#if TORRENT_USE_ASSERTS
	piece_location const original_piece = piece_iter->piece;
#endif

keep_going:

	// Snapshot all block buffer pointers from cursor onwards while holding the
	// mutex. New blocks may be added asynchronously after we release the lock,
	// so we must not read their buf() pointers then.
	int const n = piece_iter->blocks_in_piece() - int(cursor);
	TORRENT_ASSERT(n >= 0);

	// we may loop here, so this may not be the first time around the hashing loop.
	// Make sure blocks_storage only covers the remaining blocks in the piece
	blocks_storage = blocks_storage.subspan(0, n);
	for (int i = 0; i < n; ++i)
	{
		int const blk_idx = int(cursor) + i;
		int const blk_size = std::min(default_block_size
			, piece_iter->piece_size - blk_idx * default_block_size);
		blocks_storage[i] = piece_iter->blocks[blk_idx].buf(blk_size);
	}

	// end = first gap in the contiguous run, needed for flushed_cursor trimming
	std::uint16_t end = cursor;
	while (end < piece_iter->blocks_in_piece() && blocks_storage[end - cursor].data())
		++end;

	// insert() only sets needs_hasher_kick_flag for v1/hybrid pieces.
	TORRENT_ASSERT(piece_iter->flags & cached_piece_entry::v1_hashes_flag);
	TORRENT_ASSERT(piece_iter->ph);

	DLOG("kick_hasher: piece: %d hashed_cursor: [%d, %d] ctx: %p\n",
		static_cast<int>(piece_iter->piece.piece),
		cursor,
		end,
		piece_iter->ph.get());

	l.unlock();

	for (span<char const> const buf : blocks_storage)
	{
		if (buf.data() == nullptr) break;

		auto& ctx = const_cast<aux::piece_hasher&>(*piece_iter->ph);
		ctx.update(buf);
		++cursor;
		TORRENT_ASSERT(cursor <= piece_iter->blocks_in_piece());
	}

	l.lock();

	// sanity check to ensure the piece entry wasn't removed under our feet
	TORRENT_ASSERT(piece_iter->piece == original_piece);
	TORRENT_ASSERT(piece_iter->flags & cached_piece_entry::hashing_flag);

	// if some other thread added the next block, keep going
	if (cursor != piece_iter->blocks_in_piece()
		&& piece_iter->blocks[cursor].data())
	{
		goto keep_going;
	}

	// recount rather than relying on (cursor - old_cursor): blocks flushed
	// during the unlocked hashing window have already had their
	// m_num_unhashed contribution removed by flush_piece_impl.
	int const count = cursor - piece_iter->hasher_cursor;
	{
		int actual_hashed = 0;
		for (auto const& cbe : piece_iter->get_blocks().subspan(piece_iter->hasher_cursor, count))
			if (cbe.has_buf() || cbe.get_write_job()) ++actual_hashed;
		TORRENT_ASSERT(m_num_unhashed >= actual_hashed);
		m_num_unhashed -= actual_hashed;
	}

	// blocks that have been flushed and hashed can be removed from the cache immediately
	TORRENT_ASSERT(m_allocator);
	bulk_free_buffer to_free(*m_allocator);
	for (auto& cbe : piece_iter->get_blocks().subspan(piece_iter->hasher_cursor, count))
	{
		if (!cbe.has_buf()) continue;
		to_free.add(cbe.take_buf());
		TORRENT_ASSERT(m_blocks > 0);
		--m_blocks;
	}

	TORRENT_ASSERT(l.owns_lock());
	m_back_pressure.check_buffer_level(m_blocks + int(m_v2_hash_queue.size()));

	auto& view = m_pieces.template get<4>();

	if (cursor != piece_iter->blocks_in_piece())
	{
		// if some other thread added the next block, keep going
		if (piece_iter->blocks[cursor].data())
			goto keep_going;

		disk_job* rj = nullptr;
		view.modify(piece_iter, [&rj, cursor](cached_piece_entry& e) {
			rj = std::exchange(e.hash_job, nullptr);
			e.hasher_cursor = cursor;
			e.flags &= ~cached_piece_entry::hashing_flag;
		});
		if (rj) retry_jobs.push_back(rj);

		DLOG("kick_hasher: no attached hash job\n");
		return false;
	}

	if (!piece_iter->hash_job)
	{
		// All blocks have been hashed, but no async_hash() job is pending.
		// Mark the piece for flushing so the next optimistic flush picks it up.
		view.modify(piece_iter, [cursor](cached_piece_entry& e) {
			e.hasher_cursor = cursor;
			e.flags &= ~cached_piece_entry::hashing_flag;
			e.flags |= cached_piece_entry::force_flush_flag;
		});

		return true;
	}

	// there's a hash job hung on this piece, post it now
	disk_job* const j = piece_iter->hash_job;
	TORRENT_ASSERT(j != nullptr);
	TORRENT_ASSERT(!(piece_iter->flags & cached_piece_entry::piece_hash_returned_flag));

	// peek at the hung job to decide between inline completion and a retry
	// through the disk thread pool. A non-empty block_hashes array means
	// the caller wants v2 block hashes too -- do_job(hash) will pull those
	// from pread_storage::precomputed_block_hashes (populated by the v2
	// drain) with a disk fallback for any still missing.
	auto& job = std::get<job::hash>(j->action);
	bool const will_retry = !job.block_hashes.empty();

	sha1_hash piece_hash;
	bool const force_flush = cursor == piece_iter->blocks_in_piece()
		|| bool(piece_iter->flags & cached_piece_entry::piece_hash_returned_flag);
	view.modify(piece_iter, [&piece_hash, force_flush, cursor, will_retry](cached_piece_entry& e) {
		e.hash_job = nullptr;
		if (force_flush) e.flags |= cached_piece_entry::force_flush_flag;
		e.hasher_cursor = cursor;
		e.flags &= ~cached_piece_entry::hashing_flag;
		// only mark the hash as returned when we're delivering it inline.
		// In the retry case do_job(hash) -> hash_piece's scope_end sets
		// the flag once the v2 block hashes are actually in the job. If we
		// set it eagerly, flush_to_disk could evict the cpe between here
		// and the retry, forcing do_job(hash) to read everything back
		// from disk instead of reusing the cached state.
		if (!will_retry) e.flags |= cached_piece_entry::piece_hash_returned_flag;
		TORRENT_ASSERT(bool(e.ph) == bool(e.flags & cached_piece_entry::v1_hashes_flag));
		piece_hash = e.ph ? e.ph->final_hash() : sha1_hash{};
	});

	job.piece_hash = piece_hash;
	if (will_retry)
	{
		DLOG("kick_hasher: retrying attached v2 job piece: %d\n",
			static_cast<int>(piece_iter->piece.piece));
		retry_jobs.push_back(j);
	}
	else
	{
		DLOG("kick_hasher: posting attached job piece: %d\n",
			static_cast<int>(piece_iter->piece.piece));
		completed_jobs.push_back(j);
	}

	return force_flush;
}

// returns true if any piece finished hashing, and is ready to be flushed to
// disk. Any such piece will also have had the force_flush_flag set.
bool disk_cache::kick_pending_hashers(jobqueue_t& completed_jobs, jobqueue_t& retry_jobs)
{
	bool needs_flush = false;
	std::unique_lock<std::mutex> l(m_mutex);
	auto& view = m_pieces.template get<4>();
	for (;;)
	{
		auto const it = view.begin();
		if (it == view.end() || !it->needs_hasher_kick())
			break;

		// Skip pieces already being hashed or fully hashed; just clear the flag.
		if ((it->flags & cached_piece_entry::hashing_flag)
			|| (it->flags & cached_piece_entry::piece_hash_returned_flag))
		{
			view.modify(it, [](cached_piece_entry& e)
				{ e.flags &= ~cached_piece_entry::needs_hasher_kick_flag; });
			continue;
		}

		// Clear needs_hasher_kick_flag and set hashing_flag in a single modify(),
		// so the piece moves out of the front of index 4 before we release the lock.
		view.modify(it, [](cached_piece_entry& e) {
			e.flags = (e.flags & ~cached_piece_entry::needs_hasher_kick_flag)
				| cached_piece_entry::hashing_flag;
		});

		needs_flush |= kick_hasher(it, l, completed_jobs, retry_jobs);
	}
	return needs_flush;
}

template <typename Iter, typename View>
Iter disk_cache::flush_piece_impl(View& view,
	Iter piece_iter,
	std::function<int(bitfield&, span<disk_job* const>)> const& f,
	std::unique_lock<std::mutex>& l,
	span<cached_block_entry> const blocks,
	std::function<void(jobqueue_t, disk_job*)> clear_piece_fun)
{
	TORRENT_ASSERT(l.owns_lock());
	// when "blocks" covers the whole piece we can use the maintained per-piece
	// counter instead of scanning. A subspan (the cheap-flush pass) still needs
	// to count the write jobs within that range.
	int const num_blocks = (blocks.size() == piece_iter->blocks_in_piece())
		? int(piece_iter->num_jobs)
		: count_jobs(blocks);
	TORRENT_ASSERT(num_blocks == count_jobs(blocks));
	if (num_blocks <= 0)
		return std::next(piece_iter);

	// blocks may be a subspan of all the blocks in the piece, so when comparing flushed_cursor and hasher_cursor, we need to add the offset.
	// TODO: pass the block offset as a parameter instead of computing it like this
	int const block_offset = static_cast<int>(blocks.data() - piece_iter->get_blocks().data());

	view.modify(piece_iter, [](cached_piece_entry& e) {
		TORRENT_ASSERT(!(e.flags & cached_piece_entry::flushing_flag));
		e.flags |= cached_piece_entry::flushing_flag;
	});

	// Snapshot the pending write_job pointer for each block while we still
	// hold the mutex. flushing_flag prevents other threads from flushing
	// this piece, but disk_cache::insert() may still populate previously
	// empty trailing slots (insert only requires block_idx >= hasher_cursor).
	// Reading cached_block_entry::write_state from outside the lock would
	// race with that. Once a slot holds a disk_job the network thread won't
	// touch it (nor the job's contents) until the disk thread takes it
	// back, so the snapshotted pointers and the buffers they reference stay
	// valid for the entire unlocked region below.
	TORRENT_ALLOCA(snapshot, disk_job*, blocks.size());
	for (int i = 0; i < blocks.size(); ++i)
		snapshot[i] = blocks[i].get_write_job();

	// we have to release the lock while flushing, but since we set the
	// "flushing" member to true, this piece is pinned to the cache
	l.unlock();

	int count = 0;
	bitfield flushed_blocks;
	{
		auto se = scope_end([&] {
			l.lock();
			bool notify = false;
			view.modify(piece_iter, [&notify](cached_piece_entry& e) {
				TORRENT_ASSERT(bool(e.flags & cached_piece_entry::flushing_flag));
				notify = bool(e.flags & cached_piece_entry::notify_flushed_flag);
				e.flags &= ~cached_piece_entry::flushing_flag;
			});
			if (notify) m_flushing_cv.notify_all();
		});
		flushed_blocks.resize(int(blocks.size()));
		flushed_blocks.clear_all();
		count = f(flushed_blocks, snapshot);
	}
	TORRENT_UNUSED(count);
	TORRENT_ASSERT(l.owns_lock());

	int const hasher_cursor = piece_iter->hasher_cursor;

	// now that we hold the mutex again, we can update the entries for
	// all the blocks that were flushed
	std::uint16_t jobs = 0;

	// note that i is not the block index in the piece. the "blocks" span may
	// be a subspan of the whole piece. When comparing against the
	// hasher_cursor or flushed_cursor, we need to use the block index.
	TORRENT_ASSERT(m_allocator);
	bulk_free_buffer to_free(*m_allocator);
	for (int i = 0; i < blocks.size(); ++i)
	{
		if (!flushed_blocks.get_bit(i)) continue;
		cached_block_entry& blk = blocks[i];
		int const block_index = block_offset + i;

		auto* j = blk.take_write_job();
		TORRENT_ASSERT(j);
		TORRENT_ASSERT(j->get_type() == aux::job_action_t::write);
		auto& job = std::get<aux::job::write>(j->action);
		// borrowed_buf is write-once and stays valid for the buffer's
		// lifetime; leave it alone for the unlocked completed-job path.
		disk_buffer_ref ref(std::move(job.buf));

		// ref empty means the v2 hash queue owns the buffer; the cache
		// didn't count it in m_blocks and can't hold-alive for the hasher.
		bool const needed_by_hasher = bool(ref)
			&& (piece_iter->flags & cached_piece_entry::hashing_flag)
			&& block_index >= hasher_cursor;
		if (needed_by_hasher)
		{
			blk.write_state = std::move(ref);
		}
		else
		{
			bool const cache_owned = bool(ref);
			to_free.add(std::move(ref));
			// mark as flushed (with null buffer) so compute_flushed_cursor can
			// advance past this block even though the buffer has been freed
			blk.write_state = disk_buffer_ref{};
			if ((piece_iter->flags & cached_piece_entry::v1_hashes_flag)
				&& block_index >= hasher_cursor)
			{
				TORRENT_ASSERT(m_num_unhashed > 0);
				--m_num_unhashed;
			}
			if (cache_owned)
			{
				TORRENT_ASSERT(m_blocks > 0);
				--m_blocks;
			}
		}

		++jobs;
	}

	// Compute next_iter before the view.modify below as the element may be
	// moved when modified.
	auto next_iter = std::next(piece_iter);
	bool const force_flush = compute_force_flush(*piece_iter);
	view.modify(piece_iter, [jobs, force_flush](cached_piece_entry& e) {
		span<cached_block_entry const> const all_blocks = e.get_blocks();
		e.flushed_cursor = compute_flushed_cursor(all_blocks);
		if (force_flush) e.flags |= cached_piece_entry::force_flush_flag;
		TORRENT_ASSERT(e.num_jobs >= jobs);
		e.num_jobs -= jobs;
	});
	DLOG("flush_piece_impl: piece: %d flushed_cursor: %d force_flush: %d\n"
		, static_cast<int>(piece_iter->piece.piece), piece_iter->flushed_cursor, bool(piece_iter->flags & cached_piece_entry::force_flush_flag));
	TORRENT_ASSERT(count <= blocks.size());
	if (piece_iter->clear_piece)
	{
		jobqueue_t aborted;
		disk_job* clear_piece = nullptr;
		view.modify(piece_iter, [&](cached_piece_entry& e) {
			clear_piece_impl(e, aborted);
			// if a v2 drain still owes us a callback, leave e.clear_piece
			// armed so drain_v2_hash_queue finishes the clear once
			// v2_pending hits zero (the late store_precomputed_v2() must
			// land before drop_precomputed_v2()).
			if (e.v2_pending == 0) clear_piece = std::exchange(e.clear_piece, nullptr);
		});
		if (clear_piece != nullptr || !aborted.empty())
			clear_piece_fun(std::move(aborted), clear_piece);
	}

	return next_iter;
}

int disk_cache::drop_v2_queue_entries(piece_location const loc)
{
	int dropped = 0;
	auto new_end = std::remove_if(
		m_v2_hash_queue.begin(), m_v2_hash_queue.end(), [&](v2_hash_queue_entry const& e) {
			if (!(e.piece == loc)) return false;
			++dropped;
			return true;
		});
	m_v2_hash_queue.erase(new_end, m_v2_hash_queue.end());
	return dropped;
}

void disk_cache::free_piece(cached_piece_entry const& cpe)
{
#if TORRENT_USE_ASSERTS
	// piece_hash_returned_flag implies hasher_cursor == blocks_in_piece():
	// try_hash_piece() requires the cursor to already be at the end before
	// it sets the flag, and kick_hasher() and hash_piece() both move the
	// cursor to the end in the same modify() that sets the flag.
	// flushed_cursor does *not* have a similar invariant: hash_piece() can
	// complete a hash by reading blocks from disk for slots that were never
	// written via async_write this session. Those slots stay in monostate,
	// so the contiguous-from-zero count in compute_flushed_cursor() stops at
	// the first such slot even though every block needing to be written
	// has in fact been written.
	if (cpe.flags & cached_piece_entry::piece_hash_returned_flag)
		TORRENT_ASSERT(cpe.hasher_cursor == cpe.blocks_in_piece());
	TORRENT_ASSERT(cpe.hash_job == nullptr);
#endif
	TORRENT_ASSERT(m_allocator);

	// drop any queued v2 hash entries for this piece. The queue owns those
	// buffers and they're released when the entries go out of scope. Any
	// drain batch already in flight for this piece keeps its own buffer; on
	// dispose the drain will look up the piece, find it gone, and drop the
	// buffer naturally.
	drop_v2_queue_entries(cpe.piece);

	bulk_free_buffer to_free(*m_allocator);
	bool const is_v1 = bool(cpe.flags & cached_piece_entry::v1_hashes_flag);
	int idx = 0;
	for (auto& blk : cpe.get_blocks())
	{
		if (blk.has_buf())
		{
			if (is_v1 && idx >= cpe.hasher_cursor)
			{
				TORRENT_ASSERT(m_num_unhashed > 0);
				--m_num_unhashed;
			}
			--m_blocks;
			to_free.add(blk.take_buf());
		}
		TORRENT_ASSERT(!blk.has_buf() || idx >= cpe.hasher_cursor);
		TORRENT_ASSERT(blk.get_write_job() == nullptr);
		++idx;
	}
}

// this should be called by a disk thread
// the callback should return the number of blocks it successfully flushed
// to disk. Optimistic flush means we'll only flush pieces that are ready to
// be flushed, and already hashed. We don't gain anything from keeping those in
// the cache.
void disk_cache::flush_to_disk(std::function<int(bitfield&, span<disk_job* const>)> f,
	int const target_blocks,
	std::function<void(jobqueue_t, disk_job*)> clear_piece_fun,
	bool const optimistic)
{
	std::unique_lock<std::mutex> l(m_mutex);

	INVARIANT_CHECK;

	auto se = aux::scope_end([&] {
		TORRENT_ASSERT(l.owns_lock());
		// checks to see if we're no longer exceeding the high watermark,
		// and if we're in fact below the low watermark. If so, we need to
		// post the notification messages to the peers that are waiting for
		// more buffers to received data into
		m_back_pressure.check_buffer_level(m_blocks + int(m_v2_hash_queue.size()));
	});

	// first we look for pieces that are ready to be flushed and should be
	// updating
	auto& view = m_pieces.template get<2>();
	for (auto piece_iter = view.begin(); piece_iter != view.end();)
	{
		// We want to flush all pieces that are ready to flush regardless of
		// the flush target. There's not much value in keeping them in RAM
		// when we've completely downloaded the piece and hashed it
		// so, we don't check flush target in this loop

		if (piece_iter->flags & cached_piece_entry::flushing_flag)
		{
			++piece_iter;
			continue;
		}

		if (!(piece_iter->flags & cached_piece_entry::force_flush_flag))
			break;

		TORRENT_ASSERT(piece_iter->blocks_in_piece() >= 0);
		if (piece_iter->blocks_in_piece() == 0)
		{
			++piece_iter;
			continue;
		}
		span<cached_block_entry> blocks = piece_iter->get_blocks();

		auto const next_iter = flush_piece_impl(view, piece_iter, f, l
			, blocks, clear_piece_fun);

		if (piece_iter->flushed_cursor == piece_iter->blocks_in_piece()
			&& bool(piece_iter->flags & cached_piece_entry::piece_hash_returned_flag)
			&& !(piece_iter->flags & cached_piece_entry::flushing_flag)
			&& !(piece_iter->flags & cached_piece_entry::hashing_flag))
		{
			// piece_hash_returned_flag is set at the same modify() that
			// extracts hash_job (try_hash_piece's job_completed path, or
			// kick_hasher / hash_piece scope_end), so flag-set implies
			// hash_job == nullptr -- the cpe is safe to evict.
			free_piece(*piece_iter);
			view.erase(piece_iter);
		}
		piece_iter = next_iter;
	}

	if (optimistic) return;

	// if we get here, we have to flush some blocks even though we
	// don't have all the blocks in the piece yet. Start by flushing pieces that have the
	// most contiguous blocks to flush.
	auto& view2 = m_pieces.template get<1>();
	for (auto piece_iter = view2.begin(); piece_iter != view2.end();)
	{
		// exit only on the actual level. Predicting the level after in-flight
		// flushes finish (subtracting concurrent flushing count) creates a
		// race where each thread can exit assuming the others will finish
		// their work, but if the last thread also takes that shortcut, no
		// one drives the level down to target and back-pressure stays on.
		// Cheap flushing is the preferred path (no read-back later), so we
		// want to exhaust it here rather than fall through to the expensive
		// pass.
		if (m_blocks + int(m_v2_hash_queue.size()) <= target_blocks) return;

		int const num_eligible_blocks = piece_iter->hasher_cursor - piece_iter->flushed_cursor;

		// the pieces are ordered by the number of blocks that are cheap to
		// flush (i.e. won't require read-back later)
		// if we encounter a 0, all the remaining ones will also be zero
		if (num_eligible_blocks <= 0) break;

		if (piece_iter->flags & cached_piece_entry::flushing_flag)
		{
			++piece_iter;
			continue;
		}

		span<cached_block_entry> const blocks = piece_iter->get_blocks().subspan(
			piece_iter->flushed_cursor
			, num_eligible_blocks);

		piece_iter = flush_piece_impl(view2, piece_iter, f, l
			, blocks, clear_piece_fun);
	}

	// before doing any more disk I/O, drop buffers that were kept alive
	// for a hasher snapshot that has since released (the needed_by_hasher
	// branch in flush_piece_impl). The data is already on disk, so freeing
	// these buffers requires no I/O. Without this pass, when kick_hasher
	// returns without advancing the cursor (e.g. v1 SHA-1 is blocked on a
	// missing earlier block) the held-alive buffers stay in
	// disk_buffer_ref{buf} state contributing to m_blocks. With nothing
	// inserting more writes (back-pressure observers waiting for
	// on_disk()) the cache can never drop below the low watermark and
	// the back-pressure observer is never notified.
	auto& view3 = m_pieces.template get<0>();
	for (auto piece_iter = view3.begin(); piece_iter != view3.end();)
	{
		// safety net pass: exit only on the actual level. See the comment
		// in the cheap pass above for why we don't subtract the concurrent
		// flushing count from this check.
		if (m_blocks + int(m_v2_hash_queue.size()) <= target_blocks) return;

		// skip pieces a hasher or another flush is currently using
		if (piece_iter->flags
			& (cached_piece_entry::flushing_flag | cached_piece_entry::hashing_flag))
		{
			++piece_iter;
			continue;
		}

		TORRENT_ASSERT(m_allocator);
		bulk_free_buffer to_free(*m_allocator);
		int const hasher_cursor = piece_iter->hasher_cursor;
		bool const is_v1 = bool(piece_iter->flags & cached_piece_entry::v1_hashes_flag);
		span<cached_block_entry> const blocks = piece_iter->get_blocks();
		for (int i = 0; i < int(blocks.size()); ++i)
		{
			auto& blk = blocks[i];
			if (!blk.has_buf()) continue;
			to_free.add(blk.take_buf());
			TORRENT_ASSERT(m_blocks > 0);
			--m_blocks;
			if (is_v1 && i >= hasher_cursor)
			{
				TORRENT_ASSERT(m_num_unhashed > 0);
				--m_num_unhashed;
			}
		}
		++piece_iter;
	}

	// we may still need to flush blocks at this point, even though we
	// would require read-back later to compute the piece hash
	auto& view4 = m_pieces.template get<0>();
	for (auto piece_iter = view4.begin(); piece_iter != view4.end();)
	{
		// safety-net pass: exit only on the actual level. See the comment
		// in pass 3.
		if (m_blocks + int(m_v2_hash_queue.size()) <= target_blocks) return;

		if (piece_iter->flags & cached_piece_entry::flushing_flag)
		{
			++piece_iter;
			continue;
		}

		int const num_blocks = piece_iter->num_jobs;
		if (num_blocks == 0)
		{
			++piece_iter;
			continue;
		}

		span<cached_block_entry> const blocks = piece_iter->get_blocks();

		piece_iter = flush_piece_impl(view4, piece_iter, f, l, blocks, clear_piece_fun);
	}
}

void disk_cache::remove_storage(storage_index_t const storage, jobqueue_t& aborted)
{
	std::unique_lock<std::mutex> l(m_mutex);

	INVARIANT_CHECK;

	auto& view = m_pieces.template get<0>();
	auto const [begin, end] = view.equal_range(storage, compare_storage());

	for (auto i = begin; i != end;)
	{
		// advance before erasing invalidates the iterator
		auto const next = std::next(i);

		// removing a storage happens after its torrent has been fully torn down:
		// the release_files/stop_torrent fence has flushed every dirty block and
		// no new jobs can be queued, so nothing is mid-flush or mid-hash here.
		TORRENT_ASSERT(!(i->flags & cached_piece_entry::flushing_flag));
		TORRENT_ASSERT(!(i->flags & cached_piece_entry::hashing_flag));

		view.modify(i, [&](cached_piece_entry& e) { clear_piece_impl(e, aborted); });
		view.erase(i);

		i = next;
	}
}

void disk_cache::flush_storage(std::function<int(bitfield&, span<disk_job* const>)> f,
	storage_index_t const storage,
	std::function<void(jobqueue_t, disk_job*)> clear_piece_fun)
{
	std::unique_lock<std::mutex> l(m_mutex);

	INVARIANT_CHECK;

	auto& range_view = m_pieces.template get<0>();
	auto& view = m_pieces.template get<3>();
	auto const [begin, end] = range_view.equal_range(storage, compare_storage());

	std::vector<piece_index_t> pieces;
	for (auto i = begin; i != end; ++i)
		pieces.push_back(i->piece.piece);

	for (auto piece : pieces)
	{
		auto piece_iter = view.find(piece_location{storage, piece});
		if (piece_iter == view.end())
			continue;

		// If another flush_storage() is already waiting on this piece, leave it to
		// them. notify_flushed_flag is a single shared wakeup signal that supports
		// only one waiter at a time: a second waiter could be stranded if the
		// first one clears the flag and then re-flushes the piece -- that
		// re-flush's scope_end would see no flag set and never wake the second.
		if (piece_iter->flags & cached_piece_entry::notify_flushed_flag) continue;

		// Another thread may be flushing this piece right now (flushing_flag set,
		// lock released). Set notify_flushed_flag so that thread's flush_piece_impl
		// wakes us from its scope_end, then wait for it to finish. The flag is only
		// a wakeup signal, not a pin: while we wait the piece may be flushed,
		// hashed, or even evicted by another thread, so we re-look-up the piece on
		// each wakeup and move on if it is gone.
		if (piece_iter->flags & cached_piece_entry::flushing_flag)
		{
			view.modify(piece_iter, [](cached_piece_entry& e)
				{ e.flags |= cached_piece_entry::notify_flushed_flag; });
			m_flushing_cv.wait(l, [&] {
				piece_iter = view.find(piece_location{storage, piece});
				return piece_iter == view.end()
					|| !(piece_iter->flags & cached_piece_entry::flushing_flag);
			});
			if (piece_iter == view.end()) continue;
			view.modify(piece_iter, [](cached_piece_entry& e)
				{ e.flags &= ~cached_piece_entry::notify_flushed_flag; });
		}

		TORRENT_ASSERT(!(piece_iter->flags & cached_piece_entry::flushing_flag));

		int const num_blocks = piece_iter->num_jobs;
		if (num_blocks == 0) continue;

		span<cached_block_entry> const blocks = piece_iter->get_blocks();
		flush_piece_impl(view, piece_iter, f, l, blocks, clear_piece_fun);
		TORRENT_ASSERT(l.owns_lock());
		TORRENT_ASSERT(!(piece_iter->flags & cached_piece_entry::flushing_flag));
	}
}

std::size_t disk_cache::size() const
{
	std::unique_lock<std::mutex> l(m_mutex);
	INVARIANT_CHECK;
	return static_cast<std::size_t>(m_blocks) + m_v2_hash_queue.size();
}

std::tuple<std::int64_t, std::int64_t> disk_cache::stats() const
{
	std::unique_lock<std::mutex> l(m_mutex);
	INVARIANT_CHECK;
	return {std::int64_t(m_blocks) + std::int64_t(m_v2_hash_queue.size()), m_num_unhashed};
}

#if TORRENT_USE_INVARIANT_CHECKS
void disk_cache::check_invariant() const
{
	// mutex must be held by caller
	int dirty_blocks = 0;
	int flushed_blocks = 0;
	int unhashed_blocks = 0;

	// v2_pending on each cpe counts that piece's blocks in m_v2_hash_queue
	// plus any blocks held in a drain batch (popped from the queue but not
	// yet decremented). We can directly observe only the queue half; we
	// derive bounds against the cpe by counting queue entries per piece.
	std::map<piece_location, int> queue_counts;
	for (auto const& entry : m_v2_hash_queue)
		++queue_counts[entry.piece];

	// every queue entry must reference a cpe in the cache: clear_piece_impl
	// and free_piece drop matching entries when a cpe is removed, so a
	// queue entry without a cpe would mean a leak.
	{
		auto const& view0 = m_pieces.template get<0>();
		for (auto const& entry : m_v2_hash_queue)
		{
			auto const it = view0.find(entry.piece);
			TORRENT_ASSERT(it != view0.end());
			TORRENT_ASSERT(int(entry.block) < it->blocks_in_piece());
		}
	}

	int total_v2_pending = 0;

	auto& view = m_pieces.template get<2>();
	for (auto const& piece_entry : view)
	{
		int const num_blocks = piece_entry.blocks_in_piece();
		bool const is_v1 = bool(piece_entry.flags & cached_piece_entry::v1_hashes_flag);

		span<cached_block_entry> const blocks = piece_entry.get_blocks();

		TORRENT_ASSERT(piece_entry.flushed_cursor <= num_blocks);
		TORRENT_ASSERT(piece_entry.hasher_cursor <= num_blocks);

		// each block can contribute at most one v2 hash queue entry (insert()
		// pushes once per write, and a block can only be written once per
		// cycle), so v2_pending is bounded by blocks_in_piece.
		TORRENT_ASSERT(int(piece_entry.v2_pending) <= num_blocks);

		// v2_pending >= (queue entries for this piece). The slack is the
		// number of entries this piece has in flight inside drain batches.
		{
			auto const it = queue_counts.find(piece_entry.piece);
			int const queued = (it != queue_counts.end()) ? it->second : 0;
			TORRENT_ASSERT(queued <= int(piece_entry.v2_pending));
		}
		total_v2_pending += int(piece_entry.v2_pending);

		int idx = 0;
		for (auto& be : blocks)
		{
			if (auto* j = be.get_write_job())
			{
				auto const& wj = std::get<aux::job::write>(j->action);
				// If wj.buf is empty, the buffer for this block lives in a
				// v2 hash queue entry (counted by queue_blocks below), not
				// in this write job, so this slot does not hold a buffer
				// in cache.
				if (bool(wj.buf)) ++dirty_blocks;
			}
			if (be.has_buf()) ++flushed_blocks;

			if (!(piece_entry.flags & cached_piece_entry::flushing_flag))
			{
				// while a piece is being written to
				// disk, the corresponding thread owns
				// the piece entry and it will move
				// write jobs onto a completed queue
				// before clearing this pointer. From a
				// separate thread's point of view,
				// this invariant may be violated while
				// this is happening
				if (auto* j = be.get_write_job())
				{
					TORRENT_ASSERT(j->get_type() == aux::job_action_t::write);
					TORRENT_ASSERT(j->next == nullptr);
				}

				if (idx < piece_entry.flushed_cursor)
					TORRENT_ASSERT(be.get_write_job() == nullptr);
				else if (idx == piece_entry.flushed_cursor)
					TORRENT_ASSERT(!be.is_flushed());

				if (piece_entry.flags & cached_piece_entry::force_flush_flag)
					TORRENT_ASSERT(be.get_write_job() != nullptr
						|| be.is_flushed()
						|| piece_entry.hasher_cursor == piece_entry.blocks_in_piece());
			}

			if (is_v1 && idx >= piece_entry.hasher_cursor && (be.has_buf() || be.get_write_job()))
				++unhashed_blocks;

			++idx;
		}
	}
	// if one or more blocks are being flushed, we cannot know how many blocks
	// are in flight. We just know the limit
	TORRENT_ASSERT(dirty_blocks <= m_blocks);
	TORRENT_ASSERT(dirty_blocks + flushed_blocks == m_blocks);
	TORRENT_ASSERT(unhashed_blocks == m_num_unhashed);

	// Sum of v2_pending across cpes equals (queue size) + (in-flight drain
	// batches' size). Without drain activity Σ v2_pending == queue size;
	// with a drain mid-execution the sum is strictly greater.
	TORRENT_ASSERT(int(m_v2_hash_queue.size()) <= total_v2_pending);
}
#endif

// this requires the mutex to be locked
void disk_cache::clear_piece_impl(cached_piece_entry& cpe, jobqueue_t& aborted)
{
	INVARIANT_CHECK;
	TORRENT_ASSERT(!(cpe.flags & cached_piece_entry::flushing_flag));
	TORRENT_ASSERT(!(cpe.flags & cached_piece_entry::hashing_flag));

	std::uint16_t jobs = 0;
	int const hasher_cursor = cpe.hasher_cursor;
	TORRENT_ASSERT(m_allocator);
	bulk_free_buffer to_free(*m_allocator);

	// hash_job may be hung via wait_for_v2_queue (hash request waiting for
	// the hasher to finish). Abort it -- the caller is throwing the piece
	// away.
	if (cpe.hash_job) aborted.push_back(std::exchange(cpe.hash_job, nullptr));

	// drop any queued v2 hash entries for this piece. The queue owns those
	// buffers and they're released when the entries go out of scope. v2_pending
	// is updated by the drain when it disposes in-flight entries, which is
	// safe because those entries own their own buffer copy.
	{
		int const dropped = drop_v2_queue_entries(cpe.piece);
		TORRENT_ASSERT(cpe.v2_pending >= dropped);
		cpe.v2_pending -= static_cast<std::uint16_t>(dropped);
	}

	bool const is_v1 = bool(cpe.flags & cached_piece_entry::v1_hashes_flag);
	for (int idx = 0; idx < cpe.blocks_in_piece(); ++idx)
	{
		auto& cbe = cpe.blocks[idx];
		if (is_v1 && cbe.data() && idx >= hasher_cursor)
		{
			TORRENT_ASSERT(m_num_unhashed > 0);
			--m_num_unhashed;
		}

		if (auto* j = cbe.get_write_job())
		{
			// only decrement m_blocks if the cache owned the buffer (i.e.
			// wj.buf is non-empty). Queue-owned buffers were never counted.
			auto const& job = std::get<aux::job::write>(j->action);
			bool const cache_owned = bool(job.buf);
			aborted.push_back(cbe.take_write_job());
			++jobs;
			if (cache_owned) --m_blocks;
		}

		if (cbe.has_buf())
		{
			to_free.add(cbe.take_buf());
			TORRENT_ASSERT(m_blocks > 0);
			--m_blocks;
		}
		// reset any null disk_buffer_ref back to monostate so that
		// flushed_cursor=0 is consistent after we reset it below
		if (cbe.is_flushed())
			cbe.write_state = std::monostate{};
	}
	cpe.flags &= ~(cached_piece_entry::force_flush_flag
		| cached_piece_entry::piece_hash_returned_flag
		| cached_piece_entry::needs_hasher_kick_flag);
	cpe.hasher_cursor = 0;
	cpe.flushed_cursor = 0;
	TORRENT_ASSERT(cpe.num_jobs >= jobs);
	cpe.num_jobs -= jobs;
	if (cpe.ph) *cpe.ph = piece_hasher{};
	DLOG("clear_piece: piece: %d\n", static_cast<int>(cpe.piece.piece));
}

}
