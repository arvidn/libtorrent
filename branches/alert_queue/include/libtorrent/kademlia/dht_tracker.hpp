/*

Copyright (c) 2006-2014, Arvid Norberg
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

#ifndef TORRENT_DISABLE_DHT

#ifndef TORRENT_DHT_TRACKER
#define TORRENT_DHT_TRACKER

#include <fstream>
#include <set>
#include <numeric>
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/smart_ptr/enable_shared_from_this.hpp>

#include "libtorrent/kademlia/node.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/traversal_algorithm.hpp"
#include "libtorrent/kademlia/dos_blocker.hpp"

#include "libtorrent/session_settings.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/deadline_timer.hpp"

namespace libtorrent
{
	namespace aux { struct session_impl; }
	struct counters;
#ifndef TORRENT_NO_DEPRECATE
	struct session_status;
#endif
}

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_DECLARE_LOG(dht_tracker);
#endif

	struct dht_tracker;

	struct dht_tracker
		: udp_socket_interface
		, udp_socket_observer
		, boost::enable_shared_from_this<dht_tracker>
	{
		dht_tracker(libtorrent::aux::session_impl& ses, rate_limited_udp_socket& sock
			, dht_settings const& settings, counters& cnt, entry const* state = 0);
		virtual ~dht_tracker();

		void start(entry const& bootstrap
			, find_data::nodes_callback const& f);
		void stop();

		void add_node(udp::endpoint node);
		void add_node(std::pair<std::string, int> const& node);
		void add_router_node(udp::endpoint const& node);

		entry state() const;

		enum flags_t { flag_seed = 1, flag_implied_port = 2 };
		void announce(sha1_hash const& ih, int listen_port, int flags
			, boost::function<void(std::vector<tcp::endpoint> const&)> f);

		void get_item(sha1_hash const& target
			, boost::function<void(item const&)> cb);

		// key is a 32-byte binary string, the public key to look up.
		// the salt is optional
		void get_item(char const* key
			, boost::function<void(item const&)> cb
			, std::string salt = std::string());

		void put_item(entry data
			, boost::function<void()> cb);

		void put_item(char const* key
			, boost::function<void(item&)> cb, std::string salt = std::string());

#ifndef TORRENT_NO_DEPRECATE
		void dht_status(session_status& s);
#endif
		void dht_status(std::vector<dht_routing_bucket>& table
			, std::vector<dht_lookup>& requests);

		// translate bittorrent kademlia message into the generic kademlia message
		// used by the library
		virtual bool incoming_packet(error_code const& ec
			, udp::endpoint const&, char const* buf, int size);

	private:
	
		boost::shared_ptr<dht_tracker> self()
		{ return shared_from_this(); }

		void on_name_lookup(error_code const& e
			, udp::resolver::iterator host);
		void on_router_name_lookup(error_code const& e
			, udp::resolver::iterator host);
		void connection_timeout(error_code const& e);
		void refresh_timeout(error_code const& e);
		void tick(error_code const& e);

		// implements udp_socket_interface
		virtual bool has_quota();
		virtual bool send_packet(libtorrent::entry& e, udp::endpoint const& addr
			, int send_flags);

		// this is the bdecode_node DHT messages are parsed into. It's a member
		// in order to avoid having to deallocate and re-allocate it for every
		// message.
		bdecode_node m_msg;

		counters& m_counters;
		node_impl m_dht;
		rate_limited_udp_socket& m_sock;

		std::vector<char> m_send_buf;
		dos_blocker m_blocker;

		time_point m_last_new_key;
		deadline_timer m_timer;
		deadline_timer m_connection_timer;
		deadline_timer m_refresh_timer;
		dht_settings const& m_settings;
		int m_refresh_bucket;

		bool m_abort;

		// used to resolve hostnames for nodes
		udp::resolver m_host_resolver;
	};
}}

#endif
#endif
