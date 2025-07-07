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

namespace libtorrent { namespace aux {

	template <typename T, typename IndexType = std::ptrdiff_t>
	struct unique_ptr;

	template <typename T, typename IndexType>
	struct unique_ptr<T[], IndexType> : std::unique_ptr<T[]>
	{
		using base = std::unique_ptr<T[]>;
		using underlying_index = typename underlying_index_t<IndexType>::type;

		unique_ptr() = default;
		explicit unique_ptr(T* arr) : base(arr) {}

		unique_ptr(base b): base(std::move(b)) {}

		decltype(auto) operator[](IndexType idx) const
		{
			TORRENT_ASSERT(idx >= IndexType(0));
			return this->base::operator[](std::size_t(static_cast<underlying_index>(idx)));
		}
	};

	template <typename T, typename IndexType = std::ptrdiff_t>
	unique_ptr<T, IndexType> make_unique(IndexType const num) {
		static_assert(std::is_array_v<T>);
		return unique_ptr<T, IndexType>(new std::remove_extent_t<T>[std::size_t(num)]);
	}
}}

#endif
