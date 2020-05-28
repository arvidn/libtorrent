/*

Copyright (c) 2005-2009, 2013, 2015-2017, 2019, Arvid Norberg
Copyright (c) 2016-2018, Alden Torres
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

#include "libtorrent/ip_filter.hpp"
#include "setup_transfer.hpp" // for addr()
#include <utility>

#include "test.hpp"
#include "settings.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"

/*

Currently this test only tests that the filter can handle
IPv4 addresses. Maybe it should be extended to IPv6 as well,
but the actual code is just a template, so it is probably
pretty safe to assume that as long as it works for IPv4 it
also works for IPv6.

*/

using namespace lt;

template <class T>
void test_rules_invariant(std::vector<ip_range<T>> const& r, ip_filter const& f)
{
	TEST_CHECK(!r.empty());
	if (r.empty()) return;

	if (sizeof(r.front().first) == sizeof(address_v4))
	{
		TEST_CHECK(r.front().first == addr("0.0.0.0"));
		TEST_CHECK(r.back().last == addr("255.255.255.255"));
	}
	else
	{
		TEST_CHECK(r.front().first == addr("::0"));
		TEST_CHECK(r.back().last == addr("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
	}

	for (auto i(r.begin()), j(std::next(r.begin()))
		, end(r.end()); j != end; ++j, ++i)
	{
		TEST_EQUAL(f.access(i->last), i->flags);
		TEST_EQUAL(f.access(j->first), j->flags);
		TEST_CHECK(aux::plus_one(i->last.to_bytes()) == j->first.to_bytes());
	}
}

TORRENT_TEST(session_get_ip_filter)
{
	session ses(settings());
	ip_filter const& ipf = ses.get_ip_filter();
	TEST_EQUAL(std::get<0>(ipf.export_filter()).size(), 1);
}

std::vector<ip_range<address_v4>> const expected1 =
{
	{addr4("0.0.0.0"), addr4("0.255.255.255"), 0}
	, {addr4("1.0.0.0"), addr4("3.0.0.0"), ip_filter::blocked}
	, {addr4("3.0.0.1"), addr4("255.255.255.255"), 0}
};

// **** test joining of ranges at the end ****
TORRENT_TEST(joining_ranges_at_end)
{
	ip_filter f;
	f.add_rule(addr("1.0.0.0"), addr("2.0.0.0"), ip_filter::blocked);
	f.add_rule(addr("2.0.0.1"), addr("3.0.0.0"), ip_filter::blocked);

	auto const range = std::get<0>(f.export_filter());
	test_rules_invariant(range, f);

	TEST_CHECK(range == expected1);
}

// **** test joining of ranges at the start ****
TORRENT_TEST(joining_ranges_at_start)
{
	ip_filter f;
	f.add_rule(addr("2.0.0.1"), addr("3.0.0.0"), ip_filter::blocked);
	f.add_rule(addr("1.0.0.0"), addr("2.0.0.0"), ip_filter::blocked);

	auto const range = std::get<0>(f.export_filter());
	test_rules_invariant(range, f);

	TEST_CHECK(range == expected1);
}

// **** test joining of overlapping ranges at the start ****
TORRENT_TEST(joining_overlapping_ranges_at_start)
{
	ip_filter f;
	f.add_rule(addr("2.0.0.1"), addr("3.0.0.0"), ip_filter::blocked);
	f.add_rule(addr("1.0.0.0"), addr("2.4.0.0"), ip_filter::blocked);

	auto const range = std::get<0>(f.export_filter());
	test_rules_invariant(range, f);

	TEST_CHECK(range == expected1);
}

// **** test joining of overlapping ranges at the end ****
TORRENT_TEST(joining_overlapping_ranges_at_end)
{
	ip_filter f;
	f.add_rule(addr("1.0.0.0"), addr("2.4.0.0"), ip_filter::blocked);
	f.add_rule(addr("2.0.0.1"), addr("3.0.0.0"), ip_filter::blocked);

	auto const range = std::get<0>(f.export_filter());
	test_rules_invariant(range, f);

	TEST_CHECK(range == expected1);
}

// **** test joining of multiple overlapping ranges 1 ****
TORRENT_TEST(joining_multiple_overlapping_ranges_1)
{
	ip_filter f;
	f.add_rule(addr("1.0.0.0"), addr("2.0.0.0"), ip_filter::blocked);
	f.add_rule(addr("3.0.0.0"), addr("4.0.0.0"), ip_filter::blocked);
	f.add_rule(addr("5.0.0.0"), addr("6.0.0.0"), ip_filter::blocked);
	f.add_rule(addr("7.0.0.0"), addr("8.0.0.0"), ip_filter::blocked);

	f.add_rule(addr("1.0.1.0"), addr("9.0.0.0"), ip_filter::blocked);

	auto const range = std::get<0>(f.export_filter());
	test_rules_invariant(range, f);

	std::vector<ip_range<address_v4>> const expected =
	{
		{addr4("0.0.0.0"), addr4("0.255.255.255"), 0}
		, {addr4("1.0.0.0"), addr4("9.0.0.0"), ip_filter::blocked}
		, {addr4("9.0.0.1"), addr4("255.255.255.255"), 0}
	};
	TEST_CHECK(range == expected);
}

// **** test joining of multiple overlapping ranges 2 ****
TORRENT_TEST(joining_multiple_overlapping_ranges_2)
{
	ip_filter f;
	f.add_rule(addr("1.0.0.0"), addr("2.0.0.0"), ip_filter::blocked);
	f.add_rule(addr("3.0.0.0"), addr("4.0.0.0"), ip_filter::blocked);
	f.add_rule(addr("5.0.0.0"), addr("6.0.0.0"), ip_filter::blocked);
	f.add_rule(addr("7.0.0.0"), addr("8.0.0.0"), ip_filter::blocked);

	f.add_rule(addr("0.0.1.0"), addr("7.0.4.0"), ip_filter::blocked);

	auto const range = std::get<0>(f.export_filter());
	test_rules_invariant(range, f);

	std::vector<ip_range<address_v4>> const expected =
	{
		{addr4("0.0.0.0"), addr4("0.0.0.255"), 0}
		, {addr4("0.0.1.0"), addr4("8.0.0.0"), ip_filter::blocked}
		, {addr4("8.0.0.1"), addr4("255.255.255.255"), 0}
	};

	TEST_CHECK(range == expected);
}

// **** test IPv6 ****
TORRENT_TEST(ipv6)
{
	std::vector<ip_range<address_v6>> const expected2 =
	{
		{addr6("::0"), addr6("0:ffff:ffff:ffff:ffff:ffff:ffff:ffff"), 0}
		, {addr6("1::"), addr6("3::"), ip_filter::blocked}
		, {addr6("3::1"), addr6("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"), 0}
	};

	ip_filter f;
	f.add_rule(addr("2::1"), addr("3::"), ip_filter::blocked);
	f.add_rule(addr("1::"), addr("2::"), ip_filter::blocked);

	std::vector<ip_range<address_v6>> rangev6;
	rangev6 = std::get<1>(f.export_filter());
	test_rules_invariant(rangev6, f);

	TEST_CHECK(rangev6 == expected2);
}

TORRENT_TEST(default_empty)
{
	{
		ip_filter f;
		TEST_CHECK(f.empty());

		f.add_rule(addr("1::"), addr("2::"), ip_filter::blocked);
		TEST_CHECK(!f.empty());
	}

	{
		ip_filter f;
		f.add_rule(addr("0.0.1.0"), addr("7.0.4.0"), ip_filter::blocked);
		TEST_CHECK(!f.empty());
	}

	{
		ip_filter f;
		f.add_rule(addr("0.0.1.0"), addr("7.0.4.0"), 0);
		TEST_CHECK(f.empty());
	}
}

TORRENT_TEST(port_filter)
{
	port_filter pf;

	// default constructed port filter should allow any port
	TEST_CHECK(pf.access(0) == 0);
	TEST_CHECK(pf.access(65535) == 0);
	TEST_CHECK(pf.access(6881) == 0);

	// block port 100 - 300
	pf.add_rule(100, 300, port_filter::blocked);

	TEST_CHECK(pf.access(0) == 0);
	TEST_CHECK(pf.access(99) == 0);
	TEST_CHECK(pf.access(100) == port_filter::blocked);
	TEST_CHECK(pf.access(150) == port_filter::blocked);
	TEST_CHECK(pf.access(300) == port_filter::blocked);
	TEST_CHECK(pf.access(301) == 0);
	TEST_CHECK(pf.access(6881) == 0);
	TEST_CHECK(pf.access(65535) == 0);
}

