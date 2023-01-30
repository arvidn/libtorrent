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
	pread_disk_job* write_job;
};

struct cached_piece_entry
{
	cached_piece_entry::cached_piece_entry(piece_location const& loc, int const num_blocks)
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

	std::variant<sha1_hash, hasher> ph;
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
		mi::ordered_non_unique<mi::member<cached_piece_entry, int, &cached_piece_entry::hasher_cursor>>,
		// ordered by whether the piece is ready to be flushed or not
		mi::ordered_non_unique<mi::member<cached_piece_entry, bool, &cached_piece_entry::ready_to_flush>>
		>
	>;
/*
	template <typename Fun>
	bool get(piece_location const loc, Fun f) const
	{
		std::unique_lock<std::mutex> l(m_mutex);
		auto const it = m_store_buffer.find(loc);
		if (it != m_store_buffer.end())
		{
			// TODO: it would be nice if this could be called without holding
			// the mutex. It would require a reference counter on the store
			// buffer entries and that we potentially erases it after this call.
			// it would also require the store buffer being able to take over
			// ownership of the buffer when the owner erases it. Perhase erase()
			// could be made to take a buffer_holder, which is held onto if the
			// refcount > 0
			f(it->second);
			return true;
		}
		return false;
	}

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
		std::lock_guard<std::mutex> l(m_mutex);

		auto& view = m_pieces.template get<0>();
		auto i = view.find(loc);
		if (i == view.end())
		{
			int const blocks_in_piece = (write_job->storage->files().piece_size(loc.piece) + default_block_size - 1) / default_block_size;
			cached_piece_entry pe(loc, blocks_in_piece);
			i = m_pieces.insert(std::move(pe)).first;
		}

		//TORRENT_ASSERT(i->blocks[block_idx].buffer == nullptr);
		TORRENT_ASSERT(i->blocks[block_idx].write_job == nullptr);
		//i->blocks[block_idx].buffer = job->buf.get();
		i->blocks[block_idx].write_job = write_job;
		++m_blocks;
		// TODO: maybe trigger hash job
	}

	// this should be called by a disk thread
	// the callback should return the number of blocks it successfully flushed
	// to disk
	void flush_to_disk(std::function<int(span<cached_block_entry>)> f, int const target_blocks)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		// first we look for pieces that are ready to be flushed and should be
		auto& view = m_pieces.template get<2>();
		for (auto piece_iter = view.rbegin(); piece_iter != view.rend();)
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

			if (!piece_iter->ready_to_flush)
				break;

			view.modify(piece_iter, [](cached_piece_entry& e) { e.flushing = true; });
			m_flushing_blocks += piece_iter->blocks_in_piece;

			// we have to release the lock while flushing, but since we set the
			// "flushing" member to true, this piece is pinned to the cache
			l.unlock();
			span<cached_block_entry> blocks(piece_iter->blocks.get()
				, piece_iter->blocks_in_piece);

			int count = 0;
			try
			{
				count = f(blocks);
			}
			catch (...)
			{
				l.lock();
				view.modify(piece_iter, [](cached_piece_entry& e) { e.flushing = false; });
				throw;
			}
			l.lock();

			view.modify(piece_iter, [](cached_piece_entry& e) { e.flushing = false; });

			TORRENT_ASSERT(m_blocks >= count);
			m_blocks -= count;
			m_flushing_blocks -= piece_iter->blocks_in_piece;
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
		for (auto piece_iter = view2.rbegin(); piece_iter != view2.rend(); ++piece_iter)
		{
			// We avoid flushing if other threads have already initiated sufficient
			// amount of flushing
			if (m_blocks - m_flushing_blocks <= target_blocks)
				return;

			if (piece_iter->flushing)
				continue;

			view2.modify(piece_iter, [](cached_piece_entry& e) { e.flushing = true; });

			m_flushing_blocks += piece_iter->hasher_cursor;

			// we have to release the lock while flushing, but since we set the
			// "flushing" member to true, this piece is pinned to the cache
			l.unlock();
			span<cached_block_entry> blocks(piece_iter->blocks.get()
				, piece_iter->hasher_cursor);

			int count = 0;
			try
			{
				count = f(blocks);
			}
			catch (...)
			{
				l.lock();
				view2.modify(piece_iter, [](cached_piece_entry& e) { e.flushing = false; });
				throw;
			}
			l.lock();

			view2.modify(piece_iter, [](cached_piece_entry& e) { e.flushing = false; });

			TORRENT_ASSERT(m_blocks >= count);
			m_blocks -= count;
			m_flushing_blocks -= blocks.size();
			if (count < blocks.size())
				return;
		}

		// TODO: we may still need to flush blocks at this point, even though we
		// would require read-back later to compute the piece hash
	}

	std::size_t size() const
	{
		return m_blocks;
	}

private:

	mutable std::mutex m_mutex;
	piece_container m_pieces;
	int m_blocks = 0;

	// the number of blocks currently being flushed by a disk thread
	// we use this to avoid over-shooting flushing blocks
	int m_flushing_blocks = 0;
};

}
}

#endif

