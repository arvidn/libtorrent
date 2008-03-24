/*

Copyright (c) 2006, Arvid Norberg
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
#include <boost/optional.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/detail/atomic_count.hpp>
#include <boost/thread/mutex.hpp>

#include "libtorrent/kademlia/node.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/traversal_algorithm.hpp"
#include "libtorrent/kademlia/packet_iterator.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/session_status.hpp"

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_DECLARE_LOG(dht_tracker);
#endif

	struct dht_tracker;

	TORRENT_EXPORT void intrusive_ptr_add_ref(dht_tracker const*);
	TORRENT_EXPORT void intrusive_ptr_release(dht_tracker const*);	

	struct dht_tracker
	{
		friend void intrusive_ptr_add_ref(dht_tracker const*);
		friend void intrusive_ptr_release(dht_tracker const*);
		dht_tracker(asio::io_service& ios, dht_settings const& settings
			, asio::ip::address listen_interface, entry const& bootstrap);
		void stop();

		void add_node(udp::endpoint node);
		void add_node(std::pair<std::string, int> const& node);
		void add_router_node(std::pair<std::string, int> const& node);

		void rebind(asio::ip::address listen_interface, int listen_port);

		entry state() const;

		void announce(sha1_hash const& ih, int listen_port
			, boost::function<void(std::vector<tcp::endpoint> const&
			, sha1_hash const&)> f);

		void dht_status(session_status& s);

	private:
	
		boost::intrusive_ptr<dht_tracker> self()
		{ return boost::intrusive_ptr<dht_tracker>(this); }

		void on_name_lookup(asio::error_code const& e
			, udp::resolver::iterator host);
		void on_router_name_lookup(asio::error_code const& e
			, udp::resolver::iterator host);
		void connection_timeout(asio::error_code const& e);
		void refresh_timeout(asio::error_code const& e);
		void tick(asio::error_code const& e);

		// translate bittorrent kademlia message into the generic kademlia message
		// used by the library
		void on_receive(asio::error_code const& error, size_t bytes_transferred);
		void on_bootstrap();
		void send_packet(msg const& m);

		asio::strand m_strand;
		asio::ip::udp::socket m_socket;

		node_impl m_dht;

		// this is the index of the receive buffer we are currently receiving to
		// the other buffer is the one containing the last message
		int m_buffer;
		std::vector<char> m_in_buf[2];
		udp::endpoint m_remote_endpoint[2];
		std::vector<char> m_send_buf;

		ptime m_last_new_key;
		deadline_timer m_timer;
		deadline_timer m_connection_timer;
		deadline_timer m_refresh_timer;
		dht_settings const& m_settings;
		int m_refresh_bucket;

		// The mutex is used to abort the dht node
		// it's only used to set m_abort to true
		typedef boost::mutex mutex_t;
		mutable mutex_t m_mutex;
		bool m_abort;

		// used to resolve hostnames for nodes
		udp::resolver m_host_resolver;

		// used to ignore abusive dht nodes
		struct node_ban_entry
		{
			node_ban_entry(): count(0) {}
			udp::endpoint src;
			ptime limit;
			int count;
		};

		enum { num_ban_nodes = 20 };

		node_ban_entry m_ban_nodes[num_ban_nodes];

		// reference counter for intrusive_ptr
		mutable boost::detail::atomic_count m_refs;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		int m_replies_sent[5];
		int m_queries_received[5];
		int m_replies_bytes_sent[5];
		int m_queries_bytes_received[5];
		int m_counter;
		int m_announces;
		int m_failed_announces;

		int m_total_message_input;
		int m_ut_message_input;
		int m_lt_message_input;
		int m_mp_message_input;
		int m_gr_message_input;
		int m_mo_message_input;
		
		int m_total_in_bytes;
		int m_total_out_bytes;
		
		int m_queries_out_bytes;
#endif
	};
}}

#endif
#endif
