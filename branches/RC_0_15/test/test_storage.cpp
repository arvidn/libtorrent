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

#include <boost/utility.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/thread/mutex.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;
using namespace boost::filesystem;

const int piece_size = 16 * 1024 * 16;
const int block_size = 16 * 1024;

const int half = piece_size / 2;

char* piece0 = page_aligned_allocator::malloc(piece_size);
char* piece1 = page_aligned_allocator::malloc(piece_size);
char* piece2 = page_aligned_allocator::malloc(piece_size);

void signal_bool(bool* b, char const* string)
{
	*b = true;
	std::cerr << string << std::endl;
}

void on_read_piece(int ret, disk_io_job const& j, char const* data, int size)
{
	std::cerr << "on_read_piece piece: " << j.piece << std::endl;
	TEST_CHECK(ret == size);
	if (ret > 0) TEST_CHECK(std::equal(j.buffer, j.buffer + ret, data));
}

void on_check_resume_data(int ret, disk_io_job const& j, bool* done)
{
	std::cerr << "on_check_resume_data ret: " << ret;
	switch (ret)
	{
		case piece_manager::no_error:
			std::cerr << " success" << std::endl;
			break;
		case piece_manager::fatal_disk_error:
			std::cerr << " disk error: " << j.str
				<< " file: " << j.error_file << std::endl;
			break;
		case piece_manager::need_full_check:
			std::cerr << " need full check" << std::endl;
			break;
		case piece_manager::disk_check_aborted:
			std::cerr << " aborted" << std::endl;
			break;
	}
	*done = true;
}

void on_check_files(int ret, disk_io_job const& j, bool* done)
{
	std::cerr << "on_check_files ret: " << ret;

	switch (ret)
	{
		case piece_manager::no_error:
			std::cerr << " done" << std::endl;
			*done = true;
			break;
		case piece_manager::fatal_disk_error:
			std::cerr << " disk error: " << j.str
				<< " file: " << j.error_file << std::endl;
			*done = true;
			break;
		case piece_manager::need_full_check:
			std::cerr << " current slot: " << j.piece
				<< " have: " << j.offset << std::endl;
			break;
		case piece_manager::disk_check_aborted:
			std::cerr << " aborted" << std::endl;
			*done = true;
			break;
	}
}

void on_read(int ret, disk_io_job const& j, bool* done)
{
	std::cerr << "on_read ret: " << ret;
	*done = true;

	if (ret < 0)
	{
		std::cerr << j.error.message() << std::endl;
		std::cerr << j.error_file << std::endl;

	}
}

void on_move_storage(int ret, bool* done, disk_io_job const& j, std::string path)
{
	std::cerr << "on_move_storage ret: " << ret << " path: " << j.str << std::endl;
	TEST_CHECK(ret == 0);
	TEST_CHECK(j.str == path);
	*done = true;
}

void print_error(int ret, boost::scoped_ptr<storage_interface> const& s)
{
	std::cerr << "returned: " << ret
		<< " error: " << s->error().message()
		<< " file: " << s->error_file()
		<< std::endl;
}

int bufs_size(file::iovec_t const* bufs, int num_bufs);

// simulate a very slow first read
struct test_storage : storage_interface
{
	test_storage(): m_started(false), m_ready(false) {}

	virtual bool initialize(bool allocate_files) { return true; }
	virtual bool has_any_file() { return true; }


	int write(
		const char* buf
		, int slot
		, int offset
		, int size)
	{
		return size;
	}

	int read(
		char* buf
		, int slot
		, int offset
		, int size)
	{
		if (slot == 0 || slot == 5999)
		{
			boost::mutex::scoped_lock l(m_mutex);
			std::cerr << "--- starting job " << slot << " waiting for main thread ---\n" << std::endl;
			m_ready = true;
			m_ready_condition.notify_all();

			while (!m_started)
				m_condition.wait(l);

			m_ready = false;
			m_started = false;
			std::cerr << "--- starting ---\n" << std::endl;
		}
		return size;
	}

	size_type physical_offset(int slot, int offset)
	{ return slot * 16 * 1024 + offset; }

	virtual int sparse_end(int start) const
	{ return start; }

	virtual bool move_storage(fs::path save_path)
	{ return false; }

	virtual bool verify_resume_data(lazy_entry const& rd, error_code& error)
	{ return false; }

	virtual bool write_resume_data(entry& rd) const
	{ return false; }

	virtual bool move_slot(int src_slot, int dst_slot)
	{ return false; }

	virtual bool swap_slots(int slot1, int slot2)
	{ return false; }

	virtual bool swap_slots3(int slot1, int slot2, int slot3)
	{ return false; }

	virtual bool release_files() { return false; }

	virtual bool rename_file(int index, std::string const& new_filename)
	{ return false; }

	virtual bool delete_files() { return false; }

	virtual ~test_storage() {}

	void wait_for_ready()
	{
		boost::mutex::scoped_lock l(m_mutex);
		while (!m_ready)
			m_ready_condition.wait(l);
	}

	void start()
	{
		boost::mutex::scoped_lock l(m_mutex);
		m_started = true;
		m_condition.notify_all();
	}

private:
	boost::condition_variable m_ready_condition;
	boost::condition_variable m_condition;
	boost::mutex m_mutex;
	bool m_started;
	bool m_ready;

};

storage_interface* create_test_storage(file_storage const& fs
	, file_storage const* mapped, fs::path const& path, file_pool& fp)
{
	return new test_storage;
}

void nop() {}

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
	++job_counter;
	dio.add_job(j, boost::bind(&callback, _1, _2));
}

void run_elevator_test()
{
	io_service ios;
	file_pool fp;
	boost::intrusive_ptr<torrent_info> ti = ::create_torrent(0, 16, 6000);

	{
		disk_io_thread dio(ios, &nop, fp);
		boost::intrusive_ptr<piece_manager> pm(new piece_manager(boost::shared_ptr<void>(), ti, ""
			, fp, dio, &create_test_storage, storage_mode_sparse));

		// we must disable the read cache in order to
		// verify that the elevator algorithm works.
		// since any read cache hit will circumvent
		// the elevator order
		session_settings set;
		set.use_read_cache = false;
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
			ios.run_one();

		TEST_CHECK(turns == 0);
		TEST_CHECK(job_counter == 0);
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
			ios.run_one();

		TEST_CHECK(turns == 0);
		TEST_CHECK(job_counter == 0);
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
			ios.run_one();

		TEST_CHECK(job_counter == 0);
		std::cerr << "number of elevator turns: " << turns << std::endl;

		// this is not guaranteed, but very very likely
		TEST_CHECK(turns > 20);

		dio.join();
	}
}

void run_until(io_service& ios, bool const& done)
{
	while (!done)
	{
		ios.reset();
		error_code ec;
		ios.run_one(ec);
		if (ec)
		{
			std::cerr << "run_one: " << ec.message() << std::endl;
			return;
		}
		std::cerr << "done: " << done << std::endl;
	}
}

void run_storage_tests(boost::intrusive_ptr<torrent_info> info
	, file_storage& fs
	, path const& test_path
	, libtorrent::storage_mode_t storage_mode
	, bool unbuffered)
{
	TORRENT_ASSERT(fs.num_files() > 0);
	create_directory(test_path / "temp_storage");
	remove_all(test_path / "temp_storage2");
	remove_all(test_path / "part0");

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
		default_storage_constructor(fs, 0, test_path, fp));
	s->m_settings = &set;
	s->m_disk_pool = &dp;

	int ret = 0;

	// write piece 1 (in slot 0)
	ret = s->write(piece1, 0, 0, half);
	if (ret != half) print_error(ret, s);
	ret = s->write(piece1 + half, 0, half, half);
	if (ret != half) print_error(ret, s);

	// test unaligned read (where the bytes are aligned)
	ret = s->read(piece + 3, 0, 3, piece_size-9);
	if (ret != piece_size - 9) print_error(ret, s);
	TEST_CHECK(std::equal(piece+3, piece + piece_size-9, piece1+3));
	
	// test unaligned read (where the bytes are not aligned)
	ret = s->read(piece, 0, 3, piece_size-9);
	if (ret != piece_size - 9) print_error(ret, s);
	TEST_CHECK(std::equal(piece, piece + piece_size-9, piece1+3));

	// verify piece 1
	ret = s->read(piece, 0, 0, piece_size);
	if (ret != piece_size) print_error(ret, s);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece1));
	
	// do the same with piece 0 and 2 (in slot 1 and 2)
	ret = s->write(piece0, 1, 0, piece_size);
	if (ret != piece_size) print_error(ret, s);
	ret = s->write(piece2, 2, 0, piece_size);
	if (ret != piece_size) print_error(ret, s);

	// verify piece 0 and 2
	ret = s->read(piece, 1, 0, piece_size);
	if (ret != piece_size) print_error(ret, s);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece0));

	ret = s->read(piece, 2, 0, piece_size);
	if (ret != piece_size) print_error(ret, s);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece2));

	s->release_files();
	}

	// make sure the piece_manager can identify the pieces
	{
	file_pool fp;
	libtorrent::asio::io_service ios;
	disk_io_thread io(ios, boost::function<void()>(), fp);
	boost::shared_ptr<int> dummy(new int);
	boost::intrusive_ptr<piece_manager> pm = new piece_manager(dummy, info
		, test_path, fp, io, default_storage_constructor, storage_mode);
	boost::mutex lock;

	error_code ec;
	bool done = false;
	lazy_entry frd;
	pm->async_check_fastresume(&frd, boost::bind(&on_check_resume_data, _1, _2, &done));
	ios.reset();
	run_until(ios, done);

	done = false;
	pm->async_check_files(boost::bind(&on_check_files, _1, _2, &done));
	run_until(ios, done);

	done = false;
	peer_request r;
	r.piece = 0;
	r.start = 10;
	r.length = 16 * 1024;
	pm->async_read(r, boost::bind(&on_read, _1, _2, &done));
	run_until(ios, done);

	// test rename_file
	remove(test_path / "part0");
	TEST_CHECK(exists(test_path / "temp_storage/test1.tmp"));
	TEST_CHECK(!exists(test_path / "part0"));	
	boost::function<void(int, disk_io_job const&)> none;
	done = false;
	pm->async_rename_file(0, "part0", boost::bind(&signal_bool, &done, "rename_file"));
	run_until(ios, done);

	TEST_CHECK(!exists(test_path / "temp_storage/test1.tmp"));
	TEST_CHECK(!exists(test_path / "temp_storage2"));
	TEST_CHECK(exists(test_path / "part0"));

	// test move_storage with two files in the root directory
	TEST_CHECK(exists(test_path / "temp_storage"));

	done = false;
	pm->async_move_storage(test_path / "temp_storage2"
		, boost::bind(on_move_storage, _1, &done, _2, (test_path / "temp_storage2").string()));
	run_until(ios, done);

	if (fs.num_files() > 1)
	{
		TEST_CHECK(!exists(test_path / "temp_storage"));
		TEST_CHECK(exists(test_path / "temp_storage2/temp_storage"));
	}
	TEST_CHECK(exists(test_path / "temp_storage2/part0"));	

	done = false;
	pm->async_move_storage(test_path, boost::bind(on_move_storage, _1, &done, _2, test_path.string()));
	run_until(ios, done);

	TEST_CHECK(exists(test_path / "part0"));	
	TEST_CHECK(!exists(test_path / "temp_storage2/temp_storage"));	
	TEST_CHECK(!exists(test_path / "temp_storage2/part0"));	

	r.piece = 0;
	r.start = 0;
	r.length = block_size;
	pm->async_read(r, boost::bind(&on_read_piece, _1, _2, piece0, block_size));
	r.piece = 1;
	pm->async_read(r, boost::bind(&on_read_piece, _1, _2, piece1, block_size));
	r.piece = 2;
	pm->async_read(r, boost::bind(&on_read_piece, _1, _2, piece2, block_size));

	std::cerr << "async_release_files" << std::endl;
	done = false;
	pm->async_release_files(boost::bind(&signal_bool, &done, "async_release_files"));
	run_until(ios, done);

	std::cerr << "async_rename_file" << std::endl;
	done = false;
	pm->async_rename_file(0, "temp_storage/test1.tmp", boost::bind(&signal_bool, &done, "rename_file"));
	run_until(ios, done);
  
	TEST_CHECK(!exists(test_path / "part0"));	
	TEST_CHECK(exists(test_path / "temp_storage/test1.tmp"));

	ios.reset();
	ios.poll(ec);

	io.join();
	remove_all(test_path / "temp_storage2");
	remove_all(test_path / "part0");
	}
	page_aligned_allocator::free(piece);
}

void test_remove(path const& test_path, bool unbuffered)
{
	file_storage fs;
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
	error_code ec;
	boost::intrusive_ptr<torrent_info> info(new torrent_info(&buf[0], buf.size(), ec));

	session_settings set;
	set.disk_io_write_mode = set.disk_io_read_mode
		= unbuffered ? session_settings::disable_os_cache_for_aligned_files
		: session_settings::enable_os_cache;

	file_pool fp;
	disk_buffer_pool dp(16 * 1024);
	boost::scoped_ptr<storage_interface> s(
		default_storage_constructor(fs, 0, test_path, fp));
	s->m_settings = &set;
	s->m_disk_pool = &dp;

	// allocate the files and create the directories
	s->initialize(true);
	TEST_CHECK(!s->error());
	if (s->error())
		fprintf(stderr, "%s: %s\n", s->error().message().c_str(), s->error_file().c_str());

	TEST_CHECK(exists(test_path / "temp_storage/_folder3/subfolder/test5.tmp"));	
	TEST_CHECK(exists(test_path / "temp_storage/folder2/test3.tmp"));	

	s->delete_files();

	TEST_CHECK(!exists(test_path / "temp_storage"));	
}

namespace
{
	void check_files_fill_array(int ret, disk_io_job const& j, bool* array, bool* done)
	{
		std::cerr << "check_files_fill_array ret: " << ret
			<< " piece: " << j.piece
			<< " have: " << j.offset
			<< " str: " << j.str
			<< " e: " << j.error.message()
			<< std::endl;

		if (j.offset >= 0) array[j.offset] = true;
		if (ret != piece_manager::need_full_check)
		{
			*done = true;
		}
	}
}

void test_check_files(path const& test_path
	, libtorrent::storage_mode_t storage_mode
	, bool unbuffered)
{
	boost::intrusive_ptr<torrent_info> info;

	const int piece_size = 16 * 1024;
	remove_all(test_path / "temp_storage");
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

	create_directory(test_path / "temp_storage");

	std::ofstream f;
	f.open((test_path / "temp_storage/test1.tmp").string().c_str(), std::ios::trunc | std::ios::binary);
	f.write(piece0, sizeof(piece0));
	f.close();
	f.open((test_path / "temp_storage/test3.tmp").string().c_str(), std::ios::trunc | std::ios::binary);
	f.write(piece2, sizeof(piece2));
	f.close();

	std::vector<char> buf;
	error_code ec;
	bencode(std::back_inserter(buf), t.generate());
	info = new torrent_info(&buf[0], buf.size(), ec);

	file_pool fp;
	libtorrent::asio::io_service ios;
	disk_io_thread io(ios, boost::function<void()>(), fp);
	boost::shared_ptr<int> dummy(new int);
	boost::intrusive_ptr<piece_manager> pm = new piece_manager(dummy, info
		, test_path, fp, io, default_storage_constructor, storage_mode);
	boost::mutex lock;

	bool done = false;
	lazy_entry frd;
	pm->async_check_fastresume(&frd, boost::bind(&on_check_resume_data, _1, _2, &done));
	ios.reset();
	run_until(ios, done);

	bool pieces[4] = {false, false, false, false};

	done = false;
	pm->async_check_files(boost::bind(&check_files_fill_array, _1, _2, pieces, &done));
	run_until(ios, done);

	TEST_CHECK(pieces[0] == true);
	TEST_CHECK(pieces[1] == false);
	TEST_CHECK(pieces[2] == false);
	TEST_CHECK(pieces[3] == true);
	io.join();
}

void run_test(path const& test_path, bool unbuffered)
{
	std::cerr << "\n=== " << test_path.string() << " ===\n" << std::endl;

	boost::intrusive_ptr<torrent_info> info;

	{
	remove_all(test_path / "temp_storage");
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
	error_code ec;
	info = new torrent_info(&buf[0], buf.size(), ec);
	std::cerr << "=== test 1 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_compact, unbuffered);

	// make sure the files have the correct size
	TEST_CHECK(file_size(test_path / "temp_storage" / "test1.tmp") == 17);
	TEST_CHECK(file_size(test_path / "temp_storage" / "test2.tmp") == 612);
	TEST_CHECK(exists(test_path / "temp_storage/test3.tmp"));
	TEST_CHECK(exists(test_path / "temp_storage/test4.tmp"));
	TEST_CHECK(file_size(test_path / "temp_storage" / "test5.tmp") == 3253);
	TEST_CHECK(file_size(test_path / "temp_storage" / "test6.tmp") == 841);
	TEST_CHECK(file_size(test_path / "temp_storage" / "test7.tmp") == last_file_size - piece_size);
	std::cerr << file_size(test_path / "temp_storage" / "test1.tmp") << std::endl;
	std::cerr << file_size(test_path / "temp_storage" / "test2.tmp") << std::endl;
	std::cerr << file_size(test_path / "temp_storage" / "test3.tmp") << std::endl;
	std::cerr << file_size(test_path / "temp_storage" / "test4.tmp") << std::endl;
	std::cerr << file_size(test_path / "temp_storage" / "test5.tmp") << std::endl;
	std::cerr << file_size(test_path / "temp_storage" / "test6.tmp") << std::endl;
	remove_all(test_path / "temp_storage");
	}

// ==============================================

	{
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", 3 * piece_size);
	libtorrent::create_torrent t(fs, piece_size, -1, 0);
	TEST_CHECK(fs.begin()->path == "temp_storage/test1.tmp");
	t.set_hash(0, hasher(piece0, piece_size).final());
	t.set_hash(1, hasher(piece1, piece_size).final());
	t.set_hash(2, hasher(piece2, piece_size).final());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	error_code ec;
	info = new torrent_info(&buf[0], buf.size(), ec);

	std::cerr << "=== test 3 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_compact, unbuffered);

	TEST_CHECK(file_size(test_path / "temp_storage" / "test1.tmp") == piece_size * 3);
	remove_all(test_path / "temp_storage");

// ==============================================

	std::cerr << "=== test 4 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_allocate, unbuffered);

	std::cerr << file_size(test_path / "temp_storage" / "test1.tmp") << std::endl;
	TEST_CHECK(file_size(test_path / "temp_storage" / "test1.tmp") == 3 * piece_size);

	remove_all(test_path / "temp_storage");

	}

// ==============================================

	std::cerr << "=== test 5 ===" << std::endl;
	test_remove(test_path, unbuffered);

// ==============================================

	std::cerr << "=== test 6 ===" << std::endl;
	test_check_files(test_path, storage_mode_sparse, unbuffered);
	test_check_files(test_path, storage_mode_compact, unbuffered);
}

void test_fastresume(path const& test_path)
{
	std::cout << "\n\n=== test fastresume ===" << std::endl;
	remove_all(test_path / "tmp1");
	create_directory(test_path / "tmp1");
	std::ofstream file((test_path / "tmp1/temporary").external_file_string().c_str());
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file);
	file.close();
	TEST_CHECK(exists(test_path / "tmp1/temporary"));

	entry resume;
	{
		session ses(fingerprint("  ", 0,0,0,0), 0);
		ses.set_alert_mask(alert::all_categories);

		add_torrent_params p;
		p.ti = boost::intrusive_ptr<torrent_info>(new torrent_info(*t));
		p.save_path = test_path / "tmp1";
		p.storage_mode = storage_mode_compact;
		torrent_handle h = ses.add_torrent(p);

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
	TEST_CHECK(!exists(test_path / "tmp1/temporary"));
#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
	resume.print(std::cout);
#endif

	// make sure the fast resume check fails! since we removed the file
	{
		session ses(fingerprint("  ", 0,0,0,0), 0);
		ses.set_alert_mask(alert::all_categories);
		add_torrent_params p;
		p.save_path = test_path / "tmp1";
		p.ti = t;
		std::vector<char> resume_buf;
		bencode(std::back_inserter(resume_buf), resume);
		p.resume_data = &resume_buf;
		p.storage_mode = storage_mode_compact;
		torrent_handle h = ses.add_torrent(p);
	
		std::auto_ptr<alert> a = ses.pop_alert();
		ptime end = time_now() + seconds(20);
		while (a.get() == 0 || dynamic_cast<fastresume_rejected_alert*>(a.get()) == 0)
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
	remove_all(test_path / "tmp1");
}

bool got_file_rename_alert(alert* a)
{
	return dynamic_cast<libtorrent::file_renamed_alert*>(a)
		|| dynamic_cast<libtorrent::file_rename_failed_alert*>(a);
}

void test_rename_file_in_fastresume(path const& test_path)
{
	std::cout << "\n\n=== test rename file in fastresume ===" << std::endl;
	remove_all(test_path / "tmp2");
	create_directory(test_path / "tmp2");
	std::ofstream file((test_path / "tmp2/temporary").external_file_string().c_str());
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file);
	file.close();
	TEST_CHECK(exists(test_path / "tmp2/temporary"));

	entry resume;
	{
		session ses(fingerprint("  ", 0,0,0,0), 0);
		ses.set_alert_mask(alert::all_categories);

		add_torrent_params p;
		p.save_path = test_path / "tmp2";
		p.ti = boost::intrusive_ptr<torrent_info>(new torrent_info(*t));
		p.storage_mode = storage_mode_compact;
		torrent_handle h = ses.add_torrent(p);

		h.rename_file(0, "testing_renamed_files");
		std::cout << "renaming file" << std::endl;
		bool renamed = false;
		for (int i = 0; i < 100; ++i)
		{
			if (print_alerts(ses, "ses", true, true, true, &got_file_rename_alert)) renamed = true;
			test_sleep(1000);
			torrent_status s = h.status();
			if (s.state == torrent_status::seeding && renamed) return;
		}
		std::cout << "stop loop" << std::endl;
		torrent_status s = h.status();
		TEST_CHECK(s.state == torrent_status::seeding);
		resume = h.write_resume_data();
		ses.remove_torrent(h);
	}
	TEST_CHECK(!exists(test_path / "tmp2/temporary"));
	TEST_CHECK(exists(test_path / "tmp2/testing_renamed_files"));
	TEST_CHECK(resume.dict().find("mapped_files") != resume.dict().end());
#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
	resume.print(std::cout);
#endif

	// make sure the fast resume check succeeds, even though we renamed the file
	{
		session ses(fingerprint("  ", 0,0,0,0), 0);
		ses.set_alert_mask(alert::all_categories);
		add_torrent_params p;
		p.save_path = test_path / "tmp2";
		p.ti = t;
		std::vector<char> resume_buf;
		bencode(std::back_inserter(resume_buf), resume);
		p.resume_data = &resume_buf;
		p.storage_mode = storage_mode_compact;
		torrent_handle h = ses.add_torrent(p);
	
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
	remove_all(test_path / "tmp2");
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

	std::vector<path> test_paths;
	char* env = std::getenv("TORRENT_TEST_PATHS");
	if (env == 0)
	{
		test_paths.push_back(initial_path<path>());
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

