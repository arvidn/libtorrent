/*

Copyright (c) 2003-2012, Arvid Norberg
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

#include "libtorrent/torrent_peer.hpp"
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
	struct logger;

	enum
	{
		// the limits of the download queue size
		min_request_queue = 2,
	};

	void request_a_block(torrent& t, peer_connection& c);

	class TORRENT_EXTRA_EXPORT policy
	{
	public:

		policy(torrent* t);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		static void print_size(logger& l);
#endif

#if TORRENT_USE_I2P
		torrent_peer* add_i2p_peer(char const* destination, int source, char flags);
#endif

		// this is called once for every torrent_peer we get from
		// the tracker, pex, lsd or dht.
		torrent_peer* add_peer(const tcp::endpoint& remote, const peer_id& pid
			, int source, char flags);

		// false means duplicate connection
		bool update_peer_port(int port, torrent_peer* p, int src);

		// called when an incoming connection is accepted
		// false means the connection was refused or failed
		bool new_connection(peer_connection& c, int session_time);

		// the given connection was just closed
		void connection_closed(const peer_connection& c, int session_time);

		void ban_peer(torrent_peer* p);
		void set_connection(torrent_peer* p, peer_connection* c);
		void set_failcount(torrent_peer* p, int f);

		// the torrent_peer has got at least one interesting piece
		void peer_is_interesting(peer_connection& c);

		void ip_filter_updated();

		void set_seed(torrent_peer* p, bool s);

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		bool has_connection(const peer_connection* p);
#endif
#ifdef TORRENT_DEBUG
		void check_invariant() const;
#endif

		int num_peers() const { return m_peers.size(); }

#ifdef TORRENT_OPTIMIZE_MEMORY_USAGE
		typedef std::vector<torrent_peer*> peers_t;
#else
		typedef std::deque<torrent_peer*> peers_t;
#endif

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

		bool has_peer(torrent_peer const* p) const;

		int num_seeds() const { return m_num_seeds; }
		int num_connect_candidates() const { return m_num_connect_candidates; }
		void recalculate_connect_candidates();

		void erase_peer(torrent_peer* p);
		void erase_peer(iterator i);

	private:

		void update_connect_candidates(int delta);

		void update_peer(torrent_peer* p, int src, int flags
		, tcp::endpoint const& remote, char const* destination);
		bool insert_peer(torrent_peer* p, iterator iter, int flags);

		bool compare_peer_erase(torrent_peer const& lhs, torrent_peer const& rhs) const;
		bool compare_peer(torrent_peer const& lhs, torrent_peer const& rhs
			, address const& external_ip) const;

		iterator find_connect_candidate(int session_time);

		bool is_connect_candidate(torrent_peer const& p, bool finished) const;
		bool is_erase_candidate(torrent_peer const& p, bool finished) const;
		bool is_force_erase_candidate(torrent_peer const& pe) const;
		bool should_erase_immediately(torrent_peer const& p) const;

		enum flags_t { force_erase = 1 };
		void erase_peers(int flags = 0);

		peers_t m_peers;

		torrent* m_torrent;

		// since the torrent_peer list can grow too large
		// to scan all of it, start at this iterator
		int m_round_robin;

		// The number of peers in our torrent_peer list
		// that are connect candidates. i.e. they're
		// not already connected and they have not
		// yet reached their max try count and they
		// have the connectable state (we have a listen
		// port for them).
		int m_num_connect_candidates;

		// the number of seeds in the torrent_peer list
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

}

#endif // TORRENT_POLICY_HPP_INCLUDED

