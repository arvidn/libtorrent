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
#include "libtorrent/aux_/pread_disk_job.hpp"
#include "libtorrent/aux_/pread_storage.hpp"
#include "libtorrent/aux_/disk_io_thread_pool.hpp" // for jobqueue_t
#include "libtorrent/aux_/unique_ptr.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/hasher.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/functional/hash.hpp>

#define BOOST_BIND_NO_PLACEHOLDERS

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"


namespace libtorrent::aux {

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

inline size_t hash_value(piece_location const& l)
{
	std::size_t ret = 0;
	boost::hash_combine(ret, std::hash<storage_index_t>{}(l.torrent));
	boost::hash_combine(ret, std::hash<piece_index_t>{}(l.piece));
	return ret;
}

struct piece_hasher
{
	piece_hasher() : ph(hasher{}) {}

	sha1_hash final_hash();
	void update(span<char const> const buf);
	lt::hasher& ctx();

private:
	std::variant<hasher, sha1_hash> ph;
};

struct cached_block_entry
{
	// returns the buffer associated with this block. It either picks it from
	// the write job that's hung on this block, or from the buffer in the block
	// object, if it has been flushed to disk already.
	// If there is no buffer, it returns an empty span.
	span<char const> buf() const;

	// returns the buffer associated with the write job hanging on this block.
	// If there is no write job, it returns an empty span.
	span<char const> write_buf() const;

	// once the write job has been executed, and we've flushed the buffer, we
	// move it into buf_holder, to keep the buffer alive until any hash job has
	// completed as well. The underlying data can be accessed through buf, but
	// the owner moves from the pread_disk_job object to this buf_holder.
	// TODO: save space by just storing the buffer pointer here. The
	// cached_piece_entry could hold the pointer to the buffer pool to be able
	// to free these on destruction
	// we would still need to save the *size* of the block, to support the
	// shorter last block of a torrent
	disk_buffer_holder buf_holder;
	pread_disk_job* write_job = nullptr;

	bool flushed_to_disk = false;

	// TODO: only allocate this field for v2 torrents
	sha256_hash block_hash;
};

struct cached_piece_entry
{
	cached_piece_entry(piece_location const& loc
		, int const num_blocks
		, int const piece_size_v2
		, bool v1
		, bool v2);

	span<cached_block_entry> get_blocks() const;

	piece_location piece;

	// this is set to true when the piece has been populated with all blocks
	// it will make it prioritized for flushing to disk
	// it will be cleared once all blocks have been flushed
	bool ready_to_flush = false;

	// when this is true, there is a thread currently hashing blocks and
	// updating the hash context in "ph". Other threads may not touch "ph",
	// "hasing_cursor", and may only read "hasing".
	bool hashing = false;

	// when a thread is writing this piece to disk, this is true. Only one
	// thread at a time should be flushing a piece to disk.
	bool flushing = false;

	// this is set to true if the piece hash has been computed and returned
	// to the bittorrent engine.
	bool piece_hash_returned = false;

	// this indicates that this piece belongs to a v2 torrent, and it has the
	// block_hash member of cached_block_entry and we need to compute the block
	// hashes as well
	bool v1_hashes = false;
	bool v2_hashes = false;

	// if this is a v2 torrent, this is the exact size of this piece. The
	// end-piece of each file may be truncated for v2 torrents
	int piece_size2;

	int blocks_in_piece = 0;

	// the number of blocks that have been hashed so far. Specifically for the
	// v1 SHA1 hash of the piece, so all blocks are contiguous starting at block
	// 0.
	int hasher_cursor = 0;

	// the number of contiguous blocks, starting at 0, that have been flushed to
	// disk so far. This is used to determine how many blocks are left to flush
	// from this piece without requiring read-back to hash them, by substracting
	// flushed_cursor from hasher_cursor.
	int flushed_cursor = 0;

	// the number of blocks that have a write job associated with them
	int num_jobs = 0;

	// returns the number of blocks in this piece that have been hashed and
	// ready to be flushed without requiring reading them back in the future.
	int cheap_to_flush() const
	{
		return int(hasher_cursor) - int(flushed_cursor);
	}

	unique_ptr<cached_block_entry[]> blocks;

	piece_hasher ph;

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
		// look up ranges of pieces by (torrent, piece-index)
		mi::ordered_unique<mi::member<cached_piece_entry, piece_location, &cached_piece_entry::piece>>,
		// ordered by the number of contiguous blocks we can flush without
		// read-back. large numbers are ordered first
		mi::ordered_non_unique<mi::const_mem_fun<cached_piece_entry, int, &cached_piece_entry::cheap_to_flush>, std::greater<void>>,
		// ordered by whether the piece is ready to be flushed or not
		// true is ordered before false
		mi::ordered_non_unique<mi::member<cached_piece_entry, bool, &cached_piece_entry::ready_to_flush>, std::greater<void>>,
		// hash-table lookup of individual pieces. faster than index 0
		mi::hashed_unique<mi::member<cached_piece_entry, piece_location, &cached_piece_entry::piece>>
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

		if (i->blocks[block_idx].buf().data())
		{
			// TODO: it would be nice if this could be called without holding
			// the mutex. It would require being able to lock the piece
			f(i->blocks[block_idx].buf());
			return true;
		}
		return false;
	}

	template <typename Fun>
	sha256_hash hash2(piece_location const loc, int const block_idx, Fun f) const
	{
		std::unique_lock<std::mutex> l(m_mutex);

		INVARIANT_CHECK;

		auto& view = m_pieces.template get<0>();
		auto i = view.find(loc);
		if (i != view.end())
		{
			if (i->hashing)
			{
				// TODO: it would probably be more efficient to wait here.
				// #error we should hang the hash job onto the piece. If there is a
				// job already, form a queue
				l.unlock();
				return f();
			}
			auto const& cbe = i->blocks[block_idx];
			// There's nothing stopping the hash threads from hashing the blocks in
			// parallel. This should not depend on the hasher_cursor. That's a v1
			// concept
			if (i->hasher_cursor > block_idx)
				return cbe.block_hash;
			if (cbe.buf().data())
			{
				hasher256 h;
				h.update(cbe.buf());
				return h.final();
			}
		}
		l.unlock();
		return f();
	}

	// returns false if the piece is not in the cache
	template <typename Fun>
	bool hash_piece(piece_location const loc, Fun f)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		INVARIANT_CHECK;

		auto& view = m_pieces.template get<0>();
		auto piece_iter = view.find(loc);
		if (piece_iter == view.end()) return false;

		TORRENT_ALLOCA(blocks, char const*, piece_iter->blocks_in_piece);
		TORRENT_ALLOCA(v2_hashes, sha256_hash, piece_iter->blocks_in_piece);

		for (int i = 0; i < piece_iter->blocks_in_piece; ++i)
		{
			blocks[i] = piece_iter->blocks[i].buf().data();
			v2_hashes[i] = piece_iter->blocks[i].block_hash;
		}

		view.modify(piece_iter, [](cached_piece_entry& e) { e.hashing = true; });
		int const hasher_cursor = piece_iter->hasher_cursor;
		l.unlock();

		auto se = scope_end([&] {
			l.lock();
			view.modify(piece_iter, [&](cached_piece_entry& e) {
				e.hashing = false;
			});
		});
		f(const_cast<piece_hasher&>(piece_iter->ph), hasher_cursor, blocks, v2_hashes);
		return true;
	}

	// If the specified piece exists in the cache, and it's unlocked, clear all
	// write jobs (return them in "aborted"). Returns true if the clear_piece
	// job should be posted as complete. Returns false if the piece is locked by
	// another thread, and the clear_piece job has been queued to be issued once
	// the piece is unlocked.
	bool try_clear_piece(piece_location const loc, pread_disk_job* j, jobqueue_t& aborted);

	template <typename Fun>
	int get2(piece_location const loc, int const block_idx, Fun f) const
	{
		std::unique_lock<std::mutex> l(m_mutex);

		INVARIANT_CHECK;

		auto& view = m_pieces.template get<0>();
		auto i = view.find(loc);
		if (i == view.end()) return 0;

		char const* buf1 = i->blocks[block_idx].buf().data();
		char const* buf2 = i->blocks[block_idx + 1].buf().data();

		if (buf1 == nullptr && buf2 == nullptr)
			return 0;

		return f(buf1, buf2);
	}

	// returns true if this piece needs to have its hasher kicked
	bool insert(piece_location const loc
		, int const block_idx
		, pread_disk_job* write_job);

	enum hash_result: std::uint8_t
	{
		job_completed,
		job_queued,
		post_job,
	};

	hash_result try_hash_piece(piece_location const loc, pread_disk_job* hash_job);

	// this should be called from a hasher thread
	void kick_hasher(piece_location const& loc, jobqueue_t& completed_jobs);

	// this should be called by a disk thread
	// the callback should return the number of blocks it successfully flushed
	// to disk
	void flush_to_disk(std::function<int(bitfield&, span<cached_block_entry const>, int)> f
		, int const target_blocks
		, std::function<void(jobqueue_t, pread_disk_job*)> clear_piece_fun);

	void flush_storage(std::function<int(bitfield&, span<cached_block_entry const>, int)> f
		, storage_index_t const storage
		, std::function<void(jobqueue_t, pread_disk_job*)> clear_piece_fun);

	std::size_t size() const;
	std::size_t num_flushing() const;

#if TORRENT_USE_INVARIANT_CHECKS
	void check_invariant() const;
#endif

private:

	// this requires the mutex to be locked
	void clear_piece_impl(cached_piece_entry& cpe, jobqueue_t& aborted);

	template <typename Iter, typename View>
	Iter flush_piece_impl(View& view
		, Iter piece_iter
		, std::function<int(bitfield&, span<cached_block_entry const>, int)> const& f
		, std::unique_lock<std::mutex>& l
		, int const num_blocks
		, span<cached_block_entry> const blocks
		, std::function<void(jobqueue_t, pread_disk_job*)> clear_piece_fun);

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

#endif

