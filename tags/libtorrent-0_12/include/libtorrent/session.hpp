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
#include "libtorrent/entry.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/fingerprint.hpp"

#include "libtorrent/resource_request.hpp"

#ifdef _MSC_VER
#	include <eh.h>
#endif

namespace libtorrent
{
	struct torrent_plugin;
	class torrent;
	class ip_filter;


	namespace aux
	{
		// workaround for microsofts
		// hardware exceptions that makes
		// it hard to debug stuff
#ifdef _MSC_VER
		struct eh_initializer
		{
			eh_initializer()
			{
				::_set_se_translator(straight_to_debugger);
			}

			static void straight_to_debugger(unsigned int, _EXCEPTION_POINTERS*)
			{ throw; }
		};
#else
		struct eh_initializer {};
#endif
		struct session_impl;
		
		struct filesystem_init
		{
			filesystem_init();
		};

	}

	class TORRENT_EXPORT session_proxy
	{
		friend class session;
	public:
		session_proxy() {}
	private:
		session_proxy(boost::shared_ptr<aux::session_impl> impl)
			: m_impl(impl) {}
		boost::shared_ptr<aux::session_impl> m_impl;
	};
	
	class TORRENT_EXPORT session: public boost::noncopyable, aux::eh_initializer
	{
	public:

		session(fingerprint const& print = fingerprint("LT"
			, LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR, 0, 0));
		session(
			fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = "0.0.0.0");
			
		~session();

		// returns a list of all torrents in this session
		std::vector<torrent_handle> get_torrents() const;
		
		// returns an invalid handle in case the torrent doesn't exist
		torrent_handle find_torrent(sha1_hash const& info_hash) const;

		// all torrent_handles must be destructed before the session is destructed!
		torrent_handle add_torrent(
			torrent_info const& ti
			, boost::filesystem::path const& save_path
			, entry const& resume_data = entry()
			, bool compact_mode = true
			, int block_size = 16 * 1024);

		// TODO: deprecated, this is for backwards compatibility only
		torrent_handle add_torrent(
			entry const& e
			, boost::filesystem::path const& save_path
			, entry const& resume_data = entry()
			, bool compact_mode = true
			, int block_size = 16 * 1024)
		{
			return add_torrent(torrent_info(e), save_path, resume_data
				, compact_mode, block_size);
		}

		torrent_handle add_torrent(
			char const* tracker_url
			, sha1_hash const& info_hash
			, char const* name
			, boost::filesystem::path const& save_path
			, entry const& resume_data = entry()
			, bool compact_mode = true
			, int block_size = 16 * 1024);

		session_proxy abort() { return session_proxy(m_impl); }

		session_status status() const;

#ifndef TORRENT_DISABLE_DHT
		void start_dht(entry const& startup_state = entry());
		void stop_dht();
		void set_dht_settings(dht_settings const& settings);
		entry dht_state() const;
		void add_dht_node(std::pair<std::string, int> const& node);
		void add_dht_router(std::pair<std::string, int> const& node);
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS

		void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*)> ext);

#endif

		void set_ip_filter(ip_filter const& f);
		void set_peer_id(peer_id const& pid);
		void set_key(int key);

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

		// Get the number of uploads.
		int num_uploads() const;

		// Get the number of connections. This number also contains the
		// number of half open connections.
		int num_connections() const;

		void remove_torrent(const torrent_handle& h);

		void set_settings(session_settings const& s);
		session_settings const& settings();

		int upload_rate_limit() const;
		int download_rate_limit() const;

		void set_upload_rate_limit(int bytes_per_second);
		void set_download_rate_limit(int bytes_per_second);
		void set_max_uploads(int limit);
		void set_max_connections(int limit);
		void set_max_half_open_connections(int limit);

		std::auto_ptr<alert> pop_alert();
		void set_severity_level(alert::severity_t s);

		// Resource management used for global limits.
		resource_request m_ul_bandwidth_quota;
		resource_request m_dl_bandwidth_quota;
		resource_request m_uploads_quota;
		resource_request m_connections_quota;

	private:

		// just a way to initialize boost.filesystem
		// before the session_impl is created
		aux::filesystem_init m_dummy;

		// data shared between the main thread
		// and the working thread
		boost::shared_ptr<aux::session_impl> m_impl;
	};

}

#endif // TORRENT_SESSION_HPP_INCLUDED

