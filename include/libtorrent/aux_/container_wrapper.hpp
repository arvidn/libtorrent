/*

Copyright (c) 2017, Steven Siloti
Copyright (c) 2018-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_CONTAINER_WRAPPER_HPP
#define TORRENT_CONTAINER_WRAPPER_HPP

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/index_range.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

#include <type_traits>

namespace lt::aux {

	template <typename T, typename IndexType, typename Base>
	struct container_wrapper : Base
	{
		using underlying_index = typename underlying_index_t<IndexType>::type;

		// pull in constructors from Base class
		using Base::Base;
		container_wrapper() = default;
		constexpr explicit container_wrapper(Base&& b) noexcept : Base(std::move(b)) {}

		explicit container_wrapper(IndexType const s)
			: Base(numeric_cast<std::size_t>(static_cast<underlying_index>(s))) {}

		decltype(auto) operator[](IndexType idx) const
		{
			TORRENT_ASSERT(idx >= IndexType(0));
			TORRENT_ASSERT(idx < end_index());
			return this->Base::operator[](std::size_t(static_cast<underlying_index>(idx)));
		}

		decltype(auto) operator[](IndexType idx)
		{
			TORRENT_ASSERT(idx >= IndexType(0));
			TORRENT_ASSERT(idx < end_index());
			return this->Base::operator[](std::size_t(static_cast<underlying_index>(idx)));
		}

		IndexType end_index() const
		{
			TORRENT_ASSERT(this->size() <= std::size_t((std::numeric_limits<underlying_index>::max)()));
			return IndexType(numeric_cast<underlying_index>(this->size()));
		}

		// returns an object that can be used in a range-for to iterate over all
		// indices
		index_range<IndexType> range() const noexcept
		{
			return {IndexType{0}, end_index()};
		}

		template <typename U = underlying_index, typename Cond
			= typename std::enable_if<std::is_signed<U>::value>::type>
		void resize(underlying_index s)
		{
			TORRENT_ASSERT(s >= 0);
			this->Base::resize(std::size_t(s));
		}

		template <typename U = underlying_index, typename Cond
			= typename std::enable_if<std::is_signed<U>::value>::type>
		void resize(underlying_index s, T const& v)
		{
			TORRENT_ASSERT(s >= 0);
			this->Base::resize(std::size_t(s), v);
		}

		void resize(std::size_t s)
		{
			TORRENT_ASSERT(s <= std::size_t((std::numeric_limits<underlying_index>::max)()));
			this->Base::resize(s);
		}

		void resize(std::size_t s, T const& v)
		{
			TORRENT_ASSERT(s <= std::size_t((std::numeric_limits<underlying_index>::max)()));
			this->Base::resize(s, v);
		}

		template <typename U = underlying_index, typename Cond
			= typename std::enable_if<std::is_signed<U>::value>::type>
		void reserve(underlying_index s)
		{
			TORRENT_ASSERT(s >= 0);
			this->Base::reserve(std::size_t(s));
		}

		void reserve(std::size_t s)
		{
			TORRENT_ASSERT(s <= std::size_t((std::numeric_limits<underlying_index>::max)()));
			this->Base::reserve(s);
		}
	};
}

#endif

