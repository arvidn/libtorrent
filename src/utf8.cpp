/*

Copyright (c) 2017, Andrei Kurushin
Copyright (c) 2012, 2015-2017, 2019-2020, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"

#include <iterator>
#include <stdexcept>
#include "libtorrent/aux_/utf8.hpp"
#include "libtorrent/assert.hpp"

namespace lt::aux {

namespace {
	// return the number of bytes in the UTF-8 sequence starting with this
	// character. Returns 0 if the lead by is invalid
	int utf8_sequence_length(char const c)
	{
		auto const b = static_cast<std::uint8_t>(c);
		if (b < 0b10000000) return 1;
		if ((b >> 5) == 0b110) return 2;
		if ((b >> 4) == 0b1110) return 3;
		if ((b >> 3) == 0b11110) return 4;
		// this is an invalid prefix, but we still parse it to skip this many
		// bytes
		if ((b >> 2) == 0b111110) return 5;
		return 0;
	}

} // anonymous namespace

	std::int32_t const max_codepoint = 0x10ffff;

	void append_utf8_codepoint(std::string& ret, std::int32_t codepoint)
	{
		std::int32_t const surrogate_start = 0xd800;
		std::int32_t const surrogate_end = 0xdfff;

		if (codepoint >= surrogate_start
			&& codepoint <= surrogate_end)
			codepoint = '_';

		if (codepoint > max_codepoint)
			codepoint = '_';

		int seq_len = 0;
		if (codepoint < 0x80) seq_len = 1;
		else if (codepoint < 0x800) seq_len = 2;
		else if (codepoint < 0x10000) seq_len = 3;
		else seq_len = 4;

		switch (seq_len)
		{
			case 1:
				ret.push_back(static_cast<char>(codepoint));
				break;
			case 2:
				ret.push_back(static_cast<char>(0b11000000 | (codepoint >> 6)));
				break;
			case 3:
				ret.push_back(static_cast<char>(0b11100000 | (codepoint >> 12)));
				break;
			case 4:
				ret.push_back(static_cast<char>(0b11110000 | (codepoint >> 18)));
				break;
		}

		for (int i = seq_len - 2; i >= 0; --i)
			ret.push_back(static_cast<char>(0b10000000 | ((codepoint >> (6 * i)) & 0b111111)));
	}

	// returns the unicode codepoint and the number of bytes of the utf8 sequence
	// that was parsed. The codepoint is -1 if it's invalid
	std::pair<std::int32_t, int> parse_utf8_codepoint(string_view str)
	{
		TORRENT_ASSERT(!str.empty());
		if (str.empty()) return std::make_pair(-1, 0);

		int const sequence_len = utf8_sequence_length(str[0]);

		// this is likely the most common case
		if (sequence_len == 1) return std::make_pair(std::int32_t(str[0]), sequence_len);

		// if we find an invalid sequence length, skip one byte
		if (sequence_len == 0)
			return std::make_pair(-1, 1);

		if (sequence_len > 4)
			return std::make_pair(-1, sequence_len);

		if (sequence_len > int(str.size()))
			return std::make_pair(-1, static_cast<int>(str.size()));

		std::int32_t ch = 0;
		// first byte
		switch (sequence_len)
		{
			case 1:
				ch = str[0] & 0b01111111;
				break;
			case 2:
				ch = str[0] & 0b00011111;
				break;
			case 3:
				ch = str[0] & 0b00001111;
				break;
			case 4:
				ch = str[0] & 0b00000111;
				break;
		}
		for (int i = 1; i < sequence_len; ++i)
		{
			auto const b = static_cast<std::uint8_t>(str[static_cast<std::size_t>(i)]);
			// continuation bytes must start with 10xxxxxx
			if (b > 0b10111111 || b < 0b10000000)
				return std::make_pair(-1, sequence_len);
			ch <<= 6;
			ch += b & 0b111111;
		}

		// check if the sequence is overlong, i.e. whether it has leading
		// (redundant) zeros
		switch (sequence_len)
		{
			case 2:
				if (ch < 0x80) return std::make_pair(-1, sequence_len);
				break;
			case 3:
				if (ch < 0x800) return std::make_pair(-1, sequence_len);
				break;
			case 4:
				if (ch < 0x10000) return std::make_pair(-1, sequence_len);
				break;
		}

		if (ch > max_codepoint)
			return std::make_pair(-1, sequence_len);

		return std::make_pair(static_cast<std::int32_t>(ch), sequence_len);
	}

	// Converts ISO-8859-1 (aka latin1) input to UTF-8
	std::string latin1_utf8(span<char const> s)
	{
		std::string ret;
		for (char const c : s)
		{
			std::int32_t const cp = static_cast<unsigned char>(c);
			append_utf8_codepoint(ret, cp);
		}
		return ret;
	}

	// Converts UTF-8 input to ISO-8859-1 (aka latin1)
	// Throws an invalid_argument exception if it finds an unrepresentable character.
	std::string utf8_latin1(std::string_view sv)
	{
		std::string out;
		while (!sv.empty())
		{
			auto const [cp, len] = parse_utf8_codepoint(sv);

			if (cp == -1 || cp > 0xff)
				throw std::invalid_argument("code point out of latin1 range: " + std::to_string(cp));

			sv = sv.substr(std::size_t(len));

			out.push_back(char(static_cast<unsigned char>(cp)));
		}
		return out;
	}
}
