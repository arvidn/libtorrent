/*

Copyright (c) 2012, Arvid Norberg
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

#include "test.hpp"
#include "libtorrent/block_cache.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/aux_/path.hpp" // for bufs_size

#include <functional>
#include <memory>

using namespace lt;

namespace {

struct test_storage_impl : storage_interface
{
	explicit test_storage_impl(file_storage const& fs) : storage_interface(fs) {}
	void initialize(storage_error&) override {}

	int readv(span<iovec_t const> bufs
		, piece_index_t, int /*offset*/, open_mode_t, storage_error&) override
	{
		return bufs_size(bufs);
	}
	int writev(span<iovec_t const> bufs
		, piece_index_t, int /*offset*/, open_mode_t, storage_error&) override
	{
		return bufs_size(bufs);
	}

	bool has_any_file(storage_error&) override { return false; }
	void set_file_priority(aux::vector<download_priority_t, file_index_t>&
		, storage_error&) override {}
	status_t move_storage(std::string const&, move_flags_t
		, storage_error&) override { return status_t::no_error; }
	bool verify_resume_data(add_torrent_params const&
		, aux::vector<std::string, file_index_t> const&
		, storage_error&) override { return true; }
	void release_files(storage_error&) override {}
	void rename_file(file_index_t, std::string const&
		, storage_error&) override {}
	void delete_files(remove_flags_t, storage_error&) override {}
};

struct allocator : buffer_allocator_interface
{
	allocator(block_cache& bc, storage_interface* st)
		: m_cache(bc), m_storage(st) {}

	void free_disk_buffer(char* b) override
	{ m_cache.free_buffer(b); }

	void reclaim_blocks(span<aux::block_cache_reference> refs) override
	{
		for (auto ref : refs)
			m_cache.reclaim_block(m_storage, ref);
	}

	virtual ~allocator() = default;
private:
	block_cache& m_cache;
	storage_interface* m_storage;
};

static void nop() {}

#if TORRENT_USE_ASSERTS
#define INITIALIZE_JOB(j) j.in_use = true;
#else
#define INITIALIZE_JOB(j)
#endif

#define TEST_SETUP \
	io_service ios; \
	block_cache bc(ios, std::bind(&nop)); \
	aux::session_settings sett; \
	file_storage fs; \
	fs.add_file("a/test0", 0x4000); \
	fs.add_file("a/test1", 0x4000); \
	fs.add_file("a/test2", 0x4000); \
	fs.add_file("a/test3", 0x4000); \
	fs.add_file("a/test4", 0x4000); \
	fs.add_file("a/test5", 0x4000); \
	fs.add_file("a/test6", 0x4000); \
	fs.add_file("a/test7", 0x4000); \
	fs.set_piece_length(0x8000); \
	fs.set_num_pieces(5); \
	std::shared_ptr<storage_interface> pm \
		= std::make_shared<test_storage_impl>(fs); \
	allocator alloc(bc, pm.get()); \
	bc.set_settings(sett); \
	pm->m_settings = &sett; \
	disk_io_job rj; \
	disk_io_job wj; \
	INITIALIZE_JOB(rj) \
	INITIALIZE_JOB(wj) \
	rj.storage = pm; \
	wj.storage = pm; \
	cached_piece_entry* pe = nullptr; \
	int ret = 0; \
	iovec_t iov; \
	(void)iov; \
	(void)ret; \
	(void)pe

#define WRITE_BLOCK(p, b) \
	wj.flags = disk_io_job::in_progress; \
	wj.action = job_action_t::write; \
	wj.d.io.offset = (b) * 0x4000; \
	wj.d.io.buffer_size = 0x4000; \
	wj.piece = piece_index_t(p); \
	wj.argument = disk_buffer_holder(alloc, bc.allocate_buffer("write-test"), 0x4000); \
	pe = bc.add_dirty_block(&wj, true)

#define READ_BLOCK(p, b, r) \
	rj.action = job_action_t::read; \
	rj.d.io.offset = (b) * 0x4000; \
	rj.d.io.buffer_size = 0x4000; \
	rj.piece = piece_index_t(p); \
	rj.storage = pm; \
	rj.argument = disk_buffer_holder(alloc, nullptr, 0); \
	ret = bc.try_read(&rj, alloc)

#define FLUSH(flushing) \
	for (int i = 0; i < int(sizeof(flushing)/sizeof((flushing)[0])); ++i) \
	{ \
		pe->blocks[(flushing)[i]].pending = true; \
		bc.inc_block_refcount(pe, 0, block_cache::ref_flushing); \
	} \
	bc.blocks_flushed(pe, flushing, sizeof(flushing)/sizeof((flushing)[0]))

#define INSERT(p, b) \
	wj.piece = piece_index_t(p); \
	pe = bc.allocate_piece(&wj, cached_piece_entry::read_lru1); \
	ret = bc.allocate_iovec(iov); \
	TEST_EQUAL(ret, 0); \
	bc.insert_blocks(pe, b, iov, &wj)

void test_write()
{
	TEST_SETUP;

	// write block (0,0)
	WRITE_BLOCK(0, 0);

	counters c;
	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::write_cache_blocks], 1);
	TEST_EQUAL(c[counters::read_cache_blocks], 0);
	TEST_EQUAL(c[counters::pinned_blocks], 0);
	TEST_EQUAL(c[counters::arc_mru_size], 0);
	TEST_EQUAL(c[counters::arc_mru_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_write_size], 1);
	TEST_EQUAL(c[counters::arc_volatile_size], 0);

	// try to read it back
	READ_BLOCK(0, 0, 1);
	TEST_EQUAL(bc.pinned_blocks(), 1);
	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::pinned_blocks], 1);

	// it's supposed to be a cache hit
	TEST_CHECK(ret >= 0);

	// return the reference to the buffer we just read
	rj.argument = remove_flags_t{};

	TEST_EQUAL(bc.pinned_blocks(), 0);
	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::pinned_blocks], 0);

	// try to read block (1, 0)
	READ_BLOCK(1, 0, 1);

	// that's supposed to be a cache miss
	TEST_CHECK(ret < 0);
	TEST_EQUAL(bc.pinned_blocks(), 0);
	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::pinned_blocks], 0);

	rj.argument = remove_flags_t{};

	tailqueue<disk_io_job> jobs;
	bc.clear(jobs);
}

void test_flush()
{
	TEST_SETUP;

	// write block (0,0)
	WRITE_BLOCK(0, 0);

	// pretend to flush to disk
	int flushing[1] = {0};
	FLUSH(flushing);

	tailqueue<disk_io_job> jobs;
	bc.clear(jobs);
}

void test_insert()
{
	TEST_SETUP;

	INSERT(0, 0);

	counters c;
	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::write_cache_blocks], 0);
	TEST_EQUAL(c[counters::read_cache_blocks], 1);
	TEST_EQUAL(c[counters::pinned_blocks], 0);
	TEST_EQUAL(c[counters::arc_mru_size], 1);
	TEST_EQUAL(c[counters::arc_mru_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_write_size], 0);
	TEST_EQUAL(c[counters::arc_volatile_size], 0);

	tailqueue<disk_io_job> jobs;
	bc.clear(jobs);
}

void test_evict()
{
	TEST_SETUP;

	INSERT(0, 0);

	counters c;
	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::write_cache_blocks], 0);
	TEST_EQUAL(c[counters::read_cache_blocks], 1);
	TEST_EQUAL(c[counters::pinned_blocks], 0);
	TEST_EQUAL(c[counters::arc_mru_size], 1);
	TEST_EQUAL(c[counters::arc_mru_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_write_size], 0);
	TEST_EQUAL(c[counters::arc_volatile_size], 0);

	tailqueue<disk_io_job> jobs;
	// this should make it not be evicted
	// just free the buffers
	++pe->piece_refcount;
	bc.evict_piece(pe, jobs, block_cache::allow_ghost);

	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::write_cache_blocks], 0);
	TEST_EQUAL(c[counters::read_cache_blocks], 0);
	TEST_EQUAL(c[counters::pinned_blocks], 0);
	TEST_EQUAL(c[counters::arc_mru_size], 1);
	TEST_EQUAL(c[counters::arc_mru_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_write_size], 0);
	TEST_EQUAL(c[counters::arc_volatile_size], 0);

	--pe->piece_refcount;
	bc.evict_piece(pe, jobs, block_cache::allow_ghost);

	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::write_cache_blocks], 0);
	TEST_EQUAL(c[counters::read_cache_blocks], 0);
	TEST_EQUAL(c[counters::pinned_blocks], 0);
	TEST_EQUAL(c[counters::arc_mru_size], 0);
	TEST_EQUAL(c[counters::arc_mru_ghost_size], 1);
	TEST_EQUAL(c[counters::arc_mfu_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_write_size], 0);
	TEST_EQUAL(c[counters::arc_volatile_size], 0);

	bc.clear(jobs);
}

// test to have two different requestors read a block and
// make sure it moves into the MFU list
void test_arc_promote()
{
	TEST_SETUP;

	INSERT(0, 0);

	counters c;
	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::write_cache_blocks], 0);
	TEST_EQUAL(c[counters::read_cache_blocks], 1);
	TEST_EQUAL(c[counters::pinned_blocks], 0);
	TEST_EQUAL(c[counters::arc_mru_size], 1);
	TEST_EQUAL(c[counters::arc_mru_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_write_size], 0);
	TEST_EQUAL(c[counters::arc_volatile_size], 0);

	READ_BLOCK(0, 0, 1);
	TEST_EQUAL(bc.pinned_blocks(), 1);
	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::pinned_blocks], 1);

	// it's supposed to be a cache hit
	TEST_CHECK(ret >= 0);
	// return the reference to the buffer we just read
	rj.argument = remove_flags_t{};

	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::write_cache_blocks], 0);
	TEST_EQUAL(c[counters::read_cache_blocks], 1);
	TEST_EQUAL(c[counters::pinned_blocks], 0);
	TEST_EQUAL(c[counters::arc_mru_size], 1);
	TEST_EQUAL(c[counters::arc_mru_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_write_size], 0);
	TEST_EQUAL(c[counters::arc_volatile_size], 0);

	READ_BLOCK(0, 0, 2);
	TEST_EQUAL(bc.pinned_blocks(), 1);
	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::pinned_blocks], 1);

	// it's supposed to be a cache hit
	TEST_CHECK(ret >= 0);
	// return the reference to the buffer we just read
	rj.argument = remove_flags_t{};

	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::write_cache_blocks], 0);
	TEST_EQUAL(c[counters::read_cache_blocks], 1);
	TEST_EQUAL(c[counters::pinned_blocks], 0);
	TEST_EQUAL(c[counters::arc_mru_size], 0);
	TEST_EQUAL(c[counters::arc_mru_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_size], 1);
	TEST_EQUAL(c[counters::arc_mfu_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_write_size], 0);
	TEST_EQUAL(c[counters::arc_volatile_size], 0);

	tailqueue<disk_io_job> jobs;
	bc.clear(jobs);
}

void test_arc_unghost()
{
	TEST_SETUP;

	INSERT(0, 0);

	counters c;
	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::write_cache_blocks], 0);
	TEST_EQUAL(c[counters::read_cache_blocks], 1);
	TEST_EQUAL(c[counters::pinned_blocks], 0);
	TEST_EQUAL(c[counters::arc_mru_size], 1);
	TEST_EQUAL(c[counters::arc_mru_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_write_size], 0);
	TEST_EQUAL(c[counters::arc_volatile_size], 0);

	tailqueue<disk_io_job> jobs;
	bc.evict_piece(pe, jobs, block_cache::allow_ghost);

	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::write_cache_blocks], 0);
	TEST_EQUAL(c[counters::read_cache_blocks], 0);
	TEST_EQUAL(c[counters::pinned_blocks], 0);
	TEST_EQUAL(c[counters::arc_mru_size], 0);
	TEST_EQUAL(c[counters::arc_mru_ghost_size], 1);
	TEST_EQUAL(c[counters::arc_mfu_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_write_size], 0);
	TEST_EQUAL(c[counters::arc_volatile_size], 0);

	// the block is now a ghost. If we cache-hit it,
	// it should be promoted back to the main list
	bc.cache_hit(pe, 0, false);

	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::write_cache_blocks], 0);
	// we didn't actually read in any blocks, so the cache size
	// is still 0
	TEST_EQUAL(c[counters::read_cache_blocks], 0);
	TEST_EQUAL(c[counters::pinned_blocks], 0);
	TEST_EQUAL(c[counters::arc_mru_size], 1);
	TEST_EQUAL(c[counters::arc_mru_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_size], 0);
	TEST_EQUAL(c[counters::arc_mfu_ghost_size], 0);
	TEST_EQUAL(c[counters::arc_write_size], 0);
	TEST_EQUAL(c[counters::arc_volatile_size], 0);

	bc.clear(jobs);
}

void test_iovec()
{
	TEST_SETUP;

	ret = bc.allocate_iovec(iov);
	TEST_EQUAL(ret, 0);
	bc.free_iovec(iov);
}

void test_unaligned_read()
{
	TEST_SETUP;

	INSERT(0, 0);
	INSERT(0, 1);

	rj.action = job_action_t::read;
	rj.d.io.offset = 0x2000;
	rj.d.io.buffer_size = 0x4000;
	rj.piece = piece_index_t(0);
	rj.storage = pm;
	rj.argument = disk_buffer_holder(alloc, nullptr, 0);
	ret = bc.try_read(&rj, alloc);

	// unaligned reads copies the data into a new buffer
	// rather than
	TEST_EQUAL(bc.pinned_blocks(), 0);
	counters c;
	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::pinned_blocks], 0);

	// it's supposed to be a cache hit
	TEST_CHECK(ret >= 0);
	// return the reference to the buffer we just read
	rj.argument = remove_flags_t{};

	tailqueue<disk_io_job> jobs;
	bc.clear(jobs);
}

} // anonymous namespace

TORRENT_TEST(block_cache)
{
	test_write();
	test_flush();
	test_insert();
	test_evict();
	test_arc_promote();
	test_arc_unghost();
	test_iovec();
	test_unaligned_read();

	// TODO: test try_evict_blocks
	// TODO: test evicting volatile pieces, to see them be removed
	// TODO: test evicting dirty pieces
	// TODO: test free_piece
	// TODO: test abort_dirty
	// TODO: test unaligned reads
}

TORRENT_TEST(delete_piece)
{
	TEST_SETUP;

	TEST_CHECK(bc.num_pieces() == 0);

	INSERT(0, 0);

	TEST_CHECK(bc.num_pieces() == 1);

	rj.action = job_action_t::read;
	rj.d.io.offset = 0x2000;
	rj.d.io.buffer_size = 0x4000;
	rj.piece = piece_index_t(0);
	rj.storage = pm;
	rj.argument = remove_flags_t{};
	ret = bc.try_read(&rj, alloc);
	TEST_EQUAL(ret, -1);

	cached_piece_entry* pe_ = bc.find_piece(pm.get(), piece_index_t(0));
	bc.mark_for_eviction(pe_, block_cache::disallow_ghost);

	TEST_CHECK(bc.num_pieces() == 0);
}
