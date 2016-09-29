/*

Copyright (c) 2006-2016, Arvid Norberg
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

#ifndef TORRENT_DHT_TRACKER
#define TORRENT_DHT_TRACKER

#include <functional>

#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/dos_blocker.hpp>
#include <libtorrent/kademlia/dht_state.hpp>

#include <libtorrent/socket.hpp>
#include <libtorrent/deadline_timer.hpp>
#include <libtorrent/span.hpp>
#include <libtorrent/io_service.hpp>

namespace libtorrent
{
	struct counters;
	struct dht_settings;
#ifndef TORRENT_NO_DEPRECATE
	struct session_status;
#endif
}

namespace libtorrent { namespace dht
{
	struct TORRENT_EXTRA_EXPORT dht_tracker final
		: udp_socket_interface
		, std::enable_shared_from_this<dht_tracker>
	{
		using send_fun_t = std::function<void(udp::endpoint const&
			, span<char const>, error_code&, int)>;

		dht_tracker(dht_observer* observer
			, io_service& ios
			, send_fun_t const& send_fun
			, dht_settings const& settings
			, counters& cnt
			, dht_storage_interface& storage
			, dht_state state);
		virtual ~dht_tracker();

		void start(find_data::nodes_callback const& f);
		void stop();

		// tell the node to recalculate its node id based on the current
		// understanding of its external address (which may have changed)
		void update_node_id();

		void add_node(udp::endpoint const& node);
		void add_router_node(udp::endpoint const& node);

		dht_state state() const;

		enum flags_t { flag_seed = 1, flag_implied_port = 2 };
		void get_peers(sha1_hash const& ih
			, std::function<void(std::vector<tcp::endpoint> const&)> f);
		void announce(sha1_hash const& ih, int listen_port, int flags
			, std::function<void(std::vector<tcp::endpoint> const&)> f);

		void get_item(sha1_hash const& target
			, std::function<void(item const&)> cb);

		// key is a 32-byte binary string, the public key to look up.
		// the salt is optional
		void get_item(public_key const& key
			, std::function<void(item const&, bool)> cb
			, std::string salt = std::string());

		// for immutable_item.
		// the callback function will be called when put operation is done.
		// the int parameter indicates the success numbers of put operation.
		void put_item(entry const& data
			, std::function<void(int)> cb);

		// for mutable_item.
		// the data_cb will be called when we get authoritative mutable_item,
		// the cb is same as put immutable_item.
		void put_item(public_key const& key
			, std::function<void(item const&, int)> cb
			, std::function<void(item&)> data_cb, std::string salt = std::string());

		// send an arbitrary DHT request directly to a node
		void direct_request(udp::endpoint const& ep, entry& e
			, std::function<void(msg const&)> f);

#ifndef TORRENT_NO_DEPRECATE
		void dht_status(session_status& s);
#endif
		void dht_status(std::vector<dht_routing_bucket>& table
			, std::vector<dht_lookup>& requests);
		void update_stats_counters(counters& c) const;

		void incoming_error(error_code const& ec, udp::endpoint const& ep);
		bool incoming_packet(udp::endpoint const& ep, span<char const> buf);

	private:

		std::shared_ptr<dht_tracker> self()
		{ return shared_from_this(); }

		void connection_timeout(node& n, error_code const& e);
		void refresh_timeout(error_code const& e);
		void refresh_key(error_code const& e);
		void update_storage_node_ids();

		// implements udp_socket_interface
		virtual bool has_quota() override;
		virtual bool send_packet(entry& e, udp::endpoint const& addr) override;

		// this is the bdecode_node DHT messages are parsed into. It's a member
		// in order to avoid having to deallocate and re-allocate it for every
		// message.
		bdecode_node m_msg;

		counters& m_counters;
		dht_storage_interface& m_storage;
		dht_state m_state; // to be used only once
		node m_dht;
#if TORRENT_USE_IPV6
		node m_dht6;
#endif
		send_fun_t m_send_fun;
		dht_logger* m_log;

		std::vector<char> m_send_buf;
		dos_blocker m_blocker;

		deadline_timer m_key_refresh_timer;
		deadline_timer m_connection_timer;
#if TORRENT_USE_IPV6
		deadline_timer m_connection_timer6;
#endif
		deadline_timer m_refresh_timer;
		dht_settings const& m_settings;

		std::map<std::string, node*> m_nodes;

		bool m_abort;

		// used to resolve hostnames for nodes
		udp::resolver m_host_resolver;

		// state for the send rate limit
		int m_send_quota;
		time_point m_last_tick;
	};
}}

#endif
