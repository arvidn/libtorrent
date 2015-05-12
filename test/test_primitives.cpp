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
#include "libtorrent/escape_string.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/timestamp_history.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/bloom_filter.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/ip_voter.hpp"
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

address rand_v4()
{
	return address_v4(((boost::uint32_t(rand()) << 16) | rand()) & 0xffffffff);
}

#if TORRENT_USE_IPV6
address rand_v6()
{
	address_v6::bytes_type bytes;
	for (int i = 0; i < bytes.size(); ++i) bytes[i] = rand() & 0xff;
	return address_v6(bytes);
}
#endif

int test_main()
{
	using namespace libtorrent;
	using namespace libtorrent::dht;
	error_code ec;

	// make sure the retry interval keeps growing
	// on failing announces
	announce_entry ae("dummy");
	int last = 0;
	session_settings sett;
	sett.tracker_backoff = 250;
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

	// test bloom_filter
	bloom_filter<32> filter;
	sha1_hash k1 = hasher("test1", 5).final();
	sha1_hash k2 = hasher("test2", 5).final();
	sha1_hash k3 = hasher("test3", 5).final();
	sha1_hash k4 = hasher("test4", 5).final();
	TEST_CHECK(!filter.find(k1));
	TEST_CHECK(!filter.find(k2));
	TEST_CHECK(!filter.find(k3));
	TEST_CHECK(!filter.find(k4));

	filter.set(k1);
	TEST_CHECK(filter.find(k1));
	TEST_CHECK(!filter.find(k2));
	TEST_CHECK(!filter.find(k3));
	TEST_CHECK(!filter.find(k4));

	filter.set(k4);
	TEST_CHECK(filter.find(k1));
	TEST_CHECK(!filter.find(k2));
	TEST_CHECK(!filter.find(k3));
	TEST_CHECK(filter.find(k4));

	// test timestamp_history
	{
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

	// test identify_client

	TEST_EQUAL(identify_client(peer_id("-AZ123B-............")), "Azureus 1.2.3.11");
	TEST_EQUAL(identify_client(peer_id("-AZ1230-............")), "Azureus 1.2.3");
	TEST_EQUAL(identify_client(peer_id("S123--..............")), "Shadow 1.2.3");
	TEST_EQUAL(identify_client(peer_id("S\x1\x2\x3....\0...........")), "Shadow 1.2.3");
	TEST_EQUAL(identify_client(peer_id("M1-2-3--............")), "Mainline 1.2.3");
	TEST_EQUAL(identify_client(peer_id("\0\0\0\0\0\0\0\0\0\0\0\0........")), "Generic");
	TEST_EQUAL(identify_client(peer_id("-xx1230-............")), "xx 1.2.3");

	// test network functions

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
		address::from_string("10.0.1.3", ec),
		address::from_string("10.0.3.3", ec),
		address::from_string("255.255.0.0", ec)));

	TEST_CHECK(!match_addr_mask(
		address::from_string("10.0.1.3", ec),
		address::from_string("10.1.3.3", ec),
		address::from_string("255.255.0.0", ec)));

	// test peer_id/sha1_hash type

	sha1_hash h1(0);
	sha1_hash h2(0);
	TEST_CHECK(h1 == h2);
	TEST_CHECK(!(h1 != h2));
	TEST_CHECK(!(h1 < h2));
	TEST_CHECK(!(h1 < h2));
	TEST_CHECK(h1.is_all_zeros());

	h1 = to_hash("0123456789012345678901234567890123456789");
	h2 = to_hash("0113456789012345678901234567890123456789");

	TEST_CHECK(h2 < h1);
	TEST_CHECK(h2 == h2);
	TEST_CHECK(h1 == h1);
	h2.clear();
	TEST_CHECK(h2.is_all_zeros());
	
	h2 = to_hash("ffffffffff0000000000ffffffffff0000000000");
	h1 = to_hash("fffff00000fffff00000fffff00000fffff00000");
	h1 &= h2;
	TEST_CHECK(h1 == to_hash("fffff000000000000000fffff000000000000000"));

	h2 = to_hash("ffffffffff0000000000ffffffffff0000000000");
	h1 = to_hash("fffff00000fffff00000fffff00000fffff00000");
	h1 |= h2;
	TEST_CHECK(h1 == to_hash("fffffffffffffff00000fffffffffffffff00000"));
	
	h2 = to_hash("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
	h1 ^= h2;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("f0f0f0f0f0f0f0ff0f0ff0f0f0f0f0f0f0ff0f0f"));
	TEST_CHECK(h1 != h2);

	h2 = sha1_hash("                    ");
	TEST_CHECK(h2 == to_hash("2020202020202020202020202020202020202020"));

	h1 = to_hash("ffffffffff0000000000ffffffffff0000000000");
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	h1 <<= 12;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("fffffff0000000000ffffffffff0000000000000"));
	h1 >>= 12;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("000fffffff0000000000ffffffffff0000000000"));

	h1 = to_hash("7000000000000000000000000000000000000000");
	h1 <<= 1;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("e000000000000000000000000000000000000000"));

	h1 = to_hash("0000000000000000000000000000000000000007");
	h1 <<= 1;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("000000000000000000000000000000000000000e"));

	h1 = to_hash("0000000000000000000000000000000000000007");
	h1 >>= 1;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("0000000000000000000000000000000000000003"));

	h1 = to_hash("7000000000000000000000000000000000000000");
	h1 >>= 1;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("3800000000000000000000000000000000000000"));
	
	// CIDR distance test
	h1 = to_hash("0123456789abcdef01232456789abcdef0123456");
	h2 = to_hash("0123456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 160);
	h2 = to_hash("0120456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 14);
	h2 = to_hash("012f456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 12);
	h2 = to_hash("0123456789abcdef11232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 16 * 4 + 3);

	return 0;
}

