/*

Copyright (c) 2017, Alden Torres
Copyright (c) 2018, Steven Siloti
Copyright (c) 2018, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_NUMERIC_CAST_HPP
#define TORRENT_NUMERIC_CAST_HPP

#include <type_traits>
#include <limits>

#include "libtorrent/assert.hpp"

namespace libtorrent { namespace aux {

	template <class T, class In, typename Cond = typename std::enable_if<
		std::is_integral<T>::value && std::is_integral<In>::value>::type>
	T numeric_cast(In v)
	{
		T r = static_cast<T>(v);
		TORRENT_ASSERT(v == static_cast<In>(r));
		TORRENT_ASSERT(std::is_unsigned<In>::value || std::is_signed<T>::value
			|| std::int64_t(v) >= 0);
		TORRENT_ASSERT(std::is_signed<In>::value || std::is_unsigned<T>::value
			|| std::size_t(v) <= std::size_t((std::numeric_limits<T>::max)()));
		return r;
	}

	// in C++ 17 you can use std::clamp
	template <class T>
	T clamp(T v, T lo, T hi)
	{
		TORRENT_ASSERT(lo <= hi);
		if (v < lo) return lo;
		if (hi < v) return hi;
		return v;
	}

}}

#endif
