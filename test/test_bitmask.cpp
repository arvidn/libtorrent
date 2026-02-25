/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/bitmask.hpp"
#include "test.hpp"

using namespace libtorrent::aux;

TORRENT_TEST(bitmask)
{
	enum class options {
		one = 1 << 0,
		two = 1 << 1,
		three = 1 << 2,
		four = 1 << 3,
		five = 1 << 4
	};

	bitmask<options> mask;
	TEST_CHECK(!mask);

	// Set and test individual bits
	mask |= options::one;
	TEST_CHECK(mask.test(options::one));
	TEST_CHECK(!mask.test(options::two));

	mask |= options::two;
	TEST_CHECK(mask.test(options::two));

	// bitwise AND operation
	bitmask<options> mask2 = mask & options::one;
	TEST_CHECK(mask2.test(options::one));
	TEST_CHECK(!mask2.test(options::two));

	// bitwise OR operation
	bitmask<options> mask3 = mask | options::three;
	TEST_CHECK(mask3.test(options::three));

	// bitwise XOR operation
	bitmask<options> mask4 = mask ^ options::two;
	TEST_CHECK(mask4.test(options::one));
	TEST_CHECK(!mask4.test(options::two));

	// unset functionality
	mask.unset(options::one);
	TEST_CHECK(!mask.test(options::one));
	TEST_CHECK(mask.test(options::two));

	// bitwise NOT operation
	bitmask<options> mask5 = ~mask;
	TEST_CHECK(!mask5.test(options::two));
	TEST_CHECK(mask5.test(options::three)); // Assuming options::three was not set

	TEST_CHECK(mask.raw() == static_cast<std::underlying_type_t<options>>(options::two));

	// explicit bool conversion
	TEST_CHECK(static_cast<bool>(mask));

	// constructor with underlying type
	bitmask<options> mask6(static_cast<std::underlying_type_t<options>>(0));
	TEST_CHECK(!mask6);

	// operator==
	bitmask<options> mask7 = options::two;
	bitmask<options> mask8 = options::two;
	TEST_CHECK(mask7 == mask8);

	// operator^=
	mask7 ^= options::two;
	TEST_CHECK(!mask7.test(options::two));
	mask7 ^= options::three;
	TEST_CHECK(mask7.test(options::three));

	// operator&=
	mask7 &= options::three;
	TEST_CHECK(mask7.test(options::three));
	mask7 &= options::two;
	TEST_CHECK(!mask7.test(options::two));

	// operator|=
	mask7 |= options::four;
	TEST_CHECK(mask7.test(options::four));

	// unset function
	mask7.unset(options::four);
	TEST_CHECK(!mask7.test(options::four));
}
