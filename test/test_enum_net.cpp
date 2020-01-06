/*

Copyright (c) 2008-2015, Arvid Norberg
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
#include "libtorrent/enum_net.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"

using namespace lt;

TORRENT_TEST(is_local)
{
	error_code ec;
	TEST_CHECK(is_local(address::from_string("192.168.0.1", ec)));
	TEST_CHECK(!ec);
	TEST_CHECK(is_local(address::from_string("10.1.1.56", ec)));
	TEST_CHECK(!ec);
	TEST_CHECK(!is_local(address::from_string("14.14.251.63", ec)));
	TEST_CHECK(!ec);
}

TORRENT_TEST(is_loopback)
{
	error_code ec;
	TEST_CHECK(is_loopback(address::from_string("127.0.0.1", ec)));
	TEST_CHECK(!ec);
	if (supports_ipv6())
	{
		TEST_CHECK(is_loopback(address::from_string("::1", ec)));
		TEST_CHECK(!ec);
	}
}

TORRENT_TEST(is_any)
{
	TEST_CHECK(is_any(address_v4::any()));
	error_code ec;
	TEST_CHECK(!is_any(address::from_string("31.53.21.64", ec)));
	TEST_CHECK(!ec);
	if (supports_ipv6())
	{
		TEST_CHECK(is_any(address_v6::any()));
		TEST_CHECK(!ec);
	}
}

TORRENT_TEST(match_addr_mask)
{
	error_code ec;
	TEST_CHECK(match_addr_mask(
		address::from_string("10.0.1.176", ec),
		address::from_string("10.0.1.176", ec),
		address::from_string("255.255.255.0", ec)));
	TEST_CHECK(!ec);

	TEST_CHECK(match_addr_mask(
		address::from_string("10.0.1.3", ec),
		address::from_string("10.0.3.3", ec),
		address::from_string("255.255.0.0", ec)));
	TEST_CHECK(!ec);

	TEST_CHECK(!match_addr_mask(
		address::from_string("10.0.1.3", ec),
		address::from_string("10.1.3.3", ec),
		address::from_string("255.255.0.0", ec)));
	TEST_CHECK(!ec);
}

TORRENT_TEST(is_ip_address)
{
	TEST_EQUAL(is_ip_address("1.2.3.4"), true);
	TEST_EQUAL(is_ip_address("a.b.c.d"), false);
	TEST_EQUAL(is_ip_address("a:b:b:c"), false);
	TEST_EQUAL(is_ip_address("::1"), true);
	TEST_EQUAL(is_ip_address("2001:db8:85a3:0:0:8a2e:370:7334"), true);
}

TORRENT_TEST(build_netmask_v4)
{
	TEST_CHECK(build_netmask(0, AF_INET)  == make_address("0.0.0.0"));
	TEST_CHECK(build_netmask(1, AF_INET)  == make_address("128.0.0.0"));
	TEST_CHECK(build_netmask(2, AF_INET)  == make_address("192.0.0.0"));
	TEST_CHECK(build_netmask(3, AF_INET)  == make_address("224.0.0.0"));
	TEST_CHECK(build_netmask(4, AF_INET)  == make_address("240.0.0.0"));
	TEST_CHECK(build_netmask(5, AF_INET)  == make_address("248.0.0.0"));
	TEST_CHECK(build_netmask(6, AF_INET)  == make_address("252.0.0.0"));
	TEST_CHECK(build_netmask(7, AF_INET)  == make_address("254.0.0.0"));
	TEST_CHECK(build_netmask(8, AF_INET)  == make_address("255.0.0.0"));
	TEST_CHECK(build_netmask(9, AF_INET)  == make_address("255.128.0.0"));
	TEST_CHECK(build_netmask(10, AF_INET) == make_address("255.192.0.0"));
	TEST_CHECK(build_netmask(11, AF_INET) == make_address("255.224.0.0"));

	TEST_CHECK(build_netmask(22, AF_INET) == make_address("255.255.252.0"));
	TEST_CHECK(build_netmask(23, AF_INET) == make_address("255.255.254.0"));
	TEST_CHECK(build_netmask(24, AF_INET) == make_address("255.255.255.0"));
	TEST_CHECK(build_netmask(25, AF_INET) == make_address("255.255.255.128"));
	TEST_CHECK(build_netmask(26, AF_INET) == make_address("255.255.255.192"));
	TEST_CHECK(build_netmask(27, AF_INET) == make_address("255.255.255.224"));
	TEST_CHECK(build_netmask(28, AF_INET) == make_address("255.255.255.240"));
	TEST_CHECK(build_netmask(29, AF_INET) == make_address("255.255.255.248"));
	TEST_CHECK(build_netmask(30, AF_INET) == make_address("255.255.255.252"));
	TEST_CHECK(build_netmask(31, AF_INET) == make_address("255.255.255.254"));
	TEST_CHECK(build_netmask(32, AF_INET) == make_address("255.255.255.255"));
}

TORRENT_TEST(build_netmask_v6)
{
	TEST_CHECK(build_netmask(0, AF_INET6)  == make_address("::"));
	TEST_CHECK(build_netmask(1, AF_INET6)  == make_address("8000::"));
	TEST_CHECK(build_netmask(2, AF_INET6)  == make_address("c000::"));
	TEST_CHECK(build_netmask(3, AF_INET6)  == make_address("e000::"));
	TEST_CHECK(build_netmask(4, AF_INET6)  == make_address("f000::"));
	TEST_CHECK(build_netmask(5, AF_INET6)  == make_address("f800::"));
	TEST_CHECK(build_netmask(6, AF_INET6)  == make_address("fc00::"));
	TEST_CHECK(build_netmask(7, AF_INET6)  == make_address("fe00::"));
	TEST_CHECK(build_netmask(8, AF_INET6)  == make_address("ff00::"));
	TEST_CHECK(build_netmask(9, AF_INET6)  == make_address("ff80::"));
	TEST_CHECK(build_netmask(10, AF_INET6) == make_address("ffc0::"));
	TEST_CHECK(build_netmask(11, AF_INET6) == make_address("ffe0::"));

	TEST_CHECK(build_netmask(119, AF_INET6) == make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:fe00"));
	TEST_CHECK(build_netmask(120, AF_INET6) == make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ff00"));
	TEST_CHECK(build_netmask(121, AF_INET6) == make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ff80"));
	TEST_CHECK(build_netmask(122, AF_INET6) == make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffc0"));
	TEST_CHECK(build_netmask(123, AF_INET6) == make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffe0"));
	TEST_CHECK(build_netmask(124, AF_INET6) == make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:fff0"));
	TEST_CHECK(build_netmask(125, AF_INET6) == make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:fff8"));
	TEST_CHECK(build_netmask(126, AF_INET6) == make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffc"));
	TEST_CHECK(build_netmask(127, AF_INET6) == make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe"));
	TEST_CHECK(build_netmask(128, AF_INET6) == make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
}

TORRENT_TEST(build_netmask_unknown)
{
	TEST_CHECK(build_netmask(0, -1) == address{});
}
