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
#include "libtorrent/uncork_interface.hpp"
#include "libtorrent/linked_list.hpp"
#include "libtorrent/torrent_peer.hpp"
#include "libtorrent/torrent_peer_allocator.hpp"
#include "libtorrent/performance_counters.hpp" // for counters

#include <algorithm>
#include <vector>
#include <set>
#include <list>
#include <condition_variable>
#include <mutex>
#include <cstdarg> // for va_start, va_end
#include <unordered_map>

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
#include "libtorrent/session_settings.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/bandwidth_manager.hpp"
#include "libtorrent/socket_type.hpp"
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
#include "libtorrent/resolver.hpp"
#include "libtorrent/invariant_check.hpp"

#if TORRENT_COMPLETE_TYPES_REQUIRED
#include "libtorrent/peer_connection.hpp"
#endif

namespace libtorrent
{

	struct plugin;
	struct upnp;
	struct natpmp;
	class lsd;
	class torrent;
	class alert;
	struct cache_info;
	struct torrent_handle;

	namespace dht
	{
		struct dht_tracker;
		class item;
	}

	struct bencode_map_entry;

	typedef boost::function<bool(udp::endpoint const& source
		, bdecode_node const& request, entry& response)> dht_extension_handler_t;

	struct listen_socket_t
	{
		listen_socket_t()
			: tcp_external_port(0)
			, udp_external_port(0)
			, ssl(false)
			, udp_write_blocked(false)
		{
			tcp_port_mapping[0] = -1;
			tcp_port_mapping[1] = -1;
			udp_port_mapping[0] = -1;
			udp_port_mapping[1] = -1;
		}

		// this is typically empty but can be set
		// to the WAN IP address of NAT-PMP or UPnP router
		address external_address;

		// this is a cached local endpoint for the listen TCP socket
		tcp::endpoint local_endpoint;

		// this is typically set to the same as the local
		// listen port. In case a NAT port forward was
		// successfully opened, this will be set to the
		// port that is open on the external (NAT) interface
		// on the NAT box itself. This is the port that has
		// to be published to peers, since this is the port
		// the client is reachable through.
		int tcp_external_port;
		int udp_external_port;

		// 0 is natpmp 1 is upnp
		int tcp_port_mapping[2];
		int udp_port_mapping[2];

		// set to true if this is an SSL listen socket
		bool ssl;

		// this is true when the udp socket send() has failed with EAGAIN or
		// EWOULDBLOCK. i.e. we're currently waiting for the socket to become
		// writeable again. Once it is, we'll set it to false and notify the utp
		// socket manager
		bool udp_write_blocked;

		// the actual sockets (TCP listen socket and UDP socket)
		// An entry does not necessarily have a UDP or TCP socket. One of these
		// pointers may be null!
		// These must be shared_ptr to avoid a dangling reference if an
		// incoming packet is in the event queue when the socket is erased
		boost::shared_ptr<tcp::acceptor> sock;
		boost::shared_ptr<udp_socket> udp_sock;
	};

	namespace aux
	{
		struct session_impl;
		struct session_settings;

#ifndef TORRENT_DISABLE_LOGGING
		struct tracker_logger;
#endif

		TORRENT_EXPORT std::pair<bencode_map_entry*, int> settings_map();

		// this is the link between the main thread and the
		// thread started to run the main downloader loop
		struct TORRENT_EXTRA_EXPORT session_impl final
			: session_interface
			, dht::dht_observer
			, boost::noncopyable
			, uncork_interface
			, single_threaded
		{
			// the size of each allocation that is chained in the send buffer
			enum { send_buffer_size_impl = 128 };
			// maximum length of query names which can be registered by extensions
			enum { max_dht_query_length = 15 };

#if TORRENT_USE_INVARIANT_CHECKS
			friend class libtorrent::invariant_access;
#endif
			typedef std::set<boost::shared_ptr<peer_connection> > connection_map;
			typedef std::unordered_map<sha1_hash, boost::shared_ptr<torrent> > torrent_map;

			session_impl(io_service& ios);
			virtual ~session_impl();

			void start_session(settings_pack const& pack);

			void set_load_function(user_load_function_t fun)
			{ m_user_load_torrent = fun; }

			void init_peer_class_filter(bool unlimited_local);

#ifndef TORRENT_DISABLE_EXTENSIONS
			void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(
				torrent_handle const&, void*)> ext);
			void add_ses_extension(boost::shared_ptr<plugin> ext);
#endif
#if TORRENT_USE_ASSERTS
			bool has_peer(peer_connection const* p) const override;
			bool any_torrent_has_peer(peer_connection const* p) const override;
			bool is_single_thread() const override { return single_threaded::is_single_thread(); }
			bool is_posting_torrent_updates() const override { return m_posting_torrent_updates; }
			// this is set while the session is building the
			// torrent status update message
			bool m_posting_torrent_updates;
#endif

			void reopen_listen_sockets();

			torrent_peer_allocator_interface* get_peer_allocator() override
			{ return &m_peer_allocator; }

			io_service& get_io_service() override { return m_io_service; }
			resolver_interface& get_resolver() override { return m_host_resolver; }
			void async_resolve(std::string const& host, int flags
				, callback_t const& h) override;

			std::vector<torrent*>& torrent_list(int i) override
			{
				TORRENT_ASSERT(i >= 0);
				TORRENT_ASSERT(i < session_interface::num_torrent_lists);
				return m_torrent_lists[i];
			}

			// prioritize this torrent to be allocated some connection
			// attempts, because this torrent needs more peers.
			// this is typically done when a torrent starts out and
			// need the initial push to connect peers
			void prioritize_connections(boost::weak_ptr<torrent> t) override;

			tcp::endpoint get_ipv6_interface() const override;
			tcp::endpoint get_ipv4_interface() const override;

			void async_accept(boost::shared_ptr<tcp::acceptor> const& listener, bool ssl);
			void on_accept_connection(boost::shared_ptr<socket_type> const& s
				, boost::weak_ptr<tcp::acceptor> listener, error_code const& e, bool ssl);
			void on_socks_listen(boost::shared_ptr<socket_type> const& s
				, error_code const& e);
			void on_socks_accept(boost::shared_ptr<socket_type> const& s
				, error_code const& e);

			void incoming_connection(boost::shared_ptr<socket_type> const& s);

			boost::weak_ptr<torrent> find_torrent(sha1_hash const& info_hash) const override;
#ifndef TORRENT_NO_DEPRECATE
			//deprecated in 1.2
			boost::weak_ptr<torrent> find_torrent(std::string const& uuid) const;
#endif
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
			std::vector<boost::shared_ptr<torrent> > find_collection(
				std::string const& collection) const override;
#endif
			boost::weak_ptr<torrent> find_disconnect_candidate_torrent() const override;
			int num_torrents() const override { return int(m_torrents.size()); }

			void insert_torrent(sha1_hash const& ih, boost::shared_ptr<torrent> const& t
				, std::string uuid) override;
#ifndef TORRENT_NO_DEPRECATE
			//deprecated in 1.2
			void insert_uuid_torrent(std::string uuid, boost::shared_ptr<torrent> const& t) override
			{ m_uuids.insert(std::make_pair(uuid, t)); }
#endif
			boost::shared_ptr<torrent> delay_load_torrent(sha1_hash const& info_hash
				, peer_connection* pc) override;
			void set_queue_position(torrent* t, int p) override;

			peer_id const& get_peer_id() const override { return m_peer_id; }

			void close_connection(peer_connection* p, error_code const& ec) override;

#ifndef TORRENT_NO_DEPRECATE
			void set_settings(libtorrent::session_settings const& s);
			libtorrent::session_settings deprecated_settings() const;
#endif

			void apply_settings_pack(boost::shared_ptr<settings_pack> pack) override;
			void apply_settings_pack_impl(settings_pack const& pack);
			session_settings const& settings() const override { return m_settings; }
			settings_pack get_settings() const;

#ifndef TORRENT_DISABLE_DHT
			dht::dht_tracker* dht() override { return m_dht.get(); }
			bool announce_dht() const override { return !m_listen_sockets.empty(); }

			void add_dht_node_name(std::pair<std::string, int> const& node);
			void add_dht_node(udp::endpoint n) override;
			void add_dht_router(std::pair<std::string, int> const& node);
			void set_dht_settings(dht_settings const& s);
			dht_settings const& get_dht_settings() const { return m_dht_settings; }
			void set_dht_storage(dht::dht_storage_constructor_type sc);
			void start_dht();
			void stop_dht();
			void start_dht(entry const& startup_state);
			bool has_dht() const override;

			// this is called for torrents when they are started
			// it will prioritize them for announcing to
			// the DHT, to get the initial peers quickly
			void prioritize_dht(boost::weak_ptr<torrent> t) override;

			void get_immutable_callback(sha1_hash target
				, dht::item const& i);
			void get_mutable_callback(dht::item const& i, bool);

			void dht_get_immutable_item(sha1_hash const& target);

			void dht_get_mutable_item(std::array<char, 32> key
				, std::string salt = std::string());

			void dht_put_immutable_item(entry const& data, sha1_hash target);

			void dht_put_mutable_item(std::array<char, 32> key
				, boost::function<void(entry&, std::array<char,64>&
				, boost::uint64_t&, std::string const&)> cb
				, std::string salt = std::string());

			void dht_get_peers(sha1_hash const& info_hash);
			void dht_announce(sha1_hash const& info_hash, int port = 0, int flags = 0);

			void dht_direct_request(udp::endpoint ep, entry& e
				, void* userdata = 0);

#ifndef TORRENT_NO_DEPRECATE
			entry dht_state() const;
			void start_dht_deprecated(entry const& startup_state);
#endif
			void on_dht_announce(error_code const& e);
			void on_dht_name_lookup(error_code const& e
				, std::vector<address> const& addresses, int port);
			void on_dht_router_name_lookup(error_code const& e
				, std::vector<address> const& addresses, int port);
#endif

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
			torrent const* find_encrypted_torrent(
				sha1_hash const& info_hash, sha1_hash const& xor_mask) override;

			void add_obfuscated_hash(sha1_hash const& obfuscated, boost::weak_ptr<torrent> const& t) override;
#endif

			void on_port_map_log(char const* msg, int map_transport);

			void on_lsd_announce(error_code const& e);
#ifndef TORRENT_DISABLE_LOGGING
			void on_lsd_log(char const* log);
#endif

			// called when a port mapping is successful, or a router returns
			// a failure to map a port
			void on_port_mapping(int mapping, address const& ip, int port
				, int protocol, error_code const& ec, int nat_transport);

			bool is_aborted() const override { return m_abort; }
			bool is_paused() const { return m_paused; }

			void pause();
			void resume();

			void set_ip_filter(boost::shared_ptr<ip_filter> const& f);
			ip_filter const& get_ip_filter();

			void set_port_filter(port_filter const& f);
			port_filter const& get_port_filter() const override;
			void ban_ip(address addr) override;

			void queue_tracker_request(tracker_request& req
				, boost::weak_ptr<request_callback> c) override;

			// ==== peer class operations ====

			// implements session_interface
			void set_peer_classes(peer_class_set* s, address const& a, int st) override;
			peer_class_pool const& peer_classes() const override { return m_classes; }
			peer_class_pool& peer_classes() override { return m_classes; }
			bool ignore_unchoke_slots_set(peer_class_set const& set) const override;
			int copy_pertinent_channels(peer_class_set const& set
				, int channel, bandwidth_channel** dst, int max) override;
			int use_quota_overhead(peer_class_set& set, int amount_down, int amount_up) override;
			bool use_quota_overhead(bandwidth_channel* ch, int amount);

			int create_peer_class(char const* name);
			void delete_peer_class(int cid);
			void set_peer_class_filter(ip_filter const& f);
			ip_filter const& get_peer_class_filter() const;

			void set_peer_class_type_filter(peer_class_type_filter f);
			peer_class_type_filter get_peer_class_type_filter();

			peer_class_info get_peer_class(int cid);
			void set_peer_class(int cid, peer_class_info const& pci);

			bool is_listening() const;

#ifndef TORRENT_DISABLE_EXTENSIONS
			void add_extensions_to_torrent(
				boost::shared_ptr<torrent> const& torrent_ptr, void* userdata);
#endif

			torrent_handle add_torrent(add_torrent_params const&, error_code& ec);
			boost::shared_ptr<torrent> add_torrent_impl(add_torrent_params& p, error_code& ec);
			void async_add_torrent(add_torrent_params* params);
			void on_async_load_torrent(disk_io_job const* j);

			void remove_torrent(torrent_handle const& h, int options) override;
			void remove_torrent_impl(boost::shared_ptr<torrent> tptr, int options) override;

			void get_torrent_status(std::vector<torrent_status>* ret
				, boost::function<bool(torrent_status const&)> const& pred
				, boost::uint32_t flags) const;
			void refresh_torrent_status(std::vector<torrent_status>* ret
				, boost::uint32_t flags) const;
			void post_torrent_updates(boost::uint32_t flags);
			void post_session_stats();
			void post_dht_stats();

			std::vector<torrent_handle> get_torrents() const;

			void pop_alerts(std::vector<alert*>* alerts);
			alert* wait_for_alert(time_duration max_wait);

#ifndef TORRENT_NO_DEPRECATE
			void pop_alerts();
			alert const* pop_alert();
			size_t set_alert_queue_size_limit(size_t queue_size_limit_);
			int upload_rate_limit_depr() const;
			int download_rate_limit_depr() const;
			int local_upload_rate_limit() const;
			int local_download_rate_limit() const;

			void set_local_download_rate_limit(int bytes_per_second);
			void set_local_upload_rate_limit(int bytes_per_second);
			void set_download_rate_limit_depr(int bytes_per_second);
			void set_upload_rate_limit_depr(int bytes_per_second);
			void set_max_connections(int limit);
			void set_max_uploads(int limit);

			int max_connections() const;
			int max_uploads() const;
#endif

			bandwidth_manager* get_bandwidth_manager(int channel) override;

			int upload_rate_limit(peer_class_t c) const;
			int download_rate_limit(peer_class_t c) const;
			void set_upload_rate_limit(peer_class_t c, int limit);
			void set_download_rate_limit(peer_class_t c, int limit);

			void set_rate_limit(peer_class_t c, int channel, int limit);
			int rate_limit(peer_class_t c, int channel) const;

			bool preemptive_unchoke() const override;
			int num_uploads() const override
			{ return int(m_stats_counters[counters::num_peers_up_unchoked]); }
			int num_connections() const override { return int(m_connections.size()); }

			int peak_up_rate() const { return m_peak_up_rate; }

			void trigger_unchoke() override
			{
				TORRENT_ASSERT(is_single_thread());
				m_unchoke_time_scaler = 0;
			}
			void trigger_optimistic_unchoke() override
			{
				TORRENT_ASSERT(is_single_thread());
				m_optimistic_unchoke_time_scaler = 0;
			}

#ifndef TORRENT_NO_DEPRECATE
			session_status status() const;
#endif

			void set_peer_id(peer_id const& id);
			void set_key(int key);
			boost::uint16_t listen_port() const override;
			boost::uint16_t ssl_listen_port() const override;

			alert_manager& alerts() override { return m_alerts; }
			disk_interface& disk_thread() override { return m_disk_thread; }

			void abort();
			void abort_stage2();

			torrent_handle find_torrent_handle(sha1_hash const& info_hash);

			void announce_lsd(sha1_hash const& ih, int port, bool broadcast = false) override;

			void save_state(entry* e, boost::uint32_t flags) const;
			void load_state(bdecode_node const* e, boost::uint32_t flags);

			bool has_connection(peer_connection* p) const override;
			void insert_peer(boost::shared_ptr<peer_connection> const& c) override;

			proxy_settings proxy() const override;

#ifndef TORRENT_DISABLE_DHT
			bool is_dht_running() const { return (m_dht.get() != NULL); }
			int external_udp_port() const override
			{
				for (std::list<listen_socket_t>::const_iterator i = m_listen_sockets.begin()
					, end(m_listen_sockets.end()); i != end; ++i)
				{
					if (i->udp_sock) return i->udp_external_port;
				}
				return -1;
			}
#endif

#if TORRENT_USE_I2P
			char const* i2p_session() const override { return m_i2p_conn.session_id(); }
			proxy_settings i2p_proxy() const override;

			void on_i2p_open(error_code const& ec);
			void open_new_incoming_i2p_connection();
			void on_i2p_accept(boost::shared_ptr<socket_type> const& s
				, error_code const& e);
#endif

			void start_lsd();
			natpmp* start_natpmp();
			upnp* start_upnp();

			void stop_lsd();
			void stop_natpmp();
			void stop_upnp();

			int add_port_mapping(int t, int external_port
				, int local_port);
			void delete_port_mapping(int handle);

			int next_port() const;

			// load the specified torrent, also
			// pick the least recently used torrent and unload it, unless
			// t is the least recently used, then the next least recently
			// used is picked
			// returns true if the torrent was loaded successfully
			bool load_torrent(torrent* t) override;

			// bump t to the top of the list of least recently used. i.e.
			// make it the most recently used. This is done every time
			// an action is performed that required the torrent to be
			// loaded, indicating activity
			void bump_torrent(torrent* t, bool back = true) override;

			// evict torrents until there's space for one new torrent,
			void evict_torrents_except(torrent* ignore);
			void evict_torrent(torrent* t) override;

			void deferred_submit_jobs() override;

			char* allocate_buffer() override;
			torrent_peer* allocate_peer_entry(int type);
			void free_peer_entry(torrent_peer* p);

			void free_buffer(char* buf) override;
			int send_buffer_size() const override { return send_buffer_size_impl; }

			// implements buffer_allocator_interface
			void free_disk_buffer(char* buf) override;
			char* allocate_disk_buffer(char const* category) override;
			char* allocate_disk_buffer(bool& exceeded
				, boost::shared_ptr<disk_observer> o
				, char const* category) override;
			void reclaim_block(block_cache_reference ref) override;

			bool exceeded_cache_use() const
			{ return m_disk_thread.exceeded_cache_use(); }

			// implements dht_observer
			virtual void set_external_address(address const& ip
				, address const& source) override;
			virtual address external_address(udp proto) override;
			virtual void get_peers(sha1_hash const& ih) override;
			virtual void announce(sha1_hash const& ih, address const& addr, int port) override;
			virtual void outgoing_get_peers(sha1_hash const& target
				, sha1_hash const& sent_target, udp::endpoint const& ep) override;

#ifndef TORRENT_DISABLE_LOGGING
			virtual void log(libtorrent::dht::dht_logger::module_t m, char const* fmt, ...)
				override TORRENT_FORMAT(3,4);
			virtual void log_packet(message_direction_t dir, char const* pkt, int len
				, udp::endpoint node) override;
#endif

			virtual bool on_dht_request(char const* query, int query_len
				, dht::msg const& request, entry& response) override;

			void set_external_address(address const& ip
				, int source_type, address const& source) override;
			virtual external_ip const& external_address() const override;

			// used when posting synchronous function
			// calls to session_impl and torrent objects
			mutable std::mutex mut;
			mutable std::condition_variable cond;

			// cork a peer and schedule a delayed uncork
			// does nothing if the peer is already corked
			void cork_burst(peer_connection* p) override;

			// uncork all peers added to the delayed uncork queue
			// implements uncork_interface
			virtual void do_delayed_uncork() override;

			// implements session_interface
			virtual tcp::endpoint bind_outgoing_socket(socket_type& s, address
				const& remote_address, error_code& ec) const override;
			virtual bool verify_bound_address(address const& addr, bool utp
				, error_code& ec) override;

			bool has_lsd() const override { return m_lsd.get() != NULL; }

			std::vector<block_info>& block_info_storage() override { return m_block_info_storage; }

			libtorrent::utp_socket_manager* utp_socket_manager() override
			{ return &m_utp_socket_manager; }
#ifdef TORRENT_USE_OPENSSL
			libtorrent::utp_socket_manager* ssl_utp_socket_manager() override
			{ return &m_ssl_utp_socket_manager; }
#endif

			void inc_boost_connections() override { ++m_boost_connections; }

#ifndef TORRENT_NO_DEPRECATE
			void update_ssl_listen();
			void update_dht_upload_rate_limit();
			void update_local_download_rate();
			void update_local_upload_rate();
			void update_rate_limit_utp();
			void update_ignore_rate_limits_on_local_network();
#endif

			void update_proxy();
			void update_i2p_bridge();
			void update_peer_tos();
			void update_user_agent();
			void update_unchoke_limit();
			void update_connection_speed();
			void update_queued_disk_bytes();
			void update_alert_queue_size();
			void update_disk_threads();
			void update_cache_buffer_chunk_size();
			void update_report_web_seed_downloads();
			void update_outgoing_interfaces();
			void update_listen_interfaces();
			void update_privileged_ports();
			void update_auto_sequential();
			void update_max_failcount();

			void update_upnp();
			void update_natpmp();
			void update_lsd();
			void update_dht();
			void update_count_slow();
			void update_peer_fingerprint();

			void update_socket_buffer_size();
			void update_dht_announce_interval();
			void update_anonymous_mode();
			void update_force_proxy();
			void update_download_rate();
			void update_upload_rate();
			void update_connections_limit();
			void update_alert_mask();

			void trigger_auto_manage() override;

		private:

			// return the settings value for int setting "n", if the value is
			// negative, return INT_MAX
			int get_int_setting(int n) const;

			std::vector<torrent*> m_torrent_lists[num_torrent_lists];

			peer_class_pool m_classes;

			void init(boost::shared_ptr<settings_pack> pack);

			void submit_disk_jobs();

			void on_trigger_auto_manage();

			void on_lsd_peer(tcp::endpoint peer, sha1_hash const& ih);
			void setup_socket_buffers(socket_type& s) override;

			// the settings for the client
			aux::session_settings m_settings;

			counters m_stats_counters;

			// this is a pool allocator for torrent_peer objects
			torrent_peer_allocator m_peer_allocator;

			// this vector is used to store the block_info
			// objects pointed to by partial_piece_info returned
			// by torrent::get_download_queue.
			std::vector<block_info> m_block_info_storage;

#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
			// this pool is used to allocate and recycle send
			// buffers from.
			boost::pool<> m_send_buffers;
#endif

			io_service& m_io_service;

#ifdef TORRENT_USE_OPENSSL
			// this is a generic SSL context used when talking to
			// unauthenticated HTTPS servers
			ssl::context m_ssl_ctx;
#endif

			// handles delayed alerts
			mutable alert_manager m_alerts;

#ifndef TORRENT_NO_DEPRECATE
			// the alert pointers stored in m_alerts
			mutable std::vector<alert*> m_alert_pointers;

			// if not all the alerts in m_alert_pointers have been delivered to
			// the client. This is the offset into m_alert_pointers where the next
			// alert is. If this is greater than or equal to m_alert_pointers.size()
			// it means we need to request new alerts from the main thread.
			mutable int m_alert_pointer_pos;
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
			peer_class_t m_global_class;

			// the peer class all TCP peers belong to by default
			// all tcp peer connections are subject to these
			// bandwidth limits. Local peers are exempted
			// from this limit. The purpose is to be able to
			// throttle TCP that passes over the internet
			// bottleneck (i.e. modem) to avoid starving out
			// uTP connections.
			peer_class_t m_tcp_peer_class;

			// peer class for local peers
			peer_class_t m_local_peer_class;

			tracker_manager m_tracker_manager;
			torrent_map m_torrents;

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
			// this maps obfuscated hashes to torrents. It's only
			// used when encryption is enabled
			torrent_map m_obfuscated_torrents;
#endif

			// this is an LRU for torrents. It's used to determine
			// which torrents should be loaded into RAM and which ones
			// shouldn't. Each torrent that's loaded is part of this
			// list.
			linked_list<torrent> m_torrent_lru;

#ifndef TORRENT_NO_DEPRECATE
			//deprecated in 1.2
			std::map<std::string, boost::shared_ptr<torrent> > m_uuids;
#endif

			// peer connections are put here when disconnected to avoid
			// race conditions with the disk thread. It's important that
			// peer connections are destructed from the network thread,
			// once a peer is disconnected, it's put in this list and
			// every second their refcount is checked, and if it's 1,
			// they are deleted (from the network thread)
			std::vector<boost::shared_ptr<peer_connection> > m_undead_peers;

			// keep the io_service alive until we have posted the job
			// to clear the undead peers
			boost::optional<io_service::work> m_work;

			// this maps sockets to their peer_connection
			// object. It is the complete list of all connected
			// peers.
			connection_map m_connections;

			// this list holds incoming connections while they
			// are performing SSL handshake. When we shut down
			// the session, all of these are disconnected, otherwise
			// they would linger and stall or hang session shutdown
			std::set<boost::shared_ptr<socket_type> > m_incoming_sockets;

			// maps IP ranges to bitfields representing peer class IDs
			// to assign peers matching a specific IP range based on its
			// remote endpoint
			ip_filter m_peer_class_filter;

			// maps socket types to peer classes
			peer_class_type_filter m_peer_class_type_filter;

			// filters incoming connections
			boost::shared_ptr<ip_filter> m_ip_filter;

			// filters outgoing connections
			port_filter m_port_filter;

			// the peer id that is generated at the start of the session
			peer_id m_peer_id;

			// this is the highest queue position of any torrent
			// in this session. queue positions are packed (i.e. there
			// are no gaps). If there are no torrents with queue positions
			// this is -1.
			int m_max_queue_pos;

			// the key is an id that is used to identify the
			// client with the tracker only. It is randomized
			// at startup
			int m_key;

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
			std::list<listen_socket_t> m_listen_sockets;

#if TORRENT_USE_I2P
			i2p_connection m_i2p_conn;
			boost::shared_ptr<socket_type> m_i2p_listen_socket;
#endif

#ifdef TORRENT_USE_OPENSSL
			ssl::context* ssl_ctx() override { return &m_ssl_ctx; }
			void on_incoming_utp_ssl(boost::shared_ptr<socket_type> const& s);
			void ssl_handshake(error_code const& ec, boost::shared_ptr<socket_type> s);
#endif

			// when as a socks proxy is used for peers, also
			// listen for incoming connections on a socks connection
			boost::shared_ptr<socket_type> m_socks_listen_socket;
			boost::uint16_t m_socks_listen_port;

			// round-robin index into m_outgoing_interfaces
			mutable boost::uint8_t m_interface_index;

			void open_new_incoming_socks_connection();

			enum listen_on_flags_t
			{
				open_ssl_socket = 0x10
			};

			listen_socket_t setup_listener(std::string const& device
				, tcp::endpoint bind_ep, int flags, error_code& ec);

#ifndef TORRENT_DISABLE_DHT
			entry m_dht_state;
#endif

			// this is initialized to the unchoke_interval
			// session_setting and decreased every second.
			// when it reaches zero, it is reset to the
			// unchoke_interval and the unchoke set is
			// recomputed.
			// TODO: replace this by a proper asio timer
			int m_unchoke_time_scaler;

			// this is used to decide when to recalculate which
			// torrents to keep queued and which to activate
			// TODO: replace this by a proper asio timer
			int m_auto_manage_time_scaler;

			// works like unchoke_time_scaler but it
			// is only decreased when the unchoke set
			// is recomputed, and when it reaches zero,
			// the optimistic unchoke is moved to another peer.
			// TODO: replace this by a proper asio timer
			int m_optimistic_unchoke_time_scaler;

			// works like unchoke_time_scaler. Each time
			// it reaches 0, and all the connections are
			// used, the worst connection will be disconnected
			// from the torrent with the most peers
			int m_disconnect_time_scaler;

			// when this scaler reaches zero, it will
			// scrape one of the auto managed, paused,
			// torrents.
			int m_auto_scrape_time_scaler;

			// statistics gathered from all torrents.
			stat m_stat;

			// implements session_interface
			virtual void sent_bytes(int bytes_payload, int bytes_protocol) override;
			virtual void received_bytes(int bytes_payload, int bytes_protocol) override;
			virtual void trancieve_ip_packet(int bytes, bool ipv6) override;
			virtual void sent_syn(bool ipv6) override;
			virtual void received_synack(bool ipv6) override;

			int m_peak_up_rate;
			int m_peak_down_rate;

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
			boost::uint16_t session_time() const override
			{
				// +1 is here to make it possible to distinguish uninitialized (to
				// 0) timestamps and timestamps of things that happened during the
				// first second after the session was constructed
				boost::int64_t const ret = total_seconds(aux::time_now()
					- m_created) + 1;
				TORRENT_ASSERT(ret >= 0);
				TORRENT_ASSERT(ret <= (std::numeric_limits<boost::uint16_t>::max)());
				return static_cast<boost::uint16_t>(ret);
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
			mutable int m_next_port;

#ifndef TORRENT_DISABLE_DHT
			std::unique_ptr<dht::dht_storage_interface> m_dht_storage;
			boost::shared_ptr<dht::dht_tracker> m_dht;
			dht_settings m_dht_settings;
			dht::dht_storage_constructor_type m_dht_storage_constructor;

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
			int m_dht_interval_update_torrents;

			// the number of DHT router lookups there are currently outstanding. As
			// long as this is > 0, we'll postpone starting the DHT
			int m_outstanding_router_lookups;
#endif

			void send_udp_packet_hostname(char const* hostname
				, int port
				, array_view<char const> p
				, error_code& ec
				, int flags);

			void send_udp_packet(bool ssl
				, udp::endpoint const& ep
				, array_view<char const> p
				, error_code& ec
				, int flags);

			void on_udp_writeable(boost::weak_ptr<udp_socket> s, error_code const& ec);

			void on_udp_packet(boost::weak_ptr<udp_socket> const& s
				, bool ssl, error_code const& ec);

			libtorrent::utp_socket_manager m_utp_socket_manager;

#ifdef TORRENT_USE_OPENSSL
			// used for uTP connections over SSL
			libtorrent::utp_socket_manager m_ssl_utp_socket_manager;
#endif

			// the number of torrent connection boosts
			// connections that have been made this second
			// this is deducted from the connect speed
			int m_boost_connections;

			boost::shared_ptr<natpmp> m_natpmp;
			boost::shared_ptr<upnp> m_upnp;
			boost::shared_ptr<lsd> m_lsd;

			// mask is a bitmask of which protocols to remap on:
			// 1: NAT-PMP
			// 2: UPnP
			// TODO: 3 perhaps this function should move into listen_socket_t
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

			template <class Handler>
			aux::allocating_handler<Handler, TORRENT_READ_HANDLER_MAX_SIZE>
			make_tick_handler(Handler const& handler)
			{
				return aux::allocating_handler<Handler, TORRENT_READ_HANDLER_MAX_SIZE>(
					handler, m_tick_handler_storage);
			}

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
			std::deque<boost::weak_ptr<torrent> > m_dht_torrents;
#endif

			// torrents prioritized to get connection attempts
			std::deque<std::pair<boost::weak_ptr<torrent>, int> > m_prio_torrents;

			// this announce timer is used
			// by Local service discovery
			deadline_timer m_lsd_announce_timer;

			resolver m_host_resolver;

			// the index of the torrent that will be offered to
			// connect to a peer next time on_tick is called.
			// This implements a round robin peer connections among
			// torrents that want more peers. The index is into
			// m_torrent_lists[torrent_want_peers_downloading]
			// (which is a list of torrent pointers with all
			// torrents that want peers and are downloading)
			int m_next_downloading_connect_torrent;
			int m_next_finished_connect_torrent;

			// this is the number of attempts of connecting to
			// peers we have given to downloading torrents.
			// when this gets high enough, we try to connect
			// a peer from a finished torrent
			int m_download_connect_attempts;

			// index into m_torrent_lists[torrent_want_scrape] referring
			// to the next torrent to auto-scrape
			int m_next_scrape_torrent;

#if TORRENT_USE_INVARIANT_CHECKS
			void check_invariant() const;
#endif

			counters& stats_counters() override { return m_stats_counters; }

			void received_buffer(int size) override;
			void sent_buffer(int size) override;

			// each second tick the timer takes a little
			// bit longer than one second to trigger. The
			// extra time it took is accumulated into this
			// counter. Every time it exceeds 1000, torrents
			// will tick their timers 2 seconds instead of one.
			// this keeps the timers more accurate over time
			// as a kind of "leap second" to adjust for the
			// accumulated error
			boost::uint16_t m_tick_residual;

#ifndef TORRENT_DISABLE_LOGGING
			virtual void session_log(char const* fmt, ...) const override TORRENT_FORMAT(2,3);
			virtual void session_vlog(char const* fmt, va_list& va) const override TORRENT_FORMAT(2,0);

			// this list of tracker loggers serves as tracker_callbacks when
			// shutting down. This list is just here to keep them alive during
			// whe shutting down process
			std::list<boost::shared_ptr<tracker_logger> > m_tracker_loggers;
#endif

			// state for keeping track of external IPs
			external_ip m_external_ip;

#ifndef TORRENT_DISABLE_EXTENSIONS
			// this is a list to allow extensions to potentially remove themselves.
			std::vector<boost::shared_ptr<plugin> > m_ses_extensions;

			// the union of all session extensions' implemented_features(). This is
			// used to exclude callbacks to the session extensions.
			boost::uint32_t m_session_extension_features;

			// std::string could be used for the query names if only all common
			// implementations used SSO *glares at gcc*
			struct extension_dht_query
			{
				boost::uint8_t query_len;
				std::array<char, max_dht_query_length> query;
				dht_extension_handler_t handler;
			};
			typedef std::vector<extension_dht_query> m_extension_dht_queries_t;
			m_extension_dht_queries_t m_extension_dht_queries;
#endif

			// if this function is set, it indicates that torrents are allowed
			// to be unloaded. If it isn't, torrents will never be unloaded
			user_load_function_t m_user_load_torrent;

			// this is true whenever we have posted a deferred-disk job
			// it means we don't need to post another one
			bool m_deferred_submit_disk_jobs;

			// this is set to true when a torrent auto-manage
			// event is triggered, and reset whenever the message
			// is delivered and the auto-manage is executed.
			// there should never be more than a single pending auto-manage
			// message in-flight at any given time.
			bool m_pending_auto_manage;

			// this is also set to true when triggering an auto-manage
			// of the torrents. However, if the normal auto-manage
			// timer comes along and executes the auto-management,
			// this is set to false, which means the triggered event
			// no longer needs to execute the auto-management.
			bool m_need_auto_manage;

			// set to true when the session object
			// is being destructed and the thread
			// should exit
			bool m_abort;

			// is true if the session is paused
			bool m_paused;

			// this is a list of peer connections who have been
			// corked (i.e. their network socket) and needs to be
			// uncorked at the end of the burst of events. This is
			// here to coalesce the effects of bursts of events
			// into fewer network writes, saving CPU and possibly
			// ending up sending larger network packets
			std::vector<peer_connection*> m_delayed_uncorks;
		};

#ifndef TORRENT_DISABLE_LOGGING
		struct tracker_logger : request_callback
		{
			tracker_logger(session_interface& ses);
			void tracker_warning(tracker_request const& req
				, std::string const& str);
			void tracker_response(tracker_request const&
				, libtorrent::address const& tracker_ip
				, std::list<address> const& ip_list
				, struct tracker_response const& resp);
			void tracker_request_timed_out(
				tracker_request const&);
			void tracker_request_error(tracker_request const& r
				, int response_code, error_code const& ec, const std::string& str
				, int retry_interval);
			void debug_log(const char* fmt, ...) const TORRENT_FORMAT(2,3);
			session_interface& m_ses;
		private:
			// explicitly disallow assignment, to silence msvc warning
			tracker_logger& operator=(tracker_logger const&);
		};
#endif

	}
}


#endif

