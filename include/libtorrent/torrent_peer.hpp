/*

Copyright (c) 2012-2018, Arvid Norberg
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

#ifndef TORRENT_TORRENT_PEER_HPP_INCLUDED
#define TORRENT_TORRENT_PEER_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_info.hpp" // for peer_source_flags_t
#include "libtorrent/aux_/string_ptr.hpp"
#include "libtorrent/string_view.hpp"

namespace libtorrent {

	struct peer_connection_interface;
	struct external_ip;

	// calculate the priority of a peer based on its address. One of the
	// endpoint should be our own. The priority is symmetric, so it doesn't
	// matter which is which
	TORRENT_EXTRA_EXPORT std::uint32_t peer_priority(
		tcp::endpoint e1, tcp::endpoint e2);

	struct TORRENT_EXTRA_EXPORT torrent_peer
	{
		torrent_peer(std::uint16_t port, bool connectable, peer_source_flags_t src);
#if TORRENT_USE_ASSERTS
		torrent_peer(torrent_peer const&) = default;
		torrent_peer& operator=(torrent_peer const&) = default;
		~torrent_peer() { TORRENT_ASSERT(in_use); in_use = false; }
#endif

		std::int64_t total_download() const;
		std::int64_t total_upload() const;

		std::uint32_t rank(external_ip const& external, int external_port) const;

		libtorrent::address address() const;
		string_view dest() const;

		tcp::endpoint ip() const { return tcp::endpoint(address(), port); }

#ifndef TORRENT_DISABLE_LOGGING
		std::string to_string() const;
#endif

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
		std::uint32_t prev_amount_upload;
		std::uint32_t prev_amount_download;

		// if the torrent_peer is connected now, this
		// will refer to a valid peer_connection
		peer_connection_interface* connection;

		// as computed by hashing our IP with the remote
		// IP of this peer
		// calculated lazily
		mutable std::uint32_t peer_rank;

		// the time when this torrent_peer was optimistically unchoked
		// the last time. in seconds since session was created
		// 16 bits is enough to last for 18.2 hours
		// when the session time reaches 18 hours, it jumps back by
		// 9 hours, and all peers' times are updated to be
		// relative to that new time offset
		std::uint16_t last_optimistically_unchoked;

		// the time when the torrent_peer connected to us
		// or disconnected if it isn't connected right now
		// in number of seconds since session was created
		std::uint16_t last_connected;

		// the port this torrent_peer is or was connected on
		std::uint16_t port;

		// the number of times this torrent_peer has been
		// part of a piece that failed the hash check
		std::uint8_t hashfails;

		// the number of failed connection attempts
		// this torrent_peer has
		std::uint32_t failcount:5; // [0, 31]

		// incoming peers (that don't advertise their listen port)
		// will not be considered connectable. Peers that
		// we have a listen port for will be assumed to be.
		bool connectable:1;

		// true if this torrent_peer currently is unchoked
		// because of an optimistic unchoke.
		// when the optimistic unchoke is moved to
		// another torrent_peer, this torrent_peer will be choked
		// if this is true
		bool optimistically_unchoked:1;

		// this is true if the torrent_peer is a seed
		bool seed:1;

		// the number of times we have allowed a fast
		// reconnect for this torrent_peer.
		std::uint32_t fast_reconnects:4;

		// for every valid piece we receive where this
		// torrent_peer was one of the participants, we increase
		// this value. For every invalid piece we receive
		// where this torrent_peer was a participant, we decrease
		// this value. If it sinks below a threshold, its
		// considered a bad torrent_peer and will be banned.
		signed trust_points:4; // [-7, 8]

		// a bitmap combining the peer_source flags
		// from peer_info.
		std::uint32_t source:6;

		peer_source_flags_t peer_source() const
		{ return peer_source_flags_t(source); }

#if !defined TORRENT_DISABLE_ENCRYPTION
		// Hints encryption support of torrent_peer. Only effective
		// for and when the outgoing encryption policy
		// allows both encrypted and non encrypted
		// connections (pe_settings::out_enc_policy
		// == enabled). The initial state of this flag
		// determines the initial connection attempt
		// type (true = encrypted, false = standard).
		// This will be toggled everytime either an
		// encrypted or non-encrypted handshake fails.
		bool pe_support:1;
#endif

		// this is true if the v6 union member in addr is
		// the one to use, false if it's the v4 one
		bool is_v6_addr:1;
#if TORRENT_USE_I2P
		// set if the i2p_destination is in use in the addr union
		bool is_i2p_addr:1;
#endif

		// if this is true, the torrent_peer has previously
		// participated in a piece that failed the piece
		// hash check. This will put the torrent_peer on parole
		// and only request entire pieces. If a piece pass
		// that was partially requested from this torrent_peer it
		// will leave parole mode and continue download
		// pieces as normal peers.
		bool on_parole:1;

		// is set to true if this torrent_peer has been banned
		bool banned:1;

		// we think this torrent_peer supports uTP
		bool supports_utp:1;
		// we have been connected via uTP at least once
		bool confirmed_supports_utp:1;
		bool supports_holepunch:1;
		// this is set to one for web seeds. Web seeds
		// are not stored in the policy m_peers list,
		// and are exempt from connect candidate bookkeeping
		// so, any torrent_peer with the web_seed bit set, is
		// never considered a connect candidate
		bool web_seed:1;
#if TORRENT_USE_ASSERTS
		bool in_use = true;
#endif
	};

	struct TORRENT_EXTRA_EXPORT ipv4_peer : torrent_peer
	{
		ipv4_peer(tcp::endpoint const& ip, bool connectable, peer_source_flags_t src);
		ipv4_peer(ipv4_peer const& p);
		ipv4_peer& operator=(ipv4_peer const& p);

		address_v4 addr;
	};

#if TORRENT_USE_I2P
	struct TORRENT_EXTRA_EXPORT i2p_peer : torrent_peer
	{
		i2p_peer(string_view dst, bool connectable, peer_source_flags_t src);
		i2p_peer(i2p_peer const&) = delete;
		i2p_peer& operator=(i2p_peer const&) = delete;
		i2p_peer(i2p_peer&&) = default;
		i2p_peer& operator=(i2p_peer&&) = default;

		aux::string_ptr destination;
	};
#endif

	struct TORRENT_EXTRA_EXPORT ipv6_peer : torrent_peer
	{
		ipv6_peer(tcp::endpoint const& ip, bool connectable, peer_source_flags_t src);
		ipv6_peer(ipv6_peer const& p);

		const address_v6::bytes_type addr;
	};

	struct peer_address_compare
	{
		bool operator()(torrent_peer const* lhs, address const& rhs) const
		{
			return lhs->address() < rhs;
		}

		bool operator()(address const& lhs, torrent_peer const* rhs) const
		{
			return lhs < rhs->address();
		}

#if TORRENT_USE_I2P
		bool operator()(torrent_peer const* lhs, string_view rhs) const
		{
			return lhs->dest().compare(rhs) < 0;
		}

		bool operator()(string_view lhs, torrent_peer const* rhs) const
		{
			return lhs.compare(rhs->dest()) < 0;
		}
#endif

		bool operator()(torrent_peer const* lhs, torrent_peer const* rhs) const
		{
#if TORRENT_USE_I2P
			if (rhs->is_i2p_addr == lhs->is_i2p_addr)
				return lhs->dest().compare(rhs->dest()) < 0;
#endif
			return lhs->address() < rhs->address();
		}
	};
}

#endif
