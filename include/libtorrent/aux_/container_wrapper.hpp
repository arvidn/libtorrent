/*

Copyright (c) 2018, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_CONTAINER_WRAPPER_HPP
#define TORRENT_CONTAINER_WRAPPER_HPP

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/index_range.hpp"
#include "libtorrent/units.hpp"

#include <type_traits>

namespace libtorrent { namespace aux {

	template <typename T, typename IndexType, typename Base>
	struct container_wrapper : Base
	{
		using underlying_index = typename underlying_index_t<IndexType>::type;

		// pull in constructors from Base class
		using Base::Base;
		container_wrapper() = default;
		explicit container_wrapper(Base&& b) : Base(std::move(b)) {}

		auto operator[](IndexType idx) const ->
#if TORRENT_AUTO_RETURN_TYPES
			decltype(auto)
#else
			decltype(this->Base::operator[](underlying_index()))
#endif
		{
			TORRENT_ASSERT(idx >= IndexType(0));
			TORRENT_ASSERT(idx < end_index());
			return this->Base::operator[](std::size_t(static_cast<underlying_index>(idx)));
		}

		auto operator[](IndexType idx) ->
#if TORRENT_AUTO_RETURN_TYPES
			decltype(auto)
#else
			decltype(this->Base::operator[](underlying_index()))
#endif
		{
			TORRENT_ASSERT(idx >= IndexType(0));
			TORRENT_ASSERT(idx < end_index());
			return this->Base::operator[](std::size_t(static_cast<underlying_index>(idx)));
		}

		IndexType end_index() const
		{
			TORRENT_ASSERT(this->size() <= std::size_t((std::numeric_limits<underlying_index>::max)()));
			return IndexType(static_cast<underlying_index>(this->size()));
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
}}

#endif

