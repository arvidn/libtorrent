/*

Copyright (c) 2012-2018, Arvid Norberg
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

#include "libtorrent/config.hpp"

// on windows we need these functions for
// convert_to_native and convert_from_native
#if TORRENT_USE_WSTRING || defined TORRENT_WINDOWS

#include <iterator>
#include "libtorrent/utf8.hpp"
#include "libtorrent/ConvertUTF.h"


#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-member-function"
#endif

namespace libtorrent
{
	namespace
	{
		// ==== utf-8 -> wide ===
		template<int width>
		struct convert_to_wide
		{
			static utf8_conv_result_t convert(UTF8 const** src_start
				, UTF8 const* src_end
				, std::wstring& wide)
			{
				TORRENT_UNUSED(src_start);
				TORRENT_UNUSED(src_end);
				TORRENT_UNUSED(wide);
				return source_illegal;
			}
		};

		// ==== utf-8 -> utf-32 ===
		template<>
		struct convert_to_wide<4>
		{
			static utf8_conv_result_t convert(char const** src_start
				, char const* src_end
				, std::wstring& wide)
			{
				wchar_t* dst_start = &wide[0];
				int ret = ConvertUTF8toUTF32(
					reinterpret_cast<UTF8 const**>(src_start)
					, reinterpret_cast<UTF8 const*>(src_end)
					, reinterpret_cast<UTF32**>(&dst_start)
					, reinterpret_cast<UTF32*>(dst_start + wide.size())
					, lenientConversion);
				if (ret == sourceIllegal)
				{
					// assume Latin-1
					wide.clear();
					std::copy(reinterpret_cast<boost::uint8_t const*>(*src_start)
						,reinterpret_cast<boost::uint8_t const*>(src_end)
						, std::back_inserter(wide));
					return static_cast<utf8_conv_result_t>(ret);
				}
				wide.resize(dst_start - wide.c_str());
				return static_cast<utf8_conv_result_t>(ret);
			}
		};

		// ==== utf-8 -> utf-16 ===
		template<>
		struct convert_to_wide<2>
		{
			static utf8_conv_result_t convert(char const** src_start
				, char const* src_end
				, std::wstring& wide)
			{
				wchar_t* dst_start = &wide[0];
				int ret = ConvertUTF8toUTF16(
					reinterpret_cast<UTF8 const**>(src_start)
					, reinterpret_cast<UTF8 const*>(src_end)
					, reinterpret_cast<UTF16**>(&dst_start)
					, reinterpret_cast<UTF16*>(dst_start + wide.size())
					, lenientConversion);
				if (ret == sourceIllegal)
				{
					// assume Latin-1
					wide.clear();
					std::copy(reinterpret_cast<boost::uint8_t const*>(*src_start)
						, reinterpret_cast<boost::uint8_t const*>(src_end)
						, std::back_inserter(wide));
					return static_cast<utf8_conv_result_t>(ret);
				}
				wide.resize(dst_start - wide.c_str());
				return static_cast<utf8_conv_result_t>(ret);
			}
		};

		// ==== wide -> utf-8 ===
		template<int width>
		struct convert_from_wide
		{
			static utf8_conv_result_t convert(wchar_t const** src_start
				, wchar_t const* src_end
				, std::string& utf8)
			{
				TORRENT_UNUSED(src_start);
				TORRENT_UNUSED(src_end);
				TORRENT_UNUSED(utf8);
				return source_illegal;
			}
		};

		// ==== utf-32 -> utf-8 ===
		template<>
		struct convert_from_wide<4>
		{
			static utf8_conv_result_t convert(wchar_t const** src_start
				, wchar_t const* src_end
				, std::string& utf8)
			{
				char* dst_start = &utf8[0];
				int ret = ConvertUTF32toUTF8(
					reinterpret_cast<UTF32 const**>(src_start)
					, reinterpret_cast<UTF32 const*>(src_end)
					, reinterpret_cast<UTF8**>(&dst_start)
					, reinterpret_cast<UTF8*>(dst_start + utf8.size())
					, lenientConversion);
				utf8.resize(dst_start - &utf8[0]);
				return static_cast<utf8_conv_result_t>(ret);
			}
		};

		// ==== utf-16 -> utf-8 ===
		template<>
		struct convert_from_wide<2>
		{
			static utf8_conv_result_t convert(wchar_t const** src_start
				, wchar_t const* src_end
				, std::string& utf8)
			{
				char* dst_start = &utf8[0];
				int ret = ConvertUTF16toUTF8(
					reinterpret_cast<UTF16 const**>(src_start)
					, reinterpret_cast<UTF16 const*>(src_end)
					, reinterpret_cast<UTF8**>(&dst_start)
					, reinterpret_cast<UTF8*>(dst_start + utf8.size())
					, lenientConversion);
				utf8.resize(dst_start - &utf8[0]);
				return static_cast<utf8_conv_result_t>(ret);
			}
		};
	} // anonymous namespace

	utf8_conv_result_t utf8_wchar(std::string const& utf8, std::wstring &wide)
	{
		// allocate space for worst-case
		wide.resize(utf8.size());
		char const* src_start = utf8.c_str();
		return convert_to_wide<sizeof(wchar_t)>::convert(
			&src_start, src_start + utf8.size(), wide);
	}

	utf8_conv_result_t wchar_utf8(std::wstring const& wide, std::string &utf8)
	{
		// allocate space for worst-case
		utf8.resize(wide.size() * 6);
		if (wide.empty()) return conversion_ok;
		wchar_t const* src_start = wide.c_str();
		return convert_from_wide<sizeof(wchar_t)>::convert(
			&src_start, src_start + wide.size(), utf8);
	}

	// returns the unicode codepoint and the number of bytes of the utf8 sequence
	// that was parsed. The codepoint is -1 if it's invalid
	std::pair<boost::int32_t, int> parse_utf8_codepoint(char const* str, int const len)
	{
		int const sequence_len = trailingBytesForUTF8[static_cast<boost::uint8_t>(*str)] + 1;
		if (sequence_len > len) return std::make_pair(-1, len);

		if (sequence_len > 4)
		{
			return std::make_pair(-1, sequence_len);
		}

		if (!isLegalUTF8(reinterpret_cast<UTF8 const*>(str), sequence_len))
		{
			return std::make_pair(-1, sequence_len);
		}

		boost::uint32_t ch = 0;
		for (int i = 0; i < sequence_len; ++i)
		{
			ch <<= 6;
			ch += static_cast<boost::uint8_t>(str[i]);
		}
		ch -= offsetsFromUTF8[sequence_len-1];

		if (ch > 0x7fffffff)
		{
			return std::make_pair(-1, sequence_len);
		}

		return std::make_pair(static_cast<boost::int32_t>(ch), sequence_len);
	}
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif

