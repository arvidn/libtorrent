/*

Copyright (c) 2015, 2017, 2019, Arvid Norberg
Copyright (c) 2017, Alden Torres
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
	TEST_CHECK(is_local(make_address("192.168.0.1", ec)));
	TEST_CHECK(!ec);
	TEST_CHECK(is_local(make_address("10.1.1.56", ec)));
	TEST_CHECK(!ec);
	TEST_CHECK(!is_local(make_address("14.14.251.63", ec)));
	TEST_CHECK(!ec);
}

TORRENT_TEST(is_loopback)
{
	error_code ec;
	TEST_CHECK(is_loopback(make_address("127.0.0.1", ec)));
	TEST_CHECK(!ec);
	if (supports_ipv6())
	{
		TEST_CHECK(is_loopback(make_address("::1", ec)));
		TEST_CHECK(!ec);
	}
}

TORRENT_TEST(is_any)
{
	TEST_CHECK(is_any(address_v4::any()));
	error_code ec;
	TEST_CHECK(!is_any(make_address("31.53.21.64", ec)));
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
		make_address("10.0.1.176", ec),
		make_address("10.0.1.176", ec),
		make_address("255.255.255.0", ec)));
	TEST_CHECK(!ec);

	TEST_CHECK(match_addr_mask(
		make_address("10.0.1.3", ec),
		make_address("10.0.3.3", ec),
		make_address("255.255.0.0", ec)));
	TEST_CHECK(!ec);

	TEST_CHECK(!match_addr_mask(
		make_address("10.0.1.3", ec),
		make_address("10.1.3.3", ec),
		make_address("255.255.0.0", ec)));
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
