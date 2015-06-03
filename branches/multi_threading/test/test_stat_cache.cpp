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

	sc.init(10);

	for (int i = 0; i < 10; ++i)
	{
		TEST_CHECK(sc.get_filesize(i) == stat_cache::not_in_cache);
		TEST_CHECK(sc.get_filetime(i) == stat_cache::not_in_cache);
	}

	// out of bound accesses count as not-in-cache
	TEST_CHECK(sc.get_filesize(10) == stat_cache::not_in_cache);
	TEST_CHECK(sc.get_filesize(11) == stat_cache::not_in_cache);

	sc.set_error(3);
	TEST_CHECK(sc.get_filesize(3) == stat_cache::cache_error);

	sc.set_noexist(3);
	TEST_CHECK(sc.get_filesize(3) == stat_cache::no_exist);

	sc.set_cache(3, 101, 5555);
	TEST_CHECK(sc.get_filesize(3) == 101);
	TEST_CHECK(sc.get_filetime(3) == 5555);

	sc.set_error(11);
	TEST_CHECK(sc.get_filesize(10) == stat_cache::not_in_cache);
	TEST_CHECK(sc.get_filesize(11) == stat_cache::cache_error);

	sc.set_noexist(13);
	TEST_CHECK(sc.get_filesize(12) == stat_cache::not_in_cache);
	TEST_CHECK(sc.get_filesize(13) == stat_cache::no_exist);

	sc.set_cache(15, 1000, 3000);
	TEST_CHECK(sc.get_filesize(14) == stat_cache::not_in_cache);
	TEST_CHECK(sc.get_filesize(15) == 1000);
	TEST_CHECK(sc.get_filetime(15) == 3000);
}

