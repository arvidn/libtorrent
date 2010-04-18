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

#include "libtorrent/ptime.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/io_service_fwd.hpp"

namespace libtorrent
{
	struct disk_io_job;
	struct piece_manager;
	struct disk_buffer_pool;

	using boost::multi_index::multi_index_container;
	using boost::multi_index::ordered_non_unique;
	using boost::multi_index::ordered_unique;
	using boost::multi_index::indexed_by;
	using boost::multi_index::member;
	using boost::multi_index::const_mem_fun;
	using boost::multi_index::composite_key;

	struct block_cache
	{
		block_cache(disk_buffer_pool& p);

		struct cached_block_entry
		{
			cached_block_entry(): buf(0), refcount(0), dirty(false), pending(false) {}

			char* buf;

			// the number of references to this buffer. These references
			// might be in outstanding asyncronous requests or in peer
			// connection send buffers. We can't free the buffer until
			// all references are gone and refcount reaches 0. The buf
			// pointer in this struct doesn't count as a reference and
			// is always the last to be cleared
			boost::uint32_t refcount:30;

			// if this is true, this block needs to be written to
			// disk before it's freed. Typically all blocks in a piece
			// would either be dirty (write coalesce cache) or not dirty
			// (read-ahead cache).
			bool dirty:1;

			// pending means that this buffer has not yet
			// been filled in with valid date. There's an
			// outstanding read job for this
			bool pending:1;
		};

		struct cached_piece_entry
		{
			cached_piece_entry();

			int piece;
			// storage this piece belongs to
			boost::intrusive_ptr<piece_manager> storage;

			void* storage_ptr() const
			{ return storage.get(); }

			// the last time a block was writting to this piece
			// plus the minimum amount of time the block is guaranteed
			// to stay in the cache
			time_t expire;

			// the sum of all refcounts in all blocks
			boost::uint16_t refcount;
			
			// the number of dirty blocks in this piece
			boost::uint16_t num_dirty;

			// the number of blocks in the cache for this piece
			boost::uint16_t num_blocks;

			// the total number of blocks in this piece (and the number
			// of elements in the blocks array)
			boost::uint16_t blocks_in_piece;

			// if this is true, whenever refcount hits 0, 
			// this piece should be deleted
			bool marked_for_deletion:1;

			// the pointers to the block data
			boost::shared_array<cached_block_entry> blocks;
			
			// these are outstanding jobs, waiting to be
			// handled for this piece. For read pieces, these
			// are the write jobs that will be dispatched back
			// to the writing peer once their data hits disk.
			// for read jobs, these are outstanding read jobs
			// for this piece that are waiting for data to become
			// avaialable. Read jobs may be overlapping.
			std::list<disk_io_job> jobs;
		};

	private:

		typedef multi_index_container<
			cached_piece_entry, indexed_by<
				// first index. Ordered by storage pointer and piece index
				ordered_unique<
					composite_key<cached_piece_entry,
						const_mem_fun<cached_piece_entry, void*, &cached_piece_entry::storage_ptr>,
						member<cached_piece_entry, int, &cached_piece_entry::piece>
					>
				>
				// second index. Ordered by expiration time
				, ordered_non_unique<member<cached_piece_entry, time_t
					, &cached_piece_entry::expire> >
				> 
			> cache_t;

		typedef cache_t::nth_index<0>::type cache_piece_index_t;
		typedef cache_t::nth_index<1>::type cache_lru_index_t;

	public:

		typedef cache_piece_index_t::iterator iterator;

		// returns the range of all pieces that belongs to the
		// given storage
		std::pair<iterator, iterator> pieces_for_storage(void* st);

		// returns a range of all pieces. This migh be a very
		// long list, use carefully
		std::pair<iterator, iterator> all_pieces();

		// mark this piece for deletion. If there are no outstanding
		// requests to this piece, it's removed immediately, and the
		// passed in iterator will be invalidated
		void mark_for_deletion(iterator i);

		// returns the number of bytes read on success (cache hit)
		// -1 on cache miss
		int try_read(disk_io_job const& j);

		// either returns the piece in the cache, or allocates
		// a new empty piece and returns it.
		block_cache::iterator allocate_piece(disk_io_job const& j);

		// looks for this piece in the cache. If it's there, returns a pointer
		// to it, otherwise 0.
		block_cache::iterator find_piece(disk_io_job const& j);

		block_cache::iterator end();

		// allocates and marks the covered blocks as pending to
		// be filled in. This should be followed by issuing an
		// async read operation to read in the bytes. The function
		// returns the number of blocks that were allocated. If
		// it's less than the requested, it means the cache is
		// full and there's no space left
		int allocate_pending(iterator p
			, int start, int end, disk_io_job const& j);

		// clear the pending flags of the specified block range.
		// these blocks must be completely filled with valid
		// data from the disk before this call is made, unless
		// the disk call failed. If the disk read call failed,
		// the error code is passed in as 'ec'. In this case
		// all jobs for this piece are dispatched with the error
		// code. The io_service passed in is where the jobs are
		// dispatched
		void mark_as_done(iterator p, int begin, int end
			, io_service& ios, int queue_buffer_size, error_code const& ec);

		// clear free all buffers marked as dirty with
		// refcount of 0.
		void abort_dirty(iterator p, io_service& ios);

		// adds a block to the cache, marks it as dirty and
		// associates the job with it. When the block is
		// flushed, the callback is posted
		block_cache::iterator add_dirty_block(disk_io_job const& j);
	
#ifdef TORRENT_DEBUG
		void check_invariant() const;
#endif
		
	private:

		// returns number of bytes read on success, -1 on cache miss
		// (just because the piece is in the cache, doesn't mean all
		// the blocks are there)
		int copy_from_piece(iterator p, disk_io_job const& j);

		void free_piece(iterator i);

		// block container
		cache_t m_pieces;

		// total number of blocks allowed to be cached
		boost::uint32_t m_max_size;

		// the total number of blocks in the cache
		boost::uint32_t m_cache_size;
		// the number of blocks in the cache
		// that are in the read cache
		boost::uint32_t m_read_cache_size;
		boost::uint32_t m_write_cache_size;

		boost::uint32_t m_blocks_read;
		boost::uint32_t m_blocks_read_hit;

		// this is where buffers are allocated from
		disk_buffer_pool& m_buffer_pool;
	};

}

#endif // TORRENT_BLOCK_CACHE

