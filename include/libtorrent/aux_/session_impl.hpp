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

#ifndef TORRENT_SESSION_IMPL_HPP_INCLUDED
#define TORRENT_SESSION_IMPL_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/aux_/session_udp_sockets.hpp"
#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/torrent_peer.hpp"
#include "libtorrent/torrent_peer_allocator.hpp"
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/aux_/allocating_handler.hpp"

#ifdef TORRENT_USE_OPENSSL
#include "libtorrent/ssl_stream.hpp"
#endif

#include "libtorrent/session.hpp" // for user_load_function_t
#include "libtorrent/ip_voter.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/piece_block_progress.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/aux_/ip_notifier.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/bandwidth_manager.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/alert_manager.hpp" // for alert_manager
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/socket_io.hpp" // for print_address
#include "libtorrent/address.hpp"
#include "libtorrent/utp_socket_manager.hpp"
#include "libtorrent/bloom_filter.hpp"
#include "libtorrent/peer_class.hpp"
#include "libtorrent/disk_io_job.hpp" // block_cache_reference
#include "libtorrent/peer_class_type_filter.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/kademlia/dht_state.hpp"
#include "libtorrent/kademlia/announce_flags.hpp"
#include "libtorrent/resolver.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/portmap.hpp"
#include "libtorrent/aux_/lsd.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/span.hpp"

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/session_settings.hpp"
#endif

#if TORRENT_COMPLETE_TYPES_REQUIRED
#include "libtorrent/peer_connection.hpp"
#endif

#include <algorithm>
#include <vector>
#include <set>
#include <list>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <cstdarg> // for va_start, va_end
#include <unordered_map>

namespace libtorrent {

	struct plugin;
	struct upnp;
	struct natpmp;
	struct lsd;
	class torrent;
	class alert;
	struct torrent_handle;

namespace dht {

	struct dht_tracker;
	class item;

}

namespace aux {

	struct session_impl;
	struct session_settings;

#ifndef TORRENT_DISABLE_LOGGING
	struct tracker_logger;
#endif

	using listen_socket_flags_t = flags::bitfield_flag<std::uint8_t, struct listen_socket_flags_tag>;

	struct listen_port_mapping
	{
		port_mapping_t mapping = port_mapping_t{-1};
		int port = 0;
	};

	struct TORRENT_EXTRA_EXPORT listen_socket_t : utp_socket_interface
	{
		// we accept incoming connections on this interface
		static constexpr listen_socket_flags_t accept_incoming = 0_bit;

		// this interface was specified to be just the local network. If this flag
		// is not set, this interface is assumed to have a path to the internet
		// (i.e. have a gateway configured)
		static constexpr listen_socket_flags_t local_network = 1_bit;

		// this interface was expanded from the user requesting to
		// listen on an unspecified address (either IPv4 or IPv6)
		static constexpr listen_socket_flags_t was_expanded = 2_bit;

		// there's a proxy configured, and this is the only one interface
		// representing that one proxy
		static constexpr listen_socket_flags_t proxy = 3_bit;

		listen_socket_t() = default;

		// listen_socket_t should not be copied or moved because
		// references to it are held by the DHT and tracker announce
		// code. That code expects a listen_socket_t to always refer
		// to the same socket. It would be easy to accidentally
		// invalidate that assumption if copying or moving were allowed.
		listen_socket_t(listen_socket_t const&) = delete;
		listen_socket_t(listen_socket_t&&) = delete;
		listen_socket_t& operator=(listen_socket_t const&) = delete;
		listen_socket_t& operator=(listen_socket_t&&) = delete;

		udp::endpoint get_local_endpoint() override
		{
			error_code ec;
			if (udp_sock) return udp_sock->sock.local_endpoint(ec);
			return {local_endpoint.address(), local_endpoint.port()};
		}

		// returns true if this listen socket/interface can reach and be reached
		// by the given address. This is useful to know whether it should be
		// annoucned to a tracker (given the tracker's IP) or whether it should
		// have a SOCKS5 UDP tunnel set up (given the IP of the socks proxy)
		bool can_route(address const&) const;

		// this may be empty but can be set
		// to the WAN IP address of a NAT router
		ip_voter external_address;

		// this is a cached local endpoint for the listen TCP socket
		tcp::endpoint local_endpoint;

		address netmask;

		// the name of the device the socket is bound to, may be empty
		// if the socket is not bound to a device
		std::string device;

		// this is the port that was originally specified to listen on it may be
		// different from local_endpoint.port() if we had to retry binding with a
		// higher port
		int original_port = 0;

		// tcp_external_port and udp_external_port return the port which
		// should be published to peers/trackers for this socket
		// If there are active NAT mappings the return value will be
		// the external port returned by the NAT router, otherwise the
		// local listen port is returned
		int tcp_external_port()
		{
			for (auto const& m : tcp_port_mapping)
			{
				if (m.port != 0) return m.port;
			}
			return local_endpoint.port();
		}

		int udp_external_port()
		{
			for (auto const& m : udp_port_mapping)
			{
				if (m.port != 0) return m.port;
			}
			if (udp_sock) return udp_sock->sock.local_port();
			return 0;
		}

		// 0 is natpmp 1 is upnp
		// the order of these arrays determines the priorty in
		// which their ports will be announced to peers
		aux::array<listen_port_mapping, 2, portmap_transport> tcp_port_mapping;
		aux::array<listen_port_mapping, 2, portmap_transport> udp_port_mapping;

		// indicates whether this is an SSL listen socket or not
		transport ssl = transport::plaintext;

		listen_socket_flags_t flags = accept_incoming;

		// the actual sockets (TCP listen socket and UDP socket)
		// An entry does not necessarily have a UDP or TCP socket. One of these
		// pointers may be nullptr!
		// These must be shared_ptr to avoid a dangling reference if an
		// incoming packet is in the event queue when the socket is erased
		// TODO: make these direct members and generate shared_ptrs to them
		// which alias the listen_socket_t shared_ptr
		std::shared_ptr<tcp::acceptor> sock;
		std::shared_ptr<aux::session_udp_socket> udp_sock;

		// since udp packets are expected to be dispatched frequently, this saves
		// time on handler allocation every time we read again.
		aux::handler_storage<TORRENT_READ_HANDLER_MAX_SIZE> udp_handler_storage;

		std::shared_ptr<natpmp> natpmp_mapper;
		std::shared_ptr<upnp> upnp_mapper;

		std::shared_ptr<struct lsd> lsd;

		// set to true when we receive an incoming connection from this listen
		// socket
		bool incoming_connection = false;
	};

		struct TORRENT_EXTRA_EXPORT listen_endpoint_t
		{
			listen_endpoint_t(address const& adr, int p, std::string dev, transport s
				, listen_socket_flags_t f, address const& nmask = address{})
				: addr(adr), netmask(nmask), port(p), device(std::move(dev)), ssl(s), flags(f) {}

			bool operator==(listen_endpoint_t const& o) const
			{
				return addr == o.addr
					&& port == o.port
					&& device == o.device
					&& ssl == o.ssl
					&& flags == o.flags;
			}

			address addr;
			// if this listen endpoint/interface doesn't have a gateway, we cannot
			// route outside of our network, this netmask defines the range of our
			// local network
			address netmask;
			int port;
			std::string device;
			transport ssl;
			listen_socket_flags_t flags;
		};

		// partitions sockets based on whether they match one of the given endpoints
		// all matched sockets are ordered before unmatched sockets
		// matched endpoints are removed from the vector
		// returns an iterator to the first unmatched socket
		TORRENT_EXTRA_EXPORT std::vector<std::shared_ptr<aux::listen_socket_t>>::iterator
		partition_listen_sockets(
			std::vector<listen_endpoint_t>& eps
			, std::vector<std::shared_ptr<aux::listen_socket_t>>& sockets);

		TORRENT_EXTRA_EXPORT void interface_to_endpoints(
			listen_interface_t const& iface
			, listen_socket_flags_t flags
			, span<ip_interface const> const ifs
			, std::vector<listen_endpoint_t>& eps);

		// expand [::] to all IPv6 interfaces for BEP 45 compliance
		TORRENT_EXTRA_EXPORT void expand_unspecified_address(
			span<ip_interface const> ifs
			, span<ip_route const> routes
			, std::vector<listen_endpoint_t>& eps);

		TORRENT_EXTRA_EXPORT void expand_devices(span<ip_interface const>
			, std::vector<listen_endpoint_t>& eps);

		// this is the link between the main thread and the
		// thread started to run the main downloader loop
		struct TORRENT_EXTRA_EXPORT session_impl final
			: session_interface
			, dht::dht_observer
			, aux::portmap_callback
			, aux::lsd_callback
			, boost::noncopyable
			, single_threaded
			, aux::error_handler_interface
			, std::enable_shared_from_this<session_impl>
		{
			// plugin feature-index key map
			enum
			{
				plugins_all_idx = 0, // to store all plugins
				plugins_optimistic_unchoke_idx = 1, // optimistic_unchoke_feature
				plugins_tick_idx = 2, // tick_feature
				plugins_dht_request_idx = 3 // dht_request_feature
			};

			template <typename Fun, typename... Args>
			void wrap(Fun f, Args&&... a);

#if TORRENT_USE_INVARIANT_CHECKS
			friend class libtorrent::invariant_access;
#endif
			using connection_map = std::set<std::shared_ptr<peer_connection>>;
			using torrent_map = std::unordered_map<sha1_hash, std::shared_ptr<torrent>>;

			session_impl(io_service& ios, settings_pack const& pack);
			~session_impl() override;

			void start_session();

			void init_peer_class_filter(bool unlimited_local);

			void call_abort()
			{
				auto self = shared_from_this();
				m_io_service.dispatch(make_handler([self] { self->abort(); }
					, m_abort_handler_storage, *this));
			}

#ifndef TORRENT_DISABLE_EXTENSIONS
			using ext_function_t
				= std::function<std::shared_ptr<torrent_plugin>(torrent_handle const&, void*)>;

			struct session_plugin_wrapper : plugin
			{
				explicit session_plugin_wrapper(ext_function_t f) : m_f(std::move(f)) {}

				std::shared_ptr<torrent_plugin> new_torrent(torrent_handle const& t, void* user) override
				{ return m_f(t, user); }
				ext_function_t m_f;
			};

			void add_extension(std::function<std::shared_ptr<torrent_plugin>(
				torrent_handle const&, void*)> ext);
			void add_ses_extension(std::shared_ptr<plugin> ext);
#endif
#if TORRENT_USE_ASSERTS
			bool has_peer(peer_connection const* p) const override;
			bool any_torrent_has_peer(peer_connection const* p) const override;
			bool is_single_thread() const override { return single_threaded::is_single_thread(); }
			bool is_posting_torrent_updates() const override { return m_posting_torrent_updates; }
			// this is set while the session is building the
			// torrent status update message
			bool m_posting_torrent_updates = false;
			bool verify_queue_position(torrent const* t, queue_position_t pos) override;
#endif

			void on_exception(std::exception const& e) override;
			void on_error(error_code const& ec) override;

			void on_ip_change(error_code const& ec);
			void reopen_listen_sockets(bool map_ports = true);
			void reopen_outgoing_sockets();
			void reopen_network_sockets(reopen_network_flags_t options);

			torrent_peer_allocator_interface& get_peer_allocator() override
			{ return m_peer_allocator; }

			io_service& get_io_service() override { return m_io_service; }
			resolver_interface& get_resolver() override { return m_host_resolver; }

			aux::vector<torrent*>& torrent_list(torrent_list_index_t i) override
			{
				TORRENT_ASSERT(i >= torrent_list_index_t{});
				TORRENT_ASSERT(i < m_torrent_lists.end_index());
				return m_torrent_lists[i];
			}

			// prioritize this torrent to be allocated some connection
			// attempts, because this torrent needs more peers.
			// this is typically done when a torrent starts out and
			// need the initial push to connect peers
			void prioritize_connections(std::weak_ptr<torrent> t) override;

			void async_accept(std::shared_ptr<tcp::acceptor> const& listener, transport ssl);
			void on_accept_connection(std::shared_ptr<socket_type> const& s
				, std::weak_ptr<tcp::acceptor> listener, error_code const& e, transport ssl);

			void incoming_connection(std::shared_ptr<socket_type> const& s);

			std::weak_ptr<torrent> find_torrent(sha1_hash const& info_hash) const override;
#if TORRENT_ABI_VERSION == 1
			//deprecated in 1.2

			TORRENT_DEPRECATED
			void set_load_function(user_load_function_t fun)
			{ m_user_load_torrent = fun; }

			TORRENT_DEPRECATED
			std::weak_ptr<torrent> find_torrent(std::string const& uuid) const;

			TORRENT_DEPRECATED
			void insert_uuid_torrent(std::string uuid, std::shared_ptr<torrent> const& t) override
			{ m_uuids.insert(std::make_pair(uuid, t)); }
#endif
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
			std::vector<std::shared_ptr<torrent>> find_collection(
				std::string const& collection) const override;
#endif
			std::weak_ptr<torrent> find_disconnect_candidate_torrent() const override;
			int num_torrents() const override { return int(m_torrents.size()); }

			void insert_torrent(sha1_hash const& ih, std::shared_ptr<torrent> const& t
#if TORRENT_ABI_VERSION == 1
				, std::string uuid
#endif
			) override;

			std::shared_ptr<torrent> delay_load_torrent(sha1_hash const& info_hash
				, peer_connection* pc) override;
			void set_queue_position(torrent* t, queue_position_t p) override;

			void close_connection(peer_connection* p) noexcept override;

			void apply_settings_pack(std::shared_ptr<settings_pack> pack) override;
			void apply_settings_pack_impl(settings_pack const& pack);
			session_settings const& settings() const override { return m_settings; }
			settings_pack get_settings() const;

#ifndef TORRENT_DISABLE_DHT
			dht::dht_tracker* dht() override { return m_dht.get(); }
			bool announce_dht() const override { return !m_listen_sockets.empty(); }

			void add_dht_node_name(std::pair<std::string, int> const& node);
			void add_dht_node(udp::endpoint const& n) override;
			void add_dht_router(std::pair<std::string, int> const& node);
			void set_dht_settings(dht::dht_settings const& s);
			dht::dht_settings const& get_dht_settings() const { return m_dht_settings; }

			// you must give up ownership of the dht state
			void set_dht_state(dht::dht_state&& state);
			void set_dht_state(dht::dht_state const& state) = delete;

			void set_dht_storage(dht::dht_storage_constructor_type sc);
			void start_dht();
			void stop_dht();
			bool has_dht() const override;

			// this is called for torrents when they are started
			// it will prioritize them for announcing to
			// the DHT, to get the initial peers quickly
			void prioritize_dht(std::weak_ptr<torrent> t) override;

			void get_immutable_callback(sha1_hash target
				, dht::item const& i);
			void get_mutable_callback(dht::item const& i, bool);

			void dht_get_immutable_item(sha1_hash const& target);

			void dht_get_mutable_item(std::array<char, 32> key
				, std::string salt = std::string());

			void dht_put_immutable_item(entry const& data, sha1_hash target);

			void dht_put_mutable_item(std::array<char, 32> key
				, std::function<void(entry&, std::array<char,64>&
					, std::int64_t&, std::string const&)> cb
				, std::string salt = std::string());

			void dht_get_peers(sha1_hash const& info_hash);
			void dht_announce(sha1_hash const& info_hash, int port = 0, dht::announce_flags_t flags = {});

			void dht_live_nodes(sha1_hash const& nid);
			void dht_sample_infohashes(udp::endpoint const& ep, sha1_hash const& target);

			void dht_direct_request(udp::endpoint const& ep, entry& e
				, void* userdata = nullptr);

#if TORRENT_ABI_VERSION == 1
			TORRENT_DEPRECATED
			entry dht_state() const;
			TORRENT_DEPRECATED
			void start_dht_deprecated(entry const& startup_state);
#endif
			void on_dht_announce(error_code const& e);
			void on_dht_name_lookup(error_code const& e
				, std::vector<address> const& addresses, int port);
			void on_dht_router_name_lookup(error_code const& e
				, std::vector<address> const& addresses, int port);
#endif

#if !defined TORRENT_DISABLE_ENCRYPTION
			torrent const* find_encrypted_torrent(
				sha1_hash const& info_hash, sha1_hash const& xor_mask) override;

			void add_obfuscated_hash(sha1_hash const& obfuscated, std::weak_ptr<torrent> const& t) override;
#endif

			void on_lsd_announce(error_code const& e);

			// called when a port mapping is successful, or a router returns
			// a failure to map a port
			void on_port_mapping(port_mapping_t mapping, address const& ip, int port
				, portmap_protocol proto, error_code const& ec
				, portmap_transport transport) override;

			bool is_aborted() const override { return m_abort; }
			bool is_paused() const { return m_paused; }

			void pause();
			void resume();

			void set_ip_filter(std::shared_ptr<ip_filter> const& f);
			ip_filter const& get_ip_filter();

			void set_port_filter(port_filter const& f);
			port_filter const& get_port_filter() const override;
			void ban_ip(address addr) override;

			void queue_tracker_request(tracker_request&& req
				, std::weak_ptr<request_callback> c) override;

			// ==== peer class operations ====

			// implements session_interface
			void set_peer_classes(peer_class_set* s, address const& a, int st) override;
			peer_class_pool const& peer_classes() const override { return m_classes; }
			peer_class_pool& peer_classes() override { return m_classes; }
			bool ignore_unchoke_slots_set(peer_class_set const& set) const override;
			int copy_pertinent_channels(peer_class_set const& set
				, int channel, bandwidth_channel** dst, int m) override;
			int use_quota_overhead(peer_class_set& set, int amount_down, int amount_up) override;
			bool use_quota_overhead(bandwidth_channel* ch, int amount);

			peer_class_t create_peer_class(char const* name);
			void delete_peer_class(peer_class_t cid);
			void set_peer_class_filter(ip_filter const& f);
			ip_filter const& get_peer_class_filter() const;

			void set_peer_class_type_filter(peer_class_type_filter f);
			peer_class_type_filter get_peer_class_type_filter();

			peer_class_info get_peer_class(peer_class_t cid) const;
			void set_peer_class(peer_class_t cid, peer_class_info const& pci);

			bool is_listening() const;

#ifndef TORRENT_DISABLE_EXTENSIONS
			void add_extensions_to_torrent(
				std::shared_ptr<torrent> const& torrent_ptr, void* userdata);
#endif

			// the add_torrent_params object must be moved in
			torrent_handle add_torrent(add_torrent_params&&, error_code& ec);

			// second return value is true if the torrent was added and false if an
			// existing one was found.
			std::pair<std::shared_ptr<torrent>, bool>
			add_torrent_impl(add_torrent_params& p, error_code& ec);
			void async_add_torrent(add_torrent_params* params);

#if TORRENT_ABI_VERSION == 1
			void on_async_load_torrent(add_torrent_params* params, error_code ec);
#endif

			void remove_torrent(torrent_handle const& h, remove_flags_t options) override;
			void remove_torrent_impl(std::shared_ptr<torrent> tptr, remove_flags_t options) override;

			void get_torrent_status(std::vector<torrent_status>* ret
				, std::function<bool(torrent_status const&)> const& pred
				, status_flags_t flags) const;
			void refresh_torrent_status(std::vector<torrent_status>* ret
				, status_flags_t flags) const;
			void post_torrent_updates(status_flags_t flags);
			void post_session_stats();
			void post_dht_stats();

			std::vector<torrent_handle> get_torrents() const;

			void pop_alerts(std::vector<alert*>* alerts);
			alert* wait_for_alert(time_duration max_wait);

#if TORRENT_ABI_VERSION == 1
			TORRENT_DEPRECATED void pop_alerts();
			TORRENT_DEPRECATED alert const* pop_alert();
			TORRENT_DEPRECATED std::size_t set_alert_queue_size_limit(std::size_t queue_size_limit_);
			TORRENT_DEPRECATED int upload_rate_limit_depr() const;
			TORRENT_DEPRECATED int download_rate_limit_depr() const;
			TORRENT_DEPRECATED int local_upload_rate_limit() const;
			TORRENT_DEPRECATED int local_download_rate_limit() const;

			TORRENT_DEPRECATED void set_local_download_rate_limit(int bytes_per_second);
			TORRENT_DEPRECATED void set_local_upload_rate_limit(int bytes_per_second);
			TORRENT_DEPRECATED void set_download_rate_limit_depr(int bytes_per_second);
			TORRENT_DEPRECATED void set_upload_rate_limit_depr(int bytes_per_second);
			TORRENT_DEPRECATED void set_max_connections(int limit);
			TORRENT_DEPRECATED void set_max_uploads(int limit);

			TORRENT_DEPRECATED int max_connections() const;
			TORRENT_DEPRECATED int max_uploads() const;
#endif

			bandwidth_manager* get_bandwidth_manager(int channel) override;

			int upload_rate_limit(peer_class_t c) const;
			int download_rate_limit(peer_class_t c) const;
			void set_upload_rate_limit(peer_class_t c, int limit);
			void set_download_rate_limit(peer_class_t c, int limit);

			void set_rate_limit(peer_class_t c, int channel, int limit);
			int rate_limit(peer_class_t c, int channel) const;

			bool preemptive_unchoke() const override;

			// deprecated, use stats counters ``num_peers_up_unchoked`` instead
			int num_uploads() const override
			{ return int(m_stats_counters[counters::num_peers_up_unchoked]); }

			// deprecated, use stats counters ``num_peers_connected`` +
			// ``num_peers_half_open`` instead.
			int num_connections() const override { return int(m_connections.size()); }

			void trigger_unchoke() noexcept override
			{
				TORRENT_ASSERT(is_single_thread());
				m_unchoke_time_scaler = 0;
			}
			void trigger_optimistic_unchoke() noexcept override
			{
				TORRENT_ASSERT(is_single_thread());
				m_optimistic_unchoke_time_scaler = 0;
			}

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/aux_/disable_warnings_push.hpp"
			session_status status() const;
			peer_id deprecated_get_peer_id() const;
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

			void get_cache_info(torrent_handle h, cache_status* ret, int flags) const;

			std::uint16_t listen_port() const override;
			std::uint16_t listen_port(listen_socket_t* sock) const;
			std::uint16_t ssl_listen_port() const override;
			std::uint16_t ssl_listen_port(listen_socket_t* sock) const;

			// used by the DHT tracker, returns a UDP listen port
			int get_listen_port(transport ssl, aux::listen_socket_handle const& s) override;
			// used by peer connections, returns a TCP listen port
			// or zero if no matching listen socket is found
			int listen_port(transport ssl, address const& local_addr) override;

			void for_each_listen_socket(std::function<void(aux::listen_socket_handle const&)> f) override
			{
				for (auto& s : m_listen_sockets)
				{
					f(listen_socket_handle(s));
				}
			}

			alert_manager& alerts() override { return m_alerts; }
			disk_interface& disk_thread() override { return m_disk_thread; }

			void abort() noexcept;
			void abort_stage2() noexcept;

			torrent_handle find_torrent_handle(sha1_hash const& info_hash);

			void announce_lsd(sha1_hash const& ih, int port) override;

			void save_state(entry* e, save_state_flags_t flags) const;
			void load_state(bdecode_node const* e, save_state_flags_t flags);

			bool has_connection(peer_connection* p) const override;
			void insert_peer(std::shared_ptr<peer_connection> const& c) override;

			proxy_settings proxy() const override;

#ifndef TORRENT_DISABLE_DHT
			bool is_dht_running() const { return (m_dht.get() != nullptr); }
			int external_udp_port(address const& local_address) const override;
#endif

#if TORRENT_USE_I2P
			char const* i2p_session() const override { return m_i2p_conn.session_id(); }
			proxy_settings i2p_proxy() const override;

			void on_i2p_open(error_code const& ec);
			void open_new_incoming_i2p_connection();
			void on_i2p_accept(std::shared_ptr<socket_type> const& s
				, error_code const& e);
#endif

			void start_ip_notifier();
			void start_lsd();
			void start_natpmp();
			void start_upnp();

			void stop_ip_notifier();
			void stop_lsd();
			void stop_natpmp();
			void stop_upnp();

			std::vector<port_mapping_t> add_port_mapping(portmap_protocol t, int external_port
				, int local_port);
			void delete_port_mapping(port_mapping_t handle);

			int next_port() const;

			void deferred_submit_jobs() override;

			// implements dht_observer
			void set_external_address(aux::listen_socket_handle const& iface
				, address const& ip, address const& source) override;
			void get_peers(sha1_hash const& ih) override;
			void announce(sha1_hash const& ih, address const& addr, int port) override;
			void outgoing_get_peers(sha1_hash const& target
				, sha1_hash const& sent_target, udp::endpoint const& ep) override;

#ifndef TORRENT_DISABLE_LOGGING
			bool should_log(module_t m) const override;
			void log(module_t m, char const* fmt, ...)
				override TORRENT_FORMAT(3,4);
			void log_packet(message_direction_t dir, span<char const> pkt
				, udp::endpoint const& node) override;

			bool should_log_portmap(portmap_transport transport) const override;
			void log_portmap(portmap_transport transport, char const* msg) const override;

			bool should_log_lsd() const override;
			void log_lsd(char const* msg) const override;
#endif

			bool on_dht_request(string_view query
				, dht::msg const& request, entry& response) override;

			void set_external_address(tcp::endpoint const& local_endpoint
				, address const& ip
				, ip_source_t source_type, address const& source) override;
			external_ip external_address() const override;

			// used when posting synchronous function
			// calls to session_impl and torrent objects
			mutable std::mutex mut;
			mutable std::condition_variable cond;

			// implements session_interface
			tcp::endpoint bind_outgoing_socket(socket_type& s
				, address const& remote_address, error_code& ec) const override;
			bool verify_incoming_interface(address const& addr);
			bool verify_bound_address(address const& addr, bool utp
				, error_code& ec) override;

			bool has_lsd() const override;

			std::vector<block_info>& block_info_storage() override { return m_block_info_storage; }

			libtorrent::utp_socket_manager* utp_socket_manager() override
			{ return &m_utp_socket_manager; }
#ifdef TORRENT_USE_OPENSSL
			libtorrent::utp_socket_manager* ssl_utp_socket_manager() override
			{ return &m_ssl_utp_socket_manager; }
#endif

			void inc_boost_connections() override
			{
				++m_boost_connections;
				m_stats_counters.inc_stats_counter(counters::boost_connection_attempts);
			}

			// the settings for the client
			aux::session_settings m_settings;

#if TORRENT_ABI_VERSION == 1
			void update_ssl_listen();
			void update_local_download_rate();
			void update_local_upload_rate();
			void update_rate_limit_utp();
			void update_ignore_rate_limits_on_local_network();
#endif

			void update_dht_upload_rate_limit();
			void update_proxy();
			void update_i2p_bridge();
			void update_peer_tos();
			void update_user_agent();
			void update_unchoke_limit();
			void update_connection_speed();
			void update_queued_disk_bytes();
			void update_alert_queue_size();
			void update_disk_threads();
			void update_report_web_seed_downloads();
			void update_outgoing_interfaces();
			void update_listen_interfaces();
			void update_privileged_ports();
			void update_auto_sequential();
			void update_max_failcount();
			void update_resolver_cache_timeout();

			void update_ip_notifier();
			void update_upnp();
			void update_natpmp();
			void update_lsd();
			void update_dht();
			void update_count_slow();
			void update_dht_bootstrap_nodes();
			void update_dht_settings();

			void update_socket_buffer_size();
			void update_dht_announce_interval();
			void update_download_rate();
			void update_upload_rate();
			void update_connections_limit();
			void update_alert_mask();
			void update_validate_https();

			void trigger_auto_manage() override;

		private:

			// return the settings value for int setting "n", if the value is
			// negative, return INT_MAX
			int get_int_setting(int n) const;

			aux::array<aux::vector<torrent*>, num_torrent_lists, torrent_list_index_t>
				m_torrent_lists;

			peer_class_pool m_classes;

			void init();

			void submit_disk_jobs();

			void on_trigger_auto_manage();

			void on_lsd_peer(tcp::endpoint const& peer, sha1_hash const& ih) override;

			void start_natpmp(aux::listen_socket_t& s);
			void start_upnp(aux::listen_socket_t& s);

			void set_external_address(std::shared_ptr<listen_socket_t> const& sock, address const& ip
				, ip_source_t source_type, address const& source);

			counters m_stats_counters;

			// this is a pool allocator for torrent_peer objects
			// torrents and the disk cache (implicitly by holding references to the
			// torrents) depend on this outliving them.
			torrent_peer_allocator m_peer_allocator;

			// this vector is used to store the block_info
			// objects pointed to by partial_piece_info returned
			// by torrent::get_download_queue.
			std::vector<block_info> m_block_info_storage;

			io_service& m_io_service;

#ifdef TORRENT_USE_OPENSSL
			// this is a generic SSL context used when talking to HTTPS servers
			ssl::context m_ssl_ctx;

			// this is the SSL context used for SSL listen sockets. It doesn't
			// verify peers, but it has the servername callback set on it. Once it
			// knows which torrent a peer is connecting to, it will switch the
			// socket over to the torrent specific context, which does verify peers
			ssl::context m_peer_ssl_ctx;
#endif

			// handles delayed alerts
			mutable alert_manager m_alerts;

#if TORRENT_ABI_VERSION == 1
			// the alert pointers stored in m_alerts
			mutable aux::vector<alert*> m_alert_pointers;

			// if not all the alerts in m_alert_pointers have been delivered to
			// the client. This is the offset into m_alert_pointers where the next
			// alert is. If this is greater than or equal to m_alert_pointers.size()
			// it means we need to request new alerts from the main thread.
			mutable int m_alert_pointer_pos = 0;
#endif

			// handles disk io requests asynchronously
			// peers have pointers into the disk buffer
			// pool, and must be destructed before this
			// object. The disk thread relies on the file
			// pool object, and must be destructed before
			// m_files. The disk io thread posts completion
			// events to the io service, and needs to be
			// constructed after it.
			disk_io_thread m_disk_thread;

			// the bandwidth manager is responsible for
			// handing out bandwidth to connections that
			// asks for it, it can also throttle the
			// rate.
			bandwidth_manager m_download_rate;
			bandwidth_manager m_upload_rate;

			// the peer class that all peers belong to by default
			peer_class_t m_global_class{0};

			// the peer class all TCP peers belong to by default
			// all tcp peer connections are subject to these
			// bandwidth limits. Local peers are exempted
			// from this limit. The purpose is to be able to
			// throttle TCP that passes over the internet
			// bottleneck (i.e. modem) to avoid starving out
			// uTP connections.
			peer_class_t m_tcp_peer_class{0};

			// peer class for local peers
			peer_class_t m_local_peer_class{0};

			resolver m_host_resolver;

			tracker_manager m_tracker_manager;

			// the torrents must be destructed after the torrent_peer_allocator,
			// since the torrents hold the peer lists that own the torrent_peers
			// (which are allocated in the torrent_peer_allocator)
			torrent_map m_torrents;

			// all torrents that are downloading or queued,
			// ordered by their queue position
			aux::vector<torrent*, queue_position_t> m_download_queue;

#if !defined TORRENT_DISABLE_ENCRYPTION
			// this maps obfuscated hashes to torrents. It's only
			// used when encryption is enabled
			torrent_map m_obfuscated_torrents;
#endif

#if TORRENT_ABI_VERSION == 1
			//deprecated in 1.2
			std::map<std::string, std::shared_ptr<torrent>> m_uuids;
#endif

			// peer connections are put here when disconnected to avoid
			// race conditions with the disk thread. It's important that
			// peer connections are destructed from the network thread,
			// once a peer is disconnected, it's put in this list and
			// every second their refcount is checked, and if it's 1,
			// they are deleted (from the network thread)
			std::vector<std::shared_ptr<peer_connection>> m_undead_peers;

			// keep the io_service alive until we have posted the job
			// to clear the undead peers
			std::unique_ptr<io_service::work> m_work;

			// this maps sockets to their peer_connection
			// object. It is the complete list of all connected
			// peers.
			connection_map m_connections;

			// this list holds incoming connections while they
			// are performing SSL handshake. When we shut down
			// the session, all of these are disconnected, otherwise
			// they would linger and stall or hang session shutdown
			std::set<std::shared_ptr<socket_type>> m_incoming_sockets;

			// maps IP ranges to bitfields representing peer class IDs
			// to assign peers matching a specific IP range based on its
			// remote endpoint
			ip_filter m_peer_class_filter;

			// maps socket types to peer classes
			peer_class_type_filter m_peer_class_type_filter;

			// filters incoming connections
			std::shared_ptr<ip_filter> m_ip_filter;

			// filters outgoing connections
			port_filter m_port_filter;

			// posts a notification when the set of local IPs changes
			std::unique_ptr<ip_change_notifier> m_ip_notifier;

			// the addresses or device names of the interfaces we are supposed to
			// listen on. if empty, it means that we should let the os decide
			// which interface to listen on
			std::vector<listen_interface_t> m_listen_interfaces;

			// the network interfaces outgoing connections are opened through. If
			// there is more then one, they are used in a round-robin fashion
			// each element is a device name or IP address (in string form) and
			// a port number. The port determines which port to bind the listen
			// socket to, and the device or IP determines which network adapter
			// to be used. If no adapter with the specified name exists, the listen
			// socket fails.
			std::vector<std::string> m_outgoing_interfaces;

			// since we might be listening on multiple interfaces
			// we might need more than one listen socket
			std::vector<std::shared_ptr<listen_socket_t>> m_listen_sockets;

#if TORRENT_USE_I2P
			i2p_connection m_i2p_conn;
			std::shared_ptr<socket_type> m_i2p_listen_socket;
#endif

#ifdef TORRENT_USE_OPENSSL
			ssl::context* ssl_ctx() override { return &m_ssl_ctx; }
			void on_incoming_utp_ssl(std::shared_ptr<socket_type> const& s);
			void ssl_handshake(error_code const& ec, std::shared_ptr<socket_type> s);
#endif

			// round-robin index into m_outgoing_interfaces
			mutable std::uint8_t m_interface_index = 0;

			std::shared_ptr<listen_socket_t> setup_listener(
				listen_endpoint_t const& lep, error_code& ec);

#ifndef TORRENT_DISABLE_DHT
			dht::dht_state m_dht_state;
#endif

			// this is initialized to the unchoke_interval
			// session_setting and decreased every second.
			// when it reaches zero, it is reset to the
			// unchoke_interval and the unchoke set is
			// recomputed.
			// TODO: replace this by a proper asio timer
			int m_unchoke_time_scaler = 0;

			// this is used to decide when to recalculate which
			// torrents to keep queued and which to activate
			// TODO: replace this by a proper asio timer
			int m_auto_manage_time_scaler = 0;

			// works like unchoke_time_scaler but it
			// is only decreased when the unchoke set
			// is recomputed, and when it reaches zero,
			// the optimistic unchoke is moved to another peer.
			// TODO: replace this by a proper asio timer
			int m_optimistic_unchoke_time_scaler = 0;

			// works like unchoke_time_scaler. Each time
			// it reaches 0, and all the connections are
			// used, the worst connection will be disconnected
			// from the torrent with the most peers
			int m_disconnect_time_scaler = 90;

			// when this scaler reaches zero, it will
			// scrape one of the auto managed, paused,
			// torrents.
			int m_auto_scrape_time_scaler = 180;

			// statistics gathered from all torrents.
			stat m_stat;

			// implements session_interface
			void sent_bytes(int bytes_payload, int bytes_protocol) override;
			void received_bytes(int bytes_payload, int bytes_protocol) override;
			void trancieve_ip_packet(int bytes, bool ipv6) override;
			void sent_syn(bool ipv6) override;
			void received_synack(bool ipv6) override;

#if TORRENT_ABI_VERSION == 1
			int m_peak_up_rate = 0;
#endif

			void on_tick(error_code const& e);

			void try_connect_more_peers();
			void auto_manage_checking_torrents(std::vector<torrent*>& list
				, int& limit);
			void auto_manage_torrents(std::vector<torrent*>& list
				, int& dht_limit, int& tracker_limit
				, int& lsd_limit, int& hard_limit, int type_limit);
			void recalculate_auto_managed_torrents();
			void recalculate_unchoke_slots();
			void recalculate_optimistic_unchoke_slots();

			time_point m_created;
			std::uint16_t session_time() const override
			{
				// +1 is here to make it possible to distinguish uninitialized (to
				// 0) timestamps and timestamps of things that happened during the
				// first second after the session was constructed
				std::int64_t const ret = total_seconds(aux::time_now()
					- m_created) + 1;
				TORRENT_ASSERT(ret >= 0);
				if (ret > (std::numeric_limits<std::uint16_t>::max)())
					return (std::numeric_limits<std::uint16_t>::max)();
				return static_cast<std::uint16_t>(ret);
			}
			time_point session_start_time() const override
			{
				return m_created;
			}

			time_point m_last_tick;
			time_point m_last_second_tick;

			// the last time we went through the peers
			// to decide which ones to choke/unchoke
			time_point m_last_choke;

			// the last time we recalculated which torrents should be started
			// and stopped (only the auto managed ones)
			time_point m_last_auto_manage;

			// when outgoing_ports is configured, this is the
			// port we'll bind the next outgoing socket to
			mutable int m_next_port = 0;

#ifndef TORRENT_DISABLE_DHT
			std::unique_ptr<dht::dht_storage_interface> m_dht_storage;
			std::shared_ptr<dht::dht_tracker> m_dht;
			dht::settings m_dht_settings;
			dht::dht_storage_constructor_type m_dht_storage_constructor
				= dht::dht_default_storage_constructor;

			// these are used when starting the DHT
			// (and bootstrapping it), and then erased
			std::vector<udp::endpoint> m_dht_router_nodes;

			// if a DHT node is added when there's no DHT instance, they're stored
			// here until we start the DHT
			std::vector<udp::endpoint> m_dht_nodes;

			// this announce timer is used
			// by the DHT.
			deadline_timer m_dht_announce_timer;

			// the number of torrents there were when the
			// update_dht_announce_interval() was last called.
			// if the number of torrents changes significantly
			// compared to this number, the DHT announce interval
			// is updated again. This especially matters for
			// small numbers.
			int m_dht_interval_update_torrents = 0;

			// the number of DHT router lookups there are currently outstanding. As
			// long as this is > 0, we'll postpone starting the DHT
			int m_outstanding_router_lookups = 0;
#endif

			void send_udp_packet_hostname(std::weak_ptr<utp_socket_interface> sock
				, char const* hostname
				, int port
				, span<char const> p
				, error_code& ec
				, udp_send_flags_t flags);

			void send_udp_packet_hostname_listen(aux::listen_socket_handle const& sock
				, char const* hostname
				, int port
				, span<char const> p
				, error_code& ec
				, udp_send_flags_t const flags)
			{
				listen_socket_t* s = sock.get();
				if (!s)
				{
					ec = boost::asio::error::bad_descriptor;
					return;
				}
				send_udp_packet_hostname(sock.get_ptr(), hostname, port, p, ec, flags);
			}

			void send_udp_packet(std::weak_ptr<utp_socket_interface> sock
				, udp::endpoint const& ep
				, span<char const> p
				, error_code& ec
				, udp_send_flags_t flags);

			void send_udp_packet_listen(aux::listen_socket_handle const& sock
				, udp::endpoint const& ep
				, span<char const> p
				, error_code& ec
				, udp_send_flags_t const flags)
			{
				listen_socket_t* s = sock.get();
				if (!s)
				{
					ec = boost::asio::error::bad_descriptor;
					return;
				}
				send_udp_packet(sock.get_ptr(), ep, p, ec, flags);
			}

			void on_udp_writeable(std::weak_ptr<session_udp_socket> s, error_code const& ec);

			void on_udp_packet(std::weak_ptr<session_udp_socket> s
				, std::weak_ptr<listen_socket_t> ls
				, transport ssl, error_code const& ec);

			libtorrent::utp_socket_manager m_utp_socket_manager;

#ifdef TORRENT_USE_OPENSSL
			// used for uTP connections over SSL
			libtorrent::utp_socket_manager m_ssl_utp_socket_manager;
#endif

			// the number of torrent connection boosts
			// connections that have been made this second
			// this is deducted from the connect speed
			int m_boost_connections = 0;

#if TORRENT_ABI_VERSION == 1
			struct work_thread_t
			{
				work_thread_t()
					: work(new boost::asio::io_service::work(ios))
					, thread([this] { ios.run(); })
				{}
				~work_thread_t()
				{
					work.reset();
					thread.join();
				}
				work_thread_t(work_thread_t const&) = delete;
				work_thread_t& operator=(work_thread_t const&) = delete;

				boost::asio::io_service ios;
				std::unique_ptr<boost::asio::io_service::work> work;
				std::thread thread;
			};
			std::unique_ptr<work_thread_t> m_torrent_load_thread;
#endif

			// mask is a bitmask of which protocols to remap on:
			enum remap_port_mask_t
			{
				remap_natpmp = 1,
				remap_upnp = 2,
				remap_natpmp_and_upnp = 3
			};
			void remap_ports(remap_port_mask_t mask, listen_socket_t& s);

			// the timer used to fire the tick
			deadline_timer m_timer;
			aux::handler_storage<TORRENT_READ_HANDLER_MAX_SIZE> m_tick_handler_storage;

			// abort may not fail and cannot allocate memory
#if defined BOOST_ASIO_ENABLE_HANDLER_TRACKING
			aux::handler_storage<100> m_abort_handler_storage;
#elif defined _M_AMD64
			aux::handler_storage<88> m_abort_handler_storage;
#else
			aux::handler_storage<56> m_abort_handler_storage;
#endif

			// torrents are announced on the local network in a
			// round-robin fashion. All torrents are cycled through
			// within the LSD announce interval (which defaults to
			// 5 minutes)
			torrent_map::iterator m_next_lsd_torrent;

#ifndef TORRENT_DISABLE_DHT
			// torrents are announced on the DHT in a
			// round-robin fashion. All torrents are cycled through
			// within the DHT announce interval (which defaults to
			// 15 minutes)
			torrent_map::iterator m_next_dht_torrent;

			// torrents that don't have any peers
			// when added should be announced to the DHT
			// as soon as possible. Such torrents are put
			// in this queue and get announced the next time
			// the timer fires, instead of the next one in
			// the round-robin sequence.
			std::deque<std::weak_ptr<torrent>> m_dht_torrents;
#endif

			// torrents prioritized to get connection attempts
			std::deque<std::pair<std::weak_ptr<torrent>, int>> m_prio_torrents;

			// this announce timer is used
			// by Local service discovery
			deadline_timer m_lsd_announce_timer;

			// this is the timer used to call ``close_oldest`` on the ``file_pool``
			// object. This closes the file that's been opened the longest every
			// time it's called, to force the windows disk cache to be flushed
			deadline_timer m_close_file_timer;

			// the index of the torrent that will be offered to
			// connect to a peer next time on_tick is called.
			// This implements a round robin peer connections among
			// torrents that want more peers. The index is into
			// m_torrent_lists[torrent_want_peers_downloading]
			// (which is a list of torrent pointers with all
			// torrents that want peers and are downloading)
			int m_next_downloading_connect_torrent = 0;
			int m_next_finished_connect_torrent = 0;

			// this is the number of attempts of connecting to
			// peers we have given to downloading torrents.
			// when this gets high enough, we try to connect
			// a peer from a finished torrent
			int m_download_connect_attempts = 0;

			// index into m_torrent_lists[torrent_want_scrape] referring
			// to the next torrent to auto-scrape
			int m_next_scrape_torrent = 0;

#if TORRENT_USE_INVARIANT_CHECKS
			void check_invariant() const;
#endif

			counters& stats_counters() override { return m_stats_counters; }

			void received_buffer(int size) override;
			void sent_buffer(int size) override;

#ifndef TORRENT_DISABLE_LOGGING
			bool should_log() const override;
			void session_log(char const* fmt, ...) const noexcept override TORRENT_FORMAT(2,3);
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
			// this is a list to allow extensions to potentially remove themselves.
			std::array<std::vector<std::shared_ptr<plugin>>, 4> m_ses_extensions;
#endif

#if TORRENT_ABI_VERSION == 1
			user_load_function_t m_user_load_torrent;
#endif

			// this is true whenever we have posted a deferred-disk job
			// it means we don't need to post another one
			bool m_deferred_submit_disk_jobs = false;

			// this is set to true when a torrent auto-manage
			// event is triggered, and reset whenever the message
			// is delivered and the auto-manage is executed.
			// there should never be more than a single pending auto-manage
			// message in-flight at any given time.
			bool m_pending_auto_manage = false;

			// this is also set to true when triggering an auto-manage
			// of the torrents. However, if the normal auto-manage
			// timer comes along and executes the auto-management,
			// this is set to false, which means the triggered event
			// no longer needs to execute the auto-management.
			bool m_need_auto_manage = false;

			// set to true when the session object
			// is being destructed and the thread
			// should exit
			bool m_abort = false;

			// is true if the session is paused
			bool m_paused = false;

			// set to true the first time post_session_stats() is
			// called and we post the headers alert
			bool m_posted_stats_header = false;
		};

#ifndef TORRENT_DISABLE_LOGGING
		struct tracker_logger : request_callback
		{
			explicit tracker_logger(session_interface& ses);
			void tracker_warning(tracker_request const& req
				, std::string const& str) override;
			void tracker_response(tracker_request const&
				, libtorrent::address const& tracker_ip
				, std::list<address> const& ip_list
				, struct tracker_response const& resp) override;
			void tracker_request_error(tracker_request const& r
				, error_code const& ec, const std::string& str
				, seconds32 retry_interval) override;
			bool should_log() const override;
			void debug_log(const char* fmt, ...) const noexcept override TORRENT_FORMAT(2,3);
			session_interface& m_ses;
		private:
			// explicitly disallow assignment, to silence msvc warning
			tracker_logger& operator=(tracker_logger const&);
		};
#endif

	}
}

#endif
