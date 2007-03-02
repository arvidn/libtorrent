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

namespace libtorrent
{

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
			boost::filesystem::path save_path;

			sha1_hash info_hash;

			void parse_resume_data(
				const entry& rd
				, const torrent_info& info
				, std::string& error);

			std::vector<int> piece_map;
			std::vector<piece_picker::downloading_piece> unfinished_pieces;
			std::vector<tcp::endpoint> peers;
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
			void remove_torrent(sha1_hash const& info_hash);

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
#ifndef NDEBUG
			friend class ::libtorrent::peer_connection;
#endif
			friend struct checker_impl;
			friend class invariant_access;
			typedef std::map<boost::shared_ptr<stream_socket>
				, boost::intrusive_ptr<peer_connection> >
				connection_map;
			typedef std::map<sha1_hash, boost::shared_ptr<torrent> > torrent_map;
			typedef std::deque<boost::intrusive_ptr<peer_connection> >
				connection_queue;

			session_impl(
				std::pair<int, int> listen_port_range
				, fingerprint const& cl_fprint
				, char const* listen_interface = "0.0.0.0");
			~session_impl();

#ifndef TORRENT_DISABLE_EXTENSIONS
			void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*)> ext);
#endif
			void operator()();

			void open_listen_port();

			void async_accept();
			void on_incoming_connection(boost::shared_ptr<stream_socket> const& s
				, boost::weak_ptr<socket_acceptor> const& as, asio::error_code const& e);
		
			// must be locked to access the data
			// in this struct
			typedef boost::recursive_mutex mutex_t;
			mutable mutex_t m_mutex;

			boost::weak_ptr<torrent> find_torrent(const sha1_hash& info_hash);
			peer_id const& get_peer_id() const { return m_peer_id; }

			// this will see if there are any pending connection attempts
			// and in that case initiate new connections until the limit
			// is reached.
			void process_connection_queue();

			void close_connection(boost::intrusive_ptr<peer_connection> const& p);
			void connection_completed(boost::intrusive_ptr<peer_connection> const& p);
			void connection_failed(boost::shared_ptr<stream_socket> const& s
				, tcp::endpoint const& a, char const* message);

			void set_settings(session_settings const& s);
			session_settings const& settings() const { return m_settings; }

#ifndef TORRENT_DISABLE_DHT	
			void add_dht_node(std::pair<std::string, int> const& node);
			void add_dht_node(udp::endpoint n);
			void add_dht_router(std::pair<std::string, int> const& node);
			void set_dht_settings(dht_settings const& s);
			dht_settings const& kad_settings() const { return m_dht_settings; }
			void start_dht(entry const& startup_state);
			void stop_dht();
			entry dht_state() const;
#endif
			bool is_aborted() const { return m_abort; }

			void set_ip_filter(ip_filter const& f);

			bool listen_on(
				std::pair<int, int> const& port_range
				, const char* net_interface = 0);
			bool is_listening() const;

			torrent_handle add_torrent(
				torrent_info const& ti
				, boost::filesystem::path const& save_path
				, entry const& resume_data
				, bool compact_mode
				, int block_size);

			torrent_handle add_torrent(
				char const* tracker_url
				, sha1_hash const& info_hash
				, char const* name
				, boost::filesystem::path const& save_path
				, entry const& resume_data
				, bool compact_mode
				, int block_size);

			void remove_torrent(torrent_handle const& h);

			std::vector<torrent_handle> get_torrents();
			
			void set_severity_level(alert::severity_t s);
			std::auto_ptr<alert> pop_alert();

			int upload_rate_limit() const;
			int download_rate_limit() const;

			void set_download_rate_limit(int bytes_per_second);
			void set_upload_rate_limit(int bytes_per_second);
			void set_max_half_open_connections(int limit);
			void set_max_connections(int limit);
			void set_max_uploads(int limit);

			int num_uploads() const;
			int num_connections() const;

			session_status status() const;
			void set_peer_id(peer_id const& id);
			void set_key(int key);
			unsigned short listen_port() const;
			
			void abort();
			
			torrent_handle find_torrent_handle(sha1_hash const& info_hash);

			
			// handles delayed alerts
			alert_manager m_alerts;
			
//		private:

			// this is where all active sockets are stored.
			// the selector can sleep while there's no activity on
			// them
			io_service m_io_service;
			asio::strand m_strand;

			// the bandwidth manager is responsible for
			// handing out bandwidth to connections that
			// asks for it, it can also throttle the
			// rate.
			bandwidth_manager m_dl_bandwidth_manager;
			bandwidth_manager m_ul_bandwidth_manager;

			tracker_manager m_tracker_manager;
			torrent_map m_torrents;

			// this maps sockets to their peer_connection
			// object. It is the complete list of all connected
			// peers.
			connection_map m_connections;
			
			// this is a list of half-open tcp connections
			// (only outgoing connections)
			connection_map m_half_open;

			// this is a queue of pending outgoing connections. If the
			// list of half-open connections is full (given the global
			// limit), new outgoing connections are put on this queue,
			// waiting for one slot in the half-open queue to open up.
			connection_queue m_connection_queue;

			// filters incoming connections
			ip_filter m_ip_filter;
			
			// the peer id that is generated at the start of the session
			peer_id m_peer_id;

			// the key is an id that is used to identify the
			// client with the tracker only. It is randomized
			// at startup
			int m_key;

			// the range of ports we try to listen on
			std::pair<int, int> m_listen_port_range;

			// the ip-address of the interface
			// we are supposed to listen on.
			// if the ip is set to zero, it means
			// that we should let the os decide which
			// interface to listen on
			tcp::endpoint m_listen_interface;

			boost::shared_ptr<socket_acceptor> m_listen_socket;

			// the settings for the client
			session_settings m_settings;

			// set to true when the session object
			// is being destructed and the thread
			// should exit
			volatile bool m_abort;

			int m_max_uploads;
			int m_max_connections;
			// the number of simultaneous half-open tcp
			// connections libtorrent will have.
			int m_half_open_limit;

			// statistics gathered from all torrents.
			stat m_stat;

			// is false by default and set to true when
			// the first incoming connection is established
			// this is used to know if the client is behind
			// NAT or not.
			bool m_incoming_connection;
			
			// the file pool that all storages in this session's
			// torrents uses. It sets a limit on the number of
			// open files by this session.
			file_pool m_files;

			void second_tick(asio::error_code const& e);
			boost::posix_time::ptime m_last_tick;

#ifndef TORRENT_DISABLE_DHT
			boost::intrusive_ptr<dht::dht_tracker> m_dht;
			dht_settings m_dht_settings;
#endif
			// the timer used to fire the second_tick
			deadline_timer m_timer;
#ifndef NDEBUG
			void check_invariant(const char *place = 0);
#endif
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			boost::shared_ptr<logger> create_log(std::string const& name
				, int instance, bool append = true);
			
			// this list of tracker loggers serves as tracker_callbacks when
			// shutting down. This list is just here to keep them alive during
			// whe shutting down process
			std::list<boost::shared_ptr<tracker_logger> > m_tracker_loggers;
			
			// logger used to write bandwidth usage statistics
			boost::shared_ptr<logger> m_stats_logger;
			int m_second_counter;
		public:
			boost::shared_ptr<logger> m_logger;
		private:
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
			typedef std::list<boost::function<boost::shared_ptr<
				torrent_plugin>(torrent*)> > extension_list_t;

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
				(*m_ses.m_logger) << line << "\n";
			}
			session_impl& m_ses;
		};
#endif

	}
}


#endif

