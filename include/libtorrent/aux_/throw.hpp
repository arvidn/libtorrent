/*

Copyright (c) 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_THROW_HPP_INCLUDED
#define TORRENT_THROW_HPP_INCLUDED

#include <utility> // for forward()

#include "libtorrent/config.hpp"

namespace lt::aux {

	template <typename T, typename... Args>
#ifdef BOOST_NO_EXCEPTIONS
	[[noreturn]] void throw_ex(Args&&...) {
		std::terminate();
	}
#else
	[[noreturn]] void throw_ex(Args&&... args) {
		throw T(std::forward<Args>(args)...);
	}
#endif
}

#endif
