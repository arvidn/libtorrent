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
#include "libtorrent/debug.hpp"
#include "libtorrent/peer_connection_interface.hpp"

namespace libtorrent
{

	class peer_connection;
	struct logger;
	struct external_ip;
	class torrent_info;
	struct torrent_handle;
	class alert_manager;

	enum
	{
		// the limits of the download queue size
		min_request_queue = 2,
	};

	namespace aux
	{
		struct session_interface;
		struct session_settings;
	}

	struct torrent_interface
	{
		virtual piece_picker& picker() = 0;
		virtual int num_peers() const = 0;
		virtual aux::session_settings const& settings() const = 0;
		virtual aux::session_interface& session() = 0;
		virtual bool apply_ip_filter() const = 0;
		virtual bool is_i2p() const = 0;

		virtual int port_filter_access(int port) const = 0;
		virtual int ip_filter_access(address const& addr) const = 0;

		virtual torrent_peer* allocate_peer_entry(int type) = 0;
		virtual void free_peer_entry(torrent_peer* p) = 0;

		virtual alert_manager& alerts() const = 0;

		virtual bool has_picker() const = 0;

		// this is only used to determine the max peer list size. That could be controlled externally
		virtual bool is_paused() const = 0;

		// some of these properties are really only used to determine which kinds of peers are connect
		// candidates. raise the abstraction level to hand the peer to the torrent and ask it if
		// it's a connect candidate or not.. maybe?
		virtual bool is_finished() const = 0;

		// this is only used when recalculating or altering the number of connect candidates.
		// it could be done by the caller instead
		virtual void update_want_peers() = 0;

		// this is used when we add a new connection, and it succeeds to actually be added
		virtual void state_updated() = 0;

		// this is only used to post alerts. raise the abstraction level, don't do that from within policy
		virtual torrent_handle get_handle() = 0;
#ifndef TORRENT_DISABLE_EXTENSIONS
		virtual void notify_extension_add_peer(tcp::endpoint const& ip, int src, int flags) = 0;
#endif
		virtual bool connect_to_peer(torrent_peer* peerinfo, bool ignore_limit = false) = 0;
	};

	// TODO: this class should be renamed peer_list
	class TORRENT_EXTRA_EXPORT policy : single_threaded
	{
	public:

		policy(torrent_interface* t);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		static void print_size(logger& l);
#endif

#if TORRENT_USE_I2P
		torrent_peer* add_i2p_peer(char const* destination, int source, char flags);
#endif

		// this is called once for every torrent_peer we get from
		// the tracker, pex, lsd or dht.
		torrent_peer* add_peer(const tcp::endpoint& remote
			, int source, char flags);

		// false means duplicate connection
		bool update_peer_port(int port, torrent_peer* p, int src);

		// called when an incoming connection is accepted
		// false means the connection was refused or failed
		bool new_connection(peer_connection_interface& c, int session_time);

		// the given connection was just closed
		void connection_closed(const peer_connection_interface& c, int session_time);

		void ban_peer(torrent_peer* p);
		void set_connection(torrent_peer* p, peer_connection_interface* c);
		void set_failcount(torrent_peer* p, int f);

		void ip_filter_updated();

		void set_seed(torrent_peer* p, bool s);

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		bool has_connection(const peer_connection_interface* p);
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
			, external_ip const& external, int source_port) const;

		iterator find_connect_candidate(int session_time);

		bool is_connect_candidate(torrent_peer const& p, bool finished) const;
		bool is_erase_candidate(torrent_peer const& p, bool finished) const;
		bool is_force_erase_candidate(torrent_peer const& pe) const;
		bool should_erase_immediately(torrent_peer const& p) const;

		enum flags_t { force_erase = 1 };
		void erase_peers(int flags = 0);

		peers_t m_peers;

		torrent_interface* m_torrent;

		// this shouldbe NULL for the most part. It's set
		// to point to a valid torrent_peer object if that
		// object needs to be kept alive. If we ever feel
		// like removing a torrent_peer from m_peers, we
		// first check if the peer matches this one, and
		// if so, don't delete it.
		torrent_peer* m_locked_peer;

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

