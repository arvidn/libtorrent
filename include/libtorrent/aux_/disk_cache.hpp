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
#include <thread>
#include <algorithm>

#include "libtorrent/storage_defs.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/aux_/scope_end.hpp"
#include "libtorrent/aux_/alloca.hpp"
#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/aux_/disk_job.hpp"
#include "libtorrent/aux_/disk_io_thread_pool.hpp" // for jobqueue_t
#include "libtorrent/aux_/unique_ptr.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/aux_/back_pressure.hpp"
#include "libtorrent/flags.hpp"

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

namespace libtorrent {
	struct settings_interface;
	struct disk_observer;
}

namespace libtorrent::aux {

namespace mi = boost::multi_index;

using cached_piece_flags = libtorrent::flags::bitfield_flag<std::uint8_t, struct cached_piece_flags_tag>;

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

struct TORRENT_EXTRA_EXPORT cached_block_entry
{
	// the three mutually exclusive states a block can be in:
	//   monostate        - no data present
	//   disk_job*        - pending write; buffer is owned by the job
	//   disk_buffer_ref  - write was attempted; buffer kept for the hasher (may be null)
	using write_state_t = std::variant<std::monostate, disk_job*, disk_buffer_ref>;

	// returns the buffer pointer for this block, regardless of whether it comes
	// from the write job or the owned buffer. Returns nullptr if no data is available.
	char const* data() const noexcept;

	// returns a span covering the buffer for this block. block_size must be the
	// number of bytes in this specific block (may be less than default_block_size
	// for the last block of a piece). For write_job blocks the job's own
	// buffer_size is used instead, so block_size only affects the owned-buffer case.
	// If there is no buffer, it returns an empty span.
	span<char const> buf(int block_size) const;

	// returns the buffer associated with the write job hanging on this block.
	// If there is no write job, it returns an empty span.
	span<char const> write_buf() const;

	// returns true if write_state is disk_buffer_ref and the buffer is non-null
	bool has_buf() const noexcept;
	// returns true if write_state is disk_buffer_ref (write was attempted), regardless
	// of whether the buffer is still held (may be a null/freed ref)
	bool is_flushed() const noexcept;
	// returns the write job pointer, or nullptr if not in write-job state
	disk_job* get_write_job() const noexcept;
	// moves the buffer out and transitions to disk_buffer_ref{null}; requires has_buf()
	disk_buffer_ref take_buf() noexcept;
	// returns the write job and sets write_state to monostate; requires get_write_job() != nullptr
	disk_job* take_write_job() noexcept;

	write_state_t write_state;
};

struct cached_piece_entry
{
	cached_piece_entry(piece_location const& loc
		, int const piece_size_v2
		, int const piece_size
		, int const num_blocks
		, bool v1
		, bool v2);

	span<cached_block_entry> get_blocks() const;

	piece_location piece;

	unique_ptr<cached_block_entry[], int> blocks;
	// allocated only for v2 torrents (null for v1-only)
	unique_ptr<sha256_hash[], int> block_hashes;
	// allocated only for v1 torrents (null for v2-only)
	std::unique_ptr<piece_hasher> ph;

	// if there is a hash_job set on this piece, whenever we complete hashing
	// the last block, we should post this
	disk_job* hash_job = nullptr;

	// if the piece has been requested to be cleared, but it was locked
	// (flushing) at the time. We hang this job here to complete it once the
	// thread currently flushing is done with it
	disk_job* clear_piece = nullptr;
	// the exact v2 size of this piece (respecting file boundaries).
	// Used for bytes_left computation in kick_hasher's v2 path.
	int piece_size2;

	// the effective piece size for computing per-block sizes when hashing
	// (max of v1 and v2 sizes, i.e. the v1 piece_size when v1 is active,
	// or piece_size2 for v2-only pieces).
	int piece_size;

	// the number of blocks in this piece, derived from piece_size.
	int blocks_in_piece() const
	{
		return (piece_size + default_block_size - 1) / default_block_size;
	}

	// the number of blocks that have been hashed so far. Specifically for the
	// v1 SHA1 hash of the piece, so all blocks are contiguous starting at block
	// 0.
	std::uint16_t hasher_cursor = 0;

	// the number of contiguous blocks, starting at 0, that have been flushed to
	// disk so far. This is used to determine how many blocks are left to flush
	// from this piece without requiring read-back to hash them, by substracting
	// flushed_cursor from hasher_cursor.
	std::uint16_t flushed_cursor = 0;

	// the number of blocks that have a write job associated with them
	std::uint16_t num_jobs = 0;

	// this is set when the piece has been populated with all blocks;
	// it will make it prioritized for flushing to disk.
	// it will be cleared once all blocks have been flushed.
	static constexpr cached_piece_flags force_flush_flag = 0_bit;

	// when set, there is a thread currently hashing blocks and updating the
	// hash context in "ph". Other threads may not touch "ph",
	// "hasher_cursor", and may only read this flag.
	static constexpr cached_piece_flags hashing_flag = 1_bit;

	// when a thread is writing this piece to disk, this is set. Only one
	// thread at a time should be flushing a piece to disk.
	static constexpr cached_piece_flags flushing_flag = 2_bit;

	// set if the piece hash has been computed and returned to the bittorrent
	// engine.
	static constexpr cached_piece_flags piece_hash_returned_flag = 3_bit;

	// set if this piece requires v1 SHA1 hashing (ph member is allocated).
	static constexpr cached_piece_flags v1_hashes_flag = 4_bit;

	// set if this piece requires v2 SHA256 block hashing (block_hashes
	// member is allocated).
	static constexpr cached_piece_flags v2_hashes_flag = 5_bit;

	// set when a block is inserted into this piece and the hasher thread
	// should be woken to make hashing progress. Cleared when the hasher
	// thread picks it up. Used to coalesce multiple insertions into a
	// single wakeup.
	static constexpr cached_piece_flags needs_hasher_kick_flag = 6_bit;

	// flags are protected by the main disk cache mutex and may only be
	// accessed while holding it
	cached_piece_flags flags{};

	// returns the number of blocks in this piece that have been hashed and
	// ready to be flushed without requiring reading them back in the future.
	int cheap_to_flush() const
	{
		return int(hasher_cursor) - int(flushed_cursor);
	}

	bool need_force_flush() const
	{
		return bool(flags & force_flush_flag);
	}

	bool needs_hasher_kick() const
	{
		return bool(flags & needs_hasher_kick_flag);
	}
};

using insert_result_flags = libtorrent::flags::bitfield_flag<std::uint8_t, struct insert_result_flags_tag>;

struct TORRENT_EXTRA_EXPORT disk_cache
{
	disk_cache(io_context& ios);

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
		mi::ordered_non_unique<mi::const_mem_fun<cached_piece_entry, bool, &cached_piece_entry::need_force_flush>, std::greater<void>>,
		// hash-table lookup of individual pieces. faster than index 0
		mi::hashed_unique<mi::member<cached_piece_entry, piece_location, &cached_piece_entry::piece>>,
		// ordered by whether the piece needs its hasher kicked.
		// true is ordered before false, so pieces needing a kick come first.
		mi::ordered_non_unique<mi::const_mem_fun<cached_piece_entry, bool, &cached_piece_entry::needs_hasher_kick>, std::greater<void>>
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

		if (i->blocks[block_idx].data())
		{
			// TODO: it would be nice if this could be called without holding
			// the mutex. It would require being able to lock the piece
			int const blk_size = std::min(default_block_size
				, i->piece_size - block_idx * default_block_size);
			f(i->blocks[block_idx].buf(blk_size));
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
		if (i == view.end())
		{
			l.unlock();
			return f();
		}

		// Another thread may be hashing this piece right now. In this case we
		// need to wait
		// TODO: there may be a better way to solve this, maybe hang the job on
		// the piece and try later. But it's a big change to do so
		while (i->flags & cached_piece_entry::hashing_flag)
		{
			l.unlock();
			std::this_thread::yield();
			l.lock();

			i = view.find(loc);
			if (i == view.end())
			{
				l.unlock();
				return f();
			}
		}
		auto const& cbe = i->blocks[block_idx];
		// There's nothing stopping the hash threads from hashing the blocks in
		// parallel. This should not depend on the hasher_cursor. That's a v1
		// concept
		TORRENT_ASSERT(i->block_hashes);
		if (!i->block_hashes[block_idx].is_all_zeros())
			return i->block_hashes[block_idx];
		if (cbe.data())
		{
			int const blk_size = std::min(default_block_size
				, i->piece_size2 - block_idx * default_block_size);
			hasher256 h;
			h.update(cbe.buf(blk_size));
			return h.final();
		}
		l.unlock();
		return f();
	}

	enum class hash_piece_result { not_in_cache, completed, deferred };

	// Looks up the piece in the cache and calls f() to complete its hash computation.
	// Returns not_in_cache if the piece is not in the cache; the caller must then read
	// all blocks from disk itself.
	// Returns deferred if a hasher thread is currently processing this piece; the job
	// j has been hung on the piece and kick_hasher will retry it when
	// done. The caller must NOT access j after receiving deferred.
	// Returns completed when f() has been called and hashing is done.
	//
	// When the piece is found, hash_piece() takes a snapshot of the cache state under
	// the mutex, sets hashing=true (which pins all buffers at index >= hasher_cursor
	// so flush_piece_impl cannot free them), then releases the lock and calls:
	//
	//   f(ph, hasher_cursor, blocks, v2_hashes)
	//
	//   ph: SHA1 piece hasher already fed blocks [0, hasher_cursor).
	//   hasher_cursor: index of the first block not yet incorporated in ph.
	//     f() must continue feeding blocks from this index onward.
	//   blocks: per-block buffer pointers. A null entry means the block is not
	//     in the cache and must be read from disk.
	//   v2_hashes: per-block SHA256 hash snapshots. all-zeros means not-yet-computed.
	//     a non-zero entry is a pre-computed hash that f() may use directly without re-hashing.
	//
	// After f() returns hasher_cursor is advanced and buffers that have
	// already been flushed to disk are freed. If all blocks are flushed, the
	// piece entry is removed from the cache entirely.
	template <typename Fun>
	hash_piece_result hash_piece(piece_location const loc, disk_job* j, Fun f)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		INVARIANT_CHECK;

		auto& view = m_pieces.template get<0>();
		auto piece_iter = view.find(loc);
		if (piece_iter == view.end()) return hash_piece_result::not_in_cache;

		// If a hasher thread is currently processing this piece, hang the job
		// on the piece. kick_hasher will retry it when done.
		if (piece_iter->flags & cached_piece_entry::hashing_flag)
		{
			// if this assert fires, the bittorrent layer may have called
			// async_hash() more than once for the same piece. It's not
			// supposed to do that.
			TORRENT_ASSERT(piece_iter->hash_job == nullptr);
			view.modify(piece_iter, [j](cached_piece_entry& e) { e.hash_job = j; });
			return hash_piece_result::deferred;
		}

		int const blocks_in_piece = piece_iter->blocks_in_piece();
		std::uint16_t const hasher_cursor = piece_iter->hasher_cursor;

		TORRENT_ALLOCA(blocks, char const*, blocks_in_piece);
		TORRENT_ALLOCA(v2_hashes, sha256_hash, (piece_iter->flags & cached_piece_entry::v2_hashes_flag) ? blocks_in_piece : 0);

		int num_unhashed = 0;
		for (int i = 0; i < blocks_in_piece; ++i)
		{
			char const* buf = piece_iter->blocks[i].data();
			blocks[i] = buf;
			if (buf && i >= hasher_cursor)
				++num_unhashed;
		}
		if (piece_iter->block_hashes)
		{
			for (int i = 0; i < blocks_in_piece; ++i)
				v2_hashes[i] = piece_iter->block_hashes[i];
		}

		view.modify(piece_iter, [](cached_piece_entry& e) { e.flags |= cached_piece_entry::hashing_flag; });
		l.unlock();

		auto se = scope_end([&] {
			l.lock();
			view.modify(piece_iter, [&](cached_piece_entry& e) {
				e.flags |= cached_piece_entry::force_flush_flag | cached_piece_entry::piece_hash_returned_flag;
				e.flags &= ~cached_piece_entry::hashing_flag;
				e.hasher_cursor = static_cast<std::uint16_t>(blocks_in_piece);
			});
			TORRENT_ASSERT(m_num_unhashed >= num_unhashed);
			m_num_unhashed -= num_unhashed;
			{
				TORRENT_ASSERT(m_allocator);
				bulk_free_buffer to_free(*m_allocator);
				for (auto& cbe : piece_iter->get_blocks().subspan(0, piece_iter->flushed_cursor))
				{
					if (cbe.has_buf())
					{
						to_free.add(cbe.take_buf());
						TORRENT_ASSERT(m_blocks > 0);
						--m_blocks;
					}
				}
			}
			if (piece_iter->flushed_cursor == piece_iter->blocks_in_piece())
			{
				free_piece(*piece_iter);
				view.erase(piece_iter);
			}
		});
		f(piece_iter->ph.get(), hasher_cursor, blocks, v2_hashes);
		return hash_piece_result::completed;
	}

	// If the specified piece exists in the cache, and it's unlocked, clear all
	// write jobs (return them in "aborted"). Returns true if the clear_piece
	// job should be posted as complete. Returns false if the piece is locked by
	// another thread, and the clear_piece job has been queued to be issued once
	// the piece is unlocked.
	bool try_clear_piece(piece_location const loc, disk_job* j, jobqueue_t& aborted);

	template <typename Fun>
	int get2(piece_location const loc, int const block_idx, Fun f) const
	{
		std::unique_lock<std::mutex> l(m_mutex);

		INVARIANT_CHECK;

		auto& view = m_pieces.template get<0>();
		auto i = view.find(loc);
		if (i == view.end()) return 0;

		char const* buf1 = i->blocks[block_idx].data();
		char const* buf2 = i->blocks[block_idx + 1].data();

		if (buf1 == nullptr && buf2 == nullptr)
			return 0;

		return f(buf1, buf2);
	}

	// the piece this block was inserted into needs its piece hasher kicked. This
	// block unblocked the hasher.
	static constexpr insert_result_flags need_hasher_kick = 0_bit;

	// the disk cache is full and the peer needs back-pressure applied to it.
	static constexpr insert_result_flags exceeded_limit = 1_bit;

	// piece metadata required when inserting the first block of a new piece.
	struct piece_entry_params
	{
		// size (in bytes) of this piece according to v2 file boundaries
		// (only meaningful for v2 torrents)
		int piece_size2;
		// effective piece size for per-block size computation: v1 piece_size
		// when v1 is active, piece_size2 for v2-only pieces
		int piece_size;
		bool v1; // piece requires SHA-1 hashing
		bool v2; // piece requires per-block SHA-256 hashing
	};

	// the return value indicates whether the piece needs its hasher kicked or
	// whether the cache is full and the peer needs to stop downloading until
	// we've flushed below the low watermark
	insert_result_flags insert(piece_location loc
		, int block_idx
		, bool force_flush
		, std::shared_ptr<disk_observer> o
		, disk_job* write_job
		, piece_entry_params const& params);

	void set_max_size(int max_size);

	enum hash_result: std::uint8_t
	{
		job_completed,
		job_queued,
		post_job,
	};

	hash_result try_hash_piece(piece_location loc, disk_job* hash_job);

	// Called from a hasher thread when woken by interrupt. Processes all
	// pieces marked with needs_hasher_kick_flag, calling kick_hasher on each.
	// Returns true if force_flush_flag was set on any piece, meaning a generic
	// thread should be woken to flush those pieces to disk.
	// Jobs hung on a piece that stops hashing early are extracted and placed in
	// retry_jobs so the caller can re-submit them.
	bool kick_pending_hashers(jobqueue_t& completed_jobs, jobqueue_t& retry_jobs);

	// this should be called by a disk thread
	// the callback should return the number of blocks it successfully flushed
	// to disk
	void flush_to_disk(std::function<int(bitfield&, span<cached_block_entry const>)> f
		, int target_blocks
		, std::function<void(jobqueue_t, disk_job*)> clear_piece_fun
		, bool optimistic = false);

	void flush_storage(std::function<int(bitfield&, span<cached_block_entry const>)> f
		, storage_index_t storage
		, std::function<void(jobqueue_t, disk_job*)> clear_piece_fun);

	std::size_t size() const;
	std::size_t num_flushing() const;
	std::tuple<std::int64_t, std::int64_t> stats() const;


	std::optional<int> flush_request() const;

#if TORRENT_USE_INVARIANT_CHECKS
	void check_invariant() const;
#endif

private:

	// this should be called from a hasher thread, with m_mutex held.
	// hashing_flag must already be set on the piece by the caller.
	// Returns true if force_flush_flag was set on the piece.
	// Jobs hung on a piece that stops hashing early are placed in retry_jobs.
	bool kick_hasher(piece_container::nth_index<4>::type::iterator piece_iter
		, std::unique_lock<std::mutex>& l, jobqueue_t& completed_jobs
		, jobqueue_t& retry_jobs);

	void free_piece(cached_piece_entry const& cpe);

	// this requires the mutex to be locked
	void clear_piece_impl(cached_piece_entry& cpe, jobqueue_t& aborted);

	template <typename Iter, typename View>
	Iter flush_piece_impl(View& view
		, Iter piece_iter
		, std::function<int(bitfield&, span<cached_block_entry const>)> const& f
		, std::unique_lock<std::mutex>& l
		, span<cached_block_entry> const blocks
		, std::function<void(jobqueue_t, disk_job*)> clear_piece_fun);

	mutable std::mutex m_mutex;
	piece_container m_pieces;

	// allocator used for all disk buffers in this cache. Set lazily on the
	// first insert() call. All blocks share the same allocator.
	buffer_allocator_interface* m_allocator = nullptr;

	// the number of *dirty* blocks in the cache. i.e. blocks that need to be
	// flushed to disk. The cache may (briefly) hold more buffers than this
	// while finishing hashing blocks.
	int m_blocks = 0;

	// the number of blocks currently being flushed by a disk thread
	// we use this to avoid over-shooting flushing blocks
	int m_flushing_blocks = 0;

	// the number of blocks in the cache that have not yet been passed throught
	// the piece hasher. i.e. where the hasher_cursor is <= the block index
	int m_num_unhashed = 0;

	// record disk_observers that we've signalled back-pressure to. Once the
	// cache size drop below the low watermark, we'll signal them they can resume
	back_pressure m_back_pressure;
};

}

#endif

