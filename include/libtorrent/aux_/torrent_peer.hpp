/*

Copyright (c) 2014-2017, 2019-2022, Arvid Norberg
Copyright (c) 2016-2017, 2020, Alden Torres
Copyright (c) 2018, Steven Siloti
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_TORRENT_PEER_HPP_INCLUDED
#define TORRENT_TORRENT_PEER_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_info.hpp" // for peer_source_flags_t
#include "libtorrent/info_hash.hpp"
#include "libtorrent/aux_/string_ptr.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/flags.hpp"

namespace libtorrent { struct peer_connection_interface; }

namespace libtorrent::aux {

	struct external_ip;

	// calculate the priority of a peer based on its address. One of the
	// endpoint should be our own. The priority is symmetric, so it doesn't
	// matter which is which
	TORRENT_EXTRA_EXPORT std::uint32_t peer_priority(
		tcp::endpoint e1, tcp::endpoint e2);

	using torrent_peer_flags_t = flags::bitfield_flag<std::uint16_t, struct torrent_peer_flags_tag>;

	// determines which torrent_peer subclass an object is
	enum class peer_address_type : std::uint8_t
	{
		ipv4,
		ipv6,
#if TORRENT_USE_I2P
		i2p,
#endif
#if TORRENT_USE_RTC
		rtc,
#endif
	};

	struct TORRENT_EXTRA_EXPORT torrent_peer
	{
		torrent_peer(
			peer_address_type type, std::uint16_t port, bool connectable, peer_source_flags_t src);
#if TORRENT_USE_ASSERTS
		torrent_peer(torrent_peer const&) = default;
		torrent_peer& operator=(torrent_peer const&) & = default;
		~torrent_peer() { TORRENT_ASSERT(in_use); in_use = false; }
#endif

		std::int64_t total_download() const;
		std::int64_t total_upload() const;

		std::uint32_t rank(aux::external_ip const& external, int external_port) const;

		peer_address_type address_type() const { return static_cast<peer_address_type>(addr_type); }

		libtorrent::address address() const;
		string_view dest() const;

		tcp::endpoint ip() const { return tcp::endpoint(address(), port); }

#ifndef TORRENT_DISABLE_LOGGING
		std::string to_string() const;
#endif

		protocol_version protocol() const
		{
			return (flags & protocol_v2) ? protocol_version::V2 : protocol_version::V1;
		}

		// this is the accumulated amount of
		// uploaded and downloaded data to this
		// torrent_peer. It only accounts for what was
		// shared during the last connection to
		// this torrent_peer. i.e. These are only updated
		// when the connection is closed. For the
		// total amount of upload and download
		// we'll have to add these figures with the
		// statistics from the peer_connection.
		// since these values don't need to be stored
		// with byte-precision, they specify the number
		// of kiB. i.e. shift left 10 bits to compare to
		// byte counters.
		std::uint32_t prev_amount_upload = 0;
		std::uint32_t prev_amount_download = 0;

		// if the torrent_peer is connected now, this
		// will refer to a valid peer_connection
		peer_connection_interface* connection = nullptr;

		// as computed by hashing our IP with the remote
		// IP of this peer
		// calculated lazily
		mutable std::uint32_t peer_rank = 0;

		// the time when this torrent_peer was optimistically unchoked
		// the last time. in seconds since session was created
		// 16 bits is enough to last for 18.2 hours
		// when the session time reaches 18 hours, it jumps back by
		// 9 hours, and all peers' times are updated to be
		// relative to that new time offset
		std::uint16_t last_optimistically_unchoked = 0;

		// the time when the torrent_peer connected to us
		// or disconnected if it isn't connected right now
		// in number of seconds since session was created
		std::uint16_t last_connected = 0;

		// the port this torrent_peer is or was connected on
		std::uint16_t port;

		torrent_peer_flags_t flags = supports_utp;

		// the number of times this torrent_peer has been
		// part of a piece that failed the hash check
		std::uint8_t hashfails = 0;

		// the number of failed connection attempts
		// this torrent_peer has
		std::uint32_t failcount:5 = 0; // [0, 31]

		// the number of times we have allowed a fast
		// reconnect for this torrent_peer.
		std::uint32_t fast_reconnects:4 = 0;

		// a bitmap combining the peer_source flags
		// from peer_info.
		std::uint32_t source:6;

		// newer peers added in the same IPv6 /56, saturating. An entry is
		// evicted by the newest once this saturates, which bounds how much
		// of the peer list one prefix can take over
		std::uint32_t duplicate_prefix:4 = 0;
		static constexpr int max_duplicate_prefix = (1 << 4) - 1;

		// a peer_address_type
		std::uint32_t addr_type:2;

		peer_source_flags_t peer_source() const { return peer_source_flags_t(source); }

		// incoming peers (that don't advertise their listen port)
		// will not be considered connectable. Peers that
		// we have a listen port for will be assumed to be.
		static constexpr torrent_peer_flags_t connectable = 0_bit;

		// set if this torrent_peer currently is unchoked
		// because of an optimistic unchoke.
		// when the optimistic unchoke is moved to
		// another torrent_peer, this torrent_peer will be choked
		// if this is set
		static constexpr torrent_peer_flags_t optimistically_unchoked = 1_bit;

		// set if the torrent_peer is a seed, and we know for sure
		// because we have connected to it and it told us it was a seed
		static constexpr torrent_peer_flags_t seed = 2_bit;

		// this peer confirmed via BEP-21 (LTEP upload_only=1) that it will
		// not download any more. A seed is upload_only by implication,
		// but a partial seed can be upload_only without being a seed, so
		// this is tracked separately from seed.
		static constexpr torrent_peer_flags_t upload_only = 3_bit;

		// we've been told that this peer is upload-only, but we don't know for
		// sure because we haven't connected to it yet. If we are finished, we
		// will de-prioritize peers that may be seeds
		static constexpr torrent_peer_flags_t maybe_upload_only = 4_bit;

		// if set, the torrent_peer has repeatedly
		// participated in pieces that failed the piece
		// hash check. This will put the torrent_peer on parole
		// and only request entire pieces. If a piece pass
		// that was partially requested from this torrent_peer it
		// will leave parole mode and continue download
		// pieces as normal peers.
		static constexpr torrent_peer_flags_t on_parole = 5_bit;

		// set if this torrent_peer has been banned
		static constexpr torrent_peer_flags_t banned = 6_bit;

		// we think this torrent_peer supports uTP; assumed until proven otherwise
		static constexpr torrent_peer_flags_t supports_utp = 7_bit;

		// we have been connected via uTP at least once
		static constexpr torrent_peer_flags_t confirmed_supports_utp = 8_bit;

		static constexpr torrent_peer_flags_t supports_holepunch = 9_bit;

		// this is set for web seeds. Web seeds
		// are not stored in the policy m_peers list,
		// and are exempt from connect candidate bookkeeping
		// so, any torrent_peer with the web_seed bit set, is
		// never considered a connect candidate
		static constexpr torrent_peer_flags_t web_seed = 10_bit;

		// this peer supports protocol version 2
		static constexpr torrent_peer_flags_t protocol_v2 = 11_bit;

#if !defined TORRENT_DISABLE_ENCRYPTION
		// Hints encryption support of torrent_peer. Only effective
		// for and when the outgoing encryption policy
		// allows both encrypted and non encrypted
		// connections (pe_settings::out_enc_policy
		// == enabled). The initial state of this flag
		// determines the initial connection attempt
		// type (set = encrypted, cleared = standard).
		// This will be toggled everytime either an
		// encrypted or non-encrypted handshake fails.
		// defaults to cleared (assume no support) to prefer opening
		// non-encrypted connections first
		static constexpr torrent_peer_flags_t pe_support = 12_bit;
#endif

#if TORRENT_USE_ASSERTS
		bool in_use = true;
#endif
	};

	struct TORRENT_EXTRA_EXPORT ipv4_peer : torrent_peer
	{
		ipv4_peer(tcp::endpoint const& ep, bool connectable, peer_source_flags_t src);
		ipv4_peer(ipv4_peer const& p);
		ipv4_peer& operator=(ipv4_peer const& p) &;

		address_v4 addr;
	};

#if TORRENT_USE_I2P
	struct TORRENT_EXTRA_EXPORT i2p_peer : torrent_peer
	{
		i2p_peer(string_view dest, bool connectable, peer_source_flags_t src);
		i2p_peer(i2p_peer const&) = delete;
		i2p_peer& operator=(i2p_peer const&) = delete;
		i2p_peer(i2p_peer&&) = default;
		i2p_peer& operator=(i2p_peer&&) & = default;

		// TODO: This destination should probably be kept in binary form (rather than
		// base64). The peers should primarily be handled by their sha256 hash
		// of the destination, that should be kept in here as well.
		aux::string_ptr destination;
	};
#endif

#if TORRENT_USE_RTC
	struct TORRENT_EXTRA_EXPORT rtc_peer : torrent_peer
	{
		rtc_peer(tcp::endpoint ep, peer_source_flags_t src);
		rtc_peer(rtc_peer const&) = delete;
		rtc_peer& operator=(rtc_peer const&) = delete;
		rtc_peer(rtc_peer&&) = default;
		rtc_peer& operator=(rtc_peer&&) & = default;

		tcp::endpoint endpoint;
	};
#endif

	struct TORRENT_EXTRA_EXPORT ipv6_peer : torrent_peer
	{
		ipv6_peer(tcp::endpoint const& ep, bool connectable, peer_source_flags_t src);
		ipv6_peer(ipv6_peer const& p);

		const address_v6::bytes_type addr;
	};

	// if we have i2p peer addresses, sort them *after* all IP addresses
	struct peer_address_compare
	{
		bool operator()(torrent_peer const* lhs, address const& rhs) const
		{
#if TORRENT_USE_I2P
			if (lhs->address_type() == peer_address_type::i2p)
				return false;
#endif
			return lhs->address() < rhs;
		}

		bool operator()(address const& lhs, torrent_peer const* rhs) const
		{
#if TORRENT_USE_I2P
			if (rhs->address_type() == peer_address_type::i2p)
				return true;
#endif
			return lhs < rhs->address();
		}

#if TORRENT_USE_I2P
		bool operator()(torrent_peer const* lhs, string_view rhs) const
		{
			if (lhs->address_type() != peer_address_type::i2p)
				return true;
			return lhs->dest() < rhs;
		}

		bool operator()(string_view lhs, torrent_peer const* rhs) const
		{
			if (rhs->address_type() != peer_address_type::i2p)
				return false;
			return lhs < rhs->dest();
		}
#endif

		bool operator()(torrent_peer const* lhs, torrent_peer const* rhs) const
		{
#if TORRENT_USE_I2P
			bool const lhs_i2p = lhs->address_type() == peer_address_type::i2p;
			bool const rhs_i2p = rhs->address_type() == peer_address_type::i2p;
			if (lhs_i2p != rhs_i2p)
				return lhs_i2p < rhs_i2p;

			if (lhs_i2p)
				return lhs->dest() < rhs->dest();
#endif
			return lhs->address() < rhs->address();
		}
	};

	inline bool torrent_peer_equal(torrent_peer const* lhs, torrent_peer const* rhs)
	{
#if TORRENT_USE_I2P
		bool const lhs_i2p = lhs->address_type() == peer_address_type::i2p;
		bool const rhs_i2p = rhs->address_type() == peer_address_type::i2p;
		if (lhs_i2p != rhs_i2p)
			return false;

		if (lhs_i2p)
			return lhs->dest() == rhs->dest();
#endif
		return lhs->address() == rhs->address()
			&& lhs->port == rhs->port;
	}
}

#endif
