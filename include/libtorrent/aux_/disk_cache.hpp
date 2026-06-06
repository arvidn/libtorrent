/*

Copyright (c) 2023, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_CACHE
#define TORRENT_DISK_CACHE

#include <unordered_map>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <algorithm>
#include <vector>

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

	struct pread_storage;


	namespace mi = boost::multi_index;

	using cached_piece_flags =
		libtorrent::flags::bitfield_flag<std::uint16_t, struct cached_piece_flags_tag>;

	// uniquely identifies a torrent and piece
	struct piece_location
	{
		piece_location(storage_index_t const t, piece_index_t const p)
			: torrent(t)
			, piece(p)
		{}
		storage_index_t torrent;
		piece_index_t piece;
		bool operator==(piece_location const& rhs) const
		{
			return std::tie(torrent, piece) == std::tie(rhs.torrent, rhs.piece);
		}

		bool operator<(piece_location const& rhs) const
		{
			return std::tie(torrent, piece) < std::tie(rhs.torrent, rhs.piece);
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

// ADL hook for visit_block_iovecs over span<disk_job* const>. Reads only
// borrowed_buf (not wj.buf) because flush_piece_impl invokes this without
// holding the cache mutex.
inline span<char const> write_buf(disk_job const* j)
{
	if (j == nullptr) return {};
	TORRENT_ASSERT(j->get_type() == aux::job_action_t::write);
	auto const& w = std::get<aux::job::write>(j->action);
	return {w.borrowed_buf, w.borrowed_buf ? w.buffer_size : std::uint16_t{0}};
}

struct TORRENT_EXTRA_EXPORT cached_piece_entry
{
	cached_piece_entry(piece_location const& loc,
		int const piece_size_v2,
		int const piece_size,
		int const num_blocks,
		bool v1);

	span<cached_block_entry> get_blocks() const;

	piece_location piece;

	unique_ptr<cached_block_entry[], int> blocks;
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

	// number of this piece's blocks that are either in m_v2_hash_queue or
	// currently being processed by a drain batch. Decremented by
	// drain_v2_hash_queue once a batch has stored its hashes on
	// pread_storage. While > 0, a clear_piece must wait (try_clear_piece
	// parks it on clear_piece) and a hash_job hung via wait_for_v2_queue or
	// try_hash_piece's deferred path cannot post yet. drain_v2_hash_queue
	// dispatches both when this hits zero.
	std::uint16_t v2_pending = 0;

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

	// set when a block is inserted into this piece and the hasher thread
	// should be woken to make hashing progress. Cleared when the hasher
	// thread picks it up. Used to coalesce multiple insertions into a
	// single wakeup.
	static constexpr cached_piece_flags needs_hasher_kick_flag = 6_bit;

	// set by a thread about to wait on m_flushing_cv for flushing_flag to
	// clear on this piece. flush_piece_impl only calls notify_all() when this
	// flag is set, avoiding the cost of signalling.
	static constexpr cached_piece_flags notify_flushed_flag = 7_bit;

	// set by flush_storage() when it wants to erase a piece but a hasher
	// thread holds hashing_flag. When kick_hasher() later clears
	// hashing_flag it will see this flag and free+erase the piece itself.
	static constexpr cached_piece_flags pending_free_flag = 8_bit;

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

// one entry per v2 block waiting for a hasher thread. The entry owns the
// buffer until the hash is computed and stored on the storage; if the cbe
// is flushed while the entry is still queued, the queue keeps the buffer
// alive so the hasher can still consume it without a disk read-back. (If
// the cpe is removed entirely -- free_piece / clear_piece_impl -- the
// matching queue entries are dropped along with their buffers.)
struct v2_hash_queue_entry
{
	v2_hash_queue_entry(piece_location p,
		std::shared_ptr<pread_storage> s,
		std::uint16_t b,
		std::uint16_t hl,
		disk_buffer_holder buf_)
		: piece(p)
		, storage(std::move(s))
		, block(b)
		, hash_len(hl)
		, buf(std::move(buf_))
	{}

	v2_hash_queue_entry(v2_hash_queue_entry&&) = default;
	v2_hash_queue_entry& operator=(v2_hash_queue_entry&&) = default;

	piece_location piece;
	// keeps the storage alive past torrent removal, so the precomputed
	// hash can still be stored on it.
	std::shared_ptr<pread_storage> storage;
	std::uint16_t block = 0;
	// blocks straddling the v2 piece boundary in a hybrid torrent are
	// hashed only up to piece_size2.
	std::uint16_t hash_len = 0;
	disk_buffer_holder buf;
};

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
		// independent of hasher_cursor: any v2 block may be hashed in any
		// order. Precomputed v2 hashes live on pread_storage and are
		// consulted by the caller before reaching here.
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
	//   f(ph, hasher_cursor, blocks)
	//
	//   ph: SHA1 piece hasher already fed blocks [0, hasher_cursor).
	//   hasher_cursor: index of the first block not yet incorporated in ph.
	//     f() must continue feeding blocks from this index onward.
	//   blocks: per-block buffer pointers. A null entry means the block is not
	//     in the cache and must be read from disk.
	//
	// After f() returns hasher_cursor is advanced and buffers that have
	// already been flushed to disk are freed. If all blocks are flushed, the
	// piece entry is removed from the cache entirely.
	// v2 block hashes (when applicable) live in pread_storage's
	// precomputed_block_hashes; the caller is expected to consult that
	// directly.
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

		for (int i = 0; i < blocks_in_piece; ++i)
		{
			char const* buf = piece_iter->blocks[i].data();
			blocks[i] = buf;
		}

		view.modify(piece_iter, [](cached_piece_entry& e) { e.flags |= cached_piece_entry::hashing_flag; });
		l.unlock();

		auto se = scope_end([&] {
			l.lock();
			// recount rather than relying on the pre-unlock snapshot:
			// blocks flushed during the hashing window had their
			// m_num_unhashed contribution removed by flush_piece_impl.
			bool const is_v1 = bool(piece_iter->flags & cached_piece_entry::v1_hashes_flag);
			int actual_unhashed = 0;
			if (is_v1)
			{
				for (int i = hasher_cursor; i < blocks_in_piece; ++i)
				{
					auto const& cbe = piece_iter->blocks[i];
					if (cbe.has_buf() || cbe.get_write_job()) ++actual_unhashed;
				}
			}
			view.modify(piece_iter, [&](cached_piece_entry& e) {
				e.flags |= cached_piece_entry::force_flush_flag | cached_piece_entry::piece_hash_returned_flag;
				e.flags &= ~cached_piece_entry::hashing_flag;
				e.hasher_cursor = static_cast<std::uint16_t>(blocks_in_piece);
			});
			TORRENT_ASSERT(m_num_unhashed >= actual_unhashed);
			m_num_unhashed -= actual_unhashed;
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
		f(piece_iter->ph.get(), hasher_cursor, blocks);
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
		// owning reference to the storage; used to keep it alive while v2
		// blocks for this piece sit in m_v2_hash_queue. May be null when v2
		// is false or when the caller (e.g. tests) doesn't need the queue.
		std::shared_ptr<pread_storage> storage;
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

	// Hangs hash_job on the piece if there are pending v2 hash queue
	// entries for it (either still in m_v2_hash_queue or currently being
	// hashed by a drain). drain_v2_hash_queue will post the job when the
	// queue empties for the piece. Returns true if hung; false if there's
	// nothing to wait for (piece gone, no pending entries) or another
	// path already owns hash_job.
	bool wait_for_v2_queue(piece_location loc, disk_job* hash_job);

	// Called from a hasher thread when woken by interrupt. Processes all
	// pieces marked with needs_hasher_kick_flag, calling kick_hasher on each.
	// Returns true if force_flush_flag was set on any piece, meaning a generic
	// thread should be woken to flush those pieces to disk.
	// Jobs hung on a piece that stops hashing early are extracted and placed in
	// retry_jobs so the caller can re-submit them.
	bool kick_pending_hashers(jobqueue_t& completed_jobs, jobqueue_t& retry_jobs);

	// Drains the queue: pops entries in batches, hashes each batch outside
	// the cache lock, and forwards results via
	// `store(storage, piece, block, hash)`. Hash jobs parked on a piece
	// (via try_hash_piece's v2-pending defer branch or wait_for_v2_queue)
	// are appended to `posted` once that piece's v2_pending reaches zero.
	// `clear_piece_fun` is invoked for clear_piece jobs deferred for the
	// same reason (see try_clear_piece). Returns when the queue is empty.
	template <typename Fun>
	void drain_v2_hash_queue(Fun store,
		jobqueue_t& posted,
		std::function<void(jobqueue_t, disk_job*)> const& clear_piece_fun);

	// this should be called by a disk thread
	// the callback should return the number of blocks it successfully flushed
	// to disk. The flush callback receives a snapshot of the pending write
	// jobs (null entries for blocks with no pending job), taken under the
	// cache mutex so the callback can run without it.
	void flush_to_disk(std::function<int(bitfield&, span<disk_job* const>)> f,
		int target_blocks,
		std::function<void(jobqueue_t, disk_job*)> clear_piece_fun,
		bool optimistic = false);

	void flush_storage(std::function<int(bitfield&, span<disk_job* const>)> f,
		storage_index_t storage,
		std::function<void(jobqueue_t, disk_job*)> clear_piece_fun);

	std::size_t size() const;
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

	// remove every m_v2_hash_queue entry whose piece matches loc, releasing
	// the buffers they own. Returns the count removed. Mutex must be held.
	int drop_v2_queue_entries(piece_location loc);

	// this requires the mutex to be locked
	void clear_piece_impl(cached_piece_entry& cpe, jobqueue_t& aborted);

	template <typename Iter, typename View>
	Iter flush_piece_impl(View& view,
		Iter piece_iter,
		std::function<int(bitfield&, span<disk_job* const>)> const& f,
		std::unique_lock<std::mutex>& l,
		span<cached_block_entry> const blocks,
		std::function<void(jobqueue_t, disk_job*)> clear_piece_fun);

	mutable std::mutex m_mutex;
	std::condition_variable m_flushing_cv;
	piece_container m_pieces;

	// allocator used for all disk buffers in this cache. Set lazily on the
	// first insert() call. All blocks share the same allocator.
	buffer_allocator_interface* m_allocator = nullptr;

	// the number of *dirty* blocks in the cache. i.e. blocks that need to be
	// flushed to disk. The cache may (briefly) hold more buffers than this
	// while finishing hashing blocks.
	int m_blocks = 0;

	// number of blocks in the cache belonging to v1 (or hybrid) pieces that
	// have not yet been passed through the v1 piece hasher, i.e. where
	// hasher_cursor <= block_idx. Pure-v2 pieces have no v1 hasher cursor
	// concept and don't contribute to this counter.
	int m_num_unhashed = 0;

	// record disk_observers that we've signalled back-pressure to. Once the
	// cache size drop below the low watermark, we'll signal them they can resume
	back_pressure m_back_pressure;

	// FIFO queue of v2 block hashing work. Each entry owns its buffer (moved
	// out of the originating write_job in insert()); the cbe reads through
	// borrowed_buf while the entry is queued. Drained by drain_v2_hash_queue()
	// from hasher threads: on success the buffer moves back into the cbe's
	// write_job for the normal flush path; if the cbe is gone (clear_piece,
	// flush completed) the buffer drops with the entry. cpe.v2_pending counts
	// this piece's outstanding entries (queued + in flight inside a drain
	// batch) so a clear_piece can wait for them.
	std::deque<v2_hash_queue_entry> m_v2_hash_queue;
};

template <typename Fun>
void disk_cache::drain_v2_hash_queue(Fun store,
	jobqueue_t& posted,
	std::function<void(jobqueue_t, disk_job*)> const& clear_piece_fun)
{
	// Batched so the cache mutex is acquired once per batch_size entries
	// instead of per entry. The bound also caps the stack footprint and
	// lets other hasher threads grab their own batches in parallel.
	constexpr int batch_size = 16;
	std::vector<v2_hash_queue_entry> batch;
	batch.reserve(batch_size);

	for (;;)
	{
		batch.clear();
		{
			std::unique_lock<std::mutex> l(m_mutex);
			INVARIANT_CHECK;
			int const take = std::min(int(m_v2_hash_queue.size()), batch_size);
			if (take == 0) return;
			for (int i = 0; i < take; ++i)
			{
				batch.push_back(std::move(m_v2_hash_queue.front()));
				m_v2_hash_queue.pop_front();
			}
		}

		for (auto& entry : batch)
		{
			hasher256 h;
			h.update({entry.buf.data(), entry.hash_len});
			store(entry.storage, entry.piece.piece, int(entry.block), h.final());
		}

		{
			std::unique_lock<std::mutex> l(m_mutex);
			INVARIANT_CHECK;
			// If the cbe still references our buffer, hand it back so the
			// regular flush path can take it. Otherwise the block was already
			// flushed (or the piece evicted) and the buffer just drops.
			auto& view = m_pieces.template get<0>();
			// piece_iters is kept aligned with batch so the loop below
			// (which gates on v2_pending == 0 after all decrements have
			// landed) can reuse the lookups. v2_pending isn't an indexed
			// key, so view.modify doesn't invalidate cached iterators.
			// Every slot must be assigned exactly once before any
			// `continue` -- a default-constructed multi_index iterator is
			// singular and comparing it is undefined.
			TORRENT_ALLOCA(piece_iters,
				typename piece_container::nth_index<0>::type::iterator,
				int(batch.size()));
			int i = 0;
			for (auto& entry : batch)
			{
				auto piece_iter = view.find(entry.piece);
				piece_iters[i++] = piece_iter;
				if (piece_iter == view.end()) continue;
				view.modify(piece_iter, [](cached_piece_entry& e) {
					TORRENT_ASSERT(e.v2_pending > 0);
					--e.v2_pending;
				});
				if (entry.block >= piece_iter->blocks_in_piece()) continue;
				auto& cbe = piece_iter->blocks[entry.block];
				auto* j = cbe.get_write_job();
				if (j == nullptr)
				{
					// cbe was flushed while our v2 entry was outstanding.
					// flush_piece_impl couldn't hold-alive because the buffer
					// wasn't cache-owned (the queue owned it). If a hasher
					// (kick_hasher for v1/hybrid, hash_piece for any) is
					// mid-update, its block snapshot still points into
					// entry.buf -- dropping now would dangle that pointer.
					// Move the buffer into the cbe so kick_hasher's cleanup
					// loop (or free_piece / clear_piece_impl) frees it once
					// the cursor passes this block.
					// !has_buf() should always be true here: in v2/hybrid mode
					// nothing else parks a held-alive buf in the cbe (the
					// queue owns the buffer, so flush_piece_impl's
					// needed_by_hasher branch is never taken). The check
					// guards against the degenerate "same block inserted
					// twice and flushed twice before the drain runs" case;
					// otherwise disk_buffer_ref's move-assign would abort
					// trying to overwrite a non-empty ref.
					auto& cbe2 = piece_iter->blocks[entry.block];
					if ((piece_iter->flags & cached_piece_entry::hashing_flag)
						&& int(entry.block) >= piece_iter->hasher_cursor && !cbe2.has_buf())
					{
						cbe2.write_state = disk_buffer_ref(std::move(entry.buf));
						++m_blocks;
						// flush_piece_impl decremented m_num_unhashed when it
						// transitioned the cbe to flushed-no-buf (v1/hybrid).
						// Re-parking the buffer as held-alive makes this block
						// unhashed again until kick_hasher's cleanup sweeps it
						// past the cursor.
						if (piece_iter->flags & cached_piece_entry::v1_hashes_flag)
							++m_num_unhashed;
					}
					continue;
				}
				auto& wj = std::get<aux::job::write>(j->action);
				// cbe may have been cleared and re-inserted with a new buffer
				// while we were hashing; an address mismatch means this
				// write_job isn't ours.
				if (wj.borrowed_buf != entry.buf.data()) continue;
				wj.buf = std::move(entry.buf);
				++m_blocks;
			}
			// post any hash_job parked on the cpe by try_hash_piece's
			// v2-pending defer branch or wait_for_v2_queue, and finish any
			// clear_piece job try_clear_piece (or flush_piece_impl's deferred-
			// clear branch) parked on the cpe -- all gated on v2_pending == 0.
			for (auto piece_iter : piece_iters)
			{
				if (piece_iter == view.end()) continue;
				if (piece_iter->v2_pending > 0) continue;
				if (piece_iter->hash_job != nullptr)
				{
					disk_job* j = nullptr;
					view.modify(piece_iter,
						[&](cached_piece_entry& e) { j = std::exchange(e.hash_job, nullptr); });
					posted.push_back(j);
				}
				if (piece_iter->clear_piece != nullptr)
				{
					disk_job* clear_piece = nullptr;
					view.modify(piece_iter, [&](cached_piece_entry& e) {
						clear_piece = std::exchange(e.clear_piece, nullptr);
					});
					// clear_piece_impl already ran when the clear was parked;
					// any aborted writes were dispatched at that point. We
					// only need to post the clear job's completion now.
					clear_piece_fun({}, clear_piece);
				}
			}
			m_back_pressure.check_buffer_level(m_blocks + int(m_v2_hash_queue.size()));
		}
	}
}
}

#endif

