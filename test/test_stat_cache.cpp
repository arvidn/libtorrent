/*

Copyright (c) 2010, 2014-2017, 2020-2022, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/stat_cache.hpp"
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
		fs.add_file(buf, (i + 1) * 10);
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
