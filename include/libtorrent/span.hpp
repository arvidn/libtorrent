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

#ifndef TORRENT_SPAN_HPP_INCLUDED
#define TORRENT_SPAN_HPP_INCLUDED

#include <vector>
#include <array>
#include <type_traits> // for std::is_convertible
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	template <typename T>
	struct span
	{
		span() : m_ptr(nullptr), m_len(0) {}

		template <typename U>
		span(span<U> const& v) // NOLINT
			: m_ptr(v.data()), m_len(v.size()) {}

		span(T& p) : m_ptr(&p), m_len(1) {} // NOLINT
		span(T* p, std::size_t const l) : m_ptr(p), m_len(l) {} // NOLINT

		template <typename U, std::size_t N>
		span(std::array<U, N>& arr) // NOLINT
			: m_ptr(arr.data()), m_len(arr.size()) {}

		template <typename U, std::size_t N>
		span(U (&arr)[N]) // NOLINT
			: m_ptr(&arr[0]), m_len(N) {}

		// anything with a .data() member function is considered a container
		template <typename Cont, typename = decltype(std::declval<Cont>().data())>
		span(Cont& c) // NOLINT
			: m_ptr(c.data()), m_len(c.size()) {}

		std::size_t size() const { return m_len; }
		bool empty() const { return m_len == 0; }
		T* data() const { return m_ptr; }

		using iterator = T*;
		using reverse_iterator = std::reverse_iterator<T*>;

		T* begin() const { return m_ptr; }
		T* end() const { return m_ptr + m_len; }
		reverse_iterator rbegin() const { return reverse_iterator(end()); }
		reverse_iterator rend() const { return reverse_iterator(begin()); }

		T& front() const { TORRENT_ASSERT(m_len > 0); return m_ptr[0]; }
		T& back() const { TORRENT_ASSERT(m_len > 0); return m_ptr[m_len - 1]; }

		span<T> first(std::size_t const n) const
		{
			TORRENT_ASSERT(size() >= n);
			return { data(), n };
		}

		span<T> last(std::size_t const n) const
		{
			TORRENT_ASSERT(size() >= n);
			return { data() + size() - n, n };
		}

		span<T> subspan(std::size_t const offset) const
		{
			TORRENT_ASSERT(size() >= offset);
			return { data() + offset, size() - offset };
		}

		span<T> subspan(std::size_t const offset, std::size_t const count) const
		{
			TORRENT_ASSERT(size() >= offset);
			TORRENT_ASSERT(size() >= offset + count);
			return { data() + offset, count };
		}

		T& operator[](std::size_t const idx) const
		{
			TORRENT_ASSERT(idx < m_len);
			return m_ptr[idx];
		}

	private:
		T* m_ptr;
		std::size_t m_len;
	};
}

#endif // TORRENT_SPAN_HPP_INCLUDED
