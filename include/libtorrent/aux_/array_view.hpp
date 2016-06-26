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

#ifndef TORRENT_ARRAY_VIEW_HPP_INCLUDED
#define TORRENT_ARRAY_VIEW_HPP_INCLUDED

#include <vector>
#include <array>
#include <type_traits> // for std::is_convertible
#include "libtorrent/assert.hpp"

namespace libtorrent { namespace aux {

	template <typename T>
	struct array_view
	{
		array_view() : m_ptr(nullptr), m_len(0) {}

		// T -> const T conversion constructor
		template <typename U, typename
			= std::enable_if<std::is_convertible<U*, T*>::value>
			>
		array_view(array_view<U> const& v)
			: m_ptr(v.data()), m_len(v.size()) {}

		array_view(T& p) : m_ptr(&p), m_len(1) {}
		array_view(T* p, int l) : m_ptr(p), m_len(l)
		{
			TORRENT_ASSERT(l >= 0);
		}

		template <size_t N>
		array_view(std::array<T, N>& arr)
			: m_ptr(arr.data()), m_len(arr.size()) {}

		template <size_t N>
		array_view(T (&arr)[N])
			: m_ptr(&arr[0]), m_len(N) {}

		array_view(std::vector<T>& vec)
			: m_ptr(vec.data()), m_len(vec.size()) {}

		size_t size() const { return m_len; }
		T* data() const { return m_ptr; }

		using iterator = T*;
		using reverse_iterator = std::reverse_iterator<T*>;

		T* begin() const { return m_ptr; }
		T* end() const { return m_ptr + m_len; }
		reverse_iterator rbegin() const { return reverse_iterator(end()); }
		reverse_iterator rend() const { return reverse_iterator(begin()); }

		T& front() const { TORRENT_ASSERT(m_len > 0); return m_ptr[0]; }
		T& back() const { TORRENT_ASSERT(m_len > 0); return m_ptr[m_len-1]; }

		array_view<T> first(int const n) const
		{
			TORRENT_ASSERT(size() >= n);
			TORRENT_ASSERT(n >= 0);
			return { data(), size() - n };
		}

		array_view<T> last(int const n) const
		{
			TORRENT_ASSERT(size() >= n);
			TORRENT_ASSERT(n >= 0);
			return { data() + size() - n, n };
		}

		array_view<T> cut_first(int const n) const
		{
			TORRENT_ASSERT(size() >= n);
			TORRENT_ASSERT(n >= 0);
			return { data() + n, int(size()) - n };
		}

		T& operator[](int const idx)
		{
			TORRENT_ASSERT(idx >= 0);
			TORRENT_ASSERT(idx < m_len);
			return m_ptr[idx];
		}

	private:
		T* m_ptr;
		size_t m_len;
	};
}}

#endif

