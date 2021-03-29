/*

Copyright (c) 2015, 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/timestamp_history.hpp"

TORRENT_TEST(timestamp_history)
{
	using namespace lt;

	aux::timestamp_history h;
	TEST_EQUAL(h.add_sample(0x32, false), 0);
	TEST_EQUAL(h.base(), 0x32);
	TEST_EQUAL(h.add_sample(0x33, false), 0x1);
	TEST_EQUAL(h.base(), 0x32);
	TEST_EQUAL(h.add_sample(0x3433, false), 0x3401);
	TEST_EQUAL(h.base(), 0x32);
	TEST_EQUAL(h.add_sample(0x30, false), 0);
	TEST_EQUAL(h.base(), 0x30);

	// test that wrapping of the timestamp is properly handled
	h.add_sample(0xfffffff3, false);
	TEST_EQUAL(h.base(), 0xfffffff3);

	// TODO: test the case where we have > 120 samples (and have the base delay actually be updated)
	// TODO: test the case where a sample is lower than the history entry but not lower than the base
}

