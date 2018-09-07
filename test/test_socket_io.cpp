/*

Copyright (c) 2014, Arvid Norberg
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
#include "setup_transfer.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/socket.hpp"

#include <string>

using namespace lt;
using namespace lt::detail;

TORRENT_TEST(address_to_bytes)
{
	// test address_to_bytes
	TEST_EQUAL(address_to_bytes(addr4("10.11.12.13")), "\x0a\x0b\x0c\x0d");
	TEST_EQUAL(address_to_bytes(addr4("16.5.127.1")), "\x10\x05\x7f\x01");

	// test endpoint_to_bytes
	TEST_EQUAL(endpoint_to_bytes(uep("10.11.12.13", 8080)), "\x0a\x0b\x0c\x0d\x1f\x90");
	TEST_EQUAL(endpoint_to_bytes(uep("16.5.127.1", 12345)), "\x10\x05\x7f\x01\x30\x39");
}

TORRENT_TEST(read_v4_address)
{
	std::string buf;
	write_address(addr4("16.5.128.1"), std::back_inserter(buf));
	TEST_EQUAL(buf, "\x10\x05\x80\x01");
	address addr = read_v4_address(buf.begin());
	TEST_EQUAL(addr, addr4("16.5.128.1"));

	buf.clear();
	write_endpoint(uep("16.5.128.1", 1337)
		, std::back_inserter(buf));
	TEST_EQUAL(buf, "\x10\x05\x80\x01\x05\x39");
	udp::endpoint ep4 = read_v4_endpoint<udp::endpoint>(buf.begin());
	TEST_EQUAL(ep4, uep("16.5.128.1", 1337));
}

TORRENT_TEST(read_v6_endpoint)
{
	std::string buf;
	write_address(addr6("1000::ffff"), std::back_inserter(buf));
	TEST_CHECK(std::equal(buf.begin(), buf.end(), "\x10\0\0\0\0\0\0\0\0\0\0\0\0\0\xff\xff"));
	address addr = read_v6_address(buf.begin());
	TEST_EQUAL(addr, addr6("1000::ffff"));

	buf.clear();
	write_endpoint(uep("1000::ffff", 1337)
		, std::back_inserter(buf));
	TEST_CHECK(std::equal(buf.begin(), buf.end()
			, "\x10\0\0\0\0\0\0\0\0\0\0\0\0\0\xff\xff\x05\x39"));
	TEST_EQUAL(buf.size(), 18);
	udp::endpoint ep6 = read_v6_endpoint<udp::endpoint>(buf.begin());
	TEST_EQUAL(ep6, uep("1000::ffff", 1337));
}

TORRENT_TEST(read_endpoint_list)
{
	char const eplist[] = "l6:\x10\x05\x80\x01\x05\x39"
		"18:\x10\0\0\0\0\0\0\0\0\0\0\0\0\0\xff\xff\x05\x39" "e";
	bdecode_node e;
	error_code ec;
	bdecode(eplist, eplist + sizeof(eplist)-1, e, ec);
	TEST_CHECK(!ec);
	std::vector<udp::endpoint> list = read_endpoint_list<udp::endpoint>(e);

	TEST_EQUAL(list.size(), 2);
	TEST_EQUAL(list[1], uep("1000::ffff", 1337));
	TEST_EQUAL(list[0], uep("16.5.128.1", 1337));
}

TORRENT_TEST(parse_invalid_ipv4_endpoint)
{
	error_code ec;
	tcp::endpoint endp;

	endp = parse_endpoint("", ec);
	TEST_CHECK(ec);
	ec.clear();

	endp = parse_endpoint("\n\t ", ec);
	TEST_CHECK(ec);
	ec.clear();

	endp = parse_endpoint("127.0.0.1-4", ec);
	TEST_CHECK(ec);
	ec.clear();

	endp = parse_endpoint("127.0.0.1:-4", ec);
	TEST_CHECK(ec);
	ec.clear();

	endp = parse_endpoint("127.0.0.1:66000", ec);
	TEST_CHECK(ec);
	ec.clear();

	endp = parse_endpoint("127.0.0.1:abc", ec);
	TEST_CHECK(ec);
	ec.clear();

	endp = parse_endpoint("127.0.0.1", ec);
	TEST_CHECK(ec);
	ec.clear();

#ifndef TORRENT_WINDOWS
	// it appears windows silently accepts truncated IP addresses
	endp = parse_endpoint("127.0.0:123", ec);
	TEST_CHECK(ec);
	ec.clear();
#endif

	endp = parse_endpoint("127.0.0.1:", ec);
	TEST_CHECK(ec);
	ec.clear();

	endp = parse_endpoint("127.0.0.1X", ec);
	TEST_CHECK(ec);
	ec.clear();
}

TORRENT_TEST(parse_valid_ip4_endpoint)
{
	error_code ec;
	TEST_EQUAL(parse_endpoint("127.0.0.1:4", ec), ep("127.0.0.1", 4));
	TEST_CHECK(!ec);
	ec.clear();

	TEST_EQUAL(parse_endpoint("\t 127.0.0.1:4 \n", ec), ep("127.0.0.1", 4));
	TEST_CHECK(!ec);
	ec.clear();

	TEST_EQUAL(parse_endpoint("127.0.0.1:23", ec), ep("127.0.0.1", 23));
	TEST_CHECK(!ec);
	ec.clear();
}

TORRENT_TEST(parse_invalid_ipv6_endpoint)
{
	error_code ec;
	tcp::endpoint endp;

	endp = parse_endpoint("[::1]-4", ec);
	TEST_CHECK(ec);
	ec.clear();

	endp = parse_endpoint("[::1]", ec);
	TEST_CHECK(ec);
	ec.clear();

	endp = parse_endpoint("[::1]:", ec);
	TEST_CHECK(ec);
	ec.clear();

	endp = parse_endpoint("[::1]X", ec);
	TEST_CHECK(ec);
	ec.clear();

	endp = parse_endpoint("[::1", ec);
	TEST_CHECK(ec == errors::expected_close_bracket_in_address);
	ec.clear();

	parse_endpoint("[ff::1:5", ec);
	TEST_EQUAL(ec, error_code(errors::expected_close_bracket_in_address));
	ec.clear();

	endp = parse_endpoint("[abcd]:123", ec);
	TEST_CHECK(ec);
	ec.clear();

	endp = parse_endpoint("[ff::1]", ec);
	TEST_EQUAL(ec, error_code(errors::invalid_port));
	ec.clear();
}

TORRENT_TEST(parse_valid_ipv6_endpoint)
{
	error_code ec;
	TEST_EQUAL(parse_endpoint("[::1]:4", ec), ep("::1", 4));
	TEST_CHECK(!ec);
	ec.clear();

	TEST_EQUAL(parse_endpoint(" \t[ff::1]:1214 \r", ec), ep("ff::1", 1214));
	TEST_CHECK(!ec);
	ec.clear();
}

