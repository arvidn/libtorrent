/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_BITMASK_HPP
#define LIBTORRENT_BITMASK_HPP

#include <type_traits>

template<typename E>
class bitmask {
	using U = std::underlying_type_t<E>;
	U bits;
public:
	static_assert(std::is_enum_v<E>);
	using option = E;

	constexpr bitmask(E e = static_cast<E>(0)) noexcept : bits(static_cast<U>(e)) {}
	constexpr explicit bitmask(U b) noexcept : bits(b) {}
	[[nodiscard]] constexpr U raw() const noexcept { return bits; }

	constexpr bool operator==(bitmask o) const noexcept { return bits == o.bits; }
	constexpr bitmask operator|(bitmask o) const noexcept { return bitmask(bits | o.bits); }
	constexpr bitmask operator&(bitmask o) const noexcept { return bitmask(bits & o.bits); }
	constexpr bitmask operator^(bitmask o) const noexcept { return bitmask(bits ^ o.bits); }
	constexpr bitmask operator~() const noexcept { return bitmask(~bits); }
	constexpr bitmask& operator|=(bitmask o) noexcept { bits |= o.bits; return *this; }
	constexpr bitmask& operator&=(bitmask o) noexcept { bits &= o.bits; return *this; }
	constexpr bitmask& operator^=(bitmask o) noexcept { bits ^= o.bits; return *this; }

	constexpr explicit operator bool() const noexcept { return bits != 0; }
	[[nodiscard]] constexpr bool test(E e)       const noexcept { return (bits & static_cast<U>(e)) != 0; }
	[[nodiscard]] constexpr bool test(bitmask o) const noexcept { return (bits & o.bits) != 0; }
	constexpr void unset(E e) noexcept { bits &= ~static_cast<U>(e); }
};

#endif //LIBTORRENT_BITMASK_HPP
