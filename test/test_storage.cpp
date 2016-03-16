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

#include "test.hpp"
#include "setup_transfer.hpp"
#include "test_utils.hpp"

#include "libtorrent/storage.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/torrent_info.hpp"

#include <boost/make_shared.hpp>
#include <boost/utility.hpp>

#include <iostream>
#include <fstream>

using namespace libtorrent;
namespace lt = libtorrent;

const int piece_size = 16 * 1024 * 16;
const int half = piece_size / 2;

void signal_bool(bool* b, char const* string)
{
	*b = true;
	std::cerr << time_now_string() << " " << string << std::endl;
}

void on_read_piece(int ret, disk_io_job const& j, char const* data, int size)
{
	std::cerr << time_now_string() << " on_read_piece piece: " << j.piece << std::endl;
	TEST_EQUAL(ret, size);
	if (ret > 0) TEST_CHECK(std::equal(j.buffer.disk_block, j.buffer.disk_block + ret, data));
}

void on_check_resume_data(disk_io_job const* j, bool* done)
{
	std::cerr << time_now_string() << " on_check_resume_data ret: " << j->ret;
	switch (j->ret)
	{
		case piece_manager::no_error:
			std::cerr << time_now_string() << " success" << std::endl;
			break;
		case piece_manager::fatal_disk_error:
			std::cerr << time_now_string() << " disk error: " << j->error.ec.message()
				<< " file: " << j->error.file << std::endl;
			break;
		case piece_manager::need_full_check:
			std::cerr << time_now_string() << " need full check" << std::endl;
			break;
		case piece_manager::disk_check_aborted:
			std::cerr << time_now_string() << " aborted" << std::endl;
			break;
	}
	std::cerr << std::endl;
	*done = true;
}

void print_error(char const* call, int ret, storage_error const& ec)
{
	fprintf(stderr, "%s: %s() returned: %d error: \"%s\" in file: %d operation: %d\n"
		, time_now_string(), call, ret, ec.ec.message().c_str(), ec.file, ec.operation);
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
			std::cerr << "run_one: " << ec.message().c_str() << std::endl;
			return;
		}
		std::cerr << time_now_string() << " done: " << done << std::endl;
	}
}

void nop() {}

boost::shared_ptr<default_storage> setup_torrent(file_storage& fs
	, file_pool& fp
	, std::vector<char>& buf
	, std::string const& test_path
	, aux::session_settings& set)
{
	fs.add_file(combine_path("temp_storage", "test1.tmp"), 8);
	fs.add_file(combine_path("temp_storage", combine_path("folder1", "test2.tmp")), 8);
	fs.add_file(combine_path("temp_storage", combine_path("folder2", "test3.tmp")), 0);
	fs.add_file(combine_path("temp_storage", combine_path("_folder3", "test4.tmp")), 0);
	fs.add_file(combine_path("temp_storage", combine_path("_folder3", combine_path("subfolder", "test5.tmp"))), 8);
	libtorrent::create_torrent t(fs, 4, -1, 0);

	char buf_[4] = {0, 0, 0, 0};
	sha1_hash h = hasher(buf_, 4).final();
	for (int i = 0; i < 6; ++i) t.set_hash(i, h);

	bencode(std::back_inserter(buf), t.generate());
	error_code ec;

	boost::shared_ptr<torrent_info> info(boost::make_shared<torrent_info>(&buf[0]
		, buf.size(), boost::ref(ec), 0));

	if (ec)
	{
		fprintf(stderr, "torrent_info constructor failed: %s\n"
			, ec.message().c_str());
	}

	storage_params p;
	p.files = &fs;
	p.pool = &fp;
	p.path = test_path;
	p.mode = storage_mode_allocate;
	boost::shared_ptr<default_storage> s(new default_storage(p));
	s->m_settings = &set;

	// allocate the files and create the directories
	storage_error se;
	s->initialize(se);
	if (se)
	{
		TEST_ERROR(se.ec.message().c_str());
		fprintf(stderr, "default_storage::initialize %s: %d\n", se.ec.message().c_str(), int(se.file));
	}

	return s;
}

typedef boost::shared_array<char> buf_ptr;

buf_ptr new_piece(int size)
{
	buf_ptr ret(static_cast<char*>(malloc(size)), &free);
	std::generate(ret.get(), ret.get() + size, random_byte);
	return ret;
}

void run_storage_tests(boost::shared_ptr<torrent_info> info
	, file_storage& fs
	, std::string const& test_path
	, libtorrent::storage_mode_t storage_mode
	, bool unbuffered)
{
	TORRENT_ASSERT(fs.num_files() > 0);
	error_code ec;
	create_directory(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cerr << "create_directory '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	remove_all(combine_path(test_path, "temp_storage2"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage2")
		<< "': " << ec.message() << std::endl;
	remove_all(combine_path(test_path, "part0"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "part0")
		<< "': " << ec.message() << std::endl;

	int num_pieces = fs.num_pieces();
	TEST_CHECK(info->num_pieces() == num_pieces);

	buf_ptr piece0 = new_piece(piece_size);
	buf_ptr piece1 = new_piece(piece_size);
	buf_ptr piece2 = new_piece(piece_size);

	aux::session_settings set;
	set.set_int(settings_pack::disk_io_write_mode
		, unbuffered ? settings_pack::disable_os_cache
		: settings_pack::enable_os_cache);
	set.set_int(settings_pack::disk_io_read_mode
		, unbuffered ? settings_pack::disable_os_cache
		: settings_pack::enable_os_cache);

	char* piece = static_cast<char*>(malloc(piece_size));

	{ // avoid having two storages use the same files
	file_pool fp;
	boost::asio::io_service ios;
	disk_buffer_pool dp(16 * 1024, ios, boost::bind(&nop));
	storage_params p;
	p.path = test_path;
	p.files = &fs;
	p.pool = &fp;
	p.mode = storage_mode;
	boost::scoped_ptr<storage_interface> s(new default_storage(p));
	s->m_settings = &set;

	storage_error ec;
	s->initialize(ec);
	TEST_CHECK(!ec);
	if (ec) print_error("initialize", 0, ec);

	int ret = 0;

	// write piece 1 (in slot 0)
	file::iovec_t iov = { piece1.get(), half};
	ret = s->writev(&iov, 1, 0, 0, 0, ec);
	if (ret != half) print_error("writev", ret, ec);

	iov.iov_base = piece1.get() + half;
	iov.iov_len = half;
	ret = s->writev(&iov, 1, 0, half, 0, ec);
	if (ret != half) print_error("writev", ret, ec);

	// test unaligned read (where the bytes are aligned)
	iov.iov_base = piece + 3;
	iov.iov_len = piece_size - 9;
	ret = s->readv(&iov, 1, 0, 3, 0, ec);
	if (ret != piece_size - 9) print_error("readv",ret, ec);
	TEST_CHECK(std::equal(piece+3, piece + piece_size-9, piece1.get()+3));

	// test unaligned read (where the bytes are not aligned)
	iov.iov_base = piece;
	iov.iov_len = piece_size - 9;
	ret = s->readv(&iov, 1, 0, 3, 0, ec);
	TEST_CHECK(ret == piece_size - 9);
	if (ret != piece_size - 9) print_error("readv", ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size-9, piece1.get()+3));

	// verify piece 1
	iov.iov_base = piece;
	iov.iov_len = piece_size;
	ret = s->readv(&iov, 1, 0, 0, 0, ec);
	TEST_CHECK(ret == piece_size);
	if (ret != piece_size) print_error("readv", ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece1.get()));

	// do the same with piece 0 and 2 (in slot 1 and 2)
	iov.iov_base = piece0.get();
	iov.iov_len = piece_size;
	ret = s->writev(&iov, 1, 1, 0, 0, ec);
	if (ret != piece_size) print_error("writev", ret, ec);

	iov.iov_base = piece2.get();
	iov.iov_len = piece_size;
	ret = s->writev(&iov, 1, 2, 0, 0, ec);
	if (ret != piece_size) print_error("writev", ret, ec);

	// verify piece 0 and 2
	iov.iov_base = piece;
	iov.iov_len = piece_size;
	ret = s->readv(&iov, 1, 1, 0, 0, ec);
	if (ret != piece_size) print_error("readv", ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece0.get()));

	iov.iov_base = piece;
	iov.iov_len = piece_size;
	ret = s->readv(&iov, 1, 2, 0, 0, ec);
	if (ret != piece_size) print_error("readv", ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece2.get()));

	s->release_files(ec);
	}

	free(piece);
}

void test_remove(std::string const& test_path, bool unbuffered)
{
	error_code ec;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	TEST_CHECK(!exists(combine_path(test_path, "temp_storage")));

	file_storage fs;
	std::vector<char> buf;
	file_pool fp;
	io_service ios;
	disk_buffer_pool dp(16 * 1024, ios, boost::bind(&nop));

	aux::session_settings set;
	set.set_int(settings_pack::disk_io_write_mode
		, unbuffered ? settings_pack::disable_os_cache
		: settings_pack::enable_os_cache);
	set.set_int(settings_pack::disk_io_read_mode
		, unbuffered ? settings_pack::disable_os_cache
		: settings_pack::enable_os_cache);

	boost::shared_ptr<default_storage> s = setup_torrent(fs, fp, buf, test_path, set);

	// directories are not created up-front, unless they contain
	// an empty file (all of which are created up-front, along with
	// all required directories)
	// files are created on first write
	TEST_CHECK(!exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", combine_path("subfolder", "test5.tmp"))))));

	// this directory and file is created up-front because it's an empty file
	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder2", "test3.tmp")))));

	// this isn't
	TEST_CHECK(!exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder1", "test2.tmp")))));

	file::iovec_t b = {&buf[0], 4};
	storage_error se;
	s->writev(&b, 1, 2, 0, 0, se);

	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder1", "test2.tmp")))));
	TEST_CHECK(!exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", combine_path("subfolder", "test5.tmp"))))));
	file_status st;
	stat_file(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder1", "test2.tmp"))), &st, ec);
	TEST_EQUAL(st.file_size, 8);

	s->writev(&b, 1, 4, 0, 0, se);

	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", combine_path("subfolder", "test5.tmp"))))));
	stat_file(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", "test5.tmp"))), &st, ec);
	TEST_EQUAL(st.file_size, 8);

	s->delete_files(session::delete_files, se);
	if (se) print_error("delete_files", 0, se.ec);

	if (se)
	{
		TEST_ERROR(se.ec.message().c_str());
		fprintf(stderr, "default_storage::delete_files %s: %d\n", se.ec.message().c_str(), int(se.file));
	}

	TEST_CHECK(!exists(combine_path(test_path, "temp_storage")));
}

void test_rename(std::string const& test_path)
{
	error_code ec;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	TEST_CHECK(!exists(combine_path(test_path, "temp_storage")));

	file_storage fs;
	std::vector<char> buf;
	file_pool fp;
	io_service ios;
	disk_buffer_pool dp(16 * 1024, ios, boost::bind(&nop));
	aux::session_settings set;

	boost::shared_ptr<default_storage> s = setup_torrent(fs, fp, buf, test_path
		, set);

	// directories are not created up-front, unless they contain
	// an empty file
	std::string first_file = fs.file_path(0);
	for (int i = 0; i < fs.num_files(); ++i)
	{
		TEST_CHECK(!exists(combine_path(test_path, combine_path("temp_storage"
			, fs.file_path(i)))));
	}

	storage_error se;
	s->rename_file(0, "new_filename", se);
	if (se.ec)
	{
		fprintf(stderr, "default_storage::rename_file failed: %s\n"
			, se.ec.message().c_str());
	}
	TEST_CHECK(!se.ec);

	TEST_EQUAL(s->files().file_path(0), "new_filename");
}

void test_check_files(std::string const& test_path
	, libtorrent::storage_mode_t storage_mode
	, bool unbuffered)
{
	boost::shared_ptr<torrent_info> info;

	error_code ec;
	const int piece_size = 16 * 1024;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", piece_size);
	fs.add_file("temp_storage/test2.tmp", piece_size * 2);
	fs.add_file("temp_storage/test3.tmp", piece_size);

	buf_ptr piece0 = new_piece(piece_size);
	buf_ptr piece2 = new_piece(piece_size);

	libtorrent::create_torrent t(fs, piece_size, -1, 0);
	t.set_hash(0, hasher(piece0.get(), piece_size).final());
	t.set_hash(1, sha1_hash(0));
	t.set_hash(2, sha1_hash(0));
	t.set_hash(3, hasher(piece2.get(), piece_size).final());

	create_directory(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cerr << "create_directory: " << ec.message() << std::endl;

	std::ofstream f;
	f.open(combine_path(test_path, combine_path("temp_storage", "test1.tmp")).c_str()
		, std::ios::trunc | std::ios::binary);
	f.write(piece0.get(), piece_size);
	f.close();
	f.open(combine_path(test_path, combine_path("temp_storage", "test3.tmp")).c_str()
		, std::ios::trunc | std::ios::binary);
	f.write(piece2.get(), piece_size);
	f.close();

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = boost::make_shared<torrent_info>(&buf[0], buf.size(), boost::ref(ec), 0);

	aux::session_settings set;
	file_pool fp;
	boost::asio::io_service ios;
	counters cnt;
	disk_io_thread io(ios, cnt, NULL);
	io.set_num_threads(1);
	disk_buffer_pool dp(16 * 1024, ios, boost::bind(&nop));
	storage_params p;
	p.files = &fs;
	p.path = test_path;
	p.pool = &fp;
	p.mode = storage_mode;

	boost::shared_ptr<void> dummy;
	boost::shared_ptr<piece_manager> pm = boost::make_shared<piece_manager>(new default_storage(p), dummy, &fs);
	libtorrent::mutex lock;

	bool done = false;
	bdecode_node frd;
	std::vector<std::string> links;
	io.async_check_fastresume(pm.get(), &frd, links
		, boost::bind(&on_check_resume_data, _1, &done));
	io.submit_jobs();
	ios.reset();
	run_until(ios, done);

	io.set_num_threads(0);
}

// TODO: 2 split this test up into smaller parts
void run_test(bool unbuffered)
{
	std::string test_path = current_working_directory();
	std::cerr << "\n=== " << test_path << " ===\n" << std::endl;

	boost::shared_ptr<torrent_info> info;

	buf_ptr piece0 = new_piece(piece_size);
	buf_ptr piece1 = new_piece(piece_size);
	buf_ptr piece2 = new_piece(piece_size);
	buf_ptr piece3 = new_piece(piece_size);

	{
	error_code ec;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", 17);
	fs.add_file("temp_storage/test2.tmp", 612);
	fs.add_file("temp_storage/test3.tmp", 0);
	fs.add_file("temp_storage/test4.tmp", 0);
	fs.add_file("temp_storage/test5.tmp", 3253);
	fs.add_file("temp_storage/test6.tmp", 841);
	const int last_file_size = 4 * piece_size - fs.total_size();
	fs.add_file("temp_storage/test7.tmp", last_file_size);

	// File layout
	// +-+--+++-------+-------+----------------------------------------------------------------------------------------+
	// |1| 2||| file5 | file6 | file7                                                                                  |
	// +-+--+++-------+-------+----------------------------------------------------------------------------------------+
	// |                           |                           |                           |                           |
	// | piece 0                   | piece 1                   | piece 2                   | piece 3                   |

	libtorrent::create_torrent t(fs, piece_size, -1, 0);
	TEST_CHECK(t.num_pieces() == 4);
	t.set_hash(0, hasher(piece0.get(), piece_size).final());
	t.set_hash(1, hasher(piece1.get(), piece_size).final());
	t.set_hash(2, hasher(piece2.get(), piece_size).final());
	t.set_hash(3, hasher(piece3.get(), piece_size).final());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = boost::make_shared<torrent_info>(&buf[0], buf.size(), boost::ref(ec), 0);
	std::cerr << "=== test 1 === " << (unbuffered?"unbuffered":"buffered") << std::endl;

	// run_storage_tests writes piece 0, 1 and 2. not 3
	run_storage_tests(info, fs, test_path, storage_mode_sparse, unbuffered);

	// make sure the files have the correct size
	std::string base = combine_path(test_path, "temp_storage");
	fprintf(stderr, "base = \"%s\"\n", base.c_str());
	TEST_EQUAL(file_size(combine_path(base, "test1.tmp")), 17);
	TEST_EQUAL(file_size(combine_path(base, "test2.tmp")), 612);

	// these files should have been allocated as 0 size
	TEST_CHECK(exists(combine_path(base, "test3.tmp")));
	TEST_CHECK(exists(combine_path(base, "test4.tmp")));
	TEST_CHECK(file_size(combine_path(base, "test3.tmp")) == 0);
	TEST_CHECK(file_size(combine_path(base, "test4.tmp")) == 0);

	TEST_EQUAL(file_size(combine_path(base, "test5.tmp")), 3253);
	TEST_EQUAL(file_size(combine_path(base, "test6.tmp")), 841);
	printf("file: %d expected: %d last_file_size: %d, piece_size: %d\n"
		, int(file_size(combine_path(base, "test7.tmp")))
		, int(last_file_size - piece_size), last_file_size, piece_size);
	TEST_EQUAL(file_size(combine_path(base, "test7.tmp")), last_file_size - piece_size);
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	}

// ==============================================

	{
	error_code ec;
	file_storage fs;
	fs.add_file(combine_path("temp_storage", "test1.tmp"), 3 * piece_size);
	libtorrent::create_torrent t(fs, piece_size, -1, 0);
	TEST_CHECK(fs.file_path(0) == combine_path("temp_storage", "test1.tmp"));
	t.set_hash(0, hasher(piece0.get(), piece_size).final());
	t.set_hash(1, hasher(piece1.get(), piece_size).final());
	t.set_hash(2, hasher(piece2.get(), piece_size).final());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = boost::make_shared<torrent_info>(&buf[0], buf.size(), boost::ref(ec), 0);

	std::cerr << "=== test 3 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_sparse, unbuffered);

	TEST_EQUAL(file_size(combine_path(test_path, combine_path("temp_storage", "test1.tmp"))), piece_size * 3);
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;

// ==============================================

	std::cerr << "=== test 4 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_allocate, unbuffered);

	std::cerr << file_size(combine_path(test_path, combine_path("temp_storage", "test1.tmp"))) << std::endl;
	TEST_EQUAL(file_size(combine_path(test_path, combine_path("temp_storage", "test1.tmp"))), 3 * piece_size);

	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;

	}

// ==============================================

	std::cerr << "=== test 5 ===" << std::endl;
	test_remove(test_path, unbuffered);

// ==============================================

	std::cerr << "=== test 6 ===" << std::endl;
	test_check_files(test_path, storage_mode_sparse, unbuffered);
	test_check_files(test_path, storage_mode_sparse, unbuffered);

	std::cerr << "=== test 7 ===" << std::endl;
	test_rename(test_path);
}

TORRENT_TEST(fastresume)
{
	std::string test_path = current_working_directory();
	error_code ec;
	std::cout << "\n\n=== test fastresume ===" << std::endl;
	remove_all(combine_path(test_path, "tmp1"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "tmp1")
		<< "': " << ec.message() << std::endl;
	create_directory(combine_path(test_path, "tmp1"), ec);
	if (ec) std::cerr << "create_directory '" << combine_path(test_path, "tmp1")
		<< "': " << ec.message() << std::endl;
	std::ofstream file(combine_path(test_path, "tmp1/temporary").c_str());
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file);
	file.close();
	TEST_CHECK(exists(combine_path(test_path, "tmp1/temporary")));
	if (!exists(combine_path(test_path, "tmp1/temporary")))
		return;

	entry resume;
	{
		const int mask = alert::all_categories
			& ~(alert::progress_notification
				| alert::performance_warning
				| alert::stats_notification);

		settings_pack pack;
		pack.set_bool(settings_pack::enable_lsd, false);
		pack.set_bool(settings_pack::enable_natpmp, false);
		pack.set_bool(settings_pack::enable_upnp, false);
		pack.set_bool(settings_pack::enable_dht, false);
		pack.set_int(settings_pack::alert_mask, mask);
		lt::session ses(pack);

		error_code ec;

		add_torrent_params p;
		p.ti = boost::make_shared<torrent_info>(boost::cref(*t));
		p.save_path = combine_path(test_path, "tmp1");
		p.storage_mode = storage_mode_sparse;
		torrent_handle h = ses.add_torrent(p, ec);
		TEST_CHECK(exists(combine_path(p.save_path, "temporary")));
		if (!exists(combine_path(p.save_path, "temporary")))
			return;

		torrent_status s;
		for (int i = 0; i < 50; ++i)
		{
			print_alerts(ses, "ses");
			s = h.status();
			if (s.progress == 1.0f)
			{
				std::cout << "progress: 1.0f" << std::endl;
				break;
			}
			test_sleep(100);
		}

		// the whole point of the test is to have a resume
		// data which expects the file to exist in full. If
		// we failed to do that, we might as well abort
		TEST_EQUAL(s.progress, 1.0f);
		if (s.progress != 1.0f)
			return;

		h.save_resume_data();
		alert const* ra = wait_for_alert(ses, save_resume_data_alert::alert_type);
		TEST_CHECK(ra);
		if (ra) resume = *alert_cast<save_resume_data_alert>(ra)->resume_data;
		ses.remove_torrent(h, lt::session::delete_files);
		alert const* da = wait_for_alert(ses, torrent_deleted_alert::alert_type);
		TEST_CHECK(da);
	}
	TEST_CHECK(!exists(combine_path(test_path, combine_path("tmp1", "temporary"))));
	if (exists(combine_path(test_path, combine_path("tmp1", "temporary"))))
		return;

	std::cerr << resume.to_string() << "\n";
	TEST_CHECK(resume.dict().find("file sizes") != resume.dict().end());

	// make sure the fast resume check fails! since we removed the file
	{
		const int mask = alert::all_categories
			& ~(alert::progress_notification
				| alert::performance_warning
				| alert::stats_notification);

		settings_pack pack;
		pack.set_bool(settings_pack::enable_lsd, false);
		pack.set_bool(settings_pack::enable_natpmp, false);
		pack.set_bool(settings_pack::enable_upnp, false);
		pack.set_bool(settings_pack::enable_dht, false);
		pack.set_int(settings_pack::alert_mask, mask);
		lt::session ses(pack);

		add_torrent_params p;
		p.flags &= ~add_torrent_params::flag_paused;
		p.flags &= ~add_torrent_params::flag_auto_managed;
		p.ti = boost::make_shared<torrent_info>(boost::cref(*t));
		p.save_path = combine_path(test_path, "tmp1");
		p.storage_mode = storage_mode_sparse;
		bencode(std::back_inserter(p.resume_data), resume);
		torrent_handle h = ses.add_torrent(p, ec);

		alert const* a = wait_for_alert(ses, fastresume_rejected_alert::alert_type
			, "ses");
		// we expect the fast resume to be rejected because the files were removed
		TEST_CHECK(alert_cast<fastresume_rejected_alert>(a) != 0);
	}
	remove_all(combine_path(test_path, "tmp1"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "tmp1")
		<< "': " << ec.message() << std::endl;
}

bool got_file_rename_alert(alert const* a)
{
	return alert_cast<libtorrent::file_renamed_alert>(a)
		|| alert_cast<libtorrent::file_rename_failed_alert>(a);
}

TORRENT_TEST(rename_file_fastresume)
{
	std::string test_path = current_working_directory();
	error_code ec;
	std::cout << "\n\n=== test rename file in fastresume ===" << std::endl;
	remove_all(combine_path(test_path, "tmp2"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "tmp2")
		<< "': " << ec.message() << std::endl;
	create_directory(combine_path(test_path, "tmp2"), ec);
	if (ec) std::cerr << "create_directory: " << ec.message() << std::endl;
	std::ofstream file(combine_path(test_path, "tmp2/temporary").c_str());
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file);
	file.close();
	TEST_CHECK(exists(combine_path(test_path, "tmp2/temporary")));

	entry resume;
	{
		const int mask = alert::all_categories
			& ~(alert::progress_notification
				| alert::performance_warning
				| alert::stats_notification);

		settings_pack pack;
		pack.set_bool(settings_pack::enable_lsd, false);
		pack.set_bool(settings_pack::enable_natpmp, false);
		pack.set_bool(settings_pack::enable_upnp, false);
		pack.set_bool(settings_pack::enable_dht, false);
		pack.set_int(settings_pack::alert_mask, mask);
		lt::session ses(pack);

		add_torrent_params p;
		p.ti = boost::make_shared<torrent_info>(boost::cref(*t));
		p.save_path = combine_path(test_path, "tmp2");
		p.storage_mode = storage_mode_sparse;
		torrent_handle h = ses.add_torrent(p, ec);

		h.rename_file(0, "testing_renamed_files");
		std::cout << "renaming file" << std::endl;
		bool renamed = false;
		for (int i = 0; i < 10; ++i)
		{
			if (print_alerts(ses, "ses", true, true, true, &got_file_rename_alert)) renamed = true;
			torrent_status s = h.status();
			if (s.state == torrent_status::seeding && renamed) break;
			test_sleep(100);
		}
		std::cout << "stop loop" << std::endl;
		torrent_status s = h.status();
		TEST_CHECK(s.state == torrent_status::seeding);

		h.save_resume_data();
		alert const* ra = wait_for_alert(ses, save_resume_data_alert::alert_type);
		TEST_CHECK(ra);
		if (ra) resume = *alert_cast<save_resume_data_alert>(ra)->resume_data;
		ses.remove_torrent(h);
	}
	TEST_CHECK(!exists(combine_path(test_path, "tmp2/temporary")));
	TEST_CHECK(exists(combine_path(test_path, "tmp2/testing_renamed_files")));
	TEST_CHECK(resume.dict().find("mapped_files") != resume.dict().end());
	TEST_CHECK(resume.dict().find("file sizes") != resume.dict().end());

	std::cerr << resume.to_string() << "\n";

	// make sure the fast resume check succeeds, even though we renamed the file
	{
		const int mask = alert::all_categories
			& ~(alert::progress_notification
				| alert::performance_warning
				| alert::stats_notification);

		settings_pack pack;
		pack.set_bool(settings_pack::enable_lsd, false);
		pack.set_bool(settings_pack::enable_natpmp, false);
		pack.set_bool(settings_pack::enable_upnp, false);
		pack.set_bool(settings_pack::enable_dht, false);
		pack.set_int(settings_pack::alert_mask, mask);
		lt::session ses(pack);

		add_torrent_params p;
		p.ti = boost::make_shared<torrent_info>(boost::cref(*t));
		p.save_path = combine_path(test_path, "tmp2");
		p.storage_mode = storage_mode_sparse;
		bencode(std::back_inserter(p.resume_data), resume);
		torrent_handle h = ses.add_torrent(p, ec);

		torrent_status stat;
		for (int i = 0; i < 50; ++i)
		{
			stat = h.status();
			print_alerts(ses, "ses");
			if (stat.state == torrent_status::seeding)
				break;
			test_sleep(100);
		}
		TEST_CHECK(stat.state == torrent_status::seeding);

		h.save_resume_data();
		alert const* ra = wait_for_alert(ses, save_resume_data_alert::alert_type);
		TEST_CHECK(ra);
		if (ra) resume = *alert_cast<save_resume_data_alert>(ra)->resume_data;
		ses.remove_torrent(h);
	}
	TEST_CHECK(resume.dict().find("mapped_files") != resume.dict().end());

	std::cerr << resume.to_string() << "\n";

	remove_all(combine_path(test_path, "tmp2"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cerr << "remove_all '" << combine_path(test_path, "tmp2")
		<< "': " << ec.message() << std::endl;
}

void alloc_iov(file::iovec_t* iov, int num_bufs)
{
	for (int i = 0; i < num_bufs; ++i)
	{
		iov[i].iov_base = malloc(num_bufs * (i + 1));
		iov[i].iov_len  = num_bufs * (i + 1);
	}
}

void fill_pattern(file::iovec_t* iov, int num_bufs)
{
	int counter = 0;
	for (int i = 0; i < num_bufs; ++i)
	{
		unsigned char* buf = (unsigned char*)iov[i].iov_base;
		for (int k = 0; k < int(iov[i].iov_len); ++k)
		{
			buf[k] = counter & 0xff;
			++counter;
		}
	}
}

bool check_pattern(std::vector<char> const& buf, int counter)
{
	unsigned char* p = (unsigned char*)&buf[0];
	for (int k = 0; k < int(buf.size()); ++k)
	{
		if (p[k] != (counter & 0xff)) return false;
		++counter;
	}
	return true;
}

void fill_pattern2(file::iovec_t* iov, int num_bufs)
{
	for (int i = 0; i < num_bufs; ++i)
	{
		unsigned char* buf = (unsigned char*)iov[i].iov_base;
		memset(buf, 0xfe, int(iov[i].iov_len));
	}
}

void free_iov(file::iovec_t* iov, int num_bufs)
{
	for (int i = 0; i < num_bufs; ++i)
	{
		free(iov[i].iov_base);
		iov[i].iov_len = 0;
		iov[i].iov_base = NULL;
	}
}

TORRENT_TEST(iovec_copy_bufs)
{
	file::iovec_t iov1[10];
	file::iovec_t iov2[10];

	alloc_iov(iov1, 10);
	fill_pattern(iov1, 10);

	TEST_CHECK(bufs_size(iov1, 10) >= 106);

	// copy exactly 106 bytes from iov1 to iov2
	int num_bufs = copy_bufs(iov1, 106, iov2);

	// verify that the first 100 bytes is pattern 1
	// and that the remaining bytes are pattern 2

	int counter = 0;
	for (int i = 0; i < num_bufs; ++i)
	{
		unsigned char* buf = (unsigned char*)iov2[i].iov_base;
		for (int k = 0; k < int(iov2[i].iov_len); ++k)
		{
			TEST_EQUAL(int(buf[k]), (counter & 0xff));
			++counter;
		}
	}
	TEST_EQUAL(counter, 106);

	free_iov(iov1, 10);
}

TORRENT_TEST(iovec_clear_bufs)
{
	file::iovec_t iov[10];
	alloc_iov(iov, 10);
	fill_pattern(iov, 10);

	clear_bufs(iov, 10);
	for (int i = 0; i < 10; ++i)
	{
		unsigned char* buf = (unsigned char*)iov[i].iov_base;
		for (int k = 0; k < int(iov[i].iov_len); ++k)
		{
			TEST_EQUAL(int(buf[k]), 0);
		}
	}
	free_iov(iov, 10);
}

TORRENT_TEST(iovec_bufs_size)
{
	file::iovec_t iov[10];

	for (int i = 1; i < 10; ++i)
	{
		alloc_iov(iov, i);

		int expected_size = 0;
		for (int k = 0; k < i; ++k) expected_size += i * (k + 1);
		TEST_EQUAL(bufs_size(iov, i), expected_size);

		free_iov(iov, i);
	}
}

TORRENT_TEST(iovec_advance_bufs)
{
	file::iovec_t iov1[10];
	file::iovec_t iov2[10];
	alloc_iov(iov1, 10);
	fill_pattern(iov1, 10);

	memcpy(iov2, iov1, sizeof(iov1));

	file::iovec_t* iov = iov2;
	file::iovec_t* end = iov2 + 10;

	// advance iov 13 bytes. Make sure what's left fits pattern 1 shifted
	// 13 bytes
	advance_bufs(iov, 13);

	// make sure what's in 
	int counter = 13;
	for (int i = 0; i < end - iov; ++i)
	{
		unsigned char* buf = (unsigned char*)iov[i].iov_base;
		for (int k = 0; k < int(iov[i].iov_len); ++k)
		{
			TEST_EQUAL(int(buf[k]), (counter & 0xff));
			++counter;
		}
	}

	free_iov(iov1, 10);
}

TORRENT_TEST(unbuffered) { run_test(true); }
TORRENT_TEST(buffered) { run_test(false); }

file_storage make_fs()
{
	file_storage fs;
	fs.add_file(combine_path("readwritev", "1"), 3);
	fs.add_file(combine_path("readwritev", "2"), 9);
	fs.add_file(combine_path("readwritev", "3"), 81);
	fs.add_file(combine_path("readwritev", "4"), 6561);
	fs.set_piece_length(0x1000);
	fs.set_num_pieces((fs.total_size() + 0xfff) / 0x1000);
	return fs;
}

struct test_fileop : fileop
{
	test_fileop(int stripe_size) : m_stripe_size(stripe_size) {}

	int file_op(int file_index, boost::int64_t file_offset, int size
		, file::iovec_t const* bufs, storage_error& ec)
	{
		if (file_index >= int(m_file_data.size()))
		{
			m_file_data.resize(file_index + 1);
		}

		const int write_size = (std::min)(m_stripe_size, size);

		std::vector<char>& file = m_file_data[file_index];

		if (file_offset + write_size > int(file.size()))
		{
			file.resize(file_offset + write_size);
		}

		int left = write_size;
		while (left > 0)
		{
			const int copy_size = (std::min)(left, int(bufs->iov_len));
			memcpy(&file[file_offset], bufs->iov_base, copy_size);
			++bufs;
			file_offset += copy_size;
			left -= copy_size;
		}
		return write_size;
	}

	int m_stripe_size;
	std::vector<std::vector<char> > m_file_data;
};

struct test_read_fileop : fileop
{
	// EOF after size bytes read
	test_read_fileop(int size) : m_size(size), m_counter(0) {}

	int file_op(int file_index, boost::int64_t file_offset, int size
		, file::iovec_t const* bufs, storage_error& ec)
	{
		size = (std::min)(m_size, size);
		const int read = size;
		while (size > 0)
		{
			unsigned char* p = (unsigned char*)bufs->iov_base;
			const int len = (std::min)(int(bufs->iov_len), size);
			for (int i = 0; i < len; ++i)
			{
				p[i] = m_counter & 0xff;
				++m_counter;
			}
			size -= len;
			m_size -= len;
			++bufs;
		}
		return read;
	}

	int m_size;
	int m_counter;
};

struct test_error_fileop : fileop
{
	// EOF after size bytes read
	test_error_fileop(int error_file)
		: m_error_file(error_file) {}

	int file_op(int file_index, boost::int64_t file_offset, int size
		, file::iovec_t const* bufs, storage_error& ec)
	{
		if (m_error_file == file_index)
		{
			ec.file = file_index;
			ec.ec.assign(boost::system::errc::permission_denied
				, boost::system::generic_category());
			ec.operation = storage_error::read;
			return -1;
		}
		return size;
	}

	int m_error_file;
};

int count_bufs(file::iovec_t const* bufs, int bytes)
{
	int size = 0;
	int count = 1;
	if (bytes == 0) return 0;
	for (file::iovec_t const* i = bufs;; ++i, ++count)
	{
		size += i->iov_len;
		if (size >= bytes) return count;
	}
}

TORRENT_TEST(readwritev_stripe_1)
{
	const int num_bufs = 30;
	file::iovec_t iov[num_bufs];

	alloc_iov(iov, num_bufs);
	fill_pattern(iov, num_bufs);

	file_storage fs = make_fs();
	test_fileop fop(1);
	storage_error ec;

	TEST_CHECK(bufs_size(iov, num_bufs) >= fs.total_size());

	file::iovec_t iov2[num_bufs];
	copy_bufs(iov, fs.total_size(), iov2);
	int num_bufs2 = count_bufs(iov2, fs.total_size());
	TEST_CHECK(num_bufs2 <= num_bufs);

	int ret = readwritev(fs, iov2, 0, 0, num_bufs2, fop, ec);

	TEST_EQUAL(ret, fs.total_size());
	TEST_EQUAL(fop.m_file_data.size(), 4);
	TEST_EQUAL(fop.m_file_data[0].size(), 3);
	TEST_EQUAL(fop.m_file_data[1].size(), 9);
	TEST_EQUAL(fop.m_file_data[2].size(), 81);
	TEST_EQUAL(fop.m_file_data[3].size(), 6561);

	TEST_CHECK(check_pattern(fop.m_file_data[0], 0));
	TEST_CHECK(check_pattern(fop.m_file_data[1], 3));
	TEST_CHECK(check_pattern(fop.m_file_data[2], 3 + 9));
	TEST_CHECK(check_pattern(fop.m_file_data[3], 3 + 9 + 81));

	free_iov(iov, num_bufs);
}

TORRENT_TEST(readwritev_single_buffer)
{
	file_storage fs = make_fs();
	test_fileop fop(10000000);
	storage_error ec;

	std::vector<char> buf(fs.total_size());
	file::iovec_t iov = { &buf[0], buf.size() };
	fill_pattern(&iov, 1);

	int ret = readwritev(fs, &iov, 0, 0, 1, fop, ec);

	TEST_EQUAL(ret, fs.total_size());
	TEST_EQUAL(fop.m_file_data.size(), 4);
	TEST_EQUAL(fop.m_file_data[0].size(), 3);
	TEST_EQUAL(fop.m_file_data[1].size(), 9);
	TEST_EQUAL(fop.m_file_data[2].size(), 81);
	TEST_EQUAL(fop.m_file_data[3].size(), 6561);

	TEST_CHECK(check_pattern(fop.m_file_data[0], 0));
	TEST_CHECK(check_pattern(fop.m_file_data[1], 3));
	TEST_CHECK(check_pattern(fop.m_file_data[2], 3 + 9));
	TEST_CHECK(check_pattern(fop.m_file_data[3], 3 + 9 + 81));
}

TORRENT_TEST(readwritev_read)
{
	file_storage fs = make_fs();
	test_read_fileop fop(10000000);
	storage_error ec;

	std::vector<char> buf(fs.total_size());
	file::iovec_t iov = { &buf[0], buf.size() };

	// read everything
	int ret = readwritev(fs, &iov, 0, 0, 1, fop, ec);

	TEST_EQUAL(ret, fs.total_size());
	TEST_CHECK(check_pattern(buf, 0));
}

TORRENT_TEST(readwritev_read_short)
{
	file_storage fs = make_fs();
	test_read_fileop fop(100);
	storage_error ec;

	std::vector<char> buf(fs.total_size());
	file::iovec_t iov = { &buf[0]
		, static_cast<size_t>(fs.total_size()) };

	// read everything
	int ret = readwritev(fs, &iov, 0, 0, 1, fop, ec);

	TEST_EQUAL(ec.file, 3);

	TEST_EQUAL(ret, 100);
	buf.resize(100);
	TEST_CHECK(check_pattern(buf, 0));
}

TORRENT_TEST(readwritev_error)
{
	file_storage fs = make_fs();
	test_error_fileop fop(2);
	storage_error ec;

	std::vector<char> buf(fs.total_size());
	file::iovec_t iov = { &buf[0]
		, static_cast<size_t>(fs.total_size()) };

	// read everything
	int ret = readwritev(fs, &iov, 0, 0, 1, fop, ec);

	TEST_EQUAL(ret, -1);
	TEST_EQUAL(ec.file, 2);
	TEST_EQUAL(ec.operation, storage_error::read);
	TEST_EQUAL(ec.ec, boost::system::errc::permission_denied);
	printf("error: %s\n", ec.ec.message().c_str());
}

TORRENT_TEST(readwritev_zero_size_files)
{
	file_storage fs;
	fs.add_file(combine_path("readwritev", "1"), 3);
	fs.add_file(combine_path("readwritev", "2"), 0);
	fs.add_file(combine_path("readwritev", "3"), 81);
	fs.add_file(combine_path("readwritev", "4"), 0);
	fs.add_file(combine_path("readwritev", "5"), 6561);
	fs.set_piece_length(0x1000);
	fs.set_num_pieces((fs.total_size() + 0xfff) / 0x1000);
	test_read_fileop fop(10000000);
	storage_error ec;

	std::vector<char> buf(fs.total_size());
	file::iovec_t iov = { &buf[0]
		, static_cast<size_t>(fs.total_size()) };

	// read everything
	int ret = readwritev(fs, &iov, 0, 0, 1, fop, ec);

	TEST_EQUAL(ret, fs.total_size());
	TEST_CHECK(check_pattern(buf, 0));
}

TORRENT_TEST(move_storage_into_self)
{
	aux::session_settings set;
	file_storage fs;
	std::vector<char> buf;
	file_pool fp;
	io_service ios;
	disk_buffer_pool dp(16 * 1024, ios, boost::bind(&nop));
	boost::shared_ptr<default_storage> s = setup_torrent(fs, fp, buf
		, current_working_directory()
		, set);

	file::iovec_t b = {&buf[0], 4};
	storage_error se;
	s->writev(&b, 1, 2, 0, 0, se);

	s->move_storage(combine_path("temp_storage", "folder1"), 0, se);

	printf("move error: %s\n", se.ec.message().c_str());
#ifdef _WIN32
	TEST_EQUAL(se.ec, boost::system::errc::permission_denied);
#else
	TEST_EQUAL(se.ec, boost::system::errc::invalid_argument);
#endif
}

