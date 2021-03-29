/*

Copyright (c) 2014, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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
