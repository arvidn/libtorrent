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

#include "libtorrent/config.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/fingerprint.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/peer_id.hpp"

#include "libtorrent/storage.hpp"

#ifdef _MSC_VER
#	include <eh.h>
#endif

namespace libtorrent
{
	struct torrent_plugin;
	class torrent;
	class ip_filter;
	class port_filter;
	class connection_queue;
	class natpmp;
	class upnp;

	namespace fs = boost::filesystem;

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

	struct add_torrent_params
	{
		add_torrent_params(storage_constructor_type sc = default_storage_constructor)
			: tracker_url(0)
			, name(0)
			, resume_data(0)
			, storage_mode(storage_mode_sparse)
			, paused(true)
			, auto_managed(true)
			, duplicate_is_error(false)
			, storage(sc)
			, userdata(0)
		{}

		boost::intrusive_ptr<torrent_info> ti;
		char const* tracker_url;
		sha1_hash info_hash;
		char const* name;
		fs::path save_path;
		std::vector<char>* resume_data;
		storage_mode_t storage_mode;
		bool paused;
		bool auto_managed;
		bool duplicate_is_error;
		storage_constructor_type storage;
		void* userdata;
	};
	
	class TORRENT_EXPORT session: public boost::noncopyable, aux::eh_initializer
	{
	public:

		session(fingerprint const& print = fingerprint("LT"
			, LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR, 0, 0)
			, int flags = start_default_features | add_default_plugins
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			, fs::path logpath = "."
#endif
				);
		session(
			fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = "0.0.0.0"
			, int flags = start_default_features | add_default_plugins
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			, fs::path logpath = "."
#endif
			);
			
		~session();

		// returns a list of all torrents in this session
		std::vector<torrent_handle> get_torrents() const;
		
		// returns an invalid handle in case the torrent doesn't exist
		torrent_handle find_torrent(sha1_hash const& info_hash) const;

		// all torrent_handles must be destructed before the session is destructed!
		torrent_handle add_torrent(add_torrent_params const& params);
		
#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.14
		torrent_handle add_torrent(
			torrent_info const& ti
			, fs::path const& save_path
			, entry const& resume_data = entry()
			, storage_mode_t storage_mode = storage_mode_sparse
			, bool paused = false
			, storage_constructor_type sc = default_storage_constructor) TORRENT_DEPRECATED;

		// deprecated in 0.14
		torrent_handle add_torrent(
			boost::intrusive_ptr<torrent_info> ti
			, fs::path const& save_path
			, entry const& resume_data = entry()
			, storage_mode_t storage_mode = storage_mode_sparse
			, bool paused = false
			, storage_constructor_type sc = default_storage_constructor
			, void* userdata = 0) TORRENT_DEPRECATED;

		// deprecated in 0.14
		torrent_handle add_torrent(
			char const* tracker_url
			, sha1_hash const& info_hash
			, char const* name
			, fs::path const& save_path
			, entry const& resume_data = entry()
			, storage_mode_t storage_mode = storage_mode_sparse
			, bool paused = false
			, storage_constructor_type sc = default_storage_constructor
			, void* userdata = 0) TORRENT_DEPRECATED;
#endif

		session_proxy abort() { return session_proxy(m_impl); }

		void pause();
		void resume();
		bool is_paused() const;

		session_status status() const;
		cache_status get_cache_status() const;

		void get_cache_info(sha1_hash const& ih
			, std::vector<cached_piece_info>& ret) const;

#ifndef TORRENT_DISABLE_DHT
		void start_dht(entry const& startup_state = entry());
		void stop_dht();
		void set_dht_settings(dht_settings const& settings);
		entry dht_state() const;
		void add_dht_node(std::pair<std::string, int> const& node);
		void add_dht_router(std::pair<std::string, int> const& node);
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
		void set_pe_settings(pe_settings const& settings);
		pe_settings const& get_pe_settings() const;
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> ext);
#endif

#ifndef TORRENT_DISABLE_GEO_IP
		int as_for_ip(address const& addr);
		bool load_asnum_db(char const* file);
		bool load_country_db(char const* file);
#endif

		void load_state(entry const& ses_state);
		entry state() const;

		void set_ip_filter(ip_filter const& f);
		ip_filter const& get_ip_filter() const;
		
		void set_port_filter(port_filter const& f);
		void set_peer_id(peer_id const& pid);
		void set_key(int key);
		peer_id id() const;

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

		enum options_t
		{
			none = 0,
			delete_files = 1
		};

		enum session_flags_t
		{
			add_default_plugins = 1,
			start_default_features = 2
		};

		void remove_torrent(const torrent_handle& h, int options = none);

		void set_settings(session_settings const& s);
		session_settings const& settings();

		void set_peer_proxy(proxy_settings const& s);
		void set_web_seed_proxy(proxy_settings const& s);
		void set_tracker_proxy(proxy_settings const& s);

		proxy_settings const& peer_proxy() const;
		proxy_settings const& web_seed_proxy() const;
		proxy_settings const& tracker_proxy() const;

#ifndef TORRENT_DISABLE_DHT
		void set_dht_proxy(proxy_settings const& s);
		proxy_settings const& dht_proxy() const;
#endif

		int upload_rate_limit() const;
		int download_rate_limit() const;
		int max_half_open_connections() const;

		void set_upload_rate_limit(int bytes_per_second);
		void set_download_rate_limit(int bytes_per_second);
		void set_max_uploads(int limit);
		void set_max_connections(int limit);
		void set_max_half_open_connections(int limit);

		std::auto_ptr<alert> pop_alert();
#ifndef TORRENT_NO_DEPRECATE
		void set_severity_level(alert::severity_t s) TORRENT_DEPRECATED;
#endif
		void set_alert_mask(int m);
		size_t set_alert_queue_size_limit(size_t queue_size_limit_);

		alert const* wait_for_alert(time_duration max_wait);

		connection_queue& get_connection_queue();

		// starts/stops UPnP, NATPMP or LSD port mappers
		// they are stopped by default
		void start_lsd();
		natpmp* start_natpmp();
		upnp* start_upnp();

		void stop_lsd();
		void stop_natpmp();
		void stop_upnp();
		
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

