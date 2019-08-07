/*

Copyright (c) 2003-2018, Arvid Norberg
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

#ifndef TORRENT_IO_HPP_INCLUDED
#define TORRENT_IO_HPP_INCLUDED

#include <cstdint>
#include <string>
#include <algorithm> // for copy
#include <cstring> // for memcpy
#include <type_traits>
#include <iterator>

#include "assert.hpp"

namespace libtorrent {
namespace detail {

		template <class T> struct type {};

		// reads an integer from a byte stream
		// in big endian byte order and converts
		// it to native endianess
		template <class T, class InIt>
		inline T read_impl(InIt& start, type<T>)
		{
			T ret = 0;
			for (int i = 0; i < int(sizeof(T)); ++i)
			{
				ret <<= 8;
				ret |= static_cast<std::uint8_t>(*start);
				++start;
			}
			return ret;
		}

		template <class InIt>
		std::uint8_t read_impl(InIt& start, type<std::uint8_t>)
		{
			return static_cast<std::uint8_t>(*start++);
		}

		template <class InIt>
		std::int8_t read_impl(InIt& start, type<std::int8_t>)
		{
			return static_cast<std::int8_t>(*start++);
		}

		template <class T, class In, class OutIt>
		typename std::enable_if<(std::is_integral<In>::value
			&& !std::is_same<In, bool>::value)
			|| std::is_enum<In>::value, void>::type
		write_impl(In data, OutIt& start)
		{
			// Note: the test for [OutItT==void] below is necessary because
			// in C++11 std::back_insert_iterator::value_type is void.
			// This could change in C++17 or above
			using OutItT = typename std::iterator_traits<OutIt>::value_type;
			using Byte = typename std::conditional<
				std::is_same<OutItT, void>::value, char, OutItT>::type;
			static_assert(sizeof(Byte) == 1, "wrong iterator or pointer type");

			T val = static_cast<T>(data);
			TORRENT_ASSERT(data == static_cast<In>(val));
			for (int i = int(sizeof(T)) - 1; i >= 0; --i)
			{
				*start = static_cast<Byte>((val >> (i * 8)) & 0xff);
				++start;
			}
		}

		template <class T, class Val, class OutIt>
		typename std::enable_if<std::is_same<Val, bool>::value, void>::type
		write_impl(Val val, OutIt& start)
		{ write_impl<T>(val ? 1 : 0, start); }

		// -- adaptors

		template <class InIt>
		std::int64_t read_int64(InIt& start)
		{ return read_impl(start, type<std::int64_t>()); }

		template <class InIt>
		std::uint64_t read_uint64(InIt& start)
		{ return read_impl(start, type<std::uint64_t>()); }

		template <class InIt>
		std::uint32_t read_uint32(InIt& start)
		{ return read_impl(start, type<std::uint32_t>()); }

		template <class InIt>
		std::int32_t read_int32(InIt& start)
		{ return read_impl(start, type<std::int32_t>()); }

		template <class InIt>
		std::int16_t read_int16(InIt& start)
		{ return read_impl(start, type<std::int16_t>()); }

		template <class InIt>
		std::uint16_t read_uint16(InIt& start)
		{ return read_impl(start, type<std::uint16_t>()); }

		template <class InIt>
		std::int8_t read_int8(InIt& start)
		{ return read_impl(start, type<std::int8_t>()); }

		template <class InIt>
		std::uint8_t read_uint8(InIt& start)
		{ return read_impl(start, type<std::uint8_t>()); }


		template <class T, class OutIt>
		void write_uint64(T val, OutIt& start)
		{ write_impl<std::uint64_t>(val, start); }

		template <class T, class OutIt>
		void write_int64(T val, OutIt& start)
		{ write_impl<std::int64_t>(val, start); }

		template <class T, class OutIt>
		void write_uint32(T val, OutIt& start)
		{ write_impl<std::uint32_t>(val, start); }

		template <class T, class OutIt>
		void write_int32(T val, OutIt& start)
		{ write_impl<std::int32_t>(val, start); }

		template <class T, class OutIt>
		void write_uint16(T val, OutIt& start)
		{ write_impl<std::uint16_t>(val, start); }

		template <class T, class OutIt>
		void write_int16(T val, OutIt& start)
		{ write_impl<std::int16_t>(val, start); }

		template <class T, class OutIt>
		void write_uint8(T val, OutIt& start)
		{ write_impl<std::uint8_t>(val, start); }

		template <class T, class OutIt>
		void write_int8(T val, OutIt& start)
		{ write_impl<std::int8_t>(val, start); }

		inline int write_string(std::string const& str, char*& start)
		{
			std::memcpy(reinterpret_cast<void*>(start), str.c_str(), str.size());
			start += str.size();
			return int(str.size());
		}

		template <class OutIt>
		int write_string(std::string const& val, OutIt& out)
		{
			for (auto const c : val) *out++ = c;
			return int(val.length());
		}
} // namespace detail
}

#endif // TORRENT_IO_HPP_INCLUDED
