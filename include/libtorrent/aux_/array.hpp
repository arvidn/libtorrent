/*

Copyright (c) 2017, Alden Torres
Copyright (c) 2018, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_ARRAY_HPP
#define TORRENT_ARRAY_HPP

#include <array>

#include "libtorrent/aux_/container_wrapper.hpp"

namespace lt::aux {

	template <typename T, std::size_t Size, typename IndexType = std::ptrdiff_t>
	using array = container_wrapper<T, IndexType, std::array<T, Size>>;

}

#endif
