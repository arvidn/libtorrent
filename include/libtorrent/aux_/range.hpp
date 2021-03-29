/*

Copyright (c) 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_RANGE_HPP
#define TORRENT_RANGE_HPP

#include "libtorrent/aux_/vector.hpp"

namespace lt::aux {

	template <typename Iter>
	struct iterator_range
	{
		Iter _begin;
		Iter _end;
		Iter begin() { return _begin; }
		Iter end() { return _end; }
	};

	template <typename Iter>
	iterator_range<Iter> range(Iter begin, Iter end)
	{ return { begin, end}; }

	template <typename T, typename IndexType>
	iterator_range<T*> range(vector<T, IndexType>& vec
		, IndexType begin, IndexType end)
	{
		using type = typename underlying_index_t<IndexType>::type;
		return {vec.data() + static_cast<type>(begin), vec.data() + static_cast<type>(end)};
	}

	template <typename T, typename IndexType>
	iterator_range<T const*> range(vector<T, IndexType> const& vec
		, IndexType begin, IndexType end)
	{
		using type = typename underlying_index_t<IndexType>::type;
		return {vec.data() + static_cast<type>(begin), vec.data() + static_cast<type>(end)};
	}
}

#endif
