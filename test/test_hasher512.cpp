/*

Copyright (c) 2016, Alden Torres
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

#ifndef TORRENT_DISABLE_DHT

#include "libtorrent/hasher512.hpp"
#include "libtorrent/hex.hpp"

#include "test.hpp"

using namespace lt;

namespace
{
	void test_vector(std::string s, std::string output, int const n = 1)
	{
		hasher512 h;
		for (int i = 0; i < n; i++)
			h.update(s);
		std::string digest = h.final().to_string();
		TEST_EQUAL(aux::to_hex(digest), output);
	}
}

// http://www.di-mgt.com.au/sha_testvectors.html
TORRENT_TEST(hasher512_test_vec1)
{
	test_vector(
		"abc"
		, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
		  "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"
	);

	test_vector(
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
		, "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c335"
		  "96fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445"
	);

	test_vector(
		"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhi"
		"jklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"
		, "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
		  "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909"
	);

	test_vector(
		"a"
		, "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
		  "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b"
		, 1000000
	);

	test_vector(
		"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
		, "b47c933421ea2db149ad6e10fce6c7f93d0752380180ffd7f4629a712134831d"
		  "77be6091b819ed352c2967a2e2d4fa5050723c9630691f1a05a7281dbe6c1086"
		, 16777216
	);
}

#endif // TORRENT_DISABLE_DHT
