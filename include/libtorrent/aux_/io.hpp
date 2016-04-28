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

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/cstdint.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <string>
#include <algorithm> // for copy
#include "libtorrent/aux_/array_view.hpp"

namespace libtorrent { namespace aux
{
	template <class T> struct type {};

	// reads an integer from a byte stream
	// in big endian byte order and converts
	// it to native endianess
	template <class T, class Byte>
	inline typename std::enable_if<sizeof(Byte)==1, T>::type
	read_impl(array_view<Byte>& view, type<T>)
	{
		T ret = 0;
		for (int i = 0; i < int(sizeof(T)); ++i)
		{
			ret <<= 8;
			ret |= static_cast<boost::uint8_t>(view[i]);
		}
		view = view.cut_first(sizeof(T));
		return ret;
	}

	template <class T, class Byte>
	inline typename std::enable_if<sizeof(Byte)==1, void>::type
	write_impl(T val, array_view<Byte>& view)
	{
		int shift = int(sizeof(T)) * 8;
		for (int i = 0; i < int(sizeof(T)); ++i)
		{
			shift -= 8;
			view[i] = static_cast<unsigned char>((val >> shift) & 0xff);
		}
		view = view.cut_first(sizeof(T));
	}

	// -- adaptors

	template <typename Byte>
	boost::int64_t read_int64(array_view<Byte>& view)
	{ return read_impl(view, type<boost::int64_t>()); }

	template <typename Byte>
	boost::uint64_t read_uint64(array_view<Byte>& view)
	{ return read_impl(view, type<boost::uint64_t>()); }

	template <typename Byte>
	boost::uint32_t read_uint32(array_view<Byte>& view)
	{ return read_impl(view, type<boost::uint32_t>()); }

	template <typename Byte>
	boost::int32_t read_int32(array_view<Byte>& view)
	{ return read_impl(view, type<boost::int32_t>()); }

	template <typename Byte>
	boost::int16_t read_int16(array_view<Byte>& view)
	{ return read_impl(view, type<boost::int16_t>()); }

	template <typename Byte>
	boost::uint16_t read_uint16(array_view<Byte>& view)
	{ return read_impl(view, type<boost::uint16_t>()); }

	template <typename Byte>
	boost::int8_t read_int8(array_view<Byte>& view)
	{ return read_impl(view, type<boost::int8_t>()); }

	template <typename Byte>
	boost::uint8_t read_uint8(array_view<Byte>& view)
	{ return read_impl(view, type<boost::uint8_t>()); }


	template <typename Byte>
	void write_uint64(boost::uint64_t val, array_view<Byte>& view)
	{ write_impl(val, view); }

	template <typename Byte>
	void write_int64(boost::int64_t val, array_view<Byte>& view)
	{ write_impl(val, view); }

	template <typename Byte>
	void write_uint32(boost::uint32_t val, array_view<Byte>& view)
	{ write_impl(val, view); }

	template <typename Byte>
	void write_int32(boost::int32_t val, array_view<Byte>& view)
	{ write_impl(val, view); }

	template <typename Byte>
	void write_uint16(boost::uint16_t val, array_view<Byte>& view)
	{ write_impl(val, view); }

	template <typename Byte>
	void write_int16(boost::int16_t val, array_view<Byte>& view)
	{ write_impl(val, view); }

	template <typename Byte>
	void write_uint8(boost::uint8_t val, array_view<Byte>& view)
	{ write_impl(val, view); }

	template <typename Byte>
	void write_int8(boost::int8_t val, array_view<Byte>& view)
	{ write_impl(val, view); }

	template<typename Byte>
	inline int write_string(std::string const& str, array_view<Byte>& view)
	{
		int const len = int(str.size());
		for (int i = 0; i < len; ++i)
			view[i] = str[i];

		view = array_view<Byte>(view.data() + len, view.size() - len);
		return len;
	}

}}

#endif // TORRENT_AUX_IO_HPP_INCLUDED

