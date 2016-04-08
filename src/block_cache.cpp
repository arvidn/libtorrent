/*

Copyright (c) 2010-2016, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include "libtorrent/block_cache.hpp"
#include "libtorrent/disk_buffer_pool.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/disk_io_job.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/io_service_fwd.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/disk_io_thread.hpp" // disk_operation_failed
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/time.hpp"

#ifdef TORRENT_DEBUG
#include "libtorrent/random.hpp"
#endif

/*

	The disk cache mimics ARC (adaptive replacement cache).
	See paper: http://dbs.uni-leipzig.de/file/ARC.pdf
	See slides: http://www-vlsi.stanford.edu/smart_memories/protected/meetings/spring2004/arc-fast.pdf

	This cache has a few modifications to make it fit the bittorrent use
	case better. It has a few more lists and it defers the eviction
	of pieces.

	read_lru1
		This is a plain LRU for items that have been requested once. If a piece
		in this list gets accessed again, by someone other than the first
		accessor, the piece is promoted into LRU2. which holds pieces that are
		more frequently used, and more important to keep around as this LRU list
		takes churn.

	read_lru1_ghost
		This is a list of pieces that were least recently evicted from read_lru1.
		These pieces don't hold any actual blocks in the cache, they are just
		here to extend the reach and probability for pieces to be promoted into
		read_lru2. Any piece in this list that get one more access is promoted to
		read_lru2. This is technically a cache-miss, since there's no cached
		blocks here, but for the purposes of promoting the piece from
		infrequently used to frequently used), it's considered a cache-hit.

	read_lru2
		TODO

	read_lru2_ghost
		TODO

	volatile_read_lru
		TODO

	write_lru
		TODO

	Cache hits
	..........

	When a piece get a cache hit, it's promoted, either to the beginning of the
	lru2 or into lru2. Since this ARC implementation operates on pieces instead
	of blocks, any one peer requesting blocks from one piece would essentially
	always produce a "cache hit" the second block it requests. In order to make
	the promotions make more sense, and be more in the spirit of the ARC
	algorithm, each access contains a token, unique to each peer. If any access
	has a different token than the last one, it's considered a cache hit. This
	is because at least two peers requested blocks from the same piece.

	Deferred evictions
	..................

	Since pieces and blocks can be pinned in the cache, and it's not always
	practical, or possible, to evict a piece at the point where a new block is
	allocated (because it's not known what the block will be used for),
	evictions are not done at the time of allocating blocks. Instead, whenever
	an operation requires to add a new piece to the cache, it also records the
	cache event leading to it, in m_last_cache_op. This is one of cache_miss
	(piece did not exist in cache), lru1_ghost_hit (the piece was found in
	lru1_ghost and it was promoted) or lru2_ghost_hit (the piece was found in
	lru2_ghost and it was promoted). This cache operation then guides the cache
	eviction algorithm to know which list to evict from. The volatile list is
	always the first one to be evicted however.

	Write jobs
	..........

	When the write cache is enabled, write jobs are not issued via the normal
	job queue. They are just hung on its corresponding cached piece entry, and a
	flush_hashed job is issued. This job will inspect the current state of the
	cached piece and determine if any of the blocks should be flushed. It also
	kicks the hasher, i.e. progresses the SHA1 context, which calculates the
	SHA-1 hash of the piece. This job flushed blocks that have been hashed and
	also form a contiguous block run of at least the write cache line size.

	Read jobs
	.........

	The data blocks pulled in from disk by read jobs, are hung on the
	corresponding cache piece (cached_piece_entry) once the operation completes.
	Read operations typically pulls in an entire read cache stripe, and not just
	the one block that was requested. When adjacent blocks are requested to be
	read in quick succession, there is a risk that each block would pull in more
	blocks (read ahead) and potentially read the same blocks several times, if
	the original requests were serviced by different disk thread. This is
	because all the read operation may start before any of them has completed,
	hanging the resulting blocks in the cache. i.e. they would all be cache
	misses, even though all but the first should be cache hits in the first's
	read ahead.

	In order to solve this problem, there is only a single outstanding read job
	at any given time per piece. When there is an outstanding read job on a
	piece, the *outstanding_read* member is set to 1. This indicates that the
	job should be hung on the piece for later processing, instead of being
	issued into the main job queue. There is a tailqueue on each piece entry
	called read_jobs where these jobs are added.

	At the end of every read job, this job list is inspected, any job in it is
	tried against the cache to see if it's a cache hit now. If it is, complete
	it right away. If it isn't, put it back in the read_jobs list except for
	one, which is issued into the regular job queue.
*/

#define DEBUG_CACHE 0

#if __cplusplus >= 201103L || defined __clang__

#if DEBUG_CACHE
#define DLOG(...) fprintf(__VA_ARGS__)
#else
#define DLOG(...) do {} while (false)
#endif

#else // cplusplus

#if DEBUG_CACHE
#define DLOG fprintf
#else
#define DLOG TORRENT_WHILE_0 fprintf
#endif

#endif // cplusplus

namespace libtorrent {

#if DEBUG_CACHE
void log_refcounts(cached_piece_entry const* pe)
{
	char out[4096];
	char* ptr = out;
	char* end = ptr + sizeof(out);
	ptr += snprintf(ptr, end - ptr, "piece: %d [ ", int(pe->piece));
	for (int i = 0; i < pe->blocks_in_piece; ++i)
	{
		ptr += snprintf(ptr, end - ptr, "%d ", int(pe->blocks[i].refcount));
	}
	strncpy(ptr, "]\n", end - ptr);
	DLOG(stderr, out);
}
#endif

const char* const job_action_name[] =
{
	"read",
	"write",
	"hash",
	"move_storage",
	"release_files",
	"delete_files",
	"check_fastresume",
	"save_resume_data",
	"rename_file",
	"stop_torrent",
#ifndef TORRENT_NO_DEPRECATE
	"cache_piece",
	"finalize_file",
#endif
	"flush_piece",
	"flush_hashed",
	"flush_storage",
	"trim_cache",
	"set_file_priority",
	"load_torrent",
	"clear_piece",
	"tick_storage",
	"resolve_links"
};

#if __cplusplus >= 201103L
// make sure the job names array covers all the job IDs
static_assert(sizeof(job_action_name)/sizeof(job_action_name[0])
	== disk_io_job::num_job_ids, "disk-job-action and action-name-array mismatch");
#endif

#if TORRENT_USE_ASSERTS

	char const* const piece_log_t::job_names[7] =
	{
		"flushing",
		"flush_expired",
		"try_flush_write_blocks",
		"try_flush_write_blocks2",
		"flush_range",
		"clear_outstanding_jobs",
		"set_outstanding_jobs",
	};

	char const* job_name(int j)
	{
		if (j < 0 || j >= piece_log_t::last_job)
			return "unknown";

		if (j < piece_log_t::flushing)
			return job_action_name[j];
		return piece_log_t::job_names[j - piece_log_t::flushing];
	}

	void print_piece_log(std::vector<piece_log_t> const& piece_log)
	{
		for (int i = 0; i < int(piece_log.size()); ++i)
		{
			if (piece_log[i].block == -1)
			{
				printf("%d: %s\n", i, job_name(piece_log[i].job));
			}
			else
			{
				printf("%d: %s %d\n", i, job_name(piece_log[i].job), piece_log[i].block);
			}
		}
	}

	void assert_print_piece(cached_piece_entry const* pe)
	{
		static const char* const cache_state[] =
		{
			"write", "volatile-read", "read-lru", "read-lru-ghost", "read-lfu", "read-lfu-ghost"
		};

		if (pe == NULL)
		{
			assert_print("piece: NULL\n");
		}
		else
		{
			assert_print("piece: %d\nrefcount: %d\npiece_refcount: %d\n"
				"num_blocks: %d\nhashing: %d\n\nhash: %p\nhash_offset: %d\n"
				"cache_state: (%d) %s\noutstanding_flush: %d\npiece: %d\n"
				"num_dirty: %d\nnum_blocks: %d\nblocks_in_piece: %d\n"
				"hashing_done: %d\nmarked_for_deletion: %d\nneed_readback: %d\n"
				"hash_passed: %d\nread_jobs: %d\njobs: %d\n"
				"piece_log:\n"
				, int(pe->piece), pe->refcount, pe->piece_refcount, int(pe->num_blocks)
				, int(pe->hashing), static_cast<void*>(pe->hash), pe->hash ? pe->hash->offset : -1
				, int(pe->cache_state)
				, pe->cache_state < cached_piece_entry::num_lrus ? cache_state[pe->cache_state] : ""
				, int(pe->outstanding_flush), int(pe->piece), int(pe->num_dirty)
				, int(pe->num_blocks), int(pe->blocks_in_piece), int(pe->hashing_done)
				, int(pe->marked_for_deletion), int(pe->need_readback), pe->hash_passes
				, int(pe->read_jobs.size()), int(pe->jobs.size()));
			for (int i = 0; i < pe->piece_log.size(); ++i)
			{
				assert_print("%s %s (%d)", (i==0?"":",")
					, job_name(pe->piece_log[i].job), pe->piece_log[i].block);
			}
		}
		assert_print("\n");
	}


#define TORRENT_PIECE_ASSERT(cond, piece) \
	do { if (!(cond)) { assert_print_piece(piece); assert_fail(#cond, __LINE__, __FILE__, TORRENT_FUNCTION, 0); } } TORRENT_WHILE_0

#else
#define TORRENT_PIECE_ASSERT(cond, piece) do {} TORRENT_WHILE_0
#endif

cached_piece_entry::cached_piece_entry()
	: storage()
	, hash(0)
	, last_requester(NULL)
	, blocks()
	, expire(min_time())
	, piece(0)
	, num_dirty(0)
	, num_blocks(0)
	, blocks_in_piece(0)
	, hashing(0)
	, hashing_done(0)
	, marked_for_deletion(false)
	, need_readback(false)
	, cache_state(read_lru1)
	, piece_refcount(0)
	, outstanding_flush(0)
	, outstanding_read(0)
	, pinned(0)
	, refcount(0)
#if TORRENT_USE_ASSERTS
	, hash_passes(0)
	, in_storage(false)
	, in_use(true)
#endif
{}

cached_piece_entry::~cached_piece_entry()
{
	TORRENT_ASSERT(piece_refcount == 0);
	TORRENT_ASSERT(jobs.size() == 0);
	TORRENT_ASSERT(read_jobs.size() == 0);
#if TORRENT_USE_ASSERTS
	for (int i = 0; i < blocks_in_piece; ++i)
	{
		TORRENT_ASSERT(blocks[i].buf == 0);
		TORRENT_ASSERT(!blocks[i].pending);
		TORRENT_ASSERT(blocks[i].refcount == 0);
		TORRENT_ASSERT(blocks[i].hashing_count == 0);
		TORRENT_ASSERT(blocks[i].flushing_count == 0);
	}
	in_use = false;
#endif
	delete hash;
}

block_cache::block_cache(int block_size, io_service& ios
	, boost::function<void()> const& trigger_trim)
	: disk_buffer_pool(block_size, ios, trigger_trim)
	, m_last_cache_op(cache_miss)
	, m_ghost_size(8)
	, m_max_volatile_blocks(100)
	, m_volatile_size(0)
	, m_read_cache_size(0)
	, m_write_cache_size(0)
	, m_send_buffer_blocks(0)
	, m_pinned_blocks(0)
{
}

// returns:
// -1: not in cache
// -2: no memory
int block_cache::try_read(disk_io_job* j, bool expect_no_fail)
{
	INVARIANT_CHECK;

	TORRENT_ASSERT(j->buffer.disk_block == 0);

#if TORRENT_USE_ASSERTS
	// we're not allowed to add dirty blocks
	// for a deleted storage!
	TORRENT_ASSERT(std::find(m_deleted_storages.begin(), m_deleted_storages.end()
		, std::make_pair(j->storage->files()->name()
			, reinterpret_cast<void const*>(j->storage->files())))
		== m_deleted_storages.end());
#endif

	cached_piece_entry* p = find_piece(j);

	int ret = 0;

	// if the piece cannot be found in the cache,
	// it's a cache miss
	TORRENT_ASSERT(!expect_no_fail || p != NULL);
	if (p == 0) return -1;

#if TORRENT_USE_ASSERTS
	p->piece_log.push_back(piece_log_t(j->action, j->d.io.offset / 0x4000));
#endif
	cache_hit(p, j->requester, j->flags & disk_io_job::volatile_read);

	ret = copy_from_piece(p, j, expect_no_fail);
	if (ret < 0) return ret;

	ret = j->d.io.buffer_size;
	return ret;
}

void block_cache::bump_lru(cached_piece_entry* p)
{
	// move to the top of the LRU list
	TORRENT_PIECE_ASSERT(p->cache_state == cached_piece_entry::write_lru, p);
	linked_list<cached_piece_entry>* lru_list = &m_lru[p->cache_state];

	// move to the back (MRU) of the list
	lru_list->erase(p);
	lru_list->push_back(p);
	p->expire = aux::time_now();
}

// this is called for pieces that we're reading from, when they
// are in the cache (including the ghost lists)
void block_cache::cache_hit(cached_piece_entry* p, void* requester, bool volatile_read)
{
// this can be pretty expensive
//	INVARIANT_CHECK;

	TORRENT_ASSERT(p);
	TORRENT_ASSERT(p->in_use);

	// move the piece into this queue. Whenever we have a cache
	// hit, we move the piece into the lru2 queue (i.e. the most
	// frequently used piece). However, we only do that if the
	// requester is different than the last one. This is to
	// avoid a single requester making it look like a piece is
	// frequently requested, when in fact it's only a single peer
	int target_queue = cached_piece_entry::read_lru2;

	if (p->last_requester == requester || requester == NULL)
	{
		// if it's the same requester and the piece isn't in
		// any of the ghost lists, ignore it
		if (p->cache_state == cached_piece_entry::read_lru1
			|| p->cache_state == cached_piece_entry::read_lru2
			|| p->cache_state == cached_piece_entry::write_lru
			|| p->cache_state == cached_piece_entry::volatile_read_lru)
			return;

		if (p->cache_state == cached_piece_entry::read_lru1_ghost)
			target_queue = cached_piece_entry::read_lru1;
	}

	if (p->cache_state == cached_piece_entry::volatile_read_lru)
	{
		// a volatile read hit on a volatile piece doesn't do anything
		if (volatile_read) return;

		// however, if this is a proper read on a volatile piece
		// we need to promote it to lru1
		target_queue = cached_piece_entry::read_lru1;
	}

	if (requester != NULL)
		p->last_requester = requester;

	// if we have this piece anywhere in L1 or L2, it's a "hit"
	// and it should be bumped to the highest priority in L2
	// i.e. "frequently used"
	if (p->cache_state < cached_piece_entry::read_lru1
		|| p->cache_state > cached_piece_entry::read_lru2_ghost)
		return;

	// if we got a cache hit in a ghost list, that indicates the proper
	// list is too small. Record which ghost list we got the hit in and
	// it will be used to determine which end of the cache we'll evict
	// from, next time we need to reclaim blocks
	if (p->cache_state == cached_piece_entry::read_lru1_ghost)
	{
		m_last_cache_op = ghost_hit_lru1;
		p->storage->add_piece(p);
	}
	else if (p->cache_state == cached_piece_entry::read_lru2_ghost)
	{
		m_last_cache_op = ghost_hit_lru2;
		p->storage->add_piece(p);
	}

	// move into L2 (frequently used)
	m_lru[p->cache_state].erase(p);
	m_lru[target_queue].push_back(p);
	p->cache_state = target_queue;
	p->expire = aux::time_now();
#if TORRENT_USE_ASSERTS
	switch (p->cache_state)
	{
		case cached_piece_entry::write_lru:
		case cached_piece_entry::volatile_read_lru:
		case cached_piece_entry::read_lru1:
		case cached_piece_entry::read_lru2:
			TORRENT_ASSERT(p->in_storage == true);
			break;
		default:
			TORRENT_ASSERT(p->in_storage == false);
			break;
	}
#endif
}

// this is used to move pieces primarily from the write cache
// to the read cache. Technically it can move from read to write
// cache as well, it's unclear if that ever happens though
void block_cache::update_cache_state(cached_piece_entry* p)
{
	int state = p->cache_state;
	int desired_state = p->cache_state;
	if (p->num_dirty > 0 || p->hash != 0)
		desired_state = cached_piece_entry::write_lru;
	else if (p->cache_state == cached_piece_entry::write_lru)
		desired_state = cached_piece_entry::read_lru1;

	if (desired_state == state) return;

	TORRENT_PIECE_ASSERT(state < cached_piece_entry::num_lrus, p);
	TORRENT_PIECE_ASSERT(desired_state < cached_piece_entry::num_lrus, p);
	linked_list<cached_piece_entry>* src = &m_lru[state];
	linked_list<cached_piece_entry>* dst = &m_lru[desired_state];

	src->erase(p);
	dst->push_back(p);
	p->expire = aux::time_now();
	p->cache_state = desired_state;
#if TORRENT_USE_ASSERTS
	switch (p->cache_state)
	{
		case cached_piece_entry::write_lru:
		case cached_piece_entry::volatile_read_lru:
		case cached_piece_entry::read_lru1:
		case cached_piece_entry::read_lru2:
			TORRENT_ASSERT(p->in_storage == true);
			break;
		default:
			TORRENT_ASSERT(p->in_storage == false);
			break;
	}
#endif
}

void block_cache::try_evict_one_volatile()
{
	INVARIANT_CHECK;

	DLOG(stderr, "[%p] try_evict_one_volatile\n", static_cast<void*>(this));

	if (m_volatile_size < m_max_volatile_blocks) return;

	linked_list<cached_piece_entry>* piece_list = &m_lru[cached_piece_entry::volatile_read_lru];

	for (list_iterator<cached_piece_entry> i = piece_list->iterate(); i.get();)
	{
		cached_piece_entry* pe = reinterpret_cast<cached_piece_entry*>(i.get());
		TORRENT_PIECE_ASSERT(pe->in_use, pe);
		i.next();

		if (pe->ok_to_evict())
		{
#ifdef TORRENT_DEBUG
			for (int j = 0; j < pe->blocks_in_piece; ++j)
				TORRENT_PIECE_ASSERT(pe->blocks[j].buf == 0, pe);
#endif
			TORRENT_PIECE_ASSERT(pe->refcount == 0, pe);
			move_to_ghost(pe);
			continue;
		}

		TORRENT_PIECE_ASSERT(pe->num_dirty == 0, pe);

		// someone else is using this piece
		if (pe->refcount > 0) continue;

		// some blocks are pinned in this piece, skip it
		if (pe->pinned > 0) continue;

		char** to_delete = TORRENT_ALLOCA(char*, pe->blocks_in_piece);
		int num_to_delete = 0;

		// go through the blocks and evict the ones that are not dirty and not
		// referenced
		for (int j = 0; j < pe->blocks_in_piece; ++j)
		{
			cached_block_entry& b = pe->blocks[j];

			TORRENT_PIECE_ASSERT(b.dirty == false, pe);
			TORRENT_PIECE_ASSERT(b.pending == false, pe);

			if (b.buf == 0 || b.refcount > 0 || b.dirty || b.pending) continue;

			to_delete[num_to_delete++] = b.buf;
			b.buf = NULL;
			TORRENT_PIECE_ASSERT(pe->num_blocks > 0, pe);
			--pe->num_blocks;
			TORRENT_PIECE_ASSERT(m_read_cache_size > 0, pe);
			--m_read_cache_size;
			TORRENT_PIECE_ASSERT(m_volatile_size > 0, pe);
			--m_volatile_size;
		}

		if (pe->ok_to_evict())
		{
#ifdef TORRENT_DEBUG
			for (int j = 0; j < pe->blocks_in_piece; ++j)
				TORRENT_PIECE_ASSERT(pe->blocks[j].buf == 0, pe);
#endif
			move_to_ghost(pe);
		}

		if (num_to_delete == 0) return;

		DLOG(stderr, "[%p]    removed %d blocks\n", static_cast<void*>(this)
			, num_to_delete);

		free_multiple_buffers(to_delete, num_to_delete);
		return;
	}
}

cached_piece_entry* block_cache::allocate_piece(disk_io_job const* j, int cache_state)
{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
	INVARIANT_CHECK;
#endif

	TORRENT_ASSERT(cache_state < cached_piece_entry::num_lrus);

	// we're assuming we're not allocating a ghost piece
	// a bit further down
	TORRENT_ASSERT(cache_state != cached_piece_entry::read_lru1_ghost
		&& cache_state != cached_piece_entry::read_lru2_ghost);

	cached_piece_entry* p = find_piece(j);
	if (p == 0)
	{
		int const piece_size = j->storage->files()->piece_size(j->piece);
		int const blocks_in_piece = (piece_size + block_size() - 1) / block_size();

		cached_piece_entry pe;
		pe.piece = j->piece;
		pe.storage = j->storage;
		pe.expire = aux::time_now();
		pe.blocks_in_piece = blocks_in_piece;
		pe.blocks.reset(new (std::nothrow) cached_block_entry[blocks_in_piece]);
		pe.cache_state = cache_state;
		pe.last_requester = j->requester;
		TORRENT_PIECE_ASSERT(pe.blocks, &pe);
		if (!pe.blocks) return 0;
		p = const_cast<cached_piece_entry*>(&*m_pieces.insert(pe).first);

		j->storage->add_piece(p);

		TORRENT_PIECE_ASSERT(p->cache_state < cached_piece_entry::num_lrus, p);
		linked_list<cached_piece_entry>* lru_list = &m_lru[p->cache_state];
		lru_list->push_back(p);

		// this piece is part of the ARC cache (as opposed to
		// the write cache). Allocating a new read piece indicates
		// that we just got a cache miss. Record this to determine
		// which end to evict blocks from next time we need to
		// evict blocks
		if (cache_state == cached_piece_entry::read_lru1)
			m_last_cache_op = cache_miss;

#if TORRENT_USE_ASSERTS
		switch (p->cache_state)
		{
			case cached_piece_entry::write_lru:
			case cached_piece_entry::volatile_read_lru:
			case cached_piece_entry::read_lru1:
			case cached_piece_entry::read_lru2:
				TORRENT_ASSERT(p->in_storage == true);
				break;
			default:
				TORRENT_ASSERT(p->in_storage == false);
				break;
		}
#endif
	}
	else
	{
		TORRENT_PIECE_ASSERT(p->in_use, p);

		// we want to retain the piece now
		p->marked_for_deletion = false;

		// only allow changing the cache state downwards. i.e. turn a ghost
		// piece into a non-ghost, or a read piece into a write piece
		if (p->cache_state > cache_state)
		{
			// this can happen for instance if a piece fails the hash check
			// first it's in the write cache, then it completes and is moved
			// into the read cache, but fails and is cleared (into the ghost list)
			// then we want to add new dirty blocks to it and we need to move
			// it back into the write cache

			// it also happens when pulling a ghost piece back into the proper cache

			if (p->cache_state == cached_piece_entry::read_lru1_ghost
				|| p->cache_state == cached_piece_entry::read_lru2_ghost)
			{
				// since it used to be a ghost piece, but no more,
				// we need to add it back to the storage
				p->storage->add_piece(p);
			}
			m_lru[p->cache_state].erase(p);
			p->cache_state = cache_state;
			m_lru[p->cache_state].push_back(p);
			p->expire = aux::time_now();
#if TORRENT_USE_ASSERTS
			switch (p->cache_state)
			{
				case cached_piece_entry::write_lru:
				case cached_piece_entry::volatile_read_lru:
				case cached_piece_entry::read_lru1:
				case cached_piece_entry::read_lru2:
					TORRENT_ASSERT(p->in_storage == true);
					break;
				default:
					TORRENT_ASSERT(p->in_storage == false);
					break;
			}
#endif
		}
	}

	return p;
}

#if TORRENT_USE_ASSERTS
void block_cache::mark_deleted(file_storage const& fs)
{
	m_deleted_storages.push_back(std::make_pair(fs.name()
		, reinterpret_cast<void const*>(&fs)));
	if (m_deleted_storages.size() > 100)
		m_deleted_storages.erase(m_deleted_storages.begin());
}
#endif

cached_piece_entry* block_cache::add_dirty_block(disk_io_job* j)
{
#if !defined TORRENT_DISABLE_POOL_ALLOCATOR
	TORRENT_ASSERT(is_disk_buffer(j->buffer.disk_block));
#endif
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
	INVARIANT_CHECK;
#endif

#if TORRENT_USE_ASSERTS
	// we're not allowed to add dirty blocks
	// for a deleted storage!
	TORRENT_ASSERT(std::find(m_deleted_storages.begin(), m_deleted_storages.end()
		, std::make_pair(j->storage->files()->name()
		, static_cast<void const*>(j->storage->files())))
		== m_deleted_storages.end());
#endif

	TORRENT_ASSERT(j->buffer.disk_block);
	TORRENT_ASSERT(m_write_cache_size + m_read_cache_size + 1 <= in_use());

	cached_piece_entry* pe = allocate_piece(j, cached_piece_entry::write_lru);
	TORRENT_ASSERT(pe);
	if (pe == 0) return pe;

	TORRENT_PIECE_ASSERT(pe->in_use, pe);

	int block = j->d.io.offset / block_size();
	TORRENT_ASSERT((j->d.io.offset % block_size()) == 0);

	// we should never add a new dirty block on a piece
	// that has checked the hash. Before we add it, the
	// piece need to be cleared (with async_clear_piece)
	TORRENT_PIECE_ASSERT(pe->hashing_done == 0, pe);

	// this only evicts read blocks

	int evict = num_to_evict(1);
	if (evict > 0) try_evict_blocks(evict, pe);

	TORRENT_PIECE_ASSERT(block < pe->blocks_in_piece, pe);
	TORRENT_PIECE_ASSERT(j->piece == pe->piece, pe);
	TORRENT_PIECE_ASSERT(!pe->marked_for_deletion, pe);

	TORRENT_PIECE_ASSERT(pe->blocks[block].refcount == 0, pe);

	cached_block_entry& b = pe->blocks[block];

	TORRENT_PIECE_ASSERT(b.buf != j->buffer.disk_block, pe);

	// we might have a left-over read block from
	// hash checking
	// we might also have a previous dirty block which
	// we're still waiting for to be written
	if (b.buf != 0 && b.buf != j->buffer.disk_block)
	{
		TORRENT_PIECE_ASSERT(b.refcount == 0 && !b.pending, pe);
		free_block(pe, block);
		TORRENT_PIECE_ASSERT(b.dirty == 0, pe);
	}

	b.buf = j->buffer.disk_block;

	b.dirty = true;
	++pe->num_blocks;
	++pe->num_dirty;
	++m_write_cache_size;
	j->buffer.disk_block = 0;
	TORRENT_PIECE_ASSERT(j->piece == pe->piece, pe);
	TORRENT_PIECE_ASSERT(j->flags & disk_io_job::in_progress, pe);
	TORRENT_PIECE_ASSERT(j->piece == pe->piece, pe);
	pe->jobs.push_back(j);

	if (block == 0 && pe->hash == NULL && pe->hashing_done == false)
		pe->hash = new partial_hash;

	update_cache_state(pe);

	bump_lru(pe);

	return pe;
}

// flushed is an array of num_flushed integers. Each integer is the block index
// that was flushed. This function marks those blocks as not pending and not
// dirty. It also adjusts its understanding of the read vs. write cache size
// (since these blocks now are part of the read cache) the refcounts of the
// blocks are also decremented by this function. They are expected to have been
// incremented by the caller.
void block_cache::blocks_flushed(cached_piece_entry* pe, int const* flushed, int num_flushed)
{
	TORRENT_PIECE_ASSERT(pe->in_use, pe);

	for (int i = 0; i < num_flushed; ++i)
	{
		int block = flushed[i];
		TORRENT_PIECE_ASSERT(block >= 0, pe);
		TORRENT_PIECE_ASSERT(block < pe->blocks_in_piece, pe);
		TORRENT_PIECE_ASSERT(pe->blocks[block].dirty, pe);
		TORRENT_PIECE_ASSERT(pe->blocks[block].pending, pe);
		pe->blocks[block].pending = false;
		// it's important to mark it as non-dirty before decrementing the
		// refcount because the buffer may be marked as discardable/volatile it
		// this is the last reference to it
		pe->blocks[block].dirty = false;
		dec_block_refcount(pe, block, block_cache::ref_flushing);
	}

	m_write_cache_size -= num_flushed;
	m_read_cache_size += num_flushed;
	pe->num_dirty -= num_flushed;

	update_cache_state(pe);
}

std::pair<block_cache::iterator, block_cache::iterator> block_cache::all_pieces() const
{
	return std::make_pair(m_pieces.begin(), m_pieces.end());
}

void block_cache::free_block(cached_piece_entry* pe, int block)
{
	TORRENT_ASSERT(pe != 0);
	TORRENT_PIECE_ASSERT(pe->in_use, pe);
	TORRENT_PIECE_ASSERT(block < pe->blocks_in_piece, pe);
	TORRENT_PIECE_ASSERT(block >= 0, pe);

	cached_block_entry& b = pe->blocks[block];

	TORRENT_PIECE_ASSERT(b.refcount == 0, pe);
	TORRENT_PIECE_ASSERT(!b.pending, pe);
	TORRENT_PIECE_ASSERT(b.buf, pe);

	if (b.dirty)
	{
		--pe->num_dirty;
		b.dirty = false;
		TORRENT_PIECE_ASSERT(m_write_cache_size > 0, pe);
		--m_write_cache_size;
	}
	else
	{
		TORRENT_PIECE_ASSERT(m_read_cache_size > 0, pe);
		--m_read_cache_size;
		if (pe->cache_state == cached_piece_entry::volatile_read_lru)
		{
			--m_volatile_size;
		}
	}


	TORRENT_PIECE_ASSERT(pe->num_blocks > 0, pe);
	--pe->num_blocks;
	free_buffer(b.buf);
	b.buf = NULL;
}

bool block_cache::evict_piece(cached_piece_entry* pe, tailqueue<disk_io_job>& jobs)
{
	INVARIANT_CHECK;

	TORRENT_PIECE_ASSERT(pe->in_use, pe);

	char** to_delete = TORRENT_ALLOCA(char*, pe->blocks_in_piece);
	int num_to_delete = 0;
	for (int i = 0; i < pe->blocks_in_piece; ++i)
	{
		if (pe->blocks[i].buf == 0 || pe->blocks[i].refcount > 0) continue;
		TORRENT_PIECE_ASSERT(!pe->blocks[i].pending, pe);
		TORRENT_PIECE_ASSERT(pe->blocks[i].buf != 0, pe);
		TORRENT_PIECE_ASSERT(num_to_delete < pe->blocks_in_piece, pe);
		to_delete[num_to_delete++] = pe->blocks[i].buf;
		pe->blocks[i].buf = NULL;
		TORRENT_PIECE_ASSERT(pe->num_blocks > 0, pe);
		--pe->num_blocks;
		if (!pe->blocks[i].dirty)
		{
			TORRENT_PIECE_ASSERT(m_read_cache_size > 0, pe);
			--m_read_cache_size;
		}
		else
		{
			TORRENT_PIECE_ASSERT(pe->num_dirty > 0, pe);
			--pe->num_dirty;
			pe->blocks[i].dirty = false;
			TORRENT_PIECE_ASSERT(m_write_cache_size > 0, pe);
			--m_write_cache_size;
		}
		if (pe->num_blocks == 0) break;
	}

	if (pe->cache_state == cached_piece_entry::volatile_read_lru)
	{
		m_volatile_size -= num_to_delete;
	}

	if (num_to_delete) free_multiple_buffers(to_delete, num_to_delete);

	if (pe->ok_to_evict(true))
	{
		delete pe->hash;
		pe->hash = NULL;

		// append will move the items from pe->jobs onto the end of jobs
		jobs.append(pe->jobs);
		TORRENT_ASSERT(pe->jobs.size() == 0);

		if (pe->cache_state == cached_piece_entry::read_lru1_ghost
			|| pe->cache_state == cached_piece_entry::read_lru2_ghost)
			return true;

		if (pe->cache_state == cached_piece_entry::write_lru
			|| pe->cache_state == cached_piece_entry::volatile_read_lru)
			erase_piece(pe);
		else
			move_to_ghost(pe);
		return true;
	}

	return false;
}

void block_cache::mark_for_deletion(cached_piece_entry* p)
{
	INVARIANT_CHECK;

	DLOG(stderr, "[%p] block_cache mark-for-deletion "
		"piece: %d\n", static_cast<void*>(this), int(p->piece));

	TORRENT_PIECE_ASSERT(p->jobs.empty(), p);
	tailqueue<disk_io_job> jobs;
	if (!evict_piece(p, jobs))
	{
		p->marked_for_deletion = true;
	}
}

void block_cache::erase_piece(cached_piece_entry* pe)
{
	INVARIANT_CHECK;

	TORRENT_PIECE_ASSERT(pe->ok_to_evict(), pe);
	TORRENT_PIECE_ASSERT(pe->cache_state < cached_piece_entry::num_lrus, pe);
	TORRENT_PIECE_ASSERT(pe->jobs.empty(), pe);
	linked_list<cached_piece_entry>* lru_list = &m_lru[pe->cache_state];
	if (pe->hash)
	{
		TORRENT_PIECE_ASSERT(pe->hash->offset == 0, pe);
		delete pe->hash;
		pe->hash = NULL;
	}
	if (pe->cache_state != cached_piece_entry::read_lru1_ghost
		&& pe->cache_state != cached_piece_entry::read_lru2_ghost)
		pe->storage->remove_piece(pe);
	lru_list->erase(pe);
	m_pieces.erase(*pe);
}

// this only evicts read blocks. For write blocks, see
// try_flush_write_blocks in disk_io_thread.cpp
int block_cache::try_evict_blocks(int num, cached_piece_entry* ignore)
{
	INVARIANT_CHECK;

	if (num <= 0) return 0;

	DLOG(stderr, "[%p] try_evict_blocks: %d\n", static_cast<void*>(this), num);

	char** to_delete = TORRENT_ALLOCA(char*, num);
	int num_to_delete = 0;

	// There are two ends of the ARC cache we can evict from. There's L1 and L2.
	// The last cache operation determines which end we'll evict from. If we go
	// through the entire list from the preferred end, and still need to evict
	// more blocks, we'll go to the other end and start evicting from there. The
	// lru_list is an array of two lists, these are the two ends to evict from,
	// ordered by preference.

	linked_list<cached_piece_entry>* lru_list[3];

	// however, before we consider any of the proper LRU lists, we evict pieces
	// from the volatile list. These are low priority pieces that were
	// specifically marked as to not survive long in the cache. These are the
	// first pieces to go when evicting
	lru_list[0] = &m_lru[cached_piece_entry::volatile_read_lru];

	if (m_last_cache_op == cache_miss)
	{
		// when there was a cache miss, evict from the largest list, to tend to
		// keep the lists of equal size when we don't know which one is
		// performing better
		if (m_lru[cached_piece_entry::read_lru2].size()
			> m_lru[cached_piece_entry::read_lru1].size())
		{
			lru_list[1] = &m_lru[cached_piece_entry::read_lru2];
			lru_list[2] = &m_lru[cached_piece_entry::read_lru1];
		}
		else
		{
			lru_list[1] = &m_lru[cached_piece_entry::read_lru1];
			lru_list[2] = &m_lru[cached_piece_entry::read_lru2];
		}
	}
	else if (m_last_cache_op == ghost_hit_lru1)
	{
		// when we insert new items or move things from L1 to L2
		// evict blocks from L2
		lru_list[1] = &m_lru[cached_piece_entry::read_lru2];
		lru_list[2] = &m_lru[cached_piece_entry::read_lru1];
	}
	else
	{
		// when we get cache hits in L2 evict from L1
		lru_list[1] = &m_lru[cached_piece_entry::read_lru1];
		lru_list[2] = &m_lru[cached_piece_entry::read_lru2];
	}

	// end refers to which end of the ARC cache we're evicting
	// from. The LFU or the LRU end
	for (int end = 0; num > 0 && end < 3; ++end)
	{
		// iterate over all blocks in order of last being used (oldest first) and
		// as long as we still have blocks to evict TODO: it's somewhat expensive
		// to iterate over this linked list. Presumably because of the random
		// access of memory. It would be nice if pieces with no evictable blocks
		// weren't in this list
		for (list_iterator<cached_piece_entry> i = lru_list[end]->iterate(); i.get() && num > 0;)
		{
			cached_piece_entry* pe = reinterpret_cast<cached_piece_entry*>(i.get());
			TORRENT_PIECE_ASSERT(pe->in_use, pe);
			i.next();

			if (pe == ignore)
				continue;

			if (pe->ok_to_evict())
			{
#ifdef TORRENT_DEBUG
				for (int j = 0; j < pe->blocks_in_piece; ++j)
					TORRENT_PIECE_ASSERT(pe->blocks[j].buf == 0, pe);
#endif
				TORRENT_PIECE_ASSERT(pe->refcount == 0, pe);
				move_to_ghost(pe);
				continue;
			}

			TORRENT_PIECE_ASSERT(pe->num_dirty == 0, pe);

			// all blocks are pinned in this piece, skip it
			if (pe->num_blocks <= pe->pinned) continue;

			// go through the blocks and evict the ones that are not dirty and not
			// referenced
			int removed = 0;
			for (int j = 0; j < pe->blocks_in_piece && num > 0; ++j)
			{
				cached_block_entry& b = pe->blocks[j];

				if (b.buf == 0 || b.refcount > 0 || b.dirty || b.pending) continue;

				to_delete[num_to_delete++] = b.buf;
				b.buf = NULL;
				TORRENT_PIECE_ASSERT(pe->num_blocks > 0, pe);
				--pe->num_blocks;
				++removed;
				--num;
			}

			TORRENT_PIECE_ASSERT(m_read_cache_size >= removed, pe);
			m_read_cache_size -= removed;
			if (pe->cache_state == cached_piece_entry::volatile_read_lru)
			{
				m_volatile_size -= removed;
			}

			if (pe->ok_to_evict())
			{
#ifdef TORRENT_DEBUG
				for (int j = 0; j < pe->blocks_in_piece; ++j)
					TORRENT_PIECE_ASSERT(pe->blocks[j].buf == 0, pe);
#endif
				move_to_ghost(pe);
			}
		}
	}

	// if we can't evict enough blocks from the read cache, also look at write
	// cache pieces for blocks that have already been written to disk and can be
	// evicted the first pass, we only evict blocks that have been hashed, the
	// second pass we flush anything this is potentially a very expensive
	// operation, since we're likely to have iterate every single block in the
	// cache, and we might not get to evict anything.

	// TODO: this should probably only be done every n:th time
	if (num > 0 && m_read_cache_size > m_pinned_blocks)
	{
		for (int pass = 0; pass < 2 && num > 0; ++pass)
		{
			for (list_iterator<cached_piece_entry> i = m_lru[cached_piece_entry::write_lru].iterate(); i.get() && num > 0;)
			{
				cached_piece_entry* pe = reinterpret_cast<cached_piece_entry*>(i.get());
				TORRENT_PIECE_ASSERT(pe->in_use, pe);

				i.next();

				if (pe == ignore)
					continue;

				if (pe->ok_to_evict())
				{
#ifdef TORRENT_DEBUG
					for (int j = 0; j < pe->blocks_in_piece; ++j)
						TORRENT_PIECE_ASSERT(pe->blocks[j].buf == 0, pe);
#endif
					TORRENT_PIECE_ASSERT(pe->refcount == 0, pe);
					erase_piece(pe);
					continue;
				}

				// all blocks in this piece are dirty
				if (pe->num_dirty == pe->num_blocks)
					continue;

				int end = pe->blocks_in_piece;

				// the first pass, only evict blocks that have been
				// hashed
				if (pass == 0 && pe->hash)
					end = pe->hash->offset / block_size();

				// go through the blocks and evict the ones
				// that are not dirty and not referenced
				int removed = 0;
				for (int j = 0; j < end && num > 0; ++j)
				{
					cached_block_entry& b = pe->blocks[j];

					if (b.buf == 0 || b.refcount > 0 || b.dirty || b.pending) continue;

					to_delete[num_to_delete++] = b.buf;
					b.buf = NULL;
					TORRENT_PIECE_ASSERT(pe->num_blocks > 0, pe);
					--pe->num_blocks;
					++removed;
					--num;
				}

				TORRENT_PIECE_ASSERT(m_read_cache_size >= removed, pe);
				m_read_cache_size -= removed;
				if (pe->cache_state == cached_piece_entry::volatile_read_lru)
				{
					m_volatile_size -= removed;
				}

				if (pe->ok_to_evict())
				{
#ifdef TORRENT_DEBUG
					for (int j = 0; j < pe->blocks_in_piece; ++j)
						TORRENT_PIECE_ASSERT(pe->blocks[j].buf == 0, pe);
#endif
					erase_piece(pe);
				}
			}
		}
	}

	if (num_to_delete == 0) return num;

	DLOG(stderr, "[%p]    removed %d blocks\n", static_cast<void*>(this)
		, num_to_delete);

	free_multiple_buffers(to_delete, num_to_delete);

	return num;
}

void block_cache::clear(tailqueue<disk_io_job>& jobs)
{
	INVARIANT_CHECK;

	// this holds all the block buffers we want to free
	// at the end
	std::vector<char*> bufs;

	for (iterator p = m_pieces.begin()
		, end(m_pieces.end()); p != end; ++p)
	{
		cached_piece_entry& pe = const_cast<cached_piece_entry&>(*p);
#if TORRENT_USE_ASSERTS
		for (tailqueue_iterator<disk_io_job> i = pe.jobs.iterate(); i.get(); i.next())
			TORRENT_PIECE_ASSERT((static_cast<disk_io_job const*>(i.get()))->piece == pe.piece, &pe);
		for (tailqueue_iterator<disk_io_job> i = pe.read_jobs.iterate(); i.get(); i.next())
			TORRENT_PIECE_ASSERT((static_cast<disk_io_job const*>(i.get()))->piece == pe.piece, &pe);
#endif
		// this also removes the jobs from the piece
		jobs.append(pe.jobs);
		jobs.append(pe.read_jobs);

		drain_piece_bufs(pe, bufs);
	}

	if (!bufs.empty()) free_multiple_buffers(&bufs[0], bufs.size());

	// clear lru lists
	for (int i = 0; i < cached_piece_entry::num_lrus; ++i)
		m_lru[i].get_all();

	m_pieces.clear();
}

void block_cache::move_to_ghost(cached_piece_entry* pe)
{
	TORRENT_PIECE_ASSERT(pe->refcount == 0, pe);
	TORRENT_PIECE_ASSERT(pe->piece_refcount == 0, pe);
	TORRENT_PIECE_ASSERT(pe->num_blocks == 0, pe);
	TORRENT_PIECE_ASSERT(pe->in_use, pe);

	if (pe->cache_state == cached_piece_entry::volatile_read_lru)
	{
		erase_piece(pe);
		return;
	}

	TORRENT_PIECE_ASSERT(pe->cache_state == cached_piece_entry::read_lru1
		|| pe->cache_state == cached_piece_entry::read_lru2, pe);

	// if the piece is in L1 or L2, move it into the ghost list
	// i.e. recently evicted
	if (pe->cache_state != cached_piece_entry::read_lru1
		&& pe->cache_state != cached_piece_entry::read_lru2)
		return;

	// if the ghost list is growing too big, remove the oldest entry
	linked_list<cached_piece_entry>* ghost_list = &m_lru[pe->cache_state + 1];
	while (ghost_list->size() >= m_ghost_size)
	{
		cached_piece_entry* p = static_cast<cached_piece_entry*>(ghost_list->front());
		TORRENT_PIECE_ASSERT(p != pe, p);
		TORRENT_PIECE_ASSERT(p->num_blocks == 0, p);
		TORRENT_PIECE_ASSERT(p->refcount == 0, p);
		TORRENT_PIECE_ASSERT(p->piece_refcount == 0, p);
		erase_piece(p);
	}

	pe->storage->remove_piece(pe);
	m_lru[pe->cache_state].erase(pe);
	pe->cache_state += 1;
	ghost_list->push_back(pe);
}

int block_cache::pad_job(disk_io_job const* j, int blocks_in_piece
	, int read_ahead) const
{
	int block_offset = j->d.io.offset & (block_size()-1);
	int start = j->d.io.offset / block_size();
	int end = block_offset > 0 && (read_ahead > block_size() - block_offset) ? start + 2 : start + 1;

	// take the read-ahead into account
	// make sure to not overflow in this case
	if (read_ahead == INT_MAX) end = blocks_in_piece;
	else end = (std::min)(blocks_in_piece, (std::max)(start + read_ahead, end));

	return end - start;
}

void block_cache::insert_blocks(cached_piece_entry* pe, int block, file::iovec_t *iov
	, int iov_len, disk_io_job* j, int flags)
{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
	INVARIANT_CHECK;
#endif

	TORRENT_ASSERT(pe);
	TORRENT_ASSERT(pe->in_use);
	TORRENT_PIECE_ASSERT(iov_len > 0, pe);

#if TORRENT_USE_ASSERTS
	// we're not allowed to add dirty blocks
	// for a deleted storage!
	TORRENT_ASSERT(std::find(m_deleted_storages.begin(), m_deleted_storages.end()
		, std::make_pair(j->storage->files()->name(), static_cast<void const*>(j->storage->files())))
		== m_deleted_storages.end());
#endif

	cache_hit(pe, j->requester, (j->flags & disk_io_job::volatile_read) != 0);

	TORRENT_ASSERT(pe->in_use);

	for (int i = 0; i < iov_len; ++i, ++block)
	{
		// each iovec buffer has to be the size of a block (or the size of the last block)
		TORRENT_PIECE_ASSERT(iov[i].iov_len == (std::min)(block_size()
			, pe->storage->files()->piece_size(pe->piece) - block * block_size()), pe);

		// no NULL pointers allowed
		TORRENT_ASSERT(iov[i].iov_base);

#ifdef TORRENT_DEBUG_BUFFERS
		TORRENT_PIECE_ASSERT(is_disk_buffer((char*)iov[i].iov_base), pe);
#endif

		if (pe->blocks[block].buf && (flags & blocks_inc_refcount))
		{
			inc_block_refcount(pe, block, ref_reading);
		}

		// either free the block or insert it. Never replace a block
		if (pe->blocks[block].buf)
		{
			free_buffer(static_cast<char*>(iov[i].iov_base));
		}
		else
		{
			pe->blocks[block].buf = static_cast<char*>(iov[i].iov_base);

			TORRENT_PIECE_ASSERT(iov[i].iov_base != NULL, pe);
			TORRENT_PIECE_ASSERT(pe->blocks[block].dirty == false, pe);
			++pe->num_blocks;
			++m_read_cache_size;
			if (j->flags & disk_io_job::volatile_read) ++m_volatile_size;

			if (flags & blocks_inc_refcount)
			{
				bool ret = inc_block_refcount(pe, block, ref_reading);
				TORRENT_UNUSED(ret); // suppress warning
				TORRENT_ASSERT(ret);
			}
		}

		TORRENT_ASSERT(pe->blocks[block].buf != NULL);
	}

	TORRENT_PIECE_ASSERT(pe->cache_state != cached_piece_entry::read_lru1_ghost, pe);
	TORRENT_PIECE_ASSERT(pe->cache_state != cached_piece_entry::read_lru2_ghost, pe);
}

// return false if the memory was purged
bool block_cache::inc_block_refcount(cached_piece_entry* pe, int block, int reason)
{
	TORRENT_PIECE_ASSERT(pe->in_use, pe);
	TORRENT_PIECE_ASSERT(block < pe->blocks_in_piece, pe);
	TORRENT_PIECE_ASSERT(block >= 0, pe);
	if (pe->blocks[block].buf == NULL) return false;
	TORRENT_PIECE_ASSERT(pe->blocks[block].refcount < cached_block_entry::max_refcount, pe);
	if (pe->blocks[block].refcount == 0)
	{
		++pe->pinned;
		++m_pinned_blocks;
	}
	++pe->blocks[block].refcount;
	++pe->refcount;
#if TORRENT_USE_ASSERTS
	switch (reason)
	{
		case ref_hashing: ++pe->blocks[block].hashing_count; break;
		case ref_reading: ++pe->blocks[block].reading_count; break;
		case ref_flushing: ++pe->blocks[block].flushing_count; break;
	};
	TORRENT_ASSERT(pe->blocks[block].refcount >= pe->blocks[block].hashing_count
		+ pe->blocks[block].reading_count + pe->blocks[block].flushing_count);
#else
	TORRENT_UNUSED(reason);
#endif
	return true;
}

void block_cache::dec_block_refcount(cached_piece_entry* pe, int block, int reason)
{
	TORRENT_PIECE_ASSERT(pe->in_use, pe);
	TORRENT_PIECE_ASSERT(block < pe->blocks_in_piece, pe);
	TORRENT_PIECE_ASSERT(block >= 0, pe);

	TORRENT_PIECE_ASSERT(pe->blocks[block].buf != NULL, pe);
	TORRENT_PIECE_ASSERT(pe->blocks[block].refcount > 0, pe);
	--pe->blocks[block].refcount;
	TORRENT_PIECE_ASSERT(pe->refcount > 0, pe);
	--pe->refcount;
	if (pe->blocks[block].refcount == 0)
	{
		TORRENT_PIECE_ASSERT(pe->pinned > 0, pe);
		--pe->pinned;
		TORRENT_PIECE_ASSERT(m_pinned_blocks > 0, pe);
		--m_pinned_blocks;
	}
#if TORRENT_USE_ASSERTS
	switch (reason)
	{
		case ref_hashing: --pe->blocks[block].hashing_count; break;
		case ref_reading: --pe->blocks[block].reading_count; break;
		case ref_flushing: --pe->blocks[block].flushing_count; break;
	};
	TORRENT_PIECE_ASSERT(pe->blocks[block].refcount >= pe->blocks[block].hashing_count
		+ pe->blocks[block].reading_count + pe->blocks[block].flushing_count, pe);
#else
	TORRENT_UNUSED(reason);
#endif
}

void block_cache::abort_dirty(cached_piece_entry* pe)
{
	INVARIANT_CHECK;

	TORRENT_PIECE_ASSERT(pe->in_use, pe);

	char** to_delete = TORRENT_ALLOCA(char*, pe->blocks_in_piece);
	int num_to_delete = 0;
	for (int i = 0; i < pe->blocks_in_piece; ++i)
	{
		if (!pe->blocks[i].dirty
			|| pe->blocks[i].refcount > 0
			|| pe->blocks[i].buf == NULL) continue;

		TORRENT_PIECE_ASSERT(!pe->blocks[i].pending, pe);
		TORRENT_PIECE_ASSERT(pe->blocks[i].dirty, pe);
		to_delete[num_to_delete++] = pe->blocks[i].buf;
		pe->blocks[i].buf = NULL;
		pe->blocks[i].dirty = false;
		TORRENT_PIECE_ASSERT(pe->num_blocks > 0, pe);
		--pe->num_blocks;
		TORRENT_PIECE_ASSERT(m_write_cache_size > 0, pe);
		--m_write_cache_size;
		TORRENT_PIECE_ASSERT(pe->num_dirty > 0, pe);
		--pe->num_dirty;
	}
	if (num_to_delete) free_multiple_buffers(to_delete, num_to_delete);

	update_cache_state(pe);
}

// frees all buffers associated with this piece. May only
// be called for pieces with a refcount of 0
void block_cache::free_piece(cached_piece_entry* pe)
{
	INVARIANT_CHECK;

	TORRENT_PIECE_ASSERT(pe->in_use, pe);

	TORRENT_PIECE_ASSERT(pe->refcount == 0, pe);
	TORRENT_PIECE_ASSERT(pe->piece_refcount == 0, pe);
	TORRENT_PIECE_ASSERT(pe->outstanding_read == 0, pe);

	// build a vector of all the buffers we need to free
	// and free them all in one go
	char** to_delete = TORRENT_ALLOCA(char*, pe->blocks_in_piece);
	int num_to_delete = 0;
	int removed_clean = 0;
	for (int i = 0; i < pe->blocks_in_piece; ++i)
	{
		if (pe->blocks[i].buf == 0) continue;
		TORRENT_PIECE_ASSERT(pe->blocks[i].pending == false, pe);
		TORRENT_PIECE_ASSERT(pe->blocks[i].refcount == 0, pe);
		TORRENT_PIECE_ASSERT(num_to_delete < pe->blocks_in_piece, pe);
		to_delete[num_to_delete++] = pe->blocks[i].buf;
		pe->blocks[i].buf = NULL;
		TORRENT_PIECE_ASSERT(pe->num_blocks > 0, pe);
		--pe->num_blocks;
		if (pe->blocks[i].dirty)
		{
			TORRENT_PIECE_ASSERT(m_write_cache_size > 0, pe);
			--m_write_cache_size;
			TORRENT_PIECE_ASSERT(pe->num_dirty > 0, pe);
			--pe->num_dirty;
		}
		else
		{
			++removed_clean;
		}
	}

	TORRENT_PIECE_ASSERT(m_read_cache_size >= removed_clean, pe);
	m_read_cache_size -= removed_clean;
	if (pe->cache_state == cached_piece_entry::volatile_read_lru)
	{
		m_volatile_size -= num_to_delete;
	}
	if (num_to_delete) free_multiple_buffers(to_delete, num_to_delete);
	update_cache_state(pe);
}

int block_cache::drain_piece_bufs(cached_piece_entry& p, std::vector<char*>& buf)
{
	int const piece_size = p.storage->files()->piece_size(p.piece);
	int const blocks_in_piece = (piece_size + block_size() - 1) / block_size();
	int ret = 0;

	TORRENT_PIECE_ASSERT(p.in_use, &p);

	int removed_clean = 0;
	for (int i = 0; i < blocks_in_piece; ++i)
	{
		if (p.blocks[i].buf == 0) continue;
		TORRENT_PIECE_ASSERT(p.blocks[i].refcount == 0, &p);
		buf.push_back(p.blocks[i].buf);
		++ret;
		p.blocks[i].buf = NULL;
		TORRENT_PIECE_ASSERT(p.num_blocks > 0, &p);
		--p.num_blocks;

		if (p.blocks[i].dirty)
		{
			TORRENT_ASSERT(m_write_cache_size > 0);
			--m_write_cache_size;
			TORRENT_PIECE_ASSERT(p.num_dirty > 0, &p);
			--p.num_dirty;
		}
		else
		{
			++removed_clean;
		}
	}

	TORRENT_ASSERT(m_read_cache_size >= removed_clean);
	m_read_cache_size -= removed_clean;
	if (p.cache_state == cached_piece_entry::volatile_read_lru)
	{
		m_volatile_size -= removed_clean;
	}

	update_cache_state(&p);
	return ret;
}

void block_cache::update_stats_counters(counters& c) const
{
	c.set_value(counters::write_cache_blocks, m_write_cache_size);
	c.set_value(counters::read_cache_blocks, m_read_cache_size);
	c.set_value(counters::pinned_blocks, m_pinned_blocks);

	c.set_value(counters::arc_mru_size, m_lru[cached_piece_entry::read_lru1].size());
	c.set_value(counters::arc_mru_ghost_size, m_lru[cached_piece_entry::read_lru1_ghost].size());
	c.set_value(counters::arc_mfu_size, m_lru[cached_piece_entry::read_lru2].size());
	c.set_value(counters::arc_mfu_ghost_size, m_lru[cached_piece_entry::read_lru2_ghost].size());
	c.set_value(counters::arc_write_size, m_lru[cached_piece_entry::write_lru].size());
	c.set_value(counters::arc_volatile_size, m_lru[cached_piece_entry::volatile_read_lru].size());
}

#ifndef TORRENT_NO_DEPRECATE
void block_cache::get_stats(cache_status* ret) const
{
	ret->write_cache_size = m_write_cache_size;
	ret->read_cache_size = m_read_cache_size;
	ret->pinned_blocks = m_pinned_blocks;
	ret->cache_size = m_read_cache_size + m_write_cache_size;

	ret->arc_mru_size = m_lru[cached_piece_entry::read_lru1].size();
	ret->arc_mru_ghost_size = m_lru[cached_piece_entry::read_lru1_ghost].size();
	ret->arc_mfu_size = m_lru[cached_piece_entry::read_lru2].size();
	ret->arc_mfu_ghost_size = m_lru[cached_piece_entry::read_lru2_ghost].size();
	ret->arc_write_size = m_lru[cached_piece_entry::write_lru].size();
	ret->arc_volatile_size = m_lru[cached_piece_entry::volatile_read_lru].size();
}
#endif

void block_cache::set_settings(aux::session_settings const& sett, error_code& ec)
{
	// the ghost size is the number of pieces to keep track of
	// after they are evicted. Since cache_size is blocks, the
	// assumption is that there are about 128 blocks per piece,
	// and there are two ghost lists, so divide by 2.

	m_ghost_size = (std::max)(8, sett.get_int(settings_pack::cache_size)
		/ (std::max)(sett.get_int(settings_pack::read_cache_line_size), 4) / 2);

	m_max_volatile_blocks = sett.get_int(settings_pack::cache_size_volatile);
	disk_buffer_pool::set_settings(sett, ec);
}

#if TORRENT_USE_INVARIANT_CHECKS
void block_cache::check_invariant() const
{
	int cached_write_blocks = 0;
	int cached_read_blocks = 0;
	int num_pinned = 0;

	std::set<piece_manager*> storages;

	for (int i = 0; i < cached_piece_entry::num_lrus; ++i)
	{
		time_point timeout = min_time();

		for (list_iterator<cached_piece_entry> p = m_lru[i].iterate(); p.get(); p.next())
		{
			cached_piece_entry* pe = static_cast<cached_piece_entry*>(p.get());
			TORRENT_PIECE_ASSERT(pe->cache_state == i, pe);
			if (pe->num_dirty > 0)
				TORRENT_PIECE_ASSERT(i == cached_piece_entry::write_lru, pe);

//			if (i == cached_piece_entry::write_lru)
//				TORRENT_ASSERT(pe->num_dirty > 0);
			for (tailqueue_iterator<disk_io_job> j = pe->jobs.iterate(); j.get(); j.next())
			{
				disk_io_job const* job = static_cast<disk_io_job const*>(j.get());
				TORRENT_PIECE_ASSERT(job->piece == pe->piece, pe);
				TORRENT_PIECE_ASSERT(job->in_use, pe);
				TORRENT_PIECE_ASSERT(!job->callback_called, pe);
			}

			if (i != cached_piece_entry::read_lru1_ghost
				&& i != cached_piece_entry::read_lru2_ghost)
			{
				TORRENT_PIECE_ASSERT(pe->storage->has_piece(pe), pe);
				TORRENT_PIECE_ASSERT(pe->expire >= timeout, pe);
				timeout = pe->expire;
				TORRENT_PIECE_ASSERT(pe->in_storage, pe);
				TORRENT_PIECE_ASSERT(pe->storage->has_piece(pe), pe);
			}
			else
			{
				// pieces in the ghost lists should never have any blocks
				TORRENT_PIECE_ASSERT(pe->num_blocks == 0, pe);
				TORRENT_PIECE_ASSERT(pe->storage->has_piece(pe) == false, pe);
			}

			storages.insert(pe->storage.get());
		}
	}

	for (std::set<piece_manager*>::iterator i = storages.begin()
		, end(storages.end()); i != end; ++i)
	{
		for (boost::unordered_set<cached_piece_entry*>::iterator j = (*i)->cached_pieces().begin()
			, end2((*i)->cached_pieces().end()); j != end2; ++j)
		{
			cached_piece_entry* pe = *j;
			TORRENT_PIECE_ASSERT(pe->storage.get() == *i, pe);
		}
	}

	boost::unordered_set<char*> buffers;
	for (iterator i = m_pieces.begin(), end(m_pieces.end()); i != end; ++i)
	{
		cached_piece_entry const& p = *i;
		TORRENT_PIECE_ASSERT(p.blocks, &p);

		TORRENT_PIECE_ASSERT(p.storage, &p);
		int num_blocks = 0;
		int num_dirty = 0;
		int num_pending = 0;
		int num_refcount = 0;

		bool in_storage = p.storage->has_piece(&p);
		switch (p.cache_state)
		{
			case cached_piece_entry::write_lru:
			case cached_piece_entry::volatile_read_lru:
			case cached_piece_entry::read_lru1:
			case cached_piece_entry::read_lru2:
				TORRENT_ASSERT(in_storage == true);
				break;
			default:
				TORRENT_ASSERT(in_storage == false);
				break;
		}

		for (int k = 0; k < p.blocks_in_piece; ++k)
		{
			if (p.blocks[k].buf)
			{
#if !defined TORRENT_DISABLE_POOL_ALLOCATOR && defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
				TORRENT_PIECE_ASSERT(is_disk_buffer(p.blocks[k].buf), &p);

				// make sure we don't have the same buffer
				// in the cache twice
				TORRENT_PIECE_ASSERT(buffers.count(p.blocks[k].buf) == 0, &p);
				buffers.insert(p.blocks[k].buf);
#endif
				++num_blocks;
				if (p.blocks[k].dirty)
				{
					++num_dirty;
					++cached_write_blocks;
				}
				else
				{
					++cached_read_blocks;
				}
				if (p.blocks[k].pending) ++num_pending;
				if (p.blocks[k].refcount > 0) ++num_pinned;

				TORRENT_PIECE_ASSERT(p.blocks[k].refcount >=
					p.blocks[k].hashing_count
					+ p.blocks[k].reading_count
					+ p.blocks[k].flushing_count, &p);

			}
			else
			{
				TORRENT_PIECE_ASSERT(!p.blocks[k].dirty, &p);
				TORRENT_PIECE_ASSERT(!p.blocks[k].pending, &p);
				TORRENT_PIECE_ASSERT(p.blocks[k].refcount == 0, &p);
			}
			num_refcount += p.blocks[k].refcount;
		}
		TORRENT_PIECE_ASSERT(num_blocks == p.num_blocks, &p);
		TORRENT_PIECE_ASSERT(num_pending <= p.refcount, &p);
		TORRENT_PIECE_ASSERT(num_refcount == p.refcount, &p);
		TORRENT_PIECE_ASSERT(num_dirty == p.num_dirty, &p);
	}
	TORRENT_ASSERT(m_read_cache_size == cached_read_blocks);
	TORRENT_ASSERT(m_write_cache_size == cached_write_blocks);
	TORRENT_ASSERT(m_pinned_blocks == num_pinned);
	TORRENT_ASSERT(m_write_cache_size + m_read_cache_size <= in_use());
}
#endif

// TODO: 2 turn these return values into enums
// returns
// -1: block not in cache
// -2: out of memory

int block_cache::copy_from_piece(cached_piece_entry* const pe
	, disk_io_job* const j
	, bool const expect_no_fail)
{
	INVARIANT_CHECK;
	TORRENT_UNUSED(expect_no_fail);

	TORRENT_PIECE_ASSERT(j->buffer.disk_block == 0, pe);
	TORRENT_PIECE_ASSERT(pe->in_use, pe);

	// copy from the cache and update the last use timestamp
	int block = j->d.io.offset / block_size();
	int block_offset = j->d.io.offset & (block_size()-1);
	int buffer_offset = 0;
	int size = j->d.io.buffer_size;
	int const blocks_to_read = block_offset > 0 && (size > block_size() - block_offset) ? 2 : 1;
	TORRENT_PIECE_ASSERT(size <= block_size(), pe);
	int const start_block = block;

#if TORRENT_USE_ASSERTS
	int const piece_size = j->storage->files()->piece_size(j->piece);
	int const blocks_in_piece = (piece_size + block_size() - 1) / block_size();
	TORRENT_PIECE_ASSERT(start_block < blocks_in_piece, pe);
#endif

	// if there's no buffer, we don't have this block in
	// the cache, and we're not currently reading it in either
	// since it's not pending

	if (inc_block_refcount(pe, start_block, ref_reading) == false)
	{
		TORRENT_ASSERT(!expect_no_fail);
		return -1;
	}

	// if block_offset > 0, we need to read two blocks, and then
	// copy parts of both, because it's not aligned to the block
	// boundaries
	if (blocks_to_read == 1 && (j->flags & disk_io_job::force_copy) == 0)
	{
		// special case for block aligned request
		// don't actually copy the buffer, just reference
		// the existing block. Which means we don't want to decrement the
		// refcount, we're handing the ownership of the reference to the calling
		// thread.
		cached_block_entry& bl = pe->blocks[start_block];

		// make sure it didn't wrap
		TORRENT_PIECE_ASSERT(pe->refcount > 0, pe);
		j->d.io.ref.storage = j->storage.get();
		j->d.io.ref.piece = pe->piece;
		j->d.io.ref.block = start_block;
		j->buffer.disk_block = bl.buf + (j->d.io.offset & (block_size()-1));
		++m_send_buffer_blocks;
		return j->d.io.buffer_size;
	}

	// if we don't have the second block, it's a cache miss
	if (blocks_to_read == 2 && inc_block_refcount(pe, start_block + 1, ref_reading) == false)
	{
		TORRENT_ASSERT(!expect_no_fail);
		dec_block_refcount(pe, start_block, ref_reading);
		return -1;
	}

	j->buffer.disk_block = allocate_buffer("send buffer");
	if (j->buffer.disk_block == 0) return -2;

	while (size > 0)
	{
		TORRENT_PIECE_ASSERT(pe->blocks[block].buf, pe);
		int to_copy = (std::min)(block_size()
			- block_offset, size);
		std::memcpy(j->buffer.disk_block + buffer_offset
			, pe->blocks[block].buf + block_offset
			, to_copy);
		size -= to_copy;
		block_offset = 0;
		buffer_offset += to_copy;
		++block;
	}
	// we incremented the refcount for both of these blocks.
	// now decrement it.
	// TODO: create a holder for refcounts that automatically decrement
	dec_block_refcount(pe, start_block, ref_reading);
	if (blocks_to_read == 2) dec_block_refcount(pe, start_block + 1, ref_reading);
	return j->d.io.buffer_size;
}

void block_cache::reclaim_block(block_cache_reference const& ref)
{
	cached_piece_entry* pe = find_piece(ref);
	TORRENT_ASSERT(pe);
	if (pe == NULL) return;

	TORRENT_PIECE_ASSERT(pe->in_use, pe);

	TORRENT_PIECE_ASSERT(pe->blocks[ref.block].buf, pe);
	dec_block_refcount(pe, ref.block, block_cache::ref_reading);

	TORRENT_PIECE_ASSERT(m_send_buffer_blocks > 0, pe);
	--m_send_buffer_blocks;

	maybe_free_piece(pe);
}

bool block_cache::maybe_free_piece(cached_piece_entry* pe)
{
	if (!pe->ok_to_evict()
		|| !pe->marked_for_deletion
		|| !pe->jobs.empty())
		return false;

	DLOG(stderr, "[%p] block_cache maybe_free_piece "
		"piece: %d refcount: %d marked_for_deletion: %d\n"
		, static_cast<void*>(this)
		, int(pe->piece), int(pe->refcount), int(pe->marked_for_deletion));

	tailqueue<disk_io_job> jobs;
	bool removed = evict_piece(pe, jobs);
	TORRENT_UNUSED(removed); // suppress warning
	TORRENT_PIECE_ASSERT(removed, pe);
	TORRENT_PIECE_ASSERT(jobs.empty(), pe);

	return true;
}

cached_piece_entry* block_cache::find_piece(block_cache_reference const& ref)
{
	return find_piece(static_cast<piece_manager*>(ref.storage), ref.piece);
}

cached_piece_entry* block_cache::find_piece(disk_io_job const* j)
{
	return find_piece(j->storage.get(), j->piece);
}

cached_piece_entry* block_cache::find_piece(piece_manager* st, int piece)
{
	cached_piece_entry model;
	model.storage = st->shared_from_this();
	model.piece = piece;
	iterator i = m_pieces.find(model);
	TORRENT_ASSERT(i == m_pieces.end() || (i->storage.get() == st && i->piece == piece));
	if (i == m_pieces.end()) return 0;
	TORRENT_PIECE_ASSERT(i->in_use, &*i);

#if TORRENT_USE_ASSERTS
	for (tailqueue_iterator<const disk_io_job> j = i->jobs.iterate(); j.get(); j.next())
	{
		disk_io_job const* job = static_cast<disk_io_job const*>(j.get());
		TORRENT_PIECE_ASSERT(job->piece == piece, &*i);
	}
#endif

	return const_cast<cached_piece_entry*>(&*i);
}

}

