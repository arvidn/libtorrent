/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_SESSION_HPP_INCLUDED
#define TORRENT_SESSION_HPP_INCLUDED

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

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent.hpp"
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

#if !defined(NDEBUG) && defined(_MSC_VER)
#	include <float.h>
#	include <eh.h>
#endif

namespace libtorrent
{

	namespace detail
	{
		// workaround for microsofts
		// hardware exceptions that makes
		// it hard to debug stuff
#if defined(_MSC_VER)
		struct eh_initializer
		{
			eh_initializer()
			{
#ifndef NDEBUG
				_clearfp();
				_controlfp(_EM_INEXACT | _EM_UNDERFLOW, _MCW_EM );
				::_set_se_translator(straight_to_debugger);
#endif
			}

			static void straight_to_debugger(unsigned int, _EXCEPTION_POINTERS*)
			{ throw; }
		};
#else
		struct eh_initializer {};
#endif

		// this data is shared between the main thread and the
		// thread that initialize pieces
		struct piece_checker_data
		{
			piece_checker_data(): progress(0.f), abort(false) {}

			boost::shared_ptr<torrent> torrent_ptr;
			boost::filesystem::path save_path;

			sha1_hash info_hash;

			void parse_resume_data(
				const entry& rd
				, const torrent_info& info);

			std::vector<int> piece_map;
			std::vector<piece_picker::downloading_piece> unfinished_pieces;
			std::vector<address> peers;
			entry resume_data;

			// is filled in by storage::initialize_pieces()
			// and represents the progress. It should be a
			// value in the range [0, 1]
			volatile float progress;

			// abort defaults to false and is typically
			// filled in by torrent_handle when the user
			// aborts the torrent
			volatile bool abort;
		};

		struct checker_impl: boost::noncopyable
		{
			checker_impl(session_impl& s): m_ses(s), m_abort(false) {}
			void operator()();
			piece_checker_data* find_torrent(const sha1_hash& info_hash);

			// when the files has been checked
			// the torrent is added to the session
			session_impl& m_ses;

			mutable boost::mutex m_mutex;
			boost::condition m_cond;

			// a list of all torrents that are currently in queue
			// or checking their files
			std::deque<piece_checker_data> m_torrents;

			bool m_abort;
		};

		// this is the link between the main thread and the
		// thread started to run the main downloader loop
		struct session_impl: boost::noncopyable
		{
			friend class invariant_access;
			// TODO: maybe this should be changed to a sorted vector
			// using lower_bound?
			typedef std::map<boost::shared_ptr<socket>, boost::shared_ptr<peer_connection> > connection_map;
			typedef std::map<sha1_hash, boost::shared_ptr<torrent> > torrent_map;

			session_impl(
				std::pair<int, int> listen_port_range
				, fingerprint const& cl_fprint
				, const char* listen_interface);

			void operator()();

			void open_listen_port();

			// must be locked to access the data
			// in this struct
			mutable boost::mutex m_mutex;
			torrent* find_torrent(const sha1_hash& info_hash);
			const peer_id& get_peer_id() const { return m_peer_id; }

			tracker_manager m_tracker_manager;
			torrent_map m_torrents;

			// this maps sockets to their peer_connection
			// object. It is the complete list of all connected
			// peers.
			connection_map m_connections;

			// this is a list of iterators into the m_connections map
			// that should be disconnected as soon as possible.
			// It is used to delay disconnections to avoid troubles
			// in loops that iterate over them.
			std::vector<connection_map::iterator> m_disconnect_peer;

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
			address m_listen_interface;

			// this is where all active sockets are stored.
			// the selector can sleep while there's no activity on
			// them
			selector m_selector;

			boost::shared_ptr<socket> m_listen_socket;

			// the entries in this array maps the
			// extension index (as specified in peer_connection)
			bool m_extension_enabled[peer_connection::num_supported_extensions];

			bool extensions_enabled() const;

			// the settings for the client
			http_settings m_settings;

			// set to true when the session object
			// is being destructed and the thread
			// should exit
			volatile bool m_abort;

			// maximum upload rate given in
			// bytes per second. -1 means
			// unlimited
			int m_upload_rate;
			int m_download_rate;
			int m_max_uploads;
			int m_max_connections;

			// statistics gathered from all torrents.
			stat m_stat;

			// handles delayed alerts
			alert_manager m_alerts;

			// is false by default and set to true when
			// the first incoming connection is established
			// this is used to know if the client is behind
			// NAT or not.
			bool m_incoming_connection;

			// does the actual disconnections
			// that are queued up in m_disconnect_peer
			void purge_connections();

#ifndef NDEBUG
			void check_invariant(const char *place = 0);
			boost::shared_ptr<logger> create_log(std::string name);
			boost::shared_ptr<logger> m_logger;
#endif
		};
	}

	struct http_settings;

	struct session_status
	{
		bool has_incoming_connections;

		float upload_rate;
		float download_rate;

		float payload_upload_rate;
		float payload_download_rate;

		size_type total_download;
		size_type total_upload;

		size_type total_payload_download;
		size_type total_payload_upload;

		int num_peers;
	};

	class session: public boost::noncopyable, detail::eh_initializer
	{
	public:

		session(fingerprint const& print = fingerprint("LT", 0, 1, 0, 0));
		session(
			fingerprint const& print
			, std::pair<int, int> listen_port_range
			, const char* listen_interface = 0);

		~session();

		std::vector<torrent_handle> get_torrents();

		// all torrent_handles must be destructed before the session is destructed!
		torrent_handle add_torrent(
			entry const& metadata
			, boost::filesystem::path const& save_path
			, entry const& resume_data = entry());

		torrent_handle add_torrent(
			char const* tracker_url
			, sha1_hash const& info_hash
			, boost::filesystem::path const& save_path
			, entry const& resume_data = entry());

		session_status status() const;

		void enable_extension(peer_connection::extension_index i);
		void disable_extensions();

		bool is_listening() const;

		// if the listen port failed in some way
		// you can retry to listen on another port-
		// range with this function. If the listener
		// succeeded and is currently listening,
		// a call to this function will shut down the
		// listen port and reopen it using these new
		// properties (the given interface and port range).
		// As usual, if the interface is left as 0
		// this function will return false on failure.
		// If it fails, it will also generate alerts describing
		// the error. It will return true on success.
		bool listen_on(
			std::pair<int, int> const& port_range
			, const char* net_interface = 0);

		// returns the port we ended up listening on
		unsigned short listen_port() const;

		void remove_torrent(const torrent_handle& h);

		void set_http_settings(const http_settings& s);
		void set_upload_rate_limit(int bytes_per_second);
		void set_download_rate_limit(int bytes_per_second);
		void set_max_uploads(int limit);
		void set_max_connections(int limit);

		std::auto_ptr<alert> pop_alert();
		void set_severity_level(alert::severity_t s);

	private:

		// data shared between the main thread
		// and the working thread
		detail::session_impl m_impl;

		// data shared between the main thread
		// and the checker thread
		detail::checker_impl m_checker_impl;

		// the main working thread
		boost::thread m_thread;

		// the thread that calls initialize_pieces()
		// on all torrents before they start downloading
		boost::thread m_checker_thread;
	};

}

#endif // TORRENT_SESSION_HPP_INCLUDED
