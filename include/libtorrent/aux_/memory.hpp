/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_MEMORY_HPP
#define LIBTORRENT_MEMORY_HPP

#include <memory>

namespace libtorrent::aux {

template<auto F>
struct unique_ptr_destructor {
	template<typename T>
	constexpr void operator()(T* arg) const { (void) F(arg); }
};

template<typename T, auto F>
using unique_ptr_with_deleter = std::unique_ptr<T, unique_ptr_destructor<F> >;
}

#endif //LIBTORRENT_MEMORY_HPP
