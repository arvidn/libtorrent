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

#include <string>
#include <array>
#include <type_traits>
#include "libtorrent/assert.hpp"

namespace libtorrent {

namespace aux {

	template <typename From, typename To>
	struct compatible_type
	{
		// conversions that are OK
		// int -> int
		// int const -> int const
		// int -> int const
		// int -> int volatile
		// int -> int const volatile
		// int volatile -> int const volatile
		// int const -> int const volatile

		static const bool value = std::is_same<From, To>::value
			|| std::is_same<From, typename std::remove_const<To>::type>::value
			|| std::is_same<From, typename std::remove_volatile<To>::type>::value
			|| std::is_same<From, typename std::remove_cv<To>::type>::value
			;
	};
}

	template <typename T>
	struct span
	{
		using difference_type = std::ptrdiff_t;
		using index_type = std::ptrdiff_t;

		span() noexcept : m_ptr(nullptr), m_len(0) {}

		template <typename U, typename
			= typename std::enable_if<aux::compatible_type<U, T>::value>::type>
		span(span<U> const& v) noexcept // NOLINT
			: m_ptr(v.data()), m_len(v.size()) {}

		span(T& p) noexcept : m_ptr(&p), m_len(1) {} // NOLINT
		span(T* p, difference_type const l) noexcept : m_ptr(p), m_len(l) // NOLINT
		{ TORRENT_ASSERT(l >= 0); }

		template <typename U, std::size_t N>
		span(std::array<U, N>& arr) noexcept // NOLINT
			: m_ptr(arr.data()), m_len(static_cast<difference_type>(arr.size())) {}

		// this is necessary until C++17, where data() returns a non-const pointer
		template <typename U>
		span(std::basic_string<U>& str) noexcept // NOLINT
			: m_ptr(&str[0]), m_len(static_cast<difference_type>(str.size())) {}

		template <typename U, difference_type N>
		span(U (&arr)[N]) noexcept // NOLINT
			: m_ptr(&arr[0]), m_len(N) {}

		// anything with a .data() member function is considered a container
		// but only if the value type is compatible with T
		template <typename Cont
			, typename U = typename std::remove_reference<decltype(*std::declval<Cont>().data())>::type
			, typename = typename std::enable_if<aux::compatible_type<U, T>::value>::type>
		span(Cont& c) // NOLINT
			: m_ptr(c.data()), m_len(static_cast<difference_type>(c.size())) {}

		// allow construction from const containers if T is const
		// this allows const spans to be constructed from a temporary container
		template <typename Cont
			, typename U = typename std::remove_reference<decltype(*std::declval<Cont>().data())>::type
			, typename = typename std::enable_if<aux::compatible_type<U, T>::value
				&& std::is_const<T>::value>::type>
		span(Cont const& c) // NOLINT
			: m_ptr(c.data()), m_len(static_cast<difference_type>(c.size())) {}

		template <typename U, typename
			= typename std::enable_if<aux::compatible_type<U, T>::value>::type>
		span& operator=(span<U> const& rhs) noexcept
		{
			m_ptr = rhs.data();
			m_len = rhs.size();
			return *this;
		}

		index_type size() const noexcept { return m_len; }
		bool empty() const noexcept { return m_len == 0; }
		T* data() const noexcept { return m_ptr; }

		using const_iterator = T const*;
		using const_reverse_iterator = std::reverse_iterator<T const*>;
		using iterator = T*;
		using reverse_iterator = std::reverse_iterator<T*>;

		T* begin() const noexcept { return m_ptr; }
		T* end() const noexcept { return m_ptr + m_len; }
		reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
		reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }

		T& front() const noexcept { TORRENT_ASSERT(m_len > 0); return m_ptr[0]; }
		T& back() const noexcept { TORRENT_ASSERT(m_len > 0); return m_ptr[m_len - 1]; }

		span<T> first(difference_type const n) const
		{
			TORRENT_ASSERT(size() >= n);
			return { data(), n };
		}

		span<T> last(difference_type const n) const
		{
			TORRENT_ASSERT(size() >= n);
			return { data() + size() - n, n };
		}

		span<T> subspan(index_type const offset) const
		{
			TORRENT_ASSERT(size() >= offset);
			return { data() + offset, size() - offset };
		}

		span<T> subspan(index_type const offset, difference_type const count) const
		{
			TORRENT_ASSERT(count >= 0);
			TORRENT_ASSERT(size() >= offset);
			TORRENT_ASSERT(size() >= offset + count);
			return { data() + offset, count };
		}

		T& operator[](index_type const idx) const
		{
			TORRENT_ASSERT(idx < m_len);
			TORRENT_ASSERT(idx >= 0);
			return m_ptr[idx];
		}

	private:
		T* m_ptr;
		difference_type m_len;
	};

	template <class T, class U>
	inline bool operator==(span<T> const& lhs, span<U> const& rhs)
	{
		return  lhs.size() == rhs.size()
			&& (lhs.data() == rhs.data() || std::equal(lhs.begin(), lhs.end(), rhs.begin()));
	}

	template <class T, class U>
	inline bool operator!=(span<T> const& lhs, span<U> const& rhs)
	{
		return  lhs.size() != rhs.size()
			|| (lhs.data() != rhs.data() && !std::equal(lhs.begin(), lhs.end(), rhs.begin()));
	}
}

#endif // TORRENT_SPAN_HPP_INCLUDED
