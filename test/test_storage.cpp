/*

Copyright (c) 2005, 2007-2010, 2012-2022, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016-2018, Steven Siloti
Copyright (c) 2016, Vladimir Golovnev
Copyright (c) 2018, d-komarov
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

#include "libtorrent/mmap_storage.hpp"
#include "libtorrent/aux_/posix_storage.hpp"
#include "libtorrent/aux_/file_view_pool.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/storage_utils.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/mmap_disk_io.hpp"
#include "libtorrent/posix_disk_io.hpp"
#include "libtorrent/flags.hpp"

#include <memory>
#include <functional> // for bind

#include <iostream>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/variant/get.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

using namespace std::placeholders;
using namespace lt;

#if ! TORRENT_HAVE_MMAP && ! TORRENT_HAVE_MAP_VIEW_OF_FILE
namespace libtorrent {
namespace aux {
	struct file_view_pool {};
}
}
#endif

namespace {

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
using lt::mmap_storage;
#endif
using lt::aux::posix_storage;

constexpr int piece_size = 16 * 1024 * 16;
constexpr int half = piece_size / 2;

void delete_dirs(std::string path)
{
	path = complete(path);
	error_code ec;
	remove_all(path, ec);
	if (ec && ec != boost::system::errc::no_such_file_or_directory)
	{
		std::printf("remove_all \"%s\": %s\n"
			, path.c_str(), ec.message().c_str());
	}
	TEST_CHECK(!exists(path));
}

void on_check_resume_data(lt::status_t const status, storage_error const& error, bool* done, bool* oversized)
{
	std::cout << time_now_string() << " on_check_resume_data ret: "
		<< static_cast<int>(status);
	if ((status & lt::status_t::oversized_file) != status_t{})
	{
		std::cout << " oversized file(s) - ";
		*oversized = true;
	}
	else
	{
		*oversized = false;
	}

	switch (status & ~lt::status_t::mask)
	{
		case lt::status_t::no_error:
			std::cout << " success" << std::endl;
			break;
		case lt::status_t::fatal_disk_error:
			std::cout << " disk error: " << error.ec.message()
				<< " file: " << error.file() << std::endl;
			break;
		case lt::status_t::need_full_check:
			std::cout << " need full check" << std::endl;
			break;
		case lt::status_t::file_exist:
			std::cout << " file exist" << std::endl;
			break;
		case lt::status_t::mask:
		case lt::status_t::oversized_file:
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
		, time_now_string().c_str(), call, ret, ec.ec.message().c_str()
		, static_cast<int>(ec.file()), operation_name(ec.operation));
}

void run_until(io_context& ios, bool const& done)
{
	while (!done)
	{
		ios.restart();
		ios.run_one();
		std::cout << time_now_string() << " done: " << done << std::endl;
	}
}

std::shared_ptr<torrent_info> setup_torrent_info(file_storage& fs
	, std::vector<char>& buf)
{
	fs.add_file(combine_path("temp_storage", "test1.tmp"), 0x8000);
	fs.add_file(combine_path("temp_storage", combine_path("folder1", "test2.tmp")), 0x8000);
	fs.add_file(combine_path("temp_storage", combine_path("folder2", "test3.tmp")), 0);
	fs.add_file(combine_path("temp_storage", combine_path("_folder3", "test4.tmp")), 0);
	fs.add_file(combine_path("temp_storage", combine_path("_folder3", combine_path("subfolder", "test5.tmp"))), 0x8000);
	lt::create_torrent t(fs, 0x4000);

	sha1_hash h = hasher(std::vector<char>(0x4000, 0)).final();
	for (piece_index_t i(0); i < 6_piece; ++i) t.set_hash(i, h);

	bencode(std::back_inserter(buf), t.generate());
	error_code ec;

	auto info = std::make_shared<torrent_info>(buf, ec, from_span);

	if (ec)
	{
		std::printf("torrent_info constructor failed: %s\n"
			, ec.message().c_str());
		throw system_error(ec);
	}

	return info;
}

// file_pool_type is a meta function returning the file pool type for a specific
// StorageType
// maybe this would be easier to follow if it was all bundled up in a
// traits class
template <typename StorageType>
struct file_pool_type {};

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
template <>
struct file_pool_type<mmap_storage>
{
	using type = aux::file_view_pool;
};
#endif

template <>
struct file_pool_type<posix_storage>
{
	using type = int;
};

template <typename StorageType>
std::shared_ptr<StorageType> make_storage(storage_params const& p
	, typename file_pool_type<StorageType>::type& fp);

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
template <>
std::shared_ptr<mmap_storage> make_storage(storage_params const& p
	, aux::file_view_pool& fp)
{
	return std::make_shared<mmap_storage>(p, fp);
}
#endif

template <>
std::shared_ptr<posix_storage> make_storage(storage_params const& p
	, int&)
{
	return std::make_shared<posix_storage>(p);
}

template <typename StorageType, typename FilePool>
std::shared_ptr<StorageType> setup_torrent(file_storage& fs
	, FilePool& fp
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
	auto s = make_storage<StorageType>(p, fp);

	// allocate the files and create the directories
	storage_error se;
	s->initialize(set, se);
	if (se)
	{
		TEST_ERROR(se.ec.message().c_str());
		std::printf("mmap_storage::initialize %s: %d\n"
			, se.ec.message().c_str(), static_cast<int>(se.file()));
		throw system_error(se.ec);
	}

	return s;
}

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
int write(std::shared_ptr<mmap_storage> s
	, aux::session_settings const& sett
	, span<char> buf
	, piece_index_t const piece, int const offset
	, aux::open_mode_t const mode
	, storage_error& error)
{
	return s->write(sett, buf, piece, offset, mode, disk_job_flags_t{}, error);
}

int read(std::shared_ptr<mmap_storage> s
	, aux::session_settings const& sett
	, span<char> buf
	, piece_index_t piece
	, int const offset
	, aux::open_mode_t mode
	, storage_error& ec)
{
	return s->read(sett, buf, piece, offset, mode, disk_job_flags_t{}, ec);
}

void release_files(std::shared_ptr<mmap_storage> s, storage_error& ec)
{
	s->release_files(ec);
}
#endif

int write(std::shared_ptr<posix_storage> s
	, aux::session_settings const& sett
	, span<char> buf
	, piece_index_t const piece
	, int const offset
	, aux::open_mode_t
	, storage_error& error)
{
	return s->write(sett, buf, piece, offset, error);
}

int read(std::shared_ptr<posix_storage> s
	, aux::session_settings const& sett
	, span<char> buf
	, piece_index_t piece
	, int offset
	, aux::open_mode_t
	, storage_error& ec)
{
	return s->read(sett, buf, piece, offset, ec);
}

void release_files(std::shared_ptr<posix_storage>, storage_error&) {}

std::vector<char> new_piece(std::size_t const size)
{
	std::vector<char> ret(size);
	aux::random_bytes(ret);
	return ret;
}

template <typename StorageType>
void run_storage_tests(std::shared_ptr<torrent_info> info
	, file_storage& fs
	, lt::storage_mode_t storage_mode)
{
	TORRENT_ASSERT(fs.num_files() > 0);
	{
	error_code ec;
	create_directory(complete("temp_storage"), ec);
	if (ec) std::cout << "create_directory '" << complete("temp_storage")
		<< "': " << ec.message() << std::endl;
	}
	int const num_pieces = fs.num_pieces();
	TEST_EQUAL(info->num_pieces(), num_pieces);

	std::vector<char> piece0 = new_piece(piece_size);
	std::vector<char> piece1 = new_piece(piece_size);
	std::vector<char> piece2 = new_piece(piece_size);

	aux::session_settings set;

	std::vector<char> piece(piece_size);

	{
	// avoid having two storages use the same files
	typename file_pool_type<StorageType>::type fp;
	boost::asio::io_context ios;
	aux::vector<download_priority_t, file_index_t> priorities;
	sha1_hash info_hash;
	std::string const cwd = current_working_directory();
	storage_params p{
		fs,
		nullptr,
		cwd,
		storage_mode,
		priorities,
		info_hash
	};
	auto s = make_storage<StorageType>(p, fp);

	storage_error ec;
	s->initialize(set, ec);
	TEST_CHECK(!ec);
	if (ec) print_error("initialize", 0, ec);

	int ret = 0;

	// write piece 1 (in slot 0)
	span<char> iov = span<char>(piece1).first(half);

	ret = write(s, set, iov, 0_piece, 0, aux::open_mode::write, ec);
	TEST_EQUAL(ret, int(iov.size()));
	if (ret != int(iov.size())) print_error("write", ret, ec);

	iov = span<char>(piece1).last(half);
	ret = write(s, set, iov, 0_piece, half, aux::open_mode::write, ec);
	TEST_EQUAL(ret, int(iov.size()));
	if (ret != int(iov.size())) print_error("write", ret, ec);

	// test unaligned read (where the bytes are aligned)
	iov = span<char>(piece).subspan(3, piece_size - 9);
	ret = read(s, set, iov, 0_piece, 3, aux::open_mode::write, ec);
	TEST_EQUAL(ret, int(iov.size()));
	if (ret != int(iov.size())) print_error("read",ret, ec);
	TEST_CHECK(iov == span<char>(piece1).subspan(3, piece_size - 9));

	// test unaligned read (where the bytes are not aligned)
	iov = span<char>(piece).first(piece_size - 9);
	ret = read(s, set, iov, 0_piece, 3, aux::open_mode::write, ec);
	TEST_EQUAL(ret, int(iov.size()));
	if (ret != int(iov.size())) print_error("read", ret, ec);
	TEST_CHECK(iov == span<char>(piece1).subspan(3, piece_size - 9));

	// verify piece 1
	iov = piece;
	ret = read(s, set, iov, 0_piece, 0, aux::open_mode::write, ec);
	TEST_EQUAL(ret, int(iov.size()));
	if (ret != int(iov.size())) print_error("read", ret, ec);
	TEST_CHECK(piece == piece1);

	// do the same with piece 0 and 2 (in slot 1 and 2)
	iov = piece0;
	ret = write(s, set, iov, 1_piece, 0, aux::open_mode::write, ec);
	TEST_EQUAL(ret, int(iov.size()));
	if (ret != int(iov.size())) print_error("write", ret, ec);

	iov = piece2;
	ret = write(s, set, iov, 2_piece, 0, aux::open_mode::write, ec);
	TEST_EQUAL(ret, int(iov.size()));
	if (ret != int(iov.size())) print_error("write", ret, ec);

	// verify piece 0 and 2
	iov = piece;
	ret = read(s, set, iov, 1_piece, 0, aux::open_mode::write, ec);
	TEST_EQUAL(ret, int(iov.size()));
	if (ret != int(iov.size())) print_error("read", ret, ec);
	TEST_CHECK(piece == piece0);

	iov = piece;
	ret = read(s, set, iov, 2_piece, 0, aux::open_mode::write, ec);
	TEST_EQUAL(ret, int(iov.size()));
	if (ret != int(piece_size)) print_error("read", ret, ec);
	TEST_CHECK(piece == piece2);

	release_files(s, ec);
	}
}

template <typename StorageType>
void test_remove(std::string const& test_path)
{
	delete_dirs("temp_storage");

	file_storage fs;
	std::vector<char> buf;
	typename file_pool_type<StorageType>::type fp;
	io_context ios;

	aux::session_settings set;
	auto s = setup_torrent<StorageType>(fs, fp, buf, test_path, set);

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

	buf.resize(0x4000);
	span<char> b = {&buf[0], 0x4000};
	storage_error se;
	write(s, set, b, 2_piece, 0, aux::open_mode::write, se);

	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder1", "test2.tmp")))));
	TEST_CHECK(!exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", combine_path("subfolder", "test5.tmp"))))));
	file_status st;
	error_code ec;
	stat_file(combine_path(test_path, combine_path("temp_storage"
		, combine_path("folder1", "test2.tmp"))), &st, ec);

	// if the storage truncates the file to the full size, it's 8, otherwise it's
	// 4
	TEST_CHECK(st.file_size == 0x8000 || st.file_size == 0x4000);

	write(s, set, b, 0_piece, 0, aux::open_mode::write, se);

	TEST_CHECK(exists(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", combine_path("subfolder", "test5.tmp"))))));
	stat_file(combine_path(test_path, combine_path("temp_storage"
		, combine_path("_folder3", "test5.tmp"))), &st, ec);

	// if the storage truncates the file to the full size, it's 8, otherwise it's
	// 4
	TEST_CHECK(st.file_size == 0x8000 || st.file_size == 0x4000);

	s->delete_files(session::delete_files, se);
	if (se) print_error("delete_files", 0, se);

	if (se)
	{
		TEST_ERROR(se.ec.message().c_str());
		std::printf("mmap_storage::delete_files %s: %d\n"
			, se.ec.message().c_str(), static_cast<int>(se.file()));
	}

	TEST_CHECK(!exists(combine_path(test_path, "temp_storage")));
}

template <typename StorageType>
void test_rename(std::string const& test_path)
{
	delete_dirs("temp_storage");

	file_storage fs;
	std::vector<char> buf;
	typename file_pool_type<StorageType>::type fp;
	io_context ios;
	aux::session_settings set;

	auto s = setup_torrent<StorageType>(fs, fp, buf, test_path, set);

	// directories are not created up-front, unless they contain
	// an empty file
	std::string first_file = fs.file_path(0_file);
	for (auto const i : fs.file_range())
	{
		TEST_CHECK(!exists(combine_path(test_path, combine_path("temp_storage"
			, fs.file_path(i)))));
	}

	storage_error se;
	s->rename_file(0_file, "new_filename", se);
	if (se.ec)
	{
		std::printf("mmap_storage::rename_file failed: %s\n"
			, se.ec.message().c_str());
	}
	TEST_CHECK(!se.ec);

	TEST_EQUAL(s->files().file_path(0_file), "new_filename");
}

using lt::operator""_bit;
using check_files_flag_t = lt::flags::bitfield_flag<std::uint64_t, struct check_files_flag_type_tag>;

constexpr check_files_flag_t sparse = 0_bit;
constexpr check_files_flag_t test_oversized = 1_bit;
constexpr check_files_flag_t zero_prio = 2_bit;

void test_check_files(check_files_flag_t const flags
	, lt::disk_io_constructor_type const disk_constructor)
{
	std::string const test_path = current_working_directory();
	std::shared_ptr<torrent_info> info;

	error_code ec;
	constexpr int piece_size_check = 16 * 1024;
	delete_dirs("temp_storage");

	file_storage fs;
	fs.add_file("temp_storage/test1.tmp", piece_size_check);
	fs.add_file("temp_storage/test2.tmp", piece_size_check * 2);
	fs.add_file("temp_storage/test3.tmp", piece_size_check);

	std::vector<char> piece0 = new_piece(piece_size_check);
	std::vector<char> piece2 = new_piece(piece_size_check);

	lt::create_torrent t(fs, piece_size_check);
	t.set_hash(0_piece, hasher(piece0).final());
	t.set_hash(1_piece, sha1_hash::max());
	t.set_hash(2_piece, sha1_hash::max());
	t.set_hash(3_piece, hasher(piece2).final());

	create_directory(combine_path(test_path, "temp_storage"), ec);
	if (ec) std::cout << "create_directory: " << ec.message() << std::endl;

	if (flags & test_oversized)
		piece2.push_back(0x42);

	ofstream(combine_path(test_path, combine_path("temp_storage", "test1.tmp")).c_str())
		.write(piece0.data(), std::streamsize(piece0.size()));
	ofstream(combine_path(test_path, combine_path("temp_storage", "test3.tmp")).c_str())
		.write(piece2.data(), std::streamsize(piece2.size()));

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = std::make_shared<torrent_info>(buf, ec, from_span);

	aux::session_settings set;
	boost::asio::io_context ios;
	counters cnt;

	aux::session_settings sett;
	sett.set_int(settings_pack::aio_threads, 1);
	std::unique_ptr<disk_interface> io = disk_constructor(ios, sett, cnt);

	aux::vector<download_priority_t, file_index_t> priorities;

	if (flags & zero_prio)
		priorities.resize(std::size_t(info->num_files()), download_priority_t{});

	sha1_hash info_hash;
	storage_params p{
		fs,
		nullptr,
		test_path,
		(flags & sparse) ? storage_mode_sparse : storage_mode_allocate,
		priorities,
		info_hash
	};

	auto st = io->new_torrent(std::move(p), std::shared_ptr<void>());

	bool done = false;
	bool oversized = false;
	add_torrent_params frd;
	aux::vector<std::string, file_index_t> links;
	io->async_check_files(st, &frd, links
		, std::bind(&on_check_resume_data, _1, _2, &done, &oversized));
	io->submit_jobs();
	ios.restart();
	run_until(ios, done);

	TEST_EQUAL(oversized, bool(flags & test_oversized));

	for (auto const i : info->piece_range())
	{
		done = false;
		io->async_hash(st, i, {}
			, disk_interface::sequential_access | disk_interface::volatile_read | disk_interface::v1_hash
			, std::bind(&on_piece_checked, _1, _2, _3, &done));
		io->submit_jobs();
		ios.restart();
		run_until(ios, done);
	}

	io->abort(true);
}

// TODO: 2 split this test up into smaller parts
template <typename StorageType>
void run_test()
{
	std::string const test_path = current_working_directory();
	std::cout << "\n=== " << test_path << " ===\n" << std::endl;

	std::shared_ptr<torrent_info> info;

	std::vector<char> piece0 = new_piece(piece_size);
	std::vector<char> piece1 = new_piece(piece_size);
	std::vector<char> piece2 = new_piece(piece_size);
	std::vector<char> piece3 = new_piece(piece_size);

	delete_dirs("temp_storage");

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

	lt::create_torrent t(fs, piece_size, create_torrent::v1_only);
	TEST_CHECK(t.num_pieces() == 4);
	t.set_hash(0_piece, hasher(piece0).final());
	t.set_hash(1_piece, hasher(piece1).final());
	t.set_hash(2_piece, hasher(piece2).final());
	t.set_hash(3_piece, hasher(piece3).final());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	info = std::make_shared<torrent_info>(buf, from_span);

	// run_storage_tests writes piece 0, 1 and 2. not 3
	run_storage_tests<StorageType>(info, fs, storage_mode_sparse);

	// make sure the files have the correct size
	std::string const base = complete("temp_storage");

	// these files should have been allocated as 0 size
	TEST_CHECK(exists(combine_path(base, "test3.tmp")));
	TEST_CHECK(exists(combine_path(base, "test4.tmp")));

	delete_dirs("temp_storage");
}

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
TORRENT_TEST(check_files_sparse_mmap)
{
	test_check_files(sparse | zero_prio, lt::mmap_disk_io_constructor);
}

TORRENT_TEST(check_files_oversized_mmap_zero_prio)
{
	test_check_files(sparse | zero_prio | test_oversized, lt::mmap_disk_io_constructor);
}

TORRENT_TEST(check_files_oversized_mmap)
{
	test_check_files(sparse | test_oversized, lt::mmap_disk_io_constructor);
}


TORRENT_TEST(check_files_allocate_mmap)
{
	test_check_files(zero_prio, lt::mmap_disk_io_constructor);
}
#endif
TORRENT_TEST(check_files_sparse_posix)
{
	test_check_files(sparse | zero_prio, lt::posix_disk_io_constructor);
}

TORRENT_TEST(check_files_oversized_zero_prio_posix)
{
	test_check_files(sparse | zero_prio | test_oversized, lt::posix_disk_io_constructor);
}

TORRENT_TEST(check_files_oversized_posix)
{
	test_check_files(sparse | test_oversized, lt::posix_disk_io_constructor);
}


TORRENT_TEST(check_files_allocate_posix)
{
	test_check_files(zero_prio, lt::posix_disk_io_constructor);
}

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
TORRENT_TEST(rename_mmap_disk_io)
{
	test_rename<mmap_storage>(current_working_directory());
}

TORRENT_TEST(remove_mmap_disk_io)
{
	test_remove<mmap_storage>(current_working_directory());
}
#endif

TORRENT_TEST(rename_posix_disk_io)
{
	test_rename<posix_storage>(current_working_directory());
}

TORRENT_TEST(remove_posix_disk_io)
{
	test_remove<posix_storage>(current_working_directory());
}

void test_fastresume(bool const test_deprecated)
{
	std::string test_path = current_working_directory();
	error_code ec;
	std::cout << "\n\n=== test fastresume ===" << std::endl;
	delete_dirs("tmp1");

	create_directory(combine_path(test_path, "tmp1"), ec);
	if (ec) std::cout << "create_directory '" << combine_path(test_path, "tmp1")
		<< "': " << ec.message() << std::endl;
	ofstream file(combine_path(test_path, "tmp1/temporary").c_str());
	std::shared_ptr<torrent_info> t = ::create_torrent(&file);
	file.close();
	TEST_CHECK(exists(complete("tmp1/temporary")));
	if (!exists(complete("tmp1/temporary")))
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
		TEST_EQUAL(int(s.progress * 1000), 1000);
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
	delete_dirs("tmp1");
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

	// prevent race conditions of adding pieces while checking
	lt::torrent_status st = h.status();
	for (int i = 0; i < 40; ++i)
	{
		print_alerts(ses, "ses", true, true);
		st = h.status();
		if (st.state != torrent_status::checking_files
			&& st.state != torrent_status::checking_resume_data)
			break;
		std::this_thread::sleep_for(lt::milliseconds(100));
	}

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
	delete_dirs("tmp2");
	create_directory(combine_path(test_path, "tmp2"), ec);
	if (ec) std::cout << "create_directory: " << ec.message() << std::endl;
	ofstream file(combine_path(test_path, "tmp2/temporary").c_str());
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

		h.rename_file(0_file, "testing_renamed_files");
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

void fill_pattern(span<char> buf)
{
	int counter = 0;
	for (char& v : buf)
	{
		v = char(counter & 0xff);
		++counter;
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

} // anonymous namespace

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
TORRENT_TEST(mmap_disk_io) { run_test<mmap_storage>(); }
#endif
TORRENT_TEST(posix_disk_io) { run_test<posix_storage>(); }

namespace {

file_storage make_fs()
{
	file_storage fs;
	fs.add_file(combine_path("readwrite", "1"), 3);
	fs.add_file(combine_path("readwrite", "2"), 9);
	fs.add_file(combine_path("readwrite", "3"), 81);
	fs.add_file(combine_path("readwrite", "4"), 6561);
	fs.set_piece_length(0x1000);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	return fs;
}

struct test_fileop
{
	explicit test_fileop(int stripe_size) : m_stripe_size(stripe_size) {}

	int operator()(file_index_t const file_index, std::int64_t const file_offset
		, span<char> buf, storage_error&)
	{
		std::size_t offset = size_t(file_offset);
		if (file_index >= m_file_data.end_index())
		{
			m_file_data.resize(static_cast<int>(file_index) + 1);
		}

		std::size_t const write_size = std::size_t(std::min(m_stripe_size, int(buf.size())));
		buf = buf.first(int(write_size));

		std::vector<char>& file = m_file_data[file_index];

		if (offset + write_size > file.size())
			file.resize(offset + write_size);

		std::memcpy(&file[offset], buf.data(), write_size);
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
		, span<char> buf, storage_error&)
	{
		if (buf.size() > m_size)
			buf = buf.first(m_size);
		for (char& v : buf)
		{
			v = char(m_counter & 0xff);
			++m_counter;
		}
		m_size -= int(buf.size());
		return int(buf.size());
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
		, span<char> buf, storage_error& ec)
	{
		if (m_error_file == file_index)
		{
			ec.file(file_index);
			ec.ec.assign(boost::system::errc::permission_denied
				, boost::system::generic_category());
			ec.operation = operation_t::file_read;
			return 0;
		}
		return int(buf.size());
	}

	file_index_t m_error_file;
};

} // anonymous namespace

TORRENT_TEST(readwrite_stripe_1)
{
	file_storage fs = make_fs();
	test_fileop fop(1);
	storage_error ec;

	std::vector<char> buf(std::size_t(fs.total_size()));
	fill_pattern(buf);

	int ret = readwrite(fs, buf, 0_piece, 0, ec
		, std::ref(fop));

	TEST_EQUAL(ret, fs.total_size());
	TEST_EQUAL(fop.m_file_data.size(), 4);
	TEST_EQUAL(fop.m_file_data[0_file].size(), 3);
	TEST_EQUAL(fop.m_file_data[1_file].size(), 9);
	TEST_EQUAL(fop.m_file_data[2_file].size(), 81);
	TEST_EQUAL(fop.m_file_data[3_file].size(), 6561);

	TEST_CHECK(check_pattern(fop.m_file_data[0_file], 0));
	TEST_CHECK(check_pattern(fop.m_file_data[1_file], 3));
	TEST_CHECK(check_pattern(fop.m_file_data[2_file], 3 + 9));
	TEST_CHECK(check_pattern(fop.m_file_data[3_file], 3 + 9 + 81));
}

TORRENT_TEST(readwrite_single_buffer)
{
	file_storage fs = make_fs();
	test_fileop fop(10000000);
	storage_error ec;

	std::vector<char> buf(size_t(fs.total_size()));
	fill_pattern(buf);

	int ret = readwrite(fs, buf, 0_piece, 0, ec, std::ref(fop));

	TEST_EQUAL(ret, fs.total_size());
	TEST_EQUAL(fop.m_file_data.size(), 4);
	TEST_EQUAL(fop.m_file_data[0_file].size(), 3);
	TEST_EQUAL(fop.m_file_data[1_file].size(), 9);
	TEST_EQUAL(fop.m_file_data[2_file].size(), 81);
	TEST_EQUAL(fop.m_file_data[3_file].size(), 6561);

	TEST_CHECK(check_pattern(fop.m_file_data[0_file], 0));
	TEST_CHECK(check_pattern(fop.m_file_data[1_file], 3));
	TEST_CHECK(check_pattern(fop.m_file_data[2_file], 3 + 9));
	TEST_CHECK(check_pattern(fop.m_file_data[3_file], 3 + 9 + 81));
}

TORRENT_TEST(readwrite_read)
{
	file_storage fs = make_fs();
	test_read_fileop fop(10000000);
	storage_error ec;

	std::vector<char> buf(size_t(fs.total_size()));

	// read everything
	int ret = readwrite(fs, buf, 0_piece, 0, ec, std::ref(fop));

	TEST_EQUAL(ret, fs.total_size());
	TEST_CHECK(check_pattern(buf, 0));
}

TORRENT_TEST(readwrite_read_short)
{
	file_storage fs = make_fs();
	test_read_fileop fop(100);
	storage_error ec;

	std::vector<char> buf(size_t(fs.total_size()));

	// read everything
	int ret = readwrite(fs, buf, 0_piece, 0, ec, std::ref(fop));

	TEST_EQUAL(static_cast<int>(ec.file()), 3);

	TEST_EQUAL(ret, 100);
	buf.resize(100);
	TEST_CHECK(check_pattern(buf, 0));
}

TORRENT_TEST(readwrite_error)
{
	file_storage fs = make_fs();
	test_error_fileop fop(2_file);
	storage_error ec;

	std::vector<char> buf(size_t(fs.total_size()));

	// read everything
	int ret = readwrite(fs, buf, 0_piece, 0, ec, std::ref(fop));

	TEST_EQUAL(ret, 12);
	TEST_EQUAL(static_cast<int>(ec.file()), 2);
	TEST_CHECK(ec.operation == operation_t::file_read);
	TEST_EQUAL(ec.ec, boost::system::errc::permission_denied);
	std::printf("error: %s\n", ec.ec.message().c_str());
}

TORRENT_TEST(readwrite_zero_size_files)
{
	file_storage fs;
	fs.add_file(combine_path("readwrite", "1"), 3);
	fs.add_file(combine_path("readwrite", "2"), 0);
	fs.add_file(combine_path("readwrite", "3"), 81);
	fs.add_file(combine_path("readwrite", "4"), 0);
	fs.add_file(combine_path("readwrite", "5"), 6561);
	fs.set_piece_length(0x1000);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	test_read_fileop fop(10000000);
	storage_error ec;

	std::vector<char> buf(size_t(fs.total_size()));

	// read everything
	int ret = readwrite(fs, buf, 0_piece, 0, ec, std::ref(fop));

	TEST_EQUAL(ret, fs.total_size());
	TEST_CHECK(check_pattern(buf, 0));
}

template <typename StorageType>
void test_move_storage_to_self()
{
	// call move_storage with the path to the exising storage. should be a no-op
	std::string const save_path = current_working_directory();
	std::string const test_path = complete("temp_storage");
	delete_dirs(test_path);

	aux::session_settings set;
	file_storage fs;
	std::vector<char> buf;
	typename file_pool_type<StorageType>::type fp;
	io_context ios;
	auto s = setup_torrent<StorageType>(fs, fp, buf, save_path, set);

	span<char> const b = {&buf[0], 4};
	storage_error se;
	TEST_EQUAL(se.ec, boost::system::errc::success);
	write(s, set, b, 1_piece, 0, aux::open_mode::write, se);

	TEST_CHECK(exists(combine_path(test_path, combine_path("folder2", "test3.tmp"))));
	TEST_CHECK(exists(combine_path(test_path, combine_path("_folder3", "test4.tmp"))));
	TEST_EQUAL(se.ec, boost::system::errc::success);

	s->move_storage(save_path, move_flags_t::always_replace_files, se);
	TEST_EQUAL(se.ec, boost::system::errc::success);
	std::cerr << "file: " << se.file() << '\n';
	std::cerr << "op: " << int(se.operation) << '\n';
	std::cerr << "ec: " << se.ec.message() << '\n';

	TEST_CHECK(exists(test_path));

	TEST_CHECK(exists(combine_path(test_path, combine_path("folder2", "test3.tmp"))));
	TEST_CHECK(exists(combine_path(test_path, combine_path("_folder3", "test4.tmp"))));
}

template <typename StorageType>
void test_move_storage_into_self()
{
	std::string const save_path = current_working_directory();
	delete_dirs("temp_storage");

	aux::session_settings set;
	file_storage fs;
	std::vector<char> buf;
	typename file_pool_type<StorageType>::type fp;
	io_context ios;
	auto s = setup_torrent<StorageType>(fs, fp, buf, save_path, set);

	span<char> const b = {&buf[0], 4};
	storage_error se;
	write(s, set, b, 2_piece, 0, aux::open_mode::write, se);

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

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
TORRENT_TEST(move_default_storage_to_self)
{
	test_move_storage_to_self<mmap_storage>();
}

TORRENT_TEST(move_default_storage_into_self)
{
	test_move_storage_into_self<mmap_storage>();
}

#endif

TORRENT_TEST(move_posix_storage_to_self)
{
	test_move_storage_to_self<posix_storage>();
}

TORRENT_TEST(move_posix_storage_into_self)
{
	test_move_storage_into_self<posix_storage>();
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

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
TORRENT_TEST(dont_move_intermingled_files)
{
	std::string const save_path = complete("save_path_1");
	delete_dirs(combine_path(save_path, "temp_storage"));

	std::string const test_path = complete("save_path_2");
	delete_dirs(combine_path(test_path, "temp_storage"));

	aux::session_settings set;
	file_storage fs;
	std::vector<char> buf;
	typename file_pool_type<mmap_storage>::type fp;
	io_context ios;
	auto s = setup_torrent<mmap_storage>(fs, fp, buf, save_path, set);

	span<char> b = {&buf[0], 4};
	storage_error se;
	s->write(set, b, 2_piece, 0, aux::open_mode::write, disk_job_flags_t{}, se);

	error_code ec;
	create_directory(combine_path(save_path, combine_path("temp_storage"
		, combine_path("_folder3", "alien_folder1"))), ec);
	TEST_EQUAL(ec, boost::system::errc::success);

	ofstream(combine_path(save_path, combine_path("temp_storage", "alien1.tmp")).c_str());
	ofstream(combine_path(save_path, combine_path("temp_storage", combine_path("folder1", "alien2.tmp"))).c_str());

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
#endif

namespace {

void sync(lt::io_context& ioc, int& outstanding)
{
	while (outstanding > 0)
	{
		ioc.run_one();
		ioc.restart();
	}
}

template <typename Fun>
void test_unaligned_read(lt::disk_io_constructor_type constructor, Fun fun)
{
	lt::io_context ioc;
	lt::counters cnt;
	lt::settings_pack pack;
	pack.set_int(lt::settings_pack::aio_threads, 1);
	pack.set_int(lt::settings_pack::file_pool_size, 2);

	std::unique_ptr<lt::disk_interface> disk_io
		= constructor(ioc, pack, cnt);

	lt::file_storage fs;
	fs.add_file("test", lt::default_block_size * 2);
	fs.set_num_pieces(1);
	fs.set_piece_length(lt::default_block_size * 2);

	std::string const save_path = complete("save_path");
	delete_dirs(combine_path(save_path, "test"));

	lt::aux::vector<lt::download_priority_t, lt::file_index_t> prios;
	lt::storage_params params(fs, nullptr
		, save_path
		, lt::storage_mode_sparse
		, prios
		, lt::sha1_hash("01234567890123456789"));

	lt::storage_holder t = disk_io->new_torrent(params, {});

	int outstanding = 0;
	lt::add_torrent_params atp;
	disk_io->async_check_files(t, &atp, lt::aux::vector<std::string, lt::file_index_t>{}
		, [&](lt::status_t, lt::storage_error const&) { --outstanding; });
	++outstanding;
	disk_io->submit_jobs();
	sync(ioc, outstanding);

	fun(disk_io.get(), t, ioc, outstanding);

	t.reset();
	disk_io->abort(true);
}

struct write_handler
{
	write_handler(int& outstanding) : m_out(&outstanding) {}
	void operator()(lt::storage_error const& ec) const
	{
		--(*m_out);
		if (ec) std::cout << "async_write failed " << ec.ec.message() << '\n';
		TEST_CHECK(!ec);
	}
	int* m_out;
};

struct read_handler
{
	read_handler(int& outstanding, lt::span<char const> expected) : m_out(&outstanding), m_exp(expected) {}
	void operator()(lt::disk_buffer_holder h, lt::storage_error const& ec) const
	{
		--(*m_out);
		if (ec) std::cout << "async_read failed " << ec.ec.message() << '\n';
		TEST_CHECK(!ec);
		TEST_CHECK(m_exp == lt::span<char const>(h.data(), h.size()));
	}
	int* m_out;
	lt::span<char const> m_exp;
};

void both_sides_from_store_buffer(lt::disk_interface* disk_io, lt::storage_holder const& t, lt::io_context& ioc, int& outstanding)
{
	std::vector<char> write_buffer(lt::default_block_size * 2);
	aux::random_bytes(write_buffer);

	lt::peer_request const req0{0_piece, 0, lt::default_block_size};
	lt::peer_request const req1{0_piece, lt::default_block_size, lt::default_block_size};

	// this is the unaligned read request
	lt::peer_request const req2{0_piece, lt::default_block_size / 2, lt::default_block_size};

	std::vector<char> const expected_buffer(write_buffer.begin() + req2.start
		, write_buffer.begin() + req2.start + req2.length);

	++outstanding;
	disk_io->async_write(t, req0, write_buffer.data(), {}, write_handler(outstanding));
	++outstanding;
	disk_io->async_write(t, req1, write_buffer.data() + lt::default_block_size, {}, write_handler(outstanding));
	++outstanding;
	disk_io->async_read(t, req2, read_handler(outstanding, expected_buffer));
	disk_io->submit_jobs();
	sync(ioc, outstanding);
}

void first_side_from_store_buffer(lt::disk_interface* disk_io, lt::storage_holder const& t, lt::io_context& ioc, int& outstanding)
{
	std::vector<char> write_buffer(lt::default_block_size * 2);
	aux::random_bytes(write_buffer);

	lt::peer_request const req0{0_piece, 0, lt::default_block_size};
	lt::peer_request const req1{0_piece, lt::default_block_size, lt::default_block_size};

	// this is the unaligned read request
	lt::peer_request const req2{0_piece, lt::default_block_size / 2, lt::default_block_size};

	std::vector<char> const expected_buffer(write_buffer.begin() + req2.start
		, write_buffer.begin() + req2.start + req2.length);

	++outstanding;
	disk_io->async_write(t, req0, write_buffer.data(), {}, write_handler(outstanding));

	disk_io->submit_jobs();
	sync(ioc, outstanding);

	++outstanding;
	disk_io->async_write(t, req1, write_buffer.data() + lt::default_block_size, {}, write_handler(outstanding));
	++outstanding;
	disk_io->async_read(t, req2, read_handler(outstanding, expected_buffer));
	disk_io->submit_jobs();
	sync(ioc, outstanding);
}

void second_side_from_store_buffer(lt::disk_interface* disk_io, lt::storage_holder const& t, lt::io_context& ioc, int& outstanding)
{
	std::vector<char> write_buffer(lt::default_block_size * 2);
	aux::random_bytes(write_buffer);

	lt::peer_request const req0{0_piece, 0, lt::default_block_size};
	lt::peer_request const req1{0_piece, lt::default_block_size, lt::default_block_size};

	// this is the unaligned read request
	lt::peer_request const req2{0_piece, lt::default_block_size / 2, lt::default_block_size};

	std::vector<char> const expected_buffer(write_buffer.begin() + req2.start
		, write_buffer.begin() + req2.start + req2.length);

	++outstanding;
	disk_io->async_write(t, req1, write_buffer.data() + lt::default_block_size, {}, write_handler(outstanding));
	disk_io->submit_jobs();
	sync(ioc, outstanding);

	++outstanding;
	disk_io->async_write(t, req0, write_buffer.data(), {}, write_handler(outstanding));
	++outstanding;
	disk_io->async_read(t, req2, read_handler(outstanding, expected_buffer));
	disk_io->submit_jobs();
	sync(ioc, outstanding);
}

void none_from_store_buffer(lt::disk_interface* disk_io, lt::storage_holder const& t, lt::io_context& ioc, int& outstanding)
{
	std::vector<char> write_buffer(lt::default_block_size * 2);
	aux::random_bytes(write_buffer);

	lt::peer_request const req0{0_piece, 0, lt::default_block_size};
	lt::peer_request const req1{0_piece, lt::default_block_size, lt::default_block_size};

	// this is the unaligned read request
	lt::peer_request const req2{0_piece, lt::default_block_size / 2, lt::default_block_size};

	std::vector<char> const expected_buffer(write_buffer.begin() + req2.start
		, write_buffer.begin() + req2.start + req2.length);

	++outstanding;
	disk_io->async_write(t, req0, write_buffer.data(), {}, write_handler(outstanding));
	++outstanding;
	disk_io->async_write(t, req1, write_buffer.data() + lt::default_block_size, {}, write_handler(outstanding));
	disk_io->submit_jobs();
	sync(ioc, outstanding);

	++outstanding;
	disk_io->async_read(t, req2, read_handler(outstanding, expected_buffer));
	disk_io->submit_jobs();
	sync(ioc, outstanding);
}

}

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
TORRENT_TEST(mmap_unaligned_read_both_store_buffer)
{
	test_unaligned_read(lt::mmap_disk_io_constructor, both_sides_from_store_buffer);
	test_unaligned_read(lt::mmap_disk_io_constructor, first_side_from_store_buffer);
	test_unaligned_read(lt::mmap_disk_io_constructor, second_side_from_store_buffer);
	test_unaligned_read(lt::mmap_disk_io_constructor, none_from_store_buffer);
}
#endif

TORRENT_TEST(posix_unaligned_read_both_store_buffer)
{
	test_unaligned_read(lt::posix_disk_io_constructor, both_sides_from_store_buffer);
	test_unaligned_read(lt::posix_disk_io_constructor, first_side_from_store_buffer);
	test_unaligned_read(lt::posix_disk_io_constructor, second_side_from_store_buffer);
	test_unaligned_read(lt::posix_disk_io_constructor, none_from_store_buffer);
}
