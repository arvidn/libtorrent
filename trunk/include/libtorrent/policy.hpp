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

#include <boost/date_time/posix_time/posix_time.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/peer.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/size_type.hpp"

namespace libtorrent
{

	class torrent;
	class address;
	class peer_connection;

	class policy
	{
	public:

		policy(torrent* t);

		// this is called every 10 seconds to allow
		// for peer choking management
		void pulse();

		// this is called once for every peer we get from
		// the tracker
		void peer_from_tracker(const address& remote, const peer_id& id);

		// called when an incoming connection is accepted
		// return false if the connection closed
		void new_connection(peer_connection& c);

		// the given connection was just closed
		void connection_closed(const peer_connection& c);

		// is called when a peer is believed to have
		// sent invalid data
		void ban_peer(const peer_connection& c);

		// the peer has got at least one interesting piece
		void peer_is_interesting(peer_connection& c);

		void piece_finished(int index, bool successfully_verified);

		void block_finished(peer_connection& c, piece_block b);

		// the peer choked us
		void choked(peer_connection& c);

		// the peer unchoked us
		void unchoked(peer_connection& c);

		// the peer is interested in our pieces
		void interested(peer_connection& c);

		// the peer is not interested in our pieces
		void not_interested(peer_connection& c);

		void set_max_uploads(int num_unchoked);
		int get_max_uploads() const { return m_max_uploads; }

		/*
			A limit on the number of sockets opened, for use on systems where a
			user has a limited number of open file descriptors. And for windows
			which has a buggy tcp-stack.
		*/
		void set_max_connections(int num_connected);
		int get_max_connections() const { return m_max_connections; }

#ifndef NDEBUG
		bool has_connection(const peer_connection* p);

		void check_invariant();
#endif

		struct peer
		{
			enum connection_type { not_connectable,connectable };

			peer(const address& ip, connection_type t);

			size_type total_download() const;
			size_type total_upload() const;

			// the ip/port pair this peer is or was connected on
			// if it was a remote (incoming) connection, type is
			// set thereafter. If it was a peer we got from the
			// tracker, type is set to local_connection.
			address id;
			connection_type type;

			// the time when this peer was optimistically unchoked
			// the last time.
			boost::posix_time::ptime last_optimistically_unchoked;

			// the time when the peer connected to us
			// or disconnected if it isn't connected right now
			boost::posix_time::ptime connected;

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

			// if the peer is connected now, this
			// will refer to a valid peer_connection
			peer_connection* connection;
		};

	private:

		bool unchoke_one_peer();
		peer* find_choke_candidate();
		peer* find_unchoke_candidate();


		bool connect_peer(peer *);


		bool connect_one_peer();
		bool disconnect_one_peer();
		peer* find_disconnect_candidate();
		peer* find_connect_candidate();



		// a functor that identifies peers that have disconnected and that
		// are too old for still being saved.
		struct old_disconnected_peer
		{
			bool operator()(const peer& p)
			{
				using namespace boost::posix_time;

				ptime not_tried_yet(boost::gregorian::date(1970,boost::gregorian::Jan,1));

				return p.connection == 0
					&& p.connected!=not_tried_yet
					&& second_clock::local_time() - p.connected > minutes(30);
			}
		};


		std::vector<peer> m_peers;

		int m_num_peers;
		torrent* m_torrent;

		// the total number of unchoked peers at
		// any given time. If set to -1, it's unlimited.
		// must be 2 or higher otherwise.
		int m_max_uploads;

		int m_max_connections;

		// the number of unchoked peers
		// at any given time
		int m_num_unchoked;

		// free download we have got that hasn't
		// been distributed yet.
		size_type m_available_free_upload;

		// if there is a connection limit,
		// we disconnect one peer every minute in hope of
		// establishing a connection with a better peer
		boost::posix_time::ptime m_last_optimistic_disconnect;
	};

}

#endif // TORRENT_POLICY_HPP_INCLUDED
