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

/*
	implemented

	* http-proxy authentication for tracker requests
	* multitracker support
	* spawns separate temporary threads for file checking
	* 

	missing

	* endgame-mode
	* correct algorithm for choking and unchoking

*/

#ifndef TORRENT_SESION_HPP_INCLUDED
#define TORRENT_SESSION_HPP_INCLUDED

#include <ctime>
#include <algorithm>
#include <vector>
#include <set>
#include <list>

#include <boost/limits.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/thread.hpp>

#include "libtorrent/torrent.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/policy.hpp"
#include "libtorrent/url_handler.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/debug.hpp"


// TODO: if we're not interested and the peer isn't interested, close the connections
// TODO: instead of implementing end-game mode, have an algorithm that
// constantly prioritizes high-bandwidth sources.

namespace libtorrent
{

	namespace detail
	{

		struct piece_checker_data
		{
			boost::shared_ptr<torrent> torrent_ptr;
			std::string save_path;

			// when the files has been checked
			// the torrent is added to the session
			session_impl* ses;

			sha1_hash info_hash;

			// must be locked to access the data
			// below in this struct
			boost::mutex mutex;

			float progress;
		};

		struct piece_check_thread
		{
			piece_check_thread(const boost::shared_ptr<piece_checker_data>& p)
				: m_data(p)
			{}
			void operator()();
			boost::shared_ptr<piece_checker_data> m_data;
		};


		// this is the link between the main thread and the
		// thread started to run the main downloader loop
		struct session_impl
		{
			typedef std::map<boost::shared_ptr<socket>, boost::shared_ptr<peer_connection> > connection_map;

			session_impl()
				: m_abort(false)
				, m_tracker_manager(m_settings)
			{}

			// must be locked to access the data
			// in this struct
			boost::mutex m_mutex;

			tracker_manager m_tracker_manager;
			std::map<sha1_hash, boost::shared_ptr<torrent> > m_torrents;
			connection_map m_connections;

			// a list of all torrents that are currently checking
			// their files (in separate threads)
			std::map<sha1_hash, boost::shared_ptr<piece_checker_data> > m_checkers;
			boost::thread_group m_checker_threads;

			// the peer id that is generated at the start of each torrent
			peer_id m_peer_id;

			// this is where all active sockets are stored.
			// the selector can sleep while there's no activity on
			// them
			selector m_selector;

			// the settings for the client
			http_settings m_settings;

			bool m_abort;
			
			void run(int listen_port);

			torrent* find_torrent(const sha1_hash& info_hash);
			const peer_id& get_peer_id() const { return m_peer_id; }

#if defined(TORRENT_VERBOSE_LOGGING)
			boost::shared_ptr<logger> create_log(std::string name)
			{
				name += ".log";
				// current options are file_logger and cout_logger
				return boost::shared_ptr<logger>(new file_logger(name.c_str()));
			}

			boost::shared_ptr<logger> m_logger;
#endif
		};

		struct main_loop_thread
		{
			main_loop_thread(int listen_port, session_impl* s)
				: m_ses(s), m_listen_port(listen_port)
			{}

			void operator()()
			{
				try
				{
					m_ses->run(m_listen_port);
				}
				catch(...)
				{
					assert(false);
				}
			}

			session_impl* m_ses;
			int m_listen_port;
		};

	}

	struct http_settings;

	std::string extract_fingerprint(const peer_id& p);

	struct torrent_handle
	{
		friend class session;

		torrent_handle(): m_ses(0) {}

		float progress() const;
		void get_peer_info(std::vector<peer_info>& v);
		void abort();
		enum state_t
		{
			checking_files,
			connecting_to_tracker,
			downloading,
			seeding
		};
//		state_t state() const;

	private:

		torrent_handle(detail::session_impl* s, const sha1_hash& h)
			: m_ses(s)
			, m_info_hash(h)
		{}

		detail::session_impl* m_ses;
		sha1_hash m_info_hash; // should be replaced with a torrent*?

	};

	class session: public boost::noncopyable
	{
	public:

		session(int listen_port)
			: m_thread(detail::main_loop_thread(listen_port, &m_impl)) {}

		~session();

		// all torrent_handles must be destructed before the session is destructed!
		torrent_handle add_torrent(const torrent_info& ti, const std::string& save_path);

		void set_http_settings(const http_settings& s);

	private:

		// data shared between the threads
		detail::session_impl m_impl;

		boost::thread m_thread;

	};

}

#endif // TORRENT_SESSION_HPP_INCLUDED
