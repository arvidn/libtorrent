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

#include <boost/limits.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/thread.hpp>

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/policy.hpp"
#include "libtorrent/url_handler.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/fingerprint.hpp"
#include "libtorrent/debug.hpp"

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
#endif
				::_set_se_translator(straight_to_debugger);
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
			checker_impl(session_impl* s): m_ses(s), m_abort(false) {}
			void operator()();
			piece_checker_data* find_torrent(const sha1_hash& info_hash);

			// when the files has been checked
			// the torrent is added to the session
			session_impl* m_ses;

			boost::mutex m_mutex;
			boost::condition m_cond;

			// a list of all torrents that are currently checking
			// their files (in separate threads)
			std::deque<piece_checker_data> m_torrents;

			bool m_abort;
		};

		// this is the link between the main thread and the
		// thread started to run the main downloader loop
		struct session_impl: boost::noncopyable
		{
			typedef std::map<boost::shared_ptr<socket>, boost::shared_ptr<peer_connection> > connection_map;

			session_impl(int listen_port, const fingerprint& cl_fprint);
			void operator()();

			// must be locked to access the data
			// in this struct
			boost::mutex m_mutex;
			torrent* find_torrent(const sha1_hash& info_hash);
			const peer_id& get_peer_id() const { return m_peer_id; }

			tracker_manager m_tracker_manager;
			std::map<sha1_hash, boost::shared_ptr<torrent> > m_torrents;

			// this maps sockets to their peer_connection
			// object. It is the complete list of all connected
			// peers.
			connection_map m_connections;

			// this is a list of iterators into the m_connections map
			// that should be disconnected as soon as possible.
			// It is used to delay disconnections to avoid troubles
			// in loops that iterate over them.
			std::vector<connection_map::iterator> m_disconnect_peer;

			// the peer id that is generated at the start of each torrent
			peer_id m_peer_id;

			// the port we are listening on for connections
			int m_listen_port;

			// this is where all active sockets are stored.
			// the selector can sleep while there's no activity on
			// them
			selector m_selector;

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
			void assert_invariant(int marker = -1);
			boost::shared_ptr<logger> create_log(std::string name);
			boost::shared_ptr<logger> m_logger;
#endif
		};

	}

	struct http_settings;

	class session: public boost::noncopyable, detail::eh_initializer
	{
	public:

		session(int listen_port, const fingerprint& print);
		session(int listen_port);

		~session();

		// all torrent_handles must be destructed before the session is destructed!
		torrent_handle add_torrent(
			const torrent_info& ti
			, const boost::filesystem::path& save_path
			, const entry& resume_data = entry());

		void remove_torrent(const torrent_handle& h);

		void set_http_settings(const http_settings& s);
		void set_upload_rate_limit(int bytes_per_second);

		// TODO: add a session_status that contain
		// some indication of if the listen-port works

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
