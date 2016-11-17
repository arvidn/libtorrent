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

using libtorrent::aux::stack_allocator;

TORRENT_TEST(copy_string)
{
	stack_allocator a;
	int const idx1 = a.copy_string("testing");

	// attempt to trigger a reallocation
	a.allocate(100000);

	int const idx2 = a.copy_string(std::string("foobar"));


	TEST_CHECK(strcmp(a.ptr(idx1), "testing") == 0);
	TEST_CHECK(strcmp(a.ptr(idx2), "foobar") == 0);
}

TORRENT_TEST(copy_buffer)
{
	stack_allocator a;
	int const idx1 = a.copy_buffer("testing", 8);

	// attempt to trigger a reallocation
	a.allocate(100000);

	TEST_CHECK(strcmp(a.ptr(idx1), "testing") == 0);

	// attempt zero size allocation
	int const idx2 = a.copy_buffer("nothing", 0);
	TEST_CHECK(idx2 == -1);

	// attempt to get a pointer after zero allocation
	char* ptr = a.ptr(idx2);
	TEST_CHECK(ptr == NULL);
}

TORRENT_TEST(allocate)
{
	stack_allocator a;
	int const idx1 = a.allocate(100);
	char* ptr = a.ptr(idx1);
	for (int i = 0; i < 100; ++i)
		ptr[i] = char(i % 256);

	// attempt to trigger a reallocation
	a.allocate(100000);

	ptr = a.ptr(idx1);
	for (int i = 0; i < 100; ++i)
		TEST_CHECK(ptr[i] == char(i % 256));

	// attempt zero size allocation
	int const idx2 = a.allocate(0);
	TEST_CHECK(idx2 == -1);

	// attempt to get a pointer after zero allocation
	ptr = a.ptr(idx2);
	TEST_CHECK(ptr == NULL);
}

TORRENT_TEST(swap)
{
	stack_allocator a1;
	stack_allocator a2;

	int const idx1 = a1.copy_string("testing");
	int const idx2 = a2.copy_string("foobar");

	a1.swap(a2);

	TEST_CHECK(strcmp(a1.ptr(idx2), "foobar") == 0);
	TEST_CHECK(strcmp(a2.ptr(idx1), "testing") == 0);
}

