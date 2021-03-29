/*

Copyright (c) 2018, BitTorrent Inc.
Copyright (c) 2018, Steven Siloti
Copyright (c) 2019-2021, Arvid Norberg
Copyright (c) 2020, Mike Tzou
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_INFO_HASH_HPP_INCLUDED
#define TORRENT_INFO_HASH_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/sha1_hash.hpp"

#if TORRENT_USE_IOSTREAM
#include <ostream>
#endif // TORRENT_USE_IOSTREAM

namespace lt
{
	// BitTorrent version enumerator
	enum class protocol_version : std::uint8_t
	{
		// The original BitTorrent version, using SHA-1 hashes
		V1,
		// Version 2 of the BitTorrent protocol, using SHA-256 hashes
		V2,
		NUM
	};

	// internal
	constexpr std::size_t num_protocols = int(protocol_version::NUM);

namespace {
	std::initializer_list<protocol_version> const all_versions{
		protocol_version::V1,
		protocol_version::V2
	};
}

	// class holding the info-hash of a torrent. It can hold a v1 info-hash
	// (SHA-1) or a v2 info-hash (SHA-256) or both.
	//
	// .. note::
	//
	//	If ``has_v2()`` is false then the v1 hash might actually be a truncated
	//	v2 hash
	struct TORRENT_EXPORT info_hash_t
	{
		// The default constructor creates an object that has neither a v1 or v2
		// hash.
		//
		// For backwards compatibility, make it possible to construct directly
		// from a v1 hash. This constructor allows *implicit* conversion from a
		// v1 hash, but the implicitness is deprecated.
		info_hash_t() noexcept = default;
		explicit info_hash_t(sha1_hash h1) noexcept : v1(h1) {} // NOLINT
		explicit info_hash_t(sha256_hash h2) noexcept : v2(h2) {}
		info_hash_t(sha1_hash h1, sha256_hash h2) noexcept
			: v1(h1), v2(h2) {}

		// hidden
		info_hash_t(info_hash_t const&) noexcept = default;
		info_hash_t& operator=(info_hash_t const&) & noexcept = default;

		// returns true if the corresponding info hash is present in this
		// object.
		bool has_v1() const { return !v1.is_all_zeros(); }
		bool has_v2() const { return !v2.is_all_zeros(); }
		bool has(protocol_version v) const
		{
			TORRENT_ASSERT(v != protocol_version::NUM);
			return v == protocol_version::V1 ? has_v1() : has_v2();
		}

		// returns the has for the specified protocol version
		sha1_hash get(protocol_version v) const
		{
			TORRENT_ASSERT(v != protocol_version::NUM);
			return v == protocol_version::V1 ? v1 : sha1_hash(v2.data());
		}

		// returns the v2 (truncated) info-hash, if there is one, otherwise
		// returns the v1 info-hash
		sha1_hash get_best() const
		{
			return has_v2() ? get(protocol_version::V2) : v1;
		}

		friend bool operator!=(info_hash_t const& lhs, info_hash_t const& rhs)
		{
			return std::tie(lhs.v1, lhs.v2) != std::tie(rhs.v1, rhs.v2);
		}

		friend bool operator==(info_hash_t const& lhs, info_hash_t const& rhs) noexcept
		{
			return std::tie(lhs.v1, lhs.v2) == std::tie(rhs.v1, rhs.v2);
		}

		// calls the function object ``f`` for each hash that is available.
		// starting with v1. The signature of ``F`` is::
		//
		//	void(sha1_hash, protocol_version);
		template <typename F> void for_each(F f) const
		{
			if (has_v1()) f(v1, protocol_version::V1);
			if (has_v2()) f(sha1_hash(v2.data()), protocol_version::V2);
		}

		bool operator<(info_hash_t const& o) const
		{
			return std::tie(v1, v2) < std::tie(o.v1, o.v2);
		}

#if TORRENT_USE_IOSTREAM
		friend std::ostream& operator<<(std::ostream& os, info_hash_t const& ih)
		{
			return os << '[' << ih.v1 << ',' << ih.v2 << ']';
		}
#endif // TORRENT_USE_IOSTREAM

		sha1_hash v1;
		sha256_hash v2;
	};

}

namespace std {
	template <>
	struct hash<lt::info_hash_t>
	{
		std::size_t operator()(lt::info_hash_t const& k) const
		{
			return std::hash<lt::sha1_hash>{}(k.v1)
				^ std::hash<lt::sha256_hash>{}(k.v2) ;
		}
	};
}

#endif
