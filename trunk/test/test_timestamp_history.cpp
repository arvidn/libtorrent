/*

Copyright (c) 2008-2015, Arvid Norberg
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
#include "libtorrent/timestamp_history.hpp"

TORRENT_TEST(timestamp_history)
{
	using namespace libtorrent;

	timestamp_history h;
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

