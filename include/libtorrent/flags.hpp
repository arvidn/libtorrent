/*

Copyright (c) 2017, Arvid Norberg
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

#ifndef TORRENT_FLAGS_HPP_INCLUDED
#define TORRENT_FLAGS_HPP_INCLUDED

#include <type_traits> // for enable_if
#include <iosfwd>

namespace libtorrent {

struct bit_t
{
	explicit constexpr bit_t(int b) : m_bit_idx(b) {}
	explicit constexpr operator int() const { return m_bit_idx; }
private:
	int m_bit_idx;
};

constexpr bit_t operator "" _bit(unsigned long long int b) { return bit_t{static_cast<int>(b)}; }

namespace flags {

template<typename UnderlyingType, typename Tag
	, typename Cond = typename std::enable_if<std::is_integral<UnderlyingType>::value>::type>
struct bitfield_flag
{
	static_assert(std::is_unsigned<UnderlyingType>::value
		, "flags must use unsigned integers as underlying types");

	using underlying_type = UnderlyingType;

	constexpr bitfield_flag(bitfield_flag const& rhs) noexcept = default;
	constexpr bitfield_flag(bitfield_flag&& rhs) noexcept = default;
	constexpr bitfield_flag() noexcept : m_val(0) {}
	explicit constexpr bitfield_flag(UnderlyingType const val) noexcept : m_val(val) {}
	constexpr bitfield_flag(bit_t const bit) noexcept : m_val(static_cast<UnderlyingType>(UnderlyingType{1} << static_cast<int>(bit))) {}
#if TORRENT_ABI_VERSION >= 2
	explicit constexpr operator UnderlyingType() const noexcept { return m_val; }
#else
	constexpr operator UnderlyingType() const noexcept { return m_val; }
#endif
	explicit constexpr operator bool() const noexcept { return m_val != 0; }

	static constexpr bitfield_flag all()
	{
		return bitfield_flag(static_cast<UnderlyingType>(~UnderlyingType{0}));
	}

	bool constexpr operator==(bitfield_flag const f) const noexcept
	{ return m_val == f.m_val; }

	bool constexpr operator!=(bitfield_flag const f) const noexcept
	{ return m_val != f.m_val; }

	bitfield_flag& operator|=(bitfield_flag const f) noexcept
	{
		m_val |= f.m_val;
		return *this;
	}

	bitfield_flag& operator&=(bitfield_flag const f) noexcept
	{
		m_val &= f.m_val;
		return *this;
	}

	bitfield_flag& operator^=(bitfield_flag const f) noexcept
	{
		m_val ^= f.m_val;
		return *this;
	}

	constexpr friend bitfield_flag operator|(bitfield_flag const lhs, bitfield_flag const rhs) noexcept
	{
		return bitfield_flag(lhs.m_val | rhs.m_val);
	}

	constexpr friend bitfield_flag operator&(bitfield_flag const lhs, bitfield_flag const rhs) noexcept
	{
		return bitfield_flag(lhs.m_val & rhs.m_val);
	}

	constexpr friend bitfield_flag operator^(bitfield_flag const lhs, bitfield_flag const rhs) noexcept
	{
		return bitfield_flag(lhs.m_val ^ rhs.m_val);
	}

	constexpr bitfield_flag operator~() const noexcept
	{
		// technically, m_val is promoted to int before applying operator~, which
		// means the result may not fit into the underlying type again. So,
		// explicitly cast it
		return bitfield_flag(static_cast<UnderlyingType>(~m_val));
	}

	bitfield_flag& operator=(bitfield_flag const& rhs) noexcept = default;
	bitfield_flag& operator=(bitfield_flag&& rhs) noexcept = default;
private:
	UnderlyingType m_val;
};

#if TORRENT_USE_IOSTREAM
	template <typename T, typename Tag>
	std::ostream& operator<<(std::ostream& os, bitfield_flag<T, Tag> val)
	{ return os << static_cast<T>(val); }
#endif

} // flags
} // libtorrent

#endif
