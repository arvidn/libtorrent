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
#include "settings.hpp"

#include "libtorrent/storage.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/storage_utils.hpp"
#include "libtorrent/random.hpp"

#include <memory>
#include <functional> // for bind

#include <iostream>
#include <fstream>

#include <boost/variant/get.hpp>

using namespace std::placeholders;
using namespace lt;

namespace {

constexpr int piece_size = 16 * 1024 * 16;
constexpr int half = piece_size / 2;

void on_check_resume_data(status_t const status, storage_error const& error, bool* done)
{
	std::cout << time_now_string() << " on_check_resume_data ret: "
		<< static_cast<int>(status);
	switch (status)
	{
		case status_t::no_error:
			std::cout << time_now_string() << " success" << std::endl;
			break;
		case status_t::fatal_disk_error:
			std::cout << time_now_string() << " disk error: " << error.ec.message()
				<< " file: " << error.file() << std::endl;
			break;
		case status_t::need_full_check:
			std::cout << time_now_string() << " need full check" << std::endl;
			break;
		case status_t::file_exist:
			std::cout << time_now_string() << " file exist" << std::endl;
			break;
	}
	std::cout << std::endl;
	*done = true;
}

void on_piece_checked(piece_index_t, sha1_hash const&
	, storage_error const& error, bool* done)
{
	std::cout << time_now_string() << " on_piece_checked err: "
		<< error.ec.message() << '\n';
	*done = true;
}

void print_error(char const* call, int ret, storage_error const& ec)
{
	std::printf("%s: %s() returned: %d error: \"%s\" in file: %d operation: %s\n"
		, time_now_string(), call, ret, ec.ec.message().c_str()
		, static_cast<int>(ec.file()), operation_name(ec.operation));
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
			std::cout << "run_one: " << ec.message().c_str() << std::endl;
			return;
		}
		std::cout << time_now_string() << " done: " << done << std::endl;
	}
}

void nop() {}

std::shared_ptr<torrent_info> setup_torrent_info(file_storage& fs
	, std::vector<char>& buf)
{
	fs.add_file(combine_path("temp_storage", "test1.tmp"), 8);
	fs.add_file(combine_path("temp_storage", combine_path("folder1", "test2.tmp")), 8);
	fs.add_file(combine_path("temp_storage", combine_path("folder2", "test3.tmp")), 0);
	fs.add_file(combine_path("temp_storage", combine_path("_folder3", "test4.tmp")), 0);
	fs.add_file(combine_path("temp_storage", combine_path("_folder3", combine_path("subfolder", "test5.tmp"))), 8);
	lt::create_torrent t(fs, 4, -1, {});

	char buf_[4] = {0, 0, 0, 0};
	sha1_hash h = hasher(buf_).final();
	for (piece_index_t i(0); i < piece_index_t(6); ++i) t.set_hash(i, h);

	bencode(std::back_inserter(buf), t.generate());
	error_code ec;

	auto info = std::make_shared<torrent_info>(buf, ec, from_span);

	if (ec)
	{
		std::printf("torrent_info constructor failed: %s\n"
			, ec.message().c_str());
	}

	return info;
}

std::shared_ptr<default_storage> setup_torrent(file_storage& fs
	, file_pool& fp
	, std::vector<char>& buf
	, std::string const& test_path
	, aux::session_settings& set)
{
	std::shared_ptr<torrent_info> info = setup_torrent_info(fs, buf);

	aux::vector<download_priority_t, file_index_t> priorities;
	sha1_hash info_hash;
	storage_params p{
		fs,
		nullptr,
		test_path,
		storage_mode_allocate,
		priorities,
		info_hash
	};
	std::shared_ptr<default_storage> s(new default_storage(p, fp));
	s->m_settings = &set;

	// allocate the files and create the directories
	storage_error se;
	s->initialize(se);
	if (se)
	{
		TEST_ERROR(se.ec.message().c_str());
		std::printf("default_storage::initialize %s: %d\n"
			, se.ec.message().c_str(), static_cast<int>(se.file()));
	}

	return s;
}

std::vector<char> new_piece(std::size_t const size)
{
	std::vector<char> ret(size);
	aux::random_bytes(ret);
	return ret;
}

void run_storage_tests(std::shared_ptr<torrent_info> info
	, file_storage& fs
	, std::string const& test_path
	, lt::storage_mode_t storage_mode
	, bool unbuffered)
{
	TORRENT_ASSERT(fs.num_files() > 0);
	{
	error_code ec;
	create_directory(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cout << "create_directory '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	remove_all(combine_path(test_path, "temp_storage2"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "temp_storage2")
		<< "': " << ec.message() << std::endl;
	remove_all(combine_path(test_path, "part0"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "part0")
		<< "': " << ec.message() << std::endl;
	}
	int num_pieces = fs.num_pieces();
	TEST_CHECK(info->num_pieces() == num_pieces);

	std::vector<char> piece0 = new_piece(piece_size);
	std::vector<char> piece1 = new_piece(piece_size);
	std::vector<char> piece2 = new_piece(piece_size);

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
	disk_buffer_pool dp(ios, std::bind(&nop));

	aux::vector<download_priority_t, file_index_t> priorities;
	sha1_hash info_hash;
	storage_params p{
		fs,
		nullptr,
		test_path,
		storage_mode,
		priorities,
		info_hash
	};
	std::unique_ptr<storage_interface> s(new default_storage(p, fp));
	s->m_settings = &set;

	storage_error ec;
	s->initialize(ec);
	TEST_CHECK(!ec);
	if (ec) print_error("initialize", 0, ec);

	int ret = 0;

	// write piece 1 (in slot 0)
	iovec_t iov = { piece1.data(), half};
	ret = s->writev(iov, piece_index_t(0), 0, open_mode::read_write, ec);
	if (ret != half) print_error("writev", ret, ec);

	iov = { piece1.data() + half, half };
	ret = s->writev(iov, piece_index_t(0), half, open_mode::read_write, ec);
	if (ret != half) print_error("writev", ret, ec);

	// test unaligned read (where the bytes are aligned)
	iov = { piece + 3, piece_size - 9};
	ret = s->readv(iov, piece_index_t(0), 3, open_mode::read_write, ec);
	if (ret != piece_size - 9) print_error("readv",ret, ec);
	TEST_CHECK(std::equal(piece+3, piece + piece_size-9, piece1.data()+3));

	// test unaligned read (where the bytes are not aligned)
	iov = { piece, piece_size - 9};
	ret = s->readv(iov, piece_index_t(0), 3, open_mode::read_write, ec);
	TEST_CHECK(ret == piece_size - 9);
	if (ret != piece_size - 9) print_error("readv", ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size-9, piece1.data()+3));

	// verify piece 1
	iov = { piece, piece_size };
	ret = s->readv(iov, piece_index_t(0), 0, open_mode::read_write, ec);
	TEST_CHECK(ret == piece_size);
	if (ret != piece_size) print_error("readv", ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece1.data()));

	// do the same with piece 0 and 2 (in slot 1 and 2)
	iov = { piece0.data(), piece_size };
	ret = s->writev(iov, piece_index_t(1), 0, open_mode::read_write, ec);
	if (ret != piece_size) print_error("writev", ret, ec);

	iov = { piece2.data(), piece_size };
	ret = s->writev(iov, piece_index_t(2), 0, open_mode::read_write, ec);
	if (ret != piece_size) print_error("writev", ret, ec);

	// verify piece 0 and 2
	iov = { piece, piece_size };
	ret = s->readv(iov, piece_index_t(1), 0, open_mode::read_write, ec);
	if (ret != piece_size) print_error("readv", ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece0.data()));

	iov = { piece, piece_size };
	ret = s->readv(iov, piece_index_t(2), 0, open_mode::read_write, ec);
	if (ret != piece_size) print_error("readv", ret, ec);
	TEST_CHECK(std::equal(piece, piece + piece_size, piece2.data()));

	s->release_files(ec);
	}

	free(piece);
}

void test_remove(std::string const& test_path, bool unbuffered)
{
	error_code ec;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	TEST_CHECK(!exists(combine_path(test_path, "temp_storage")));

	file_storage fs;
	std::vector<char> buf;
	file_pool fp;
	io_service ios;
	disk_buffer_pool dp(ios, std::bind(&nop));

	aux::session_settings set;
	set.set_int(settings_pack::disk_io_write_mode
		, unbuffered ? settings_pack::disable_os_cache
		: settings_pack::enable_os_cache);
	set.set_int(settings_pack::disk_io_read_mode
		, unbuffered ? settings_pack::disable_os_cache
		: settings_pack::enable_os_cache);

	std::shared_ptr<default_storage> s = setup_torrent(fs, fp, buf, test_path, set);

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

	iovec_t b = {&buf[0], 4};
	storage_error se;
	s->writev(b, piece_index_t(2), 0, open_mode::read_write, se);

	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder1", "test2.tmp")))));
	TEST_CHECK(!exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", combine_path("subfolder", "test5.tmp"))))));
	file_status st;
	stat_file(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder1", "test2.tmp"))), &st, ec);
	TEST_EQUAL(st.file_size, 8);

	s->writev(b, piece_index_t(4), 0, open_mode::read_write, se);

	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", combine_path("subfolder", "test5.tmp"))))));
	stat_file(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", "test5.tmp"))), &st, ec);
	TEST_EQUAL(st.file_size, 8);

	s->delete_files(session::delete_files, se);
	if (se) print_error("delete_files", 0, se);

	if (se)
	{
		TEST_ERROR(se.ec.message().c_str());
		std::printf("default_storage::delete_files %s: %d\n"
			, se.ec.message().c_str(), static_cast<int>(se.file()));
	}

	TEST_CHECK(!exists(combine_path(test_path, "temp_storage")));
}

void test_rename(std::string const& test_path)
{
	error_code ec;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	TEST_CHECK(!exists(combine_path(test_path, "temp_storage")));

	file_storage fs;
	std::vector<char> buf;
	file_pool fp;
	io_service ios;
	disk_buffer_pool dp(ios, std::bind(&nop));
	aux::session_settings set;

	std::shared_ptr<default_storage> s = setup_torrent(fs, fp, buf, test_path
		, set);

	// directories are not created up-front, unless they contain
	// an empty file
	std::string first_file = fs.file_path(file_index_t(0));
	for (auto const i : fs.file_range())
	{
		TEST_CHECK(!exists(combine_path(test_path, combine_path("temp_storage"
			, fs.file_path(i)))));
	}

	storage_error se;
	s->rename_file(file_index_t(0), "new_filename", se);
	if (se.ec)
	{
		std::printf("default_storage::rename_file failed: %s\n"
			, se.ec.message().c_str());
	}
	TEST_CHECK(!se.ec);

	TEST_EQUAL(s->files().file_path(file_index_t(0)), "new_filename");
}

struct test_error_storage : default_storage
{
	explicit test_error_storage(storage_params const& params, file_pool& fp)
		: default_storage(params, fp) {}

	bool has_any_file(storage_error& ec) override
	{
		ec.ec = make_error_code(boost::system::errc::permission_denied);
		ec.operation = operation_t::file_stat;
		return false;
	}
};

storage_interface* error_storage_constructor(
	storage_params const& params, file_pool& fp)
{
	return new test_error_storage(params, fp);
}

void test_check_files(std::string const& test_path
	, lt::storage_mode_t storage_mode)
{
	std::shared_ptr<torrent_info> info;

	error_code ec;
	constexpr int piece_size_check = 16 * 1024;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", piece_size_check);
	fs.add_file("temp_storage/test2.tmp", piece_size_check * 2);
	fs.add_file("temp_storage/test3.tmp", piece_size_check);

	std::vector<char> piece0 = new_piece(piece_size_check);
	std::vector<char> piece2 = new_piece(piece_size_check);

	lt::create_torrent t(fs, piece_size_check, -1, {});
	t.set_hash(piece_index_t(0), hasher(piece0).final());
	t.set_hash(piece_index_t(1), {});
	t.set_hash(piece_index_t(2), {});
	t.set_hash(piece_index_t(3), hasher(piece2).final());

	create_directory(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cout << "create_directory: " << ec.message() << std::endl;

	std::ofstream f;
	f.open(combine_path(test_path, combine_path("temp_storage", "test1.tmp")).c_str()
		, std::ios::trunc | std::ios::binary);
	f.write(piece0.data(), piece_size_check);
	f.close();
	f.open(combine_path(test_path, combine_path("temp_storage", "test3.tmp")).c_str()
		, std::ios::trunc | std::ios::binary);
	f.write(piece2.data(), piece_size_check);
	f.close();

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = std::make_shared<torrent_info>(buf, ec, from_span);

	file_pool fp;
	boost::asio::io_service ios;
	counters cnt;

	aux::session_settings sett;
	sett.set_int(settings_pack::aio_threads, 1);
	disk_io_thread io(ios, sett, cnt);

	disk_buffer_pool dp(ios, std::bind(&nop));

	aux::vector<download_priority_t, file_index_t> priorities(
		std::size_t(info->num_files()), download_priority_t{});
	sha1_hash info_hash;
	storage_params p{
		fs,
		nullptr,
		test_path,
		storage_mode,
		priorities,
		info_hash
	};

	auto st = io.new_torrent(error_storage_constructor, std::move(p)
		, std::shared_ptr<void>());

	bool done = false;
	add_torrent_params frd;
	aux::vector<std::string, file_index_t> links;
	io.async_check_files(st, &frd, links
		, std::bind(&on_check_resume_data, _1, _2, &done));
	io.submit_jobs();
	ios.reset();
	run_until(ios, done);

	for (auto const i : info->piece_range())
	{
		done = false;
		io.async_hash(st, i, disk_interface::sequential_access | disk_interface::volatile_read
			, std::bind(&on_piece_checked, _1, _2, _3, &done));
		io.submit_jobs();
		ios.reset();
		run_until(ios, done);
	}

	io.abort(true);
}

// TODO: 2 split this test up into smaller parts
void run_test(bool unbuffered)
{
	std::string test_path = current_working_directory();
	std::cout << "\n=== " << test_path << " ===\n" << std::endl;

	std::shared_ptr<torrent_info> info;

	std::vector<char> piece0 = new_piece(piece_size);
	std::vector<char> piece1 = new_piece(piece_size);
	std::vector<char> piece2 = new_piece(piece_size);
	std::vector<char> piece3 = new_piece(piece_size);

	{
	error_code ec;
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", 17);
	fs.add_file("temp_storage/test2.tmp", 612);
	fs.add_file("temp_storage/test3.tmp", 0);
	fs.add_file("temp_storage/test4.tmp", 0);
	fs.add_file("temp_storage/test5.tmp", 3253);
	fs.add_file("temp_storage/test6.tmp", 841);
	int const last_file_size = 4 * int(piece_size) - int(fs.total_size());
	fs.add_file("temp_storage/test7.tmp", last_file_size);

	// File layout
	// +-+--+++-------+-------+----------------------------------------------------------------------------------------+
	// |1| 2||| file5 | file6 | file7                                                                                  |
	// +-+--+++-------+-------+----------------------------------------------------------------------------------------+
	// |                           |                           |                           |                           |
	// | piece 0                   | piece 1                   | piece 2                   | piece 3                   |

	lt::create_torrent t(fs, piece_size, -1, {});
	TEST_CHECK(t.num_pieces() == 4);
	t.set_hash(piece_index_t(0), hasher(piece0).final());
	t.set_hash(piece_index_t(1), hasher(piece1).final());
	t.set_hash(piece_index_t(2), hasher(piece2).final());
	t.set_hash(piece_index_t(3), hasher(piece3).final());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = std::make_shared<torrent_info>(buf, from_span);
	std::cout << "=== test 1 === " << (unbuffered?"unbuffered":"buffered") << std::endl;

	// run_storage_tests writes piece 0, 1 and 2. not 3
	run_storage_tests(info, fs, test_path, storage_mode_sparse, unbuffered);

	// make sure the files have the correct size
	std::string base = combine_path(test_path, "temp_storage");
	std::printf("base = \"%s\"\n", base.c_str());
	TEST_EQUAL(file_size(combine_path(base, "test1.tmp")), 17);
	TEST_EQUAL(file_size(combine_path(base, "test2.tmp")), 612);

	// these files should have been allocated as 0 size
	TEST_CHECK(exists(combine_path(base, "test3.tmp")));
	TEST_CHECK(exists(combine_path(base, "test4.tmp")));
	TEST_CHECK(file_size(combine_path(base, "test3.tmp")) == 0);
	TEST_CHECK(file_size(combine_path(base, "test4.tmp")) == 0);

	TEST_EQUAL(file_size(combine_path(base, "test5.tmp")), 3253);
	TEST_EQUAL(file_size(combine_path(base, "test6.tmp")), 841);
	std::printf("file: %d expected: %d last_file_size: %d, piece_size: %d\n"
		, int(file_size(combine_path(base, "test7.tmp")))
		, last_file_size - int(piece_size), last_file_size, int(piece_size));
	TEST_EQUAL(file_size(combine_path(base, "test7.tmp")), last_file_size - int(piece_size));
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;
	}

// ==============================================

	{
	error_code ec;
	file_storage fs;
	fs.add_file(combine_path("temp_storage", "test1.tmp"), 3 * piece_size);
	lt::create_torrent t(fs, piece_size, -1, {});
	TEST_CHECK(fs.file_path(file_index_t(0)) == combine_path("temp_storage", "test1.tmp"));
	t.set_hash(piece_index_t(0), hasher(piece0).final());
	t.set_hash(piece_index_t(1), hasher(piece1).final());
	t.set_hash(piece_index_t(2), hasher(piece2).final());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = std::make_shared<torrent_info>(buf, ec, from_span);

	std::cout << "=== test 3 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_sparse, unbuffered);

	TEST_EQUAL(file_size(combine_path(test_path, combine_path("temp_storage", "test1.tmp"))), piece_size * 3);
	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;

// ==============================================

	std::cout << "=== test 4 ===" << std::endl;

	run_storage_tests(info, fs, test_path, storage_mode_allocate, unbuffered);

	std::cout << file_size(combine_path(test_path, combine_path("temp_storage", "test1.tmp"))) << std::endl;
	TEST_EQUAL(file_size(combine_path(test_path, combine_path("temp_storage", "test1.tmp"))), 3 * piece_size);

	remove_all(combine_path(test_path, "temp_storage"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "temp_storage")
		<< "': " << ec.message() << std::endl;

	}

// ==============================================

	std::cout << "=== test 5 ===" << std::endl;
	test_remove(test_path, unbuffered);

// ==============================================

	std::cout << "=== test 6 ===" << std::endl;
	test_check_files(test_path, storage_mode_sparse);

	std::cout << "=== test 7 ===" << std::endl;
	test_rename(test_path);
}

void test_fastresume(bool const test_deprecated)
{
	std::string test_path = current_working_directory();
	error_code ec;
	std::cout << "\n\n=== test fastresume ===" << std::endl;
	remove_all(combine_path(test_path, "tmp1"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "tmp1")
		<< "': " << ec.message() << std::endl;
	create_directory(combine_path(test_path, "tmp1"), ec);
	if (ec) std::cout << "create_directory '" << combine_path(test_path, "tmp1")
		<< "': " << ec.message() << std::endl;
	std::ofstream file(combine_path(test_path, "tmp1/temporary").c_str());
	std::shared_ptr<torrent_info> t = ::create_torrent(&file);
	file.close();
	TEST_CHECK(exists(combine_path(test_path, "tmp1/temporary")));
	if (!exists(combine_path(test_path, "tmp1/temporary")))
		return;

	entry resume;
	{
		settings_pack pack = settings();
		lt::session ses(pack);

		add_torrent_params p;
		p.ti = std::make_shared<torrent_info>(std::cref(*t));
		p.save_path = combine_path(test_path, "tmp1");
		p.storage_mode = storage_mode_sparse;
		error_code ignore;
		torrent_handle h = ses.add_torrent(std::move(p), ignore);
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
			std::this_thread::sleep_for(lt::milliseconds(100));
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
		if (ra) resume = write_resume_data(alert_cast<save_resume_data_alert>(ra)->params);
		ses.remove_torrent(h, lt::session::delete_files);
		alert const* da = wait_for_alert(ses, torrent_deleted_alert::alert_type);
		TEST_CHECK(da);
	}
	TEST_CHECK(!exists(combine_path(test_path, combine_path("tmp1", "temporary"))));
	if (exists(combine_path(test_path, combine_path("tmp1", "temporary"))))
		return;

	std::cout << resume.to_string() << "\n";

	// make sure the fast resume check fails! since we removed the file
	{
		settings_pack pack = settings();
		lt::session ses(pack);

		std::vector<char> resume_data;
		bencode(std::back_inserter(resume_data), resume);

		add_torrent_params p;
		TORRENT_UNUSED(test_deprecated);
#if TORRENT_ABI_VERSION == 1
		if (test_deprecated)
		{
			p.resume_data = resume_data;
		}
		else
#endif
		{
			p = read_resume_data(resume_data);
		}

		p.flags &= ~torrent_flags::paused;
		p.flags &= ~torrent_flags::auto_managed;
		p.ti = std::make_shared<torrent_info>(std::cref(*t));
		p.save_path = combine_path(test_path, "tmp1");
		p.storage_mode = storage_mode_sparse;
		torrent_handle h = ses.add_torrent(std::move(p), ec);

		std::printf("expecting fastresume to be rejected becase the files were removed");
		alert const* a = wait_for_alert(ses, fastresume_rejected_alert::alert_type
			, "ses");
		// we expect the fast resume to be rejected because the files were removed
		TEST_CHECK(alert_cast<fastresume_rejected_alert>(a) != nullptr);
	}
	remove_all(combine_path(test_path, "tmp1"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "tmp1")
		<< "': " << ec.message() << std::endl;
}

} // anonymous namespace

TORRENT_TEST(fastresume)
{
	test_fastresume(false);
}

#if TORRENT_ABI_VERSION == 1
TORRENT_TEST(fastresume_deprecated)
{
	test_fastresume(true);
}
#endif

namespace {

bool got_file_rename_alert(alert const* a)
{
	return alert_cast<lt::file_renamed_alert>(a)
		|| alert_cast<lt::file_rename_failed_alert>(a);
}

} // anonymous namespace

TORRENT_TEST(rename_file)
{
	std::vector<char> buf;
	file_storage fs;
	std::shared_ptr<torrent_info> info = setup_torrent_info(fs, buf);

	settings_pack pack = settings();
	pack.set_bool(settings_pack::disable_hash_checks, true);
	lt::session ses(pack);

	add_torrent_params p;
	p.ti = info;
	p.save_path = ".";
	error_code ec;
	torrent_handle h = ses.add_torrent(std::move(p), ec);

	// make it a seed
	std::vector<char> tmp(std::size_t(info->piece_length()));
	for (auto const i : fs.piece_range())
		h.add_piece(i, &tmp[0]);

	// wait for the files to have been written

	for (int i = 0; i < info->num_pieces(); ++i)
	{
		alert const* pf = wait_for_alert(ses, piece_finished_alert::alert_type
			, "ses", pop_alerts::cache_alerts);
		TEST_CHECK(pf);
	}

	// now rename them. This is the test
	for (auto const i : fs.file_range())
	{
		std::string name = fs.file_path(i);
		h.rename_file(i, "temp_storage__" + name.substr(12));
	}

	// wait for the files to have been renamed
	for (int i = 0; i < info->num_files(); ++i)
	{
		alert const* fra = wait_for_alert(ses, file_renamed_alert::alert_type
			, "ses", pop_alerts::cache_alerts);
		TEST_CHECK(fra);
	}

	TEST_CHECK(exists(info->name() + "__"));

	h.save_resume_data();
	alert const* ra = wait_for_alert(ses, save_resume_data_alert::alert_type);
	TEST_CHECK(ra);
	if (!ra) return;
	add_torrent_params resume = alert_cast<save_resume_data_alert>(ra)->params;

	auto const files = resume.renamed_files;
	for (auto const& i : files)
	{
		TEST_EQUAL(i.second.substr(0, 14), "temp_storage__");
	}
}

namespace {

void test_rename_file_fastresume(bool test_deprecated)
{
	std::string test_path = current_working_directory();
	error_code ec;
	std::cout << "\n\n=== test rename file in fastresume ===" << std::endl;
	remove_all(combine_path(test_path, "tmp2"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "tmp2")
		<< "': " << ec.message() << std::endl;
	create_directory(combine_path(test_path, "tmp2"), ec);
	if (ec) std::cout << "create_directory: " << ec.message() << std::endl;
	std::ofstream file(combine_path(test_path, "tmp2/temporary").c_str());
	std::shared_ptr<torrent_info> t = ::create_torrent(&file);
	file.close();
	TEST_CHECK(exists(combine_path(test_path, "tmp2/temporary")));

	add_torrent_params resume;
	{
		settings_pack pack = settings();
		lt::session ses(pack);

		add_torrent_params p;
		p.ti = std::make_shared<torrent_info>(std::cref(*t));
		p.save_path = combine_path(test_path, "tmp2");
		p.storage_mode = storage_mode_sparse;
		torrent_handle h = ses.add_torrent(std::move(p), ec);

		h.rename_file(file_index_t(0), "testing_renamed_files");
		std::cout << "renaming file" << std::endl;
		bool renamed = false;
		for (int i = 0; i < 30; ++i)
		{
			if (print_alerts(ses, "ses", true, true, &got_file_rename_alert)) renamed = true;
			torrent_status s = h.status();
			if (s.state == torrent_status::seeding && renamed) break;
			std::this_thread::sleep_for(lt::milliseconds(100));
		}
		std::cout << "stop loop" << std::endl;
		torrent_status s = h.status();
		TEST_CHECK(s.state == torrent_status::seeding);

		h.save_resume_data();
		alert const* ra = wait_for_alert(ses, save_resume_data_alert::alert_type);
		TEST_CHECK(ra);
		if (ra) resume = alert_cast<save_resume_data_alert>(ra)->params;
		ses.remove_torrent(h);
	}
	TEST_CHECK(!exists(combine_path(test_path, "tmp2/temporary")));
	TEST_CHECK(exists(combine_path(test_path, "tmp2/testing_renamed_files")));
	TEST_CHECK(!resume.renamed_files.empty());

	entry resume_ent = write_resume_data(resume);

	std::cout << resume_ent.to_string() << "\n";

	// make sure the fast resume check succeeds, even though we renamed the file
	{
		settings_pack pack = settings();
		lt::session ses(pack);

		add_torrent_params p;
		std::vector<char> resume_data;
		bencode(std::back_inserter(resume_data), resume_ent);
		TORRENT_UNUSED(test_deprecated);
#if TORRENT_ABI_VERSION == 1
		if (test_deprecated)
		{
			p.resume_data = resume_data;
		}
		else
#endif
		{
			p = read_resume_data(resume_data);
		}
		p.ti = std::make_shared<torrent_info>(std::cref(*t));
		p.save_path = combine_path(test_path, "tmp2");
		p.storage_mode = storage_mode_sparse;
		torrent_handle h = ses.add_torrent(std::move(p), ec);

		torrent_status stat;
		for (int i = 0; i < 50; ++i)
		{
			stat = h.status();
			print_alerts(ses, "ses");
			if (stat.state == torrent_status::seeding)
				break;
			std::this_thread::sleep_for(lt::milliseconds(100));
		}
		TEST_CHECK(stat.state == torrent_status::seeding);

		h.save_resume_data();
		alert const* ra = wait_for_alert(ses, save_resume_data_alert::alert_type);
		TEST_CHECK(ra);
		if (ra) resume = alert_cast<save_resume_data_alert>(ra)->params;
		ses.remove_torrent(h);
	}
	TEST_CHECK(!resume.renamed_files.empty());

	resume_ent = write_resume_data(resume);
	std::cout << resume_ent.to_string() << "\n";

	remove_all(combine_path(test_path, "tmp2"), ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
		std::cout << "remove_all '" << combine_path(test_path, "tmp2")
		<< "': " << ec.message() << std::endl;
}

} // anonymous namespace

TORRENT_TEST(rename_file_fastresume)
{
	test_rename_file_fastresume(false);
}

#if TORRENT_ABI_VERSION == 1
TORRENT_TEST(rename_file_fastresume_deprecated)
{
	test_rename_file_fastresume(true);
}
#endif

namespace {

void alloc_iov(iovec_t* iov, int num_bufs)
{
	for (int i = 0; i < num_bufs; ++i)
	{
		iov[i] = { new char[static_cast<std::size_t>(num_bufs * (i + 1))]
			, num_bufs * (i + 1) };
	}
}

// TODO: this should take a span of iovec_ts
void fill_pattern(iovec_t* iov, int num_bufs)
{
	int counter = 0;
	for (int i = 0; i < num_bufs; ++i)
	{
		for (char& v : iov[i])
		{
			v = char(counter & 0xff);
			++counter;
		}
	}
}

bool check_pattern(std::vector<char> const& buf, int counter)
{
	unsigned char const* p = reinterpret_cast<unsigned char const*>(buf.data());
	for (int k = 0; k < int(buf.size()); ++k)
	{
		if (p[k] != (counter & 0xff)) return false;
		++counter;
	}
	return true;
}

// TODO: this should take a span
void free_iov(iovec_t* iov, int num_bufs)
{
	for (int i = 0; i < num_bufs; ++i)
	{
		delete[] iov[i].data();
		iov[i] = { nullptr, 0 };
	}
}

} // anonymous namespace

TORRENT_TEST(iovec_copy_bufs)
{
	iovec_t iov1[10];
	iovec_t iov2[10];

	alloc_iov(iov1, 10);
	fill_pattern(iov1, 10);

	TEST_CHECK(bufs_size({iov1, 10}) >= 106);

	// copy exactly 106 bytes from iov1 to iov2
	int num_bufs = aux::copy_bufs(iov1, 106, iov2);

	// verify that the first 100 bytes is pattern 1
	// and that the remaining bytes are pattern 2

	int counter = 0;
	for (int i = 0; i < num_bufs; ++i)
	{
		for (char v : iov2[i])
		{
			TEST_EQUAL(int(v), (counter & 0xff));
			++counter;
		}
	}
	TEST_EQUAL(counter, 106);

	free_iov(iov1, 10);
}

TORRENT_TEST(iovec_clear_bufs)
{
	iovec_t iov[10];
	alloc_iov(iov, 10);
	fill_pattern(iov, 10);

	lt::aux::clear_bufs({iov, 10});
	for (int i = 0; i < 10; ++i)
	{
		for (char v : iov[i])
		{
			TEST_EQUAL(int(v), 0);
		}
	}
	free_iov(iov, 10);
}

TORRENT_TEST(iovec_bufs_size)
{
	iovec_t iov[10];

	for (int i = 1; i < 10; ++i)
	{
		alloc_iov(iov, i);

		int expected_size = 0;
		for (int k = 0; k < i; ++k) expected_size += i * (k + 1);
		TEST_EQUAL(bufs_size({iov, i}), expected_size);

		free_iov(iov, i);
	}
}

TORRENT_TEST(iovec_advance_bufs)
{
	iovec_t iov1[10];
	iovec_t iov2[10];
	alloc_iov(iov1, 10);
	fill_pattern(iov1, 10);

	memcpy(iov2, iov1, sizeof(iov1));

	span<iovec_t> iov = iov2;

	// advance iov 13 bytes. Make sure what's left fits pattern 1 shifted
	// 13 bytes
	iov = aux::advance_bufs(iov, 13);

	// make sure what's in
	int counter = 13;
	for (auto buf : iov)
	{
		for (char v : buf)
		{
			TEST_EQUAL(v, static_cast<char>(counter));
			++counter;
		}
	}

	free_iov(iov1, 10);
}

TORRENT_TEST(unbuffered) { run_test(true); }
TORRENT_TEST(buffered) { run_test(false); }

namespace {

file_storage make_fs()
{
	file_storage fs;
	fs.add_file(combine_path("readwritev", "1"), 3);
	fs.add_file(combine_path("readwritev", "2"), 9);
	fs.add_file(combine_path("readwritev", "3"), 81);
	fs.add_file(combine_path("readwritev", "4"), 6561);
	fs.set_piece_length(0x1000);
	fs.set_num_pieces(int((fs.total_size() + 0xfff) / 0x1000));
	return fs;
}

struct test_fileop
{
	explicit test_fileop(int stripe_size) : m_stripe_size(stripe_size) {}

	int operator()(file_index_t const file_index, std::int64_t const file_offset
		, span<iovec_t const> bufs, storage_error&)
	{
		std::size_t offset = size_t(file_offset);
		if (file_index >= m_file_data.end_index())
		{
			m_file_data.resize(static_cast<int>(file_index) + 1);
		}

		std::size_t const write_size = std::size_t(std::min(m_stripe_size, bufs_size(bufs)));

		std::vector<char>& file = m_file_data[file_index];

		if (offset + write_size > file.size())
		{
			file.resize(offset + write_size);
		}

		int left = int(write_size);
		while (left > 0)
		{
			std::size_t const copy_size = std::size_t(std::min(left, int(bufs.front().size())));
			std::memcpy(&file[offset], bufs.front().data(), copy_size);
			bufs = bufs.subspan(1);
			offset += copy_size;
			left -= int(copy_size);
		}
		return int(write_size);
	}

	int m_stripe_size;
	aux::vector<std::vector<char>, file_index_t> m_file_data;
};

struct test_read_fileop
{
	// EOF after size bytes read
	explicit test_read_fileop(int size) : m_size(size), m_counter(0) {}

	int operator()(file_index_t, std::int64_t /*file_offset*/
		, span<iovec_t const> bufs, storage_error&)
	{
		int local_size = std::min(m_size, bufs_size(bufs));
		const int read = local_size;
		while (local_size > 0)
		{
			int const len = std::min(int(bufs.front().size()), local_size);
			auto local_buf = bufs.front().first(len);
			for (char& v : local_buf)
			{
				v = char(m_counter & 0xff);
				++m_counter;
			}
			local_size -= len;
			m_size -= len;
			bufs = bufs.subspan(1);
		}
		return read;
	}

	int m_size;
	int m_counter;
};

struct test_error_fileop
{
	// EOF after size bytes read
	explicit test_error_fileop(file_index_t error_file)
		: m_error_file(error_file) {}

	int operator()(file_index_t const file_index, std::int64_t /*file_offset*/
		, span<iovec_t const> bufs, storage_error& ec)
	{
		if (m_error_file == file_index)
		{
			ec.file(file_index);
			ec.ec.assign(boost::system::errc::permission_denied
				, boost::system::generic_category());
			ec.operation = operation_t::file_read;
			return -1;
		}
		return bufs_size(bufs);
	}

	file_index_t m_error_file;
};

int count_bufs(iovec_t const* bufs, int bytes)
{
	int size = 0;
	int count = 1;
	if (bytes == 0) return 0;
	for (iovec_t const* i = bufs;; ++i, ++count)
	{
		size += int(i->size());
		if (size >= bytes) return count;
	}
}

} // anonymous namespace

TORRENT_TEST(readwritev_stripe_1)
{
	const int num_bufs = 30;
	iovec_t iov[num_bufs];

	alloc_iov(iov, num_bufs);
	fill_pattern(iov, num_bufs);

	file_storage fs = make_fs();
	test_fileop fop(1);
	storage_error ec;

	TEST_CHECK(bufs_size({iov, num_bufs}) >= fs.total_size());

	iovec_t iov2[num_bufs];
	aux::copy_bufs(iov, int(fs.total_size()), iov2);
	int num_bufs2 = count_bufs(iov2, int(fs.total_size()));
	TEST_CHECK(num_bufs2 <= num_bufs);

	int ret = readwritev(fs, {iov2, num_bufs2}, piece_index_t(0), 0, ec
		, std::ref(fop));

	TEST_EQUAL(ret, fs.total_size());
	TEST_EQUAL(fop.m_file_data.size(), 4);
	TEST_EQUAL(fop.m_file_data[file_index_t(0)].size(), 3);
	TEST_EQUAL(fop.m_file_data[file_index_t(1)].size(), 9);
	TEST_EQUAL(fop.m_file_data[file_index_t(2)].size(), 81);
	TEST_EQUAL(fop.m_file_data[file_index_t(3)].size(), 6561);

	TEST_CHECK(check_pattern(fop.m_file_data[file_index_t(0)], 0));
	TEST_CHECK(check_pattern(fop.m_file_data[file_index_t(1)], 3));
	TEST_CHECK(check_pattern(fop.m_file_data[file_index_t(2)], 3 + 9));
	TEST_CHECK(check_pattern(fop.m_file_data[file_index_t(3)], 3 + 9 + 81));

	free_iov(iov, num_bufs);
}

TORRENT_TEST(readwritev_single_buffer)
{
	file_storage fs = make_fs();
	test_fileop fop(10000000);
	storage_error ec;

	std::vector<char> buf(size_t(fs.total_size()));
	iovec_t iov = { &buf[0], int(buf.size()) };
	fill_pattern(&iov, 1);

	int ret = readwritev(fs, iov, piece_index_t(0), 0, ec, std::ref(fop));

	TEST_EQUAL(ret, fs.total_size());
	TEST_EQUAL(fop.m_file_data.size(), 4);
	TEST_EQUAL(fop.m_file_data[file_index_t(0)].size(), 3);
	TEST_EQUAL(fop.m_file_data[file_index_t(1)].size(), 9);
	TEST_EQUAL(fop.m_file_data[file_index_t(2)].size(), 81);
	TEST_EQUAL(fop.m_file_data[file_index_t(3)].size(), 6561);

	TEST_CHECK(check_pattern(fop.m_file_data[file_index_t(0)], 0));
	TEST_CHECK(check_pattern(fop.m_file_data[file_index_t(1)], 3));
	TEST_CHECK(check_pattern(fop.m_file_data[file_index_t(2)], 3 + 9));
	TEST_CHECK(check_pattern(fop.m_file_data[file_index_t(3)], 3 + 9 + 81));
}

TORRENT_TEST(readwritev_read)
{
	file_storage fs = make_fs();
	test_read_fileop fop(10000000);
	storage_error ec;

	std::vector<char> buf(size_t(fs.total_size()));
	iovec_t iov = { &buf[0], int(buf.size()) };

	// read everything
	int ret = readwritev(fs, iov, piece_index_t(0), 0, ec, std::ref(fop));

	TEST_EQUAL(ret, fs.total_size());
	TEST_CHECK(check_pattern(buf, 0));
}

TORRENT_TEST(readwritev_read_short)
{
	file_storage fs = make_fs();
	test_read_fileop fop(100);
	storage_error ec;

	std::vector<char> buf(size_t(fs.total_size()));
	iovec_t iov = { buf.data(), static_cast<std::ptrdiff_t>(fs.total_size()) };

	// read everything
	int ret = readwritev(fs, iov, piece_index_t(0), 0, ec, std::ref(fop));

	TEST_EQUAL(static_cast<int>(ec.file()), 3);

	TEST_EQUAL(ret, 100);
	buf.resize(100);
	TEST_CHECK(check_pattern(buf, 0));
}

TORRENT_TEST(readwritev_error)
{
	file_storage fs = make_fs();
	test_error_fileop fop(file_index_t(2));
	storage_error ec;

	std::vector<char> buf(size_t(fs.total_size()));
	iovec_t iov = { buf.data(), static_cast<std::ptrdiff_t>(fs.total_size()) };

	// read everything
	int ret = readwritev(fs, iov, piece_index_t(0), 0, ec, std::ref(fop));

	TEST_EQUAL(ret, -1);
	TEST_EQUAL(static_cast<int>(ec.file()), 2);
	TEST_CHECK(ec.operation == operation_t::file_read);
	TEST_EQUAL(ec.ec, boost::system::errc::permission_denied);
	std::printf("error: %s\n", ec.ec.message().c_str());
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
	fs.set_num_pieces(int((fs.total_size() + 0xfff) / 0x1000));
	test_read_fileop fop(10000000);
	storage_error ec;

	std::vector<char> buf(size_t(fs.total_size()));
	iovec_t iov = { buf.data(), static_cast<std::ptrdiff_t>(fs.total_size()) };

	// read everything
	int ret = readwritev(fs, iov, piece_index_t(0), 0, ec, std::ref(fop));

	TEST_EQUAL(ret, fs.total_size());
	TEST_CHECK(check_pattern(buf, 0));
}

namespace {

void delete_dirs(std::string path)
{
	error_code ec;
	remove_all(path, ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
	{
		std::printf("remove_all \"%s\": %s\n"
			, path.c_str(), ec.message().c_str());
	}
	TEST_CHECK(!exists(path));
}

} // anonymous namespace

TORRENT_TEST(move_storage_to_self)
{
	// call move_storage with the path to the exising storage. should be a no-op
	std::string const save_path = current_working_directory();
	std::string const test_path = combine_path(save_path, "temp_storage");
	delete_dirs(test_path);

	aux::session_settings set;
	file_storage fs;
	std::vector<char> buf;
	file_pool fp;
	io_service ios;
	disk_buffer_pool dp(ios, std::bind(&nop));
	std::shared_ptr<default_storage> s = setup_torrent(fs, fp, buf, save_path, set);

	iovec_t const b = {&buf[0], 4};
	storage_error se;
	s->writev(b, piece_index_t(1), 0, open_mode::read_write, se);

	TEST_CHECK(exists(combine_path(test_path, combine_path("folder2", "test3.tmp"))));
	TEST_CHECK(exists(combine_path(test_path, combine_path("_folder3", "test4.tmp"))));

	s->move_storage(save_path, move_flags_t::always_replace_files, se);
	TEST_EQUAL(se.ec, boost::system::errc::success);

	TEST_CHECK(exists(test_path));

	TEST_CHECK(exists(combine_path(test_path, combine_path("folder2", "test3.tmp"))));
	TEST_CHECK(exists(combine_path(test_path, combine_path("_folder3", "test4.tmp"))));
}

TORRENT_TEST(move_storage_into_self)
{
	std::string const save_path = current_working_directory();
	delete_dirs(combine_path(save_path, "temp_storage"));

	aux::session_settings set;
	file_storage fs;
	std::vector<char> buf;
	file_pool fp;
	io_service ios;
	disk_buffer_pool dp(ios, std::bind(&nop));
	std::shared_ptr<default_storage> s = setup_torrent(fs, fp, buf, save_path, set);

	iovec_t const b = {&buf[0], 4};
	storage_error se;
	s->writev(b, piece_index_t(2), 0, open_mode::read_write, se);

	std::string const test_path = combine_path(save_path, combine_path("temp_storage", "folder1"));
	s->move_storage(test_path, move_flags_t::always_replace_files, se);
	TEST_EQUAL(se.ec, boost::system::errc::success);

	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder1", "test2.tmp")))));

	// these directories and files are created up-front because they are empty files
	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder2", "test3.tmp")))));
	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", "test4.tmp")))));
}

TORRENT_TEST(storage_paths_string_pooling)
{
	file_storage file_storage;
	file_storage.add_file(combine_path("test_storage", "root.txt"), 0x4000);
	file_storage.add_file(combine_path("test_storage", combine_path("sub", "test1.txt")), 0x4000);
	file_storage.add_file(combine_path("test_storage", combine_path("sub", "test2.txt")), 0x4000);
	file_storage.add_file(combine_path("test_storage", combine_path("sub", "test3.txt")), 0x4000);

	// "sub" paths should point to same string item, so paths.size() must not grow
	TEST_CHECK(file_storage.paths().size() <= 2);
}

TORRENT_TEST(dont_move_intermingled_files)
{
	std::string const save_path = combine_path(current_working_directory(), "save_path_1");
	delete_dirs(combine_path(save_path, "temp_storage"));

	std::string test_path = combine_path(current_working_directory(), "save_path_2");
	delete_dirs(combine_path(test_path, "temp_storage"));

	aux::session_settings set;
	file_storage fs;
	std::vector<char> buf;
	file_pool fp;
	io_service ios;
	disk_buffer_pool dp(ios, std::bind(&nop));
	std::shared_ptr<default_storage> s = setup_torrent(fs, fp, buf, save_path, set);

	iovec_t b = {&buf[0], 4};
	storage_error se;
	s->writev(b, piece_index_t(2), 0, open_mode::read_write, se);

	error_code ec;
	create_directory(combine_path(save_path, combine_path("temp_storage"
		, combine_path("_folder3", "alien_folder1"))), ec);
	TEST_EQUAL(ec, boost::system::errc::success);
	file f;
	f.open(combine_path(save_path, combine_path("temp_storage", "alien1.tmp"))
		, open_mode::write_only, ec);
	f.close();
	TEST_EQUAL(ec, boost::system::errc::success);
	f.open(combine_path(save_path, combine_path("temp_storage"
		, combine_path("folder1", "alien2.tmp"))), open_mode::write_only, ec);
	f.close();
	TEST_EQUAL(ec, boost::system::errc::success);

	s->move_storage(test_path, move_flags_t::always_replace_files, se);
	TEST_EQUAL(se.ec, boost::system::errc::success);

	// torrent files moved to new place
	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder1", "test2.tmp")))));
	// these directories and files are created up-front because they are empty files
	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder2", "test3.tmp")))));
	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", "test4.tmp")))));

	// intermingled files and directories are still in old place
	TEST_CHECK(exists(combine_path(save_path, combine_path("temp_storage"
		, "alien1.tmp"))));
	TEST_CHECK(!exists(combine_path(test_path, combine_path("temp_storage"
		, "alien1.tmp"))));
	TEST_CHECK(exists(combine_path(save_path, combine_path("temp_storage"
		, combine_path("folder1", "alien2.tmp")))));
	TEST_CHECK(!exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder1", "alien2.tmp")))));
	TEST_CHECK(exists(combine_path(save_path, combine_path("temp_storage"
		, combine_path("_folder3", "alien_folder1")))));
	TEST_CHECK(!exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", "alien_folder1")))));
}
