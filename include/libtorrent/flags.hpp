/*

Copyright (c) 2017-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_FLAGS_HPP_INCLUDED
#define TORRENT_FLAGS_HPP_INCLUDED

#include <type_traits> // for enable_if
#include <iosfwd>

namespace lt {

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

	bitfield_flag& operator|=(bitfield_flag const f) & noexcept
	{
		m_val |= f.m_val;
		return *this;
	}

	bitfield_flag& operator&=(bitfield_flag const f) & noexcept
	{
		m_val &= f.m_val;
		return *this;
	}

	bitfield_flag& operator^=(bitfield_flag const f) & noexcept
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

	bitfield_flag& operator=(bitfield_flag const& rhs) & noexcept = default;
	bitfield_flag& operator=(bitfield_flag&& rhs) & noexcept = default;

#if TORRENT_USE_IOSTREAM
	friend std::ostream& operator<<(std::ostream& os, bitfield_flag val)
	{ return os << static_cast<UnderlyingType>(val); }
#endif

private:
	UnderlyingType m_val;
};

} // flags
} // libtorrent

#endif
