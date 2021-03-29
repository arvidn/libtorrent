/*

Copyright (c) 2010, 2013-2014, 2016, 2018-2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_VECTOR_UTILS_HPP_INCLUDE
#define TORRENT_VECTOR_UTILS_HPP_INCLUDE

#include <vector>
#include <algorithm>

namespace lt::aux {

	template <typename Container, typename T>
	auto sorted_find(Container& container, T const& v)
		-> decltype(container.begin())
	{
		auto i = std::lower_bound(container.begin(), container.end(), v);
		if (i == container.end()) return container.end();
		if (*i != v) return container.end();
		return i;
	}

	template <typename T, typename U>
	void sorted_insert(std::vector<T>& container, U v)
	{
		auto i = std::lower_bound(container.begin(), container.end(), v);
		container.insert(i, v);
	}
}

#endif
