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

#include <algorithm>
#include <vector>
#include <set>
#include <list>
#include <stdarg.h> // for va_start, va_end

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/uncork_interface.hpp"
#include "libtorrent/linked_list.hpp"
#include "libtorrent/torrent_peer.hpp"
#include "libtorrent/torrent_peer_allocator.hpp"
#include "libtorrent/performance_counters.hpp" // for counters

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#if TORRENT_HAS_BOOST_UNORDERED
#include <boost/unordered_map.hpp>
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/session.hpp" // for user_load_function_t
#include "libtorrent/ip_voter.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/piece_block_progress.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/config.hpp"
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
#include "libtorrent/thread.hpp"
#include "libtorrent/alert_manager.hpp" // for alert_manager
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/socket_io.hpp" // for print_address
#include "libtorrent/address.hpp"
#include "libtorrent/utp_socket_manager.hpp"
#include "libtorrent/bloom_filter.hpp"
#include "libtorrent/rss.hpp"
#include "libtorrent/peer_class.hpp"
#include "libtorrent/disk_io_job.hpp" // block_cache_reference
#include "libtorrent/network_thread_pool.hpp"
#include "libtorrent/peer_class_type_filter.hpp"
#include "libtorrent/alert_dispatcher.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/resolver.hpp"

#if TORRENT_COMPLETE_TYPES_REQUIRED
#include "libtorrent/peer_connection.hpp"
#endif

#ifdef TORRENT_USE_OPENSSL
#include <boost/asio/ssl/context.hpp>
#endif

namespace libtorrent
{

	struct plugin;
	class upnp;
	class natpmp;
	class lsd;
	class torrent;
	class alert;
	struct cache_info;

	namespace dht
	{
		struct dht_tracker;
		class item;
	}

	struct bencode_map_entry;

	struct listen_socket_t
	{
		listen_socket_t(): external_port(0), ssl(false) {}

		// this is typically empty but can be set
		// to the WAN IP address of NAT-PMP or UPnP router
		address external_address;

		// this is typically set to the same as the local
		// listen port. In case a NAT port forward was
		// successfully opened, this will be set to the
		// port that is open on the external (NAT) interface
		// on the NAT box itself. This is the port that has
		// to be published to peers, since this is the port
		// the client is reachable through.
		int external_port;

		// set to true if this is an SSL listen socket
		bool ssl;

		// the actual socket
		boost::shared_ptr<socket_acceptor> sock;
	};

	namespace aux
	{
		struct session_impl;
		struct session_settings;

#if defined TORRENT_LOGGING
		struct tracker_logger;
#endif

		// used to initialize the g_current_time before
		// anything else
		struct initialize_timer
		{
			initialize_timer();
		};

		TORRENT_EXPORT std::pair<bencode_map_entry*, int> settings_map();

		// this is the link between the main thread and the
		// thread started to run the main downloader loop
		struct TORRENT_EXTRA_EXPORT session_impl
			: session_interface
			, alert_dispatcher
			, dht::dht_observer
			, boost::noncopyable
			, initialize_timer
			, udp_socket_observer
			, uncork_interface
			, single_threaded
		{
			// the size of each allocation that is chained in the send buffer
			enum { send_buffer_size_impl = 128 };

#ifdef TORRENT_DEBUG
			friend class ::libtorrent::peer_connection;
#endif
			friend struct checker_impl;
			friend class invariant_access;
			typedef std::set<boost::shared_ptr<peer_connection> > connection_map;
#if TORRENT_HAS_BOOST_UNORDERED
			typedef boost::unordered_map<sha1_hash, boost::shared_ptr<torrent> > torrent_map;
#else
			typedef std::map<sha1_hash, boost::shared_ptr<torrent> > torrent_map;
#endif

			session_impl();
			virtual ~session_impl();

			void init();
			void start_session(settings_pack const& pack);

			void set_load_function(user_load_function_t fun)
			{ m_user_load_torrent = fun; }

			void queue_async_resume_data(boost::shared_ptr<torrent> const& t);
			void done_async_resume();
			void async_resume_dispatched(int num_popped_resume);

			void init_peer_class_filter(bool unlimited_local);

#ifndef TORRENT_DISABLE_EXTENSIONS
			void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(
				torrent*, void*)> ext);
			void add_ses_extension(boost::shared_ptr<plugin> ext);
#endif
#if TORRENT_USE_ASSERTS
			bool has_peer(peer_connection const* p) const;
			bool any_torrent_has_peer(peer_connection const* p) const;
			bool is_single_thread() const { return single_threaded::is_single_thread(); }
			bool is_posting_torrent_updates() const { return m_posting_torrent_updates; }
			// this is set while the session is building the
			// torrent status update message
			bool m_posting_torrent_updates;
#endif

			void main_thread();

			void open_listen_port();
			void init_settings();
			
			torrent_peer_allocator_interface* get_peer_allocator() { return &m_peer_allocator; }

			io_service& get_io_service() { return m_io_service; }
			resolver_interface& get_resolver() { return m_host_resolver; }
			void async_resolve(std::string const& host, int flags
				, callback_t const& h);

			std::vector<torrent*>& torrent_list(int i)
			{
				TORRENT_ASSERT(i >= 0);
				TORRENT_ASSERT(i < session_interface::num_torrent_lists);
				return m_torrent_lists[i];
			}

			// prioritize this torrent to be allocated some connection
			// attempts, because this torrent needs more peers.
			// this is typically done when a torrent starts out and
			// need the initial push to connect peers
			void prioritize_connections(boost::weak_ptr<torrent> t);

			// if we are listening on an IPv6 interface
			// this will return one of the IPv6 addresses on this
			// machine, otherwise just an empty endpoint
			tcp::endpoint get_ipv6_interface() const;
			tcp::endpoint get_ipv4_interface() const;

			void async_accept(boost::shared_ptr<socket_acceptor> const& listener, bool ssl);
			void on_accept_connection(boost::shared_ptr<socket_type> const& s
				, boost::weak_ptr<socket_acceptor> listener, error_code const& e, bool ssl);
			void on_socks_accept(boost::shared_ptr<socket_type> const& s
				, error_code const& e);

			void incoming_connection(boost::shared_ptr<socket_type> const& s);
		
#ifndef TORRENT_NO_DEPRECATE
			feed_handle add_feed(feed_settings const& feed);
			void remove_feed(feed_handle h);
			void get_feeds(std::vector<feed_handle>* f) const;
#endif

			boost::weak_ptr<torrent> find_torrent(sha1_hash const& info_hash) const;
			boost::weak_ptr<torrent> find_torrent(std::string const& uuid) const;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
			std::vector<boost::shared_ptr<torrent> > find_collection(
				std::string const& collection) const;
#endif
			boost::weak_ptr<torrent> find_disconnect_candidate_torrent() const;
			int num_torrents() const { return m_torrents.size(); }

			void insert_torrent(sha1_hash const& ih, boost::shared_ptr<torrent> const& t
				, std::string uuid);
			void insert_uuid_torrent(std::string uuid, boost::shared_ptr<torrent> const& t)
			{ m_uuids.insert(std::make_pair(uuid, t)); }
			boost::shared_ptr<torrent> delay_load_torrent(sha1_hash const& info_hash
				, peer_connection* pc);
			void set_queue_position(torrent* t, int p);

			peer_id const& get_peer_id() const { return m_peer_id; }

			void close_connection(peer_connection* p, error_code const& ec);

#ifndef TORRENT_NO_DEPRECATE
			void set_settings(libtorrent::session_settings const& s);
			libtorrent::session_settings deprecated_settings() const;
#endif

			void apply_settings_pack(settings_pack* pack);
			session_settings const& settings() const { return m_settings; }

#ifndef TORRENT_DISABLE_DHT	
			dht::dht_tracker* dht() { return m_dht.get(); }
			bool announce_dht() const { return !m_listen_sockets.empty(); }

			void add_dht_node_name(std::pair<std::string, int> const& node);
			void add_dht_node(udp::endpoint n);
			void add_dht_router(std::pair<std::string, int> const& node);
			void set_dht_settings(dht_settings const& s);
			dht_settings const& get_dht_settings() const { return m_dht_settings; }
			void start_dht();
			void stop_dht();
			void start_dht(entry const& startup_state);
			bool has_dht() const;

			// this is called for torrents when they are started
			// it will prioritize them for announcing to
			// the DHT, to get the initial peers quickly
			void prioritize_dht(boost::weak_ptr<torrent> t);

			void get_immutable_callback(sha1_hash target
				, dht::item const& i);
			void get_mutable_callback(dht::item const& i);

			void dht_get_immutable_item(sha1_hash const& target);

			void dht_get_mutable_item(boost::array<char, 32> key
				, std::string salt = std::string());

			void dht_put_item(entry data, sha1_hash target);

			void dht_put_mutable_item(boost::array<char, 32> key
				, boost::function<void(entry&, boost::array<char,64>&
					, boost::uint64_t&, std::string const&)> cb
				, std::string salt = std::string());

#ifndef TORRENT_NO_DEPRECATE
			entry dht_state() const;
#endif
			void on_dht_announce(error_code const& e);
			void on_dht_router_name_lookup(error_code const& e
				, std::vector<address> const& addresses, int port);
#endif

			void maybe_update_udp_mapping(int nat, int local_port, int external_port);

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
			torrent const* find_encrypted_torrent(
				sha1_hash const& info_hash, sha1_hash const& xor_mask);

			void add_obfuscated_hash(sha1_hash const& obfuscated, boost::weak_ptr<torrent> const& t);
#endif

			void on_port_map_log(char const* msg, int map_transport);

			void on_lsd_announce(error_code const& e);
#if defined TORRENT_LOGGING
			void on_lsd_log(char const* log);
#endif

			// called when a port mapping is successful, or a router returns
			// a failure to map a port
			void on_port_mapping(int mapping, address const& ip, int port
				, error_code const& ec, int nat_transport);

			bool is_aborted() const { return m_abort; }
			bool is_paused() const { return m_paused; }

			void pause();
			void resume();

			void set_ip_filter(ip_filter const& f);
			ip_filter& get_ip_filter();
			
			void set_port_filter(port_filter const& f);
			port_filter const& get_port_filter() const;

			void queue_tracker_request(tracker_request& req
				, boost::weak_ptr<request_callback> c);

			// ==== peer class operations ====

			// implements session_interface
			void set_peer_classes(peer_class_set* s, address const& a, int st);
			peer_class_pool const& peer_classes() const { return m_classes; }
			peer_class_pool& peer_classes() { return m_classes; }
			bool ignore_unchoke_slots_set(peer_class_set const& set) const;
			int copy_pertinent_channels(peer_class_set const& set
				, int channel, bandwidth_channel** dst, int max);
			int use_quota_overhead(peer_class_set& set, int amount_down, int amount_up);
			bool use_quota_overhead(bandwidth_channel* ch, int channel, int amount);

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
			torrent_handle add_torrent_impl(add_torrent_params const&, error_code& ec);
			void async_add_torrent(add_torrent_params* params);
			void on_async_load_torrent(disk_io_job const* j);

			void remove_torrent(torrent_handle const& h, int options);
			void remove_torrent_impl(boost::shared_ptr<torrent> tptr, int options);

			void get_torrent_status(std::vector<torrent_status>* ret
				, boost::function<bool(torrent_status const&)> const& pred
				, boost::uint32_t flags) const;
			void refresh_torrent_status(std::vector<torrent_status>* ret
				, boost::uint32_t flags) const;
			void post_torrent_updates(boost::uint32_t flags);
			void post_session_stats();
			void post_dht_stats();

			std::vector<torrent_handle> get_torrents() const;
			
			size_t set_alert_queue_size_limit(size_t queue_size_limit_);
			std::auto_ptr<alert> pop_alert();
			void pop_alerts(std::deque<alert*>* alerts);
			void set_alert_dispatch(boost::function<void(std::auto_ptr<alert>)> const&);

			alert const* wait_for_alert(time_duration max_wait);

#ifndef TORRENT_NO_DEPRECATE
			int upload_rate_limit() const;
			int download_rate_limit() const;
			int local_upload_rate_limit() const;
			int local_download_rate_limit() const;

			void set_local_download_rate_limit(int bytes_per_second);
			void set_local_upload_rate_limit(int bytes_per_second);
			void set_download_rate_limit(int bytes_per_second);
			void set_upload_rate_limit(int bytes_per_second);
			void set_max_connections(int limit);
			void set_max_uploads(int limit);

			int max_connections() const;
			int max_uploads() const;
#endif

			bandwidth_manager* get_bandwidth_manager(int channel);

			int upload_rate_limit(peer_class_t c) const;
			int download_rate_limit(peer_class_t c) const;
			void set_upload_rate_limit(peer_class_t c, int limit);
			void set_download_rate_limit(peer_class_t c, int limit);

			void set_rate_limit(peer_class_t c, int channel, int limit);
			int rate_limit(peer_class_t c, int channel) const;

			bool preemptive_unchoke() const;
			int num_uploads() const
			{ return int(m_stats_counters[counters::num_peers_up_unchoked]); }
			int num_connections() const
			{ return m_connections.size(); }

			int peak_up_rate() const { return m_peak_up_rate; }

			void trigger_unchoke()
			{
				TORRENT_ASSERT(is_single_thread());
				m_unchoke_time_scaler = 0;
			}
			void trigger_optimistic_unchoke()
			{
				TORRENT_ASSERT(is_single_thread());
				m_optimistic_unchoke_time_scaler = 0;
			}

#ifndef TORRENT_NO_DEPRECATE
			session_status status() const;
#endif

			void set_peer_id(peer_id const& id);
			void set_key(int key);
			address listen_address() const;
			boost::uint16_t listen_port() const;
			boost::uint16_t ssl_listen_port() const;
			
			alert_manager& alerts() { return m_alerts; }
			disk_interface& disk_thread() { return m_disk_thread; }

			void abort();
			
			torrent_handle find_torrent_handle(sha1_hash const& info_hash);

			void announce_lsd(sha1_hash const& ih, int port, bool broadcast = false);

			void save_state(entry* e, boost::uint32_t flags) const;
			void load_state(bdecode_node const* e);

			bool has_connection(peer_connection* p) const;
			void insert_peer(boost::shared_ptr<peer_connection> const& c);

			proxy_settings proxy() const;

#ifndef TORRENT_DISABLE_DHT
			bool is_dht_running() const { return (m_dht.get() != NULL); }
			int external_udp_port() const { return m_external_udp_port; }
#endif

#if TORRENT_USE_I2P
			char const* i2p_session() const { return m_i2p_conn.session_id(); }
			proxy_settings i2p_proxy() const;

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
			bool load_torrent(torrent* t);

			// bump t to the top of the list of least recently used. i.e.
			// make it the most recently used. This is done every time
			// an action is performed that required the torrent to be
			// loaded, indicating activity
			void bump_torrent(torrent* t, bool back = true);

			// evict torrents until there's space for one new torrent,
			void evict_torrents_except(torrent* ignore);
			void evict_torrent(torrent* t);

			void deferred_submit_jobs();

			char* allocate_buffer();
			torrent_peer* allocate_peer_entry(int type);
			void free_peer_entry(torrent_peer* p);

			void free_buffer(char* buf);
			int send_buffer_size() const { return send_buffer_size_impl; }

			// implements buffer_allocator_interface
			void free_disk_buffer(char* buf);
			char* allocate_disk_buffer(char const* category);
			char* allocate_disk_buffer(bool& exceeded
				, boost::shared_ptr<disk_observer> o
				, char const* category);
			char* async_allocate_disk_buffer(char const* category
				, boost::function<void(char*)> const& handler);
			void reclaim_block(block_cache_reference ref);
	
			bool exceeded_cache_use() const
			{ return m_disk_thread.exceeded_cache_use(); }

			// implements dht_observer
			virtual void set_external_address(address const& ip
				, address const& source);

			void set_external_address(address const& ip
				, int source_type, address const& source);
			virtual external_ip const& external_address() const;

			// used when posting synchronous function
			// calls to session_impl and torrent objects
			mutable libtorrent::mutex mut;
			mutable libtorrent::condition_variable cond;

			// cork a peer and schedule a delayed uncork
			// does nothing if the peer is already corked
			void cork_burst(peer_connection* p);

			// uncork all peers added to the delayed uncork queue
			// implements uncork_interface
			void do_delayed_uncork();

			void post_socket_job(socket_job& j);

			// implements session_interface
			virtual tcp::endpoint bind_outgoing_socket(socket_type& s, address
				const& remote_address, error_code& ec) const;
			virtual bool verify_bound_address(address const& addr, bool utp
				, error_code& ec);

			bool has_lsd() const { return m_lsd.get() != NULL; }

			std::vector<block_info>& block_info_storage() { return m_block_info_storage; }

			libtorrent::utp_socket_manager* utp_socket_manager() { return &m_utp_socket_manager; }
			void inc_boost_connections() { ++m_boost_connections; }

//		private:

			std::vector<torrent*> m_torrent_lists[num_torrent_lists];

			peer_class_pool m_classes;

//		private:

			void submit_disk_jobs();

			// implements alert_dispatcher
			virtual bool post_alert(alert* a);

			void update_proxy();
			void update_i2p_bridge();
			void update_peer_tos();
			void update_user_agent();
			void update_unchoke_limit();
			void update_connection_speed();
			void update_queued_disk_bytes();
			void update_alert_queue_size();
			void update_dht_upload_rate_limit();
			void update_disk_threads();
			void update_network_threads();
			void update_cache_buffer_chunk_size();
			void update_report_web_seed_downloads();
			void trigger_auto_manage();
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

			void on_trigger_auto_manage();
			
			void update_socket_buffer_size();
			void update_dht_announce_interval();
			void update_anonymous_mode();
			void update_force_proxy();
			void update_download_rate();
			void update_upload_rate();
			void update_connections_limit();
#ifndef TORRENT_NO_DEPRECATE
			void update_local_download_rate();
			void update_local_upload_rate();
			void update_rate_limit_utp();
			void update_ignore_rate_limits_on_local_network();
#endif
			void update_alert_mask();

			void on_lsd_peer(tcp::endpoint peer, sha1_hash const& ih);
			void setup_socket_buffers(socket_type& s);

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

			// this is where all active sockets are stored.
			// the selector can sleep while there's no activity on
			// them
			mutable io_service m_io_service;

#ifdef TORRENT_USE_OPENSSL
			// this is a generic SSL context used when talking to
			// unauthenticated HTTPS servers
			asio::ssl::context m_ssl_ctx;
#endif

			// handles delayed alerts
			mutable alert_manager m_alerts;

			// handles disk io requests asynchronously
			// peers have pointers into the disk buffer
			// pool, and must be destructed before this
			// object. The disk thread relies on the file
			// pool object, and must be destructed before
			// m_files. The disk io thread posts completion
			// events to the io service, and needs to be
			// constructed after it.
			disk_io_thread m_disk_thread;

			// a thread pool used for async_write_some calls,
			// to distribute its cost to multiple threads
			std::vector<boost::shared_ptr<network_thread_pool> > m_net_thread_pool;

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
			// bandwidth limits. Local peers are excempted
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
			linked_list m_torrent_lru;
	
			std::map<std::string, boost::shared_ptr<torrent> > m_uuids;

			// when saving resume data for many torrents, torrents are
			// queued up in this list in order to not have too many of them
			// outstanding at any given time, since the resume data may use
			// a lot of memory.
			std::list<boost::shared_ptr<torrent> > m_save_resume_queue;

			// the number of save resume data disk jobs that are currently
			// outstanding
			int m_num_save_resume;

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

			typedef std::list<boost::shared_ptr<torrent> > check_queue_t;

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
			ip_filter m_ip_filter;

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

			// the number of retries we make when binding the
			// listen socket. For each retry the port number
			// is incremented by one
			int m_listen_port_retries;

			// the addresses or device names of the interfaces we are supposed to
			// listen on. if empty, it means that we should let the os decide
			// which interface to listen on
			std::vector<std::pair<std::string, int> > m_listen_interfaces;

			// keep this around until everything uses the list of interfaces
			// instead.
			tcp::endpoint m_listen_interface;

			// the network interfaces outgoing connections are opened through. If
			// there is more then one, they are used in a round-robin fasion
			// each element is a device name or IP address (in string form) and
			// a port number. The port determins which port to bind the listen
			// socket to, and the device or IP determines which network adapter
			// to be used. If no adapter with the specified name exists, the listen
			// socket fails.
			// TODO: should this be renamed m_outgoing_interfaces?
			std::vector<std::string> m_net_interfaces;

			// if we're listening on an IPv6 interface
			// this is one of the non local IPv6 interfaces
			// on this machine
			tcp::endpoint m_ipv6_interface;
			tcp::endpoint m_ipv4_interface;
			
			// since we might be listening on multiple interfaces
			// we might need more than one listen socket
			std::list<listen_socket_t> m_listen_sockets;

#if TORRENT_USE_I2P
			i2p_connection m_i2p_conn;
			boost::shared_ptr<socket_type> m_i2p_listen_socket;
#endif

#ifdef TORRENT_USE_OPENSSL
			boost::asio::ssl::context* ssl_ctx() { return &m_ssl_ctx; } 
			void on_incoming_utp_ssl(boost::shared_ptr<socket_type> const& s);
			void ssl_handshake(error_code const& ec, boost::shared_ptr<socket_type> s);
#endif

			// when as a socks proxy is used for peers, also
			// listen for incoming connections on a socks connection
			boost::shared_ptr<socket_type> m_socks_listen_socket;
			boost::uint16_t m_socks_listen_port;

			// round-robin index into m_net_interfaces
			mutable boost::uint8_t m_interface_index;

			void open_new_incoming_socks_connection();

			enum listen_on_flags_t
			{
				open_ssl_socket = 0x10
			};

			listen_socket_t setup_listener(std::string const& device
				, bool ipv4, int port, int& retries, int flags, error_code& ec);

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
			// is only decresed when the unchoke set
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

			// the index of the torrent that we'll
			// refresh the next time
			int m_next_explicit_cache_torrent;

			// this is a counter of the number of seconds until
			// the next time the read cache is rotated, if we're
			// using an explicit read read cache.
			int m_cache_rotation_timer;

			// the index of the torrent that we'll
			// refresh the next time
			int m_next_suggest_torrent;

			// this is a counter of the number of seconds until
			// the next time the suggest pieces are refreshed
			int m_suggest_timer;

			// statistics gathered from all torrents.
			stat m_stat;

			// implements session_interface
			virtual void sent_bytes(int bytes_payload, int bytes_protocol);
			virtual void received_bytes(int bytes_payload, int bytes_protocol);
			virtual void trancieve_ip_packet(int bytes, bool ipv6);
			virtual void sent_syn(bool ipv6);
			virtual void received_synack(bool ipv6);

			int m_peak_up_rate;
			int m_peak_down_rate;

			void on_tick(error_code const& e);

			void try_connect_more_peers();
			void auto_manage_torrents(std::vector<torrent*>& list
				, int& checking_limit, int& dht_limit, int& tracker_limit
				, int& lsd_limit, int& hard_limit, int type_limit);
			void recalculate_auto_managed_torrents();
			void recalculate_unchoke_slots();
			void recalculate_optimistic_unchoke_slots();

			time_point m_created;
			boost::int64_t session_time() const { return total_seconds(aux::time_now() - m_created); }

			time_point m_last_tick;
			time_point m_last_second_tick;

			// the last time we went through the peers
			// to decide which ones to choke/unchoke
			time_point m_last_choke;

#ifndef TORRENT_NO_DEPRECATE
			// the time when the next rss feed needs updating
			time_point m_next_rss_update;

			// update any rss feeds that need updating and
			// recalculate m_next_rss_update
			void update_rss_feeds();
#endif

			// when outgoing_ports is configured, this is the
			// port we'll bind the next outgoing socket to
			mutable int m_next_port;

#ifndef TORRENT_DISABLE_DHT
			boost::shared_ptr<dht::dht_tracker> m_dht;
			dht_settings m_dht_settings;
			
			// these are used when starting the DHT
			// (and bootstrapping it), and then erased
			std::list<udp::endpoint> m_dht_router_nodes;

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
#endif

			bool incoming_packet(error_code const& ec
				, udp::endpoint const&, char const* buf, int size);

			// see m_external_listen_port. This is the same
			// but for the udp port used by the DHT.
			int m_external_udp_port;

			rate_limited_udp_socket m_udp_socket;
			libtorrent::utp_socket_manager m_utp_socket_manager;

#ifdef TORRENT_USE_OPENSSL
			// used for uTP connectons over SSL
			udp_socket m_ssl_udp_socket;
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
			void remap_tcp_ports(boost::uint32_t mask, int tcp_port, int ssl_port);

			// 0 is natpmp 1 is upnp
			int m_tcp_mapping[2];
			int m_udp_mapping[2];
#ifdef TORRENT_USE_OPENSSL
			int m_ssl_tcp_mapping[2];
			int m_ssl_udp_mapping[2];
#endif

			// the timer used to fire the tick
			deadline_timer m_timer;

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

#ifdef TORRENT_REQUEST_LOGGING
			// used to log all requests from peers
			FILE* m_request_log;
			FILE* get_request_log() { return m_request_log; }
#endif

			counters& stats_counters() { return m_stats_counters; }

			void received_buffer(int size);
			void sent_buffer(int size);

			// each second tick the timer takes a little
			// bit longer than one second to trigger. The
			// extra time it took is accumulated into this
			// counter. Every time it exceeds 1000, torrents
			// will tick their timers 2 seconds instead of one.
			// this keeps the timers more accurate over time
			// as a kind of "leap second" to adjust for the
			// accumulated error
			boost::uint16_t m_tick_residual;

#if defined TORRENT_LOGGING
			virtual void session_log(char const* fmt, ...) const;
			virtual void session_vlog(char const* fmt, va_list& va) const;
			void log_all_torrents(peer_connection* p);

			// this list of tracker loggers serves as tracker_callbacks when
			// shutting down. This list is just here to keep them alive during
			// whe shutting down process
			std::list<boost::shared_ptr<tracker_logger> > m_tracker_loggers;

			FILE* m_request_logger;
#endif

		private:

			// state for keeping track of external IPs
			external_ip m_external_ip;

#ifndef TORRENT_DISABLE_EXTENSIONS
			typedef std::list<boost::shared_ptr<plugin> > ses_extension_list_t;
			ses_extension_list_t m_ses_extensions;
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
			
#ifndef TORRENT_NO_DEPRECATE
			std::vector<boost::shared_ptr<feed> > m_feeds;
#endif

			// this is a list of peer connections who have been
			// corked (i.e. their network socket) and needs to be
			// uncorked at the end of the burst of events. This is
			// here to coalesce the effects of bursts of events
			// into fewer network writes, saving CPU and possibly
			// ending up sending larger network packets
			std::vector<peer_connection*> m_delayed_uncorks;

			// the main working thread
			boost::scoped_ptr<thread> m_thread;

#if TORRENT_USE_ASSERTS && defined BOOST_HAS_PTHREADS
			pthread_t m_network_thread;
#endif
		};
		
#if defined TORRENT_LOGGING
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
			void debug_log(const char* fmt, ...) const;
			session_interface& m_ses;
		};
#endif

	}
}


#endif

