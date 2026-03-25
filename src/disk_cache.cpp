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

bool have_buffers(span<const cached_block_entry> blocks)
{
	for (auto const& b : blocks)
		if (b.data() == nullptr) return false;
	return true;
}

bool compute_force_flush(cached_piece_entry const& piece)
{
	// piece that are partial on startup won't have the flushed_cursor
	// updated to indicate what's on disk and what's in the cache. Once
	// the bittorrent engine asks for the piece hash, we know the piece is
	// supposed to be complete. After hashing, we should flush any remaining
	// blocks to disk
	return (piece.hasher_cursor == piece.blocks_in_piece
		|| bool(piece.flags & cached_piece_entry::piece_hash_returned_flag));
}

std::uint16_t compute_flushed_cursor(span<const cached_block_entry> blocks)
{
	std::uint16_t ret = 0;
	for (auto const& b : blocks)
	{
		if (!b.flushed_to_disk) return ret;
		++ret;
	}
	return ret;
}

std::uint16_t count_jobs(span<const cached_block_entry> blocks)
{
	return static_cast<std::uint16_t>(std::count_if(blocks.begin(), blocks.end()
		, [](cached_block_entry const& b) { return b.write_job; }));
}

}

char const* cached_block_entry::data() const noexcept
{
	if (buf_holder) return buf_holder.data();
	if (write_job != nullptr)
	{
		TORRENT_ASSERT(write_job->get_type() == aux::job_action_t::write);
		return std::get<job::write>(write_job->action).buf.data();
	}
	return nullptr;
}

span<char const> cached_block_entry::buf(int const block_size) const
{
	if (buf_holder)
	{
		TORRENT_ASSERT(block_size > 0);
		return {buf_holder.data(), block_size};
	}

	if (write_job != nullptr)
	{
		TORRENT_ASSERT(write_job->get_type() == aux::job_action_t::write);
		auto const& job = std::get<job::write>(write_job->action);
		TORRENT_ASSERT(block_size == job.buffer_size);
		return {job.buf.data(), job.buffer_size};
	}
	return {nullptr, 0};
}

span<char const> cached_block_entry::write_buf() const
{
	if (write_job != nullptr)
	{
		TORRENT_ASSERT(write_job->get_type() == aux::job_action_t::write);
		auto const& job = std::get<job::write>(write_job->action);
		return {job.buf.data(), job.buffer_size};
	}
	return {nullptr, 0};
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

cached_piece_entry::cached_piece_entry(piece_location const& loc, std::uint16_t const num_blocks
	, int const piece_size_v2, int const piece_size_arg, bool const v1, bool const v2)
	: piece(loc)
	, blocks(aux::make_unique<cached_block_entry[], std::ptrdiff_t>(num_blocks))
	, block_hashes(v2 ? aux::make_unique<sha256_hash[], std::ptrdiff_t>(num_blocks) : aux::unique_ptr<sha256_hash[]>())
	, ph(v1 ? std::make_unique<piece_hasher>() : nullptr)
	, piece_size2(piece_size_v2)
	, piece_size(piece_size_arg)
	, blocks_in_piece(num_blocks)
	, flags((v1 ? v1_hashes_flag : cached_piece_flags{})
		| (v2 ? v2_hashes_flag : cached_piece_flags{}))
{
	TORRENT_ASSERT(piece_size_arg > 0);
	TORRENT_ASSERT(num_blocks == (piece_size_arg + default_block_size - 1) / default_block_size);
}

span<cached_block_entry> cached_piece_entry::get_blocks() const
{
	return {blocks.get(), blocks_in_piece};
}

disk_cache::disk_cache(io_context& ios)
	: m_back_pressure(ios)
{}

// If the specified piece exists in the cache, and it's unlocked, clear all
// write jobs (return them in "aborted"). Returns true if the clear_piece
// job should be posted as complete. Returns false if the piece is locked by
// another thread, and the clear_piece job has been queued to be issued once
// the piece is unlocked.
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

	// we clear a piece after it fails the hash check. It doesn't make sense
	// to be hashing still
	TORRENT_ASSERT(!(i->flags & cached_piece_entry::hashing_flag));
	if (i->flags & cached_piece_entry::hashing_flag)
	{
		// postpone the clearing until we're done hashing
		view.modify(i, [&](cached_piece_entry& e) { e.clear_piece = j; });
		return false;
	}

	view.modify(i, [&](cached_piece_entry& e) {
		clear_piece_impl(e, aborted);
	});
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
		i = m_pieces.emplace(loc, params.blocks_in_piece, params.piece_size2
			, params.piece_size, params.v1, params.v2).first;
	}

	TORRENT_ASSERT(!(i->flags & cached_piece_entry::piece_hash_returned_flag));

	cached_block_entry& blk = i->blocks[block_idx];
	DLOG("disk_cache.insert: piece: %d blk: %d flushed: %d write_job: %p flushed_cursor: %d hashed_cursor: %d\n"
		, static_cast<int>(i->piece.piece)
		, block_idx
		, blk.flushed_to_disk
		, blk.write_job
		, i->flushed_cursor
		, i->hasher_cursor);
	TORRENT_ASSERT(!blk.buf_holder);
	TORRENT_ASSERT(blk.write_job == nullptr);
	TORRENT_ASSERT(blk.flushed_to_disk == false);
	TORRENT_ASSERT(block_idx >= i->flushed_cursor);
	TORRENT_ASSERT(block_idx >= i->hasher_cursor);

	TORRENT_ASSERT(write_job->get_type() == aux::job_action_t::write);
	TORRENT_ASSERT(std::get<aux::job::write>(write_job->action).buffer_size
		== std::min(default_block_size, params.piece_size - block_idx * default_block_size));
	// All write jobs must use the same disk buffer allocator
	if (!m_allocator)
		m_allocator = std::get<aux::job::write>(write_job->action).buf.m_allocator;
	else
		TORRENT_ASSERT(m_allocator == std::get<aux::job::write>(write_job->action).buf.m_allocator);

	blk.write_job = write_job;
	++m_blocks;
	++m_num_unhashed;

	bool const effective_force_flush = force_flush || compute_force_flush(*i);
	view.modify(i, [effective_force_flush](cached_piece_entry& e) {
		if (effective_force_flush) e.flags |= cached_piece_entry::force_flush_flag;
		++e.num_jobs;
	});

	insert_result_flags ret{};

	if (m_back_pressure.has_back_pressure(m_blocks, std::move(o)))
		ret |= exceeded_limit;

	if ((i->hasher_cursor == block_idx
		|| bool(i->flags & cached_piece_entry::v2_hashes_flag))
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
	return m_back_pressure.should_flush(m_blocks);
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

	if (!(i->flags & cached_piece_entry::hashing_flag) && i->hasher_cursor == i->blocks_in_piece)
	{
		view.modify(i, [&](cached_piece_entry& e) {
			// mark for flush so a generic thread will write any pending
			// write_jobs that are still in the cache for this piece
			e.flags |= cached_piece_entry::piece_hash_returned_flag | cached_piece_entry::force_flush_flag;

			auto& job = std::get<aux::job::hash>(hash_job->action);
			TORRENT_ASSERT(bool(e.ph) == bool(e.flags & cached_piece_entry::v1_hashes_flag));
			job.piece_hash = e.ph ? e.ph->final_hash() : sha1_hash{};
			if (!job.block_hashes.empty())
			{
				TORRENT_ASSERT(bool(i->flags & cached_piece_entry::v2_hashes_flag));
				TORRENT_ASSERT(e.block_hashes);
				TORRENT_ASSERT(job.block_hashes.size() <= e.blocks_in_piece);
				int const to_copy = std::min(int(job.block_hashes.size()), int(e.blocks_in_piece));
				for (int idx = 0; idx < to_copy; ++idx)
					job.block_hashes[idx] = e.block_hashes[idx];
			}
		});
		return hash_result::job_completed;
	}

	if ((i->flags & cached_piece_entry::hashing_flag)
		&& i->hasher_cursor < i->blocks_in_piece
		&& have_buffers(i->get_blocks().subspan(i->hasher_cursor))
		)
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

	// NOTE: block_storage is indexed starting at cursor. We don't waste space
	// allocating a slot for every block, just the ones in front of the cursor
	TORRENT_ALLOCA(blocks_storage, span<char const>, piece_iter->blocks_in_piece - int(cursor));

	int count_hashed = 0;

	// hashing_flag is already set by caller. We need to clear it before returning.
	TORRENT_ASSERT(piece_iter->flags & cached_piece_entry::hashing_flag);

keep_going:

	// Snapshot all block buffer pointers from cursor onwards while holding the
	// mutex. New blocks may be added asynchronously after we release the lock,
	// so we must not read their buf() pointers then.
	int const n = piece_iter->blocks_in_piece - int(cursor);
	for (int i = 0; i < n; ++i)
	{
		int const blk_idx = int(cursor) + i;
		int const blk_size = std::min(default_block_size
			, piece_iter->piece_size - blk_idx * default_block_size);
		blocks_storage[i] = piece_iter->blocks[blk_idx].buf(blk_size);
	}

	// end = first gap in the contiguous run, needed for flushed_cursor trimming
	std::uint16_t end = cursor;
	while (end < piece_iter->blocks_in_piece && blocks_storage[end - cursor].data())
		++end;

	bool const need_v1 = bool(piece_iter->flags & cached_piece_entry::v1_hashes_flag);
	bool const need_v2 = bool(piece_iter->flags & cached_piece_entry::v2_hashes_flag);

	DLOG("kick_hasher: piece: %d hashed_cursor: [%d, %d] v1: %d v2: %d ctx: %p\n"
		, static_cast<int>(piece_iter->piece.piece)
		, cursor, end
		, need_v1, need_v2
		, piece_iter->ph.get());

	l.unlock();

	std::uint16_t const cursor_start = cursor;
	int bytes_left = piece_iter->piece_size2 - int(cursor_start) * default_block_size;
	bool contiguous = true;

	// NOTE: i is in the blocks_storage space. It's offset by "cursor_start" to
	// get back to block index
	for (int i = 0; i < n; ++i, bytes_left -= default_block_size)
	{
		span<char const> const buf = blocks_storage[i];
		if (buf.data() == nullptr)
		{
			contiguous = false;
			if (!need_v2) break;
			continue;
		}

		if (contiguous)
		{
			if (need_v1)
			{
				TORRENT_ASSERT(piece_iter->ph);
				auto& ctx = const_cast<aux::piece_hasher&>(*piece_iter->ph);
				ctx.update(buf);
			}
			++cursor;
			++count_hashed;
		}

		if (need_v2)
		{
			TORRENT_ASSERT(piece_iter->block_hashes);
			int const blk_idx = int(cursor_start) + i;
			if (bytes_left > 0 && piece_iter->block_hashes[blk_idx].is_all_zeros())
				piece_iter->block_hashes[blk_idx] = hasher256(buf.first(std::min(bytes_left, default_block_size))).final();
		}
	}

	l.lock();

	// if some other thread added the next block, keep going
	if (cursor != piece_iter->blocks_in_piece
		&& piece_iter->blocks[cursor].data())
	{
		goto keep_going;
	}

	TORRENT_ASSERT(m_num_unhashed >= count_hashed);
	m_num_unhashed -= count_hashed;

	// blocks that have been flushed and hashed can be removed from the cache immediately
	int const count = cursor - piece_iter->hasher_cursor;
	TORRENT_ASSERT(m_allocator);
	bulk_free_buffer to_free(*m_allocator);
	for (auto& cbe : piece_iter->get_blocks().subspan(piece_iter->hasher_cursor, count))
	{
		if (!cbe.buf_holder) continue;
		to_free.add(std::move(cbe.buf_holder));
		TORRENT_ASSERT(m_blocks > 0);
		--m_blocks;
	}

	TORRENT_ASSERT(l.owns_lock());
	m_back_pressure.check_buffer_level(m_blocks);

	auto& view = m_pieces.template get<4>();

	if (cursor != piece_iter->blocks_in_piece)
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
	disk_job* j = nullptr;

	sha1_hash piece_hash;
	TORRENT_ASSERT(!(piece_iter->flags & cached_piece_entry::piece_hash_returned_flag));

	bool const force_flush = cursor == piece_iter->blocks_in_piece
		|| bool(piece_iter->flags & cached_piece_entry::piece_hash_returned_flag);
	view.modify(piece_iter, [&j, &piece_hash, force_flush, cursor](cached_piece_entry& e) {
		j = std::exchange(e.hash_job, nullptr);
		if (force_flush) e.flags |= cached_piece_entry::force_flush_flag;
		e.hasher_cursor = cursor;
		e.flags &= ~cached_piece_entry::hashing_flag;
		e.flags |= cached_piece_entry::piece_hash_returned_flag;
		// we've hashed all blocks, and there's a hash job associated with
		// this piece, post it.
		TORRENT_ASSERT(bool(e.ph) == bool(e.flags & cached_piece_entry::v1_hashes_flag));
		piece_hash = e.ph ? e.ph->final_hash() : sha1_hash{};
	});

	auto& job = std::get<job::hash>(j->action);
	if (need_v1) job.piece_hash = piece_hash;
	if (!job.block_hashes.empty())
	{
		TORRENT_ASSERT(need_v2);
		TORRENT_ASSERT(piece_iter->block_hashes);
		int const to_copy = std::min(
			piece_iter->blocks_in_piece,
			numeric_cast<std::uint16_t>(job.block_hashes.size()));
		for (int i = 0; i < to_copy; ++i)
			job.block_hashes[i] = piece_iter->block_hashes[i];
	}
	DLOG("kick_hasher: posting attached job piece: %d\n"
		, static_cast<int>(piece_iter->piece.piece));
	completed_jobs.push_back(j);
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
		auto it = view.begin();
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
Iter disk_cache::flush_piece_impl(View& view
	, Iter piece_iter
	, std::function<int(bitfield&, span<cached_block_entry const>)> const& f
	, std::unique_lock<std::mutex>& l
	, span<cached_block_entry> const blocks
	, std::function<void(jobqueue_t, disk_job*)> clear_piece_fun)
{
	view.modify(piece_iter, [](cached_piece_entry& e) { TORRENT_ASSERT(!(e.flags & cached_piece_entry::flushing_flag)); e.flags |= cached_piece_entry::flushing_flag; });
	int const num_blocks = count_jobs(blocks);
	if (num_blocks <= 0)
		return std::next(piece_iter);

	// blocks may be a subspan of all the blocks in the piece, so when comparing flushed_cursor and hasher_cursor, we need to add the offset.
	// TODO: pass the block offset as a parameter instead of computing it like this
	int const block_offset = static_cast<int>(blocks.data() - piece_iter->get_blocks().data());

	m_flushing_blocks += num_blocks;

	// we have to release the lock while flushing, but since we set the
	// "flushing" member to true, this piece is pinned to the cache
	l.unlock();

	int count = 0;
	bitfield flushed_blocks;
	{
		auto se = scope_end([&] {
			l.lock();
			view.modify(piece_iter, [](cached_piece_entry& e) {
				TORRENT_ASSERT(bool(e.flags & cached_piece_entry::flushing_flag));
				e.flags &= ~cached_piece_entry::flushing_flag;
			});
			TORRENT_ASSERT(m_flushing_blocks >= num_blocks);
			m_flushing_blocks -= num_blocks;
		});
		flushed_blocks.resize(int(blocks.size()));
		flushed_blocks.clear_all();
		count = f(flushed_blocks, blocks);
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

		auto* j = std::exchange(blk.write_job, nullptr);
		TORRENT_ASSERT(j);
		TORRENT_ASSERT(j->get_type() == aux::job_action_t::write);
		auto& job = std::get<aux::job::write>(j->action);
		blk.buf_holder = disk_buffer_ref(std::move(job.buf));
		if (!j->error.ec)
			blk.flushed_to_disk = true;
		TORRENT_ASSERT(blk.buf_holder);

		// if another thread is currently hashing blocks in this piece, we
		// can't remove the ones past the current hasher_cursor. They are in
		// use.
		if (block_index < hasher_cursor || !(piece_iter->flags & cached_piece_entry::hashing_flag))
		{
			to_free.add(std::move(blk.buf_holder));
			if (block_index >= hasher_cursor)
			{
				TORRENT_ASSERT(m_num_unhashed > 0);
				--m_num_unhashed;
			}
			TORRENT_ASSERT(m_blocks > 0);
			--m_blocks;
		}

		++jobs;
	}

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
			clear_piece = std::exchange(e.clear_piece, nullptr);
		});
		clear_piece_fun(std::move(aborted), clear_piece);
	}

	return next_iter;
}

void disk_cache::free_piece(cached_piece_entry const& cpe)
{
#if TORRENT_USE_ASSERTS
	if (cpe.flags & cached_piece_entry::piece_hash_returned_flag)
	{
		TORRENT_ASSERT(cpe.flushed_cursor == cpe.blocks_in_piece);
		TORRENT_ASSERT(cpe.hasher_cursor == cpe.blocks_in_piece);
	}
	TORRENT_ASSERT(cpe.hash_job == nullptr);
#endif
	TORRENT_ASSERT(m_allocator);
	bulk_free_buffer to_free(*m_allocator);
	int idx = 0;
	for (auto& blk : cpe.get_blocks())
	{
		if (blk.buf_holder)
		{
			if (idx >= cpe.hasher_cursor)
			{
				TORRENT_ASSERT(m_num_unhashed > 0);
				--m_num_unhashed;
			}
			--m_blocks;
			to_free.add(std::move(blk.buf_holder));
		}
		TORRENT_ASSERT(!blk.buf_holder || idx >= cpe.hasher_cursor);
		TORRENT_ASSERT(blk.write_job == nullptr);
		++idx;
	}
}

// this should be called by a disk thread
// the callback should return the number of blocks it successfully flushed
// to disk. Optimistic flush means we'll only flush pieces that are ready to
// be flushed, and already hashed. We don't gain anything from keeping those in
// the cache.
void disk_cache::flush_to_disk(
	std::function<int(bitfield&, span<cached_block_entry const>)> f
	, int const target_blocks
	, std::function<void(jobqueue_t, disk_job*)> clear_piece_fun
	, bool const optimistic)
{
	std::unique_lock<std::mutex> l(m_mutex);

	INVARIANT_CHECK;

	auto se = aux::scope_end([&] {
		TORRENT_ASSERT(l.owns_lock());
		// checks to see if we're no longer exceeding the high watermark,
		// and if we're in fact below the low watermark. If so, we need to
		// post the notification messages to the peers that are waiting for
		// more buffers to received data into
		m_back_pressure.check_buffer_level(m_blocks);
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

		TORRENT_ASSERT(piece_iter->blocks_in_piece >= 0);
		if (piece_iter->blocks_in_piece == 0)
		{
			++piece_iter;
			continue;
		}
		span<cached_block_entry> blocks = piece_iter->get_blocks();

		auto const next_iter = flush_piece_impl(view, piece_iter, f, l
			, blocks, clear_piece_fun);

		if (piece_iter->flushed_cursor == piece_iter->blocks_in_piece
			&& bool(piece_iter->flags & cached_piece_entry::piece_hash_returned_flag)
			&& !(piece_iter->flags & cached_piece_entry::flushing_flag))
		{
			TORRENT_ASSERT(!(piece_iter->flags & cached_piece_entry::hashing_flag));
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
		// We avoid flushing if other threads have already initiated sufficient
		// amount of flushing
		if (m_blocks - m_flushing_blocks <= target_blocks)
			return;

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

	// we may still need to flush blocks at this point, even though we
	// would require read-back later to compute the piece hash
	auto& view3 = m_pieces.template get<0>();
	for (auto piece_iter = view3.begin(); piece_iter != view3.end();)
	{
		// We avoid flushing if other threads have already initiated sufficient
		// amount of flushing
		if (m_blocks - m_flushing_blocks <= target_blocks)
			return;

		if (piece_iter->flags & cached_piece_entry::flushing_flag)
		{
			++piece_iter;
			continue;
		}

		int const num_blocks = piece_iter->num_jobs;
		TORRENT_ASSERT(count_jobs(piece_iter->get_blocks()) == num_blocks);
		if (num_blocks == 0)
		{
			++piece_iter;
			continue;
		}

		span<cached_block_entry> const blocks = piece_iter->get_blocks();

		piece_iter = flush_piece_impl(view3, piece_iter, f, l
			, blocks, clear_piece_fun);
	}
}

void disk_cache::flush_storage(std::function<int(bitfield&, span<cached_block_entry const>)> f
	, storage_index_t const storage
	, std::function<void(jobqueue_t, disk_job*)> clear_piece_fun)
{
	std::unique_lock<std::mutex> l(m_mutex);

	INVARIANT_CHECK;

	auto& range_view = m_pieces.template get<0>();
	auto& view = m_pieces.template get<3>();
	auto const [begin, end] = range_view.equal_range(storage, compare_storage());

	std::vector<piece_index_t> pieces;
	for (auto i = begin; i != end; ++i)
		pieces.push_back(i->piece.piece);

	bitfield flushed_blocks;

	for (auto piece : pieces)
	{
		auto piece_iter = view.find(piece_location{storage, piece});
		if (piece_iter == view.end())
			continue;

		// There's a risk that some other thread is flushing this piece, but
		// won't force-flush it completely. In that case parts of the piece
		// may not be flushed
		// TODO: maybe we should track these pieces and synchronize with
		// them later. maybe wait for them to be flushed or hang our job on
		// them, but that would really only work if there's only one piece
		// left
		if (piece_iter->flags & cached_piece_entry::flushing_flag)
			continue;

		int const num_blocks = piece_iter->num_jobs;
		TORRENT_ASSERT(count_jobs(piece_iter->get_blocks()) == num_blocks);
		if (num_blocks == 0) continue;
		span<cached_block_entry> const blocks = piece_iter->get_blocks();

		flush_piece_impl(view, piece_iter, f, l
			, blocks, clear_piece_fun);

		TORRENT_ASSERT(!(piece_iter->flags & cached_piece_entry::flushing_flag));
		TORRENT_ASSERT(!(piece_iter->flags & cached_piece_entry::hashing_flag));
		free_piece(*piece_iter);
		piece_iter = view.erase(piece_iter);
	}
}

std::size_t disk_cache::size() const
{
	std::unique_lock<std::mutex> l(m_mutex);
	INVARIANT_CHECK;
	return static_cast<std::size_t>(m_blocks);
}

std::size_t disk_cache::num_flushing() const
{
	std::unique_lock<std::mutex> l(m_mutex);
	INVARIANT_CHECK;
	return static_cast<std::size_t>(m_flushing_blocks);
}

std::tuple<std::int64_t, std::int64_t> disk_cache::stats() const
{
	std::unique_lock<std::mutex> l(m_mutex);
	INVARIANT_CHECK;
	return {m_blocks, m_num_unhashed};
}

#if TORRENT_USE_INVARIANT_CHECKS
void disk_cache::check_invariant() const
{
	// mutex must be held by caller
	int dirty_blocks = 0;
	int flushed_blocks = 0;
	int flushing_blocks = 0;
	int unhashed_blocks = 0;

	auto& view = m_pieces.template get<2>();
	for (auto const& piece_entry : view)
	{
		int const num_blocks = piece_entry.blocks_in_piece;

		if (piece_entry.flags & cached_piece_entry::flushing_flag)
			flushing_blocks += num_blocks;

		span<cached_block_entry> const blocks = piece_entry.get_blocks();

		TORRENT_ASSERT(piece_entry.flushed_cursor <= num_blocks);
		TORRENT_ASSERT(piece_entry.hasher_cursor <= num_blocks);

		int idx = 0;
		for (auto& be : blocks)
		{
			if (be.write_job) ++dirty_blocks;
			if (be.buf_holder) ++flushed_blocks;

			// a block holds either a write job or buffer, never both
			TORRENT_ASSERT(!(bool(be.write_job) && bool(be.buf_holder)));

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
				if (be.write_job)
				{
					TORRENT_ASSERT(be.write_job->get_type() == aux::job_action_t::write);
					TORRENT_ASSERT(be.write_job->next == nullptr);
				}

				if (idx < piece_entry.flushed_cursor)
					TORRENT_ASSERT(be.write_job == nullptr);
				else if (idx == piece_entry.flushed_cursor)
					TORRENT_ASSERT(!be.buf_holder);

				if (piece_entry.flags & cached_piece_entry::force_flush_flag)
					TORRENT_ASSERT(be.write_job != nullptr
						|| be.flushed_to_disk
						|| piece_entry.hasher_cursor == piece_entry.blocks_in_piece);
			}

			if (idx >= piece_entry.hasher_cursor && (bool(be.buf_holder) || be.write_job))
				++unhashed_blocks;

			++idx;
		}
	}
	// if one or more blocks are being flushed, we cannot know how many blocks
	// are in flight. We just know the limit
	TORRENT_ASSERT(dirty_blocks <= m_blocks);
	TORRENT_ASSERT(dirty_blocks + flushed_blocks == m_blocks);
	TORRENT_ASSERT(flushing_blocks >= m_flushing_blocks);
	TORRENT_ASSERT(unhashed_blocks == m_num_unhashed);
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
	for (int idx = 0; idx < cpe.blocks_in_piece; ++idx)
	{
		auto& cbe = cpe.blocks[idx];
		if (cbe.data() && idx >= hasher_cursor)
		{
			TORRENT_ASSERT(m_num_unhashed > 0);
			--m_num_unhashed;
		}

		if (cbe.write_job)
		{
			aborted.push_back(cbe.write_job);
			cbe.write_job = nullptr;
			++jobs;
			--m_blocks;
		}
		cbe.flushed_to_disk = false;

		if (cbe.buf_holder)
		{
			to_free.add(std::move(cbe.buf_holder));
			TORRENT_ASSERT(m_blocks > 0);
			--m_blocks;
		}
	}
	cpe.flags &= ~(cached_piece_entry::force_flush_flag
		| cached_piece_entry::piece_hash_returned_flag
		| cached_piece_entry::needs_hasher_kick_flag);
	cpe.hasher_cursor = 0;
	cpe.flushed_cursor = 0;
	if (cpe.block_hashes)
		std::fill_n(cpe.block_hashes.get(), cpe.blocks_in_piece, sha256_hash{});
	TORRENT_ASSERT(cpe.num_jobs >= jobs);
	cpe.num_jobs -= jobs;
	if (cpe.ph) *cpe.ph = piece_hasher{};
	DLOG("clear_piece: piece: %d\n", static_cast<int>(cpe.piece.piece));
}

}
