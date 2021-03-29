/*

Copyright (c) 2016, 2018, 2020, Arvid Norberg
Copyright (c) 2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_VECTOR_HPP
#define TORRENT_VECTOR_HPP

#include <vector>

#include "libtorrent/aux_/container_wrapper.hpp"

namespace lt::aux {

	template <typename T, typename IndexType = int>
	using vector = container_wrapper<T, IndexType, std::vector<T>>;

}

#endif
