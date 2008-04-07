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

#include <ctime>
#include <algorithm>
#include <vector>
#include <set>
#include <list>
#include <deque>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/limits.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/policy.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/fingerprint.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/piece_block_progress.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/bandwidth_manager.hpp"
#include "libtorrent/natpmp.hpp"
#include "libtorrent/upnp.hpp"
#include "libtorrent/lsd.hpp"
#include "libtorrent/socket_type.hpp"
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{

	namespace fs = boost::filesystem;

	namespace aux
	{
		struct session_impl;

		// this data is shared between the main thread and the
		// thread that initialize pieces
		struct piece_checker_data
		{
			piece_checker_data()
				: processing(false), progress(0.f), abort(false) {}

			boost::shared_ptr<torrent> torrent_ptr;
			fs::path save_path;

			sha1_hash info_hash;

			void parse_resume_data(
				const entry& rd
				, const torrent_info& info
				, std::string& error);

			std::vector<int> piece_map;
			std::vector<piece_picker::downloading_piece> unfinished_pieces;
			std::vector<piece_picker::block_info> block_info;
			std::vector<tcp::endpoint> peers;
			std::vector<tcp::endpoint> banned_peers;
			entry resume_data;

			// this is true if this torrent is being processed (checked)
			// if it is not being processed, then it can be removed from
			// the queue without problems, otherwise the abort flag has
			// to be set.
			bool processing;

			// is filled in by storage::initialize_pieces()
			// and represents the progress. It should be a
			// value in the range [0, 1]
			float progress;

			// abort defaults to false and is typically
			// filled in by torrent_handle when the user
			// aborts the torrent
			bool abort;
		};

		struct checker_impl: boost::noncopyable
		{
			checker_impl(session_impl& s): m_ses(s), m_abort(false) {}
			void operator()();
			piece_checker_data* find_torrent(const sha1_hash& info_hash);
			void remove_torrent(sha1_hash const& info_hash, int options);

#ifndef NDEBUG
			void check_invariant() const;
#endif

			// when the files has been checked
			// the torrent is added to the session
			session_impl& m_ses;

			mutable boost::mutex m_mutex;
			boost::condition m_cond;

			// a list of all torrents that are currently in queue
			// or checking their files
			std::deque<boost::shared_ptr<piece_checker_data> > m_torrents;
			std::deque<boost::shared_ptr<piece_checker_data> > m_processing;

			bool m_abort;
		};

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		struct tracker_logger;
#endif

		// this is the link between the main thread and the
		// thread started to run the main downloader loop
		struct session_impl: boost::noncopyable
		{

			// the size of each allocation that is chained in the send buffer
			enum { send_buffer_size = 200 };

#ifndef NDEBUG
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
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
				, fs::path const& logpath
#endif
				);
			~session_impl();

#ifndef TORRENT_DISABLE_EXTENSIONS
			void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(
				torrent*, void*)> ext);
#endif
			void operator()();

			void open_listen_port() throw();

			// if we are listening on an IPv6 interface
			// this will return one of the IPv6 addresses on this
			// machine, otherwise just an empty endpoint
			tcp::endpoint get_ipv6_interface() const;

			void async_accept(boost::shared_ptr<socket_acceptor> const& listener);
			void on_incoming_connection(boost::shared_ptr<socket_type> const& s
				, boost::weak_ptr<socket_acceptor> listener, asio::error_code const& e);
		
			// must be locked to access the data
			// in this struct
			typedef boost::recursive_mutex mutex_t;
			mutable mutex_t m_mutex;

			boost::weak_ptr<torrent> find_torrent(const sha1_hash& info_hash);
			peer_id const& get_peer_id() const { return m_peer_id; }

			void close_connection(boost::intrusive_ptr<peer_connection> const& p);
			void connection_failed(boost::intrusive_ptr<peer_connection> const& p
				, tcp::endpoint const& a, char const* message);

			void set_settings(session_settings const& s);
			session_settings const& settings() const { return m_settings; }

#ifndef TORRENT_DISABLE_DHT	
			void add_dht_node(std::pair<std::string, int> const& node);
			void add_dht_node(udp::endpoint n);
			void add_dht_router(std::pair<std::string, int> const& node);
			void set_dht_settings(dht_settings const& s);
			dht_settings const& get_dht_settings() const { return m_dht_settings; }
			void start_dht(entry const& startup_state);
			void stop_dht();
			entry dht_state() const;
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
			void set_pe_settings(pe_settings const& settings);
			pe_settings const& get_pe_settings() const { return m_pe_settings; }
#endif

			// called when a port mapping is successful, or a router returns
			// a failure to map a port
			void on_port_mapping(int tcp_port, int udp_port, std::string const& errmsg);

			bool is_aborted() const { return m_abort; }

			void set_ip_filter(ip_filter const& f);
			void set_port_filter(port_filter const& f);

			bool listen_on(
				std::pair<int, int> const& port_range
				, const char* net_interface = 0);
			bool is_listening() const;

			torrent_handle add_torrent(
				boost::intrusive_ptr<torrent_info> ti
				, fs::path const& save_path
				, entry const& resume_data
				, storage_mode_t storage_mode
				, storage_constructor_type sc
				, bool paused
				, void* userdata);

			torrent_handle add_torrent(
				char const* tracker_url
				, sha1_hash const& info_hash
				, char const* name
				, fs::path const& save_path
				, entry const& resume_data
				, storage_mode_t storage_mode
				, storage_constructor_type sc
				, bool paused
				, void* userdata);

			void remove_torrent(torrent_handle const& h, int options);

			std::vector<torrent_handle> get_torrents();
			
			void set_severity_level(alert::severity_t s);
			std::auto_ptr<alert> pop_alert();

			alert const* wait_for_alert(time_duration max_wait);

			int upload_rate_limit() const;
			int download_rate_limit() const;

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

			void unchoke_peer(peer_connection& c)
			{
				torrent* t = c.associated_torrent().lock().get();
				TORRENT_ASSERT(t);
				if (t->unchoke_peer(c))
					++m_num_unchoked;
			}

			session_status status() const;
			void set_peer_id(peer_id const& id);
			void set_key(int key);
			unsigned short listen_port() const;
			
			void abort();
			
			torrent_handle find_torrent_handle(sha1_hash const& info_hash);

			void announce_lsd(sha1_hash const& ih);

			void set_peer_proxy(proxy_settings const& s)
			{ m_peer_proxy = s; }
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
			{ m_dht_proxy = s; }
			proxy_settings const& dht_proxy() const
			{ return m_dht_proxy; }
#endif

#ifdef TORRENT_STATS
			void log_buffer_usage()
			{
				int send_buffer_capacity = 0;
				int used_send_buffer = 0;
				for (connection_map::const_iterator i = m_connections.begin()
					, end(m_connections.end()); i != end; ++i)
				{
					send_buffer_capacity += (*i)->send_buffer_capacity();
					used_send_buffer += (*i)->send_buffer_size();
				}
				TORRENT_ASSERT(send_buffer_capacity >= used_send_buffer);
				m_buffer_usage_logger << log_time() << " send_buffer_size: " << send_buffer_capacity << std::endl;
				m_buffer_usage_logger << log_time() << " used_send_buffer: " << used_send_buffer << std::endl;
				m_buffer_usage_logger << log_time() << " send_buffer_utilization: "
					<< (used_send_buffer * 100.f / send_buffer_capacity) << std::endl;
			}
#endif
			void start_lsd();
			void start_natpmp();
			void start_upnp();

			void stop_lsd();
			void stop_natpmp();
			void stop_upnp();

			// handles delayed alerts
			alert_manager m_alerts;

			std::pair<char*, int> allocate_buffer(int size);
			void free_buffer(char* buf, int size);
			void free_disk_buffer(char* buf);
			
			address m_external_address;

//		private:

			void on_lsd_peer(tcp::endpoint peer, sha1_hash const& ih);

			// this pool is used to allocate and recycle send
			// buffers from.
			boost::pool<> m_send_buffers;
			boost::mutex m_send_buffer_mutex;

			// the file pool that all storages in this session's
			// torrents uses. It sets a limit on the number of
			// open files by this session.
			// file pool must be destructed after the torrents
			// since they will still have references to it
			// when they are destructed.
			file_pool m_files;

			// handles disk io requests asynchronously
			// peers have pointers into the disk buffer
			// pool, and must be destructed before this
			// object. The disk thread relies on the file
			// pool object, and must be destructed before
			// m_files.
			disk_io_thread m_disk_thread;

			// this is where all active sockets are stored.
			// the selector can sleep while there's no activity on
			// them
			io_service m_io_service;
			asio::strand m_strand;

			// this is a list of half-open tcp connections
			// (only outgoing connections)
			// this has to be one of the last
			// members to be destructed
			connection_queue m_half_open;

			// the bandwidth manager is responsible for
			// handing out bandwidth to connections that
			// asks for it, it can also throttle the
			// rate.
			bandwidth_manager<peer_connection, torrent> m_download_channel;
			bandwidth_manager<peer_connection, torrent> m_upload_channel;

			bandwidth_manager<peer_connection, torrent>* m_bandwidth_manager[2];

			tracker_manager m_tracker_manager;
			torrent_map m_torrents;

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

			// set to true when the session object
			// is being destructed and the thread
			// should exit
			volatile bool m_abort;

			int m_max_uploads;
			int m_max_connections;

			// the number of unchoked peers
			int m_num_unchoked;

			// this is initialized to the unchoke_interval
			// session_setting and decreased every second.
			// when it reaches zero, it is reset to the
			// unchoke_interval and the unchoke set is
			// recomputed.
			int m_unchoke_time_scaler;

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

			// statistics gathered from all torrents.
			stat m_stat;

			// is false by default and set to true when
			// the first incoming connection is established
			// this is used to know if the client is behind
			// NAT or not.
			bool m_incoming_connection;
			
			void second_tick(asio::error_code const& e);
			ptime m_last_tick;

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
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
			pe_settings m_pe_settings;
#endif

			boost::intrusive_ptr<natpmp> m_natpmp;
			boost::intrusive_ptr<upnp> m_upnp;
			boost::intrusive_ptr<lsd> m_lsd;

			// the timer used to fire the second_tick
			deadline_timer m_timer;

			// the index of the torrent that will be offered to
			// connect to a peer next time second_tick is called.
			// This implements a round robin.
			int m_next_connect_torrent;
#ifndef NDEBUG
			void check_invariant() const;
#endif

#ifdef TORRENT_STATS
			// logger used to write bandwidth usage statistics
			std::ofstream m_stats_logger;
			int m_second_counter;
			// used to log send buffer usage statistics
			std::ofstream m_buffer_usage_logger;
			// the number of send buffers that are allocated
			int m_buffer_allocations;
#endif
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
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

#ifndef TORRENT_DISABLE_EXTENSIONS
			typedef std::list<boost::function<boost::shared_ptr<
				torrent_plugin>(torrent*, void*)> > extension_list_t;

			extension_list_t m_extensions;
#endif

			// data shared between the main thread
			// and the checker thread
			checker_impl m_checker_impl;

			// the main working thread
			boost::scoped_ptr<boost::thread> m_thread;

			// the thread that calls initialize_pieces()
			// on all torrents before they start downloading
			boost::scoped_ptr<boost::thread> m_checker_thread;
		};
		
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		struct tracker_logger : request_callback
		{
			tracker_logger(session_impl& ses): m_ses(ses) {}
			void tracker_warning(std::string const& str)
			{
				debug_log("*** tracker warning: " + str);
			}

			void tracker_response(tracker_request const&
				, std::vector<peer_entry>& peers
				, int interval
				, int complete
				, int incomplete)
			{
				std::stringstream s;
				s << "TRACKER RESPONSE:\n"
					"interval: " << interval << "\n"
					"peers:\n";
				for (std::vector<peer_entry>::const_iterator i = peers.begin();
					i != peers.end(); ++i)
				{
					s << "  " << std::setfill(' ') << std::setw(16) << i->ip
						<< " " << std::setw(5) << std::dec << i->port << "  ";
					if (!i->pid.is_all_zeros()) s << " " << i->pid;
					s << "\n";
				}
				debug_log(s.str());
			}

			void tracker_request_timed_out(
				tracker_request const&)
			{
				debug_log("*** tracker timed out");
			}

			void tracker_request_error(
				tracker_request const&
				, int response_code
				, const std::string& str)
			{
				debug_log(std::string("*** tracker error: ")
					+ boost::lexical_cast<std::string>(response_code) + ": "
					+ str);
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

