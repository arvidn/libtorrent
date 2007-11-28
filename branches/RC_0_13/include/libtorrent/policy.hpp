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
		void update_peer_port(int port, policy::peer* p, int src);

		// called when an incoming connection is accepted
		void new_connection(peer_connection& c);

		// the given connection was just closed
		void connection_closed(const peer_connection& c) throw();

		// the peer has got at least one interesting piece
		void peer_is_interesting(peer_connection& c);

		void piece_finished(int index, bool successfully_verified);

		// the peer choked us
		void choked(peer_connection& c);

		int count_choked() const;

		// the peer unchoked us
		void unchoked(peer_connection& c);

		// the peer is interested in our pieces
		void interested(peer_connection& c);

		// the peer is not interested in our pieces
		void not_interested(peer_connection& c);

		void ip_filter_updated();

#ifndef NDEBUG
		bool has_connection(const peer_connection* p);

		void check_invariant() const;
#endif

		struct peer
		{
			enum connection_type { not_connectable, connectable };

			peer(tcp::endpoint const& ip, connection_type t, int src);

			size_type total_download() const;
			size_type total_upload() const;

			// the ip/port pair this peer is or was connected on
			// if it was a remote (incoming) connection, type is
			// set thereafter. If it was a peer we got from the
			// tracker, type is set to local_connection.
			tcp::endpoint ip;
			connection_type type;

#ifndef TORRENT_DISABLE_ENCRYPTION
			// Hints encryption support of peer. Only effective for
			// and when the outgoing encryption policy allows both
			// encrypted and non encrypted connections
			// (pe_settings::out_enc_policy == enabled). The initial
			// state of this flag determines the initial connection
			// attempt type (true = encrypted, false = standard).
			// This will be toggled everytime either an encrypted or
			// non-encrypted handshake fails.
			bool pe_support;
#endif
			// the number of failed connection attempts this peer has
			int failcount;

			// the number of times this peer has been
			// part of a piece that failed the hash check
			int hashfails;

			// this is true if the peer is a seed
			bool seed;

			int fast_reconnects;

			// true if this peer currently is unchoked
			// because of an optimistic unchoke.
			// when the optimistic unchoke is moved to
			// another peer, this peer will be choked
			// if this is true
			bool optimistically_unchoked;

			// the time when this peer was optimistically unchoked
			// the last time.
			libtorrent::ptime last_optimistically_unchoked;

			// the time when the peer connected to us
			// or disconnected if it isn't connected right now
			libtorrent::ptime connected;

			// for every valid piece we receive where this
			// peer was one of the participants, we increase
			// this value. For every invalid piece we receive
			// where this peer was a participant, we decrease
			// this value. If it sinks below a threshold, its
			// considered a bad peer and will be banned.
			int trust_points;

			// if this is true, the peer has previously participated
			// in a piece that failed the piece hash check. This will
			// put the peer on parole and only request entire pieces.
			// if a piece pass that was partially requested from this
			// peer it will leave parole mode and continue download
			// pieces as normal peers.
			bool on_parole;

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

			// is set to true if this peer has been banned
			bool banned;

			// a bitmap combining the peer_source flags
			// from peer_info.
			int source;

			// if the peer is connected now, this
			// will refer to a valid peer_connection
			peer_connection* connection;
		};

		int num_peers() const { return m_peers.size(); }

		typedef std::multimap<address, peer>::iterator iterator;
		typedef std::multimap<address, peer>::const_iterator const_iterator;
		iterator begin_peer() { return m_peers.begin(); }
		iterator end_peer() { return m_peers.end(); }
		const_iterator begin_peer() const { return m_peers.begin(); }
		const_iterator end_peer() const { return m_peers.end(); }

		bool connect_one_peer();
		bool disconnect_one_peer();

	private:
/*
		bool unchoke_one_peer();
		void choke_one_peer();
		iterator find_choke_candidate();
		iterator find_unchoke_candidate();

		// the seed prefix means that the
		// function is used while seeding.
		bool seed_unchoke_one_peer();
		void seed_choke_one_peer();
		iterator find_seed_choke_candidate();
		iterator find_seed_unchoke_candidate();
*/
		iterator find_disconnect_candidate();
		iterator find_connect_candidate();

		std::multimap<address, peer> m_peers;

		torrent* m_torrent;

		// free download we have got that hasn't
		// been distributed yet.
		size_type m_available_free_upload;

		// if there is a connection limit,
		// we disconnect one peer every minute in hope of
		// establishing a connection with a better peer
//		ptime m_last_optimistic_disconnect;
	};

}

#endif // TORRENT_POLICY_HPP_INCLUDED

