/*

Copyright (c) 2022, Arvid Norberg
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
