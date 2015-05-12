/*

Copyright (c) 2015, Arvid Norberg
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
#include "libtorrent/ip_voter.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/random.hpp"

using namespace libtorrent;

address rand_v4()
{
	return address_v4((rand() << 16 | rand()) & 0xffffffff);
}

udp::endpoint rand_ep()
{
	return udp::endpoint(rand_v4(), rand());
}

// test the case where every time we get a new IP. Make sure
// we don't flap
void test_random()
{
	ip_voter ipv;

	random_seed(100);

	bool new_ip = ipv.cast_vote(rand_v4(), 1, rand_v4());
	TEST_CHECK(new_ip);
	for (int i = 0; i < 1000; ++i)
	{
		new_ip = ipv.cast_vote(rand_v4(), 1, rand_v4());
		TEST_CHECK(!new_ip);
	}
}

void test_two_ips()
{
	ip_voter ipv;

	random_seed(100);

	address_v4 addr1(address_v4::from_string("51.1.1.1"));
	address_v4 addr2(address_v4::from_string("53.3.3.3"));

	// addr1 is the first address we see, which is the one we pick. Even though
	// we'll have as many votes for addr2, we shouldn't flap, since addr2 never
	// gets an overwhelming majority.
	bool new_ip = ipv.cast_vote(addr1, 1, rand_v4());
	TEST_CHECK(new_ip);
	for (int i = 0; i < 1000; ++i)
	{
		fprintf(stderr, "%d\n", i);
		new_ip = ipv.cast_vote(addr2, 1, rand_v4());
		TEST_CHECK(!new_ip);
		new_ip = ipv.cast_vote(rand_v4(), 1, rand_v4());
		TEST_CHECK(!new_ip);
		new_ip = ipv.cast_vote(addr1, 1, rand_v4());
		TEST_CHECK(!new_ip);

		TEST_CHECK(ipv.external_address() == addr1);
	}
}

void test_one_ip()
{
	ip_voter ipv;

	random_seed(100);

	address_v4 addr1(address_v4::from_string("51.1.1.1"));
	address_v4 addr2(address_v4::from_string("53.3.3.3"));

	bool new_ip = ipv.cast_vote(rand_v4(), 1, rand_v4());
	TEST_CHECK(new_ip);
	bool switched_ip = false;
	for (int i = 0; i < 1000; ++i)
	{
		new_ip = ipv.cast_vote(addr2, 1, rand_v4());
		TEST_CHECK(!new_ip);
		new_ip = ipv.cast_vote(rand_v4(), 1, rand_v4());
		TEST_CHECK(!new_ip);
		new_ip = ipv.cast_vote(addr1, 1, rand_v4());
		if (new_ip) switched_ip = true;
		new_ip = ipv.cast_vote(addr1, 1, rand_v4());
		if (new_ip) switched_ip = true;

		if (switched_ip)
		{
			TEST_CHECK(ipv.external_address() == addr1);
		}
	}
	TEST_CHECK(switched_ip);
	TEST_CHECK(ipv.external_address() == addr1);
}

int test_main()
{
	test_random();
	test_two_ips();
	test_one_ip();
	return 0;
}

