/*

Copyright (c) 2017, Arvid Norberg, Alden Torres
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

#ifndef TORRENT_TYPED_SPAN_HPP
#define TORRENT_TYPED_SPAN_HPP

#include <type_traits>

#include "libtorrent/units.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent { namespace aux {

	template <typename T, typename IndexType = int>
	struct typed_span : span<T>
	{
		using base = span<T>;
		using underlying_index = typename underlying_index_t<IndexType>::type;

		// pull in constructors from base class
		using base::base;

		auto operator[](IndexType idx) const ->
#if TORRENT_AUTO_RETURN_TYPES
			decltype(auto)
#else
			decltype(this->base::operator[](underlying_index()))
#endif
		{
			TORRENT_ASSERT(idx >= IndexType(0));
			return this->base::operator[](std::size_t(static_cast<underlying_index>(idx)));
		}

		IndexType end_index() const
		{
			TORRENT_ASSERT(this->size() <= std::size_t(std::numeric_limits<underlying_index>::max()));
			return IndexType(static_cast<underlying_index>(this->size()));
		}

		template <typename U = underlying_index, typename Cond
			= typename std::enable_if<std::is_signed<U>::value>::type>
		typed_span first(underlying_index n) const
		{
			TORRENT_ASSERT(n >= 0);
			return this->base::first(std::size_t(n));
		}

		typed_span first(std::size_t n) const
		{
			TORRENT_ASSERT(n <= std::size_t((std::numeric_limits<underlying_index>::max)()));
			return this->base::first(n);
		}

		template <typename U = underlying_index, typename Cond
			= typename std::enable_if<std::is_signed<U>::value>::type>
		typed_span last(underlying_index n) const
		{
			TORRENT_ASSERT(n >= 0);
			return this->base::last(std::size_t(n));
		}

		typed_span last(std::size_t n) const
		{
			TORRENT_ASSERT(n <= std::size_t((std::numeric_limits<underlying_index>::max)()));
			return this->base::last(n);
		}

		template <typename U = underlying_index, typename Cond
			= typename std::enable_if<std::is_signed<U>::value>::type>
		typed_span subspan(underlying_index offset) const
		{
			TORRENT_ASSERT(offset >= 0);
			return this->base::subspan(std::size_t(offset));
		}

		template <typename U = underlying_index, typename Cond
			= typename std::enable_if<std::is_signed<U>::value>::type>
		typed_span subspan(underlying_index offset, underlying_index count) const
		{
			TORRENT_ASSERT(offset >= 0);
			TORRENT_ASSERT(count >= 0);
			return this->base::subspan(std::size_t(offset), std::size_t(count));
		}

		typed_span subspan(std::size_t offset) const
		{
			TORRENT_ASSERT(offset <= std::size_t((std::numeric_limits<underlying_index>::max)()));
			return this->base::subspan(offset);
		}

		typed_span subspan(std::size_t offset, std::size_t count) const
		{
			TORRENT_ASSERT(offset <= std::size_t((std::numeric_limits<underlying_index>::max)()));
			TORRENT_ASSERT(count <= std::size_t((std::numeric_limits<underlying_index>::max)()));
			return this->base::subspan(offset, count);
		}
	};

}}

#endif
