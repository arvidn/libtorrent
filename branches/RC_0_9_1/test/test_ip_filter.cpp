#include "libtorrent/ip_filter.hpp"
#include <boost/utility.hpp>

#include "test.hpp"

using namespace libtorrent;

bool compare(ip_filter::ip_range const& lhs
	, ip_filter::ip_range const& rhs)
{
	return lhs.first == rhs.first
		&& lhs.last == rhs.last
		&& lhs.flags == rhs.flags;
}

void test_rules_invariant(std::vector<ip_filter::ip_range> const& r, ip_filter const& f)
{
	typedef std::vector<ip_filter::ip_range>::const_iterator iterator;
	TEST_CHECK(!r.empty());
	if (r.empty()) return;

	TEST_CHECK(r.front().first == address(0,0,0,0,0));
	TEST_CHECK(r.back().last == address(255,255,255,255,0));
	
	iterator i = r.begin();
	iterator j = boost::next(i);
	for (iterator i(r.begin()), j(boost::next(r.begin()))
		, end(r.end()); j != end; ++j, ++i)
	{
		TEST_CHECK(f.access(i->last) == i->flags);
		TEST_CHECK(f.access(j->first) == j->flags);
		TEST_CHECK(i->last.ip() + 1 == j->first.ip());
	}
}

int test_main()
{
	using namespace libtorrent;
	std::vector<ip_filter::ip_range> range;

	// **** test joining of ranges at the end ****
	ip_filter::ip_range expected1[] =
	{
		{address(0,0,0,0,0), address(0,255,255,255,0), 0}
		, {address(1,0,0,0,0), address(3,0,0,0,0), ip_filter::blocked}
		, {address(3,0,0,1,0), address(255,255,255,255,0), 0}
	};
	
	{
		ip_filter f;
		f.add_rule(address(1,0,0,0,0), address(2,0,0,0,0), ip_filter::blocked);
		f.add_rule(address(2,0,0,1,0), address(3,0,0,0,0), ip_filter::blocked);

		range = f.export_filter();
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected1, &compare));
	}
	
	// **** test joining of ranges at the start ****

	{
		ip_filter f;
		f.add_rule(address(2,0,0,1,0), address(3,0,0,0,0), ip_filter::blocked);
		f.add_rule(address(1,0,0,0,0), address(2,0,0,0,0), ip_filter::blocked);

		range = f.export_filter();
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected1, &compare));
	}	


	// **** test joining of overlapping ranges at the start ****

	{
		ip_filter f;
		f.add_rule(address(2,0,0,1,0), address(3,0,0,0,0), ip_filter::blocked);
		f.add_rule(address(1,0,0,0,0), address(2,4,0,0,0), ip_filter::blocked);

		range = f.export_filter();
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected1, &compare));
	}	


	// **** test joining of overlapping ranges at the end ****

	{
		ip_filter f;
		f.add_rule(address(1,0,0,0,0), address(2,4,0,0,0), ip_filter::blocked);
		f.add_rule(address(2,0,0,1,0), address(3,0,0,0,0), ip_filter::blocked);

		range = f.export_filter();
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		TEST_CHECK(std::equal(range.begin(), range.end(), expected1, &compare));
	}	


	// **** test joining of multiple overlapping ranges 1 ****

	{
		ip_filter f;
		f.add_rule(address(1,0,0,0,0), address(2,0,0,0,0), ip_filter::blocked);
		f.add_rule(address(3,0,0,0,0), address(4,0,0,0,0), ip_filter::blocked);
		f.add_rule(address(5,0,0,0,0), address(6,0,0,0,0), ip_filter::blocked);
		f.add_rule(address(7,0,0,0,0), address(8,0,0,0,0), ip_filter::blocked);

		f.add_rule(address(1,0,1,0,0), address(9,0,0,0,0), ip_filter::blocked);
		
		range = f.export_filter();
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		ip_filter::ip_range expected[] =
		{
			{address(0,0,0,0,0), address(0,255,255,255,0), 0}
			, {address(1,0,0,0,0), address(9,0,0,0,0), ip_filter::blocked}
			, {address(9,0,0,1,0), address(255,255,255,255,0), 0}
		};
	
		TEST_CHECK(std::equal(range.begin(), range.end(), expected, &compare));
	}	

	// **** test joining of multiple overlapping ranges 2 ****

	{
		ip_filter f;
		f.add_rule(address(1,0,0,0,0), address(2,0,0,0,0), ip_filter::blocked);
		f.add_rule(address(3,0,0,0,0), address(4,0,0,0,0), ip_filter::blocked);
		f.add_rule(address(5,0,0,0,0), address(6,0,0,0,0), ip_filter::blocked);
		f.add_rule(address(7,0,0,0,0), address(8,0,0,0,0), ip_filter::blocked);

		f.add_rule(address(0,0,1,0,0), address(7,0,4,0,0), ip_filter::blocked);
		
		range = f.export_filter();
		test_rules_invariant(range, f);

		TEST_CHECK(range.size() == 3);
		ip_filter::ip_range expected[] =
		{
			{address(0,0,0,0,0), address(0,0,0,255,0), 0}
			, {address(0,0,1,0,0), address(8,0,0,0,0), ip_filter::blocked}
			, {address(8,0,0,1,0), address(255,255,255,255,0), 0}
		};
	
		TEST_CHECK(std::equal(range.begin(), range.end(), expected, &compare));
	}	

	return 0;
}

