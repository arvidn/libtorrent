/*

Copyright (c) 2008, Arvid Norberg
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

#include "libtorrent/hasher.hpp"
#include <boost/lexical_cast.hpp>

#include "test.hpp"

using namespace libtorrent;

// test vectors from RFC 3174
// http://www.faqs.org/rfcs/rfc3174.html

char const* test_array[4] =
{
	"abc",
	"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
	"a",
	"0123456701234567012345670123456701234567012345670123456701234567"
};

long int repeat_count[4] = { 1, 1, 1000000, 10 };

char const* result_array[4] =
{
	"A9993E364706816ABA3E25717850C26C9CD0D89D",
	"84983E441C3BD26EBAAE4AA1F95129E5E54670F1",
	"34AA973CD4C4DAA4F61EEB2BDBAD27316534016F",
	"DEA356A2CDDD90C7A7ECEDC5EBB563934F460452"
};


int test_main()
{
	using namespace libtorrent;

	for (int test = 0; test < 4; ++test)
	{
		hasher h;
		for (int i = 0; i < repeat_count[test]; ++i)
			h.update(test_array[test], std::strlen(test_array[test]));

		sha1_hash result;
		from_hex(result_array[test], 40, (char*)&result[0]);
		TEST_CHECK(result == h.final());
	}

	return 0;
}

