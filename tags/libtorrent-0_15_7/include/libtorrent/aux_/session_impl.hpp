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

#ifndef TORRENT_DISABLE_GEO_IP
#ifdef WITH_SHIPPED_GEOIP_H
#include "libtorrent/GeoIP.h"
#else
#include <GeoIP.h>
#endif
#endif

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/filesystem/path.hpp>
#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/pool/object_pool.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

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
#include "libtorrent/session.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/bandwidth_manager.hpp"
#include "libtorrent/socket_type.hpp"
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/policy.hpp" // for policy::peer
#include "libtorrent/alert.hpp" // for alert_manager

#if TORRENT_COMPLETE_TYPES_REQUIRED
#include "libtorrent/peer_connection.hpp"
#endif

namespace libtorrent
{

	namespace fs = boost::filesystem;
	class peer_connection;
	class upnp;
	class natpmp;
	class lsd;
	struct fingerprint;
	class torrent;
	class alert;

	namespace dht
	{
		struct dht_tracker;
	}

	namespace aux
	{
		struct session_impl;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		struct tracker_logger;
#endif

		// used to initialize the g_current_time before
		// anything else
		struct initialize_timer
		{
			initialize_timer();
		};

		// this is the link between the main thread and the
		// thread started to run the main downloader loop
		struct session_impl: boost::noncopyable, initialize_timer
		{

			// the size of each allocation that is chained in the send buffer
			enum { send_buffer_size = 128 };

#ifdef TORRENT_DEBUG
			friend class ::libtorrent::peer_connection;
#endif
			friend struct checker_impl;
			friend class invariant_access;
			typedef std::set<boost::intrusive_ptr<peer_connection> > connection_map;
			typedef std::map<sha1_hash, boost::shared_ptr<torrent> > torrent_map;

			session_impl(
				std::pair<int, int> listen_port_range
				, fingerprint const& cl_fprint
				, char const* listen_interface
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
				, fs::path const& logpath
#endif
				);
			~session_impl();

#ifndef TORRENT_DISABLE_EXTENSIONS
			void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(
				torrent*, void*)> ext);
#endif
#ifdef TORRENT_DEBUG
			bool has_peer(peer_connection const* p) const
			{
				return std::find_if(m_connections.begin(), m_connections.end()
					, boost::bind(&boost::intrusive_ptr<peer_connection>::get, _1) == p)
					!= m_connections.end();
			}
#endif
			void operator()();

			void open_listen_port();

			// if we are listening on an IPv6 interface
			// this will return one of the IPv6 addresses on this
			// machine, otherwise just an empty endpoint
			tcp::endpoint get_ipv6_interface() const;
			tcp::endpoint get_ipv4_interface() const;

			void async_accept(boost::shared_ptr<socket_acceptor> const& listener);
			void on_accept_connection(boost::shared_ptr<socket_type> const& s
				, boost::weak_ptr<socket_acceptor> listener, error_code const& e);
			void on_socks_accept(boost::shared_ptr<socket_type> const& s
				, error_code const& e);

			void incoming_connection(boost::shared_ptr<socket_type> const& s);
		
			// must be locked to access the data
			// in this struct
			typedef boost::mutex mutex_t;
			mutable mutex_t m_mutex;

			boost::weak_ptr<torrent> find_torrent(const sha1_hash& info_hash);
			peer_id const& get_peer_id() const { return m_peer_id; }

			void close_connection(peer_connection const* p, error_code const& ec);

			void set_settings(session_settings const& s);
			session_settings const& settings() const { return m_settings; }

#ifndef TORRENT_DISABLE_DHT	
			void add_dht_node(std::pair<std::string, int> const& node);
			void add_dht_node(udp::endpoint n);
			void add_dht_router(std::pair<std::string, int> const& node);
			void set_dht_settings(dht_settings const& s);
			dht_settings const& get_dht_settings() const { return m_dht_settings; }
			void start_dht();
			void stop_dht();
			void start_dht(entry const& startup_state);

#ifndef TORRENT_NO_DEPRECATE
			entry dht_state(session_impl::mutex_t::scoped_lock& l) const;
#endif
			void maybe_update_udp_mapping(int nat, int local_port, int external_port);
			void on_dht_router_name_lookup(error_code const& e
				, tcp::resolver::iterator host);
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
			void set_pe_settings(pe_settings const& settings);
			pe_settings const& get_pe_settings() const { return m_pe_settings; }
#endif

			void on_port_map_log(char const* msg, int map_transport);

			void on_lsd_announce(error_code const& e);

			// called when a port mapping is successful, or a router returns
			// a failure to map a port
			void on_port_mapping(int mapping, int port, error_code const& ec
				, int nat_transport);

			bool is_aborted() const { return m_abort; }
			bool is_paused() const { return m_paused; }

			void pause();
			void resume();

			void set_ip_filter(ip_filter const& f);
			ip_filter const& get_ip_filter() const;
			
			void set_port_filter(port_filter const& f);

			bool listen_on(
				std::pair<int, int> const& port_range
				, const char* net_interface = 0);
			bool is_listening() const;

			torrent_handle add_torrent(add_torrent_params const&, error_code& ec);

			void remove_torrent(torrent_handle const& h, int options);

			std::vector<torrent_handle> get_torrents();
			
			void queue_check_torrent(boost::shared_ptr<torrent> const& t);
			void dequeue_check_torrent(boost::shared_ptr<torrent> const& t);

			void set_alert_mask(boost::uint32_t m);
			size_t set_alert_queue_size_limit(size_t queue_size_limit_);
			std::auto_ptr<alert> pop_alert();
			void set_alert_dispatch(boost::function<void(alert const&)> const&);

			alert const* wait_for_alert(time_duration max_wait);

			int upload_rate_limit() const;
			int download_rate_limit() const;
			int local_upload_rate_limit() const;
			int local_download_rate_limit() const;

			void set_local_download_rate_limit(int bytes_per_second);
			void set_local_upload_rate_limit(int bytes_per_second);

			void set_download_rate_limit(int bytes_per_second);
			void set_upload_rate_limit(int bytes_per_second);
			void set_max_half_open_connections(int limit);
			void set_max_connections(int limit);
			void set_max_uploads(int limit);

			int max_connections() const { return m_max_connections; }
			int max_uploads() const { return m_max_uploads; }
			int max_half_open_connections() const { return m_half_open.limit(); }

			int num_uploads() const { return m_num_unchoked; }
			int num_connections() const
			{ return m_connections.size(); }

			void unchoke_peer(peer_connection& c);
			void choke_peer(peer_connection& c);

			session_status status() const;
			void set_peer_id(peer_id const& id);
			void set_key(int key);
			unsigned short listen_port() const;
			
			void abort();
			
			torrent_handle find_torrent_handle(sha1_hash const& info_hash);

			void announce_lsd(sha1_hash const& ih);

			void save_state(entry& e, boost::uint32_t flags, session_impl::mutex_t::scoped_lock& l) const;
			void load_state(lazy_entry const& e);

			void set_proxy(proxy_settings const& s);
			proxy_settings const& proxy() const { return m_peer_proxy; }

			void set_peer_proxy(proxy_settings const& s)
			{
				m_peer_proxy = s;
				// in case we just set a socks proxy, we might have to
				// open the socks incoming connection
				if (!m_socks_listen_socket) open_new_incoming_socks_connection();
			}
			void set_web_seed_proxy(proxy_settings const& s)
			{ m_web_seed_proxy = s; }
			void set_tracker_proxy(proxy_settings const& s)
			{ m_tracker_proxy = s; }
			proxy_settings const& peer_proxy() const
			{ return m_peer_proxy; }
			proxy_settings const& web_seed_proxy() const
			{ return m_web_seed_proxy; }
			proxy_settings const& tracker_proxy() const
			{ return m_tracker_proxy; }

#ifndef TORRENT_DISABLE_DHT
			void set_dht_proxy(proxy_settings const& s)
			{
				m_dht_proxy = s;
				m_dht_socket.set_proxy_settings(s);
			}
			proxy_settings const& dht_proxy() const
			{ return m_dht_proxy; }
#endif

#ifndef TORRENT_DISABLE_GEO_IP
			std::string as_name_for_ip(address const& a);
			int as_for_ip(address const& a);
			std::pair<const int, int>* lookup_as(int as);
			bool load_asnum_db(char const* file);
			bool has_asnum_db() const { return m_asnum_db; }

			bool load_country_db(char const* file);
			bool has_country_db() const { return m_country_db; }
			char const* country_for_ip(address const& a);

#ifndef BOOST_FILESYSTEM_NARROW_ONLY
			bool load_asnum_db(wchar_t const* file);
			bool load_country_db(wchar_t const* file);
#endif
#endif

			void start_lsd();
			void start_natpmp(natpmp* n);
			void start_upnp(upnp* u);

			void stop_lsd();
			void stop_natpmp();
			void stop_upnp();

			int next_port();

			void add_redundant_bytes(size_type b)
			{
				TORRENT_ASSERT(b > 0);
				m_total_redundant_bytes += b;
			}

			void add_failed_bytes(size_type b)
			{
				TORRENT_ASSERT(b > 0);
				m_total_failed_bytes += b;
			}

			std::pair<char*, int> allocate_buffer(int size);
			void free_buffer(char* buf, int size);

			char* allocate_disk_buffer(char const* category);
			void free_disk_buffer(char* buf);

			void set_external_address(address const& ip);
			address const& external_address() const { return m_external_address; }

//		private:

			void update_disk_thread_settings();
			void on_dht_state_callback(boost::condition& c
				, entry& e, bool& done) const;
			void on_lsd_peer(tcp::endpoint peer, sha1_hash const& ih);
			void setup_socket_buffers(socket_type& s);

			// this is a shared pool where policy_peer objects
			// are allocated. It's a pool since we're likely
			// to have tens of thousands of peers, and a pool
			// saves significant overhead
#ifdef TORRENT_STATS
			struct logging_allocator
			{
				typedef std::size_t size_type;
				typedef std::ptrdiff_t difference_type;

				static char* malloc(const size_type bytes)
				{
					allocated_bytes += bytes;
					++allocations;
					return (char*)::malloc(bytes);
				}

				static void free(char* const block)
				{
					--allocations;
					return ::free(block);
				}
			
				static int allocations;
				static int allocated_bytes;
			};
			boost::object_pool<
				policy::ipv4_peer, logging_allocator> m_ipv4_peer_pool;
# if TORRENT_USE_IPV6
			boost::object_pool<
				policy::ipv6_peer, logging_allocator> m_ipv6_peer_pool;
# endif
#else
			boost::object_pool<policy::ipv4_peer> m_ipv4_peer_pool;
# if TORRENT_USE_IPV6
			boost::object_pool<policy::ipv6_peer> m_ipv6_peer_pool;
# endif
#endif

			// this vector is used to store the block_info
			// objects pointed to by partial_piece_info returned
			// by torrent::get_download_queue.
			std::vector<block_info> m_block_info_storage;

#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
			// this pool is used to allocate and recycle send
			// buffers from.
			boost::pool<> m_send_buffers;
#endif
			boost::mutex m_send_buffer_mutex;

			// the file pool that all storages in this session's
			// torrents uses. It sets a limit on the number of
			// open files by this session.
			// file pool must be destructed after the torrents
			// since they will still have references to it
			// when they are destructed.
			file_pool m_files;

			// this is where all active sockets are stored.
			// the selector can sleep while there's no activity on
			// them
			mutable io_service m_io_service;

			tcp::resolver m_host_resolver;

			// handles delayed alerts
			alert_manager m_alerts;

			// handles disk io requests asynchronously
			// peers have pointers into the disk buffer
			// pool, and must be destructed before this
			// object. The disk thread relies on the file
			// pool object, and must be destructed before
			// m_files. The disk io thread posts completion
			// events to the io service, and needs to be
			// constructed after it.
			disk_io_thread m_disk_thread;

			// this is a list of half-open tcp connections
			// (only outgoing connections)
			// this has to be one of the last
			// members to be destructed
			connection_queue m_half_open;

			// the bandwidth manager is responsible for
			// handing out bandwidth to connections that
			// asks for it, it can also throttle the
			// rate.
			bandwidth_manager<peer_connection> m_download_rate;
			bandwidth_manager<peer_connection> m_upload_rate;

			// the global rate limiter bandwidth channels
			bandwidth_channel m_download_channel;
			bandwidth_channel m_upload_channel;

			// bandwidth channels for local peers when
			// rate limits are ignored. They are only
			// throttled by these global rate limiters
			// and they don't have a rate limit set by
			// default
			bandwidth_channel m_local_download_channel;
			bandwidth_channel m_local_upload_channel;

			bandwidth_channel* m_bandwidth_channel[2];

			tracker_manager m_tracker_manager;
			torrent_map m_torrents;
			typedef std::list<boost::shared_ptr<torrent> > check_queue_t;

			// this has all torrents that wants to be checked in it
			check_queue_t m_queued_for_checking;

			// this maps sockets to their peer_connection
			// object. It is the complete list of all connected
			// peers.
			connection_map m_connections;
			
			// filters incoming connections
			ip_filter m_ip_filter;

			// filters outgoing connections
			port_filter m_port_filter;
			
			// the peer id that is generated at the start of the session
			peer_id m_peer_id;

			// the key is an id that is used to identify the
			// client with the tracker only. It is randomized
			// at startup
			int m_key;

			// the number of retries we make when binding the
			// listen socket. For each retry the port number
			// is incremented by one
			int m_listen_port_retries;

			// the ip-address of the interface
			// we are supposed to listen on.
			// if the ip is set to zero, it means
			// that we should let the os decide which
			// interface to listen on
			tcp::endpoint m_listen_interface;

			// if we're listening on an IPv6 interface
			// this is one of the non local IPv6 interfaces
			// on this machine
			tcp::endpoint m_ipv6_interface;
			tcp::endpoint m_ipv4_interface;
			
			struct listen_socket_t
			{
				listen_socket_t(): external_port(0) {}
				// this is typically set to the same as the local
				// listen port. In case a NAT port forward was
				// successfully opened, this will be set to the
				// port that is open on the external (NAT) interface
				// on the NAT box itself. This is the port that has
				// to be published to peers, since this is the port
				// the client is reachable through.
				int external_port;

				// the actual socket
				boost::shared_ptr<socket_acceptor> sock;
			};
			// since we might be listening on multiple interfaces
			// we might need more than one listen socket
			std::list<listen_socket_t> m_listen_sockets;

			// when as a socks proxy is used for peers, also
			// listen for incoming connections on a socks connection
			boost::shared_ptr<socket_type> m_socks_listen_socket;

			void open_new_incoming_socks_connection();

			listen_socket_t setup_listener(tcp::endpoint ep, int retries, bool v6_only = false);

			// the settings for the client
			session_settings m_settings;
			// the proxy settings for different
			// kinds of connections
			proxy_settings m_peer_proxy;
			proxy_settings m_web_seed_proxy;
			proxy_settings m_tracker_proxy;
#ifndef TORRENT_DISABLE_DHT
			proxy_settings m_dht_proxy;
#endif

#ifndef TORRENT_DISABLE_DHT	
			entry m_dht_state;
#endif
			// set to true when the session object
			// is being destructed and the thread
			// should exit
			bool m_abort;

			// is true if the session is paused
			bool m_paused;

			// the max number of unchoked peers as set by the user
			int m_max_uploads;

			// the number of unchoked peers as set by the auto-unchoker
			// this should always be >= m_max_uploads
			int m_allowed_upload_slots;

			// the max number of connections, as set by the user
			int m_max_connections;

			// the number of unchoked peers
			int m_num_unchoked;

			// this is initialized to the unchoke_interval
			// session_setting and decreased every second.
			// when it reaches zero, it is reset to the
			// unchoke_interval and the unchoke set is
			// recomputed.
			int m_unchoke_time_scaler;

			// this is used to decide when to recalculate which
			// torrents to keep queued and which to activate
			int m_auto_manage_time_scaler;

			// works like unchoke_time_scaler but it
			// is only decresed when the unchoke set
			// is recomputed, and when it reaches zero,
			// the optimistic unchoke is moved to another peer.
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

			// is false by default and set to true when
			// the first incoming connection is established
			// this is used to know if the client is behind
			// NAT or not.
			bool m_incoming_connection;
			
			void on_disk_queue();
			void on_tick(error_code const& e);

			int auto_manage_torrents(std::vector<torrent*>& list
				, int hard_limit, int type_limit);
			void recalculate_auto_managed_torrents();
			void recalculate_unchoke_slots(int congested_torrents
				, int uncongested_torrents);
			void recalculate_optimistic_unchoke_slot();

			ptime m_created;
			int session_time() const { return total_seconds(time_now() - m_created); }

			ptime m_last_tick;
			ptime m_last_second_tick;

			// the last time we went through the peers
			// to decide which ones to choke/unchoke
			ptime m_last_choke;

			// when outgoing_ports is configured, this is the
			// port we'll bind the next outgoing socket to
			int m_next_port;

#ifndef TORRENT_DISABLE_DHT
			boost::intrusive_ptr<dht::dht_tracker> m_dht;
			dht_settings m_dht_settings;
			// if this is set to true, the dht listen port
			// will be set to the same as the tcp listen port
			// and will be synchronlized with it as it changes
			// it defaults to true
			bool m_dht_same_port;
			
			// see m_external_listen_port. This is the same
			// but for the udp port used by the DHT.
			int m_external_udp_port;

			rate_limited_udp_socket m_dht_socket;

			// these are used when starting the DHT
			// (and bootstrapping it), and then erased
			std::list<udp::endpoint> m_dht_router_nodes;

			void on_receive_udp(error_code const& e
				, udp::endpoint const& ep, char const* buf, int len);
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
			pe_settings m_pe_settings;
#endif

			boost::intrusive_ptr<natpmp> m_natpmp;
			boost::intrusive_ptr<upnp> m_upnp;
			boost::intrusive_ptr<lsd> m_lsd;

			// 0 is natpmp 1 is upnp
			int m_tcp_mapping[2];
			int m_udp_mapping[2];

			// the timer used to fire the tick
			deadline_timer m_timer;

			// torrents are announced on the local network in a
			// round-robin fashion. All torrents are cycled through
			// within the LSD announce interval (which defaults to
			// 5 minutes)
			torrent_map::iterator m_next_lsd_torrent;

			// this announce timer is used
			// by Local service discovery
			deadline_timer m_lsd_announce_timer;

			// the index of the torrent that will be offered to
			// connect to a peer next time on_tick is called.
			// This implements a round robin.
			torrent_map::iterator m_next_connect_torrent;
#ifdef TORRENT_DEBUG
			void check_invariant() const;
#endif

#if defined TORRENT_STATS && defined TORRENT_DISK_STATS
			void log_buffer_usage();
#endif

#if defined TORRENT_STATS
			// logger used to write bandwidth usage statistics
			std::ofstream m_stats_logger;
			int m_second_counter;
			// used to log send buffer usage statistics
			std::ofstream m_buffer_usage_logger;
			// the number of send buffers that are allocated
			int m_buffer_allocations;
#endif
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			boost::shared_ptr<logger> create_log(std::string const& name
				, int instance, bool append = true);
			
			// this list of tracker loggers serves as tracker_callbacks when
			// shutting down. This list is just here to keep them alive during
			// whe shutting down process
			std::list<boost::shared_ptr<tracker_logger> > m_tracker_loggers;

			fs::path m_logpath;
		public:
			boost::shared_ptr<logger> m_logger;
		private:

#endif
#ifdef TORRENT_UPNP_LOGGING
			std::ofstream m_upnp_log;
#endif
			address m_external_address;

#ifndef TORRENT_DISABLE_EXTENSIONS
			typedef std::list<boost::function<boost::shared_ptr<
				torrent_plugin>(torrent*, void*)> > extension_list_t;

			extension_list_t m_extensions;
#endif

#ifndef TORRENT_DISABLE_GEO_IP
			GeoIP* m_asnum_db;
			GeoIP* m_country_db;

			// maps AS number to the peak download rate
			// we've seen from it. Entries are never removed
			// from this map. Pointers to its elements
			// are kept in the policy::peer structures.
			std::map<int, int> m_as_peak;
#endif

			// total redundant and failed bytes
			size_type m_total_failed_bytes;
			size_type m_total_redundant_bytes;

			// the main working thread
			boost::scoped_ptr<boost::thread> m_thread;
		};
		
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		struct tracker_logger : request_callback
		{
			tracker_logger(session_impl& ses): m_ses(ses) {}
			void tracker_warning(tracker_request const& req
				, std::string const& str)
			{
				debug_log("*** tracker warning: " + str);
			}

			void tracker_response(tracker_request const&
				, libtorrent::address const& tracker_ip
				, std::list<address> const& ip_list
				, std::vector<peer_entry>& peers
				, int interval
				, int min_interval
				, int complete
				, int incomplete
				, address const& external_ip)
			{
				std::string s;
				s = "TRACKER RESPONSE:\n";
				char tmp[200];
				snprintf(tmp, 200, "interval: %d\nmin_interval: %d\npeers:\n", interval, min_interval);
				s += tmp;
				for (std::vector<peer_entry>::const_iterator i = peers.begin();
					i != peers.end(); ++i)
				{
					char pid[41];
					to_hex((const char*)&i->pid[0], 20, pid);
					if (i->pid.is_all_zeros()) pid[0] = 0;

					snprintf(tmp, 200, " %-16s %-5d %s\n", i->ip.c_str(), i->port, pid);
					s += tmp;
				}
				snprintf(tmp, 200, "external ip: %s\n", print_address(external_ip).c_str());
				s += tmp;
				debug_log(s);
			}

			void tracker_request_timed_out(
				tracker_request const&)
			{
				debug_log("*** tracker timed out");
			}

			void tracker_request_error(
				tracker_request const&
				, int response_code
				, const std::string& str
				, int retry_interval)
			{
				char msg[256];
				snprintf(msg, sizeof(msg), "*** tracker error: %d: %s", response_code, str.c_str());
				debug_log(msg);
			}
			
			void debug_log(const std::string& line)
			{
				(*m_ses.m_logger) << time_now_string() << " " << line << "\n";
			}
			session_impl& m_ses;
		};
#endif

	}
}


#endif

