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
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/alert_dispatcher.hpp"

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
	virtual void set_file_priority(std::vector<boost::uint8_t> const& prio, storage_error& ec) {}
	virtual void move_storage(std::string const& save_path, storage_error& ec) {}
	virtual bool verify_resume_data(lazy_entry const& rd, storage_error& ec) { return true; }
	virtual void write_resume_data(entry& rd, storage_error& ec) const {}
	virtual void release_files(storage_error& ec) {}
	virtual void rename_file(int index, std::string const& new_filenamem, storage_error& ec) {}
	virtual void delete_files(storage_error& ec) {}
	virtual void finalize_file(int, storage_error&) {}
};

int test_main()
{
	io_service ios;
	print_alert ad;
	block_cache bc(0x4000, ios, &ad);

	aux::session_settings sett;

	file_storage fs;
	fs.add_file("a/test1", 0x4000);
	fs.add_file("a/test2", 0x4000);
	fs.add_file("a/test3", 0x4000);
	fs.set_piece_length(0x4000);
	fs.set_num_pieces(3);

	test_storage_impl* st = new test_storage_impl;
	boost::intrusive_ptr<piece_manager> pm(new piece_manager(st, boost::shared_ptr<int>(new int), &fs));

	bc.set_settings(sett);
	st->m_settings = &sett;

	disk_io_job j;
	j.flags = disk_io_job::in_progress;
	j.action = disk_io_job::write;
	j.d.io.offset = 0;
	j.d.io.buffer_size = 0x4000;
	j.piece = 0;
	j.storage = pm;
	j.buffer = bc.allocate_buffer("write-test");

	cached_piece_entry* pe = bc.add_dirty_block(&j);

	j.action = disk_io_job::read;
	j.d.io.offset = 0;
	j.d.io.buffer_size = 0x4000;
	j.piece = 0;
	j.storage = pm;
	j.buffer = 0;

	int ret = bc.try_read(&j);
	// cache hit
	TEST_CHECK(ret >= 0);

	if (j.d.io.ref.storage) bc.reclaim_block(j.d.io.ref);
	else if (j.buffer) bc.free_buffer(j.buffer);

	j.d.io.ref.storage = 0;
	j.piece = 1;
	j.buffer = 0;

	ret = bc.try_read(&j);
	// cache miss
	TEST_CHECK(ret < 0);

	if (j.d.io.ref.storage) bc.reclaim_block(j.d.io.ref);
	else if (j.buffer) bc.free_buffer(j.buffer);
	j.d.io.ref.storage = 0;

	int flushing[1];
	flushing[0] = 0;
	pe->blocks[0].pending = true;
	bc.inc_block_refcount(pe, 0, block_cache::ref_flushing);
	bc.blocks_flushed(pe, flushing, 1);

	tailqueue jobs;
	bc.clear(jobs);

	return 0;
}

