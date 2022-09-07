/*

Copyright (c) 2016, 2019-2020, Arvid Norberg
Copyright (c) 2019, Steven Siloti
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

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/range/combine.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE

using namespace lt;
using namespace lt::aux;

namespace {

std::vector<char> filled_buffer(std::ptrdiff_t const size)
{
	std::vector<char> buf;
	buf.resize(static_cast<std::size_t>(size));
	std::uint8_t cnt = 0;
	std::generate(buf.begin(), buf.end()
		, [&cnt](){ return static_cast<char>(cnt++); });
	return buf;
}
}

TORRENT_TEST(mmap_read)
{
	std::vector<char> buf = filled_buffer(1024 * 1024);

	{
		std::ofstream file("test_file1", std::ios::binary);
		file.write(buf.data(), std::streamsize(buf.size()));
	}

	auto m = std::make_shared<file_mapping>(aux::file_handle("test_file1"
		, std::int64_t(buf.size()), open_mode::read_only)
		, open_mode::read_only, buf.size()
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		, std::make_shared<std::mutex>()
#endif
		);

	TORRENT_ASSERT(m->has_memory_map());

	for (auto const i : boost::combine(m->range(), buf))
	{
		if (boost::get<0>(i) != boost::get<1>(i)) TEST_ERROR("mmap view mismatching");
	}
}

TORRENT_TEST(mmap_write)
{
	std::vector<char> buf = filled_buffer(1024 * 1024);

	{
		auto m = std::make_shared<file_mapping>(aux::file_handle("test_file2"
				, std::int64_t(buf.size())
				, open_mode::write | open_mode::truncate)
			, open_mode::write | open_mode::truncate, buf.size()
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
			, std::make_shared<std::mutex>()
#endif
			);

		auto range = m->range();

		std::copy(buf.begin(), buf.end(), range.begin());
	}

	std::ifstream file("test_file2", std::ios_base::binary);
	std::vector<char> buf2;
	buf2.resize(buf.size());
	file.read(buf2.data(), std::streamsize(buf2.size()));
	TEST_EQUAL(file.gcount(), std::streamsize(buf.size()));

	for (auto const i : boost::combine(buf2, buf))
	{
		if (boost::get<0>(i) != boost::get<1>(i)) TEST_ERROR("mmap view mismatching");
	}
}

#else

TORRENT_TEST(dummy) {}

#endif

