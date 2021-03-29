/*

Copyright (c) 2006-2008, 2010, 2014-2021, Arvid Norberg
Copyright (c) 2014-2015, 2017, Steven Siloti
Copyright (c) 2015, Thomas Yuan
Copyright (c) 2015-2017, 2020-2021, Alden Torres
Copyright (c) 2020, Fonic
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DHT_TRACKER
#define TORRENT_DHT_TRACKER

#include <functional>

#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/dos_blocker.hpp>
#include <libtorrent/kademlia/dht_state.hpp>

#include <libtorrent/aux_/listen_socket_handle.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/aux_/deadline_timer.hpp>
#include <libtorrent/span.hpp>
#include <libtorrent/io_context.hpp>
#include <libtorrent/aux_/udp_socket.hpp>
#include <libtorrent/entry.hpp>

namespace lt {

	struct counters;
#if TORRENT_ABI_VERSION == 1
	struct session_status;
#endif
namespace aux {
	struct session_settings;
}
}

namespace lt::dht {

	struct TORRENT_EXTRA_EXPORT dht_tracker final
		: socket_manager
		, std::enable_shared_from_this<dht_tracker>
	{
		using send_fun_t = std::function<void(
			aux::listen_socket_handle const&, udp::endpoint const&
			, span<char const>, error_code&, aux::udp_send_flags_t)>;

		dht_tracker(dht_observer* observer
			, io_context& ios
			, send_fun_t send_fun
			, aux::session_settings const& settings
			, counters& cnt
			, dht_storage_interface& storage
			, dht_state&& state);

		// the dht_state must be moved in!
		dht_tracker(dht_observer* observer
			, io_context& ios
			, send_fun_t const& send_fun
			, aux::session_settings const& settings
			, counters& cnt
			, dht_storage_interface& storage
			, dht_state const& state) = delete;

#if defined(_MSC_VER) && _MSC_VER < 1910
		// workaround for a bug in msvc 14.0
		// it attempts to generate a copy constructor for some strange reason
		// and fails because tracker_node is not copyable
		dht_tracker(dht_tracker const&) = delete;
#endif

		void start(find_data::nodes_callback const& f);
		void stop();

		// tell the node to recalculate its node id based on the current
		// understanding of its external address (which may have changed)
		void update_node_id(aux::listen_socket_handle const& s);

		void new_socket(aux::listen_socket_handle const& s);
		void delete_socket(aux::listen_socket_handle const& s);

		void add_node(udp::endpoint const& node);
		void add_router_node(udp::endpoint const& node);

		dht_state state() const;

		void get_peers(sha1_hash const& ih
			, std::function<void(std::vector<tcp::endpoint> const&)> f);
		void announce(sha1_hash const& ih, int listen_port, announce_flags_t flags
			, std::function<void(std::vector<tcp::endpoint> const&)> f);

		void sample_infohashes(udp::endpoint const& ep, sha1_hash const& target
			, std::function<void(node_id
				, time_duration
				, int, std::vector<sha1_hash>
				, std::vector<std::pair<sha1_hash, udp::endpoint>>)> f);

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

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"
		void dht_status(session_status& s);
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif
		std::vector<lt::dht::dht_status> dht_status() const;
		void update_stats_counters(counters& c) const;

		void incoming_error(error_code const& ec, udp::endpoint const& ep);
		bool incoming_packet(aux::listen_socket_handle const& s
			, udp::endpoint const& ep, span<char const> buf);

		std::vector<std::pair<node_id, udp::endpoint>> live_nodes(node_id const& nid);

	private:
		struct tracker_node
		{
			tracker_node(io_context& ios
				, aux::listen_socket_handle const& s, socket_manager* sock
				, aux::session_settings const& settings
				, node_id const& nid
				, dht_observer* observer, counters& cnt
				, get_foreign_node_t get_foreign_node
				, dht_storage_interface& storage);
			tracker_node(tracker_node const&) = delete;
			tracker_node(tracker_node&&) = delete;

			node dht;
			aux::deadline_timer connection_timer;
		};
		using tracker_nodes_t = std::map<aux::listen_socket_handle, tracker_node>;

		std::shared_ptr<dht_tracker> self()
		{ return shared_from_this(); }

		void connection_timeout(aux::listen_socket_handle const& s, error_code const& e);
		void refresh_timeout(error_code const& e);
		void refresh_key(error_code const& e);
		void update_storage_node_ids();
		node* get_node(node_id const& id, string_view family_name);

		// implements socket_manager
		bool has_quota() override;
		bool send_packet(aux::listen_socket_handle const& s, entry& e, udp::endpoint const& addr) override;

		// this is the bdecode_node DHT messages are parsed into. It's a member
		// in order to avoid having to deallocate and re-allocate it for every
		// message.
		bdecode_node m_msg;

		counters& m_counters;
		dht_storage_interface& m_storage;
		dht_state m_state; // to be used only once
		tracker_nodes_t m_nodes;
		send_fun_t m_send_fun;
		dht_observer* m_log;

		std::vector<char> m_send_buf;
		dos_blocker m_blocker;

		aux::deadline_timer m_key_refresh_timer;
		aux::deadline_timer m_refresh_timer;
		aux::session_settings const& m_settings;

		bool m_running;

		// used to resolve hostnames for nodes
		udp::resolver m_host_resolver;

		// state for the send rate limit
		int m_send_quota;
		time_point m_last_tick;

		io_context& m_ioc;
	};
} // namespace lt::dht

#endif
