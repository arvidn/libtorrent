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
#include "libtorrent/socket_io.hpp"
#include "setup_transfer.hpp" // for rand_v4

using namespace libtorrent;

bool cast_vote(ip_voter& ipv, address ext_ip, address voter)
{
	bool new_ip = ipv.cast_vote(ext_ip, 1, voter);
	fprintf(stderr, "%15s -> %-15s\n"
		, print_address(voter).c_str()
		, print_address(ext_ip).c_str());
	if (new_ip)
	{
		fprintf(stderr, "   \x1b[1mnew external IP: %s\x1b[0m\n"
			, print_address(ipv.external_address()).c_str());
	}
	return new_ip;
}

// test the case where every time we get a new IP. Make sure
// we don't flap
TORRENT_TEST(test_random)
{
	ip_voter ipv;

	address_v4 addr1(address_v4::from_string("51.41.61.132"));

	bool new_ip = cast_vote(ipv, addr1, rand_v4());
	TEST_CHECK(new_ip);
	TEST_CHECK(ipv.external_address() == addr1);
	for (int i = 0; i < 1000; ++i)
	{
		new_ip = cast_vote(ipv, rand_v4(), rand_v4());
		TEST_CHECK(!new_ip);
	}
	TEST_CHECK(ipv.external_address() == addr1);
}

TORRENT_TEST(two_ips)
{
	ip_voter ipv;

	address_v4 addr1(address_v4::from_string("51.1.1.1"));
	address_v4 addr2(address_v4::from_string("53.3.3.3"));

	// addr1 is the first address we see, which is the one we pick. Even though
	// we'll have as many votes for addr2, we shouldn't flap, since addr2 never
	// gets an overwhelming majority.
	bool new_ip = cast_vote(ipv, addr1, rand_v4());
	TEST_CHECK(new_ip);
	for (int i = 0; i < 1000; ++i)
	{
		new_ip = cast_vote(ipv, addr2, rand_v4());
		TEST_CHECK(!new_ip);
		new_ip = cast_vote(ipv, rand_v4(), rand_v4());
		TEST_CHECK(!new_ip);
		new_ip = cast_vote(ipv, addr1, rand_v4());
		TEST_CHECK(!new_ip);

		TEST_CHECK(ipv.external_address() == addr1);
	}
}

TORRENT_TEST(one_ip)
{
	ip_voter ipv;

	address_v4 start_addr(address_v4::from_string("93.12.63.174"));
	address_v4 addr1(address_v4::from_string("51.1.1.1"));
	address_v4 addr2(address_v4::from_string("53.3.3.3"));

	bool new_ip = cast_vote(ipv, start_addr, rand_v4());
	TEST_CHECK(new_ip);
	TEST_CHECK(ipv.external_address() != addr1);
	TEST_CHECK(ipv.external_address() == start_addr);
	for (int i = 0; i < 30; ++i)
	{
		new_ip = cast_vote(ipv, addr2, rand_v4());
		if (new_ip) break;
		new_ip = cast_vote(ipv, rand_v4(), rand_v4());
		if (new_ip) break;
		new_ip = cast_vote(ipv, addr1, rand_v4());
		if (new_ip) break;
		new_ip = cast_vote(ipv, addr1, rand_v4());
		if (new_ip) break;

	}

	TEST_CHECK(ipv.external_address() == addr1);

	for (int i = 0; i < 500; ++i)
	{
		new_ip = cast_vote(ipv, addr2, rand_v4());
		TEST_CHECK(!new_ip);
		new_ip = cast_vote(ipv, rand_v4(), rand_v4());
		TEST_CHECK(!new_ip);
		new_ip = cast_vote(ipv, addr1, rand_v4());
		TEST_CHECK(!new_ip);
		new_ip = cast_vote(ipv, addr1, rand_v4());
		TEST_CHECK(!new_ip);
	}

	TEST_CHECK(ipv.external_address() == addr1);
}

