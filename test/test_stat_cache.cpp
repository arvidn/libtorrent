/*

Copyright (c) 2010, 2014-2017, 2020-2022, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/stat_cache.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/error_code.hpp"
#include "test.hpp"
#include "test_utils.hpp"

using namespace lt;

TORRENT_TEST(stat_cache)
{
	error_code ec;

	aux::stat_cache sc;

	file_storage fs;
	for (int i = 0; i < 20; ++i)
	{
		char buf[50];
		std::snprintf(buf, sizeof(buf), "test_torrent/test-%d", i);
		fs.add_file_borrow({}, buf, (i + 1) * 10);
	}
	renamed_files rf;
	filenames fn(fs, rf);

	std::string save_path = ".";

	sc.reserve(10);

	sc.set_error(3_file, error_code(boost::system::errc::permission_denied, generic_category()));
	ec.clear();
	TEST_EQUAL(sc.get_filesize(3_file, fn, save_path, ec), aux::stat_cache::file_error);
	TEST_EQUAL(ec, error_code(boost::system::errc::permission_denied, generic_category()));

	sc.set_error(3_file, error_code(boost::system::errc::no_such_file_or_directory, generic_category()));
	ec.clear();
	TEST_EQUAL(sc.get_filesize(3_file, fn, save_path, ec), aux::stat_cache::file_error);
	TEST_EQUAL(ec, error_code(boost::system::errc::no_such_file_or_directory, generic_category()));

	ec.clear();
	sc.set_cache(3_file, 101);
	TEST_EQUAL(sc.get_filesize(3_file, fn, save_path, ec), 101);
	TEST_CHECK(!ec);

	sc.set_error(11_file, error_code(boost::system::errc::broken_pipe, generic_category()));
	ec.clear();
	TEST_EQUAL(sc.get_filesize(11_file, fn, save_path, ec), aux::stat_cache::file_error);
	TEST_EQUAL(ec, error_code(boost::system::errc::broken_pipe, generic_category()));

	ec.clear();
	sc.set_error(13_file, error_code(boost::system::errc::no_such_file_or_directory, generic_category()));
	TEST_EQUAL(sc.get_filesize(13_file, fn, save_path, ec), aux::stat_cache::file_error);
	TEST_EQUAL(ec, error_code(boost::system::errc::no_such_file_or_directory, generic_category()));

	ec.clear();
	sc.set_cache(15_file, 1000);
	TEST_CHECK(sc.get_filesize(15_file, fn, save_path, ec) == 1000);
	TEST_CHECK(!ec);
}

TORRENT_TEST(stat_cache_directory)
{
	file_storage fs;
	fs.add_file_borrow({}, "stat_cache_test_dir/test.txt", 1000);
	renamed_files rf;
	filenames fn(fs, rf);

	std::string const save_path = absolute(".");
	std::string const file_path = fn.file_path(0_file, save_path);

	error_code ec;
	create_directories(file_path, ec);
	TEST_CHECK(!ec);

	aux::stat_cache sc;
	// a directory in place of the expected file must be reported as an
	// error, never a valid (and misleading) size
	TEST_EQUAL(sc.get_filesize(0_file, fn, save_path, ec), aux::stat_cache::file_error);
	TEST_EQUAL(ec, error_code(boost::system::errc::is_a_directory, generic_category()));

	remove(file_path, ec);
	remove(parent_path(file_path), ec);
}
