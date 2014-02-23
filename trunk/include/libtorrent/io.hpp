/*

Copyright (c) 2003-2014, Arvid Norberg
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

#include <boost/cstdint.hpp>
#include <string>
#include <algorithm> // for copy
#include <cstring> // for memcpy

namespace libtorrent
{
	namespace detail
	{
		template <class T> struct type {};

		// reads an integer from a byte stream
		// in big endian byte order and converts
		// it to native endianess
		template <class T, class InIt>
		inline T read_impl(InIt& start, type<T>)
		{
			T ret = 0;
			for (int i = 0; i < (int)sizeof(T); ++i)
			{
				ret <<= 8;
				ret |= static_cast<boost::uint8_t>(*start);
				++start;
			}
			return ret;
		}

		template <class InIt>
		boost::uint8_t read_impl(InIt& start, type<boost::uint8_t>)
		{
			return static_cast<boost::uint8_t>(*start++);
		}

		template <class InIt>
		boost::int8_t read_impl(InIt& start, type<boost::int8_t>)
		{
			return static_cast<boost::int8_t>(*start++);
		}

		template <class T, class OutIt>
		inline void write_impl(T val, OutIt& start)
		{
			for (int i = (int)sizeof(T)-1; i >= 0; --i)
			{
				*start = static_cast<unsigned char>((val >> (i * 8)) & 0xff);
				++start;
			}
		}

		// -- adaptors

		template <class InIt>
		boost::int64_t read_int64(InIt& start)
		{ return read_impl(start, type<boost::int64_t>()); }

		template <class InIt>
		boost::uint64_t read_uint64(InIt& start)
		{ return read_impl(start, type<boost::uint64_t>()); }

		template <class InIt>
		boost::uint32_t read_uint32(InIt& start)
		{ return read_impl(start, type<boost::uint32_t>()); }

		template <class InIt>
		boost::int32_t read_int32(InIt& start)
		{ return read_impl(start, type<boost::int32_t>()); }

		template <class InIt>
		boost::int16_t read_int16(InIt& start)
		{ return read_impl(start, type<boost::int16_t>()); }

		template <class InIt>
		boost::uint16_t read_uint16(InIt& start)
		{ return read_impl(start, type<boost::uint16_t>()); }

		template <class InIt>
		boost::int8_t read_int8(InIt& start)
		{ return read_impl(start, type<boost::int8_t>()); }

		template <class InIt>
		boost::uint8_t read_uint8(InIt& start)
		{ return read_impl(start, type<boost::uint8_t>()); }


		template <class OutIt>
		void write_uint64(boost::uint64_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_int64(boost::int64_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_uint32(boost::uint32_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_int32(boost::int32_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_uint16(boost::uint16_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_int16(boost::int16_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_uint8(boost::uint8_t val, OutIt& start)
		{ write_impl(val, start); }

		template <class OutIt>
		void write_int8(boost::int8_t val, OutIt& start)
		{ write_impl(val, start); }

		inline void write_string(std::string const& str, char*& start)
		{
			std::memcpy((void*)start, str.c_str(), str.size());
			start += str.size();
		}

		template <class OutIt>
		void write_string(std::string const& str, OutIt& start)
		{
			std::copy(str.begin(), str.end(), start);
		}

	}
}

#endif // TORRENT_IO_HPP_INCLUDED
