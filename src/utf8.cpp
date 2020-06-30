/*

Copyright (c) 2012, 2015-2019, Arvid Norberg
Copyright (c) 2016-2017, 2019, Alden Torres
Copyright (c) 2017, Andrei Kurushin
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

#include <iterator>
#include "libtorrent/utf8.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/ConvertUTF.h"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"


#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-member-function"
#endif

namespace libtorrent {

namespace {

		// ==== utf-8 -> wide ===
		template<int width>
		struct convert_to_wide
		{
			static utf8_errors::error_code_enum convert(UTF8 const** src_start
				, UTF8 const* src_end
				, std::wstring& wide)
			{
				TORRENT_UNUSED(src_start);
				TORRENT_UNUSED(src_end);
				TORRENT_UNUSED(wide);
				return utf8_errors::error_code_enum::source_illegal;
			}
		};

		// ==== utf-8 -> utf-32 ===
		template<>
		struct convert_to_wide<4>
		{
			static utf8_errors::error_code_enum convert(char const** src_start
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
					std::copy(reinterpret_cast<std::uint8_t const*>(*src_start)
						,reinterpret_cast<std::uint8_t const*>(src_end)
						, std::back_inserter(wide));
					return static_cast<utf8_errors::error_code_enum>(ret);
				}
				wide.resize(aux::numeric_cast<std::size_t>(dst_start - wide.c_str()));
				return static_cast<utf8_errors::error_code_enum>(ret);
			}
		};

		// ==== utf-8 -> utf-16 ===
		template<>
		struct convert_to_wide<2>
		{
			static utf8_errors::error_code_enum convert(char const** src_start
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
					std::copy(reinterpret_cast<std::uint8_t const*>(*src_start)
						, reinterpret_cast<std::uint8_t const*>(src_end)
						, std::back_inserter(wide));
					return static_cast<utf8_errors::error_code_enum>(ret);
				}
				wide.resize(aux::numeric_cast<std::size_t>(dst_start - wide.c_str()));
				return static_cast<utf8_errors::error_code_enum>(ret);
			}
		};

		// ==== wide -> utf-8 ===
		template<int width>
		struct convert_from_wide
		{
			static utf8_errors::error_code_enum convert(wchar_t const** src_start
				, wchar_t const* src_end
				, std::string& utf8)
			{
				TORRENT_UNUSED(src_start);
				TORRENT_UNUSED(src_end);
				TORRENT_UNUSED(utf8);
				return utf8_errors::error_code_enum::source_illegal;
			}
		};

		// ==== utf-32 -> utf-8 ===
		template<>
		struct convert_from_wide<4>
		{
			static utf8_errors::error_code_enum convert(wchar_t const** src_start
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
				utf8.resize(aux::numeric_cast<std::size_t>(dst_start - &utf8[0]));
				return static_cast<utf8_errors::error_code_enum>(ret);
			}
		};

		// ==== utf-16 -> utf-8 ===
		template<>
		struct convert_from_wide<2>
		{
			static utf8_errors::error_code_enum convert(wchar_t const** src_start
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
				utf8.resize(aux::numeric_cast<std::size_t>(dst_start - &utf8[0]));
				return static_cast<utf8_errors::error_code_enum>(ret);
			}
		};

		struct utf8_error_category final : boost::system::error_category
		{
			const char* name() const BOOST_SYSTEM_NOEXCEPT override
			{
				return "UTF error";
			}

			std::string message(int ev) const override
			{
				static char const* error_messages[] = {
					"ok",
					"source exhausted",
					"target exhausted",
					"source illegal"
				};

				TORRENT_ASSERT(ev >= 0);
				TORRENT_ASSERT(ev < int(sizeof(error_messages)/sizeof(error_messages[0])));
				return error_messages[ev];
			}

			boost::system::error_condition default_error_condition(
				int ev) const BOOST_SYSTEM_NOEXCEPT override
			{ return {ev, *this}; }
		};
	} // anonymous namespace

	namespace utf8_errors
	{
		boost::system::error_code make_error_code(utf8_errors::error_code_enum e)
		{
			return {e, utf8_category()};
		}
	} // utf_errors namespace

	boost::system::error_category const& utf8_category()
	{
		static utf8_error_category cat;
		return cat;
	}

	std::wstring utf8_wchar(string_view utf8, error_code& ec)
	{
		// allocate space for worst-case
		std::wstring wide;
		wide.resize(utf8.size());
		char const* src_start = utf8.data();
		utf8_errors::error_code_enum const ret = convert_to_wide<sizeof(wchar_t)>::convert(
			&src_start, src_start + utf8.size(), wide);
		if (ret != utf8_errors::error_code_enum::conversion_ok)
			ec = make_error_code(ret);
		return wide;
	}

	std::wstring utf8_wchar(string_view utf8)
	{
		error_code ec;
		std::wstring ret = utf8_wchar(utf8, ec);
		if (ec) aux::throw_ex<system_error>(ec);
		return ret;
	}

	std::string wchar_utf8(wstring_view wide, error_code& ec)
	{
		// allocate space for worst-case
		std::string utf8;
		utf8.resize(wide.size() * 6);
		if (wide.empty()) return {};

		wchar_t const* src_start = wide.data();
		utf8_errors::error_code_enum const ret = convert_from_wide<sizeof(wchar_t)>::convert(
			&src_start, src_start + wide.size(), utf8);
		if (ret != utf8_errors::error_code_enum::conversion_ok)
			ec = make_error_code(ret);
		return utf8;
	}

	std::string wchar_utf8(wstring_view wide)
	{
		error_code ec;
		std::string ret = wchar_utf8(wide, ec);
		if (ec) aux::throw_ex<system_error>(ec);
		return ret;
	}

	std::u32string utf8_utf32(std::string_view utf8, error_code& ec)
	{
		// allocate space for worst-case
		std::u32string utf32;
		utf32.resize(utf8.size());

		auto src = reinterpret_cast<UTF8 const*>(utf8.data());
		auto dst = reinterpret_cast<UTF32*>(utf32.data());
		auto dst_begin = dst;
		auto ret = static_cast<utf8_errors::error_code_enum>(ConvertUTF8toUTF32(
					&src, src + utf8.size()
					, &dst, dst + utf32.size()
					, lenientConversion)
				);
		if (ret == utf8_errors::source_illegal)
		{
			// assume Latin-1
			utf32.clear();
			std::copy(utf8.begin()
					, utf8.end()
					, std::back_inserter(utf32));
			return utf32;
		}

		utf32.resize(aux::numeric_cast<std::size_t>(dst - dst_begin));
		if (ret != utf8_errors::error_code_enum::conversion_ok)
			ec = make_error_code(ret);
		return utf32;
	}

	std::u32string utf8_utf32(std::string_view utf8)
	{
		error_code ec;
		std::u32string ret = utf8_utf32(utf8, ec);
		if (ec) aux::throw_ex<system_error>(ec);
		return ret;
	}

	std::string utf32_utf8(std::u32string_view utf32, error_code& ec)
	{
		// allocate space for worst-case
		std::string utf8;
		utf8.resize(utf32.size() * 6);
		if (utf32.empty()) return {};

		auto src = reinterpret_cast<UTF32 const*>(utf32.data());
		auto dst = reinterpret_cast<UTF8*>(utf8.data());
		auto dst_begin = dst;
		auto ret = static_cast<utf8_errors::error_code_enum>(ConvertUTF32toUTF8(
					&src, src+utf32.size()
					, &dst, dst + utf8.size()
					, lenientConversion)
				);
		utf8.resize(aux::numeric_cast<std::size_t>(dst - dst_begin));
		if(ret != utf8_errors::error_code_enum::conversion_ok)
			ec = make_error_code(ret);
		return utf8;
	}

	std::string utf32_utf8(std::u32string_view utf32)
	{
		error_code ec;
		std::string ret = utf32_utf8(utf32, ec);
		if (ec) aux::throw_ex<system_error>(ec);
		return ret;
	}

	// Converts ISO-8859-1 (aka latin1) input to UTF-8
	std::string latin1_utf8(span<char const> s)
	{
		std::u32string u32;
		u32.reserve(std::size_t(s.size()));
		for (char const c : s)
			u32.push_back(char32_t(static_cast<unsigned char>(c)));
		return utf32_utf8(u32);
	}

	// Converts UTF-8 input to ISO-8859-1 (aka latin1)
	// Throws an invalid_argument exception if it finds an unrepresentable character.
	std::string utf8_latin1(std::string_view sv)
	{
		std::u32string u32 = utf8_utf32(sv);
		std::string out;
		for (char32_t cp : u32)
		{
			if (cp > 0xFF)
				throw std::invalid_argument("code point out of latin1 range: " + std::to_string(cp));
			out.push_back(char(static_cast<unsigned char>(cp)));
		}
		return out;
	}

	// returns the unicode codepoint and the number of bytes of the utf8 sequence
	// that was parsed. The codepoint is -1 if it's invalid
	std::pair<std::int32_t, int> parse_utf8_codepoint(string_view str)
	{
		int const sequence_len = trailingBytesForUTF8[static_cast<std::uint8_t>(str[0])] + 1;
		if (sequence_len > int(str.size())) return std::make_pair(-1, static_cast<int>(str.size()));

		if (sequence_len > 4)
		{
			return std::make_pair(-1, sequence_len);
		}

		if (!isLegalUTF8(reinterpret_cast<UTF8 const*>(str.data()), sequence_len))
		{
			return std::make_pair(-1, sequence_len);
		}

		std::uint32_t ch = 0;
		for (int i = 0; i < sequence_len; ++i)
		{
			ch <<= 6;
			ch += static_cast<std::uint8_t>(str[static_cast<std::size_t>(i)]);
		}
		ch -= offsetsFromUTF8[sequence_len-1];

		if (ch > 0x7fffffff)
		{
			return std::make_pair(-1, sequence_len);
		}

		return std::make_pair(static_cast<std::int32_t>(ch), sequence_len);
	}
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

