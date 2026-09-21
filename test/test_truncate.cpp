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
	fs.add_file_borrow({}, combine_path("test", "a"), 100);
	fs.add_file_borrow({}, combine_path("test", "b"), 900);
	fs.add_file_borrow({}, combine_path("test", "c"), 10);

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
	fs.add_file_borrow({}, combine_path("test", "a"), 100);
	fs.add_file_borrow({}, combine_path("test", "b"), 900);
	fs.add_file_borrow({}, combine_path("test", "c"), 10);

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

// the filenames overload must truncate files at their renamed on-disk
// location, not the original name recorded in the file_storage
TORRENT_TEST(truncate_small_files_renamed)
{
	using lt::combine_path;

	lt::file_storage fs;
	fs.add_file_borrow({}, combine_path("test", "a"), 100);
	fs.add_file_borrow({}, combine_path("test", "b"), 900);
	fs.add_file_borrow({}, combine_path("test", "c"), 10);

	lt::renamed_files rf;
	rf.rename_file(fs, lt::file_index_t(0), combine_path("test", "a-renamed"));

	// sentinel file at the original (pre-rename) path, oversized so it
	// would get truncated to 100 if truncate_files() ignored the rename
	// and operated on this path instead of "test/a-renamed"
	create_file(combine_path("test", "a"), 101);

	create_file(combine_path("test", "a-renamed"), 99);
	create_file(combine_path("test", "b"), 899);
	create_file(combine_path("test", "c"), 9);

	lt::filenames const fn(fs, rf);

	lt::storage_error err;
	lt::truncate_files(fn, ".", err);
	TEST_CHECK(!err.ec);

	TEST_EQUAL(file_size(combine_path("test", "a")), 101);
	TEST_EQUAL(file_size(combine_path("test", "a-renamed")), 99);
	TEST_EQUAL(file_size(combine_path("test", "b")), 899);
	TEST_EQUAL(file_size(combine_path("test", "c")), 9);
}

TORRENT_TEST(truncate_large_files_renamed)
{
	using lt::combine_path;

	lt::file_storage fs;
	fs.add_file_borrow({}, combine_path("test", "a"), 100);
	fs.add_file_borrow({}, combine_path("test", "b"), 900);
	fs.add_file_borrow({}, combine_path("test", "c"), 10);

	lt::renamed_files rf;
	rf.rename_file(fs, lt::file_index_t(0), combine_path("test", "a-renamed"));

	// sentinel file at the original (pre-rename) path, distinct in size
	// from both the declared file size and the renamed file below, so it
	// reveals whether truncate_files() mistakenly touched this path
	// instead of "test/a-renamed"
	create_file(combine_path("test", "a"), 150);

	create_file(combine_path("test", "a-renamed"), 101);
	create_file(combine_path("test", "b"), 901);
	create_file(combine_path("test", "c"), 11);

	lt::filenames const fn(fs, rf);

	lt::storage_error err;
	lt::truncate_files(fn, ".", err);
	TEST_CHECK(!err.ec);

	TEST_EQUAL(file_size(combine_path("test", "a")), 150);
	TEST_EQUAL(file_size(combine_path("test", "a-renamed")), 100);
	TEST_EQUAL(file_size(combine_path("test", "b")), 900);
	TEST_EQUAL(file_size(combine_path("test", "c")), 10);
}
