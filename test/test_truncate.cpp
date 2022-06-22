/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <fstream>
#include <iostream>

#include "test.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/truncate.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/error_code.hpp"

namespace {

void create_file(std::string const& name, int size)
{
	lt::error_code ec;
	lt::create_directories(lt::parent_path(name), ec);
	TEST_CHECK(!ec);
	std::ofstream f(name.c_str());
	std::vector<char> buf(static_cast<std::size_t>(size));
	f.write(buf.data(), std::streamsize(buf.size()));
}

std::int64_t file_size(std::string const& name)
{
	lt::file_status st;
	lt::error_code ec;
	lt::stat_file(name, &st, ec);
	std::cerr << name << ": " << ec.message() << '\n';
	TEST_CHECK(!ec);
	return st.file_size;
}
}

TORRENT_TEST(truncate_small_files)
{
	using lt::combine_path;

	lt::file_storage fs;
	fs.add_file(combine_path("test", "a"), 100);
	fs.add_file(combine_path("test", "b"), 900);
	fs.add_file(combine_path("test", "c"), 10);

	create_file(combine_path("test", "a"), 99);
	create_file(combine_path("test", "b"), 899);
	create_file(combine_path("test", "c"), 9);

	lt::storage_error err;
	lt::truncate_files(fs, ".", err);
	TEST_CHECK(!err.ec);

	TEST_EQUAL(file_size(combine_path("test", "a")), 99);
	TEST_EQUAL(file_size(combine_path("test", "b")), 899);
	TEST_EQUAL(file_size(combine_path("test", "c")), 9);
}

TORRENT_TEST(truncate_large_files)
{
	using lt::combine_path;

	lt::file_storage fs;
	fs.add_file(combine_path("test", "a"), 100);
	fs.add_file(combine_path("test", "b"), 900);
	fs.add_file(combine_path("test", "c"), 10);

	create_file(combine_path("test", "a"), 101);
	create_file(combine_path("test", "b"), 901);
	create_file(combine_path("test", "c"), 11);

	lt::storage_error err;
	lt::truncate_files(fs, ".", err);
	TEST_CHECK(!err.ec);

	TEST_EQUAL(file_size(combine_path("test", "a")), 100);
	TEST_EQUAL(file_size(combine_path("test", "b")), 900);
	TEST_EQUAL(file_size(combine_path("test", "c")), 10);
}
