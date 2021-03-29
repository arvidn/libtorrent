/*

Copyright (c) 2017, 2020, Arvid Norberg
Copyright (c) 2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_ALIGNED_STORAGE_HPP_INCLUDE
#define TORRENT_ALIGNED_STORAGE_HPP_INCLUDE

#include <type_traits>

namespace lt::aux {

#if defined __GNUC__ && __GNUC__ < 5 && !defined(_LIBCPP_VERSION)

// this is for backwards compatibility with not-quite C++11 compilers
template <std::size_t Len, std::size_t Align = alignof(void*)>
struct aligned_storage
{
	struct type
	{
		alignas(Align) char buffer[Len];
	};
};

#else

using std::aligned_storage;

#endif

}

#endif
