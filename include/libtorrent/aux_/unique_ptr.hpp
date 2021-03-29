/*

Copyright (c) 2017, Alden Torres
Copyright (c) 2018-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_UNIQUE_PTR_HPP
#define TORRENT_UNIQUE_PTR_HPP

#include <memory>
#include <type_traits>

#include "libtorrent/units.hpp"
#include "libtorrent/assert.hpp"

namespace lt::aux {

	template <typename T, typename IndexType = std::ptrdiff_t>
	struct unique_ptr;

	template <typename T, typename IndexType>
	struct unique_ptr<T[], IndexType> : std::unique_ptr<T[]>
	{
		using base = std::unique_ptr<T[]>;
		using underlying_index = typename underlying_index_t<IndexType>::type;

		unique_ptr() = default;
		explicit unique_ptr(T* arr) : base(arr) {}

		decltype(auto) operator[](IndexType idx) const
		{
			TORRENT_ASSERT(idx >= IndexType(0));
			return this->base::operator[](std::size_t(static_cast<underlying_index>(idx)));
		}
	};
}

#endif
