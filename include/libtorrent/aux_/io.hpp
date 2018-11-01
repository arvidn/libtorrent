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

#ifndef TORRENT_AUX_IO_HPP_INCLUDED
#define TORRENT_AUX_IO_HPP_INCLUDED

#include <cstdint>
#include <string>
#include <type_traits>
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

namespace libtorrent { namespace aux {

	template <class T> struct type {};

	// reads an integer from a byte stream
	// in big endian byte order and converts
	// it to native endianess
	template <class T, class Byte>
	inline typename std::enable_if<sizeof(Byte)==1, T>::type
	read_impl(span<Byte>& view, type<T>)
	{
		T ret = 0;
		for (Byte const b : view.first(sizeof(T)))
		{
			ret <<= 8;
			ret |= static_cast<std::uint8_t>(b);
		}
		view = view.subspan(int(sizeof(T)));
		return ret;
	}

	template <class T, class In, class Byte>
	inline typename std::enable_if<std::is_integral<T>::value
		&& (std::is_integral<In>::value || std::is_enum<In>::value)
		&& sizeof(Byte)==1>::type
	write_impl(In data, span<Byte>& view)
	{
		T val = static_cast<T>(data);
		TORRENT_ASSERT(data == static_cast<In>(val));
		int shift = int(sizeof(T)) * 8;
		for (Byte& b : view.first(sizeof(T)))
		{
			shift -= 8;
			b = static_cast<Byte>((val >> shift) & 0xff);
		}
		view = view.subspan(int(sizeof(T)));
	}

	// the single-byte case is separate to avoid a warning on the shift-left by
	// 8 bits (which invokes undefined behavior)

	template <class Byte>
	inline typename std::enable_if<sizeof(Byte)==1, std::uint8_t>::type
	read_impl(span<Byte>& view, type<std::uint8_t>)
	{
		std::uint8_t ret = static_cast<std::uint8_t>(view[0]);
		view = view.subspan(1);
		return ret;
	}

	template <class Byte>
	inline typename std::enable_if<sizeof(Byte)==1, std::int8_t>::type
	read_impl(span<Byte>& view, type<std::int8_t>)
	{
		std::uint8_t ret = static_cast<std::int8_t>(view[0]);
		view = view.subspan(1);
		return ret;
	}

	// -- adaptors

	template <typename Byte>
	std::int64_t read_int64(span<Byte>& view)
	{ return read_impl(view, type<std::int64_t>()); }

	template <typename Byte>
	std::uint64_t read_uint64(span<Byte>& view)
	{ return read_impl(view, type<std::uint64_t>()); }

	template <typename Byte>
	std::uint32_t read_uint32(span<Byte>& view)
	{ return read_impl(view, type<std::uint32_t>()); }

	template <typename Byte>
	std::int32_t read_int32(span<Byte>& view)
	{ return read_impl(view, type<std::int32_t>()); }

	template <typename Byte>
	std::int16_t read_int16(span<Byte>& view)
	{ return read_impl(view, type<std::int16_t>()); }

	template <typename Byte>
	std::uint16_t read_uint16(span<Byte>& view)
	{ return read_impl(view, type<std::uint16_t>()); }

	template <typename Byte>
	std::int8_t read_int8(span<Byte>& view)
	{ return read_impl(view, type<std::int8_t>()); }

	template <typename Byte>
	std::uint8_t read_uint8(span<Byte>& view)
	{ return read_impl(view, type<std::uint8_t>()); }


	template <typename T, typename Byte>
	void write_uint64(T val, span<Byte>& view)
	{ write_impl<std::uint64_t>(val, view); }

	template <typename T, typename Byte>
	void write_int64(T val, span<Byte>& view)
	{ write_impl<std::int64_t>(val, view); }

	template <typename T, typename Byte>
	void write_uint32(T val, span<Byte>& view)
	{ write_impl<std::uint32_t>(val, view); }

	template <typename T, typename Byte>
	void write_int32(T val, span<Byte>& view)
	{ write_impl<std::int32_t>(val, view); }

	template <typename T, typename Byte>
	void write_uint16(T val, span<Byte>& view)
	{ write_impl<std::uint16_t>(val, view); }

	template <typename T, typename Byte>
	void write_int16(T val, span<Byte>& view)
	{ write_impl<std::int16_t>(val, view); }

	template <typename T, typename Byte>
	void write_uint8(T val, span<Byte>& view)
	{ write_impl<std::uint8_t>(val, view); }

	template <typename T, typename Byte>
	void write_int8(T val, span<Byte>& view)
	{ write_impl<std::int8_t>(val, view); }

	template<typename Byte>
	inline int write_string(std::string const& str, span<Byte>& view)
	{
		TORRENT_ASSERT(view.size() >= numeric_cast<int>(str.size()));
		std::copy(str.begin(), str.end(), view.begin());
		view = view.subspan(int(str.size()));
		return int(str.size());
	}

}}

#endif // TORRENT_AUX_IO_HPP_INCLUDED
