/*

Copyright (c) 2008-2012, Arvid Norberg
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

#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/socket_io.hpp"
#include <boost/bind.hpp>
#include <iostream>
#include <set>

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;

sha1_hash to_hash(char const* s)
{
	sha1_hash ret;
	from_hex(s, 40, (char*)&ret[0]);
	return ret;
}

address_v4 v4(char const* str)
{
	error_code ec;
	return address_v4::from_string(str, ec);
}

#if TORRENT_USE_IPV6
address_v6 v6(char const* str)
{
	error_code ec;
	return address_v6::from_string(str, ec);
}
#endif

static tcp::endpoint ep(char const* ip, int port)
{
	error_code ec;
	return tcp::endpoint(address::from_string(ip, ec), port);
}

TORRENT_TEST(primitives)
{
	using namespace libtorrent;
	error_code ec;

	sliding_average<4> avg;
	TEST_EQUAL(avg.mean(), 0);
	TEST_EQUAL(avg.avg_deviation(), 0);
	avg.add_sample(500);
	TEST_EQUAL(avg.mean(), 500);
	TEST_EQUAL(avg.avg_deviation(), 0);
	avg.add_sample(501);
	TEST_EQUAL(avg.avg_deviation(), 1);
	avg.add_sample(0);
	avg.add_sample(0);
	printf("avg: %d dev: %d\n", avg.mean(), avg.avg_deviation());
	TEST_CHECK(abs(avg.mean() - 250) < 50);
	TEST_CHECK(abs(avg.avg_deviation() - 250) < 80);

	// make sure the retry interval keeps growing
	// on failing announces
	announce_entry ae("dummy");
	int last = 0;
	aux::session_settings sett;
	sett.set_int(settings_pack::tracker_backoff, 250);
	for (int i = 0; i < 10; ++i)
	{
		ae.failed(sett, 5);
		int delay = ae.next_announce_in();
		TEST_CHECK(delay > last);
		last = delay;
		fprintf(stderr, "%d, ", delay);
	}
	fprintf(stderr, "\n");

	// test external ip voting
	external_ip ipv1;

	// test a single malicious node
	// adds 50 legitimate responses from different peers
	// and 50 malicious responses from the same peer
	address real_external = address_v4::from_string("5.5.5.5", ec);
	TEST_CHECK(!ec);
	address malicious = address_v4::from_string("4.4.4.4", ec);
	TEST_CHECK(!ec);
	for (int i = 0; i < 50; ++i)
	{
		ipv1.cast_vote(real_external, aux::session_impl::source_dht, rand_v4());
		ipv1.cast_vote(rand_v4(), aux::session_impl::source_dht, malicious);
	}
	TEST_CHECK(ipv1.external_address(rand_v4()) == real_external);

	external_ip ipv2;

	// test a single malicious node
	// adds 50 legitimate responses from different peers
	// and 50 consistent malicious responses from the same peer
	address real_external1 = address_v4::from_string("5.5.5.5", ec);
	TEST_CHECK(!ec);
	address real_external2;
#if TORRENT_USE_IPV6
	if (supports_ipv6())
	{
		real_external2 = address_v6::from_string("2f80::", ec);
		TEST_CHECK(!ec);
	}
#endif
	malicious = address_v4::from_string("4.4.4.4", ec);
	TEST_CHECK(!ec);
	address malicious_external = address_v4::from_string("3.3.3.3", ec);
	TEST_CHECK(!ec);
	for (int i = 0; i < 50; ++i)
	{
		ipv2.cast_vote(real_external1, aux::session_impl::source_dht, rand_v4());
#if TORRENT_USE_IPV6
		if (supports_ipv6())
			ipv2.cast_vote(real_external2, aux::session_impl::source_dht, rand_v6());
#endif
		ipv2.cast_vote(malicious_external, aux::session_impl::source_dht, malicious);
	}
	TEST_CHECK(ipv2.external_address(rand_v4()) == real_external1);
#if TORRENT_USE_IPV6
	if (supports_ipv6())
		TEST_CHECK(ipv2.external_address(rand_v6()) == real_external2);
#endif

	// test error codes
	TEST_CHECK(error_code(errors::http_error).message() == "HTTP error");
	TEST_CHECK(error_code(errors::missing_file_sizes).message() == "missing or invalid 'file sizes' entry");
	TEST_CHECK(error_code(errors::unsupported_protocol_version).message() == "unsupported protocol version");
	TEST_CHECK(error_code(errors::no_i2p_router).message() == "no i2p router is set up");
	TEST_CHECK(error_code(errors::http_parse_error).message() == "Invalid HTTP header");
	TEST_CHECK(error_code(errors::error_code_max).message() == "Unknown error");

	TEST_CHECK(error_code(errors::unauthorized, get_http_category()).message() == "401 Unauthorized");
	TEST_CHECK(error_code(errors::service_unavailable, get_http_category()).message() == "503 Service Unavailable");

	// test snprintf

	char msg[10];
	snprintf(msg, sizeof(msg), "too %s format string", "long");
	TEST_CHECK(strcmp(msg, "too long ") == 0);

	if (supports_ipv6())
	{
		// make sure the assumption we use in policy's peer list hold
		std::multimap<address, int> peers;
		std::multimap<address, int>::iterator i;
		peers.insert(std::make_pair(address::from_string("::1", ec), 0));
		peers.insert(std::make_pair(address::from_string("::2", ec), 3));
		peers.insert(std::make_pair(address::from_string("::3", ec), 5));
		i = peers.find(address::from_string("::2", ec));
		TEST_CHECK(i != peers.end());
		if (i != peers.end())
		{
			TEST_CHECK(i->first == address::from_string("::2", ec));
			TEST_CHECK(i->second == 3);
		}
	}

	// test network functions

	// TODO: 3 move this out to a test_enum_net test
	TEST_CHECK(is_local(address::from_string("192.168.0.1", ec)));
	TEST_CHECK(is_local(address::from_string("10.1.1.56", ec)));
	TEST_CHECK(!is_local(address::from_string("14.14.251.63", ec)));
	TEST_CHECK(is_loopback(address::from_string("127.0.0.1", ec)));
#if TORRENT_USE_IPV6
	if (supports_ipv6())
	{
		TEST_CHECK(is_loopback(address::from_string("::1", ec)));
		TEST_CHECK(is_any(address_v6::any()));
	}
#endif
	TEST_CHECK(is_any(address_v4::any()));
	TEST_CHECK(!is_any(address::from_string("31.53.21.64", ec)));

	TEST_CHECK(match_addr_mask(
		address::from_string("10.0.1.176", ec),
		address::from_string("10.0.1.176", ec),
		address::from_string("255.255.255.0", ec)));

	TEST_CHECK(match_addr_mask(
		address::from_string("10.0.1.3", ec),
		address::from_string("10.0.3.3", ec),
		address::from_string("255.255.0.0", ec)));

	TEST_CHECK(!match_addr_mask(
		address::from_string("10.0.1.3", ec),
		address::from_string("10.1.3.3", ec),
		address::from_string("255.255.0.0", ec)));

	// CIDR distance test
	sha1_hash h1 = to_hash("0123456789abcdef01232456789abcdef0123456");
	sha1_hash h2 = to_hash("0123456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 160);
	h2 = to_hash("0120456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 14);
	h2 = to_hash("012f456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 12);
	h2 = to_hash("0123456789abcdef11232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 16 * 4 + 3);

	// test print_endpoint, parse_endpoint and print_address
	TEST_EQUAL(print_endpoint(ep("127.0.0.1", 23)), "127.0.0.1:23");
#if TORRENT_USE_IPV6
	TEST_EQUAL(print_endpoint(ep("ff::1", 1214)), "[ff::1]:1214");
#endif
	ec.clear();
	TEST_EQUAL(parse_endpoint("127.0.0.1:23", ec), ep("127.0.0.1", 23));
	TEST_CHECK(!ec);
	ec.clear();
#if TORRENT_USE_IPV6
	TEST_EQUAL(parse_endpoint(" \t[ff::1]:1214 \r", ec), ep("ff::1", 1214));
	TEST_CHECK(!ec);
#endif
	TEST_EQUAL(print_address(v4("241.124.23.5")), "241.124.23.5");
#if TORRENT_USE_IPV6
	TEST_EQUAL(print_address(v6("2001:ff::1")), "2001:ff::1");
	parse_endpoint("[ff::1]", ec);
	TEST_EQUAL(ec, error_code(errors::invalid_port, get_libtorrent_category()));
#endif

	parse_endpoint("[ff::1:5", ec);
	TEST_EQUAL(ec, error_code(errors::expected_close_bracket_in_address, get_libtorrent_category()));

	// test address_to_bytes
	TEST_EQUAL(address_to_bytes(address_v4::from_string("10.11.12.13")), "\x0a\x0b\x0c\x0d");
	TEST_EQUAL(address_to_bytes(address_v4::from_string("16.5.127.1")), "\x10\x05\x7f\x01");

	// test endpoint_to_bytes
	TEST_EQUAL(endpoint_to_bytes(udp::endpoint(address_v4::from_string("10.11.12.13"), 8080)), "\x0a\x0b\x0c\x0d\x1f\x90");
	TEST_EQUAL(endpoint_to_bytes(udp::endpoint(address_v4::from_string("16.5.127.1"), 12345)), "\x10\x05\x7f\x01\x30\x39");
	return 0;
}

