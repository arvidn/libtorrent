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
#include "libtorrent/alert_dispatcher.hpp"
#include "libtorrent/session.hpp"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

using namespace libtorrent;

struct print_alert : alert_dispatcher
{
	virtual bool post_alert(alert* a)
	{
		fprintf(stderr, "ALERT: %s\n", a->message().c_str());
		delete a;
		return true;
	}
};

struct test_storage_impl : storage_interface
{
	virtual void initialize(storage_error& ec) {}

	virtual int readv(file::iovec_t const* bufs, int num_bufs
		, int piece, int offset, int flags, storage_error& ec)
	{
		return bufs_size(bufs, num_bufs);
	}
	virtual int writev(file::iovec_t const* bufs, int num_bufs
		, int piece, int offset, int flags, storage_error& ec)
	{
		return bufs_size(bufs, num_bufs);
	}

	virtual bool has_any_file(storage_error& ec) { return false; }
	virtual void set_file_priority(std::vector<boost::uint8_t> const& prio
		, storage_error& ec) {}
	virtual int move_storage(std::string const& save_path, int flags, storage_error& ec)
	{ return 0; }
	virtual bool verify_resume_data(bdecode_node const& rd, storage_error& ec)
	{ return true; }
	virtual void write_resume_data(entry& rd, storage_error& ec) const {}
	virtual void release_files(storage_error& ec) {}
	virtual void rename_file(int index, std::string const& new_filenamem
		, storage_error& ec) {}
	virtual void delete_files(storage_error& ec) {}
	virtual void finalize_file(int, storage_error&) {}
};

void nop() {}

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
#define INITIALIZE_JOB(j) j.in_use = true;
#else
#define INITIALIZE_JOB(j) 
#endif

#define TEST_SETUP \
	io_service ios; \
	print_alert ad; \
	block_cache bc(0x4000, ios, boost::bind(&nop), &ad); \
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
	test_storage_impl* st = new test_storage_impl; \
	boost::shared_ptr<piece_manager> pm(boost::make_shared<piece_manager>(st, boost::shared_ptr<int>(new int), &fs)); \
	bc.set_settings(sett); \
	st->m_settings = &sett; \
	disk_io_job rj; \
	disk_io_job wj; \
	INITIALIZE_JOB(rj) \
	INITIALIZE_JOB(wj) \
	rj.storage = pm; \
	wj.storage = pm; \
	cached_piece_entry* pe = NULL; \
	int ret = 0; \
	file::iovec_t iov[1]; \
	(void)iov[0]; \
	(void)ret; \
	(void)pe

#define WRITE_BLOCK(p, b) \
	wj.flags = disk_io_job::in_progress; \
	wj.action = disk_io_job::write; \
	wj.d.io.offset = b * 0x4000; \
	wj.d.io.buffer_size = 0x4000; \
	wj.piece = p; \
	wj.buffer = bc.allocate_buffer("write-test"); \
	pe = bc.add_dirty_block(&wj)

#define READ_BLOCK(p, b, r) \
	rj.action = disk_io_job::read; \
	rj.d.io.offset = b * 0x4000; \
	rj.d.io.buffer_size = 0x4000; \
	rj.piece = p; \
	rj.storage = pm; \
	rj.requester = (void*)r; \
	rj.buffer = 0; \
	ret = bc.try_read(&rj)

#define RETURN_BUFFER \
	if (rj.d.io.ref.storage) bc.reclaim_block(rj.d.io.ref); \
	else if (rj.buffer) bc.free_buffer(rj.buffer); \
	rj.d.io.ref.storage = 0

#define FLUSH(flushing) \
	for (int i = 0; i < sizeof(flushing)/sizeof(flushing[0]); ++i) \
	{ \
		pe->blocks[flushing[i]].pending = true; \
		bc.inc_block_refcount(pe, 0, block_cache::ref_flushing); \
	} \
	bc.blocks_flushed(pe, flushing, sizeof(flushing)/sizeof(flushing[0]))

#define INSERT(p, b) \
	wj.piece = p; \
	wj.requester = (void*)1; \
	pe = bc.allocate_piece(&wj, cached_piece_entry::read_lru1); \
	ret = bc.allocate_iovec(iov, 1); \
	TEST_EQUAL(ret, 0); \
	bc.insert_blocks(pe, b, iov, 1, &wj)

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
	RETURN_BUFFER;
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

	// just in case it wasn't we're supposed to return the reference
	// to the buffer
	RETURN_BUFFER;

	tailqueue jobs;
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

	tailqueue jobs;
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

	tailqueue jobs;
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

	tailqueue jobs;
	// this should make it not be evicted
	// just free the buffers
	++pe->piece_refcount;
	bc.evict_piece(pe, jobs);

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
	bc.evict_piece(pe, jobs);

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
	RETURN_BUFFER;

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
	RETURN_BUFFER;

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

	tailqueue jobs;
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

	tailqueue jobs;
	bc.evict_piece(pe, jobs);

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
	bc.cache_hit(pe, (void*)1, false);

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

	ret = bc.allocate_iovec(iov, 1);
	bc.free_iovec(iov, 1);
}

void test_unaligned_read()
{
	TEST_SETUP;

	INSERT(0, 0);
	INSERT(0, 1);

	rj.action = disk_io_job::read;
	rj.d.io.offset = 0x2000;
	rj.d.io.buffer_size = 0x4000;
	rj.piece = 0;
	rj.storage = pm;
	rj.requester = (void*)1;
	rj.buffer = 0;
	ret = bc.try_read(&rj);

	// unaligned reads copies the data into a new buffer
	// rather than
	TEST_EQUAL(bc.pinned_blocks(), 0);
	counters c;
	bc.update_stats_counters(c);
	TEST_EQUAL(c[counters::pinned_blocks], 0);

	// it's supposed to be a cache hit
	TEST_CHECK(ret >= 0);
	// return the reference to the buffer we just read
	RETURN_BUFFER;

	tailqueue jobs;
	bc.clear(jobs);
}

int test_main()
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
	return 0;
}

