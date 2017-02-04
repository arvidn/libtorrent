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

#ifndef TORRENT_ARRAY_HPP
#define TORRENT_ARRAY_HPP

#include <array>

#include "libtorrent/units.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent { namespace aux {

	template <typename T, std::size_t Size, typename IndexType = int>
	struct array : std::array<T, Size>
	{
		using base = std::array<T, Size>;
		using underlying_index = typename underlying_index_t<IndexType>::type;

		array() = default;
		explicit array(std::array<T, Size>&& arr) : base(arr) {}

		auto operator[](IndexType idx) const -> decltype(this->base::operator[](underlying_index()))
		{
			TORRENT_ASSERT(idx >= IndexType(0));
			TORRENT_ASSERT(idx < end_index());
			return this->base::operator[](std::size_t(static_cast<underlying_index>(idx)));
		}

		auto operator[](IndexType idx) -> decltype(this->base::operator[](underlying_index()))
		{
			TORRENT_ASSERT(idx >= IndexType(0));
			TORRENT_ASSERT(idx < end_index());
			return this->base::operator[](std::size_t(static_cast<underlying_index>(idx)));
		}

#if !TORRENT_USE_ASSERTS
		constexpr
#endif
		IndexType end_index() const
		{
			TORRENT_ASSERT(this->size() <= std::size_t(std::numeric_limits<underlying_index>::max()));
			return IndexType(static_cast<underlying_index>(this->size()));
		}
	};

}}

#endif
