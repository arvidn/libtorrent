/*

Copyright (c) 2021, Arvid Norberg
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
#include "test_utils.hpp"
#include "libtorrent/aux_/apply_pad_files.hpp"

using namespace lt;

namespace {

struct piece_byte
{
	piece_index_t piece;
	std::int64_t bytes;
};

struct expect_calls
{
	expect_calls(std::vector<piece_byte> const& calls)
		: m_calls(calls)
	{}

	void operator()(piece_index_t const piece, int const bytes)
	{
		TEST_CHECK(!m_calls.empty());
		if (m_calls.empty()) return;
		auto const expected = m_calls.front();
		m_calls.erase(m_calls.begin());
		TEST_EQUAL(piece, expected.piece);
		TEST_EQUAL(bytes, expected.bytes);
		m_total += bytes;
	}

	~expect_calls()
	{
		TEST_CHECK(m_calls.empty());
	}

	std::int64_t total_pad() const { return m_total; }

private:
	std::int64_t m_total = 0;
	std::vector<piece_byte> m_calls;
};

}

TORRENT_TEST(simple)
{
	auto const fs = make_files({{0x3ff0, false}, {0x10, true}}, 0x4000);
	expect_calls c({{0_piece, 0x10}});
	aux::apply_pad_files(fs, c);
	TEST_EQUAL(c.total_pad(), 0x10);
}

TORRENT_TEST(irregular_last_piece)
{
	auto const fs = make_files({{0x3ff0, false}, {0x20, true}}, 0x4000);
	expect_calls c({{1_piece, 0x10}, {0_piece, 0x10}});
	aux::apply_pad_files(fs, c);
	TEST_EQUAL(c.total_pad(), 0x20);
}

TORRENT_TEST(full_piece)
{
	auto const fs = make_files({{0x4000, false}, {0x4000, true}}, 0x4000);
	expect_calls c({{1_piece, 0x4000}});
	aux::apply_pad_files(fs, c);
	TEST_EQUAL(c.total_pad(), 0x4000);
}

TORRENT_TEST(1_byte_pad)
{
	auto const fs = make_files({{0x3fff, false}, {0x1, true}}, 0x4000);
	expect_calls c({{0_piece, 0x1}});
	aux::apply_pad_files(fs, c);
	TEST_EQUAL(c.total_pad(), 0x1);
}

TORRENT_TEST(span_multiple_pieces)
{
	auto const fs = make_files({{0x8001, false}, {0x7fff, true}}, 0x4000);
	expect_calls c({{3_piece, 0x4000}, {2_piece, 0x3fff}});
	aux::apply_pad_files(fs, c);
	TEST_EQUAL(c.total_pad(), 0x7fff);
}

TORRENT_TEST(span_multiple_full_pieces)
{
	auto const fs = make_files({{0x8000, false}, {0x8000, true}}, 0x4000);
	expect_calls c({{3_piece, 0x4000}, {2_piece, 0x4000}});
	aux::apply_pad_files(fs, c);
	TEST_EQUAL(c.total_pad(), 0x8000);
}

TORRENT_TEST(small_pieces)
{
	auto const fs = make_files({{0x2001, false}, {0x1fff, true}}, 0x1000);
	expect_calls c({{3_piece, 0x1000}, {2_piece, 0xfff}});
	aux::apply_pad_files(fs, c);
	TEST_EQUAL(c.total_pad(), 0x1fff);
}

TORRENT_TEST(smalll_piece_1_byte_pad)
{
	auto const fs = make_files({{0xfff, false}, {0x1, true}}, 0x1000);
	expect_calls c({{0_piece, 0x1}});
	aux::apply_pad_files(fs, c);
	TEST_EQUAL(c.total_pad(), 0x1);
}

TORRENT_TEST(back_to_back_pads)
{
	// In this scenario, the first pad file is invalid. It doesn't align the
	// next file to a piece boundary, nor is it the last file. It will be
	// treated like a normal file by the piece picker
	auto const fs = make_files({{0x3ff0, false}, {0x8, true}, {0x8, true}}, 0x4000);
	expect_calls c({{0_piece, 0x8}});
	aux::apply_pad_files(fs, c);
	TEST_EQUAL(c.total_pad(), 0x8);
}

TORRENT_TEST(large_pad_file)
{
	auto const fs = make_files({{0x4001, false}, {0x100003fff, true}}, 0x4000);
	piece_index_t expected_piece(fs.num_pieces() - 1);
	int num_calls = 0;
	aux::apply_pad_files(fs, [&](piece_index_t const piece, int const bytes)
	{
		TEST_EQUAL(piece, expected_piece);
		if (piece == 1_piece)
		{
			TEST_EQUAL(bytes, 0x3fff);
		}
		else
		{
			TEST_EQUAL(bytes, 0x4000);
			--expected_piece;
		}
		++num_calls;
	});
	TEST_EQUAL(num_calls, 262145);
	TEST_EQUAL(expected_piece, 1_piece);
}

