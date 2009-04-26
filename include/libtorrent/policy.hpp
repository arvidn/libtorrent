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
#include <vector>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/peer.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/size_type.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent
{

	class torrent;
	class peer_connection;

	enum
	{
		// the limits of the download queue size
		min_request_queue = 2,

		// the amount of free upload allowed before
		// the peer is choked
		free_upload_amount = 4 * 16 * 1024
	};

	void request_a_block(torrent& t, peer_connection& c);

	class TORRENT_EXPORT policy
	{
	public:

		policy(torrent* t);

		// this is called every 10 seconds to allow
		// for peer choking management
		void pulse();

		struct peer;
		// this is called once for every peer we get from
		// the tracker, pex, lsd or dht.
		policy::peer* peer_from_tracker(const tcp::endpoint& remote, const peer_id& pid
			, int source, char flags);

		// false means duplicate connection
		bool update_peer_port(int port, policy::peer* p, int src);

		// called when an incoming connection is accepted
		// false means the connection was refused or failed
		bool new_connection(peer_connection& c);

		// the given connection was just closed
		void connection_closed(const peer_connection& c);

		// the peer has got at least one interesting piece
		void peer_is_interesting(peer_connection& c);

		// the peer unchoked us
		void unchoked(peer_connection& c);

		// the peer is interested in our pieces
		void interested(peer_connection& c);

		// the peer is not interested in our pieces
		void not_interested(peer_connection& c);

		void ip_filter_updated();

#ifdef TORRENT_DEBUG
		bool has_connection(const peer_connection* p);

		void check_invariant() const;
#endif

		struct peer
		{
			enum connection_type { not_connectable, connectable };
			peer(tcp::endpoint const& ip, connection_type t, int src);

			size_type total_download() const;
			size_type total_upload() const;

			tcp::endpoint ip() const { return tcp::endpoint(addr, port); }
			void set_ip(tcp::endpoint const& endp)
			{ addr = endp.address(); port = endp.port(); }

			// this is the accumulated amount of
			// uploaded and downloaded data to this
			// peer. It only accounts for what was
			// shared during the last connection to
			// this peer. i.e. These are only updated
			// when the connection is closed. For the
			// total amount of upload and download
			// we'll have to add thes figures with the
			// statistics from the peer_connection.
			size_type prev_amount_upload;
			size_type prev_amount_download;

			// the ip address this peer is or was connected on
			address addr;

			// the time when this peer was optimistically unchoked
			// the last time.
			libtorrent::ptime last_optimistically_unchoked;

			// the time when the peer connected to us
			// or disconnected if it isn't connected right now
			libtorrent::ptime connected;

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

			// the port this peer is or was connected on
			boost::uint16_t port;

			// the number of failed connection attempts
			// this peer has
			boost::uint8_t failcount;

			// for every valid piece we receive where this
			// peer was one of the participants, we increase
			// this value. For every invalid piece we receive
			// where this peer was a participant, we decrease
			// this value. If it sinks below a threshold, its
			// considered a bad peer and will be banned.
			boost::int8_t trust_points;

			// a bitmap combining the peer_source flags
			// from peer_info.
			boost::uint8_t source;

			// the number of times this peer has been
			// part of a piece that failed the hash check
			boost::uint8_t hashfails;

			// type specifies if the connection was incoming
			// or outgoing. If we ever saw this peer as connectable
			// it will remain as connectable
			unsigned type:4;

			// the number of times we have allowed a fast
			// reconnect for this peer.
			unsigned fast_reconnects:4;

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
			// true if this peer currently is unchoked
			// because of an optimistic unchoke.
			// when the optimistic unchoke is moved to
			// another peer, this peer will be choked
			// if this is true
			bool optimistically_unchoked:1;

			// this is true if the peer is a seed
			bool seed:1;

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
		};

		int num_peers() const { return m_peers.size(); }

		typedef std::multimap<address, peer>::iterator iterator;
		typedef std::multimap<address, peer>::const_iterator const_iterator;
		iterator begin_peer() { return m_peers.begin(); }
		iterator end_peer() { return m_peers.end(); }
		const_iterator begin_peer() const { return m_peers.begin(); }
		const_iterator end_peer() const { return m_peers.end(); }
		std::pair<iterator, iterator> find_peers(address const& a)
		{ return m_peers.equal_range(a); }

		bool connect_one_peer();

		bool has_peer(policy::peer const* p) const;

		int num_seeds() const { return m_num_seeds; }
		int num_connect_candidates() const { return m_num_connect_candidates; }
		void recalculate_connect_candidates()
		{
			if (m_num_connect_candidates == 0)
				m_num_connect_candidates = 1;
		}

		void erase_peer(iterator i);

	private:

		bool compare_peer(policy::peer const& lhs, policy::peer const& rhs
			, address const& external_ip) const;

		iterator find_connect_candidate();

		bool is_connect_candidate(peer const& p, bool finished);

		std::multimap<address, peer> m_peers;

		// since the peer list can grow too large
		// to scan all of it, start at this iterator
		iterator m_round_robin;

		torrent* m_torrent;

		// free download we have got that hasn't
		// been distributed yet.
		size_type m_available_free_upload;

		// The number of peers in our peer list
		// that are connect candidates. i.e. they're
		// not already connected and they have not
		// yet reached their max try count and they
		// have the connectable state (we have a listen
		// port for them).
		int m_num_connect_candidates;

		// the number of seeds in the peer list
		int m_num_seeds;
	};

}

#endif // TORRENT_POLICY_HPP_INCLUDED

