/*

Copyright (c) 2017, Arvid Norberg
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

#include "libtorrent/aux_/mmap.hpp"
#include <fstream>

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE

using namespace libtorrent;
using namespace libtorrent::aux;

std::vector<char> filled_buffer(int const size)
{
	std::vector<char> buf;
	buf.resize(size);
	for (int i = 0; i < size; ++i)
		buf[i] = static_cast<char>(i);
	return buf;
}

TORRENT_TEST(mmap_read)
{
	std::vector<char> buf = filled_buffer(100);

	{
		std::ofstream file("test_file1", std::ios::binary);
		file.write(buf.data(), buf.size());
	}

	auto m = std::make_shared<file_mapping>(aux::file_handle("test_file1", 100, open_mode::read_only)
		, open_mode::read_only, 100);

	file_view v = m->view();
	auto range = v.range();

	for (int i = 0; i < 100; ++i)
		TEST_EQUAL(static_cast<int>(range[i]), static_cast<int>(buf[i]));
}

TORRENT_TEST(mmap_write)
{
	std::vector<char> buf = filled_buffer(100);

	{
		auto m = std::make_shared<file_mapping>(aux::file_handle("test_file2", 100
				, open_mode::write | open_mode::truncate)
			, open_mode::write | open_mode::truncate, 100);

		file_view v = m->view();
		auto range = v.range();

		for (int i = 0; i < 100; ++i)
		{
			range[i] = buf[i];
		}
	}

	std::ifstream file("test_file2", std::ios_base::binary);
	std::vector<char> buf2;
	buf2.resize(100);
	file.read(buf2.data(), buf2.size());
	TEST_EQUAL(file.gcount(), 100);

	for (int i = 0; i < 100; ++i)
		TEST_EQUAL(static_cast<int>(buf2[i]), static_cast<int>(buf[i]));
}

#else

TORRENT_TEST(dummy) {}

#endif

