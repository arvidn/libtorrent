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

#include <algorithm>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/limits.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/config.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/fingerprint.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/alert.hpp" // alert::error_notification
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/rss.hpp"
#include "libtorrent/build_config.hpp"

#include "libtorrent/storage.hpp"

#ifdef _MSC_VER
#	include <eh.h>
#endif

#ifdef TORRENT_USE_OPENSSL
// this is a nasty openssl macro
#ifdef set_key
#undef set_key
#endif
#endif

namespace libtorrent
{
	struct plugin;
	struct torrent_plugin;
	class torrent;
	struct ip_filter;
	class port_filter;
	class connection_queue;
	class natpmp;
	class upnp;
	class alert;

	TORRENT_EXPORT session_settings min_memory_usage();
	TORRENT_EXPORT session_settings high_performance_seed();

#ifndef TORRENT_CFG
#error TORRENT_CFG is not defined!
#endif

	void TORRENT_EXPORT TORRENT_CFG();

	namespace aux
	{
		// workaround for microsofts
		// hardware exceptions that makes
		// it hard to debug stuff
#ifdef _MSC_VER
		struct TORRENT_EXPORT eh_initializer
		{
			eh_initializer();
			static void straight_to_debugger(unsigned int, _EXCEPTION_POINTERS*)
			{ throw; }
		};
#else
		struct eh_initializer {};
#endif
		struct session_impl;
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

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
#define TORRENT_LOGPATH_ARG_DEFAULT , std::string logpath = "."
#define TORRENT_LOGPATH_ARG , std::string logpath
#define TORRENT_LOGPATH , logpath
#else
#define TORRENT_LOGPATH_ARG_DEFAULT
#define TORRENT_LOGPATH_ARG
#define TORRENT_LOGPATH
#endif

	class TORRENT_EXPORT session: public boost::noncopyable, aux::eh_initializer
	{
	public:

		session(fingerprint const& print = fingerprint("LT"
			, LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR, 0, 0)
			, int flags = start_default_features | add_default_plugins
			, boost::uint32_t alert_mask = alert::error_notification
			TORRENT_LOGPATH_ARG_DEFAULT)
		{
			TORRENT_CFG();
			init(std::make_pair(0, 0), "0.0.0.0", print, flags, alert_mask TORRENT_LOGPATH);
		}

		session(
			fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = "0.0.0.0"
			, int flags = start_default_features | add_default_plugins
			, int alert_mask = alert::error_notification
			TORRENT_LOGPATH_ARG_DEFAULT)
		{
			TORRENT_CFG();
			TORRENT_ASSERT(listen_port_range.first > 0);
			TORRENT_ASSERT(listen_port_range.first < listen_port_range.second);
			init(listen_port_range, listen_interface, print, flags, alert_mask TORRENT_LOGPATH);
		}
			
		~session();

		enum save_state_flags_t
		{
			save_settings =     0x001,
			save_dht_settings = 0x002,
			save_dht_state =    0x004,
			save_proxy =        0x008,
			save_i2p_proxy =    0x010,
			save_encryption_settings = 0x020,
			save_as_map =       0x040,
			save_feeds =        0x080

#ifndef TORRENT_NO_DEPRECATE
			,
			save_dht_proxy = save_proxy,
			save_peer_proxy = save_proxy,
			save_web_proxy = save_proxy,
			save_tracker_proxy = save_proxy
#endif
		};
		void save_state(entry& e, boost::uint32_t flags = 0xffffffff) const;
		void load_state(lazy_entry const& e);

		void get_torrent_status(std::vector<torrent_status>* ret
			, boost::function<bool(torrent_status const&)> const& pred
			, boost::uint32_t flags = 0) const;
		void refresh_torrent_status(std::vector<torrent_status>* ret
			, boost::uint32_t flags = 0) const;
		void post_torrent_updates();

		// returns a list of all torrents in this session
		std::vector<torrent_handle> get_torrents() const;
		
		io_service& get_io_service();

		// returns an invalid handle in case the torrent doesn't exist
		torrent_handle find_torrent(sha1_hash const& info_hash) const;

		// all torrent_handles must be destructed before the session is destructed!
#ifndef BOOST_NO_EXCEPTIONS
		torrent_handle add_torrent(add_torrent_params const& params);
#endif
		torrent_handle add_torrent(add_torrent_params const& params, error_code& ec);
		void async_add_torrent(add_torrent_params const& params);
		
#ifndef BOOST_NO_EXCEPTIONS
#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.14
		TORRENT_DEPRECATED_PREFIX
		torrent_handle add_torrent(
			torrent_info const& ti
			, std::string const& save_path
			, entry const& resume_data = entry()
			, storage_mode_t storage_mode = storage_mode_sparse
			, bool paused = false
			, storage_constructor_type sc = default_storage_constructor) TORRENT_DEPRECATED;

		// deprecated in 0.14
		TORRENT_DEPRECATED_PREFIX
		torrent_handle add_torrent(
			boost::intrusive_ptr<torrent_info> ti
			, std::string const& save_path
			, entry const& resume_data = entry()
			, storage_mode_t storage_mode = storage_mode_sparse
			, bool paused = false
			, storage_constructor_type sc = default_storage_constructor
			, void* userdata = 0) TORRENT_DEPRECATED;

		// deprecated in 0.14
		TORRENT_DEPRECATED_PREFIX
		torrent_handle add_torrent(
			char const* tracker_url
			, sha1_hash const& info_hash
			, char const* name
			, std::string const& save_path
			, entry const& resume_data = entry()
			, storage_mode_t storage_mode = storage_mode_sparse
			, bool paused = false
			, storage_constructor_type sc = default_storage_constructor
			, void* userdata = 0) TORRENT_DEPRECATED;
#endif
#endif

		session_proxy abort() { return session_proxy(m_impl); }

		void pause();
		void resume();
		bool is_paused() const;

		session_status status() const;
		cache_status get_cache_status() const;

		void get_cache_info(sha1_hash const& ih
			, std::vector<cached_piece_info>& ret) const;

		feed_handle add_feed(feed_settings const& feed);
		void remove_feed(feed_handle h);
		void get_feeds(std::vector<feed_handle>& f) const;

#ifndef TORRENT_DISABLE_DHT
		void start_dht();
		void stop_dht();
		void set_dht_settings(dht_settings const& settings);
#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.15
		// use save_state and load_state instead
		TORRENT_DEPRECATED_PREFIX
		entry dht_state() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void start_dht(entry const& startup_state) TORRENT_DEPRECATED;
#endif
		void add_dht_node(std::pair<std::string, int> const& node);
		void add_dht_router(std::pair<std::string, int> const& node);
		bool is_dht_running() const;
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
		void set_pe_settings(pe_settings const& settings);
		pe_settings get_pe_settings() const;
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> ext);
		void add_extension(boost::shared_ptr<plugin> ext);
#endif

#ifndef TORRENT_DISABLE_GEO_IP
		int as_for_ip(address const& addr);
		void load_asnum_db(char const* file);
		void load_country_db(char const* file);
#if TORRENT_USE_WSTRING
		// all wstring APIs are deprecated since 0.16.11
		// instead, use the wchar -> utf8 conversion functions
		// and pass in utf8 strings
#ifndef TORRENT_NO_DEPRECATE
		TORRENT_DEPRECATED_PREFIX
		void load_country_db(wchar_t const* file) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void load_asnum_db(wchar_t const* file) TORRENT_DEPRECATED;
#endif // TORRENT_NO_DEPRECATE
#endif // TORRENT_USE_WSTRING
#endif // TORRENT_DISABLE_GEO_IP

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.15
		// use load_state and save_state instead
		TORRENT_DEPRECATED_PREFIX
		void load_state(entry const& ses_state) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		entry state() const TORRENT_DEPRECATED;
#endif

		void set_ip_filter(ip_filter const& f);
		ip_filter get_ip_filter() const;
		
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
		enum listen_on_flags_t
		{
#ifndef TORRENT_NO_DEPRECATE
			// this is always on starting with 0.16.2
			listen_reuse_address = 0x01,
#endif
			listen_no_system_port = 0x02
		};

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.16
		TORRENT_DEPRECATED_PREFIX
		bool listen_on(
			std::pair<int, int> const& port_range
			, const char* net_interface = 0
			, int flags = 0) TORRENT_DEPRECATED;
#endif

		void listen_on(
			std::pair<int, int> const& port_range
			, error_code& ec
			, const char* net_interface = 0
			, int flags = 0);

		// returns the port we ended up listening on
		unsigned short listen_port() const;
		unsigned short ssl_listen_port() const;

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
		session_settings settings() const;

		void set_proxy(proxy_settings const& s);
		proxy_settings proxy() const;

#ifdef TORRENT_STATS
		void enable_stats_logging(bool s);
#endif

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.16
		// Get the number of uploads.
		TORRENT_DEPRECATED_PREFIX
		int num_uploads() const TORRENT_DEPRECATED;

		// Get the number of connections. This number also contains the
		// number of half open connections.
		TORRENT_DEPRECATED_PREFIX
		int num_connections() const TORRENT_DEPRECATED;

		// deprecated in 0.15.
		TORRENT_DEPRECATED_PREFIX
		void set_peer_proxy(proxy_settings const& s) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void set_web_seed_proxy(proxy_settings const& s) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void set_tracker_proxy(proxy_settings const& s) TORRENT_DEPRECATED;

		TORRENT_DEPRECATED_PREFIX
		proxy_settings peer_proxy() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		proxy_settings web_seed_proxy() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		proxy_settings tracker_proxy() const TORRENT_DEPRECATED;

#ifndef TORRENT_DISABLE_DHT
		TORRENT_DEPRECATED_PREFIX
		void set_dht_proxy(proxy_settings const& s) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		proxy_settings dht_proxy() const TORRENT_DEPRECATED;
#endif
#endif // TORRENT_NO_DEPRECATE

#if TORRENT_USE_I2P
		void set_i2p_proxy(proxy_settings const& s);
		proxy_settings i2p_proxy() const;
#endif

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.16
		TORRENT_DEPRECATED_PREFIX
		int upload_rate_limit() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		int download_rate_limit() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		int local_upload_rate_limit() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		int local_download_rate_limit() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		int max_half_open_connections() const TORRENT_DEPRECATED;

		TORRENT_DEPRECATED_PREFIX
		void set_local_upload_rate_limit(int bytes_per_second) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void set_local_download_rate_limit(int bytes_per_second) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void set_upload_rate_limit(int bytes_per_second) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void set_download_rate_limit(int bytes_per_second) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void set_max_uploads(int limit) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void set_max_connections(int limit) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void set_max_half_open_connections(int limit) TORRENT_DEPRECATED;

		TORRENT_DEPRECATED_PREFIX
		int max_connections() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		int max_uploads() const TORRENT_DEPRECATED;
#endif

		// pop one alert from the alert queue, or do nothing
		// and return a NULL pointer if there are no alerts
		// in the queue
		std::auto_ptr<alert> pop_alert();

		// pop all alerts in the alert queue and returns them
		// in the supplied dequeue 'alerts'. The passed in
		// queue must be empty when passed in.
		// the responsibility of individual alerts returned
		// in the dequeue is passed on to the caller of this function.
		// when you're done with reacting to the alerts, you need to
		// delete them all.
		void pop_alerts(std::deque<alert*>* alerts);

#ifndef TORRENT_NO_DEPRECATE
		TORRENT_DEPRECATED_PREFIX
		void set_severity_level(alert::severity_t s) TORRENT_DEPRECATED;

		TORRENT_DEPRECATED_PREFIX
		size_t set_alert_queue_size_limit(size_t queue_size_limit_) TORRENT_DEPRECATED;
#endif
		void set_alert_mask(boost::uint32_t m);

		alert const* wait_for_alert(time_duration max_wait);
		void set_alert_dispatch(boost::function<void(std::auto_ptr<alert>)> const& fun);

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

		void init(std::pair<int, int> listen_range, char const* listen_interface
			, fingerprint const& id, int flags, boost::uint32_t alert_mask TORRENT_LOGPATH_ARG);

		// data shared between the main thread
		// and the working thread
		boost::shared_ptr<aux::session_impl> m_impl;
	};

}

#endif // TORRENT_SESSION_HPP_INCLUDED

