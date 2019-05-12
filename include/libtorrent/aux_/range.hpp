/*

Copyright (c) 2016, Arvid Norberg
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

#ifndef TORRENT_RANGE_HPP
#define TORRENT_RANGE_HPP

#include "libtorrent/aux_/vector.hpp"

namespace libtorrent { namespace aux {

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
}}

#endif
