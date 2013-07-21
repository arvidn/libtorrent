/*

Copyright (c) 2006-2012, Arvid Norberg
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
	class alert;

	TORRENT_EXPORT session_settings min_memory_usage();
	TORRENT_EXPORT session_settings high_performance_seed();

#ifndef TORRENT_CFG
#error TORRENT_CFG is not defined!
#endif

	void TORRENT_EXPORT TORRENT_CFG();

	namespace aux
	{
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
#else
#define TORRENT_LOGPATH_ARG_DEFAULT
#endif

	// The session holds all state that spans multiple torrents. Among other things it runs the network
	// loop and manages all torrents.
	// Once it's created, the session object will spawn the main thread that will do all the work.
	// The main thread will be idle as long it doesn't have any torrents to participate in.
	class TORRENT_EXPORT session: public boost::noncopyable
	{
	public:

		// If the fingerprint in the first overload is omited, the client will get a default
		// fingerprint stating the version of libtorrent. The fingerprint is a short string that will be
		// used in the peer-id to identify the client and the client's version. For more details see the
		// fingerprint_ class. The constructor that only takes a fingerprint will not open a
		// listen port for the session, to get it running you'll have to call ``session::listen_on()``.
		// The other constructor, that takes a port range and an interface as well as the fingerprint
		// will automatically try to listen on a port on the given interface. For more information about
		// the parameters, see ``listen_on()`` function.
		// 
		// The flags paramater can be used to start default features (upnp & nat-pmp) and default plugins
		// (ut_metadata, ut_pex and smart_ban). The default is to start those things. If you do not want
		// them to start, pass 0 as the flags parameter.
		// 
		// The ``alert_mask`` is the same mask that you would send to `set_alert_mask()`_.
		session(fingerprint const& print = fingerprint("LT"
			, LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR, 0, 0)
			, int flags = start_default_features | add_default_plugins
			, boost::uint32_t alert_mask = alert::error_notification
			TORRENT_LOGPATH_ARG_DEFAULT)
		{
			TORRENT_CFG();
			init(std::make_pair(0, 0), "0.0.0.0", print, alert_mask);
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			set_log_path(logpath);
#endif
			start(flags);
		}
		session(fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = "0.0.0.0"
			, int flags = start_default_features | add_default_plugins
			, int alert_mask = alert::error_notification
			TORRENT_LOGPATH_ARG_DEFAULT)
		{
			TORRENT_CFG();
			TORRENT_ASSERT(listen_port_range.first > 0);
			TORRENT_ASSERT(listen_port_range.first < listen_port_range.second);
			init(listen_port_range, listen_interface, print, alert_mask);
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			set_log_path(logpath);
#endif
			start(flags);
		}
			
		// The destructor of session will notify all trackers that our torrents have been shut down.
		// If some trackers are down, they will time out. All this before the destructor of session
		// returns. So, it's advised that any kind of interface (such as windows) are closed before
		// destructing the session object. Because it can take a few second for it to finish. The
		// timeout can be set with ``set_settings()``.
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

		// loads and saves all session settings, including dht_settings, encryption settings and proxy
		// settings. ``save_state`` writes all keys to the ``entry`` that's passed in, which needs to
		// either not be initialized, or initialized as a dictionary.
		// 
		// ``load_state`` expects a ``lazy_entry`` which can be built from a bencoded buffer with
		// `lazy_bdecode()`_.
		// 
		// The ``flags`` arguments passed in to ``save_state`` can be used to filter which parts
		// of the session state to save. By default, all state is saved (except for the individual
		// torrents). see save_state_flags_t
		void save_state(entry& e, boost::uint32_t flags = 0xffffffff) const;
		void load_state(lazy_entry const& e);

		// .. note::
		// 	these calls are potentially expensive and won't scale well
		// 	with lots of torrents. If you're concerned about performance, consider
		// 	using ``post_torrent_updates()`` instead.
		// 
		// ``get_torrent_status`` returns a vector of the ``torrent_status`` for every
		// torrent which satisfies ``pred``, which is a predicate function which determines
		// if a torrent should be included in the returned set or not. Returning true means
		// it should be included and false means excluded. The ``flags`` argument is the same
		// as to ``torrent_handle::status()``. Since ``pred`` is guaranteed to be called for
		// every torrent, it may be used to count the number of torrents of different categories
		// as well.
		// 
		// ``refresh_torrent_status`` takes a vector of ``torrent_status`` structs (for instance
		// the same vector that was returned by ``get_torrent_status()``) and refreshes the
		// status based on the ``handle`` member. It is possible to use this function by
		// first setting up a vector of default constructed ``torrent_status`` objects, only
		// initializing the ``handle`` member, in order to request the torrent status for
		// multiple torrents in a single call. This can save a significant amount of time
		// if you have a lot of torrents.
		// 
		// Any ``torrent_status`` object whose ``handle`` member is not referring to a
		// valid torrent are ignored.
		void get_torrent_status(std::vector<torrent_status>* ret
			, boost::function<bool(torrent_status const&)> const& pred
			, boost::uint32_t flags = 0) const;
		void refresh_torrent_status(std::vector<torrent_status>* ret
			, boost::uint32_t flags = 0) const;

		// This functions instructs the session to post the state_update_alert_, containing
		// the status of all torrents whose state changed since the last time this function
		// was called.
		// 
		// Only torrents who has the state subscription flag set will be included. This flag
		// is on by default. See ``add_torrent_params`` under `async_add_torrent() add_torrent()`_
		void post_torrent_updates();

		io_service& get_io_service();

		// ``find_torrent()`` looks for a torrent with the given info-hash. In case there
		// is such a torrent in the session, a torrent_handle to that torrent is returned.
		// In case the torrent cannot be found, an invalid torrent_handle is returned.
		// 
		// See ``torrent_handle::is_valid()`` to know if the torrent was found or not.
		// 
		// ``get_torrents()`` returns a vector of torrent_handles to all the torrents
		// currently in the session.
		torrent_handle find_torrent(sha1_hash const& info_hash) const;
		std::vector<torrent_handle> get_torrents() const;

		// You add torrents through the ``add_torrent()`` function where you give an
		// object with all the parameters. The ``add_torrent()`` overloads will block
		// until the torrent has been added (or failed to be added) and returns an
		// error code and a ``torrent_handle``. In order to add torrents more efficiently,
		// consider using ``async_add_torrent()`` which returns immediately, without
		// waiting for the torrent to add. Notification of the torrent being added is sent
		// as add_torrent_alert_.
		// 
		// The overload that does not take an ``error_code`` throws an exception on
		// error and is not available when building without exception support.
		// The torrent_handle_ returned by ``add_torrent()`` can be used to retrieve information
		// about the torrent's progress, its peers etc. It is also used to abort a torrent.
		// 
		// If the torrent you are trying to add already exists in the session (is either queued
		// for checking, being checked or downloading) ``add_torrent()`` will throw
		// libtorrent_exception_ which derives from ``std::exception`` unless ``duplicate_is_error``
		// is set to false. In that case, ``add_torrent`` will return the handle to the existing
		// torrent.
		//
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

		// In case you want to destruct the session asynchrounously, you can request a session
		// destruction proxy. If you don't do this, the destructor of the session object will
		// block while the trackers are contacted. If you keep one ``session_proxy`` to the
		// session when destructing it, the destructor will not block, but start to close down
		// the session, the destructor of the proxy will then synchronize the threads. So, the
		// destruction of the session is performed from the ``session`` destructor call until the
		// ``session_proxy`` destructor call. The ``session_proxy`` does not have any operations
		// on it (since the session is being closed down, no operations are allowed on it). The
		// only valid operation is calling the destructor::
		// 
		// 	class session_proxy
		// 	{
		// 	public:
		// 		session_proxy();
		// 		~session_proxy()
		// 	};
		session_proxy abort() { return session_proxy(m_impl); }

		// Pausing the session has the same effect as pausing every torrent in it, except that
		// torrents will not be resumed by the auto-manage mechanism. Resuming will restore the
		// torrents to their previous paused state. i.e. the session pause state is separate from
		// the torrent pause state. A torrent is inactive if it is paused or if the session is
		// paused.
		void pause();
		void resume();
		bool is_paused() const;

		// returns session wide-statistics and status. For more information, see the ``session_status`` struct.
		session_status status() const;

		// Returns status of the disk cache for this session.
		// For more information, see the cache_status type.
		cache_status get_cache_status() const;

		// fills out the supplied vector with information for
		// each piece that is currently in the disk cache for the torrent with the
		// specified info-hash (``ih``).
		void get_cache_info(sha1_hash const& ih
			, std::vector<cached_piece_info>& ret) const;

		feed_handle add_feed(feed_settings const& feed);
		void remove_feed(feed_handle h);
		void get_feeds(std::vector<feed_handle>& f) const;

		void start_dht();
		void stop_dht();
		void set_dht_settings(dht_settings const& settings);
		void add_dht_node(std::pair<std::string, int> const& node);
		void add_dht_router(std::pair<std::string, int> const& node);
		bool is_dht_running() const;
#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.15
		// use save_state and load_state instead
		TORRENT_DEPRECATED_PREFIX
		entry dht_state() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void start_dht(entry const& startup_state) TORRENT_DEPRECATED;
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
		void set_pe_settings(pe_settings const& settings);
		pe_settings get_pe_settings() const;
#endif

		void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> ext);
		void add_extension(boost::shared_ptr<plugin> ext);

		// These functions are not available if ``TORRENT_DISABLE_GEO_IP`` is defined. They
		// expects a path to the `MaxMind ASN database`_ and `MaxMind GeoIP database`_
		// respectively. This will be used to look up which AS and country peers belong to.
		// 
		// ``as_for_ip`` returns the AS number for the IP address specified. If the IP is not
		// in the database or the ASN database is not loaded, 0 is returned.
		// 
		// The ``wchar_t`` overloads are for wide character paths.
		// 
		// .. _`MaxMind ASN database`: http://www.maxmind.com/app/asnum
		// .. _`MaxMind GeoIP database`: http://www.maxmind.com/app/geolitecountry
#ifndef TORRENT_DISABLE_GEO_IP
		void load_asnum_db(char const* file);
		void load_country_db(char const* file);
		int as_for_ip(address const& addr);
#if TORRENT_USE_WSTRING
		void load_country_db(wchar_t const* file);
		void load_asnum_db(wchar_t const* file);
#endif
#endif

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.15
		// use load_state and save_state instead
		TORRENT_DEPRECATED_PREFIX
		void load_state(entry const& ses_state) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		entry state() const TORRENT_DEPRECATED;
#endif

		// Sets a filter that will be used to reject and accept incoming as well as outgoing
		// connections based on their originating ip address. The default filter will allow
		// connections to any ip address. To build a set of rules for which addresses are
		// accepted and not, see ip_filter_.
		// 
		// Each time a peer is blocked because of the IP filter, a peer_blocked_alert_ is
		// generated.
		// ``get_ip_filter()`` Returns the ip_filter currently in the session. See ip_filter_.
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

		// ``remove_torrent()`` will close all peer connections associated with the torrent and tell
		// the tracker that we've stopped participating in the swarm. The optional second argument
`		// `options`` can be used to delete all the files downloaded by this torrent. To do this, pass
		// in the value ``session::delete_files``. The removal of the torrent is asyncronous, there is
		// no guarantee that adding the same torrent immediately after it was removed will not throw
		// a libtorrent_exception_ exception. Once the torrent is deleted, a torrent_deleted_alert_
		// is posted.
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

		TORRENT_DEPRECATED_PREFIX
		void set_dht_proxy(proxy_settings const& s) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		proxy_settings dht_proxy() const TORRENT_DEPRECATED;
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
		void start_natpmp();
		void start_upnp();

		void stop_lsd();
		void stop_natpmp();
		void stop_upnp();
		
	private:

		void init(std::pair<int, int> listen_range, char const* listen_interface
			, fingerprint const& id, boost::uint32_t alert_mask);
		void set_log_path(std::string const& p);
		void start(int flags);

		// data shared between the main thread
		// and the working thread
		boost::shared_ptr<aux::session_impl> m_impl;
	};

}

#endif // TORRENT_SESSION_HPP_INCLUDED

