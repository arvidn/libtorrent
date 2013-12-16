/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_POLICY_HPP_INCLUDED
#define TORRENT_POLICY_HPP_INCLUDED

#include <algorithm>
#include <deque>
#include "libtorrent/string_util.hpp" // for allocate_string_copy

#include "libtorrent/peer.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/size_type.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/config.hpp"

namespace libtorrent
{

	class torrent;
	class peer_connection;

	// this is compressed as an unsigned floating point value
	// the top 13 bits are the mantissa and the low
	// 3 bits is the unsigned exponent. The exponent
	// has an implicit + 4 as well.
	// This means that the resolution is no less than 16
	// The actual rate is: (upload_rate >> 4) << ((upload_rate & 0xf) + 4)
	// the resolution gets worse the higher the value is
	// min value is 0, max value is 16775168
	struct ufloat16
	{
		ufloat16():m_val(0) {}
		ufloat16(int v)
		{ *this = v; }
		operator int()
		{
			return (m_val >> 3) << ((m_val & 7) + 4);
		}

		ufloat16& operator=(int v)
		{
			if (v > 0x1fff << (7 + 4)) m_val = 0xffff;
			else if (v <= 0) m_val = 0;
			else
			{
				int exp = 4;
				v >>= 4;
				while (v > 0x1fff)
				{
					v >>= 1;
					++exp;
				}
				TORRENT_ASSERT(exp <= 7);
				m_val = (v << 3) || (exp & 7);
			}
			return *this;
		}
	private:
		boost::uint16_t m_val;
	};

	enum
	{
		// the limits of the download queue size
		min_request_queue = 2,

		// the amount of free upload allowed before
		// the peer is choked
		free_upload_amount = 4 * 16 * 1024
	};

	void request_a_block(torrent& t, peer_connection& c);

	class TORRENT_EXTRA_EXPORT policy
	{
	public:

		policy(torrent* t);

		struct peer;

#if TORRENT_USE_I2P
		policy::peer* add_i2p_peer(char const* destination, int source, char flags);
#endif

		// this is called once for every peer we get from
		// the tracker, pex, lsd or dht.
		policy::peer* add_peer(const tcp::endpoint& remote, const peer_id& pid
			, int source, char flags);

		// false means duplicate connection
		bool update_peer_port(int port, policy::peer* p, int src);

		// called when an incoming connection is accepted
		// false means the connection was refused or failed
		bool new_connection(peer_connection& c, int session_time);

		// the given connection was just closed
		void connection_closed(const peer_connection& c, int session_time);

		void ban_peer(policy::peer* p);
		void set_connection(policy::peer* p, peer_connection* c);
		void set_failcount(policy::peer* p, int f);

		// the peer has got at least one interesting piece
		void peer_is_interesting(peer_connection& c);

		void ip_filter_updated();

		void set_seed(policy::peer* p, bool s);

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		bool has_connection(const peer_connection* p);
#endif
#ifdef TORRENT_DEBUG
		void check_invariant() const;
#endif

// intended struct layout (on 32 bit architectures)
// offset size  alignment field
// 0      8     4         prev_amount_upload, prev_amount_download
// 8      4     4         connection
// 12     2     2         last_optimistically_unchoked
// 14     2     2         last_connected
// 16     16    1         addr
// 32     2     2         port
// 34     2     2         upload_rate_limit
// 36     2     2         download_rate_limit
// 38     1     1         hashfails
// 39     1     1         failcount, connectable, optimistically_unchoked, seed
// 40     1     1         fast_reconnects, trust_points
// 41     1     1         source, pe_support, is_v6_addr
// 42     1     1         on_parole, banned, added_to_dht, supports_utp,
//                        supports_holepunch, web_seed
// 43     1     1         <padding>
// 44
		struct TORRENT_EXTRA_EXPORT peer
		{
			peer(boost::uint16_t port, bool connectable, int src);

			size_type total_download() const;
			size_type total_upload() const;

			libtorrent::address address() const;
			char const* dest() const;

			tcp::endpoint ip() const { return tcp::endpoint(address(), port); }

			// this is the accumulated amount of
			// uploaded and downloaded data to this
			// peer. It only accounts for what was
			// shared during the last connection to
			// this peer. i.e. These are only updated
			// when the connection is closed. For the
			// total amount of upload and download
			// we'll have to add thes figures with the
			// statistics from the peer_connection.
			// since these values don't need to be stored
			// with byte-precision, they specify the number
			// of kiB. i.e. shift left 10 bits to compare to
			// byte counters.
			boost::uint32_t prev_amount_upload;
			boost::uint32_t prev_amount_download;

			// if the peer is connected now, this
			// will refer to a valid peer_connection
			peer_connection* connection;

#ifndef TORRENT_DISABLE_GEO_IP
#ifdef TORRENT_DEBUG
			// only used in debug mode to assert that
			// the first entry in the AS pair keeps the same
			boost::uint16_t inet_as_num;
#endif
			// The AS this peer belongs to
			std::pair<const int, int>* inet_as;
#endif

			// the time when this peer was optimistically unchoked
			// the last time. in seconds since session was created
			// 16 bits is enough to last for 18.2 hours
			// when the session time reaches 18 hours, it jumps back by
			// 9 hours, and all peers' times are updated to be
			// relative to that new time offset
			boost::uint16_t last_optimistically_unchoked;

			// the time when the peer connected to us
			// or disconnected if it isn't connected right now
			// in number of seconds since session was created
			boost::uint16_t last_connected;

			// the port this peer is or was connected on
			boost::uint16_t port;

			// the upload and download rate limits set for this peer
			ufloat16 upload_rate_limit;
			ufloat16 download_rate_limit;

			// the number of times this peer has been
			// part of a piece that failed the hash check
			boost::uint8_t hashfails;

			// the number of failed connection attempts
			// this peer has
			unsigned failcount:5; // [0, 31]

			// incoming peers (that don't advertize their listen port)
			// will not be considered connectable. Peers that
			// we have a listen port for will be assumed to be.
			bool connectable:1;

			// true if this peer currently is unchoked
			// because of an optimistic unchoke.
			// when the optimistic unchoke is moved to
			// another peer, this peer will be choked
			// if this is true
			bool optimistically_unchoked:1;

			// this is true if the peer is a seed
			bool seed:1;

			// the number of times we have allowed a fast
			// reconnect for this peer.
			unsigned fast_reconnects:4;

			// for every valid piece we receive where this
			// peer was one of the participants, we increase
			// this value. For every invalid piece we receive
			// where this peer was a participant, we decrease
			// this value. If it sinks below a threshold, its
			// considered a bad peer and will be banned.
			signed trust_points:4; // [-7, 8]

			// a bitmap combining the peer_source flags
			// from peer_info.
			unsigned source:6;

#ifndef TORRENT_DISABLE_ENCRYPTION
			// Hints encryption support of peer. Only effective
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

#if TORRENT_USE_IPV6
			// this is true if the v6 union member in addr is
			// the one to use, false if it's the v4 one
			bool is_v6_addr:1;
#endif
#if TORRENT_USE_I2P
			// set if the i2p_destination is in use in the addr union
			bool is_i2p_addr:1;
#endif

			// if this is true, the peer has previously
			// participated in a piece that failed the piece
			// hash check. This will put the peer on parole
			// and only request entire pieces. If a piece pass
			// that was partially requested from this peer it
			// will leave parole mode and continue download
			// pieces as normal peers.
			bool on_parole:1;

			// is set to true if this peer has been banned
			bool banned:1;

#ifndef TORRENT_DISABLE_DHT
			// this is set to true when this peer as been
			// pinged by the DHT
			bool added_to_dht:1;
#endif
			// we think this peer supports uTP
			bool supports_utp:1;
			// we have been connected via uTP at least once
			bool confirmed_supports_utp:1;
			bool supports_holepunch:1;
			// this is set to one for web seeds. Web seeds
			// are not stored in the policy m_peers list,
			// and are excempt from connect candidate bookkeeping
			// so, any peer with the web_seed bit set, is
			// never considered a connect candidate
			bool web_seed:1;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			bool in_use:1;
#endif
		};

		struct TORRENT_EXTRA_EXPORT ipv4_peer : peer
		{
			ipv4_peer(tcp::endpoint const& ip, bool connectable, int src);

			const address_v4 addr;
		};

#if TORRENT_USE_I2P
		struct TORRENT_EXTRA_EXPORT i2p_peer : peer
		{
			i2p_peer(char const* destination, bool connectable, int src);
			~i2p_peer();

			char* destination;
		};
#endif

#if TORRENT_USE_IPV6
		struct TORRENT_EXTRA_EXPORT ipv6_peer : peer
		{
			ipv6_peer(tcp::endpoint const& ip, bool connectable, int src);

			const address_v6::bytes_type addr;
		};
#endif

		int num_peers() const { return m_peers.size(); }

		struct peer_address_compare
		{
			bool operator()(
				peer const* lhs, libtorrent::address const& rhs) const
			{
				return lhs->address() < rhs;
			}

			bool operator()(
				libtorrent::address const& lhs, peer const* rhs) const
			{
				return lhs < rhs->address();
			}

#if TORRENT_USE_I2P
			bool operator()(
				peer const* lhs, char const* rhs) const
			{
				return strcmp(lhs->dest(), rhs) < 0;
			}

			bool operator()(
				char const* lhs, peer const* rhs) const
			{
				return strcmp(lhs, rhs->dest()) < 0;
			}
#endif

			bool operator()(
				peer const* lhs, peer const* rhs) const
			{
#if TORRENT_USE_I2P
				if (rhs->is_i2p_addr == lhs->is_i2p_addr)
					return strcmp(lhs->dest(), rhs->dest()) < 0;
#endif
				return lhs->address() < rhs->address();
			}
		};

		typedef std::deque<peer*> peers_t;

		typedef peers_t::iterator iterator;
		typedef peers_t::const_iterator const_iterator;
		iterator begin_peer() { return m_peers.begin(); }
		iterator end_peer() { return m_peers.end(); }
		const_iterator begin_peer() const { return m_peers.begin(); }
		const_iterator end_peer() const { return m_peers.end(); }

		std::pair<iterator, iterator> find_peers(address const& a)
		{
			return std::equal_range(
				m_peers.begin(), m_peers.end(), a, peer_address_compare());
		}

		std::pair<const_iterator, const_iterator> find_peers(address const& a) const
		{
			return std::equal_range(
				m_peers.begin(), m_peers.end(), a, peer_address_compare());
		}

		bool connect_one_peer(int session_time);

		bool has_peer(policy::peer const* p) const;

		int num_seeds() const { return m_num_seeds; }
		int num_connect_candidates() const { return m_num_connect_candidates; }
		void recalculate_connect_candidates();

		void erase_peer(policy::peer* p);
		void erase_peer(iterator i);

	private:

		void update_peer(policy::peer* p, int src, int flags
		, tcp::endpoint const& remote, char const* destination);
		bool insert_peer(policy::peer* p, iterator iter, int flags);

		bool compare_peer_erase(policy::peer const& lhs, policy::peer const& rhs) const;
		bool compare_peer(policy::peer const& lhs, policy::peer const& rhs
			, address const& external_ip) const;

		iterator find_connect_candidate(int session_time);

		bool is_connect_candidate(peer const& p, bool finished) const;
		bool is_erase_candidate(peer const& p, bool finished) const;
		bool is_force_erase_candidate(peer const& pe) const;
		bool should_erase_immediately(peer const& p) const;

		enum flags_t { force_erase = 1 };
		void erase_peers(int flags = 0);

		peers_t m_peers;

		torrent* m_torrent;

		// this shouldbe NULL for the most part. It's set
		// to point to a valid torrent_peer object if that
		// object needs to be kept alive. If we ever feel
		// like removing a torrent_peer from m_peers, we
		// first check if the peer matches this one, and
		// if so, don't delete it.
		peer* m_locked_peer;

		// since the peer list can grow too large
		// to scan all of it, start at this iterator
		int m_round_robin;

		// The number of peers in our peer list
		// that are connect candidates. i.e. they're
		// not already connected and they have not
		// yet reached their max try count and they
		// have the connectable state (we have a listen
		// port for them).
		int m_num_connect_candidates;

		// the number of seeds in the peer list
		int m_num_seeds;

		// this was the state of the torrent the
		// last time we recalculated the number of
		// connect candidates. Since seeds (or upload
		// only) peers are not connect candidates
		// when we're finished, the set depends on
		// this state. Every time m_torrent->is_finished()
		// is different from this state, we need to
		// recalculate the connect candidates.
		bool m_finished:1;
	};

	inline policy::ipv4_peer::ipv4_peer(
		tcp::endpoint const& ep, bool c, int src
	)
		: peer(ep.port(), c, src)
		, addr(ep.address().to_v4())
	{
#if TORRENT_USE_IPV6
		is_v6_addr = false;
#endif
#if TORRENT_USE_I2P
		is_i2p_addr = false;
#endif
	}

#if TORRENT_USE_I2P
	inline policy::i2p_peer::i2p_peer(char const* dest, bool connectable, int src)
		: peer(0, connectable, src), destination(allocate_string_copy(dest))
	{
#if TORRENT_USE_IPV6
		is_v6_addr = false;
#endif
		is_i2p_addr = true;
	}

	inline policy::i2p_peer::~i2p_peer()
	{ free(destination); }
#endif // TORRENT_USE_I2P

#if TORRENT_USE_IPV6
	inline policy::ipv6_peer::ipv6_peer(
		tcp::endpoint const& ep, bool c, int src
	)
		: peer(ep.port(), c, src)
		, addr(ep.address().to_v6().to_bytes())
	{
		is_v6_addr = true;
#if TORRENT_USE_I2P
		is_i2p_addr = false;
#endif
	}

#endif // TORRENT_USE_IPV6

#if TORRENT_USE_I2P
	inline char const* policy::peer::dest() const
	{
		if (is_i2p_addr)
			return static_cast<policy::i2p_peer const*>(this)->destination;
		return "";
	}
#endif
	
	inline libtorrent::address policy::peer::address() const
	{
#if TORRENT_USE_IPV6
		if (is_v6_addr)
			return libtorrent::address_v6(
				static_cast<policy::ipv6_peer const*>(this)->addr);
		else
#endif
#if TORRENT_USE_I2P
		if (is_i2p_addr) return libtorrent::address();
		else
#endif
		return static_cast<policy::ipv4_peer const*>(this)->addr;
	}

}

#endif // TORRENT_POLICY_HPP_INCLUDED

