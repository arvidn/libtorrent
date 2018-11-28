/*

Copyright (c) 2016, Arvid Norberg
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
#include "libtorrent/stack_allocator.hpp"
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

	// attempt to get a pointer after zero allocation
	char* ptr = a.ptr(idx2);
	TEST_CHECK(ptr == nullptr);
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

	// attempt to get a pointer after zero allocation
	ptr = a.ptr(idx2);
	TEST_CHECK(ptr == nullptr);
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
