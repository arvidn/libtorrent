/*

Copyright (c) 2007, Un Shyam
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

#include <algorithm>
#include <iostream>

#include "libtorrent/hasher.hpp"
#include "libtorrent/pe_crypto.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/random.hpp"

#include "setup_transfer.hpp"
#include "test.hpp"

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)

void test_enc_handler(libtorrent::crypto_plugin* a, libtorrent::crypto_plugin* b)
{
#ifdef TORRENT_USE_VALGRIND
	const int repcount = 10;
#else
	const int repcount = 128;
#endif
	for (int rep = 0; rep < repcount; ++rep)
	{
		int buf_len = rand() % (512 * 1024);
		char* buf = new char[buf_len];
		char* cmp_buf = new char[buf_len];

		std::generate(buf, buf + buf_len, &std::rand);
		std::memcpy(cmp_buf, buf, buf_len);

		using namespace boost::asio;
		std::vector<mutable_buffer> iovec;
		iovec.push_back(mutable_buffer(buf, buf_len));
		a->encrypt(iovec);
		TEST_CHECK(!std::equal(buf, buf + buf_len, cmp_buf));
		TEST_CHECK(iovec.empty());
		int consume = 0;
		int produce = buf_len;
		int packet_size = 0;
		iovec.push_back(mutable_buffer(buf, buf_len));
		b->decrypt(iovec, consume, produce, packet_size);
		TEST_CHECK(std::equal(buf, buf + buf_len, cmp_buf));
		TEST_CHECK(iovec.empty());
		TEST_EQUAL(consume, 0);
		TEST_EQUAL(produce, buf_len);
		TEST_EQUAL(packet_size, 0);

		iovec.push_back(mutable_buffer(buf, buf_len));
		b->encrypt(iovec);
		TEST_CHECK(!std::equal(buf, buf + buf_len, cmp_buf));
		TEST_CHECK(iovec.empty());
		consume = 0;
		produce = buf_len;
		packet_size = 0;
		iovec.push_back(mutable_buffer(buf, buf_len));
		a->decrypt(iovec, consume, produce, packet_size);
		TEST_CHECK(std::equal(buf, buf + buf_len, cmp_buf));
		TEST_CHECK(iovec.empty());
		TEST_EQUAL(consume, 0);
		TEST_EQUAL(produce, buf_len);
		TEST_EQUAL(packet_size, 0);

		delete[] buf;
		delete[] cmp_buf;
	}
}

void print_key(char const* key)
{
	for (int i = 0;i < 96; ++i)
	{
		printf("%02x ", unsigned(key[i]));
	}
	printf("\n");
}

TORRENT_TEST(diffie_hellman)
{
	using namespace libtorrent;

#ifdef TORRENT_USE_VALGRIND
	const int repcount = 10;
#else
	const int repcount = 128;
#endif

	for (int rep = 0; rep < repcount; ++rep)
	{
		dh_key_exchange DH1, DH2;

		DH1.compute_secret(DH2.get_local_key());
		DH2.compute_secret(DH1.get_local_key());

		TEST_CHECK(std::equal(DH1.get_secret(), DH1.get_secret() + 96, DH2.get_secret()));
		if (!std::equal(DH1.get_secret(), DH1.get_secret() + 96, DH2.get_secret()))
		{
			printf("DH1 local: ");
			print_key(DH1.get_local_key());

			printf("DH2 local: ");
			print_key(DH2.get_local_key());

			printf("DH1 shared_secret: ");
			print_key(DH1.get_secret());

			printf("DH2 shared_secret: ");
			print_key(DH2.get_secret());
		}
	}
}

TORRENT_TEST(rc4)
{
	using namespace libtorrent;

	sha1_hash test1_key = hasher("test1_key",8).final();
	sha1_hash test2_key = hasher("test2_key",8).final();

	fprintf(stderr, "testing RC4 handler\n");
	rc4_handler rc41;
	rc41.set_incoming_key(&test2_key[0], 20);
	rc41.set_outgoing_key(&test1_key[0], 20);
	rc4_handler rc42;
	rc42.set_incoming_key(&test1_key[0], 20);
	rc42.set_outgoing_key(&test2_key[0], 20);
	test_enc_handler(&rc41, &rc42);
}

#else
TORRENT_TEST(disabled)
{
	fprintf(stderr, "PE test not run because it's disabled\n");
}
#endif

