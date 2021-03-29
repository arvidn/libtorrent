/*

Copyright (c) 2015-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Steven Siloti
Copyright (c) 2018, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/ip_voter.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/aux_/socket_io.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "setup_transfer.hpp" // for rand_v4, supports_ipv6

using namespace lt;

namespace {

bool cast_vote(aux::ip_voter& ipv, address ext_ip, address voter)
{
	bool new_ip = ipv.cast_vote(ext_ip, aux::session_interface::source_dht, voter);
	std::printf("%15s -> %-15s\n"
		, aux::print_address(voter).c_str()
		, aux::print_address(ext_ip).c_str());
	if (new_ip)
	{
		std::printf("   \x1b[1mnew external IP: %s\x1b[0m\n"
			, aux::print_address(ipv.external_address()).c_str());
	}
	return new_ip;
}

} // anonymous namespace

// test the case where every time we get a new IP. Make sure
// we don't flap
TORRENT_TEST(test_random)
{
	init_rand_address();

	aux::ip_voter ipv;

	address_v4 addr1(make_address_v4("51.41.61.132"));

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
	init_rand_address();

	aux::ip_voter ipv;

	address_v4 addr1(make_address_v4("51.1.1.1"));
	address_v4 addr2(make_address_v4("53.3.3.3"));

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
	init_rand_address();

	aux::ip_voter ipv;

	address_v4 start_addr(make_address_v4("93.12.63.174"));
	address_v4 addr1(make_address_v4("51.1.1.1"));
	address_v4 addr2(make_address_v4("53.3.3.3"));

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

TORRENT_TEST(ip_voter_1)
{
	init_rand_address();

	// test external ip voting
	aux::ip_voter ipv1;

	// test a single malicious node
	// adds 50 legitimate responses from different peers
	// and 50 malicious responses from the same peer
	error_code ec;
	address real_external = make_address_v4("5.5.5.5", ec);
	TEST_CHECK(!ec);
	address malicious = make_address_v4("4.4.4.4", ec);
	TEST_CHECK(!ec);
	for (int i = 0; i < 50; ++i)
	{
		ipv1.cast_vote(real_external, aux::session_interface::source_dht, rand_v4());
		ipv1.cast_vote(rand_v4(), aux::session_interface::source_dht, malicious);
	}
	TEST_CHECK(ipv1.external_address() == real_external);
}

TORRENT_TEST(ip_voter_2)
{
	init_rand_address();

	aux::ip_voter ipv2,ipv6;

	// test a single malicious node
	// adds 50 legitimate responses from different peers
	// and 50 consistent malicious responses from the same peer
	error_code ec;
	address malicious = make_address_v4("4.4.4.4", ec);
	TEST_CHECK(!ec);
	address real_external1 = make_address_v4("5.5.5.5", ec);
	TEST_CHECK(!ec);
	address malicious_external = make_address_v4("3.3.3.3", ec);
	TEST_CHECK(!ec);

	address malicious2;
	address real_external2;
	address malicious_external2;
	if (supports_ipv6())
	{
		malicious2 = make_address_v6("2f90::", ec);
		TEST_CHECK(!ec);
		real_external2 = make_address_v6("2f80::", ec);
		TEST_CHECK(!ec);
		malicious_external2 = make_address_v6("2f70::", ec);
		TEST_CHECK(!ec);
	}

	for (int i = 0; i < 50; ++i)
	{
		ipv2.cast_vote(real_external1, aux::session_interface::source_dht, rand_v4());
		ipv2.cast_vote(malicious_external, aux::session_interface::source_dht, malicious);
		if (supports_ipv6())
		{
			ipv6.cast_vote(real_external2, aux::session_interface::source_dht, rand_v6());
			ipv6.cast_vote(malicious_external2, aux::session_interface::source_dht, malicious2);
		}
	}
	TEST_CHECK(ipv2.external_address() == real_external1);
	if (supports_ipv6())
		TEST_CHECK(ipv6.external_address() == real_external2);
}
