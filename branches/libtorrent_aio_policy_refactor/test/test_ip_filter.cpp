/*

Copyright (c) 2008, Arvid Norberg
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
#include <boost/utility.hpp>

#include "test.hpp"
#include "libtorrent/socket_io.hpp"

/*

Currently this test only tests that the filter can handle
IPv4 addresses. Maybe it should be extended to IPv6 as well,
but the actual code is just a template, so it is probably
pretty safe to assume that as long as it works for IPv4 it
also works for IPv6.
	
*/

using namespace libtorrent;

template <class Addr>
bool compare(ip_range<Addr> const& lhs
	, ip_range<Addr> const& rhs)
{
	return lhs.first == rhs.first
		&& lhs.last == rhs.last
		&& lhs.flags == rhs.flags;
}

#define IP(x) address::from_string(x, ec)
#define IP4(x) address_v4::from_string(x, ec)
#define IP6(x) address_v6::from_string(x, ec)

template <class T>
void test_rules_invariant(std::vector<ip_range<T> > const& r, ip_filter const& f)
{
	typedef typename std::vector<ip_range<T> >::const_iterator iterator;
	TEST_CHECK(!r.empty());
	if (r.empty()) return;

	error_code ec;
	if (sizeof(r.front().first) == sizeof(address_v4))
	{
		TEST_CHECK(r.front().first == IP("0.0.0.0"));
		TEST_CHECK(r.back().last == IP("255.255.255.255"));
	}
	else
	{
		TEST_CHECK(r.front().first == IP("::0"));
		TEST_CHECK(r.back().last == IP("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
	}
	
	iterator i = r.begin();
	iterator j = boost::next(i);
	for (iterator i(r.begin()), j(boost::next(r.begin()))
		, end(r.end()); j != end; ++j, ++i)
	{
		TEST_CHECK(f.access(i->last) == i->flags);
		TEST_CHECK(f.access(j->first) == j->flags);
		TEST_CHECK(detail::plus_one(i->last.to_bytes()) == j->first.to_bytes());
	}
}

int test_main()
{
	using namespace libtorrent;

	std::vector<ip_range<address_v4> > range;
	error_code ec;

	// **** test joining of ranges at the end ****
	ip_range<address_v4> expected1[] =
	{
		{IP4("0.0.0.0"), IP4("0.255.255.255"), 0}
		, {IP4("1.0.0.0"), IP4("3.0.0.0"), ip_filter::blocked}
		, {IP4("3.0.0.1"), IP4("255.255.255.255"), 0}
	};
	
	{
		ip_filter f;
		f.add_rule(IP("1.0.0.0"), IP("2.0.0.0"), ip_filter::blocked);
		f.add_rule(IP("2.0.0.1"), IP("3.0.0.0"), ip_filter::blocked);

#if TORRENT_USE_IPV6
		range = boost::get<0>(f.export_filter());
#else
		range = f.export_filter();
#endif
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected1, &compare<address_v4>));

	}
	
	// **** test joining of ranges at the start ****

	{
		ip_filter f;
		f.add_rule(IP("2.0.0.1"), IP("3.0.0.0"), ip_filter::blocked);
		f.add_rule(IP("1.0.0.0"), IP("2.0.0.0"), ip_filter::blocked);

#if TORRENT_USE_IPV6
		range = boost::get<0>(f.export_filter());
#else
		range = f.export_filter();
#endif
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected1, &compare<address_v4>));

	}	


	// **** test joining of overlapping ranges at the start ****

	{
		ip_filter f;
		f.add_rule(IP("2.0.0.1"), IP("3.0.0.0"), ip_filter::blocked);
		f.add_rule(IP("1.0.0.0"), IP("2.4.0.0"), ip_filter::blocked);

#if TORRENT_USE_IPV6
		range = boost::get<0>(f.export_filter());
#else
		range = f.export_filter();
#endif
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected1, &compare<address_v4>));

	}	


	// **** test joining of overlapping ranges at the end ****

	{
		ip_filter f;
		f.add_rule(IP("1.0.0.0"), IP("2.4.0.0"), ip_filter::blocked);
		f.add_rule(IP("2.0.0.1"), IP("3.0.0.0"), ip_filter::blocked);

#if TORRENT_USE_IPV6
		range = boost::get<0>(f.export_filter());
#else
		range = f.export_filter();
#endif
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected1, &compare<address_v4>));

	}	


	// **** test joining of multiple overlapping ranges 1 ****

	{
		ip_filter f;
		f.add_rule(IP("1.0.0.0"), IP("2.0.0.0"), ip_filter::blocked);
		f.add_rule(IP("3.0.0.0"), IP("4.0.0.0"), ip_filter::blocked);
		f.add_rule(IP("5.0.0.0"), IP("6.0.0.0"), ip_filter::blocked);
		f.add_rule(IP("7.0.0.0"), IP("8.0.0.0"), ip_filter::blocked);

		f.add_rule(IP("1.0.1.0"), IP("9.0.0.0"), ip_filter::blocked);
		
#if TORRENT_USE_IPV6
		range = boost::get<0>(f.export_filter());
#else
		range = f.export_filter();
#endif
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		ip_range<address_v4> expected[] =
		{
			{IP4("0.0.0.0"), IP4("0.255.255.255"), 0}
			, {IP4("1.0.0.0"), IP4("9.0.0.0"), ip_filter::blocked}
			, {IP4("9.0.0.1"), IP4("255.255.255.255"), 0}
		};
	
		TEST_CHECK(std::equal(range.begin(), range.end(), expected, &compare<address_v4>));

	}	

	// **** test joining of multiple overlapping ranges 2 ****

	{
		ip_filter f;
		f.add_rule(IP("1.0.0.0"), IP("2.0.0.0"), ip_filter::blocked);
		f.add_rule(IP("3.0.0.0"), IP("4.0.0.0"), ip_filter::blocked);
		f.add_rule(IP("5.0.0.0"), IP("6.0.0.0"), ip_filter::blocked);
		f.add_rule(IP("7.0.0.0"), IP("8.0.0.0"), ip_filter::blocked);

		f.add_rule(IP("0.0.1.0"), IP("7.0.4.0"), ip_filter::blocked);

#if TORRENT_USE_IPV6
		range = boost::get<0>(f.export_filter());
#else
		range = f.export_filter();
#endif
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		ip_range<address_v4> expected[] =
		{
			{IP4("0.0.0.0"), IP4("0.0.0.255"), 0}
			, {IP4("0.0.1.0"), IP4("8.0.0.0"), ip_filter::blocked}
			, {IP4("8.0.0.1"), IP4("255.255.255.255"), 0}
		};
	
		TEST_CHECK(std::equal(range.begin(), range.end(), expected, &compare<address_v4>));

	}

	// **** test IPv6 ****

#if TORRENT_USE_IPV6

	ip_range<address_v6> expected2[] =
	{
		{IP6("::0"), IP6("0:ffff:ffff:ffff:ffff:ffff:ffff:ffff"), 0}
		, {IP6("1::"), IP6("3::"), ip_filter::blocked}
		, {IP6("3::1"), IP6("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"), 0}
	};
	
	{
		ip_filter f;
		f.add_rule(IP("2::1"), IP("3::"), ip_filter::blocked);
		f.add_rule(IP("1::"), IP("2::"), ip_filter::blocked);

		std::vector<ip_range<address_v6> > range;
		range = boost::get<1>(f.export_filter());
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected2, &compare<address_v6>));

	}	
#endif

	port_filter pf;

	// default contructed port filter should allow any port
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

	return 0;
}

