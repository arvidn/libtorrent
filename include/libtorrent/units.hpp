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

#ifndef TORRENT_UNITS_HPP
#define TORRENT_UNITS_HPP

#include <cstdint>
#include <string>
#include <limits>
#include <iosfwd>
#include <type_traits>

#include "libtorrent/config.hpp"

namespace libtorrent { namespace aux {
	template <typename Tag>
	struct difference_tag;

	template<typename UnderlyingType, typename Tag
		, typename Cond = typename std::enable_if<std::is_integral<UnderlyingType>::value>::type>
	struct strong_typedef
	{
		using underlying_type = UnderlyingType;
		using diff_type = strong_typedef<UnderlyingType, difference_tag<Tag>>;

		constexpr strong_typedef(strong_typedef const& rhs) noexcept = default;
		constexpr strong_typedef(strong_typedef&& rhs) noexcept = default;
		strong_typedef() noexcept = default;
#if TORRENT_ABI_VERSION == 1
		constexpr strong_typedef(UnderlyingType val) : m_val(val) {}
		constexpr operator UnderlyingType() const { return m_val; }
#else
		constexpr explicit strong_typedef(UnderlyingType val) : m_val(val) {}
		constexpr explicit operator UnderlyingType() const { return m_val; }
		constexpr bool operator==(strong_typedef const& rhs) const { return m_val == rhs.m_val; }
		constexpr bool operator!=(strong_typedef const& rhs) const { return m_val != rhs.m_val; }
		constexpr bool operator<(strong_typedef const& rhs) const { return m_val < rhs.m_val; }
		constexpr bool operator>(strong_typedef const& rhs) const { return m_val > rhs.m_val; }
		constexpr bool operator>=(strong_typedef const& rhs) const { return m_val >= rhs.m_val; }
		constexpr bool operator<=(strong_typedef const& rhs) const { return m_val <= rhs.m_val; }
#endif
		strong_typedef& operator++() { ++m_val; return *this; }
		strong_typedef& operator--() { --m_val; return *this; }

		strong_typedef operator++(int) & { return strong_typedef{m_val++}; }
		strong_typedef operator--(int) & { return strong_typedef{m_val--}; }

		friend diff_type operator-(strong_typedef lhs, strong_typedef rhs)
		{ return diff_type{lhs.m_val - rhs.m_val}; }
		friend strong_typedef operator+(strong_typedef lhs, diff_type rhs)
		{ return strong_typedef{lhs.m_val + static_cast<UnderlyingType>(rhs)}; }
		friend strong_typedef operator+(diff_type lhs, strong_typedef rhs)
		{ return strong_typedef{static_cast<UnderlyingType>(lhs) + rhs.m_val}; }
		friend strong_typedef operator-(strong_typedef lhs, diff_type rhs)
		{ return strong_typedef{lhs.m_val - static_cast<UnderlyingType>(rhs)}; }

		strong_typedef& operator+=(diff_type rhs) &
		{ m_val += static_cast<UnderlyingType>(rhs); return *this; }
		strong_typedef& operator-=(diff_type rhs) &
		{ m_val -= static_cast<UnderlyingType>(rhs); return *this; }

		strong_typedef& operator=(strong_typedef const& rhs) & noexcept = default;
		strong_typedef& operator=(strong_typedef&& rhs) & noexcept = default;
	private:
		UnderlyingType m_val;
	};

	// meta function to return the underlying type of a strong_typedef or enumeration
	// , or the type itself if it isn't a strong_typedef
	template <typename T, typename = void>
	struct underlying_index_t { using type = T; };

	template <typename T>
	struct underlying_index_t<T, typename std::enable_if<std::is_enum<T>::value>::type>
	{ using type = typename std::underlying_type<T>::type; };

	template <typename U, typename Tag>
	struct underlying_index_t<aux::strong_typedef<U, Tag>> { using type = U; };

	struct piece_index_tag;
	struct file_index_tag;

	template <typename T, typename Tag>
	std::string to_string(strong_typedef<T, Tag> const t)
	{ return std::to_string(static_cast<T>(t)); }

	template <typename T, typename Tag>
	strong_typedef<T, Tag> next(strong_typedef<T, Tag> v)
	{ return ++v;}

	template <typename T, typename Tag>
	strong_typedef<T, Tag> prev(strong_typedef<T, Tag> v)
	{ return --v;}

#if TORRENT_USE_IOSTREAM
	template <typename T, typename Tag>
	std::ostream& operator<<(std::ostream& os, strong_typedef<T, Tag> val)
	{ return os << static_cast<T>(val); }
#endif

} // namespace libtorrent::aux

	// this type represents a piece index in a torrent.
	using piece_index_t = aux::strong_typedef<std::int32_t, aux::piece_index_tag>;

	// this type represents an index to a file in a torrent
	using file_index_t = aux::strong_typedef<std::int32_t, aux::file_index_tag>;

} // namespace libtorrent

namespace std {

	template<typename UnderlyingType, typename Tag>
	class numeric_limits<libtorrent::aux::strong_typedef<UnderlyingType, Tag>> : public std::numeric_limits<UnderlyingType>
	{
		using type = libtorrent::aux::strong_typedef<UnderlyingType, Tag>;
	public:

		static constexpr type (min)()
		{ return type((std::numeric_limits<UnderlyingType>::min)()); }

		static constexpr type (max)()
		{ return type((std::numeric_limits<UnderlyingType>::max)()); }
	};

	template<typename UnderlyingType, typename Tag>
	struct hash<libtorrent::aux::strong_typedef<UnderlyingType, Tag>> : std::hash<UnderlyingType>
	{
		using base = std::hash<UnderlyingType>;
		using argument_type = libtorrent::aux::strong_typedef<UnderlyingType, Tag>;
#if __cplusplus < 201402
		// this was deprecated in C++17
		using result_type = typename base::result_type;
#else
		using result_type = std::size_t;
#endif
		result_type operator()(argument_type const& s) const
		{ return this->base::operator()(static_cast<UnderlyingType>(s)); }
	};
}

#endif
