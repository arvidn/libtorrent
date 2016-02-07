/*

Copyright (c) 2012, Arvid Norberg
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

#include "libtorrent/stat_cache.hpp"
#include "libtorrent/error_code.hpp"
#include "test.hpp"

using namespace libtorrent;

TORRENT_TEST(stat_cache)
{
	error_code ec;

	stat_cache sc;

	file_storage fs;
	for (int i = 0; i < 20; ++i)
	{
		char buf[50];
		snprintf(buf, sizeof(buf), "test_torrent/test-%d", i);
		fs.add_file(buf, (i + 1) * 10);
	}

	std::string save_path = ".";

	sc.reserve(10);

	sc.set_error(3, error_code(boost::system::errc::permission_denied, generic_category()));
	ec.clear();
	TEST_EQUAL(sc.get_filesize(3, fs, save_path, ec), stat_cache::file_error);
	TEST_EQUAL(ec, error_code(boost::system::errc::permission_denied, generic_category()));

	sc.set_error(3, error_code(boost::system::errc::no_such_file_or_directory, generic_category()));
	ec.clear();
	TEST_EQUAL(sc.get_filesize(3, fs, save_path, ec), stat_cache::file_error);
	TEST_EQUAL(ec, error_code(boost::system::errc::no_such_file_or_directory, generic_category()));

	ec.clear();
	sc.set_cache(3, 101);
	TEST_EQUAL(sc.get_filesize(3, fs, save_path, ec), 101);
	TEST_CHECK(!ec);

	sc.set_error(11, error_code(boost::system::errc::broken_pipe, generic_category()));
	ec.clear();
	TEST_EQUAL(sc.get_filesize(11, fs, save_path, ec), stat_cache::file_error);
	TEST_EQUAL(ec, error_code(boost::system::errc::broken_pipe, generic_category()));

	ec.clear();
	sc.set_error(13, error_code(boost::system::errc::no_such_file_or_directory, generic_category()));
	TEST_EQUAL(sc.get_filesize(13, fs, save_path, ec), stat_cache::file_error);
	TEST_EQUAL(ec, error_code(boost::system::errc::no_such_file_or_directory, generic_category()));

	ec.clear();
	sc.set_cache(15, 1000);
	TEST_CHECK(sc.get_filesize(15, fs, save_path, ec) == 1000);
	TEST_CHECK(!ec);
}

