/*

Copyright (c) 2018, BitTorrent Inc.
Copyright (c) 2018, Steven Siloti
Copyright (c) 2019, Arvid Norberg
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

#ifndef TORRENT_INFO_HASH_HPP_INCLUDED
#define TORRENT_INFO_HASH_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/sha1_hash.hpp"

namespace libtorrent
{
	// BitTorrent version enumerator
	enum class protocol_version : std::uint8_t
	{
		V1,
		V2,
		NUM
	};

	constexpr std::size_t num_protocols = int(protocol_version::NUM);

namespace {
	std::initializer_list<protocol_version> const all_versions{
		protocol_version::V1,
		protocol_version::V2
	};
}

	// class holding the info-hash of a torrent. It can hold a v1 info-hash
	// (SHA-1) or a v2 info-hash (SHA-256) or both.
	// .. note::
	//
	// 	If !has_v2() then the v1 hash might actually be a truncated v2 hash
	struct TORRENT_EXPORT info_hash_t
	{
		info_hash_t() {}
		// for backwards compatibility, make it possible to construct directly
		// from a v1 hash
#if TORRENT_ABI_VERSION > 2
		explicit
#endif
		info_hash_t(sha1_hash h1) : v1(h1) {} // NOLINT
		explicit info_hash_t(sha256_hash h2) : v2(h2) {}
		info_hash_t(sha1_hash h1, sha256_hash h2)
			: v1(h1), v2(h2) {}

#if TORRENT_ABI_VERSION <= 2
		// for backwards compatibility, assume the v1 hash is the one the client
		// is interested in
		TORRENT_DEPRECATED operator sha1_hash() const
		{
			return v1;
		}
#endif

		bool has_v1() const { return !v1.is_all_zeros(); }
		bool has_v2() const { return !v2.is_all_zeros(); }
		bool has(protocol_version v) const
		{
			TORRENT_ASSERT(v != protocol_version::NUM);
			return v == protocol_version::V1 ? has_v1() : has_v2();
		}

		sha1_hash get(protocol_version v) const
		{
			TORRENT_ASSERT(v != protocol_version::NUM);
			return v == protocol_version::V1 ? v1 : sha1_hash(v2.data());
		}

		sha1_hash get_best() const
		{
			return has_v2() ? get(protocol_version::V2) : v1;
		}

		friend bool operator!=(info_hash_t const& lhs, info_hash_t const& rhs)
		{
			return std::tie(lhs.v1, lhs.v2) != std::tie(rhs.v1, rhs.v2);
		}

		friend bool operator==(info_hash_t const& lhs, info_hash_t const& rhs)
		{
			return std::tie(lhs.v1, lhs.v2) == std::tie(rhs.v1, rhs.v2);
		}

		template <typename F>
		void for_each(F func) const
		{
			if (has_v1()) func(v1, protocol_version::V1);
			if (has_v2()) func(sha1_hash(v2.data()), protocol_version::V2);
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

#endif
