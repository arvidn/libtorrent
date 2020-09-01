/*

Copyright (c) 2017, 2020, Arvid Norberg
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
#include "libtorrent/aux_/alloca.hpp"

using namespace lt;

namespace {

struct A
{
	int val = 1337;
};

int destructed = 0;

struct B
{
	~B() { ++destructed; }
};

}

TORRENT_TEST(alloca_construct)
{
	TORRENT_ALLOCA(vec, A, 13);

	TEST_EQUAL(vec.size(), 13);
	for (auto const& o : vec)
	{
		TEST_EQUAL(o.val, 1337);
	}
}

TORRENT_TEST(alloca_destruct)
{
	{
		destructed = 0;
		TORRENT_ALLOCA(vec, B, 3);
	}
	TEST_EQUAL(destructed, 3);
}

TORRENT_TEST(alloca_large)
{
	// this is something like 256 kiB of allocation
	// it should be made on the heap and always succeed
	TORRENT_ALLOCA(vec, A, 65536);
	for (auto const& a : vec)
		TEST_EQUAL(a.val, 1337);
}

