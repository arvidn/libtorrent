/*

Copyright (c) 2017, 2020, Arvid Norberg
Copyright (c) 2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_ALIGNED_UNION_HPP_INCLUDE
#define TORRENT_ALIGNED_UNION_HPP_INCLUDE

#include <type_traits>

namespace libtorrent { namespace aux {

#if defined __GNUC__ && __GNUC__ < 5 && !defined(_LIBCPP_VERSION)

constexpr std::size_t max(std::size_t a)
{ return a; }

constexpr std::size_t max(std::size_t a, std::size_t b)
{ return a > b ? a : b; }

template <typename... Vals>
constexpr std::size_t max(std::size_t a, std::size_t b, Vals... v)
{ return max(a, max(b, v...)); }

// this is for backwards compatibility with not-quite C++11 compilers
template <std::size_t Len, typename... Types>
struct aligned_union
{
	struct type
	{
		alignas(max(alignof(Types)...))
			char buffer[max(Len, max(sizeof(Types)...))];
	};
};

#else

using std::aligned_union;

#endif

}}

#endif
