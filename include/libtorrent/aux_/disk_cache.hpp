/*

Copyright (c) 2023, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_CACHE
#define TORRENT_DISK_CACHE

#include <unordered_map>
#include <mutex>

#include "libtorrent/storage_defs.hpp"
#include "libtorrent/aux_/scope_end.hpp"
#include "libtorrent/aux_/alloca.hpp"
#include "libtorrent/aux_/invariant_check.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/functional/hash.hpp>

#define BOOST_BIND_NO_PLACEHOLDERS

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/member.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"


namespace libtorrent {
namespace aux {

namespace mi = boost::multi_index;

// uniquely identifies a torrent and piece
struct piece_location
{
	piece_location(storage_index_t const t, piece_index_t const p)
		: torrent(t), piece(p) {}
	storage_index_t torrent;
	piece_index_t piece;
	bool operator==(piece_location const& rhs) const
	{
		return std::tie(torrent, piece)
			== std::tie(rhs.torrent, rhs.piece);
	}

	bool operator<(piece_location const& rhs) const
	{
		return std::tie(torrent, piece)
			< std::tie(rhs.torrent, rhs.piece);
	}
};

struct cached_block_entry
{
	char* buf = nullptr;
	// once the write job has been executed, and we've flushed the buffer, we
	// move it into buf_holder, to keep the buffer alive until any hash job has
	// completed as well. The underlying data can be accessed through buf, but
	// the owner moves from the pread_disk_job object to this buf_holder.
	// TODO: save space by just storing the buffer pointer here. The
	// cached_piece_entry could hold the pointer to the buffer pool to be able
	// to free these on destruction
	disk_buffer_holder buf_holder;
	pread_disk_job* write_job = nullptr;
};

struct cached_piece_entry
{
	cached_piece_entry(piece_location const& loc, int const num_blocks)
		: piece(loc)
		, blocks_in_piece(num_blocks)
		, blocks(std::make_unique<cached_block_entry[]>(num_blocks))
		, ph(hasher())
	{}

	piece_location piece;

	// this is set to true when the piece has been populated with all blocks
	bool ready_to_flush = false;

	// when this is true, there is a thread currently hashing blocks and
	// updating the hash context in "ph".
	bool hashing = false;

	// when a thread sis writing this piece to disk, this is true. Only one
	// thread at a time should be writing a piece.
	bool flushing = false;

	// this is set to true if the piece hash has been computed and returned
	// to the bittorrent engine.
	bool piece_hash_returned = false;

	int blocks_in_piece = 0;

	// the number of blocks that have been hashed so far
	int hasher_cursor = 0;

	std::unique_ptr<cached_block_entry[]> blocks;

	hasher ph;

	// if there is a hash_job set on this piece, whenever we complete hashing
	// the last block, we should post this
	pread_disk_job* hash_job = nullptr;

	// if the piece has been requested to be cleared, but it was locked
	// (flushing) at the time. We hang this job here to complete it once the
	// thread currently flushing is done with it
	pread_disk_job* clear_piece = nullptr;
};

struct disk_cache
{
	using piece_container = mi::multi_index_container<
		cached_piece_entry,
		mi::indexed_by<
		// look up pieces by (torrent, piece-index) key
		mi::ordered_unique<mi::member<cached_piece_entry, piece_location, &cached_piece_entry::piece>>,
		// ordered by the number of contiguous blocks we can flush without
		// read-back
		// large numbers are ordered first
		mi::ordered_non_unique<mi::member<cached_piece_entry, int, &cached_piece_entry::hasher_cursor>, std::greater<void>>,
		// ordered by whether the piece is ready to be flushed or not
		// true is ordered before false
		mi::ordered_non_unique<mi::member<cached_piece_entry, bool, &cached_piece_entry::ready_to_flush>, std::greater<void>>
		>
	>;

	template <typename Fun>
	bool get(piece_location const loc, int const block_idx, Fun f) const
	{
		std::unique_lock<std::mutex> l(m_mutex);

		INVARIANT_CHECK;

		auto& view = m_pieces.template get<0>();
		auto i = view.find(loc);
		if (i == view.end()) return false;

		if (i->blocks[block_idx].buf)
		{
			// TODO: it would be nice if this could be called without holding
			// the mutex. It would require ibeing able to lock the piece
			f(i->blocks[block_idx].buf);
			return true;
		}
		return false;
	}

	// If the specified piece exists in the cache, and it's unlocked, clear all
	// write jobs (return them in "aborted"). Returns true if the clear_piece
	// job should be posted as complete. Returns false if the piece is locked by
	// another thread, and the clear_piece job has been queued to be issued once
	// the piece is unlocked.
	bool try_clear_piece(piece_location const loc, pread_disk_job* j, jobqueue_t& aborted)
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
			// postpone the clearing until we're done flushing
			view.modify(i, [&](cached_piece_entry& e) { e.clear_piece = j; });
			return false;
		}

		clear_piece_impl(const_cast<cached_piece_entry&>(*i), aborted);
		return true;
	}

/*
	template <typename Fun>
	int get2(piece_location const loc1, piece_location const loc2, Fun f) const
	{
		std::unique_lock<std::mutex> l(m_mutex);
		auto const it1 = m_store_buffer.find(loc1);
		auto const it2 = m_store_buffer.find(loc2);
		char const* buf1 = (it1 == m_store_buffer.end()) ? nullptr : it1->second;
		char const* buf2 = (it2 == m_store_buffer.end()) ? nullptr : it2->second;

		if (buf1 == nullptr && buf2 == nullptr)
			return 0;

		return f(buf1, buf2);
	}
*/
	void insert(piece_location const loc, int const block_idx, pread_disk_job* write_job)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		INVARIANT_CHECK;

		auto& view = m_pieces.template get<0>();
		auto i = view.find(loc);
		if (i == view.end())
		{
			int const blocks_in_piece = (write_job->storage->files().piece_size(loc.piece) + default_block_size - 1) / default_block_size;
			cached_piece_entry pe(loc, blocks_in_piece);
			i = m_pieces.insert(std::move(pe)).first;
		}

		TORRENT_ASSERT(i->blocks[block_idx].buf == nullptr);
		TORRENT_ASSERT(i->blocks[block_idx].write_job == nullptr);
		i->blocks[block_idx].buf = std::get<job::write>(write_job->action).buf.data();
		i->blocks[block_idx].write_job = write_job;
		++m_blocks;
		// TODO: maybe trigger hash job
	}

	// this should be called from a hasher thread
	void kick_hasher(piece_location const& loc, jobqueue_t& completed_jobs)
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

		int cursor = piece_iter->hasher_cursor;
keep_going:
		int end = cursor;
		while (end < piece_iter->blocks_in_piece && piece_iter->blocks[end].buf)
			++end;

		hasher& ctx = const_cast<hasher&>(piece_iter->ph);

		view.modify(piece_iter, [](cached_piece_entry& e) { e.hashing = true; });

		l.unlock();
		for (; cursor < end; ++cursor)
		{
			// TODO: support the end piece, with smaller block size
			// and torrents with small pieces
			ctx.update({piece_iter->blocks[cursor].buf, default_block_size});
		}

		l.lock();
		view.modify(piece_iter, [&](cached_piece_entry& e) {
			e.hasher_cursor = cursor;
			e.hashing = false;
		});

		if (cursor == piece_iter->blocks_in_piece && piece_iter->hash_job)
		{
			pread_disk_job* j = nullptr;
			view.modify(piece_iter, [&](cached_piece_entry& e) {
				j = std::exchange(e.hash_job, nullptr);
			});
			// we've hashed all blocks, and there's a hash job associated with
			// this piece, post it.
			sha1_hash const piece_hash = ctx.final();
			std::get<job::hash>(j->action).piece_hash = piece_hash;
			completed_jobs.push_back(j);
		}
		else
		{
			// if some other thread added the next block, keep going
			if (piece_iter->blocks[cursor].buf)
				goto keep_going;
		}
	}

	// this should be called by a disk thread
	// the callback should return the number of blocks it successfully flushed
	// to disk
	void flush_to_disk(std::function<int(span<cached_block_entry>)> f
		, int const target_blocks
		, std::function<void(jobqueue_t, pread_disk_job*)> clear_piece_fun)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		INVARIANT_CHECK;

		// TODO: refactor this to avoid so much duplicated code

		// first we look for pieces that are ready to be flushed and should be
		auto& view = m_pieces.template get<2>();
		for (auto piece_iter = view.begin(); piece_iter != view.end();)
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			INVARIANT_CHECK;
#endif

			// We avoid flushing if other threads have already initiated sufficient
			// amount of flushing
			if (m_blocks - m_flushing_blocks <= target_blocks)
				return;

			if (piece_iter->flushing)
			{
				++piece_iter;
				continue;
			}

			if (!piece_iter->ready_to_flush)
				break;

			view.modify(piece_iter, [](cached_piece_entry& e) { e.flushing = true; });
			int const num_blocks = piece_iter->blocks_in_piece;
			m_flushing_blocks += num_blocks;

			// we have to release the lock while flushing, but since we set the
			// "flushing" member to true, this piece is pinned to the cache
			l.unlock();
			span<cached_block_entry> const blocks(piece_iter->blocks.get()
				, piece_iter->blocks_in_piece);

			int count = 0;
			{
				auto se = scope_end([&] {
					l.lock();
					view.modify(piece_iter, [](cached_piece_entry& e) { e.flushing = false; });
					TORRENT_ASSERT(m_flushing_blocks >= num_blocks);
					m_flushing_blocks -= num_blocks;
				});
				if (!blocks.empty())
					count = f(blocks);
			}
			TORRENT_ASSERT(m_blocks >= count);
			m_blocks -= count;
			for (auto& be : blocks.first(count))
			{
				TORRENT_ASSERT(be.write_job);
				TORRENT_ASSERT(!be.buf_holder);
				be.buf_holder = std::move(std::get<job::write>(be.write_job->action).buf);
				be.write_job = nullptr;
			}
			if (piece_iter->clear_piece)
			{
				jobqueue_t aborted;
				auto& cpe = const_cast<cached_piece_entry&>(*piece_iter);
				clear_piece_impl(cpe, aborted);
				clear_piece_fun(std::move(aborted), std::exchange(cpe.clear_piece, nullptr));
				return;
			}
			if (count < piece_iter->blocks_in_piece)
				return;

			if (piece_iter->piece_hash_returned)
				piece_iter = view.erase(piece_iter);
			else
				++piece_iter;
		}

		// if we get here, we have to "force flush" some blocks even though we
		// don't have all the blocks yet. Start by flushing pieces that have the
		// most contiguous blocks to flush:
		auto& view2 = m_pieces.template get<1>();
		for (auto piece_iter = view2.begin(); piece_iter != view2.end(); ++piece_iter)
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			INVARIANT_CHECK;
#endif

			// We avoid flushing if other threads have already initiated sufficient
			// amount of flushing
			if (m_blocks - m_flushing_blocks <= target_blocks)
				return;

			if (piece_iter->flushing)
				continue;

			// the pieces are ordered by hasher_cursor, decreasing order. If we
			// encounter a 0, all the remaining ones will also be zero
			if (piece_iter->hasher_cursor == 0)
				break;

			view2.modify(piece_iter, [](cached_piece_entry& e) { e.flushing = true; });

			int const num_blocks = piece_iter->hasher_cursor;
			m_flushing_blocks += num_blocks;

			// we have to release the lock while flushing, but since we set the
			// "flushing" member to true, this piece is pinned to the cache
			l.unlock();
			span<cached_block_entry> const blocks(piece_iter->blocks.get(), num_blocks);

			int count = 0;
			{
				auto se = scope_end([&] {
					l.lock();
					view2.modify(piece_iter, [](cached_piece_entry& e) { e.flushing = false; });
					TORRENT_ASSERT(m_flushing_blocks >= num_blocks);
					m_flushing_blocks -= num_blocks;
				});
				if (!blocks.empty())
					count = f(blocks);

			}
			TORRENT_ASSERT(m_blocks >= count);
			m_blocks -= count;
			for (auto& be : blocks.first(count))
			{
				TORRENT_ASSERT(be.write_job);
				TORRENT_ASSERT(!be.buf_holder);
				be.buf_holder = std::move(std::get<job::write>(be.write_job->action).buf);
				be.write_job = nullptr;
			}
			if (piece_iter->clear_piece)
			{
				jobqueue_t aborted;
				auto& cpe = const_cast<cached_piece_entry&>(*piece_iter);
				clear_piece_impl(cpe, aborted);
				clear_piece_fun(std::move(aborted), std::exchange(cpe.clear_piece, nullptr));
				return;
			}
			if (count < blocks.size())
				return;
		}

		// we may still need to flush blocks at this point, even though we
		// would require read-back later to compute the piece hash
		auto& view3 = m_pieces.template get<0>();
		for (auto piece_iter = view3.begin(); piece_iter != view3.end(); ++piece_iter)
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			INVARIANT_CHECK;
#endif

			// We avoid flushing if other threads have already initiated sufficient
			// amount of flushing
			if (m_blocks - m_flushing_blocks <= target_blocks)
				return;

			if (piece_iter->flushing)
				continue;

			view3.modify(piece_iter, [](cached_piece_entry& e) { e.flushing = true; });

			TORRENT_ALLOCA(blocks, cached_block_entry, piece_iter->blocks_in_piece);

			int num_blocks = 0;
			for (int blk = 0; blk < piece_iter->blocks_in_piece; ++blk)
			{
				auto const& cbe = piece_iter->blocks[blk];
				if (cbe.write_job == nullptr) continue;
				blocks[num_blocks].write_job = cbe.write_job;
				++num_blocks;
			}
			blocks = blocks.first(num_blocks);

			m_flushing_blocks += num_blocks;
			// we have to release the lock while flushing, but since we set the
			// "flushing" member to true, this piece is pinned to the cache
			l.unlock();

			int count = 0;
			{
				auto se = scope_end([&] {
					l.lock();
					view3.modify(piece_iter, [](cached_piece_entry& e) { e.flushing = false; });
					TORRENT_ASSERT(m_flushing_blocks >= num_blocks);
					m_flushing_blocks -= num_blocks;
				});
				if (!blocks.empty())
					count = f(blocks);
			}
			TORRENT_ASSERT(count <= blocks.size());
			TORRENT_ASSERT(m_blocks >= count);
			m_blocks -= count;

			// make sure to only clear the job pointers for the blocks that were
			// actually flushed, indicated by "count".
			int clear_count = count;
			for (auto& be : span<cached_block_entry>(piece_iter->blocks.get(), num_blocks))
			{
				if (clear_count == 0)
					break;
				if (!be.write_job) continue;
				be.buf_holder = std::move(std::get<job::write>(be.write_job->action).buf);
				be.write_job = nullptr;
				--clear_count;
			}
			if (piece_iter->clear_piece)
			{
				jobqueue_t aborted;
				auto& cpe = const_cast<cached_piece_entry&>(*piece_iter);
				clear_piece_impl(cpe, aborted);
				clear_piece_fun(std::move(aborted), std::exchange(cpe.clear_piece, nullptr));
				return;
			}
			if (count < blocks.size())
				return;
		}
	}

	std::size_t size() const
	{
		std::unique_lock<std::mutex> l(m_mutex);
		INVARIANT_CHECK;
		return m_blocks;
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void check_invariant() const
	{
		// mutex must be held by caller
		int dirty_blocks = 0;
		int clean_blocks = 0;
		int flushing_blocks = 0;

		auto& view = m_pieces.template get<2>();
		for (auto const& piece_entry : view)
		{
			int const num_blocks = piece_entry.blocks_in_piece;

			if (piece_entry.flushing)
				flushing_blocks += num_blocks;

			span<cached_block_entry> const blocks(piece_entry.blocks.get()
				, num_blocks);

			for (auto& be : blocks)
			{
				if (be.write_job) ++dirty_blocks;
				if (be.buf_holder) ++clean_blocks;
			}
		}
		TORRENT_ASSERT(dirty_blocks == m_blocks);
		TORRENT_ASSERT(m_flushing_blocks <= flushing_blocks);
	}
#endif

private:

	// this requires the mutex to be locked
	void clear_piece_impl(cached_piece_entry& cpe, jobqueue_t& aborted)
	{
		TORRENT_ASSERT(!cpe.flushing);
		TORRENT_ASSERT(!cpe.hashing);
		for (int idx = 0; idx < cpe.blocks_in_piece; ++idx)
		{
			auto& cbe = cpe.blocks[idx];
			if (cbe.write_job)
			{
				aborted.push_back(cbe.write_job);
				cbe.write_job = nullptr;
				--m_blocks;
			}
			cbe.buf = nullptr;
			cbe.buf_holder.reset();
		}
	}

	mutable std::mutex m_mutex;
	piece_container m_pieces;

	// the number of *dirty* blocks in the cache. i.e. blocks that need to be
	// flushed to disk. The cache may (briefly) hold more buffers than this
	// while finishing hashing blocks.
	int m_blocks = 0;

	// the number of blocks currently being flushed by a disk thread
	// we use this to avoid over-shooting flushing blocks
	int m_flushing_blocks = 0;
};

}
}

#endif

