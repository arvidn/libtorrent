/*

Copyright (c) 2003-2014, Arvid Norberg
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
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/peer_connection_interface.hpp"

namespace libtorrent
{

	struct external_ip;
	struct ip_filter;
	class port_filter;
	struct torrent_peer_allocator_interface;

	// this object is used to communicate torrent state and
	// some configuration to the peer_list object. This make
	// the peer_list type not depend on the torrent type directly.
	struct torrent_state
	{
		torrent_state()
			: is_paused(false), is_finished(false)
			, allow_multiple_connections_per_ip(false)
			, first_time_seen(false)
			, max_peerlist_size(1000)
			, min_reconnect_time(60)
			, loop_counter(0)
			, ip(NULL), port(0)
			, max_failcount(3)
			, peer_allocator(NULL)
		{}
		bool is_paused;
		bool is_finished;
		bool allow_multiple_connections_per_ip;

		// this is set by peer_list::add_peer to either true or false
		// true means the peer we just added was new, false means
		// we already knew about the peer
		bool first_time_seen;

		int max_peerlist_size;
		int min_reconnect_time;

		// the number of iterations over the peer list for this operation
		int loop_counter;

		// these are used only by find_connect_candidates in order
		// to implement peer ranking. See:
		// http://blog.libtorrent.org/2012/12/swarm-connectivity/
		external_ip const* ip;
		int port;

		// the number of times a peer must fail before it's no longer considered
		// a connect candidate
		int max_failcount;

		// this must be set to a torrent_peer allocator
		torrent_peer_allocator_interface* peer_allocator;

		// if any peer were removed during this call, they are returned in
		// this vector. The caller would want to make sure there are no
		// references to these torrent_peers anywhere
		std::vector<torrent_peer*> erased;
	};

	class TORRENT_EXTRA_EXPORT peer_list : single_threaded
	{
	public:

		peer_list();

#if TORRENT_USE_I2P
		torrent_peer* add_i2p_peer(char const* destination, int src, char flags
			, torrent_state* state);
#endif

		enum
		{
			// these flags match the flags passed in ut_pex
			// messages
			flag_encryption = 0x1,
			flag_seed = 0x2,
			flag_utp = 0x4,
			flag_holepunch = 0x8,
		};

		// this is called once for every torrent_peer we get from
		// the tracker, pex, lsd or dht.
		torrent_peer* add_peer(const tcp::endpoint& remote
			, int source, char flags, torrent_state* state);

		// false means duplicate connection
		bool update_peer_port(int port, torrent_peer* p, int src, torrent_state* state);

		// called when an incoming connection is accepted
		// false means the connection was refused or failed
		bool new_connection(peer_connection_interface& c, int session_time, torrent_state* state);

		// the given connection was just closed
		void connection_closed(const peer_connection_interface& c, int session_time, torrent_state* state);

		bool ban_peer(torrent_peer* p);
		void set_connection(torrent_peer* p, peer_connection_interface* c);
		void set_failcount(torrent_peer* p, int f);
		void inc_failcount(torrent_peer* p);

		void apply_ip_filter(ip_filter const& filter, torrent_state* state
			, std::vector<address>& banned);
		void apply_port_filter(port_filter const& filter, torrent_state* state
			, std::vector<address>& banned);

		void set_seed(torrent_peer* p, bool s);

		// this clears all cached peer priorities. It's called when
		// our external IP changes
		void clear_peer_prio();

#if TORRENT_USE_ASSERTS
		bool has_connection(const peer_connection_interface* p);
#endif
#if TORRENT_USE_INVARIANT_CHECKS
		void check_invariant() const;
#endif

		int num_peers() const { return int(m_peers.size()); }

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

		torrent_peer* connect_one_peer(int session_time, torrent_state* state);

		bool has_peer(torrent_peer const* p) const;

		int num_seeds() const { return m_num_seeds; }
		int num_connect_candidates() const { return m_num_connect_candidates; }

		void erase_peer(torrent_peer* p, torrent_state* state);
		void erase_peer(iterator i, torrent_state* state);

		void set_max_failcount(torrent_state* st);

	private:

		void recalculate_connect_candidates(torrent_state* state);

		void update_connect_candidates(int delta);

		void update_peer(torrent_peer* p, int src, int flags
		, tcp::endpoint const& remote, char const* destination);
		bool insert_peer(torrent_peer* p, iterator iter, int flags, torrent_state* state);

		bool compare_peer_erase(torrent_peer const& lhs, torrent_peer const& rhs) const;
		bool compare_peer(torrent_peer const* lhs, torrent_peer const* rhs
			, external_ip const& external, int source_port) const;

		void find_connect_candidates(std::vector<torrent_peer*>& peers
			, int session_time, torrent_state* state);

		bool is_connect_candidate(torrent_peer const& p) const;
		bool is_erase_candidate(torrent_peer const& p) const;
		bool is_force_erase_candidate(torrent_peer const& pe) const;
		bool should_erase_immediately(torrent_peer const& p) const;

		enum flags_t { force_erase = 1 };
		void erase_peers(torrent_state* state, int flags = 0);

		peers_t m_peers;

		// this should be NULL for the most part. It's set
		// to point to a valid torrent_peer object if that
		// object needs to be kept alive. If we ever feel
		// like removing a torrent_peer from m_peers, we
		// first check if the peer matches this one, and
		// if so, don't delete it.
		torrent_peer* m_locked_peer;

		// the number of seeds in the torrent_peer list
		boost::uint32_t m_num_seeds:31;

		// this was the state of the torrent the
		// last time we recalculated the number of
		// connect candidates. Since seeds (or upload
		// only) peers are not connect candidates
		// when we're finished, the set depends on
		// this state. Every time m_torrent->is_finished()
		// is different from this state, we need to
		// recalculate the connect candidates.
		boost::uint32_t m_finished:1;

		// since the torrent_peer list can grow too large
		// to scan all of it, start at this index
		int m_round_robin;

		// a list of good connect candidates
		std::vector<torrent_peer*> m_candidate_cache;

		// The number of peers in our torrent_peer list
		// that are connect candidates. i.e. they're
		// not already connected and they have not
		// yet reached their max try count and they
		// have the connectable state (we have a listen
		// port for them).
		int m_num_connect_candidates;

		// if a peer has failed this many times or more, we don't consider
		// it a connect candidate anymore.
		int m_max_failcount;
	};

}

#endif // TORRENT_POLICY_HPP_INCLUDED

