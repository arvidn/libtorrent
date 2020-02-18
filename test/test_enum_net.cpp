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
#include <cstring>

using namespace lt;
using boost::none;

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
	TEST_CHECK(match_addr_mask(
		address::from_string("10.0.1.176"),
		address::from_string("10.0.1.176"),
		address::from_string("255.255.255.0")));

	TEST_CHECK(match_addr_mask(
		address::from_string("10.0.1.3"),
		address::from_string("10.0.3.3"),
		address::from_string("255.255.0.0")));

	TEST_CHECK(!match_addr_mask(
		address::from_string("10.0.1.3"),
		address::from_string("10.1.3.3"),
		address::from_string("255.255.0.0")));

	TEST_CHECK(match_addr_mask(
		address::from_string("ff00:1234::"),
		address::from_string("ff00:5678::"),
		address::from_string("ffff::")));

	TEST_CHECK(!match_addr_mask(
		address::from_string("ff00:1234::"),
		address::from_string("ff00:5678::"),
		address::from_string("ffff:f000::")));

	// different scope IDs always means a mismatch
	TEST_CHECK(!match_addr_mask(
		address::from_string("ff00:1234::%1"),
		address::from_string("ff00:1234::%2"),
		address::from_string("ffff::")));
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

namespace {
	ip_route rt(char const* ip, char const* device, char const* gateway, char const* mask)
	{
		ip_route ret;
		ret.destination = address::from_string(ip);
		ret.gateway = address::from_string(gateway);
		ret.netmask = address::from_string(mask);
		std::strncpy(ret.name, device, sizeof(ret.name));
		ret.name[sizeof(ret.name) - 1] = '\0';
		return ret;
	}

	ip_interface ip(char const* addr, char const* name)
	{
		ip_interface ret;
		ret.interface_address = address::from_string(addr);
		ret.netmask = address::from_string("255.255.255.255");
		std::strncpy(ret.name, name, sizeof(ret.name));
		return ret;
	}
}

TORRENT_TEST(get_gateway_basic)
{
	std::vector<ip_route> const routes = {
		rt("0.0.0.0", "eth0", "192.168.0.1", "255.255.255.0"),
		rt("::", "eth0", "2a02::1234", "ffff::")
	};

	TEST_CHECK(get_gateway(ip("192.168.0.130", "eth0"), routes) == address::from_string("192.168.0.1"));
	TEST_CHECK(get_gateway(ip("2a02::4567", "eth0"), routes) == address::from_string("2a02::1234"));

	// the device name does not match the route
	TEST_CHECK(get_gateway(ip("192.168.0.130", "eth1"), routes) == none);
	TEST_CHECK(get_gateway(ip("2a02::4567", "eth1"), routes) == none);

	// for IPv6, the address family and device name matches, so it's a match
	TEST_CHECK(get_gateway(ip("2a02:8000::0123:4567", "eth0"), routes) == address::from_string("2a02::1234"));
}

TORRENT_TEST(get_gateway_no_default_route)
{
	std::vector<ip_route> const routes = {
		rt("192.168.0.0", "eth0", "0.0.0.0", "0.0.0.0"),
		rt("2a02::", "eth0", "::", "ffff::")
	};

	// no default route
	TEST_CHECK(get_gateway(ip("192.168.1.130", "eth0"), routes) == none);
	TEST_CHECK(get_gateway(ip("2a02::1234", "eth0"), routes) == none);
}

TORRENT_TEST(get_gateway_local_v6)
{
	std::vector<ip_route> const routes = {
		rt("2a02::", "eth0", "::", "ffff::")
	};

	// local IPv6 addresses never have a gateway
	TEST_CHECK(get_gateway(ip("fe80::1234", "eth0"), routes) == none);
}

// an odd, imaginary setup, where the loopback network has a gateway
TORRENT_TEST(get_gateway_loopback)
{
	std::vector<ip_route> const routes = {
		rt("0.0.0.0", "eth0", "192.168.0.1", "255.255.0.0"),
		rt("0.0.0.0", "lo", "127.1.1.1", "255.0.0.0"),
		rt("::", "eth0", "fec0::1234", "ffff::"),
		rt("::", "lo", "::2", "ffff:ffff:ffff:ffff::")
	};

	TEST_CHECK(get_gateway(ip("127.0.0.1", "lo"), routes) == address::from_string("127.1.1.1"));

	// with IPv6, there are no gateways for local or loopback addresses
	TEST_CHECK(get_gateway(ip("::1", "lo"), routes) == none);
}

TORRENT_TEST(get_gateway_multi_homed)
{
	std::vector<ip_route> const routes = {
		rt("0.0.0.0", "eth0", "192.168.0.1", "255.255.0.0"),
		rt("0.0.0.0", "eth1", "10.0.0.1", "255.0.0.0")
	};

	TEST_CHECK(get_gateway(ip("192.168.0.130", "eth0"), routes) == address::from_string("192.168.0.1"));
	TEST_CHECK(get_gateway(ip("10.0.1.130", "eth1"), routes) == address::from_string("10.0.0.1"));
}

TORRENT_TEST(has_default_route)
{
	std::vector<ip_route> const routes = {
		rt("0.0.0.0", "eth0", "192.168.0.1", "255.255.0.0"),
		rt("0.0.0.0", "eth1", "0.0.0.0", "255.0.0.0"),
		rt("127.0.0.0", "lo", "0.0.0.0", "255.0.0.0")
	};

	TEST_CHECK(has_default_route("eth0", AF_INET, routes));
	TEST_CHECK(!has_default_route("eth0", AF_INET6, routes));

	TEST_CHECK(has_default_route("eth1", AF_INET, routes));
	TEST_CHECK(!has_default_route("eth1", AF_INET6, routes));

	TEST_CHECK(!has_default_route("lo", AF_INET, routes));
	TEST_CHECK(!has_default_route("lo", AF_INET6, routes));

	TEST_CHECK(!has_default_route("eth2", AF_INET, routes));
	TEST_CHECK(!has_default_route("eth2", AF_INET6, routes));
}
