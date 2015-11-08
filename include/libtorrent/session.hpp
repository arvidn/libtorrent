/*

Copyright (c) 2006-2014, Arvid Norberg
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

	// The default values of the session settings are set for a regular
	// bittorrent client running on a desktop system. There are functions that
	// can set the session settings to pre set settings for other environments.
	// These can be used for the basis, and should be tweaked to fit your needs
	// better.
	// 
	// ``min_memory_usage`` returns settings that will use the minimal amount of
	// RAM, at the potential expense of upload and download performance. It
	// adjusts the socket buffer sizes, disables the disk cache, lowers the send
	// buffer watermarks so that each connection only has at most one block in
	// use at any one time. It lowers the outstanding blocks send to the disk
	// I/O thread so that connections only have one block waiting to be flushed
	// to disk at any given time. It lowers the max number of peers in the peer
	// list for torrents. It performs multiple smaller reads when it hashes
	// pieces, instead of reading it all into memory before hashing.
	// 
	// This configuration is inteded to be the starting point for embedded
	// devices. It will significantly reduce memory usage.
	// 
	// ``high_performance_seed`` returns settings optimized for a seed box,
	// serving many peers and that doesn't do any downloading. It has a 128 MB
	// disk cache and has a limit of 400 files in its file pool. It support fast
	// upload rates by allowing large send buffers.
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

	// this is a holder for the internal session implementation object. Once the
	// session destruction is explicitly initiated, this holder is used to
	// synchronize the completion of the shutdown. The lifetime of this object
	// may outlive session, causing the session destructor to not block. The
	// session_proxy destructor will block however, until the underlying session
	// is done shutting down.
	class TORRENT_EXPORT session_proxy
	{
		friend class session;
	public:
		// default constructor, does not refer to any session
		// implementation object.
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

	// The session holds all state that spans multiple torrents. Among other
	// things it runs the network loop and manages all torrents. Once it's
	// created, the session object will spawn the main thread that will do all
	// the work. The main thread will be idle as long it doesn't have any
	// torrents to participate in.
	class TORRENT_EXPORT session: public boost::noncopyable
	{
	public:

		// If the fingerprint in the first overload is omited, the client will get
		// a default fingerprint stating the version of libtorrent. The
		// fingerprint is a short string that will be used in the peer-id to
		// identify the client and the client's version. For more details see the
		// fingerprint class. The first overload (that takes a fingerprint, flags
		// and alert_mask) will listen on INADDR_ANY and let the OS pick a listen
		// port. To listen on a specific interface or specific port, you'll have
		// to call ``session::listen_on()``.
		// 
		// The second overload, that takes a port range and an interface as well
		// as the fingerprint will automatically try to listen on a port on the
		// given interface. For more information about the parameters, see
		// ``listen_on()`` function.
		// 
		// The flags parameter can be used to start default features (upnp &
		// nat-pmp) and default plugins (ut_metadata, ut_pex and smart_ban). The
		// default is to start those things. If you do not want them to start,
		// pass 0 as the flags parameter.
		// 
		// The ``alert_mask`` is the same mask that you would send to
		// set_alert_mask().
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
			TORRENT_ASSERT(listen_port_range.first <= listen_port_range.second);
			init(listen_port_range, listen_interface, print, alert_mask);
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			set_log_path(logpath);
#endif
			start(flags);
		}

		// The destructor of session will notify all trackers that our torrents
		// have been shut down. If some trackers are down, they will time out.
		// All this before the destructor of session returns. So, it's advised
		// that any kind of interface (such as windows) are closed before
		// destructing the session object. Because it can take a few second for
		// it to finish. The timeout can be set with ``set_settings()``.
		~session();

		// TODO: 2 the ip filter should probably be saved here too

		// flags that determines which aspects of the session should be
		// saved when calling save_state().
		enum save_state_flags_t
		{
			// saves settings (i.e. the session_settings)
			save_settings =     0x001,

			// saves dht_settings
			save_dht_settings = 0x002,

			// saves dht state such as nodes and node-id, possibly accelerating
			// joining the DHT if provided at next session startup.
			save_dht_state =    0x004,

			// save proxy_settings
			save_proxy =        0x008,

			// save i2p_proxy settings
			save_i2p_proxy =    0x010,

			// save pe_settings
			save_encryption_settings = 0x020,

			// internal
			save_as_map =       0x040,

			// saves RSS feeds
			save_feeds =        0x080

#ifndef TORRENT_NO_DEPRECATE
			,
			save_dht_proxy = save_proxy,
			save_peer_proxy = save_proxy,
			save_web_proxy = save_proxy,
			save_tracker_proxy = save_proxy
#endif
		};

		// loads and saves all session settings, including dht_settings,
		// encryption settings and proxy settings. ``save_state`` writes all keys
		// to the ``entry`` that's passed in, which needs to either not be
		// initialized, or initialized as a dictionary.
		// 
		// ``load_state`` expects a lazy_entry which can be built from a bencoded
		// buffer with lazy_bdecode().
		// 
		// The ``flags`` arguments passed in to ``save_state`` can be used to
		// filter which parts of the session state to save. By default, all state
		// is saved (except for the individual torrents). see save_state_flags_t
		void save_state(entry& e, boost::uint32_t flags = 0xffffffff) const;
		void load_state(lazy_entry const& e);

		// .. note::
		// 	these calls are potentially expensive and won't scale well with
		// 	lots of torrents. If you're concerned about performance, consider
		// 	using ``post_torrent_updates()`` instead.
		// 
		// ``get_torrent_status`` returns a vector of the torrent_status for
		// every torrent which satisfies ``pred``, which is a predicate function
		// which determines if a torrent should be included in the returned set
		// or not. Returning true means it should be included and false means
		// excluded. The ``flags`` argument is the same as to
		// ``torrent_handle::status()``. Since ``pred`` is guaranteed to be
		// called for every torrent, it may be used to count the number of
		// torrents of different categories as well.
		// 
		// ``refresh_torrent_status`` takes a vector of torrent_status structs
		// (for instance the same vector that was returned by
		// get_torrent_status() ) and refreshes the status based on the
		// ``handle`` member. It is possible to use this function by first
		// setting up a vector of default constructed ``torrent_status`` objects,
		// only initializing the ``handle`` member, in order to request the
		// torrent status for multiple torrents in a single call. This can save a
		// significant amount of time if you have a lot of torrents.
		// 
		// Any torrent_status object whose ``handle`` member is not referring to
		// a valid torrent are ignored.
		void get_torrent_status(std::vector<torrent_status>* ret
			, boost::function<bool(torrent_status const&)> const& pred
			, boost::uint32_t flags = 0) const;
		void refresh_torrent_status(std::vector<torrent_status>* ret
			, boost::uint32_t flags = 0) const;

		// This functions instructs the session to post the state_update_alert,
		// containing the status of all torrents whose state changed since the
		// last time this function was called.
		// 
		// Only torrents who has the state subscription flag set will be
		// included. This flag is on by default. See add_torrent_params.
		void post_torrent_updates();

		// internal
		io_service& get_io_service();

		// ``find_torrent()`` looks for a torrent with the given info-hash. In
		// case there is such a torrent in the session, a torrent_handle to that
		// torrent is returned. In case the torrent cannot be found, an invalid
		// torrent_handle is returned.
		// 
		// See ``torrent_handle::is_valid()`` to know if the torrent was found or
		// not.
		// 
		// ``get_torrents()`` returns a vector of torrent_handles to all the
		// torrents currently in the session.
		torrent_handle find_torrent(sha1_hash const& info_hash) const;
		std::vector<torrent_handle> get_torrents() const;

		// You add torrents through the add_torrent() function where you give an
		// object with all the parameters. The add_torrent() overloads will block
		// until the torrent has been added (or failed to be added) and returns
		// an error code and a torrent_handle. In order to add torrents more
		// efficiently, consider using async_add_torrent() which returns
		// immediately, without waiting for the torrent to add. Notification of
		// the torrent being added is sent as add_torrent_alert.
		// 
		// The overload that does not take an error_code throws an exception on
		// error and is not available when building without exception support.
		// The torrent_handle returned by add_torrent() can be used to retrieve
		// information about the torrent's progress, its peers etc. It is also
		// used to abort a torrent.
		// 
		// If the torrent you are trying to add already exists in the session (is
		// either queued for checking, being checked or downloading)
		// ``add_torrent()`` will throw libtorrent_exception which derives from
		// ``std::exception`` unless duplicate_is_error is set to false. In that
		// case, add_torrent() will return the handle to the existing torrent.
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

		// In case you want to destruct the session asynchrounously, you can
		// request a session destruction proxy. If you don't do this, the
		// destructor of the session object will block while the trackers are
		// contacted. If you keep one ``session_proxy`` to the session when
		// destructing it, the destructor will not block, but start to close down
		// the session, the destructor of the proxy will then synchronize the
		// threads. So, the destruction of the session is performed from the
		// ``session`` destructor call until the ``session_proxy`` destructor
		// call. The ``session_proxy`` does not have any operations on it (since
		// the session is being closed down, no operations are allowed on it).
		// The only valid operation is calling the destructor::
		// 
		// 	class session_proxy
		// 	{
		// 	public:
		// 		session_proxy();
		// 		~session_proxy()
		// 	};
		session_proxy abort() { return session_proxy(m_impl); }

		// Pausing the session has the same effect as pausing every torrent in
		// it, except that torrents will not be resumed by the auto-manage
		// mechanism. Resuming will restore the torrents to their previous paused
		// state. i.e. the session pause state is separate from the torrent pause
		// state. A torrent is inactive if it is paused or if the session is
		// paused.
		void pause();
		void resume();
		bool is_paused() const;

		// returns session wide-statistics and status. For more information, see
		// the ``session_status`` struct.
		session_status status() const;

		// Returns status of the disk cache for this session. For more
		// information, see the cache_status type.
		cache_status get_cache_status() const;

		// fills out the supplied vector with information for
		// each piece that is currently in the disk cache for the torrent with the
		// specified info-hash (``ih``).
		void get_cache_info(sha1_hash const& ih
			, std::vector<cached_piece_info>& ret) const;

		// This adds an RSS feed to the session. The feed will be refreshed
		// regularly and optionally add all torrents from the feed, as they
		// appear.
		//
		// Before adding the feed, you must set the ``url`` field to the feed's
		// url. It may point to an RSS or an atom feed. The returned feed_handle
		// is a handle which is used to interact with the feed, things like
		// forcing a refresh or querying for information about the items in the
		// feed. For more information, see feed_handle.
		feed_handle add_feed(feed_settings const& feed);

		// Removes a feed from being watched by the session. When this
		// call returns, the feed handle is invalid and won't refer
		// to any feed.
		void remove_feed(feed_handle h);

		// Returns a list of all RSS feeds that are being watched by the session.
		void get_feeds(std::vector<feed_handle>& f) const;

		// starts/stops UPnP, NATPMP or LSD port mappers they are stopped by
		// default These functions are not available in case
		// ``TORRENT_DISABLE_DHT`` is defined. ``start_dht`` starts the dht node
		// and makes the trackerless service available to torrents. The startup
		// state is optional and can contain nodes and the node id from the
		// previous session. The dht node state is a bencoded dictionary with the
		// following entries:
		// 
		// nodes
		// 	A list of strings, where each string is a node endpoint encoded in
		// 	binary. If the string is 6 bytes long, it is an IPv4 address of 4
		// 	bytes, encoded in network byte order (big endian), followed by a 2
		// 	byte port number (also network byte order). If the string is 18
		// 	bytes long, it is 16 bytes of IPv6 address followed by a 2 bytes
		// 	port number (also network byte order).
		// 
		// node-id
		// 	The node id written as a readable string as a hexadecimal number.
		// 
		// ``dht_state`` will return the current state of the dht node, this can
		// be used to start up the node again, passing this entry to
		// ``start_dht``. It is a good idea to save this to disk when the session
		// is closed, and read it up again when starting.
		// 
		// If the port the DHT is supposed to listen on is already in use, and
		// exception is thrown, ``asio::error``.
		// 
		// ``stop_dht`` stops the dht node.
		// 
		// ``add_dht_node`` adds a node to the routing table. This can be used if
		// your client has its own source of bootstrapping nodes.
		// 
		// ``set_dht_settings`` sets some parameters availavle to the dht node.
		// See dht_settings for more information.
		//
		// ``is_dht_running()`` returns true if the DHT support has been started
		// and false
		// otherwise.
		// 
		// ``get_dht_settings()`` returns the current settings
		void start_dht();
		void stop_dht();
		void set_dht_settings(dht_settings const& settings);
		bool is_dht_running() const;
		dht_settings get_dht_settings() const;

		// ``add_dht_node`` takes a host name and port pair. That endpoint will be
		// pinged, and if a valid DHT reply is received, the node will be added to
		// the routing table.
		// 
		// ``add_dht_router`` adds the given endpoint to a list of DHT router nodes.
		// If a search is ever made while the routing table is empty, those nodes will
		// be used as backups. Nodes in the router node list will also never be added
		// to the regular routing table, which effectively means they are only used
		// for bootstrapping, to keep the load off them.
		// 
		// An example routing node that you could typically add is
		// ``router.bittorrent.com``.
		void add_dht_node(std::pair<std::string, int> const& node);
		void add_dht_router(std::pair<std::string, int> const& node);

		// query the DHT for an immutable item at the ``target`` hash.
		// the result is posted as a dht_immutable_item_alert.
		void dht_get_item(sha1_hash const& target);

		// query the DHT for a mutable item under the public key ``key``.
		// this is an ed25519 key. ``salt`` is optional and may be left
		// as an empty string if no salt is to be used.
		// if the item is found in the DHT, a dht_mutable_item_alert is
		// posted.
		void dht_get_item(boost::array<char, 32> key
			, std::string salt = std::string());

		// store the given bencoded data as an immutable item in the DHT.
		// the returned hash is the key that is to be used to look the item
		// up agan. It's just the sha-1 hash of the bencoded form of the
		// structure.
		sha1_hash dht_put_item(entry data);

		// store a mutable item. The ``key`` is the public key the blob is
		// to be stored under. The optional ``salt`` argument is a string that
		// is to be mixed in with the key when determining where in the DHT
		// the value is to be stored. The callback function is called from within
		// the libtorrent network thread once we've found where to store the blob,
		// possibly with the current value stored under the key.
		// The values passed to the callback functions are:
		//
		// entry& value
		// 	the current value stored under the key (may be empty). Also expected
		// 	to be set to the value to be stored by the function.
		// 
		// boost::array<char,64>& signature
		// 	the signature authenticating the current value. This may be zeroes
		// 	if there is currently no value stored. The functon is expected to
		// 	fill in this buffer with the signature of the new value to store.
		// 	To generate the signature, you may want to use the
		// 	``sign_mutable_item`` function.
		// 
		// boost::uint64_t& seq
		// 	current sequence number. May be zero if there is no current value.
		// 	The function is expected to set this to the new sequence number of
		// 	the value that is to be stored. Sequence numbers must be monotonically
		// 	increasing. Attempting to overwrite a value with a lower or equal
		// 	sequence number will fail, even if the signature is correct.
		// 
		// std::string const& salt
		// 	this is the salt that was used for this put call.
		// 
		// Since the callback function ``cb`` is called from within libtorrent,
		// it is critical to not perform any blocking operations. Ideally not
		// even locking a mutex. Pass any data required for this function along
		// with the function object's context and make the function entirely
		// self-contained. The only reason data blobs' values are computed
		// via a function instead of just passing in the new value is to avoid
		// race conditions. If you want to *update* the value in the DHT, you
		// must first retrieve it, then modify it, then write it back. The way
		// the DHT works, it is natural to always do a lookup before storing and
		// calling the callback in between is convenient.
		void dht_put_item(boost::array<char, 32> key
			, boost::function<void(entry&, boost::array<char,64>&
				, boost::uint64_t&, std::string const&)> cb
			, std::string salt = std::string());

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.15
		// use save_state and load_state instead
		TORRENT_DEPRECATED_PREFIX
		entry dht_state() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void start_dht(entry const& startup_state) TORRENT_DEPRECATED;
#endif

		// This function adds an extension to this session. The argument is a
		// function object that is called with a ``torrent*`` and which should
		// return a ``boost::shared_ptr<torrent_plugin>``. To write custom
		// plugins, see `libtorrent plugins`_. For the typical bittorrent client
		// all of these extensions should be added. The main plugins implemented
		// in libtorrent are:
		// 
		// metadata extension
		// 	Allows peers to download the metadata (.torren files) from the swarm
		// 	directly. Makes it possible to join a swarm with just a tracker and
		// 	info-hash.
		// 
		// ::
		// 
		// 	#include <libtorrent/extensions/metadata_transfer.hpp>
		// 	ses.add_extension(&libtorrent::create_metadata_plugin);
		// 
		// uTorrent metadata
		// 	Same as ``metadata extension`` but compatible with uTorrent.
		// 
		// ::
		// 
		// 	#include <libtorrent/extensions/ut_metadata.hpp>
		// 	ses.add_extension(&libtorrent::create_ut_metadata_plugin);
		// 
		// uTorrent peer exchange
		// 	Exchanges peers between clients.
		// 
		// ::
		// 
		// 	#include <libtorrent/extensions/ut_pex.hpp>
		// 	ses.add_extension(&libtorrent::create_ut_pex_plugin);
		// 
		// smart ban plugin
		// 	A plugin that, with a small overhead, can ban peers
		// 	that sends bad data with very high accuracy. Should
		// 	eliminate most problems on poisoned torrents.
		// 
		// ::
		// 
		// 	#include <libtorrent/extensions/smart_ban.hpp>
		// 	ses.add_extension(&libtorrent::create_smart_ban_plugin);
		// 
		// 
		// .. _`libtorrent plugins`: libtorrent_plugins.html
		void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(
			torrent*, void*)> ext);
		void add_extension(boost::shared_ptr<plugin> ext);

		// These functions are not available if ``TORRENT_DISABLE_GEO_IP`` is
		// defined. They expects a path to the `MaxMind ASN database`_ and
		// `MaxMind GeoIP database`_ respectively. This will be used to look up
		// which AS and country peers belong to.
		// 
		// ``as_for_ip`` returns the AS number for the IP address specified. If
		// the IP is not in the database or the ASN database is not loaded, 0 is
		// returned.
		// 
		// .. _`MaxMind ASN database`: http://www.maxmind.com/app/asnum
		// .. _`MaxMind GeoIP database`: http://www.maxmind.com/app/geolitecountry
		void load_asnum_db(char const* file);
		void load_country_db(char const* file);
		int as_for_ip(address const& addr);
#ifndef TORRENT_NO_DEPRECATE
#if TORRENT_USE_WSTRING
		// all wstring APIs are deprecated since 0.16.11
		// instead, use the wchar -> utf8 conversion functions
		// and pass in utf8 strings
		TORRENT_DEPRECATED_PREFIX
		void load_country_db(wchar_t const* file) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void load_asnum_db(wchar_t const* file) TORRENT_DEPRECATED;
#endif // TORRENT_USE_WSTRING
#endif // TORRENT_NO_DEPRECATE

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.15
		// use load_state and save_state instead
		TORRENT_DEPRECATED_PREFIX
		void load_state(entry const& ses_state) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		entry state() const TORRENT_DEPRECATED;
#endif

		// Sets a filter that will be used to reject and accept incoming as well
		// as outgoing connections based on their originating ip address. The
		// default filter will allow connections to any ip address. To build a
		// set of rules for which addresses are accepted and not, see ip_filter.
		// 
		// Each time a peer is blocked because of the IP filter, a
		// peer_blocked_alert is generated. ``get_ip_filter()`` Returns the
		// ip_filter currently in the session. See ip_filter.
		void set_ip_filter(ip_filter const& f);
		ip_filter get_ip_filter() const;
		
		// apply port_filter ``f`` to incoming and outgoing peers. a port filter
		// will reject making outgoing peer connections to certain remote ports.
		// The main intention is to be able to avoid triggering certain
		// anti-virus software by connecting to SMTP, FTP ports.
		void set_port_filter(port_filter const& f);

		// sets and gets the raw peer ID used by libtorrent. When anonymous
		// mode is set the peer ID is randomized per peer anyway.
		void set_peer_id(peer_id const& pid);
		peer_id id() const;

		// sets the key sent to trackers. If it's not set, it is initialized
		// by libtorrent. The key may be used by the tracker to identify the
		// peer potentially across you changing your IP.
		void set_key(int key);


		// ``is_listening()`` will tell you whether or not the session has
		// successfully opened a listening port. If it hasn't, this function will
		// return false, and then you can use ``listen_on()`` to make another
		// attempt.
		// 
		// ``listen_port()`` returns the port we ended up listening on. Since you
		// just pass a port-range to the constructor and to ``listen_on()``, to
		// know which port it ended up using, you have to ask the session using
		// this function.
		// 
		// ``listen_on()`` will change the listen port and/or the listen
		// interface. If the session is already listening on a port, this socket
		// will be closed and a new socket will be opened with these new
		// settings. The port range is the ports it will try to listen on, if the
		// first port fails, it will continue trying the next port within the
		// range and so on. The interface parameter can be left as 0, in that
		// case the os will decide which interface to listen on, otherwise it
		// should be the ip-address of the interface you want the listener socket
		// bound to. ``listen_on()`` returns the error code of the operation in
		// ``ec``. If this indicates success, the session is listening on a port
		// within the specified range. If it fails, it will also generate an
		// appropriate alert (listen_failed_alert).
		// 
		// If all ports in the specified range fails to be opened for listening,
		// libtorrent will try to use port 0 (which tells the operating system to
		// pick a port that's free). If that still fails you may see a
		// listen_failed_alert with port 0 even if you didn't ask to listen on
		// it.
		// 
		// It is possible to prevent libtorrent from binding to port 0 by passing
		// in the flag ``session::no_system_port`` in the ``flags`` argument.
		// 
		// The interface parameter can also be a hostname that will resolve to
		// the device you want to listen on. If you don't specify an interface,
		// libtorrent may attempt to listen on multiple interfaces (typically
		// 0.0.0.0 and ::). This means that if your IPv6 interface doesn't work,
		// you may still see a listen_failed_alert, even though the IPv4 port
		// succeeded.
		// 
		// The ``flags`` parameter can either be 0 or
		// ``session::listen_reuse_address``, which will set the reuse address
		// socket option on the listen socket(s). By default, the listen socket
		// does not use reuse address. If you're running a service that needs to
		// run on a specific port no matter if it's in use, set this flag.
		// 
		// If you're also starting the DHT, it is a good idea to do that after
		// you've called ``listen_on()``, since the default listen port for the
		// DHT is the same as the tcp listen socket. If you start the DHT first,
		// it will assume the tcp port is free and open the udp socket on that
		// port, then later, when ``listen_on()`` is called, it may turn out that
		// the tcp port is in use. That results in the DHT and the bittorrent
		// socket listening on different ports. If the DHT is active when
		// ``listen_on`` is called, the udp port will be rebound to the new port,
		// if it was configured to use the same port as the tcp socket, and if
		// the listen_on call failed to bind to the same port that the udp uses.
		// 
		// If you want the OS to pick a port for you, pass in 0 as both first and
		// second.
		// 
		// The reason why it's a good idea to run the DHT and the bittorrent
		// socket on the same port is because that is an assumption that may be
		// used to increase performance. One way to accelerate the connecting of
		// peers on windows may be to first ping all peers with a DHT ping
		// packet, and connect to those that responds first. On windows one can
		// only connect to a few peers at a time because of a built in limitation
		// (in XP Service pack 2).
		void listen_on(
			std::pair<int, int> const& port_range
			, error_code& ec
			, const char* net_interface = 0
			, int flags = 0);
		unsigned short listen_port() const;
		unsigned short ssl_listen_port() const;
		bool is_listening() const;

		// if the listen port failed in some way you can retry to listen on
		// another port- range with this function. If the listener succeeded and
		// is currently listening, a call to this function will shut down the
		// listen port and reopen it using these new properties (the given
		// interface and port range). As usual, if the interface is left as 0
		// this function will return false on failure. If it fails, it will also
		// generate alerts describing the error. It will return true on success.
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

		// flags to be passed in to remove_torrent().
		enum options_t
		{
			// delete the files belonging to the torrent from disk.
			delete_files = 1
		};

		// flags to be passed in to the session constructor
		enum session_flags_t
		{
			// this will add common extensions like ut_pex, ut_metadata, lt_tex
			// smart_ban and possibly others.
			add_default_plugins = 1,

			// this will start features like DHT, local service discovery, UPnP
			// and NAT-PMP.
			start_default_features = 2
		};

		// ``remove_torrent()`` will close all peer connections associated with
		// the torrent and tell the tracker that we've stopped participating in
		// the swarm. This operation cannot fail. When it completes, you will
		// receive a torrent_removed_alert.
		//
		// The optional second argument ``options`` can be used to delete all the
		// files downloaded by this torrent. To do so, pass in the value
		// ``session::delete_files``. The removal of the torrent is asyncronous,
		// there is no guarantee that adding the same torrent immediately after
		// it was removed will not throw a libtorrent_exception exception. Once
		// the torrent is deleted, a torrent_deleted_alert is posted.
		void remove_torrent(const torrent_handle& h, int options = 0);

		// Sets the session settings and the packet encryption settings
		// respectively. See session_settings and pe_settings for more
		// information on available options.
		void set_settings(session_settings const& s);
		session_settings settings() const;
		void set_pe_settings(pe_settings const& settings);
		pe_settings get_pe_settings() const;

		// These functions sets and queries the proxy settings to be used for the
		// session.
		//
		// For more information on what settings are available for proxies, see
		// proxy_settings. If the session is not in anonymous mode, proxies that
		// aren't working or fail, will automatically be disabled and packets
		// will flow without using any proxy. If you want to enforce using a
		// proxy, even when the proxy doesn't work, enable anonymous_mode in
		// session_settings.
		void set_proxy(proxy_settings const& s);
		proxy_settings proxy() const;

#ifdef TORRENT_STATS
		// internal
		void enable_stats_logging(bool s);
#endif

		// ``set_i2p_proxy`` sets the i2p_ proxy, and tries to open a persistant
		// connection to it. The only used fields in the proxy settings structs
		// are ``hostname`` and ``port``.
		//
		// ``i2p_proxy`` returns the current i2p proxy in use.
		//
		// .. _i2p: http://www.i2p2.de
		void set_i2p_proxy(proxy_settings const& s);
		proxy_settings i2p_proxy() const;

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

		// ``pop_alert()`` is used to ask the session if any errors or events has
		// occurred. With set_alert_mask() you can filter which alerts to receive
		// through ``pop_alert()``. For information about the alert categories,
		// see alerts_.
		// 
		// ``pop_alerts()`` pops all pending alerts in a single call. In high
		// performance environments with a very high alert churn rate, this can
		// save significant amount of time compared to popping alerts one at a
		// time. Each call requires one round-trip to the network thread. If
		// alerts are produced in a higher rate than they can be popped (when
		// popped one at a time) it's easy to get stuck in an infinite loop,
		// trying to drain the alert queue. Popping the entire queue at once
		// avoids this problem.
		// 
		// However, the ``pop_alerts`` function comes with significantly more
		// responsibility. You pass in an *empty* ``std::dequeue<alert*>`` to it.
		// If it's not empty, all elements in it will be deleted and then
		// cleared. All currently pending alerts are returned by being swapped
		// into the passed in container. The responsibility of deleting the
		// alerts is transferred to the caller. This means you need to call
		// delete for each item in the returned dequeue. It's probably a good
		// idea to delete the alerts as you handle them, to save one extra pass
		// over the dequeue.
		// 
		// Alternatively, you can pass in the same container the next time you
		// call ``pop_alerts``.
		// 
		// ``wait_for_alert`` blocks until an alert is available, or for no more
		// than ``max_wait`` time. If ``wait_for_alert`` returns because of the
		// time-out, and no alerts are available, it returns 0. If at least one
		// alert was generated, a pointer to that alert is returned. The alert is
		// not popped, any subsequent calls to ``wait_for_alert`` will return the
		// same pointer until the alert is popped by calling ``pop_alert``. This
		// is useful for leaving any alert dispatching mechanism independent of
		// this blocking call, the dispatcher can be called and it can pop the
		// alert independently.
		// 
		// .. note::
		// 	Although these functions are all thread-safe, popping alerts from
		// 	multiple separate threads may introduce race conditions in that
		// 	the thread issuing an asynchronous operation may not be the one
		// 	receiving the alert with the result.
		// 
		// In the python binding, ``wait_for_alert`` takes the number of
		// milliseconds to wait as an integer.
		// 
		// To control the max number of alerts that's queued by the session, see
		// ``session_settings::alert_queue_size``.
		// 
		// save_resume_data_alert and save_resume_data_failed_alert are always
		// posted, regardelss of the alert mask.
		std::auto_ptr<alert> pop_alert();
		void pop_alerts(std::deque<alert*>* alerts);
		alert const* wait_for_alert(time_duration max_wait);

#ifndef TORRENT_NO_DEPRECATE
		TORRENT_DEPRECATED_PREFIX
		void set_severity_level(alert::severity_t s) TORRENT_DEPRECATED;

		TORRENT_DEPRECATED_PREFIX
		size_t set_alert_queue_size_limit(size_t queue_size_limit_) TORRENT_DEPRECATED;
#endif

		// Changes the mask of which alerts to receive. By default only errors
		// are reported. ``m`` is a bitmask where each bit represents a category
		// of alerts.
		//
		// See category_t enum for options.
		void set_alert_mask(boost::uint32_t m);

		// This sets a function to be called (from within libtorrent's netowrk
		// thread) every time an alert is posted. Since the function (``fun``) is
		// run in libtorrent's internal thread, it may not call any of
		// libtorrent's external API functions. Doing so results in a dead lock.
		// 
		// The main intention with this function is to support integration with
		// platform-dependent message queues or signalling systems. For instance,
		// on windows, one could post a message to an HNWD or on linux, write to
		// a pipe or an eventfd.
		void set_alert_dispatch(boost::function<void(std::auto_ptr<alert>)> const& fun);

		// internal
		connection_queue& get_connection_queue();

		// Starts and stops Local Service Discovery. This service will broadcast
		// the infohashes of all the non-private torrents on the local network to
		// look for peers on the same swarm within multicast reach.
		//
		// It is turned off by default.
		void start_lsd();
		void stop_lsd();

		// Starts and stops the UPnP service. When started, the listen port and
		// the DHT port are attempted to be forwarded on local UPnP router
		// devices.
		// 
		// The upnp object returned by ``start_upnp()`` can be used to add and
		// remove arbitrary port mappings. Mapping status is returned through the
		// portmap_alert and the portmap_error_alert. The object will be valid
		// until ``stop_upnp()`` is called. See upnp-and-nat-pmp_.
		// 
		// It is off by default.
		void start_upnp();
		void stop_upnp();

		// protocols used by add_port_mapping()
		enum protocol_type { udp = 1, tcp = 2 };

		// add_port_mapping adds a port forwarding on UPnP and/or NAT-PMP,
		// whichever is enabled. The return value is a handle referring to the
		// port mapping that was just created. Pass it to delete_port_mapping()
		// to remove it.
		int add_port_mapping(protocol_type t, int external_port, int local_port);
		void delete_port_mapping(int handle);

		// Starts and stops the NAT-PMP service. When started, the listen port
		// and the DHT port are attempted to be forwarded on the router through
		// NAT-PMP.
		// 
		// The natpmp object returned by ``start_natpmp()`` can be used to add
		// and remove arbitrary port mappings. Mapping status is returned through
		// the portmap_alert and the portmap_error_alert. The object will be
		// valid until ``stop_natpmp()`` is called. See upnp-and-nat-pmp_.
		// 
		// It is off by default.
		void start_natpmp();
		void stop_natpmp();

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

