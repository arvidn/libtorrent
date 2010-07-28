/*

Copyright (c) 2008, Arvid Norberg
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

#include "libtorrent/storage.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/thread.hpp"

#include <boost/utility.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"
#include <iostream>
#include <fstream>

using namespace libtorrent;

const int piece_size = 16 * 1024 * 16;
const int block_size = 16 * 1024;

const int half = piece_size / 2;

char* piece0 = page_aligned_allocator::malloc(piece_size);
char* piece1 = page_aligned_allocator::malloc(piece_size);
char* piece2 = page_aligned_allocator::malloc(piece_size);

void on_read_piece(int ret, disk_io_job const& j, char const* data, int size)
{
	std::cerr << "on_read_piece piece: " << j.piece << std::endl;
	TEST_EQUAL(ret, size);
	if (ret > 0) TEST_CHECK(std::equal(j.buffer, j.buffer + ret, data));
}

void on_check_resume_data(int ret, disk_io_job const& j, bool* done)
{
	std::cerr << "on_check_resume_data ret: " << ret;
	switch (ret)
	{
		case 0: std::cerr << " success" << std::endl; break;
		case -1: std::cerr << " need full check" << std::endl; break;
		case -2: std::cerr << " disk error: " << j.str
			<< " file: " << j.error_file << std::endl; break;
		case -3: std::cerr << " aborted" << std::endl; break;
	}
	*done = true;
}

void on_check_files(int ret, disk_io_job const& j, bool* done)
{
	std::cerr << "on_check_files ret: " << ret;

	switch (ret)
	{
		case 0: std::cerr << " done" << std::endl; *done = true; break;
		case -1: std::cerr << " current slot: " << j.piece << " have: " << j.offset << std::endl; break;
		case -2: std::cerr << " disk error: " << j.str
			<< " file: " << j.error_file << std::endl; *done = true; break;
		case -3: std::cerr << " aborted" << std::endl; *done = true; break;
	}
}

void on_read(int ret, disk_io_job const& j, bool* done)
{
	std::cerr << "on_read ret: " << ret << std::endl;
	*done = true;

	if (ret < 0)
	{
		std::cerr << j.error.message() << std::endl;
		std::cerr << j.error_file << std::endl;

	}
}

void on_move_storage(int ret, disk_io_job const& j, std::string path)
{
	std::cerr << "on_move_storage ret: " << ret << " path: " << j.str << std::endl;
	TEST_EQUAL(ret, 0);
	TEST_EQUAL(j.str, path);
}

void print_error(int ret, error_code const& ec)
{
	std::cerr << "returned: " << ret
		<< " error: " << ec.message()
		<< std::endl;
}

namespace libtorrent
{
	int bufs_size(file::iovec_t const* bufs, int num_bufs);
}

// simulate a very slow first read
struct test_storage : storage_interface
{
	test_storage(): m_started(false), m_ready(false) {}

	virtual void initialize(bool allocate_files, error_code& ec) {}
	virtual bool has_any_file(error_code& ec) { return true; }


	int write(
		const char* buf
		, int slot
		, int offset
		, int size
		, error_code& ec)
	{
		return size;
	}

	int read(
		char* buf
		, int slot
		, int offset
		, int size
		, error_code& ec)
	{
		if (slot == 0 || slot == 5999)
		{
			libtorrent::mutex::scoped_lock l(m_mutex);
			std::cerr << "--- starting job " << slot << " waiting for main thread ---\n" << std::endl;
			m_ready = true;
			m_ready_condition.signal(l);

			while (!m_started)
				m_condition.wait(l);

			m_condition.clear(l);
			m_ready_condition.clear(l);
			m_ready = false;
			m_started = false;
			std::cerr << "--- starting ---\n" << std::endl;
		}
		return size;
	}

	void async_readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs
		, boost::function<void(error_code const&, size_t)> const& handler)
	{
		error_code ec;
		int ret = bufs_size(bufs, num_bufs);
		TORRENT_ASSERT(m_disk_io_service);
		m_disk_io_service->post(boost::bind(handler, ec, ret));
	}

	void async_writev(file::iovec_t const* bufs, int slot, int offset, int num_bufs
		, boost::function<void(error_code const&, size_t)> const& handler)
	{
		error_code ec;
		int ret = bufs_size(bufs, num_bufs);
		TORRENT_ASSERT(m_disk_io_service);
		m_disk_io_service->post(boost::bind(handler, ec, ret));
	}

	size_type physical_offset(int slot, int offset)
	{ return slot * 16 * 1024 + offset; }

	virtual int sparse_end(int start) const
	{ return start; }

	virtual void move_storage(std::string const& save_path, error_code& ec) {}

	virtual bool verify_resume_data(lazy_entry const& rd, error_code& error)
	{ return false; }

	virtual void write_resume_data(entry& rd, error_code& ec) const {}
	virtual void move_slot(int src_slot, int dst_slot, error_code& ec) {}
	virtual void swap_slots(int slot1, int slot2, error_code& ec) {}
	virtual void swap_slots3(int slot1, int slot2, int slot3, error_code& ec) {}
	virtual void release_files(error_code& ec) {}
	virtual void rename_file(int index, std::string const& new_filename, error_code& ec) {}
	virtual void delete_files(error_code& ec) {}
	virtual ~test_storage() {}

	void wait_for_ready()
	{
		libtorrent::mutex::scoped_lock l(m_mutex);
		while (!m_ready)
			m_ready_condition.wait(l);
	}

	void start()
	{
		libtorrent::mutex::scoped_lock l(m_mutex);
		m_started = true;
		m_condition.signal(l);
	}

private:
	condition m_ready_condition;
	condition m_condition;
	libtorrent::mutex m_mutex;
	bool m_started;
	bool m_ready;

};

storage_interface* create_test_storage(file_storage const& fs
	, file_storage const* mapped, std::string const& path, file_pool& fp
	, std::vector<boost::uint8_t> const&)
{
	return new test_storage;
}

int job_counter = 0;
// the number of elevator turns
int turns = 0;
int direction = 0;
int last_job = 0;

void callback(int ret, disk_io_job const& j)
{
	if (j.piece > last_job && direction <= 0)
	{
		if (direction == -1)
		{
			++turns;
			std::cerr << " === ELEVATOR TURN dir: " << direction << std::endl;
		}
		direction = 1;
	}
	else if (j.piece < last_job && direction >= 0)
	{
		if (direction == 1)
		{
			++turns;
			std::cerr << " === ELEVATOR TURN dir: " << direction << std::endl;
		}
		direction = -1;
	}
	last_job = j.piece;
	std::cerr << "completed job #" << j.piece << std::endl;
	--job_counter;
}

void add_job(disk_io_thread& dio, int piece, boost::intrusive_ptr<piece_manager>& pm)
{
	disk_io_job j;
	j.action = disk_io_job::read;
	j.storage = pm;
	j.piece = piece;
	j.callback = boost::bind(&callback, _1, _2);
	++job_counter;
	dio.add_job(j);
}

void run_elevator_test()
{
	io_service ios;
	file_pool fp;
	boost::intrusive_ptr<torrent_info> ti = ::create_torrent(0, 16, 6000);

	{
		error_code ec;
		disk_io_thread dio(ios);
		boost::intrusive_ptr<piece_manager> pm(new piece_manager(boost::shared_ptr<void>(), ti, ""
			, dio, &create_test_storage, storage_mode_sparse, std::vector<boost::uint8_t>()));

		// we must disable the read cache in order to
		// verify that the elevator algorithm works.
		// since any read cache hit will circumvent
		// the elevator order
		session_settings set;
		set.use_read_cache = false;
		// if we don't limit this to 1, the order the
		// jobs will complete is undefined since they are
		// all issued asynchronously
		set.max_async_disk_jobs = 1;
		disk_io_job j;
		j.buffer = (char*)&set;
		j.action = disk_io_job::update_settings;
		dio.add_job(j);

		// test the elevator going up
		turns = 0;
		direction = 1;
		last_job = 0;
		add_job(dio, 0, pm); // trigger delay in storage
		// make sure the job is processed
		((test_storage*)pm->get_storage_impl())->wait_for_ready();

		boost::uint32_t p = 1234513;
		for (int i = 0; i < 100; ++i)
		{
			p *= 123;
			int job = (p % 5998) + 1;
			std::cerr << "starting job #" << job << std::endl;
			add_job(dio, job, pm);
		}

		((test_storage*)pm->get_storage_impl())->start();

		for (int i = 0; i < 101; ++i)
		{
			ios.run_one(ec);
			if (ec) std::cerr << "run_one: " << ec.message() << std::endl;
		}

		TEST_CHECK(turns == 0);
		TEST_EQUAL(job_counter, 0);
		std::cerr << "number of elevator turns: " << turns << std::endl;

		// test the elevator going down
		turns = 0;
		direction = -1;
		last_job = 6000;
		add_job(dio, 5999, pm); // trigger delay in storage
		// make sure the job is processed
		((test_storage*)pm->get_storage_impl())->wait_for_ready();

		for (int i = 0; i < 100; ++i)
		{
			p *= 123;
			int job = (p % 5998) + 1;
			std::cerr << "starting job #" << job << std::endl;
			add_job(dio, job, pm);
		}

		((test_storage*)pm->get_storage_impl())->start();

		for (int i = 0; i < 101; ++i)
		{
			ios.run_one(ec);
			if (ec) std::cerr << "run_one: " << ec.message() << std::endl;
		}

		TEST_CHECK(turns == 0);
		TEST_EQUAL(job_counter, 0);
		std::cerr << "number of elevator turns: " << turns << std::endl;

		// test disabling disk-reordering
		set.allow_reordered_disk_operations = false;
		j.buffer = (char*)&set;
		j.action = disk_io_job::update_settings;
		dio.add_job(j);

		turns = 0;
		direction = 0;
		add_job(dio, 0, pm); // trigger delay in storage
		// make sure the job is processed
		((test_storage*)pm->get_storage_impl())->wait_for_ready();

		for (int i = 0; i < 100; ++i)
		{
			p *= 123;
			int job = (p % 5998) + 1;
			std::cerr << "starting job #" << job << std::endl;
			add_job(dio, job, pm);
		}

		((test_storage*)pm->get_storage_impl())->start();

		for (int i = 0; i < 101; ++i)
		{
			ios.run_one(ec);
			if (ec) std::cerr << "run_one: " << ec.message() << std::endl;
		}

		TEST_EQUAL(job_counter, 0);
		std::cerr << "number of elevator turns: " << turns << std::endl;

		// this is not guaranteed, but very very likely
		TEST_CHECK(turns > 20);

		dio.abort();
		dio.join();
	}
}

void run_storage_tests(boost::intrusive_ptr<torrent_info> info
	, file_storage& fs
	, std::string const& test_path
	, libtorrent::storage_mode_t storage_mode
	, bool unbuffered)
{
	TORRENT_ASSERT(fs.num_files() > 0);
	error_code ec;
	create_directory(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cerr << "create_directory: " << ec.message() << std::endl;
	remove_all(combine_path(test_path, "temp_storage2"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;
	remove_all(combine_path(test_path, "part0"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;

	int num_pieces = fs.num_pieces();
	TEST_CHECK(info->num_pieces() == num_pieces);

	session_settings set;
	set.disk_io_write_mode = set.disk_io_read_mode
		= unbuffered ? session_settings::disable_os_cache_for_aligned_files
		: session_settings::enable_os_cache;

	char* piece = page_aligned_allocator::malloc(piece_size);

	{ // avoid having two storages use the same files	
	file_pool fp;
	disk_buffer_pool dp(16 * 1024);
	boost::scoped_ptr<storage_interface> s(
		default_storage_constructor(fs, 0, test_path, fp, std::vector<boost::uint8_t>()));
	s->m_settings = &set;
	s->m_disk_pool = &dp;

	int ret = 0;

	// write piece 1 (in slot 0)
	error_code ec;
	ret = s->write(piece1, 0, 0, half, ec);
	if (ret != half) print_error(ret, ec);
	ret = s->write(piece1 + half, 0, half, half, ec);
	if (ret != half) print_error(ret, ec);

	// test unaligned read (where the bytes are aligned)
	ret = s->read(piece + 3, 0, 3, piece_size-9, ec);
	if (ret != piece_size - 9) print_error(ret, ec);
	TEST_CHECK(std::equal(piece+3, piece + piece_size-9, piece1+3));
	
	// test unaligned read (where the bytes are not aligned)
	ret = s->read(piece, 0, 3, piece_size-9, ec);
	if (ret != piece_size - 9) print_error(ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size-9, piece1+3));

	// verify piece 1
	ret = s->read(piece, 0, 0, piece_size, ec);
	if (ret != piece_size) print_error(ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece1));
	
	// do the same with piece 0 and 2 (in slot 1 and 2)
	ret = s->write(piece0, 1, 0, piece_size, ec);
	if (ret != piece_size) print_error(ret, ec);
	ret = s->write(piece2, 2, 0, piece_size, ec);
	if (ret != piece_size) print_error(ret, ec);

	// verify piece 0 and 2
	ret = s->read(piece, 1, 0, piece_size, ec);
	if (ret != piece_size) print_error(ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece0));

	ret = s->read(piece, 2, 0, piece_size, ec);
	if (ret != piece_size) print_error(ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece2));

	s->release_files(ec);
	}

	// make sure the piece_manager can identify the pieces
	{
	libtorrent::asio::io_service ios;
	disk_io_thread io(ios);
	boost::shared_ptr<int> dummy(new int);
	boost::intrusive_ptr<piece_manager> pm = new piece_manager(dummy, info
		, test_path, io, default_storage_constructor, storage_mode, std::vector<boost::uint8_t>());
	libtorrent::mutex lock;

	error_code ec;
	bool done = false;
	lazy_entry frd;
	pm->async_check_fastresume(&frd, boost::bind(&on_check_resume_data, _1, _2, &done));
	ios.reset();
	while (!done)
	{
		ios.reset();
		ios.run_one(ec);
		if (ec) std::cerr << "run_one: " << ec.message() << std::endl;
	}

	done = false;
	pm->async_check_files(boost::bind(&on_check_files, _1, _2, &done));
	while (!done)
	{
		ios.reset();
		ios.run_one(ec);
		if (ec) std::cerr << "run_one: " << ec.message() << std::endl;
	}

	done = false;
	peer_request r;
	r.piece = 0;
	r.start = 10;
	r.length = 16 * 1024;
	pm->async_read(r, boost::bind(&on_read, _1, _2, &done));
	while (!done)
	{
		ios.reset();
		ios.run_one(ec);
		if (ec) std::cerr << "run_one: " << ec.message() << std::endl;
	}

	// test rename_file
	remove(combine_path(test_path, "part0"), ec);
	if (ec) std::cerr << "remove: " << ec.message() << std::endl;
	TEST_CHECK(exists(combine_path(test_path, "temp_storage/test1.tmp")));
	TEST_CHECK(!exists(combine_path(test_path, "part0")));	
	boost::function<void(int, disk_io_job const&)> none;
	pm->async_rename_file(0, "part0", none);

	test_sleep(1000);
	ios.reset();
	ios.poll(ec);
	if (ec) std::cerr << "poll: " << ec.message() << std::endl;

	TEST_CHECK(!exists(combine_path(test_path, "temp_storage/test1.tmp")));
	TEST_CHECK(!exists(combine_path(test_path, "temp_storage2")));
	TEST_CHECK(exists(combine_path(test_path, "part0")));

	// test move_storage with two files in the root directory
	TEST_CHECK(exists(combine_path(test_path, "temp_storage")));
	pm->async_move_storage(combine_path(test_path, "temp_storage2")
		, boost::bind(on_move_storage, _1, _2, combine_path(test_path, "temp_storage2")));

	test_sleep(2000);
	ios.reset();
	ios.poll(ec);
	if (ec) std::cerr << "poll: " << ec.message() << std::endl;

	if (fs.num_files() > 1)
	{
		TEST_CHECK(!exists(combine_path(test_path, "temp_storage")));
		TEST_CHECK(exists(combine_path(test_path, "temp_storage2/temp_storage")));
	}
	TEST_CHECK(exists(combine_path(test_path, "temp_storage2/part0")));	

	pm->async_move_storage(test_path, boost::bind(on_move_storage, _1, _2, test_path));

	test_sleep(2000);
	ios.reset();
	ios.poll(ec);
	if (ec) std::cerr << "poll: " << ec.message() << std::endl;

	TEST_CHECK(exists(combine_path(test_path, "part0")));	
	TEST_CHECK(!exists(combine_path(test_path, "temp_storage2/temp_storage")));	
	TEST_CHECK(!exists(combine_path(test_path, "temp_storage2/part0")));	

	r.piece = 0;
	r.start = 0;
	r.length = block_size;
	pm->async_read(r, boost::bind(&on_read_piece, _1, _2, piece0, block_size));
	r.piece = 1;
	pm->async_read(r, boost::bind(&on_read_piece, _1, _2, piece1, block_size));
	r.piece = 2;
	pm->async_read(r, boost::bind(&on_read_piece, _1, _2, piece2, block_size));
	pm->async_release_files(none);

	pm->async_rename_file(0, "temp_storage/test1.tmp", none);
	test_sleep(2000);
	ios.reset();
	ios.poll(ec);
	if (ec) std::cerr << "poll: " << ec.message() << std::endl;

	TEST_CHECK(!exists(combine_path(test_path, "part0")));	
	TEST_CHECK(exists(combine_path(test_path, "temp_storage/test1.tmp")));

	ios.reset();
	ios.poll(ec);
	if (ec) std::cerr << "poll: " << ec.message() << std::endl;

	io.abort();
	io.join();
	remove_all(combine_path(test_path, "temp_storage2"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;
	remove_all(combine_path(test_path, "part0"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;
	}
	page_aligned_allocator::free(piece);
}

void test_remove(std::string const& test_path, bool unbuffered)
{
	file_storage fs;
	error_code ec;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;
	TEST_CHECK(!exists(combine_path(test_path, "temp_storage")));	
	fs.add_file("temp_storage/test1.tmp", 8);
	fs.add_file("temp_storage/folder1/test2.tmp", 8);
	fs.add_file("temp_storage/folder2/test3.tmp", 0);
	fs.add_file("temp_storage/_folder3/test4.tmp", 0);
	fs.add_file("temp_storage/_folder3/subfolder/test5.tmp", 8);
	libtorrent::create_torrent t(fs, 4, -1, 0);

	char buf_[4] = {0, 0, 0, 0};
	sha1_hash h = hasher(buf_, 4).final();
	for (int i = 0; i < 6; ++i) t.set_hash(i, h);
	
	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	boost::intrusive_ptr<torrent_info> info(new torrent_info(&buf[0], buf.size(), ec));

	session_settings set;
	set.disk_io_write_mode = set.disk_io_read_mode
		= unbuffered ? session_settings::disable_os_cache_for_aligned_files
		: session_settings::enable_os_cache;

	file_pool fp;
	disk_buffer_pool dp(16 * 1024);
	boost::scoped_ptr<storage_interface> s(
		default_storage_constructor(fs, 0, test_path, fp, std::vector<boost::uint8_t>()));
	s->m_settings = &set;
	s->m_disk_pool = &dp;

	// allocate the files and create the directories
	s->initialize(true, ec);
	if (ec) std::cerr << "initialize: " << ec.message() << std::endl;

	TEST_CHECK(exists(combine_path(test_path, "temp_storage/_folder3/subfolder/test5.tmp")));	
	TEST_CHECK(exists(combine_path(test_path, "temp_storage/folder2/test3.tmp")));	

	s->delete_files(ec);
	if (ec) std::cerr << "delete_files: " << ec.message() << std::endl;

	TEST_CHECK(!exists(combine_path(test_path, "temp_storage")));	
}

namespace
{
	void check_files_fill_array(int ret, disk_io_job const& j, bool* array, bool* done)
	{
		std::cerr << "check_files_fill_array ret: " << ret
			<< " piece: " << j.piece
			<< " str: " << j.str
			<< std::endl;

		if (j.offset >= 0) array[j.offset] = true;
		if (ret != -1)
		{
			*done = true;
			return;
		}
	}
}

void test_check_files(std::string const& test_path
	, libtorrent::storage_mode_t storage_mode
	, bool unbuffered)
{
	boost::intrusive_ptr<torrent_info> info;

	error_code ec;
	const int piece_size = 16 * 1024;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", piece_size);
	fs.add_file("temp_storage/test2.tmp", piece_size * 2);
	fs.add_file("temp_storage/test3.tmp", piece_size);

	char piece0[piece_size];
	char piece2[piece_size];

	std::generate(piece0, piece0 + piece_size, std::rand);
	std::generate(piece2, piece2 + piece_size, std::rand);

	libtorrent::create_torrent t(fs, piece_size, -1, 0);
	t.set_hash(0, hasher(piece0, piece_size).final());
	t.set_hash(1, sha1_hash(0));
	t.set_hash(2, sha1_hash(0));
	t.set_hash(3, hasher(piece2, piece_size).final());

	create_directory(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cerr << "create_directory: " << ec.message() << std::endl;

	std::ofstream f;
	f.open(combine_path(test_path, "temp_storage/test1.tmp").c_str()
		, std::ios::trunc | std::ios::binary);
	f.write(piece0, sizeof(piece0));
	f.close();
	f.open(combine_path(test_path, "temp_storage/test3.tmp").c_str()
		, std::ios::trunc | std::ios::binary);
	f.write(piece2, sizeof(piece2));
	f.close();

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = new torrent_info(&buf[0], buf.size(), ec);

	libtorrent::asio::io_service ios;
	disk_io_thread io(ios);
	boost::shared_ptr<int> dummy(new int);
	boost::intrusive_ptr<piece_manager> pm = new piece_manager(dummy, info
		, test_path, io, default_storage_constructor, storage_mode, std::vector<boost::uint8_t>());
	libtorrent::mutex lock;

	bool done = false;
	lazy_entry frd;
	pm->async_check_fastresume(&frd, boost::bind(&on_check_resume_data, _1, _2, &done));
	ios.reset();
	while (!done)
	{
		ios.reset();
		ios.run_one(ec);
		if (ec) std::cerr << "run_one: " << ec.message() << std::endl;
	}

	bool pieces[4] = {false, false, false, false};
	done = false;

	pm->async_check_files(boost::bind(&check_files_fill_array, _1, _2, pieces, &done));
	while (!done)
	{
		ios.reset();
		ios.run_one(ec);
		if (ec) std::cerr << "run_one: " << ec.message() << std::endl;
	}
	TEST_EQUAL(pieces[0], true);
	TEST_EQUAL(pieces[1], false);
	TEST_EQUAL(pieces[2], false);
	TEST_EQUAL(pieces[3], true);
	io.abort();
	io.join();
}

void run_test(std::string const& test_path, bool unbuffered)
{
	std::cerr << "\n=== " << test_path << " ===\n" << std::endl;

	boost::intrusive_ptr<torrent_info> info;

	{
	error_code ec;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", 17);
	fs.add_file("temp_storage/test2.tmp", 612);
	fs.add_file("temp_storage/test3.tmp", 0);
	fs.add_file("temp_storage/test4.tmp", 0);
	fs.add_file("temp_storage/test5.tmp", 3253);
	fs.add_file("temp_storage/test6.tmp", 841);
	const int last_file_size = 4 * piece_size - fs.total_size();
	fs.add_file("temp_storage/test7.tmp", last_file_size);

	libtorrent::create_torrent t(fs, piece_size, -1, 0);
	t.set_hash(0, hasher(piece0, piece_size).final());
	t.set_hash(1, hasher(piece1, piece_size).final());
	t.set_hash(2, hasher(piece2, piece_size).final());
	
	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = new torrent_info(&buf[0], buf.size(), ec);
	std::cerr << "=== test 1 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_compact, unbuffered);

	// make sure the files have the correct size
	std::string base = combine_path(test_path, "temp_storage");
	TEST_EQUAL(file_size(combine_path(base, "test1.tmp")), 17);
	TEST_EQUAL(file_size(combine_path(base, "test2.tmp")), 612);
	// these files should have been allocated since they are 0 sized
	TEST_CHECK(exists(combine_path(base, "test3.tmp")));
	TEST_CHECK(exists(combine_path(base, "test4.tmp")));
	TEST_EQUAL(file_size(combine_path(base, "test5.tmp")), 3253);
	TEST_EQUAL(file_size(combine_path(base, "test6.tmp")), 841);
	TEST_EQUAL(file_size(combine_path(base, "test7.tmp")), last_file_size - piece_size);
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;
	}

// ==============================================

	{
	error_code ec;
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", 3 * piece_size);
	libtorrent::create_torrent t(fs, piece_size, -1, 0);
	TEST_CHECK(fs.begin()->path == "temp_storage/test1.tmp");
	t.set_hash(0, hasher(piece0, piece_size).final());
	t.set_hash(1, hasher(piece1, piece_size).final());
	t.set_hash(2, hasher(piece2, piece_size).final());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = new torrent_info(&buf[0], buf.size(), ec);

	std::cerr << "=== test 3 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_compact, unbuffered);

	TEST_EQUAL(file_size(combine_path(test_path, "temp_storage/test1.tmp")), piece_size * 3);
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;

// ==============================================

	std::cerr << "=== test 4 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_allocate, unbuffered);

	std::cerr << file_size(combine_path(test_path, "temp_storage/test1.tmp")) << std::endl;
	TEST_EQUAL(file_size(combine_path(test_path, "temp_storage/test1.tmp")), 3 * piece_size);

	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;

	}

// ==============================================

	std::cerr << "=== test 5 ===" << std::endl;
	test_remove(test_path, unbuffered);

// ==============================================

	std::cerr << "=== test 6 ===" << std::endl;
	test_check_files(test_path, storage_mode_sparse, unbuffered);
	test_check_files(test_path, storage_mode_compact, unbuffered);
}

void test_fastresume(std::string const& test_path)
{
	error_code ec;
	std::cout << "\n\n=== test fastresume ===" << std::endl;
	remove_all(combine_path(test_path, "tmp1"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;
	create_directory(combine_path(test_path, "tmp1"), ec);
	if (ec) std::cerr << "create_directory: " << ec.message() << std::endl;
	std::ofstream file(combine_path(test_path, "tmp1/temporary").c_str());
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file);
	file.close();
	TEST_CHECK(exists(combine_path(test_path, "tmp1/temporary")));

	entry resume;
	{
		session ses(fingerprint("  ", 0,0,0,0), 0);
		ses.set_alert_mask(alert::all_categories);

		error_code ec;

		add_torrent_params p;
		p.ti = new torrent_info(*t);
		p.save_path = combine_path(test_path, "tmp1");
		p.storage_mode = storage_mode_compact;
		torrent_handle h = ses.add_torrent(p, ec);
				
		for (int i = 0; i < 10; ++i)
		{
			print_alerts(ses, "ses");
			test_sleep(1000);
			torrent_status s = h.status();
			if (s.progress == 1.0f) 
			{
				std::cout << "progress: 1.0f" << std::endl;
				break;
			}
		}
		resume = h.write_resume_data();
		ses.remove_torrent(h, session::delete_files);
	}
	TEST_CHECK(!exists(combine_path(test_path, "tmp1/temporary")));
#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
	resume.print(std::cout);
#endif

	// make sure the fast resume check fails! since we removed the file
	{
		session ses(fingerprint("  ", 0,0,0,0), 0);
		ses.set_alert_mask(alert::all_categories);

		add_torrent_params p;
		p.ti = new torrent_info(*t);
		p.save_path = combine_path(test_path, "tmp1");
		p.storage_mode = storage_mode_compact;
		std::vector<char> resume_buf;
		bencode(std::back_inserter(resume_buf), resume);
		p.resume_data = &resume_buf;
		torrent_handle h = ses.add_torrent(p, ec);
	
		std::auto_ptr<alert> a = ses.pop_alert();
		ptime end = time_now() + seconds(20);
		while (time_now() < end
			&& (a.get() == 0
				|| dynamic_cast<fastresume_rejected_alert*>(a.get()) == 0))
		{
			if (ses.wait_for_alert(end - time_now()) == 0)
			{
				std::cerr << "wait_for_alert() expired" << std::endl;
				break;
			}
			a = ses.pop_alert();
			assert(a.get());
			std::cerr << a->message() << std::endl;
		}
		TEST_CHECK(dynamic_cast<fastresume_rejected_alert*>(a.get()) != 0);
	}
	remove_all(combine_path(test_path, "tmp1"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;
}

bool got_file_rename_alert(alert* a)
{
	return dynamic_cast<libtorrent::file_renamed_alert*>(a)
		|| dynamic_cast<libtorrent::file_rename_failed_alert*>(a);
}

void test_rename_file_in_fastresume(std::string const& test_path)
{
	error_code ec;
	std::cout << "\n\n=== test rename file in fastresume ===" << std::endl;
	remove_all(combine_path(test_path, "tmp2"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;
	create_directory(combine_path(test_path, "tmp2"), ec);
	if (ec) std::cerr << "create_directory: " << ec.message() << std::endl;
	std::ofstream file(combine_path(test_path, "tmp2/temporary").c_str());
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file);
	file.close();
	TEST_CHECK(exists(combine_path(test_path, "tmp2/temporary")));

	entry resume;
	{
		session ses(fingerprint("  ", 0,0,0,0), 0);
		ses.set_alert_mask(alert::all_categories);


		add_torrent_params p;
		p.ti = new torrent_info(*t);
		p.save_path = combine_path(test_path, "tmp2");
		p.storage_mode = storage_mode_compact;
		torrent_handle h = ses.add_torrent(p, ec);

		h.rename_file(0, "testing_renamed_files");
		std::cout << "renaming file" << std::endl;
		bool renamed = false;
		for (int i = 0; i < 5; ++i)
		{
			if (print_alerts(ses, "ses", true, true, true, &got_file_rename_alert)) renamed = true;
			test_sleep(1000);
			torrent_status s = h.status();
			if (s.state == torrent_status::downloading) break;
			if (s.state == torrent_status::seeding && renamed) break;
		}
		std::cout << "stop loop" << std::endl;
		torrent_status s = h.status();
		TEST_CHECK(s.state == torrent_status::seeding);
		resume = h.write_resume_data();
		ses.remove_torrent(h);
	}
	TEST_CHECK(!exists(combine_path(test_path, "tmp2/temporary")));
	TEST_CHECK(exists(combine_path(test_path, "tmp2/testing_renamed_files")));
	TEST_CHECK(resume.dict().find("mapped_files") != resume.dict().end());
#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
	resume.print(std::cout);
#endif

	// make sure the fast resume check succeeds, even though we renamed the file
	{
		session ses(fingerprint("  ", 0,0,0,0), 0);
		ses.set_alert_mask(alert::all_categories);

		add_torrent_params p;
		p.ti = new torrent_info(*t);
		p.save_path = combine_path(test_path, "tmp2");
		p.storage_mode = storage_mode_compact;
		std::vector<char> resume_buf;
		bencode(std::back_inserter(resume_buf), resume);
		p.resume_data = &resume_buf;
		torrent_handle h = ses.add_torrent(p, ec);

		for (int i = 0; i < 5; ++i)
		{
			print_alerts(ses, "ses");
			test_sleep(1000);
		}
		torrent_status stat = h.status();
		TEST_CHECK(stat.state == torrent_status::seeding);

		resume = h.write_resume_data();
		ses.remove_torrent(h);
	}
	TEST_CHECK(resume.dict().find("mapped_files") != resume.dict().end());
#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
	resume.print(std::cout);
#endif
	remove_all(combine_path(test_path, "tmp2"), ec);
	if (ec) std::cerr << "remove_all: " << ec.message() << std::endl;
}

int test_main()
{

	run_elevator_test();

	// initialize test pieces
	for (char* p = piece0, *end(piece0 + piece_size); p < end; ++p)
		*p = rand();
	for (char* p = piece1, *end(piece1 + piece_size); p < end; ++p)
		*p = rand();
	for (char* p = piece2, *end(piece2 + piece_size); p < end; ++p)
		*p = rand();

	std::vector<std::string> test_paths;
	char* env = std::getenv("TORRENT_TEST_PATHS");
	if (env == 0)
	{
		test_paths.push_back(current_working_directory());
	}
	else
	{
		char* p = std::strtok(env, ";");
		while (p != 0)
		{
			test_paths.push_back(complete(p));
			p = std::strtok(0, ";");
		}
	}

	std::for_each(test_paths.begin(), test_paths.end(), boost::bind(&test_fastresume, _1));
	std::for_each(test_paths.begin(), test_paths.end(), boost::bind(&test_rename_file_in_fastresume, _1));
	std::for_each(test_paths.begin(), test_paths.end(), boost::bind(&run_test, _1, true));
	std::for_each(test_paths.begin(), test_paths.end(), boost::bind(&run_test, _1, false));

	return 0;
}

