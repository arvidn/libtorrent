/*

Copyright (c) 2023, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/disk_cache.hpp"
#include "libtorrent/disk_observer.hpp"
#include "libtorrent/aux_/debug_disk_thread.hpp"

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
		if (b.buf().data() == nullptr) return false;
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
		|| piece.piece_hash_returned);
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

span<char const> cached_block_entry::buf() const
{
	if (buf_holder)
		return {buf_holder.data(), buf_holder.size()};

	if (write_job != nullptr)
	{
		TORRENT_ASSERT(write_job->get_type() == aux::job_action_t::write);
		auto const& job = std::get<job::write>(write_job->action);
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

cached_piece_entry::cached_piece_entry(piece_location const& loc, std::uint16_t const num_blocks, int const piece_size_v2, bool const v1, bool const v2)
	: piece(loc)
	, blocks(aux::make_unique<cached_block_entry[], std::ptrdiff_t>(num_blocks))
	, piece_size2(piece_size_v2)
	, blocks_in_piece(num_blocks)
	, force_flush(false)
	, hashing(false)
	, flushing(false)
	, piece_hash_returned(false)
	, v1_hashes(v1)
	, v2_hashes(v2)
{}

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
bool disk_cache::try_clear_piece(piece_location const loc, pread_disk_job* j, jobqueue_t& aborted)
{
	std::unique_lock<std::mutex> l(m_mutex);

	INVARIANT_CHECK;

	auto& view = m_pieces.template get<0>();
	auto i = view.find(loc);
	if (i == view.end()) return true;
	if (i->flushing)
	{
		// postpone the clearing until we're done flushing
		view.modify(i, [&](cached_piece_entry& e) { e.clear_piece = j; });
		return false;
	}

	// we clear a piece after it fails the hash check. It doesn't make sense
	// to be hashing still
	TORRENT_ASSERT(!i->hashing);
	if (i->hashing)
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
	, pread_disk_job* write_job)
{
	TORRENT_ASSERT(write_job != nullptr);
	std::unique_lock<std::mutex> l(m_mutex);

	INVARIANT_CHECK;

	auto& view = m_pieces.template get<0>();
	auto i = view.find(loc);
	if (i == view.end())
	{
		pread_storage* storage = write_job->storage.get();
		file_storage const& fs = storage->files();
		int const blocks_in_piece = (storage->files().piece_size(loc.piece) + default_block_size - 1) / default_block_size;
		int const piece_size2 = fs.piece_size2(loc.piece);
		i = m_pieces.emplace(loc, blocks_in_piece, piece_size2, storage->v1(), storage->v2()).first;
	}

	TORRENT_ASSERT(!i->piece_hash_returned);

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
	blk.write_job = write_job;
	++m_blocks;
	++m_num_unhashed;

	bool const effective_force_flush = force_flush || compute_force_flush(*i);
	view.modify(i, [effective_force_flush](cached_piece_entry& e) {
		e.force_flush |= effective_force_flush;
		++e.num_jobs;
	});

	insert_result_flags ret{};

	if (m_back_pressure.has_back_pressure(m_blocks, std::move(o)))
		ret |= exceeded_limit;

	if (i->hasher_cursor == block_idx)
		ret |= need_hasher_kick;

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
disk_cache::hash_result disk_cache::try_hash_piece(piece_location const loc, pread_disk_job* hash_job)
{
	std::unique_lock<std::mutex> l(m_mutex);

	INVARIANT_CHECK;

	auto& view = m_pieces.template get<0>();
	auto i = view.find(loc);
	if (i == view.end()) return hash_result::post_job;

	if (!i->hashing && i->hasher_cursor == i->blocks_in_piece)
	{
		view.modify(i, [&](cached_piece_entry& e) {
			e.piece_hash_returned = true;

			auto& job = std::get<aux::job::hash>(hash_job->action);
			job.piece_hash = e.ph.final_hash();
			if (!job.block_hashes.empty())
			{
				TORRENT_ASSERT(i->v2_hashes);
				for (int idx = 0; idx < e.blocks_in_piece; ++idx)
					job.block_hashes[idx] = e.blocks[idx].block_hash;
			}
		});
		return hash_result::job_completed;
	}

	if (i->hashing
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

// this should be called from a hasher thread
void disk_cache::kick_hasher(piece_location const& loc, jobqueue_t& completed_jobs)
{
	std::unique_lock<std::mutex> l(m_mutex);

	INVARIANT_CHECK;

	auto& view = m_pieces.template get<0>();
	auto piece_iter = view.find(loc);
	if (piece_iter == view.end())
		return;

	// some other thread beat us to it
	if (piece_iter->hashing)
		return;

	// this piece is done hasing
	if (piece_iter->piece_hash_returned)
	{
		// TODO: should we erase the piece from the cache, if it's also done flushing?
		return;
	}

	TORRENT_ALLOCA(blocks_storage, span<char const>, piece_iter->blocks_in_piece);
	std::uint16_t cursor = piece_iter->hasher_cursor;
keep_going:
	std::uint16_t block_idx = 0;
	std::uint16_t end = cursor;
	while (end < piece_iter->blocks_in_piece && piece_iter->blocks[end].buf().data())
	{
		blocks_storage[block_idx] = piece_iter->blocks[end].buf();
		++block_idx;
		++end;
	}
	auto const blocks = blocks_storage.first(block_idx);

	TORRENT_ASSERT(piece_iter->hashing == false);
	view.modify(piece_iter, [](cached_piece_entry& e) { e.hashing = true; });

	bool const need_v1 = piece_iter->v1_hashes;
	bool const need_v2 = piece_iter->v2_hashes;

	DLOG("kick_hasher: piece: %d hashed_cursor: [%d, %d] v1: %d v2: %d ctx: %p\n"
		, static_cast<int>(piece_iter->piece.piece)
		, cursor, end
		, need_v1, need_v2
		, &piece_iter->ph);
	l.unlock();

	int bytes_left = piece_iter->piece_size2 - (cursor * default_block_size);
	int count_hashed = 0;
	for (auto& buf: blocks)
	{
		cached_block_entry& cbe = piece_iter->blocks[cursor];

		if (need_v1)
		{
			auto& ctx = const_cast<aux::piece_hasher&>(piece_iter->ph);
			ctx.update(buf);
		}

		if (need_v2 && bytes_left > 0)
		{
			int const this_block_size = std::min(bytes_left, default_block_size);
			cbe.block_hash = hasher256(buf.first(this_block_size)).final();
			bytes_left -= default_block_size;
		}

		++cursor;
		++count_hashed;
	}

	l.lock();

	TORRENT_ASSERT(m_num_unhashed >= count_hashed);
	m_num_unhashed -= count_hashed;

	// blocks that have been flushed and hashed can be removed from the cache immediately
	int const end_idx = std::min(end, piece_iter->flushed_cursor);
	if (piece_iter->hasher_cursor < end_idx)
	{
		int const count = end_idx - piece_iter->hasher_cursor;
		for (auto& cbe : piece_iter->get_blocks().subspan(piece_iter->hasher_cursor, count))
		{
			// TODO: free these in bulk, acquiring the mutex just once
			// free them after releasing the mutex, l
			if (cbe.buf_holder)
			{
				cbe.buf_holder.reset();
				TORRENT_ASSERT(m_blocks > 0);
				--m_blocks;
			}
		}
	}

	view.modify(piece_iter, [&](cached_piece_entry& e) {
		e.hasher_cursor = cursor;
		e.hashing = false;
	});

	TORRENT_ASSERT(l.owns_lock());
	m_back_pressure.check_buffer_level(m_blocks);

	if (cursor != piece_iter->blocks_in_piece)
	{
		// if some other thread added the next block, keep going
		if (piece_iter->blocks[cursor].buf().data())
			goto keep_going;
		DLOG("kick_hasher: no attached hash job\n");
		return;
	}

	if (!piece_iter->hash_job) return;

	// there's a hash job hung on this piece, post it now
	pread_disk_job* j = nullptr;

	sha1_hash piece_hash;
	TORRENT_ASSERT(!piece_iter->piece_hash_returned);

	bool const force_flush = compute_force_flush(*piece_iter);
	view.modify(piece_iter, [&j, &piece_hash, force_flush](cached_piece_entry& e) {
		j = std::exchange(e.hash_job, nullptr);
		e.force_flush |= force_flush;
		e.piece_hash_returned = true;
		// we've hashed all blocks, and there's a hash job associated with
		// this piece, post it.
		piece_hash = e.ph.final_hash();
	});

	auto& job = std::get<job::hash>(j->action);
	job.piece_hash = piece_hash;
	if (!job.block_hashes.empty())
	{
		TORRENT_ASSERT(need_v2);
		int const to_copy = std::min(
			piece_iter->blocks_in_piece,
			numeric_cast<std::uint16_t>(job.block_hashes.size()));
		for (int i = 0; i < to_copy; ++i)
			job.block_hashes[i] = piece_iter->blocks[i].block_hash;
	}
	DLOG("kick_hasher: posting attached job piece: %d\n"
		, static_cast<int>(piece_iter->piece.piece));
	completed_jobs.push_back(j);
}

template <typename Iter, typename View>
Iter disk_cache::flush_piece_impl(View& view
	, Iter piece_iter
	, std::function<int(bitfield&, span<cached_block_entry const>)> const& f
	, std::unique_lock<std::mutex>& l
	, span<cached_block_entry> const blocks
	, std::function<void(jobqueue_t, pread_disk_job*)> clear_piece_fun)
{
	view.modify(piece_iter, [](cached_piece_entry& e) { TORRENT_ASSERT(!e.flushing); e.flushing = true; });
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
				TORRENT_ASSERT(e.flushing);
				e.flushing = false;
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
	for (int i = 0; i < blocks.size(); ++i)
	{
		if (!flushed_blocks.get_bit(i)) continue;
		cached_block_entry& blk = blocks[i];
		int const block_index = block_offset + i;

		auto* j = std::exchange(blk.write_job, nullptr);
		TORRENT_ASSERT(j);
		TORRENT_ASSERT(j->get_type() == aux::job_action_t::write);
		blk.buf_holder = std::move(std::get<aux::job::write>(j->action).buf);
		if (!j->error.ec)
			blk.flushed_to_disk = true;
		TORRENT_ASSERT(blk.buf_holder);

		// TODO: free these in bulk at the end, after releasing the mutex
		// if another thread is currently hashing blocks in this piece, we
		// can't remove the ones past the current hasher_cursor. They are in
		// use.
		if (block_index < hasher_cursor || !piece_iter->hashing)
		{
			blk.buf_holder.reset();
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
		e.force_flush |= force_flush;
		TORRENT_ASSERT(e.num_jobs >= jobs);
		e.num_jobs -= jobs;
	});
	DLOG("flush_piece_impl: piece: %d flushed_cursor: %d force_flush: %d\n"
		, static_cast<int>(piece_iter->piece.piece), piece_iter->flushed_cursor, piece_iter->force_flush);
	TORRENT_ASSERT(count <= blocks.size());
	if (piece_iter->clear_piece)
	{
		jobqueue_t aborted;
		pread_disk_job* clear_piece = nullptr;
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
	if (cpe.piece_hash_returned)
	{
		TORRENT_ASSERT(cpe.flushed_cursor == cpe.blocks_in_piece);
		TORRENT_ASSERT(cpe.hasher_cursor == cpe.blocks_in_piece);
	}
#endif
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
			// TODO: free these in bulk
			blk.buf_holder.reset();
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
	, std::function<void(jobqueue_t, pread_disk_job*)> clear_piece_fun
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

		if (piece_iter->flushing)
		{
			++piece_iter;
			continue;
		}

		if (!piece_iter->force_flush)
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
			&& piece_iter->piece_hash_returned
			&& !piece_iter->flushing)
		{
			TORRENT_ASSERT(!piece_iter->hashing);
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

		if (piece_iter->flushing)
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

		if (piece_iter->flushing)
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
	, std::function<void(jobqueue_t, pread_disk_job*)> clear_piece_fun)
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
		if (piece_iter->flushing)
			continue;

		int const num_blocks = piece_iter->num_jobs;
		TORRENT_ASSERT(count_jobs(piece_iter->get_blocks()) == num_blocks);
		if (num_blocks == 0) continue;
		span<cached_block_entry> const blocks = piece_iter->get_blocks();

		flush_piece_impl(view, piece_iter, f, l
			, blocks, clear_piece_fun);

		TORRENT_ASSERT(!piece_iter->flushing);
		TORRENT_ASSERT(!piece_iter->hashing);
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

		if (piece_entry.flushing)
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

			if (!piece_entry.flushing)
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

				if (piece_entry.force_flush)
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
	TORRENT_ASSERT(!cpe.flushing);
	TORRENT_ASSERT(!cpe.hashing);
	std::uint16_t jobs = 0;
	int const hasher_cursor = cpe.hasher_cursor;
	for (int idx = 0; idx < cpe.blocks_in_piece; ++idx)
	{
		auto& cbe = cpe.blocks[idx];
		if (cbe.buf().data() && idx >= hasher_cursor)
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


		// TODO: free these in bulk, acquiring the mutex just once
		// free them after releasing the mutex, l
		if (cbe.buf_holder)
		{
			cbe.buf_holder.reset();
			TORRENT_ASSERT(m_blocks > 0);
			--m_blocks;
		}
	}
	cpe.force_flush = false;
	cpe.piece_hash_returned = false;
	cpe.hasher_cursor = 0;
	cpe.flushed_cursor = 0;
	TORRENT_ASSERT(cpe.num_jobs >= jobs);
	cpe.num_jobs -= jobs;
	cpe.ph = piece_hasher{};
	DLOG("clear_piece: piece: %d\n", static_cast<int>(cpe.piece.piece));
}

}
