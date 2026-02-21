/*

Copyright (c) 2016-2022, Arvid Norberg
Copyright (c) 2016, Mokhtar Naamani
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/stack_allocator.hpp"
#include "libtorrent/string_view.hpp"
#include <cstdarg> // for va_list, va_start, va_end

using lt::aux::stack_allocator;
using lt::aux::allocation_slot;
using namespace lt::literals;

TORRENT_TEST(copy_string)
{
	stack_allocator a;
	allocation_slot const idx1 = a.copy_string("testing");

	// attempt to trigger a reallocation
	a.allocate(100000);

	allocation_slot const idx2 = a.copy_string(std::string("foobar"));

	TEST_CHECK(a.ptr(idx1) == "testing"_sv);
	TEST_CHECK(a.ptr(idx2) == "foobar"_sv);
}

TORRENT_TEST(copy_buffer)
{
	stack_allocator a;
	allocation_slot const idx1 = a.copy_buffer(lt::span<char const>("testing"));

	// attempt to trigger a reallocation
	a.allocate(100000);

	TEST_CHECK(a.ptr(idx1) == "testing"_sv);

	// attempt zero size allocation
	allocation_slot const idx2 = a.copy_buffer({});
	TEST_CHECK(!idx2.is_valid());

	// attempt to get a pointer after zero allocation
	char* ptr = a.ptr(idx2);
	TEST_EQUAL(std::strlen(ptr), 0);
}

TORRENT_TEST(allocate)
{
	stack_allocator a;
	allocation_slot const idx1 = a.allocate(100);
	char* ptr = a.ptr(idx1);
	for (int i = 0; i < 100; ++i)
		ptr[i] = char(i % 256);

	// attempt to trigger a reallocation
	a.allocate(100000);

	ptr = a.ptr(idx1);
	for (int i = 0; i < 100; ++i)
		TEST_CHECK(ptr[i] == char(i % 256));

	// attempt zero size allocation
	allocation_slot const idx2 = a.allocate(0);
	TEST_CHECK(!idx2.is_valid());

	// attempt to get a pointer after zero allocation
	ptr = a.ptr(idx2);
	TEST_EQUAL(std::strlen(ptr), 0);
}

TORRENT_TEST(swap)
{
	stack_allocator a1;
	stack_allocator a2;

	allocation_slot const idx1 = a1.copy_string("testing");
	allocation_slot const idx2 = a2.copy_string("foobar");

	a1.swap(a2);

	TEST_CHECK(a1.ptr(idx2) == "foobar"_sv);
	TEST_CHECK(a2.ptr(idx1) == "testing"_sv);
}

namespace {

TORRENT_FORMAT(2,3)
allocation_slot format_string_helper(stack_allocator& stack, char const* fmt, ...)
{
		va_list v;
		va_start(v, fmt);
		auto const ret = stack.format_string(fmt, v);
		va_end(v);
		return ret;
}

}

TORRENT_TEST(format_string_long)
{
	stack_allocator a;
	std::string long_string;
	for (int i = 0; i < 1024; ++i) long_string += "foobar-";
	auto const idx = format_string_helper(a, "%s", long_string.c_str());

	TEST_EQUAL(a.ptr(idx), long_string);
}

TORRENT_TEST(format_string)
{
	stack_allocator a;
	auto const idx = format_string_helper(a, "%d", 10);

	TEST_EQUAL(a.ptr(idx), "10"_sv);
}

TORRENT_TEST(out_of_space)
{
	std::string long_string;
	for (int i = 0; i < 100; ++i) long_string += "foobar-";

	stack_allocator a;
	// fill up the memory
	try {
		for (int i = 0; i < std::numeric_limits<int>::max() / 1024; ++i)
		{
			a.allocate(1024);
		}
		a.allocate(512);
		a.allocate(256);
	}
	catch (std::bad_alloc const&)
	{
		// it's reasonable that some environments won't allocate 2 GiB of RAM
		// willy nilly, and fail. This happens on the windows runner on github
		// actions. Just ignore this test.
		return;
	}

	auto slot = a.allocate(500);
	TEST_CHECK(!slot.is_valid());
	TEST_EQUAL(std::strlen(a.ptr(slot)), 0);

	slot = a.copy_buffer(long_string);
	TEST_CHECK(!slot.is_valid());
	TEST_EQUAL(std::strlen(a.ptr(slot)), 0);

	slot = a.copy_string(long_string.c_str());
	TEST_CHECK(!slot.is_valid());
	TEST_EQUAL(std::strlen(a.ptr(slot)), 0);

	slot = a.copy_string(long_string);
	TEST_CHECK(!slot.is_valid());
	TEST_EQUAL(std::strlen(a.ptr(slot)), 0);

	slot = format_string_helper(a, "test: %s", long_string.c_str());
	TEST_CHECK(!slot.is_valid());
	TEST_EQUAL(std::strlen(a.ptr(slot)), 0);
}
