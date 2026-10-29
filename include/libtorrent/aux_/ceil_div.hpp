/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_CEIL_DIV_HPP_INCLUDE
#define TORRENT_CEIL_DIV_HPP_INCLUDE

#include <type_traits>

#include "libtorrent/assert.hpp"

namespace libtorrent::aux {

// ceil(n / d). n must not be negative. computed in the unsigned domain
// so it never overflows, unlike the common (n + d - 1) / d formulation
template <typename T>
T ceil_div(T const n, T const d)
{
	if constexpr (std::is_signed<T>::value)
		TORRENT_ASSERT(n >= 0);
	TORRENT_ASSERT(d > 0);
	using U = typename std::make_unsigned<T>::type;
	U const un = static_cast<U>(n);
	U const ud = static_cast<U>(d);
	return static_cast<T>(un / ud + (un % ud != 0 ? 1 : 0));
}

}

#endif
