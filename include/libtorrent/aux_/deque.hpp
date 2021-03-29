/*

Copyright (c) 2017-2018, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DEQUE_HPP
#define TORRENT_DEQUE_HPP

#include <deque>

#include "libtorrent/aux_/container_wrapper.hpp"

namespace lt::aux {

	template <typename T, typename IndexType = std::ptrdiff_t>
	using deque = container_wrapper<T, IndexType, std::deque<T>>;

}

#endif
