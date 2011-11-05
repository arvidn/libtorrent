/*

Copyright (c) 2010, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_BLOCK_CACHE
#define TORRENT_BLOCK_CACHE

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/cstdint.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/shared_array.hpp>
#include <list>
#include <vector>

#include "libtorrent/ptime.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/io_service_fwd.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/sliding_average.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/tailqueue.hpp"
#include "libtorrent/disk_buffer_pool.hpp"

namespace libtorrent
{
	struct disk_io_job;
	class piece_manager;
	struct disk_buffer_pool;
	struct cache_status;
	struct hash_thread;
	struct block_cache_reference;
	struct tailqueue;

	using boost::multi_index::multi_index_container;
	using boost::multi_index::ordered_non_unique;
	using boost::multi_index::ordered_unique;
	using boost::multi_index::indexed_by;
	using boost::multi_index::member;
	using boost::multi_index::const_mem_fun;
	using boost::multi_index::composite_key;

	struct partial_hash
	{
		partial_hash(): offset(0) {}
		// the number of bytes in the piece that has been hashed
		int offset;
		// the sha-1 context
		hasher h;
	};

	struct cached_block_entry
	{
		cached_block_entry(): buf(0), refcount(0), written(0), hitcount(0)
			, dirty(false), pending(false), uninitialized(false)
		{
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			hashing = false;
			reading_count = 0;
			check_count = 0;
#endif
		}

		char* buf;

		// the number of references to this buffer. These references
		// might be in outstanding asyncronous requests or in peer
		// connection send buffers. We can't free the buffer until
		// all references are gone and refcount reaches 0. The buf
		// pointer in this struct doesn't count as a reference and
		// is always the last to be cleared
		boost::uint32_t refcount:15;

		// this block has been written to disk
		bool written:1;

		// the number of times this block has been copied out of
		// the cache, serving a request.
		boost::uint32_t hitcount:13;

		// if this is true, this block needs to be written to
		// disk before it's freed. Typically all blocks in a piece
		// would either be dirty (write coalesce cache) or not dirty
		// (read-ahead cache). Once blocks are written to disk, the
		// dirty flag is cleared and effectively turns the block
		// into a read cache block
		bool dirty:1;

		// pending means that this buffer has not yet been filled in
		// with valid data. There's an outstanding read job for this.
		// If the dirty flag is set, it means there's an outstanding
		// write job to write this block.
		bool pending:1;

		// this is used for freshly allocated read buffers. For read
		// operations, the disk-I/O thread will look for this flag
		// when issueing read jobs.
		// it is not valid for this flag to be set for blocks where
		// the dirty flag is set.
		bool uninitialized:1;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		// this block is part of an outstanding hash job
		bool hashing:1;
		// this block is being used in this many peer's send buffers currently
		int reading_count;
		// the number of check_piece disk jobs that have a reference to this block
		int check_count;
#endif
	};

	struct cached_piece_entry
	{
		cached_piece_entry();
		~cached_piece_entry();

		// storage this piece belongs to
		boost::intrusive_ptr<piece_manager> storage;

		int get_piece() const { return piece; }
		void* get_storage() const { return storage.get(); }

		// if this is set, we'll be calculating the hash
		// for this piece. This member stores the interim
		// state while we're calulcating the hash.
		partial_hash* hash;

		// the pointers to the block data
		boost::shared_array<cached_block_entry> blocks;
		
		// these are outstanding jobs, waiting to be
		// handled for this piece. For read pieces, these
		// are the write jobs that will be dispatched back
		// to the writing peer once their data hits disk.
		// for read jobs, these are outstanding read jobs
		// for this piece that are waiting for data to become
		// avaialable. Read jobs may be overlapping.
		tailqueue jobs;

		// the last time a block was written to this piece
		// plus the minimum amount of time the block is guaranteed
		// to stay in the cache
		ptime expire;

		boost::uint64_t piece:18;

		// the number of dirty blocks in this piece
		boost::uint64_t num_dirty:14;
		
		// the number of blocks in the cache for this piece
		boost::uint64_t num_blocks:14;

		// if this is true, whenever refcount hits 0, 
		// this piece should be deleted
		boost::uint64_t marked_for_deletion:1;

		// this is set to true once we flush blocks past
		// the hash cursor. Once this happens, there's
		// no point in keeping cache blocks around for
		// it in avoid_readback mode
		boost::uint64_t need_readback:1;

		// the total number of blocks in this piece (and the number
		// of elements in the blocks array)
		boost::uint64_t blocks_in_piece:14;

		// while we have an outstanding async hash operation
		// working on this piece, 'hashing' is set to the first block
		// in the range that is being hashed. When the operation
		// returns, this is set to -1. -1 means there's no
		// outstanding hash operation running
		boost::int32_t hashing;

		// the sum of all refcounts in all blocks
		boost::uint32_t refcount;	
	};

	struct block_cache : disk_buffer_pool
	{
		block_cache(int block_size, hash_thread& h, io_service& ios);

	private:

		typedef multi_index_container<
			cached_piece_entry, indexed_by<
				// first index. Ordered by storage pointer and piece index
				ordered_unique<
					composite_key<cached_piece_entry,
						const_mem_fun<cached_piece_entry, void*, &cached_piece_entry::get_storage>,
						const_mem_fun<cached_piece_entry, int, &cached_piece_entry::get_piece>
					>
				>
				// second index. Ordered by expiration time
				, ordered_non_unique<member<cached_piece_entry, ptime
					, &cached_piece_entry::expire> >
				> 
			> cache_t;

		typedef cache_t::nth_index<0>::type cache_piece_index_t;
		typedef cache_t::nth_index<1>::type cache_lru_index_t;

	public:

		typedef cache_piece_index_t::iterator iterator;
		typedef cache_lru_index_t::iterator lru_iterator;

		void reclaim_block(block_cache_reference const& ref, tailqueue& jobs);

		// returns the range of all pieces that belongs to the
		// given storage
		std::pair<iterator, iterator> pieces_for_storage(void* st);

		// returns a range of all pieces. This migh be a very
		// long list, use carefully
		std::pair<iterator, iterator> all_pieces();

		// returns a range of all pieces, in LRU order
		std::pair<lru_iterator, lru_iterator> all_lru_pieces();

		iterator map_iterator(lru_iterator it)
		{
			return m_pieces.project<0>(it);
		}

		// deletes all pieces in the cache. asserts that there
		// are no outstanding jobs
		void clear();

		// mark this piece for deletion. If there are no outstanding
		// requests to this piece, it's removed immediately, and the
		// passed in iterator will be invalidated
		void mark_for_deletion(iterator i);

		// simialr to  mark_for_deletion, except for actually marking the
		// piece for deletion. If the piece was actually deleted,
		// the function returns true
		bool evict_piece(iterator i);

		// returns the number of bytes read on success (cache hit)
		// -1 on cache miss
		int try_read(disk_io_job* j);

		// if the piece is marked for deletion and has a refcount
		// of 0, this function will post any sync jobs and
		// delete the piece from the cache
		bool maybe_free_piece(iterator p, tailqueue& jobs);

		// either returns the piece in the cache, or allocates
		// a new empty piece and returns it.
		block_cache::iterator allocate_piece(disk_io_job const* j);

		// looks for this piece in the cache. If it's there, returns a pointer
		// to it, otherwise 0.
		block_cache::iterator find_piece(block_cache_reference const& ref);
		block_cache::iterator find_piece(disk_io_job const* j);
		block_cache::iterator find_piece(cached_piece_entry const* pe);

		block_cache::iterator end();

		// allocates and marks the covered blocks as pending to
		// be filled in. This should be followed by issuing an
		// async read operation to read in the bytes. The function
		// returns the number of blocks that were allocated. If
		// it's less than the requested, it means the cache is
		// full and there's no space left
		int allocate_pending(iterator p
			, int start, int end, disk_io_job* j
			, int prio = 0, bool force = false);

		// clear the pending flags of the specified block range.
		// these blocks must be completely filled with valid
		// data from the disk before this call is made, unless
		// the disk call failed. If the disk read call failed,
		// the error code is passed in as 'ec'. In this case
		// all jobs for this piece are dispatched with the error
		// code. The io_service passed in is where the jobs are
		// dispatched
		void mark_as_done(iterator p, int begin, int end
			, tailqueue& jobs, storage_error const& ec);

		// this is called by the hasher thread when hashing of
		// a range of block is complete.
		void hashing_done(cached_piece_entry* p, int begin, int end
			, tailqueue& jobs);

		// clear free all buffers marked as dirty with
		// refcount of 0.
		void abort_dirty(iterator p, tailqueue& jobs);

		// adds a block to the cache, marks it as dirty and
		// associates the job with it. When the block is
		// flushed, the callback is posted
		block_cache::iterator add_dirty_block(disk_io_job* j);
	
#ifdef TORRENT_DEBUG
		void check_invariant() const;
#endif
		
		// try to remove num number of read cache blocks from the cache
		// pick the least recently used ones first
		// return the number of blocks that was requested to be evicted
		// that couldn't be
		int try_evict_blocks(int num, int prio, iterator ignore);

		void get_stats(cache_status* ret) const;

		void add_hash_time(time_duration dt, int num_blocks)
		{
			TORRENT_ASSERT(num_blocks > 0);
			m_hash_time.add_sample(int(total_microseconds(dt / num_blocks)));
			m_cumulative_hash_time += total_microseconds(dt);
		}

		void pinned_change(int diff)
		{
			TORRENT_ASSERT(diff > 0 || m_pinned_blocks >= -diff);
			m_pinned_blocks += diff;
		}

	private:

		void kick_hasher(cached_piece_entry* pe, int& hash_start, int& hash_end);

		void reap_piece_jobs(iterator p, storage_error const& ec
			, int hash_start, int hash_end, tailqueue& jobs
			, bool reap_hash_jobs);

		// returns number of bytes read on success, -1 on cache miss
		// (just because the piece is in the cache, doesn't mean all
		// the blocks are there)
		int copy_from_piece(iterator p, disk_io_job* j);

		void free_piece(iterator i);
		int drain_piece_bufs(cached_piece_entry& p, std::vector<char*>& buf);

		// block container
		cache_t m_pieces;

		// the number of blocks in the cache
		// that are in the read cache
		boost::uint32_t m_read_cache_size;
		// the number of blocks in the cache
		// that are in the write cache
		boost::uint32_t m_write_cache_size;

		// the number of blocks that are currently sitting
		// in peer's send buffers. If two peers are sending
		// the same block, it counts as 2, even though there're
		// no buffer duplication
		boost::uint32_t m_send_buffer_blocks;

		boost::uint32_t m_blocks_read;
		boost::uint32_t m_blocks_read_hit;

		// average hash time (in microseconds)
		sliding_average<512> m_hash_time;

		// microseconds
		size_type m_cumulative_hash_time;

		// the number of blocks with a refcount > 0, i.e.
		// they may not be evicted
		int m_pinned_blocks;

		hash_thread& m_hash_thread;
	};

}

#endif // TORRENT_BLOCK_CACHE

