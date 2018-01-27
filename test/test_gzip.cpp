/*

Copyright (c) 2014, Arvid Norberg
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
#include "libtorrent/gzip.hpp"
#include "setup_transfer.hpp" // for load_file
#include "libtorrent/aux_/path.hpp" // for combine_path

using namespace lt;

TORRENT_TEST(zeroes)
{
	std::vector<char> zipped;
	error_code ec;
	load_file(combine_path("..", "zeroes.gz"), zipped, ec, 1000000);
	if (ec) std::printf("failed to open file: (%d) %s\n", ec.value()
		, ec.message().c_str());
	TEST_CHECK(!ec);

	std::vector<char> inflated;
	inflate_gzip(zipped, inflated, 1000000, ec);

	if (ec) {
		std::printf("failed to unzip: %s\n", ec.message().c_str());
	}
	TEST_CHECK(!ec);
	TEST_CHECK(!inflated.empty());
	for (std::size_t i = 0; i < inflated.size(); ++i)
		TEST_EQUAL(inflated[i], 0);
}

TORRENT_TEST(corrupt)
{
	std::vector<char> zipped;
	error_code ec;
	load_file(combine_path("..", "corrupt.gz"), zipped, ec, 1000000);
	if (ec) std::printf("failed to open file: (%d) %s\n", ec.value()
		, ec.message().c_str());
	TEST_CHECK(!ec);

	std::vector<char> inflated;
	inflate_gzip(zipped, inflated, 1000000, ec);

	// we expect this to fail
	TEST_CHECK(ec);
}

TORRENT_TEST(invalid1)
{
	std::vector<char> zipped;
	error_code ec;
	load_file(combine_path("..", "invalid1.gz"), zipped, ec, 1000000);
	if (ec) std::printf("failed to open file: (%d) %s\n", ec.value()
		, ec.message().c_str());
	TEST_CHECK(!ec);

	std::vector<char> inflated;
	inflate_gzip(zipped, inflated, 1000000, ec);

	// we expect this to fail
	TEST_CHECK(ec);
}

TORRENT_TEST(empty)
{
	std::vector<char> empty;
	std::vector<char> inflated;
	error_code ec;
	inflate_gzip(empty, inflated, 1000000, ec);
	TEST_CHECK(ec);
}
