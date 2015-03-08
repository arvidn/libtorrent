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
#include "libtorrent/version.hpp"
#include "libtorrent/fingerprint.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/alert.hpp" // alert::error_notification
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/rss.hpp"
#include "libtorrent/peer_class.hpp"
#include "libtorrent/peer_class_type_filter.hpp"
#include "libtorrent/build_config.hpp"

#include "libtorrent/storage.hpp"
#include "libtorrent/session_settings.hpp"

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

#ifndef TORRENT_NO_DEPRECATE
	struct session_status;
#endif

	// describes one statistics metric from the session. For more information,
	// see the session-statistics_ section.
	struct TORRENT_EXPORT stats_metric
	{
		char const* name;
		int value_index;
		enum { type_counter, type_gauge };
		int type;
	};

	typedef boost::function<void(sha1_hash const&, std::vector<char>&
		, error_code&)> user_load_function_t;

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
	TORRENT_EXPORT void min_memory_usage(settings_pack& set);
	TORRENT_EXPORT void high_performance_seed(settings_pack& set);

#ifndef TORRENT_NO_DEPRECATE
	TORRENT_DEPRECATED_PREFIX
	TORRENT_EXPORT session_settings min_memory_usage() TORRENT_DEPRECATED;
	TORRENT_DEPRECATED_PREFIX
	TORRENT_EXPORT session_settings high_performance_seed() TORRENT_DEPRECATED;
#endif

#ifndef TORRENT_CFG
#error TORRENT_CFG is not defined!
#endif

	// given a name of a metric, this function returns the counter index of it,
	// or -1 if it could not be found. The counter index is the index into the
	// values array returned by session_stats_alert.
	TORRENT_EXPORT int find_metric_idx(char const* name);

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

	// This free function returns the list of available metrics exposed by
	// libtorrent's statistics API. Each metric has a name and a *value index*.
	// The value index is the index into the array in session_stats_alert where
	// this metric's value can be found when the session stats is sampled (by
	// calling post_session_stats()).
	TORRENT_EXPORT std::vector<stats_metric> session_stats_metrics();

	// The session holds all state that spans multiple torrents. Among other
	// things it runs the network loop and manages all torrents. Once it's
	// created, the session object will spawn the main thread that will do all
	// the work. The main thread will be idle as long it doesn't have any
	// torrents to participate in.
	//
	// You have some control over session configuration through the
	// ``session::apply_settings()`` member function. To change one or more
	// configuration options, create a settings_pack. object and fill it with
	// the settings to be set and pass it in to ``session::apply_settings()``.
	// 
	// see apply_settings().
	class TORRENT_EXPORT session: public boost::noncopyable
	{
	public:

		// If the fingerprint in the first overload is omited, the client will
		// get a default fingerprint stating the version of libtorrent. The
		// fingerprint is a short string that will be used in the peer-id to
		// identify the client and the client's version. For more details see the
		// fingerprint class.
		// 
		// The flags paramater can be used to start default features (upnp &
		// nat-pmp) and default plugins (ut_metadata, ut_pex and smart_ban). The
		// default is to start those features. If you do not want them to start,
		// pass 0 as the flags parameter.
		// 
		// The ``alert_mask`` is the same mask that you would send to
		// set_alert_mask().

		session(settings_pack const& pack
			, int flags = start_default_features | add_default_plugins)
		{
			TORRENT_CFG();
			init();
			start(flags, pack);
		}
		session(fingerprint const& print = fingerprint("LT"
			, LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR, 0, 0)
			, int flags = start_default_features | add_default_plugins
			, boost::uint32_t alert_mask = alert::error_notification)
		{
			TORRENT_CFG();
			settings_pack pack;
			pack.set_int(settings_pack::alert_mask, alert_mask);
			pack.set_str(settings_pack::peer_fingerprint, print.to_string());
			if ((flags & start_default_features) == 0)
			{
				pack.set_bool(settings_pack::enable_upnp, false);
				pack.set_bool(settings_pack::enable_natpmp, false);
				pack.set_bool(settings_pack::enable_lsd, false);
				pack.set_bool(settings_pack::enable_dht, false);
			}

			init();
			start(flags, pack);
		}
		session(fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = "0.0.0.0"
			, int flags = start_default_features | add_default_plugins
			, int alert_mask = alert::error_notification)
		{
			TORRENT_CFG();
			TORRENT_ASSERT(listen_port_range.first > 0);
			TORRENT_ASSERT(listen_port_range.first <= listen_port_range.second);

			settings_pack pack;
			pack.set_int(settings_pack::alert_mask, alert_mask);
			pack.set_int(settings_pack::max_retry_port_bind, listen_port_range.second - listen_port_range.first);
			pack.set_str(settings_pack::peer_fingerprint, print.to_string());
			char if_string[100];
			snprintf(if_string, sizeof(if_string), "%s:%d", listen_interface, listen_port_range.first);
			pack.set_str(settings_pack::listen_interfaces, if_string);

			if ((flags & start_default_features) == 0)
			{
				pack.set_bool(settings_pack::enable_upnp, false);
				pack.set_bool(settings_pack::enable_natpmp, false);
				pack.set_bool(settings_pack::enable_lsd, false);
				pack.set_bool(settings_pack::enable_dht, false);
			}
			init();
			start(flags, pack);
		}
			
		// The destructor of session will notify all trackers that our torrents
		// have been shut down. If some trackers are down, they will time out.
		// All this before the destructor of session returns. So, it's advised
		// that any kind of interface (such as windows) are closed before
		// destructing the session object. Because it can take a few second for
		// it to finish. The timeout can be set with apply_settings().
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

			// save pe_settings
			save_encryption_settings = 0x020,

			// internal
			save_as_map =       0x040,

			// saves RSS feeds
			save_feeds =        0x080

#ifndef TORRENT_NO_DEPRECATE
			,
			save_proxy =        0x008,
			save_i2p_proxy =    0x010,
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
		// ``load_state`` expects a bdecode_node which can be built from a bencoded
		// buffer with bdecode().
		// 
		// The ``flags`` arguments passed in to ``save_state`` can be used to
		// filter which parts of the session state to save. By default, all state
		// is saved (except for the individual torrents). see save_state_flags_t
		void save_state(entry& e, boost::uint32_t flags = 0xffffffff) const;
		void load_state(bdecode_node const& e);

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
		// the ``flags`` argument is the same as for torrent_handle::status().
		// see torrent_handle::status_flags_t.
		void post_torrent_updates(boost::uint32_t flags = 0xffffffff);

		// This function will post a session_stats_alert object, containing a
		// snapshot of the performance counters from the internals of libtorrent.
		// To interpret these counters, query the session via
		// session_stats_metrics().
		//
		// For more information, see the session-statistics_ section.
		void post_session_stats();

		// This will cause a dht_stats_alert to be posted.
		void post_dht_stats();

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

		// This function enables dynamic-loading-of-torrent-files_. When a
		// torrent is unloaded but needs to be availabe in memory, this function
		// is called **from within the libtorrent network thread**. From within
		// this thread, you can **not** use any of the public APIs of libtorrent
		// itself. The the info-hash of the torrent is passed in to the function
		// and it is expected to fill in the passed in ``vector<char>`` with the
		// .torrent file corresponding to it.
		// 
		// If there is an error loading the torrent file, the ``error_code``
		// (``ec``) should be set to reflect the error. In such case, the torrent
		// itself is stopped and set to an error state with the corresponding
		// error code.
		// 
		// Given that the function is called from the internal network thread of
		// libtorrent, it's important to not stall. libtorrent will not be able
		// to send nor receive any data until the function call returns.
		// 
		// The signature of the function to pass in is::
		// 
		// 	void fun(sha1_hash const& info_hash, std::vector<char>& buf, error_code& ec);
		void set_load_function(user_load_function_t fun);

#ifndef TORRENT_NO_DEPRECATE
		//  deprecated in libtorrent 1.1, use performance_counters instead
		// returns session wide-statistics and status. For more information, see
		// the ``session_status`` struct.
		TORRENT_DEPRECATED_PREFIX
		session_status status() const TORRENT_DEPRECATED;

		// deprecated in libtorrent 1.1
		// fills out the supplied vector with information for each piece that is
		// currently in the disk cache for the torrent with the specified
		// info-hash (``ih``).
		TORRENT_DEPRECATED_PREFIX
		void get_cache_info(sha1_hash const& ih
			, std::vector<cached_piece_info>& ret) const TORRENT_DEPRECATED;

		// Returns status of the disk cache for this session.
		// For more information, see the cache_status type.
		TORRENT_DEPRECATED_PREFIX
		cache_status get_cache_status() const TORRENT_DEPRECATED;
#endif

		enum { disk_cache_no_pieces = 1 };

		// Fills in the cache_status struct with information about the given torrent.
		// If ``flags`` is ``session::disk_cache_no_pieces`` the ``cache_status::pieces`` field
		// will not be set. This may significantly reduce the cost of this call.
		void get_cache_info(cache_status* ret, torrent_handle h = torrent_handle(), int flags = 0) const;

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

#ifndef TORRENT_NO_DEPRECATE
		// ``start_dht`` starts the dht node and makes the trackerless service
		// available to torrents.
		// 
		// ``stop_dht`` stops the dht node.
		// deprecated. use settings_pack::enable_dht instead
		TORRENT_DEPRECATED_PREFIX
		void start_dht() TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void stop_dht() TORRENT_DEPRECATED;
#endif

		// ``set_dht_settings`` sets some parameters availavle to the dht node.
		// See dht_settings for more information.
		//
		// ``is_dht_running()`` returns true if the DHT support has been started
		// and false
		// otherwise.
		void set_dht_settings(dht_settings const& settings);
		bool is_dht_running() const;

		// ``add_dht_node`` takes a host name and port pair. That endpoint will be
		// pinged, and if a valid DHT reply is received, the node will be added to
		// the routing table.
		// 
		// ``add_dht_router`` adds the given endpoint to a list of DHT router
		// nodes. If a search is ever made while the routing table is empty,
		// those nodes will be used as backups. Nodes in the router node list
		// will also never be added to the regular routing table, which
		// effectively means they are only used for bootstrapping, to keep the
		// load off them.
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

#ifndef TORRENT_NO_DEPRECATE
		// GeoIP support has been removed from libtorrent internals. If you
		// still need to resolve peers, please do so on the client side, using
		// libgeoip directly. This was removed in libtorrent 1.1

		// These functions expects a path to the `MaxMind ASN database`_ and
		// `MaxMind GeoIP database`_ respectively. This will be used to look up
		// which AS and country peers belong to.
		// 
		// ``as_for_ip`` returns the AS number for the IP address specified. If
		// the IP is not in the database or the ASN database is not loaded, 0 is
		// returned.
		// 
		// .. _`MaxMind ASN database`: http://www.maxmind.com/app/asnum
		// .. _`MaxMind GeoIP database`: http://www.maxmind.com/app/geolitecountry
		TORRENT_DEPRECATED_PREFIX
		void load_asnum_db(char const* file) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void load_country_db(char const* file) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		int as_for_ip(address const& addr) TORRENT_DEPRECATED;
#if TORRENT_USE_WSTRING
		// all wstring APIs are deprecated since 0.16.11
		// instead, use the wchar -> utf8 conversion functions
		// and pass in utf8 strings
		TORRENT_DEPRECATED_PREFIX
		void load_country_db(wchar_t const* file) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void load_asnum_db(wchar_t const* file) TORRENT_DEPRECATED;
#endif // TORRENT_USE_WSTRING

		// deprecated in 0.15
		// use load_state and save_state instead
		TORRENT_DEPRECATED_PREFIX
		void load_state(entry const& ses_state) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		entry state() const TORRENT_DEPRECATED;
		// deprecated in 1.1
		TORRENT_DEPRECATED_PREFIX
		void load_state(lazy_entry const& ses_state) TORRENT_DEPRECATED;
#endif // TORRENT_NO_DEPRECATE

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

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 1.1, use settings_pack::peer_fingerprint instead
		TORRENT_DEPRECATED_PREFIX
		void set_peer_id(peer_id const& pid) TORRENT_DEPRECATED;
#endif
		// returns the raw peer ID used by libtorrent. When anonymous mode is set
		// the peer ID is randomized per peer.
		peer_id id() const;

		// sets the key sent to trackers. If it's not set, it is initialized
		// by libtorrent. The key may be used by the tracker to identify the
		// peer potentially across you changing your IP.
		void set_key(int key);

		// built-in peer classes
		enum {
			global_peer_class_id,
			tcp_peer_class_id,
			local_peer_class_id
		};

		// ``is_listening()`` will tell you whether or not the session has
		// successfully opened a listening port. If it hasn't, this function will
		// return false, and then you can set a new
		// settings_pack::listen_interfaces to try another interface and port to
		// bind to.
		// 
		// ``listen_port()`` returns the port we ended up listening on. If the
		// port specified in settings_pack::listen_interfaces failed, libtorrent
		// will try to bind to the next port, and so on. If it fails
		// settings_pack::max_retry_port_bind times, it will bind to port 0
		// (meaning the OS picks the port). The only way to know which port it
		// ended up binding to is to ask for it by calling ``listen_port()``.
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
		unsigned short listen_port() const;
		unsigned short ssl_listen_port() const;
		bool is_listening() const;

		// Sets the peer class filter for this session. All new peer connections
		// will take this into account and be added to the peer classes specified
		// by this filter, based on the peer's IP address.
		// 
		// The ip-filter essentially maps an IP -> uint32. Each bit in that 32
		// bit integer represents a peer class. The least significant bit
		// represents class 0, the next bit class 1 and so on.
		// 
		// For more info, see ip_filter.
		// 
		// For example, to make all peers in the range 200.1.1.0 - 200.1.255.255
		// belong to their own peer class, apply the following filter::
		// 
		// 	ip_filter f;
		// 	int my_class = ses.create_peer_class("200.1.x.x IP range");
		// 	f.add_rule(address_v4::from_string("200.1.1.0")
		// 		, address_v4::from_string("200.1.255.255")
		// 		, 1 << my_class);
		// 	ses.set_peer_class_filter(f);
		// 
		// This setting only applies to new connections, it won't affect existing
		// peer connections.
		// 
		// This function is limited to only peer class 0-31, since there are only
		// 32 bits in the IP range mapping. Only the set bits matter; no peer
		// class will be removed from a peer as a result of this call, peer
		// classes are only added.
		// 
		// The ``peer_class`` argument cannot be greater than 31. The bitmasks
		// representing peer classes in the ``peer_class_filter`` are 32 bits.
		// 
		// For more information, see peer-classes_.
		void set_peer_class_filter(ip_filter const& f);

		// Sets and gets the *peer class type filter*. This is controls automatic
		// peer class assignments to peers based on what kind of socket it is.
		// 
		// It does not only support assigning peer classes, it also supports
		// removing peer classes based on socket type.
		//
		// The order of these rules being applied are:
		// 
		// 1. peer-class IP filter
		// 2. peer-class type filter, removing classes
		// 3. peer-class type filter, adding classes
		//
		// For more information, see peer-classes_.
		// TODO: add get_peer_class_type_filter() as well
		void set_peer_class_type_filter(peer_class_type_filter const& f);

		// Creates a new peer class (see peer-classes_) with the given name. The
		// returned integer is the new peer class' identifier. Peer classes may
		// have the same name, so each invocation of this function creates a new
		// class and returns a unique identifier.
		// 
		// Identifiers are assigned from low numbers to higher. So if you plan on
		// using certain peer classes in a call to `set_peer_class_filter()`_,
		// make sure to create those early on, to get low identifiers.
		// 
		// For more information on peer classes, see peer-classes_.
		int create_peer_class(char const* name);

		// This call dereferences the reference count of the specified peer
		// class. When creating a peer class it's automatically referenced by 1.
		// If you want to recycle a peer class, you may call this function. You
		// may only call this function **once** per peer class you create.
		// Calling it more than once for the same class will lead to memory
		// corruption.
		// 
		// Since peer classes are reference counted, this function will not
		// remove the peer class if it's still assigned to torrents or peers. It
		// will however remove it once the last peer and torrent drops their
		// references to it.
		// 
		// There is no need to call this function for custom peer classes. All
		// peer classes will be properly destructed when the session object
		// destructs.
		// 
		// For more information on peer classes, see peer-classes_.
		void delete_peer_class(int cid);

		// These functions queries information from a peer class and updates the
		// configuration of a peer class, respectively.
		// 
		// ``cid`` must refer to an existing peer class. If it does not, the
		// return value of ``get_peer_class()`` is undefined.
		// 
		// ``set_peer_class()`` sets all the information in the
		// ``peer_class_info`` object in the specified peer class. There is no
		// option to only update a single property.
		// 
		// A peer or torrent balonging to more than one class, the highest
		// priority among any of its classes is the one that is taken into
		// account.
		// 
		// For more information, see peer-classes_.
 		peer_class_info get_peer_class(int cid);
		void set_peer_class(int cid, peer_class_info const& pci);

#ifndef TORRENT_NO_DEPRECATE
		// if the listen port failed in some way you can retry to listen on
		// another port- range with this function. If the listener succeeded and
		// is currently listening, a call to this function will shut down the
		// listen port and reopen it using these new properties (the given
		// interface and port range). As usual, if the interface is left as 0
		// this function will return false on failure. If it fails, it will also
		// generate alerts describing the error. It will return true on success.
		enum listen_on_flags_t
		{
			// this is always on starting with 0.16.2
			listen_reuse_address = 0x01,
			listen_no_system_port = 0x02
		};

		// deprecated in 0.16

		// specify which interfaces to bind outgoing connections to
		// This has been moved to a session setting
		TORRENT_DEPRECATED_PREFIX
		void use_interfaces(char const* interfaces) TORRENT_DEPRECATED;

		// instead of using this, specify listen interface and port in
		// the settings_pack::listen_interfaces setting
		TORRENT_DEPRECATED_PREFIX
		void listen_on(
			std::pair<int, int> const& port_range
			, error_code& ec
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

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in aio-branch
		// Sets the session settings and the packet encryption settings
		// respectively. See session_settings and pe_settings for more
		// information on available options.
		TORRENT_DEPRECATED_PREFIX
		void set_settings(session_settings const& s) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		session_settings settings() const TORRENT_DEPRECATED;

		// deprecated in libtorrent 1.1. use settings_pack instead
		TORRENT_DEPRECATED_PREFIX
		void set_pe_settings(pe_settings const& settings) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		pe_settings get_pe_settings() const TORRENT_DEPRECATED;
#endif

		// Applies the settings specified by the settings_pack ``s``. This is an
		// asynchronous operation that will return immediately and actually apply
		// the settings to the main thread of libtorrent some time later.
		void apply_settings(settings_pack const& s);
		aux::session_settings get_settings() const;

#ifndef TORRENT_NO_DEPRECATE
		// ``set_i2p_proxy`` sets the i2p_ proxy, and tries to open a persistant
		// connection to it. The only used fields in the proxy settings structs
		// are ``hostname`` and ``port``.
		//
		// ``i2p_proxy`` returns the current i2p proxy in use.
		//
		// .. _i2p: http://www.i2p2.de

		TORRENT_DEPRECATED_PREFIX
		void set_i2p_proxy(proxy_settings const& s) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		proxy_settings i2p_proxy() const TORRENT_DEPRECATED;

		// These functions sets and queries the proxy settings to be used for the
		// session.
		//
		// For more information on what settings are available for proxies, see
		// proxy_settings. If the session is not in anonymous mode, proxies that
		// aren't working or fail, will automatically be disabled and packets
		// will flow without using any proxy. If you want to enforce using a
		// proxy, even when the proxy doesn't work, enable anonymous_mode in
		// session_settings.
		TORRENT_DEPRECATED_PREFIX
		void set_proxy(proxy_settings const& s) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		proxy_settings proxy() const TORRENT_DEPRECATED;

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
		// occurred. With settings_pack::alert_mask you can filter which alerts
		// to receive through ``pop_alert()``. For information about the alert
		// categories, see alerts_.
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

		// use the setting instead
		TORRENT_DEPRECATED_PREFIX
		size_t set_alert_queue_size_limit(size_t queue_size_limit_) TORRENT_DEPRECATED;

		// Changes the mask of which alerts to receive. By default only errors
		// are reported. ``m`` is a bitmask where each bit represents a category
		// of alerts.
		//
		// ``get_alert_mask()`` returns the current mask;
		//
		// See category_t enum for options.
		TORRENT_DEPRECATED_PREFIX
		void set_alert_mask(boost::uint32_t m) TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		boost::uint32_t get_alert_mask() const TORRENT_DEPRECATED;
#endif

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

#ifndef TORRENT_NO_DEPRECATE
		// Starts and stops Local Service Discovery. This service will broadcast
		// the infohashes of all the non-private torrents on the local network to
		// look for peers on the same swarm within multicast reach.
		//
		// deprecated. use settings_pack::enable_lsd instead
		TORRENT_DEPRECATED_PREFIX
		void start_lsd() TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void stop_lsd() TORRENT_DEPRECATED;

		// Starts and stops the UPnP service. When started, the listen port and
		// the DHT port are attempted to be forwarded on local UPnP router
		// devices.
		// 
		// The upnp object returned by ``start_upnp()`` can be used to add and
		// remove arbitrary port mappings. Mapping status is returned through the
		// portmap_alert and the portmap_error_alert. The object will be valid
		// until ``stop_upnp()`` is called. See upnp-and-nat-pmp_.
		// 
		// deprecated. use settings_pack::enable_upnp instead
		TORRENT_DEPRECATED_PREFIX
 		void start_upnp() TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void stop_upnp() TORRENT_DEPRECATED;

		// Starts and stops the NAT-PMP service. When started, the listen port
		// and the DHT port are attempted to be forwarded on the router through
		// NAT-PMP.
		// 
		// The natpmp object returned by ``start_natpmp()`` can be used to add
		// and remove arbitrary port mappings. Mapping status is returned through
		// the portmap_alert and the portmap_error_alert. The object will be
		// valid until ``stop_natpmp()`` is called. See upnp-and-nat-pmp_.
		// 
		// deprecated. use settings_pack::enable_natpmp instead
		TORRENT_DEPRECATED_PREFIX
		void start_natpmp() TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void stop_natpmp() TORRENT_DEPRECATED;
#endif

		// protocols used by add_port_mapping()
		enum protocol_type { udp = 1, tcp = 2 };

		// add_port_mapping adds a port forwarding on UPnP and/or NAT-PMP,
		// whichever is enabled. The return value is a handle referring to the
		// port mapping that was just created. Pass it to delete_port_mapping()
		// to remove it.
		int add_port_mapping(protocol_type t, int external_port, int local_port);
		void delete_port_mapping(int handle);

	private:

		void init();
		void start(int flags, settings_pack const& pack);

		// data shared between the main thread
		// and the working thread
		boost::shared_ptr<aux::session_impl> m_impl;
	};

}

#endif // TORRENT_SESSION_HPP_INCLUDED

