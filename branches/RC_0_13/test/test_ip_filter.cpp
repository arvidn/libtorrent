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

void test_rules_invariant(std::vector<ip_range<address_v4> > const& r, ip_filter const& f)
{
	typedef std::vector<ip_range<address_v4> >::const_iterator iterator;
	TEST_CHECK(!r.empty());
	if (r.empty()) return;

	TEST_CHECK(r.front().first == address::from_string("0.0.0.0"));
	TEST_CHECK(r.back().last == address::from_string("255.255.255.255"));
	
	iterator i = r.begin();
	iterator j = boost::next(i);
	for (iterator i(r.begin()), j(boost::next(r.begin()))
		, end(r.end()); j != end; ++j, ++i)
	{
		TEST_CHECK(f.access(i->last) == i->flags);
		TEST_CHECK(f.access(j->first) == j->flags);
		TEST_CHECK(i->last.to_ulong() + 1 == j->first.to_ulong());
	}
}

int test_main()
{
	using namespace libtorrent;

	std::vector<ip_range<address_v4> > range;

	// **** test joining of ranges at the end ****
	ip_range<address_v4> expected1[] =
	{
		{address_v4::from_string("0.0.0.0"), address_v4::from_string("0.255.255.255"), 0}
		, {address_v4::from_string("1.0.0.0"), address_v4::from_string("3.0.0.0"), ip_filter::blocked}
		, {address_v4::from_string("3.0.0.1"), address_v4::from_string("255.255.255.255"), 0}
	};
	
	{
		ip_filter f;
		f.add_rule(address::from_string("1.0.0.0"), address::from_string("2.0.0.0"), ip_filter::blocked);
		f.add_rule(address::from_string("2.0.0.1"), address::from_string("3.0.0.0"), ip_filter::blocked);

		range = boost::get<0>(f.export_filter());
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected1, &compare<address_v4>));

	}
	
	// **** test joining of ranges at the start ****

	{
		ip_filter f;
		f.add_rule(address::from_string("2.0.0.1"), address::from_string("3.0.0.0"), ip_filter::blocked);
		f.add_rule(address::from_string("1.0.0.0"), address::from_string("2.0.0.0"), ip_filter::blocked);

		range = boost::get<0>(f.export_filter());
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected1, &compare<address_v4>));

	}	


	// **** test joining of overlapping ranges at the start ****

	{
		ip_filter f;
		f.add_rule(address::from_string("2.0.0.1"), address::from_string("3.0.0.0"), ip_filter::blocked);
		f.add_rule(address::from_string("1.0.0.0"), address::from_string("2.4.0.0"), ip_filter::blocked);

		range = boost::get<0>(f.export_filter());
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected1, &compare<address_v4>));

	}	


	// **** test joining of overlapping ranges at the end ****

	{
		ip_filter f;
		f.add_rule(address::from_string("1.0.0.0"), address::from_string("2.4.0.0"), ip_filter::blocked);
		f.add_rule(address::from_string("2.0.0.1"), address::from_string("3.0.0.0"), ip_filter::blocked);

		range = boost::get<0>(f.export_filter());
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected1, &compare<address_v4>));

	}	


	// **** test joining of multiple overlapping ranges 1 ****

	{
		ip_filter f;
		f.add_rule(address::from_string("1.0.0.0"), address::from_string("2.0.0.0"), ip_filter::blocked);
		f.add_rule(address::from_string("3.0.0.0"), address::from_string("4.0.0.0"), ip_filter::blocked);
		f.add_rule(address::from_string("5.0.0.0"), address::from_string("6.0.0.0"), ip_filter::blocked);
		f.add_rule(address::from_string("7.0.0.0"), address::from_string("8.0.0.0"), ip_filter::blocked);

		f.add_rule(address::from_string("1.0.1.0"), address::from_string("9.0.0.0"), ip_filter::blocked);
		
		range = boost::get<0>(f.export_filter());
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		ip_range<address_v4> expected[] =
		{
			{address_v4::from_string("0.0.0.0"), address_v4::from_string("0.255.255.255"), 0}
			, {address_v4::from_string("1.0.0.0"), address_v4::from_string("9.0.0.0"), ip_filter::blocked}
			, {address_v4::from_string("9.0.0.1"), address_v4::from_string("255.255.255.255"), 0}
		};
	
		TEST_CHECK(std::equal(range.begin(), range.end(), expected, &compare<address_v4>));

	}	

	// **** test joining of multiple overlapping ranges 2 ****

	{
		ip_filter f;
		f.add_rule(address::from_string("1.0.0.0"), address::from_string("2.0.0.0"), ip_filter::blocked);
		f.add_rule(address::from_string("3.0.0.0"), address::from_string("4.0.0.0"), ip_filter::blocked);
		f.add_rule(address::from_string("5.0.0.0"), address::from_string("6.0.0.0"), ip_filter::blocked);
		f.add_rule(address::from_string("7.0.0.0"), address::from_string("8.0.0.0"), ip_filter::blocked);

		f.add_rule(address::from_string("0.0.1.0"), address::from_string("7.0.4.0"), ip_filter::blocked);

		range = boost::get<0>(f.export_filter());
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		ip_range<address_v4> expected[] =
		{
			{address_v4::from_string("0.0.0.0"), address_v4::from_string("0.0.0.255"), 0}
			, {address_v4::from_string("0.0.1.0"), address_v4::from_string("8.0.0.0"), ip_filter::blocked}
			, {address_v4::from_string("8.0.0.1"), address_v4::from_string("255.255.255.255"), 0}
		};
	
		TEST_CHECK(std::equal(range.begin(), range.end(), expected, &compare<address_v4>));

	}	

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

