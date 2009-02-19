============================
libtorrent API Documentation
============================

:Author: Arvid Norberg, arvid@rasterbar.com
:Version: 0.14.2

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

overview
========

The interface of libtorrent consists of a few classes. The main class is
the ``session``, it contains the main loop that serves all torrents.

The basic usage is as follows:

* construct a session
* start extensions (see `add_extension()`_).
* start DHT, LSD, UPnP, NAT-PMP etc (see `start_dht() stop_dht() set_dht_settings() dht_state()`_
  `start_lsd() stop_lsd()`_, `start_upnp() stop_upnp()`_ and `start_natpmp() stop_natpmp()`_)
* parse .torrent-files and add them to the session (see `bdecode() bencode()`_ and `add_torrent()`_)
* main loop (see session_)

	* query the torrent_handles for progress (see torrent_handle_)
	* query the session for information
	* add and remove torrents from the session at run-time

* save resume data for all torrent_handles (optional, see
  `save_resume_data()`_)
* destruct session object

Each class and function is described in this manual.

For a description on how to create torrent files, see make_torrent_.

.. _make_torrent: make_torrent.html

network primitives
==================

There are a few typedefs in the ``libtorrent`` namespace which pulls
in network types from the ``asio`` namespace. These are::

	typedef asio::ip::address address;
	typedef asio::ip::address_v4 address_v4;
	typedef asio::ip::address_v6 address_v6;
	using asio::ip::tcp;
	using asio::ip::udp;

These are declared in the ``<libtorrent/socket.hpp>`` header.

The ``using`` statements will give easy access to::

	tcp::endpoint
	udp::endpoint

Which are the endpoint types used in libtorrent. An endpoint is an address
with an associated port.

For documentation on these types, please refer to the `asio documentation`_.

.. _`asio documentation`: http://asio.sourceforge.net/asio-0.3.8/doc/asio/reference.html

session
=======

The ``session`` class has the following synopsis::

	class session: public boost::noncopyable
	{

		session(fingerprint const& print
			= libtorrent::fingerprint(
			"LT", 0, 1, 0, 0)
			, int flags = start_default_features | add_default_plugins);

		session(
			fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = 0
			, int flags = start_default_features | add_default_plugins);

		torrent_handle add_torrent(add_torrent_params const& params);

		void pause();
		void resume();
		bool is_paused() const;

		session_proxy abort();

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

		void remove_torrent(torrent_handle const& h, int options = none);
		torrent_handle find_torrent(sha_hash const& ih);
		std::vector<torrent_handle> get_torrents() const;

		void set_settings(session_settings const& settings);
		void set_pe_settings(pe_settings const& settings);

		void set_upload_rate_limit(int bytes_per_second);
		int upload_rate_limit() const;
		void set_download_rate_limit(int bytes_per_second);
		int download_rate_limit() const;
		void set_max_uploads(int limit);
		void set_max_connections(int limit);
		void set_max_half_open_connections(int limit);
		int max_half_open_connections() const;

		void set_peer_proxy(proxy_settings const& s);
		void set_web_seed_proxy(proxy_settings const& s);
		void set_tracker_proxy(proxy_settings const& s);

		proxy_settings const& peer_proxy() const;
		proxy_settings const& web_seed_proxy() const;
		proxy_settings const& tracker_proxy() const;

		int num_uploads() const;
		int num_connections() const;

		bool load_asnum_db(char const* file);
		bool load_country_db(char const* file);
		int as_for_ip(address const& adr);

		void load_state(entry const& ses_state);
		entry state() const;

		void set_ip_filter(ip_filter const& f);
      
		session_status status() const;
		cache_status get_cache_status() const;

		bool is_listening() const;
		unsigned short listen_port() const;
		bool listen_on(
			std::pair<int, int> const& port_range
			, char const* interface = 0);

		std::auto_ptr<alert> pop_alert();
		alert const* wait_for_alert(time_duration max_wait);
		void set_alert_mask(int m);
		size_t set_alert_queue_size_limit(size_t queue_size_limit_);

		void add_extension(boost::function<
			boost::shared_ptr<torrent_plugin>(torrent*)> ext);
 
 		void start_dht();
		void stop_dht();
		void set_dht_settings(
			dht_settings const& settings);
		entry dht_state() const;
		void add_dht_node(std::pair<std::string
			, int> const& node);
		void add_dht_router(std::pair<std::string
			, int> const& node);

		void start_lsd();
		void stop_lsd();

		upnp* start_upnp();
		void stop_upnp();

		natpmp* start_natpmp();
		void stop_natpmp();
	};

Once it's created, the session object will spawn the main thread that will do all the work.
The main thread will be idle as long it doesn't have any torrents to participate in.

session()
---------

	::

		session(fingerprint const& print
			= libtorrent::fingerprint("LT", 0, 1, 0, 0)
			, int flags = start_default_features | add_default_plugins);

		session(fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = 0
			, int flags = start_default_features | add_default_plugins);

If the fingerprint in the first overload is omited, the client will get a default
fingerprint stating the version of libtorrent. The fingerprint is a short string that will be
used in the peer-id to identify the client and the client's version. For more details see the
fingerprint_ class. The constructor that only takes a fingerprint will not open a
listen port for the session, to get it running you'll have to call ``session::listen_on()``.
The other constructor, that takes a port range and an interface as well as the fingerprint
will automatically try to listen on a port on the given interface. For more information about
the parameters, see ``listen_on()`` function.

The flags paramater can be used to start default features (upnp & nat-pmp) and default plugins
(ut_metadata, ut_pex and smart_ban). The default is to start those things. If you do not want
them to start, pass 0 as the flags parameter.

~session()
----------

The destructor of session will notify all trackers that our torrents have been shut down.
If some trackers are down, they will time out. All this before the destructor of session
returns. So, it's advised that any kind of interface (such as windows) are closed before
destructing the session object. Because it can take a few second for it to finish. The
timeout can be set with ``set_settings()``.

pause() resume() is_paused()
----------------------------

	::

		void pause();
		void resume();
		bool is_paused() const;

Pausing the session has the same effect as pausing every torrent in it, except that
torrents will not be resumed by the auto-manage mechanism. Resuming will restore the
torrents to their previous paused state. i.e. the session pause state is separate from
the torrent pause state. A torrent is inactive if it is paused or if the session is
paused.

abort()
-------

	::

		session_proxy abort();

In case you want to destruct the session asynchrounously, you can request a session
destruction proxy. If you don't do this, the destructor of the session object will
block while the trackers are contacted. If you keep one ``session_proxy`` to the
session when destructing it, the destructor will not block, but start to close down
the session, the destructor of the proxy will then synchronize the threads. So, the
destruction of the session is performed from the ``session`` destructor call until the
``session_proxy`` destructor call. The ``session_proxy`` does not have any operations
on it (since the session is being closed down, no operations are allowed on it). The
only valid operation is calling the destructor::

	class session_proxy
	{
	public:
		session_proxy();
		~session_proxy()
	};


add_torrent()
-------------

	::

		typedef storage_interface* (&storage_constructor_type)(
			file_storage const&, fs::path const&, file_pool&);

		struct add_torrent_params
		{
			add_torrent_params(storage_constructor_type s);

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

		torrent_handle add_torrent(add_torrent_params const& params);

You add torrents through the ``add_torrent()`` function where you give an
object with all the parameters.

The only mandatory parameter is ``save_path`` which is the directory where you
want the files to be saved. You also need to specify either the ``ti`` (the
torrent file) or ``info_hash`` (the info hash of the torrent). If you specify the
info-hash, the torrent file will be downloaded from peers, which requires them to
support the metadata extension. For the metadata extension to work, libtorrent must
be built with extensions enabled (``TORRENT_DISABLE_EXTENSIONS`` must not be
defined). It also takes an optional ``name`` argument. This may be 0 in case no
name should be assigned to the torrent. In case it's not 0, the name is used for
the torrent as long as it doesn't have metadata. See ``torrent_handle::name``.

If the torrent doesn't have a tracker, but relies on the DHT to find peers, the
``tracker_url`` can be 0, otherwise you might specify a tracker url that tracks this
torrent.

If the torrent you are trying to add already exists in the session (is either queued
for checking, being checked or downloading) ``add_torrent()`` will throw
duplicate_torrent_ which derives from ``std::exception`` unless ``duplicate_is_error``
is set to false. In that case, ``add_torrent`` will return the handle to the existing
torrent.

The optional parameter, ``resume_data`` can be given if up to date fast-resume data
is available. The fast-resume data can be acquired from a running torrent by calling
`save_resume_data()`_ on `torrent_handle`_. See `fast resume`_. The ``vector`` that is
passed in will be swapped into the running torrent instance with ``std::vector::swap()``.

The ``storage_mode`` parameter refers to the layout of the storage for this torrent.
There are 3 different modes:

storage_mode_sparse
	All pieces will be written to the place where they belong and sparse files
	will be used. This is the recommended, and default mode.

storage_mode_allocate
	Same as ``storage_mode_sparse`` except that files will be ftruncated on
	startup (SetEndOfFile() on windows). For filesystem that supports sparse
	files, this is in all practical aspects identical to sparse mode. For
	filesystems that don't, it will allocate the data for the files. The mac
	filesystem HFS+ doesn't support sparse files, it will allocate the files
	with zeroes.

storage_mode_compact
	The storage will grow as more pieces are downloaded, and pieces
	are rearranged to finally be in their correct places once the entire torrent has been
	downloaded.

For more information, see `storage allocation`_.

``paused`` is a boolean that specifies whether or not the torrent is to be started in
a paused state. I.e. it won't connect to the tracker or any of the peers until it's
resumed. This is typically a good way of avoiding race conditions when setting
configuration options on torrents before starting them.

If ``auto_managed`` is true, this torrent will be queued, started and seeded
automatically by libtorrent. When this is set, the torrent should also be started
as paused. The default queue order is the order the torrents were added. They
are all downloaded in that order. For more details, see queuing_.

``storage`` can be used to customize how the data is stored. The default
storage will simply write the data to the files it belongs to, but it could be
overridden to save everything to a single file at a specific location or encrypt the
content on disk for instance. For more information about the ``storage_interface``
that needs to be implemented for a custom storage, see `storage_interface`_.

The ``userdata`` parameter is optional and will be passed on to the extension
constructor functions, if any (see `add_extension()`_).

The torrent_handle_ returned by ``add_torrent()`` can be used to retrieve information
about the torrent's progress, its peers etc. It is also used to abort a torrent.


remove_torrent()
----------------

	::

		void remove_torrent(torrent_handle const& h, int options = none);

``remove_torrent()`` will close all peer connections associated with the torrent and tell
the tracker that we've stopped participating in the swarm. The optional second argument
``options`` can be used to delete all the files downloaded by this torrent. To do this, pass
in the value ``session::delete_files``. The removal of the torrent is asyncronous, there is
no guarantee that adding the same torrent immediately after it was removed will not throw
a duplicate_torrent_ exception.

find_torrent() get_torrents()
-----------------------------

	::

		torrent_handle find_torrent(sha_hash const& ih);
		std::vector<torrent_handle> get_torrents() const;

``find_torrent()`` looks for a torrent with the given info-hash. In case there
is such a torrent in the session, a torrent_handle to that torrent is returned.
In case the torrent cannot be found, an invalid torrent_handle is returned.

See ``torrent_handle::is_valid()`` to know if the torrent was found or not.

``get_torrents()`` returns a vector of torrent_handles to all the torrents
currently in the session.


set_upload_rate_limit() set_download_rate_limit() upload_rate_limit() download_rate_limit()
-------------------------------------------------------------------------------------------

	::

		void set_upload_rate_limit(int bytes_per_second);
		void set_download_rate_limit(int bytes_per_second);
		int upload_rate_limit() const;
		int download_rate_limit() const;

``set_upload_rate_limit()`` set the maximum number of bytes allowed to be
sent to peers per second. This bandwidth is distributed among all the peers. If
you don't want to limit upload rate, you can set this to -1 (the default).
``set_download_rate_limit()`` works the same way but for download rate instead
of upload rate.
``download_rate_limit()`` and ``upload_rate_limit()`` returns the previously
set limits.


set_max_uploads() set_max_connections()
---------------------------------------

	::

		void set_max_uploads(int limit);
		void set_max_connections(int limit);

These functions will set a global limit on the number of unchoked peers (uploads)
and the number of connections opened. The number of connections is set to a hard
minimum of at least two connections per torrent, so if you set a too low
connections limit, and open too many torrents, the limit will not be met. The
number of uploads is at least one per torrent.


num_uploads() num_connections()
-------------------------------

	::
		
		int num_uploads() const;
		int num_connections() const;

Returns the number of currently unchoked peers and the number of connections
(including half-open ones) respectively.


set_max_half_open_connections() max_half_open_connections()
-----------------------------------------------------------

	::
		
		void set_max_half_open_connections(int limit);
		int max_half_open_connections() const;

Sets the maximum number of half-open connections libtorrent will have when
connecting to peers. A half-open connection is one where connect() has been
called, but the connection still hasn't been established (nor failed). Windows
XP Service Pack 2 sets a default, system wide, limit of the number of half-open
connections to 10. So, this limit can be used to work nicer together with
other network applications on that system. The default is to have no limit,
and passing -1 as the limit, means to have no limit. When limiting the number
of simultaneous connection attempts, peers will be put in a queue waiting for
their turn to get connected.

``max_half_open_connections()`` returns the set limit. This limit defaults
to 8 on windows.

load_asnum_db() load_country_db() int as_for_ip()
-------------------------------------------------

	::

		bool load_asnum_db(char const* file);
		bool load_country_db(char const* file);
		int as_for_ip(address const& adr);

These functions are not available if ``TORRENT_DISABLE_GEO_IP`` is defined. They
expects a path to the `MaxMind ASN database`_ and `MaxMind GeoIP database`_
respectively. This will be used to look up which AS and country peers belong to.

``as_for_ip`` returns the AS number for the IP address specified. If the IP is not
in the database or the ASN database is not loaded, 0 is returned.

.. _`MaxMind ASN database`: http://www.maxmind.com/app/asnum
.. _`MaxMind GeoIP database`: http://www.maxmind.com/app/geolitecountry
		
load_state() state()
--------------------

	::
	
		void load_state(entry const& ses_state);
		entry state() const;

These functions loads and save session state. Currently, the only state
that's stored is peak download rates for ASes. This map is used to
determine which order to connect to peers.
		
set_ip_filter()
---------------

	::

		void set_ip_filter(ip_filter const& filter);

Sets a filter that will be used to reject and accept incoming as well as outgoing
connections based on their originating ip address. The default filter will allow
connections to any ip address. To build a set of rules for which addresses are
accepted and not, see ip_filter_.

Each time a peer is blocked because of the IP filter, a peer_blocked_alert_ is
generated.


status()
--------

	::

		session_status status() const;

``status()`` returns session wide-statistics and status. The ``session_status``
struct has the following members::

	struct session_status
	{
		bool has_incoming_connections;

		float upload_rate;
		float download_rate;

		float payload_upload_rate;
		float payload_download_rate;

		size_type total_download;
		size_type total_upload;

		size_type total_redundant_bytes;
		size_type total_failed_bytes;

		size_type total_payload_download;
		size_type total_payload_upload;

		int num_peers;
		int num_unchoked;
		int allowed_upload_slots;

		int dht_nodes;
		int dht_cache_nodes;
		int dht_torrents;
		int dht_global_nodes;
	};

``has_incoming_connections`` is false as long as no incoming connections have been
established on the listening socket. Every time you change the listen port, this will
be reset to false.

``upload_rate``, ``download_rate``, ``payload_download_rate`` and ``payload_upload_rate``
are the total download and upload rates accumulated from all torrents. The payload
versions is the payload download only.

``total_download`` and ``total_upload`` are the total number of bytes downloaded and
uploaded to and from all torrents. ``total_payload_download`` and ``total_payload_upload``
are the same thing but where only the payload is considered.

``total_redundant_bytes`` is the number of bytes that has been received more than once.
This can happen if a request from a peer times out and is requested from a different
peer, and then received again from the first one. To make this lower, increase the
``request_timeout`` and the ``piece_timeout`` in the session settings.

``total_failed_bytes`` is the number of bytes that was downloaded which later failed
the hash-check.

``num_peers`` is the total number of peer connections this session has. This includes
incoming connections that still hasn't sent their handshake or outgoing connections
that still hasn't completed the TCP connection. This number may be slightly higher
than the sum of all peers of all torrents because the incoming connections may not
be assigned a torrent yet.

``num_unchoked`` is the current number of unchoked peers.
``allowed_upload_slots`` is the current allowed number of unchoked peers.

``dht_nodes``, ``dht_cache_nodes`` and ``dht_torrents`` are only available when
built with DHT support. They are all set to 0 if the DHT isn't running. When
the DHT is running, ``dht_nodes`` is set to the number of nodes in the routing
table. This number only includes *active* nodes, not cache nodes. The
``dht_cache_nodes`` is set to the number of nodes in the node cache. These nodes
are used to replace the regular nodes in the routing table in case any of them
becomes unresponsive.

``dht_torrents`` are the number of torrents tracked by the DHT at the moment.

``dht_global_nodes`` is an estimation of the total number of nodes in the DHT
network.

get_cache_status()
------------------

	::

		cache_status get_cache_status() const;

Returns status of the disk cache for this session.

	::

		struct cache_status
		{
			size_type blocks_written;
			size_type writes;
			size_type blocks_read;
			size_type blocks_read_hit;
			size_type reads;
			int cache_size;
			int read_cache_size;
		};

``blocks_written`` is the total number of 16 KiB blocks written to disk
since this session was started.

``writes`` is the total number of write operations performed since this
session was started.

The ratio (``blocks_written`` - ``writes``) / ``blocks_written`` represents
the number of saved write operations per total write operations. i.e. a kind
of cache hit ratio for the write cahe.

``blocks_read`` is the number of blocks that were requested from the
bittorrent engine (from peers), that were served from disk or cache.

``blocks_read_hit`` is the number of blocks that were served from cache.

The ratio ``blocks_read_hit`` / ``blocks_read`` is the cache hit ratio
for the read cache.

``cache_size`` is the number of 16 KiB blocks currently in the disk cache.
This includes both read and write cache.

``read_cache_size`` is the number of 16KiB blocks in the read cache.

get_cache_info()
----------------

	::

		void get_cache_info(sha1_hash const& ih
			, std::vector<cached_piece_info>& ret) const;

``get_cache_info()`` fills out the supplied vector with information for
each piece that is currently in the disk cache for the torrent with the
specified info-hash (``ih``).

	::

		struct cached_piece_info
		{
			int piece;
			std::vector<bool> blocks;
			ptime last_use;
			enum kind_t { read_cache = 0, write_cache = 1 };
			kind_t kind;
		};

``piece`` is the piece index for this cache entry.

``blocks`` has one entry for each block in this piece. ``true`` represents
the data for that block being in the disk cache and ``false`` means it's not.

``last_use`` is the time when a block was last written to this piece. The older
a piece is, the more likely it is to be flushed to disk.
		
``kind`` specifies if this piece is part of the read cache or the write cache.

is_listening() listen_port() listen_on()
----------------------------------------

	::

		bool is_listening() const;
		unsigned short listen_port() const;
		bool listen_on(
			std::pair<int, int> const& port_range
			, char const* interface = 0);

``is_listening()`` will tell you whether or not the session has successfully
opened a listening port. If it hasn't, this function will return false, and
then you can use ``listen_on()`` to make another try.

``listen_port()`` returns the port we ended up listening on. Since you just pass
a port-range to the constructor and to ``listen_on()``, to know which port it
ended up using, you have to ask the session using this function.

``listen_on()`` will change the listen port and/or the listen interface. If the
session is already listening on a port, this socket will be closed and a new socket
will be opened with these new settings. The port range is the ports it will try
to listen on, if the first port fails, it will continue trying the next port within
the range and so on. The interface parameter can be left as 0, in that case the
os will decide which interface to listen on, otherwise it should be the ip-address
of the interface you want the listener socket bound to. ``listen_on()`` returns true
if it managed to open the socket, and false if it failed. If it fails, it will also
generate an appropriate alert (listen_failed_alert_).

The interface parameter can also be a hostname that will resolve to the device you
want to listen on.

If you're also starting the DHT, it is a good idea to do that after you've called
``listen_on()``, since the default listen port for the DHT is the same as the tcp
listen socket. If you start the DHT first, it will assume the tcp port is free and
open the udp socket on that port, then later, when ``listen_on()`` is called, it
may turn out that the tcp port is in use. That results in the DHT and the bittorrent
socket listening on different ports. If the DHT is active when ``listen_on`` is
called, the udp port will be rebound to the new port, if it was configured to use
the same port as the tcp socket, and if the listen_on call failed to bind to the
same port that the udp uses.

The reason why it's a good idea to run the DHT and the bittorrent socket on the same
port is because that is an assumption that may be used to increase performance. One
way to accelerate the connecting of peers on windows may be to first ping all peers
with a DHT ping packet, and connect to those that responds first. On windows one
can only connect to a few peers at a time because of a built in limitation (in XP
Service pack 2).

pop_alert() set_alert_mask() wait_for_alert() set_alert_queue_size_limit()
--------------------------------------------------------------------------

	::

		std::auto_ptr<alert> pop_alert();
		alert const* wait_for_alert(time_duration max_wait);
		void set_alert_mask(int m);
		size_t set_alert_queue_size_limit(size_t queue_size_limit_);

``pop_alert()`` is used to ask the session if any errors or events has occurred. With
``set_alert_mask()`` you can filter which alerts to receive through ``pop_alert()``.
For information about the alert categories, see alerts_.

``wait_for_alert`` blocks until an alert is available, or for no more than ``max_wait``
time. If ``wait_for_alert`` returns because of the time-out, and no alerts are available,
it returns 0. If at least one alert was generated, a pointer to that alert is returned.
The alert is not popped, any subsequent calls to ``wait_for_alert`` will return the
same pointer until the alert is popped by calling ``pop_alert``. This is useful for
leaving any alert dispatching mechanism independent of this blocking call, the dispatcher
can be called and it can pop the alert independently.

``set_alert_queue_size_limit()`` you can specify how many alerts can be awaiting for dispatching.
If this limit is reached, new incoming alerts can not be received until alerts are popped
by calling ``pop_alert``. Default value is 1000.

add_extension()
---------------

	::

		void add_extension(boost::function<
			boost::shared_ptr<torrent_plugin>(torrent*, void*)> ext);

This function adds an extension to this session. The argument is a function
object that is called with a ``torrent*`` and which should return a
``boost::shared_ptr<torrent_plugin>``. To write custom plugins, see
`libtorrent plugins`_. For the typical bittorrent client all of these
extensions should be added. The main plugins implemented in libtorrent are:

metadata extension
	Allows peers to download the metadata (.torren files) from the swarm
	directly. Makes it possible to join a swarm with just a tracker and
	info-hash.

::

	#include <libtorrent/extensions/metadata_transfer.hpp>
	ses.add_extension(&libtorrent::create_metadata_plugin);

uTorrent metadata
	Same as ``metadata extension`` but compatible with uTorrent.

::

	#include <libtorrent/extensions/ut_metadata.hpp>
	ses.add_extension(&libtorrent::create_ut_metadata_plugin);

uTorrent peer exchange
	Exchanges peers between clients.

::

	#include <libtorrent/extensions/ut_pex.hpp>
	ses.add_extension(&libtorrent::create_ut_pex_plugin);

smart ban plugin
	A plugin that, with a small overhead, can ban peers
	that sends bad data with very high accuracy. Should
	eliminate most problems on poisoned torrents.

::

	#include <libtorrent/extensions/smart_ban.hpp>
	ses.add_extension(&libtorrent::create_smart_ban_plugin);


.. _`libtorrent plugins`: libtorrent_plugins.html

set_settings() set_pe_settings()
--------------------------------

	::

		void set_settings(session_settings const& settings);
		void set_pe_settings(pe_settings const& settings);
		
Sets the session settings and the packet encryption settings respectively.
See session_settings_ and pe_settings_ for more information on available
options.


set_peer_proxy() set_web_seed_proxy() set_tracker_proxy() set_dht_proxy()
-------------------------------------------------------------------------

	::

		void set_peer_proxy(proxy_settings const& s);
		void set_web_seed_proxy(proxy_settings const& s);
		void set_tracker_proxy(proxy_settings const& s);
		void set_dht_proxy(proxy_settings const& s);

The ``set_dht_proxy`` is not available when DHT is disabled. These functions
sets the proxy settings for different kinds of connections, bittorrent peers,
web seeds, trackers and the DHT traffic.

``set_peer_proxy`` affects regular bittorrent peers. ``set_web_seed_proxy``
affects only web seeds. see `HTTP seeding`_.

``set_tracker_proxy`` only affects HTTP tracker connections (UDP tracker
connections are affected if the given proxy supports UDP, e.g. SOCKS5).

``set_dht_proxy`` affects the DHT messages. Since they are sent over UDP,
it only has any effect if the proxy supports UDP.

For more information on what settings are available for proxies, see
`proxy_settings`_.


peer_proxy() web_seed_proxy() tracker_proxy() dht_proxy()
---------------------------------------------------------

	::

		proxy_settings const& peer_proxy() const;
		proxy_settings const& web_seed_proxy() const;
		proxy_settings const& tracker_proxy() const;
		proxy_settings const& dht_proxy() const;

These functions returns references to their respective current settings.

The ``dht_proxy`` is not available when DHT is disabled.


start_dht() stop_dht() set_dht_settings() dht_state()
-----------------------------------------------------

	::

		void start_dht(entry const& startup_state);
		void stop_dht();
		void set_dht_settings(dht_settings const& settings);
		entry dht_state() const;

These functions are not available in case ``TORRENT_DISABLE_DHT`` is
defined. ``start_dht`` starts the dht node and makes the trackerless service
available to torrents. The startup state is optional and can contain nodes
and the node id from the previous session. The dht node state is a bencoded
dictionary with the following entries:

``nodes``
	A list of strings, where each string is a node endpoint encoded in binary. If
	the string is 6 bytes long, it is an IPv4 address of 4 bytes, encoded in
	network byte order (big endian), followed by a 2 byte port number (also
	network byte order). If the string is 18 bytes long, it is 16 bytes of IPv6
	address followed by a 2 bytes port number (also network byte order).

``node-id``
	The node id written as a readable string as a hexadecimal number.

``dht_state`` will return the current state of the dht node, this can be used
to start up the node again, passing this entry to ``start_dht``. It is a good
idea to save this to disk when the session is closed, and read it up again
when starting.

If the port the DHT is supposed to listen on is already in use, and exception
is thrown, ``asio::error``.

``stop_dht`` stops the dht node.

``add_dht_node`` adds a node to the routing table. This can be used if your
client has its own source of bootstrapping nodes.

``set_dht_settings`` sets some parameters availavle to the dht node. The
struct has the following members::

	struct dht_settings
	{
		int max_peers_reply;
		int search_branching;
		int service_port;
		int max_fail_count;
	};

``max_peers_reply`` is the maximum number of peers the node will send in
response to a ``get_peers`` message from another node.

``search_branching`` is the number of concurrent search request the node will
send when announcing and refreshing the routing table. This parameter is
called alpha in the kademlia paper.

``service_port`` is the udp port the node will listen to. This will default
to 0, which means the udp listen port will be the same as the tcp listen
port. This is in general a good idea, since some NAT implementations
reserves the udp port for any mapped tcp port, and vice versa. NAT-PMP
guarantees this for example.

``max_fail_count`` is the maximum number of failed tries to contact a node
before it is removed from the routing table. If there are known working nodes
that are ready to replace a failing node, it will be replaced immediately,
this limit is only used to clear out nodes that don't have any node that can
replace them.


add_dht_node() add_dht_router()
-------------------------------

	::

		void add_dht_node(std::pair<std::string, int> const& node);
		void add_dht_router(std::pair<std::string, int> const& node);

``add_dht_node`` takes a host name and port pair. That endpoint will be
pinged, and if a valid DHT reply is received, the node will be added to
the routing table.

``add_dht_router`` adds the given endpoint to a list of DHT router nodes.
If a search is ever made while the routing table is empty, those nodes will
be used as backups. Nodes in the router node list will also never be added
to the regular routing table, which effectively means they are only used
for bootstrapping, to keep the load off them.

An example routing node that you could typically add is
``router.bittorrent.com``.


start_lsd() stop_lsd()
----------------------

	::

		void start_lsd();
		void stop_lsd();

Starts and stops Local Service Discovery. This service will broadcast
the infohashes of all the non-private torrents on the local network to
look for peers on the same swarm within multicast reach.

It is turned off by default.

start_upnp() stop_upnp()
------------------------

	::
	
		upnp* start_upnp();
		void stop_upnp();

Starts and stops the UPnP service. When started, the listen port and the DHT
port are attempted to be forwarded on local UPnP router devices.

The upnp object returned by ``start_upnp()`` can be used to add and remove
arbitrary port mappings. Mapping status is returned through the
portmap_alert_ and the portmap_error_alert_. The object will be valid until
``stop_upnp()`` is called. See `UPnP and NAT-PMP`_.

It is off by default.

start_natpmp() stop_natpmp()
----------------------------

	::
		
		natpmp* start_natpmp();
		void stop_natpmp();

Starts and stops the NAT-PMP service. When started, the listen port and the DHT
port are attempted to be forwarded on the router through NAT-PMP.

The natpmp object returned by ``start_natpmp()`` can be used to add and remove
arbitrary port mappings. Mapping status is returned through the
portmap_alert_ and the portmap_error_alert_. The object will be valid until
``stop_natpmp()`` is called. See `UPnP and NAT-PMP`_.

It is off by default.


entry
=====

The ``entry`` class represents one node in a bencoded hierarchy. It works as a
variant type, it can be either a list, a dictionary (``std::map``), an integer
or a string. This is its synopsis::

	class entry
	{
	public:

		typedef std::map<std::string, entry> dictionary_type;
		typedef std::string string_type;
		typedef std::list<entry> list_type;
		typedef size_type integer_type;

		enum data_type
		{
			int_t,
			string_t,
			list_t,
			dictionary_t,
			undefined_t
		};

		data_type type() const;

		entry(dictionary_type const&);
		entry(string_type const&);
		entry(list_type const&);
		entry(integer_type const&);

		entry();
		entry(data_type t);
		entry(entry const& e);
		~entry();

		void operator=(entry const& e);
		void operator=(dictionary_type const&);
		void operator=(string_type const&);
		void operator=(list_type const&);
		void operator=(integer_type const&);

		integer_type& integer();
		integer_type const& integer() const;
		string_type& string();
		string_type const& string() const;
		list_type& list();
		list_type const& list() const;
		dictionary_type& dict();
		dictionary_type const& dict() const;

		// these functions requires that the entry
		// is a dictionary, otherwise they will throw	
		entry& operator[](char const* key);
		entry& operator[](std::string const& key);
		entry const& operator[](char const* key) const;
		entry const& operator[](std::string const& key) const;
		entry* find_key(char const* key);
		entry const* find_key(char const* key) const;
		
		void print(std::ostream& os, int indent = 0) const;
	};

*TODO: finish documentation of entry.*

integer() string() list() dict() type()
---------------------------------------

	::

		integer_type& integer();
		integer_type const& integer() const;
		string_type& string();
		string_type const& string() const;
		list_type& list();
		list_type const& list() const;
		dictionary_type& dict();
		dictionary_type const& dict() const;

The ``integer()``, ``string()``, ``list()`` and ``dict()`` functions
are accessors that return the respective type. If the ``entry`` object isn't of the
type you request, the accessor will throw type_error_ (which derives from
``std::runtime_error``). You can ask an ``entry`` for its type through the
``type()`` function.

The ``print()`` function is there for debug purposes only.

If you want to create an ``entry`` you give it the type you want it to have in its
constructor, and then use one of the non-const accessors to get a reference which you then
can assign the value you want it to have.

The typical code to get info from a torrent file will then look like this::

	entry torrent_file;
	// ...

	// throws if this is not a dictionary
	entry::dictionary_type const& dict = torrent_file.dict();
	entry::dictionary_type::const_iterator i;
	i = dict.find("announce");
	if (i != dict.end())
	{
		std::string tracker_url = i->second.string();
		std::cout << tracker_url << "\n";
	}


The following code is equivalent, but a little bit shorter::

	entry torrent_file;
	// ...

	// throws if this is not a dictionary
	if (entry* i = torrent_file.find_key("announce"))
	{
		std::string tracker_url = i->string();
		std::cout << tracker_url << "\n";
	}


To make it easier to extract information from a torrent file, the class torrent_info_
exists.


operator[]
----------

	::

		entry& operator[](char const* key);
		entry& operator[](std::string const& key);
		entry const& operator[](char const* key) const;
		entry const& operator[](std::string const& key) const;

All of these functions requires the entry to be a dictionary, if it isn't they
will throw ``libtorrent::type_error``.

The non-const versions of the ``operator[]`` will return a reference to either
the existing element at the given key or, if there is no element with the
given key, a reference to a newly inserted element at that key.

The const version of ``operator[]`` will only return a reference to an
existing element at the given key. If the key is not found, it will throw
``libtorrent::type_error``.


find_key()
----------

	::

		entry* find_key(char const* key);
		entry const* find_key(char const* key) const;

These functions requires the entry to be a dictionary, if it isn't they
will throw ``libtorrent::type_error``.

They will look for an element at the given key in the dictionary, if the
element cannot be found, they will return 0. If an element with the given
key is found, the return a pointer to it.


torrent_info
============

In previous versions of libtorrent, this class was also used for creating
torrent files. This functionality has been moved to ``create_torrent``, see
make_torrent_.

The ``torrent_info`` has the following synopsis::

	class torrent_info
	{
	public:

		torrent_info(sha1_hash const& info_hash);
		torrent_info(lazy_entry const& torrent_file);
		torrent_info(char const* buffer, int size);
		torrent_info(boost::filesystem::path const& filename);

		void add_tracker(std::string const& url, int tier = 0);
		std::vector<announce_entry> const& trackers() const;

		file_storage const& files() const;
		file_storage const& orig_files() const;

		void rename_file(int index, std::string const& new_filename);
		void rename_file(int index, std::wstring const& new_filename);

		typedef file_storage::iterator file_iterator;
		typedef file_storage::reverse_iterator reverse_file_iterator;

		file_iterator begin_files() const;
		file_iterator end_files() const;
		reverse_file_iterator rbegin_files() const;
		reverse_file_iterator rend_files() const;

		int num_files() const;
		file_entry const& file_at(int index) const;

		std::vector<file_slice> map_block(int piece, size_type offset
			, int size) const;
		peer_request map_file(int file_index, size_type file_offset
			, int size) const;

		bool priv() const;

		std::vector<std::string> const& url_seeds() const;

		size_type total_size() const;
		int piece_length() const;
		int num_pieces() const;
		sha1_hash const& info_hash() const;
		std::string const& name() const;
		std::string const& comment() const;
		std::string const& creator() const;

		std::vector<std::pair<std::string, int> > const& nodes() const;
		void add_node(std::pair<std::string, int> const& node);

		boost::optional<boost::posix_time::ptime>
		creation_date() const;

		int piece_size(unsigned int index) const;
		sha1_hash const& hash_for_piece(unsigned int index) const;
		char const* hash_for_piece_ptr(unsigned int index) const;

		boost::shared_array<char> metadata() const;
		int metadata_size() const;
	};

torrent_info()
--------------
   
	::

		torrent_info(sha1_hash const& info_hash);
		torrent_info(lazy_entry const& torrent_file);
		torrent_info(char const* buffer, int size);
		torrent_info(boost::filesystem::path const& filename);

The constructor that takes an info-hash  will initialize the info-hash to the given value,
but leave all other fields empty. This is used internally when downloading torrents without
the metadata. The metadata will be created by libtorrent as soon as it has been downloaded
from the swarm.

The constructor that takes a ``lazy_entry`` will create a ``torrent_info`` object from the
information found in the given torrent_file. The ``lazy_entry`` represents a tree node in
an bencoded file. To load an ordinary .torrent file
into a ``lazy_entry``, use lazy_bdecode(), see `bdecode() bencode()`_.

The version that takes a buffer pointer and a size will decode it as a .torrent file and
initialize the torrent_info object for you.

The version that takes a filename will simply load the torrent file and decode it inside
the constructor, for convenience. This might not be the most suitable for applications that
want to be able to report detailed errors on what might go wrong.


add_tracker()
-------------

	::

		void add_tracker(std::string const& url, int tier = 0);

``add_tracker()`` adds a tracker to the announce-list. The ``tier`` determines the order in
which the trackers are to be tried. For more information see `trackers()`_.

files() orig_files()
--------------------

	::

		file_storage const& file() const;
		file_storage const& orig_files() const;

The ``file_storage`` object contains the information on how to map the pieces to
files. It is separated from the ``torrent_info`` object because when creating torrents
a storage object needs to be created without having a torrent file. When renaming files
in a storage, the storage needs to make its own copy of the ``file_storage`` in order
to make its mapping differ from the one in the torrent file.

``orig_files()`` returns the original (unmodified) file storage for this torrent. This
is used by the web server connection, which needs to request files with the original
names.

For more information on the ``file_storage`` object, see the separate document on how
to create torrents.

begin_files() end_files() rbegin_files() rend_files()
-----------------------------------------------------

	::

		file_iterator begin_files() const;
		file_iterator end_files() const;
		reverse_file_iterator rbegin_files() const;
		reverse_file_iterator rend_files() const;

This class will need some explanation. First of all, to get a list of all files
in the torrent, you can use ``begin_files()``, ``end_files()``,
``rbegin_files()`` and ``rend_files()``. These will give you standard vector
iterators with the type ``file_entry``.

::

	struct file_entry
	{
		boost::filesystem::path path;
		size_type offset;
		size_type size;
		size_type file_base;
		boost::shared_ptr<const boost::filesystem::path> orig_path;
	};

The ``path`` is the full (relative) path of each file. i.e. if it is a multi-file
torrent, all the files starts with a directory with the same name as ``torrent_info::name()``.
The filenames are encoded with UTF-8.

``size`` is the size of the file (in bytes) and ``offset`` is the byte offset
of the file within the torrent. i.e. the sum of all the sizes of the files
before it in the list.

``file_base`` is the offset in the file where the storage should start. The normal
case is to have this set to 0, so that the storage starts saving data at the start
if the file. In cases where multiple files are mapped into the same file though,
the ``file_base`` should be set to an offset so that the different regions do
not overlap. This is used when mapping "unselected" files into a so-called part
file.

``orig_path`` is set to 0 in case the path element is an exact copy of that
found in the metadata. In case the path in the original metadata was
incorrectly encoded, and had to be fixed in order to be acceptable utf-8,
the original string is preserved in ``orig_path``. The reason to keep it
is to be able to reproduce the info-section exactly, with the correct
info-hash.



num_files() file_at()
---------------------

	::
	
		int num_files() const;
		file_entry const& file_at(int index) const;

If you need index-access to files you can use the ``num_files()`` and ``file_at()``
to access files using indices.


map_block()
-----------

	::

		std::vector<file_slice> map_block(int piece, size_type offset
			, int size) const;

This function will map a piece index, a byte offset within that piece and
a size (in bytes) into the corresponding files with offsets where that data
for that piece is supposed to be stored.

The file slice struct looks like this::

	struct file_slice
	{
		int file_index;
		size_type offset;
		size_type size;
	};


The ``file_index`` refers to the index of the file (in the torrent_info).
To get the path and filename, use ``file_at()`` and give the ``file_index``
as argument. The ``offset`` is the byte offset in the file where the range
starts, and ``size`` is the number of bytes this range is. The size + offset
will never be greater than the file size.


map_file()
----------

	::

		peer_request map_file(int file_index, size_type file_offset
			, int size) const;

This function will map a range in a specific file into a range in the torrent.
The ``file_offset`` parameter is the offset in the file, given in bytes, where
0 is the start of the file.
The ``peer_request`` structure looks like this::

	struct peer_request
	{
		int piece;
		int start;
		int length;
		bool operator==(peer_request const& r) const;
	};

``piece`` is the index of the piece in which the range starts.
``start`` is the offset within that piece where the range starts.
``length`` is the size of the range, in bytes.

The input range is assumed to be valid within the torrent. ``file_offset``
+ ``size`` is not allowed to be greater than the file size. ``file_index``
must refer to a valid file, i.e. it cannot be >= ``num_files()``.


url_seeds() add_url_seed()
--------------------------

	::

		std::vector<std::string> const& url_seeds() const;
		void add_url_seed(std::string const& url);

If there are any url-seeds in this torrent, ``url_seeds()`` will return a
vector of those urls. If you're creating a torrent file, ``add_url_seed()``
adds one url to the list of url-seeds. Currently, the only transport protocol
supported for the url is http.

See `HTTP seeding`_ for more information.


trackers()
----------

	::

		std::vector<announce_entry> const& trackers() const;

The ``trackers()`` function will return a sorted vector of ``announce_entry``.
Each announce entry contains a string, which is the tracker url, and a tier index. The
tier index is the high-level priority. No matter which trackers that works or not, the
ones with lower tier will always be tried before the one with higher tier number.

::

	struct announce_entry
	{
		announce_entry(std::string const& url);
		std::string url;
		int tier;
	};


total_size() piece_length() piece_size() num_pieces()
-----------------------------------------------------

	::

		size_type total_size() const;
		int piece_length() const;
		int piece_size(unsigned int index) const;
		int num_pieces() const;


``total_size()``, ``piece_length()`` and ``num_pieces()`` returns the total
number of bytes the torrent-file represents (all the files in it), the number of byte for
each piece and the total number of pieces, respectively. The difference between
``piece_size()`` and ``piece_length()`` is that ``piece_size()`` takes
the piece index as argument and gives you the exact size of that piece. It will always
be the same as ``piece_length()`` except in the case of the last piece, which may
be smaller.


hash_for_piece() hash_for_piece_ptr() info_hash()
-------------------------------------------------

	::
	
		size_type piece_size(unsigned int index) const;
		sha1_hash const& hash_for_piece(unsigned int index) const;
		char const* hash_for_piece_ptr(unsigned int index) const;

``hash_for_piece()`` takes a piece-index and returns the 20-bytes sha1-hash for that
piece and ``info_hash()`` returns the 20-bytes sha1-hash for the info-section of the
torrent file. For more information on the ``sha1_hash``, see the big_number_ class.
``hash_for_piece_ptr()`` returns a pointer to the 20 byte sha1 digest for the piece. 
Note that the string is not null-terminated.


name() comment() creation_date() creator()
------------------------------------------

	::

		std::string const& name() const;
		std::string const& comment() const;
		boost::optional<boost::posix_time::ptime> creation_date() const;

``name()`` returns the name of the torrent.

``comment()`` returns the comment associated with the torrent. If there's no comment,
it will return an empty string. ``creation_date()`` returns a `boost::posix_time::ptime`__
object, representing the time when this torrent file was created. If there's no time stamp
in the torrent file, this will return a date of January 1:st 1970.

Both the name and the comment is UTF-8 encoded strings.

``creator()`` returns the creator string in the torrent. If there is no creator string
it will return an empty string.

__ http://www.boost.org/doc/html/date_time/posix_time.html#date_time.posix_time.ptime_class


priv()
------

	::

		bool priv() const;

``priv()`` returns true if this torrent is private. i.e., it should not be
distributed on the trackerless network (the kademlia DHT).


nodes()
-------

	::

		std::vector<std::pair<std::string, int> > const& nodes() const;

If this torrent contains any DHT nodes, they are put in this vector in their original
form (host name and port number).

add_node() 
---------- 
 
    :: 
 
        void add_node(std::pair<std::string, int> const& node); 
 
This is used when creating torrent. Use this to add a known DHT node. It may 
be used, by the client, to bootstrap into the DHT network.


metadata() metadata_size()
--------------------------

	::

		boost::shared_array<char> metadata() const;
		int metadata_size() const;

``metadata()`` returns a the raw info section of the torrent file. The size
of the metadata is returned by ``metadata_size()``.


torrent_handle
==============

You will usually have to store your torrent handles somewhere, since it's the
object through which you retrieve information about the torrent and aborts the torrent.
Its declaration looks like this::

	struct torrent_handle
	{
		torrent_handle();

		torrent_status status();
		void file_progress(std::vector<size_type>& fp);
		void get_download_queue(std::vector<partial_piece_info>& queue) const;
		void get_peer_info(std::vector<peer_info>& v) const;
		torrent_info const& get_torrent_info() const;
		bool is_valid() const;

		std::string name() const;

		void save_resume_data() const;
		void force_reannounce() const;
		void force_reannounce(boost::posix_time::time_duration) const;
		void scrape_tracker() const;
		void connect_peer(asio::ip::tcp::endpoint const& adr, int source = 0) const;

		void set_tracker_login(std::string const& username
			, std::string const& password) const;

		std::vector<announce_entry> const& trackers() const;
		void replace_trackers(std::vector<announce_entry> const&);

		void add_url_seed(std::string const& url);
		void remove_url_seed(std::string const& url);
		std::set<std::string> url_seeds() const;

		void set_ratio(float ratio) const;
		void set_max_uploads(int max_uploads) const;
		void set_max_connections(int max_connections) const;
		void set_upload_limit(int limit) const;
		int upload_limit() const;
		void set_download_limit(int limit) const;
		int download_limit() const;
		void set_sequential_download(bool sd) const;
		bool is_sequential_download() const;

		void set_peer_upload_limit(asio::ip::tcp::endpoint ip, int limit) const;
		void set_peer_download_limit(asio::ip::tcp::endpoint ip, int limit) const;

		int queue_position() const;
		void queue_position_up() const;
		void queue_position_down() const;
		void queue_position_top() const;
		void queue_position_bottom() const;

		void use_interface(char const* net_interface) const;

		void pause() const;
		void resume() const;
		bool is_paused() const;
		bool is_seed() const;
		void force_recheck() const;
		void clear_error() const;

		void resolve_countries(bool r);
		bool resolve_countries() const;

		void piece_priority(int index, int priority) const;
		int piece_priority(int index) const;
		void prioritize_pieces(std::vector<int> const& pieces) const;
		std::vector<int> piece_priorities() const;

		void file_priority(int index, int priority) const;
		int file_priority(int index) const;
		void prioritize_files(std::vector<int> const& files) const;
		std::vector<int> file_priorities() const;

		bool is_auto_managed() const;
		void auto_managed(bool m) const;

		bool has_metadata() const;

		boost::filesystem::path save_path() const;
		void move_storage(boost::filesystem::path const& save_path) const;
		void rename_file(int index, boost::filesystem::path) const;
		storage_interface* get_storage_impl() const;

		sha1_hash info_hash() const;

		bool operator==(torrent_handle const&) const;
		bool operator!=(torrent_handle const&) const;
		bool operator<(torrent_handle const&) const;
	};

The default constructor will initialize the handle to an invalid state. Which
means you cannot perform any operation on it, unless you first assign it a
valid handle. If you try to perform any operation on an uninitialized handle,
it will throw ``invalid_handle``.

.. warning:: All operations on a ``torrent_handle`` may throw invalid_handle_
	exception, in case the handle is no longer refering to a torrent. There is
	one exception ``is_valid()`` will never throw.
	Since the torrents are processed by a background thread, there is no
	guarantee that a handle will remain valid between two calls.


piece_priority() prioritize_pieces() piece_priorities()
-------------------------------------------------------

	::

		void piece_priority(int index, int priority) const;
		int piece_priority(int index) const;
		void prioritize_pieces(std::vector<int> const& pieces) const;
		std::vector<int> piece_priorities() const;

These functions are used to set and get the prioritiy of individual pieces.
By default all pieces have priority 1. That means that the random rarest
first algorithm is effectively active for all pieces. You may however
change the priority of individual pieces. There are 8 different priority
levels:

 0. piece is not downloaded at all
 1. normal priority. Download order is dependent on availability
 2. higher than normal priority. Pieces are preferred over pieces with
    the same availability, but not over pieces with lower availability
 3. pieces are as likely to be picked as partial pieces.
 4. pieces are preferred over partial pieces, but not over pieces with
    lower availability
 5. *currently the same as 4*
 6. piece is as likely to be picked as any piece with availability 1
 7. maximum priority, availability is disregarded, the piece is preferred
    over any other piece with lower priority

The exact definitions of these priorities are implementation details, and
subject to change. The interface guarantees that higher number means higher
priority, and that 0 means do not download.

``piece_priority`` sets or gets the priority for an individual piece,
specified by ``index``.

``prioritize_pieces`` takes a vector of integers, one integer per piece in
the torrent. All the piece priorities will be updated with the priorities
in the vector.

``piece_priorities`` returns a vector with one element for each piece in the
torrent. Each element is the current priority of that piece.


file_priority() prioritize_files() file_priorities()
----------------------------------------------------

	::

		void file_priority(int index, int priority) const;
		int file_priority(int index) const;
		void prioritize_files(std::vector<int> const& files) const;
		std::vector<int> file_priorities() const;

``index`` must be in the range [0, number_of_files).

``file_priority`` queries or sets the priority of file ``index``.

``prioritize_files`` takes a vector that has at as many elements as there are
files in the torrent. Each entry is the priority of that file. The function
sets the priorities of all the pieces in the torrent based on the vector.

``file_priorities`` returns a vector with the priorities of all files.

The priority values are the same as for ``piece_priority``.

Whenever a file priority is changed, all other piece priorities are reset
to match the file priorities. In order to maintain sepcial priorities for
particular pieces, ``piece_priority`` has to be called again for those pieces.

file_progress()
---------------

	::

		void file_progress(std::vector<size_type>& fp);

This function fills in the supplied vector with the the number of bytes downloaded
of each file in this torrent. The progress values are ordered the same as the files
in the `torrent_info`_. This operation is not very cheap. Its complexity is *O(n + mj)*.
Where *n* is the number of files, *m* is the number of downloading pieces and *j*
is the number of blocks in a piece.


save_path()
-----------

	::

		boost::filesystem::path save_path() const;

``save_path()`` returns the path that was given to `add_torrent()`_ when this torrent
was started.

move_storage()
--------------

	::

		void move_storage(boost::filesystem::path const& save_path) const;

Moves the file(s) that this torrent are currently seeding from or downloading to. If
the given ``save_path`` is not located on the same drive as the original save path,
The files will be copied to the new drive and removed from their original location.
This will block all other disk IO, and other torrents download and upload rates may
drop while copying the file.

Since disk IO is performed in a separate thread, this operation is also asynchronous.
Once the operation completes, the ``storage_moved_alert`` is generated, with the new
path as the message.

rename_file()
-------------

	::

		void rename_file(int index, boost::filesystem::path) const;

Renames the file with the given index asynchronously. The rename operation is complete
when either a ``file_renamed_alert`` or ``file_rename_failed_alert`` is posted.

get_storage_impl()
------------------

	::

		storage_interface* get_storage_impl() const;

Returns the storage implementation for this torrent. This depends on the
storage contructor function that was passed to ``session::add_torrent``.

force_reannounce()
------------------

	::

		void force_reannounce() const;
		void force_reannounce(boost::posix_time::time_duration) const;

``force_reannounce()`` will force this torrent to do another tracker request, to receive new
peers. The second overload of ``force_reannounce`` that takes a ``time_duration`` as
argument will schedule a reannounce in that amount of time from now.

scrape_tracker()
----------------

	::

		void scrape_tracker() const;

``scrape_tracker()`` will send a scrape request to the tracker. A scrape request queries the
tracker for statistics such as total number of incomplete peers, complete peers, number of
downloads etc.

This request will specifically update the ``num_complete`` and ``num_incomplete`` fields in
the torrent_status_ struct once it completes. When it completes, it will generate a
scrape_reply_alert_. If it fails, it will generate a scrape_failed_alert_.

connect_peer()
--------------

	::

		void connect_peer(asio::ip::tcp::endpoint const& adr, int source = 0) const;

``connect_peer()`` is a way to manually connect to peers that one believe is a part of the
torrent. If the peer does not respond, or is not a member of this torrent, it will simply
be disconnected. No harm can be done by using this other than an unnecessary connection
attempt is made. If the torrent is uninitialized or in queued or checking mode, this
will throw invalid_handle_. The second (optional) argument will be bitwised ORed into
the source mask of this peer. Typically this is one of the source flags in peer_info_.
i.e. ``tracker``, ``pex``, ``dht`` etc.


name()
------

	::

		std::string name() const;

Returns the name of the torrent. i.e. the name from the metadata associated with it. In
case the torrent was started without metadata, and hasn't completely received it yet,
it returns the name given to it when added to the session. See ``session::add_torrent``.


set_ratio()
-----------

	::

		void set_ratio(float ratio) const;

``set_ratio()`` sets the desired download / upload ratio. If set to 0, it is considered being
infinite. i.e. the client will always upload as much as it can, no matter how much it gets back
in return. With this setting it will work much like the standard clients.

Besides 0, the ratio can be set to any number greater than or equal to 1. It means how much to
attempt to upload in return for each download. e.g. if set to 2, the client will try to upload
2 bytes for every byte received. The default setting for this is 0, which will make it work
as a standard client.


set_upload_limit() set_download_limit() upload_limit() download_limit()
-----------------------------------------------------------------------

	::

		void set_upload_limit(int limit) const;
		void set_download_limit(int limit) const;
		int upload_limit() const;
		int download_limit() const;

``set_upload_limit`` will limit the upload bandwidth used by this particular torrent to the
limit you set. It is given as the number of bytes per second the torrent is allowed to upload.
``set_download_limit`` works the same way but for download bandwidth instead of upload bandwidth.
Note that setting a higher limit on a torrent then the global limit (``session::set_upload_rate_limit``)
will not override the global rate limit. The torrent can never upload more than the global rate
limit.

``upload_limit`` and ``download_limit`` will return the current limit setting, for upload and
download, respectively.


set_sequential_download() is_sequential_download()
--------------------------------------------------

	::

		void set_sequential_download(bool sd);
		bool is_sequential_download() const;

``set_sequential_download()`` enables or disables *sequential download*. When enabled, the piece
picker will pick pieces in sequence instead of rarest first.

Enabling sequential download will affect the piece distribution negatively in the swarm. It should be
used sparingly.

``is_sequential_download()`` returns true if this torrent is downloading in sequence, and false
otherwise.


set_peer_upload_limit() set_peer_download_limit()
-------------------------------------------------

	::

		void set_peer_upload_limit(asio::ip::tcp::endpoint ip, int limit) const;
		void set_peer_download_limit(asio::ip::tcp::endpoint ip, int limit) const;

Works like ``set_upload_limit`` and ``set_download_limit`` respectively, but controls individual
peer instead of the whole torrent.

pause() resume() is_paused()
----------------------------

	::

		void pause() const;
		void resume() const;
		bool is_paused() const;

``pause()``, and ``resume()`` will disconnect all peers and reconnect all peers respectively.
When a torrent is paused, it will however remember all share ratios to all peers and remember
all potential (not connected) peers. You can use ``is_paused()`` to determine if a torrent
is currently paused. Torrents may be paused automatically if there is a file error (e.g. disk full)
or something similar. See file_error_alert_.

torrents that are auto-managed may be automatically resumed again. It does not make sense to
pause an auto-managed torrent without making it not automanaged first. Torrents are auto-managed
by default when added to the session. For more information, see queuing_.

``is_paused()`` only returns true if the torrent itself is paused. If the torrent
is not running because the session is paused, this still returns false. To know if a
torrent is active or not, you need to inspect both ``torrent_handle::is_paused()``
and ``session::is_paused()``.

force_recheck()
---------------

	::

		void force_recheck() const;

``force_recheck`` puts the torrent back in a state where it assumes to have no resume data.
All peers will be disconnected and the torrent will stop announcing to the tracker. The torrent
will be added to the checking queue, and will be checked (all the files will be read and
compared to the piece hashes). Once the check is complete, the torrent will start connecting
to peers again, as normal.

clear_error()
-------------

	::

		void clear_error() const;

If the torrent is in an error state (i.e. ``torrent_status::error`` is non-empty), this
will clear the error and start the torrent again.

resolve_countries()
-------------------

	::

		void resolve_countries(bool r);
		bool resolve_countries() const;

Sets or gets the flag that derermines if countries should be resolved for the peers of this
torrent. It defaults to false. If it is set to true, the peer_info_ structure for the peers
in this torrent will have their ``country`` member set. See peer_info_ for more information
on how to interpret this field.

is_seed()
---------

	::

		bool is_seed() const;

Returns true if the torrent is in seed mode (i.e. if it has finished downloading).

is_auto_managed() auto_managed()
--------------------------------

	::

		bool is_auto_managed() const;
		void auto_managed(bool m) const;

``is_auto_managed()`` returns true if this torrent is currently *auto managed*.
``auto_managed()`` changes whether the torrent is auto managed or not. For more info,
see queuing_.

has_metadata()
--------------

	::

		bool has_metadata() const;

Returns true if this torrent has metadata (either it was started from a .torrent file or the
metadata has been downloaded). The only scenario where this can return false is when the torrent
was started torrent-less (i.e. with just an info-hash and tracker ip). Note that if the torrent
doesn't have metadata, the member `get_torrent_info()`_ will throw.

set_tracker_login()
-------------------

	::

		void set_tracker_login(std::string const& username
			, std::string const& password) const;

``set_tracker_login()`` sets a username and password that will be sent along in the HTTP-request
of the tracker announce. Set this if the tracker requires authorization.


trackers() replace_trackers()
-----------------------------

  ::

		std::vector<announce_entry> const& trackers() const;
		void replace_trackers(std::vector<announce_entry> const&) const;

``trackers()`` will return the list of trackers for this torrent. The
announce entry contains both a string ``url`` which specify the announce url
for the tracker as well as an int ``tier``, which is specifies the order in
which this tracker is tried. If you want libtorrent to use another list of
trackers for this torrent, you can use ``replace_trackers()`` which takes
a list of the same form as the one returned from ``trackers()`` and will
replace it. If you want an immediate effect, you have to call
`force_reannounce()`_.


add_url_seed() remove_url_seed() url_seeds()
--------------------------------------------

	::

		void add_url_seed(std::string const& url);
		void remove_url_seed(std::string const& url);
		std::set<std::string> url_seeds() const;

``add_url_seed()`` adds another url to the torrent's list of url seeds. If the
given url already exists in that list, the call has no effect. The torrent
will connect to the server and try to download pieces from it, unless it's
paused, queued, checking or seeding. ``remove_url_seed()`` removes the given
url if it exists already. ``url_seeds()`` return a set of the url seeds
currently in this torrent. Note that urls that fails may be removed
automatically from the list.

See `HTTP seeding`_ for more information.


queue_position() queue_position_up() queue_position_down() queue_position_top() queue_position_bottom()
-------------------------------------------------------------------------------------------------------

	::

		int queue_position() const;
		void queue_position_up() const;
		void queue_position_down() const;
		void queue_position_top() const;
		void queue_position_bottom() const;

Every torrent that is added is assigned a queue position exactly one greater than
the greatest queue position of all existing torrents. Torrents that are being
seeded have -1 as their queue position, since they're no longer in line to be downloaded.

When a torrent is removed or turns into a seed, all torrents with greater queue positions
have their positions decreased to fill in the space in the sequence.

``queue_position()`` returns the torrent's position in the download queue. The torrents
with the smallest numbers are the ones that are being downloaded. The smaller number,
the closer the torrent is to the front of the line to be started.

The ``queue_position_*()`` functions adjust the torrents position in the queue. Up means
closer to the front and down means closer to the back of the queue. Top and bottom refers
to the front and the back of the queue respectively.


use_interface()
---------------

	::

		void use_interface(char const* net_interface) const;

``use_interface()`` sets the network interface this torrent will use when it opens outgoing
connections. By default, it uses the same interface as the session_ uses to listen on. The
parameter must be a string containing an ip-address (either an IPv4 or IPv6 address). If
the string does not conform to this format and exception is thrown.


info_hash()
-----------

	::

		sha1_hash info_hash() const;

``info_hash()`` returns the info-hash for the torrent.


set_max_uploads() set_max_connections()
---------------------------------------

	::

		void set_max_uploads(int max_uploads) const;
		void set_max_connections(int max_connections) const;

``set_max_uploads()`` sets the maximum number of peers that's unchoked at the same time on this
torrent. If you set this to -1, there will be no limit.

``set_max_connections()`` sets the maximum number of connection this torrent will open. If all
connections are used up, incoming connections may be refused or poor connections may be closed.
This must be at least 2. The default is unlimited number of connections. If -1 is given to the
function, it means unlimited.


save_resume_data()
------------------

	::

		void save_resume_data() const;

``save_resume_data()`` generates fast-resume data and returns it as an entry_. This entry_
is suitable for being bencoded. For more information about how fast-resume works, see `fast resume`_.

This operation is asynchronous, ``save_resume_data`` will return immediately. The resume data
is delivered when it's done through an `save_resume_data_alert`_.

The fast resume data will be empty in the following cases:

	1. The torrent handle is invalid.
	2. The torrent is checking (or is queued for checking) its storage, it will obviously
	   not be ready to write resume data.
	3. The torrent hasn't received valid metadata and was started without metadata
	   (see libtorrent's `metadata from peers`_ extension)

Note that by the time you receive the fast resume data, it may already be invalid if the torrent
is still downloading! The recommended practice is to first pause the session, then generate the
fast resume data, and then close it down. Make sure to not `remove_torrent()`_ before you receive
the `save_resume_data_alert`_ though. There's no need to pause when saving intermittent resume data.

.. warning:: If you pause every torrent individually instead of pausing the session, every torrent
	will have its paused state saved in the resume data!

.. note:: It is typically a good idea to save resume data whenever a torrent is completed or paused. In those
	cases you don't need to pause the torrent or the session, since the torrent will do no more writing
	to its files. If you save resume data for torrents when they are paused, you can accelerate the
	shutdown process by not saving resume data again for paused torrents. Completed torrents should
	have their resume data saved when they complete and on exit, since their statistics might be updated.

	In full allocation mode the reume data is never invalidated by subsequent
	writes to the files, since pieces won't move around. This means that you don't need to
	pause before writing resume data in full or sparse mode. If you don't, however, any data written to
	disk after you saved resume data and before the session_ closed is lost.

It also means that if the resume data is out dated, libtorrent will not re-check the files, but assume
that it is fairly recent. The assumption is that it's better to loose a little bit than to re-check
the entire file.

It is still a good idea to save resume data periodically during download as well as when
closing down.

Example code to pause and save resume data for all torrents and wait for the alerts::

	int num_resume_data = 0;
	std::vector<torrent_handle> handles = ses.get_torrents();
	ses.pause();
	for (std::vector<torrent_handle>::iterator i = handles.begin();
		i != handles.end(); ++i)
	{
		torrent_handle& h = *i;
		if (!h.has_metadata()) continue;
		if (!h.is_valid()) continue;

		h.save_resume_data();
		++num_resume_data;
	}

	while (num_resume_data > 0)
	{
		alert const* a = ses.wait_for_alert(seconds(10));

		// if we don't get an alert within 10 seconds, abort
		if (a == 0) break;
		
		std::auto_ptr<alert> holder = ses.pop_alert();

		if (dynamic_cast<save_resume_data_failed_alert const*>(a))
		{
			process_alert(a);
			--num_resume_data;
			continue;
		}

		save_resume_data_alert const* rd = dynamic_cast<save_resume_data_alert const*>(a);
		if (rd == 0)
		{
			process_alert(a);
			continue;
		}
		
		torrent_handle h = rd->handle;
		boost::filesystem::ofstream out(h.save_path()
			/ (h.get_torrent_info().name() + ".fastresume"), std::ios_base::binary);
		out.unsetf(std::ios_base::skipws);
		bencode(std::ostream_iterator<char>(out), *rd->resume_data);
		--num_resume_data;
	}
	


status()
--------

	::

		torrent_status status() const;

``status()`` will return a structure with information about the status of this
torrent. If the torrent_handle_ is invalid, it will throw invalid_handle_ exception.
See torrent_status_.


get_download_queue()
--------------------

	::

		void get_download_queue(std::vector<partial_piece_info>& queue) const;

``get_download_queue()`` takes a non-const reference to a vector which it will fill with
information about pieces that are partially downloaded or not downloaded at all but partially
requested. The entry in the vector (``partial_piece_info``) looks like this::

	struct partial_piece_info
	{
		int piece_index;
		int blocks_in_piece;
		block_info blocks[256];
		enum state_t { none, slow, medium, fast };
		state_t piece_state;
	};

``piece_index`` is the index of the piece in question. ``blocks_in_piece`` is the
number of blocks in this particular piece. This number will be the same for most pieces, but
the last piece may have fewer blocks than the standard pieces.

``piece_state`` is set to either ``fast``, ``medium``, ``slow`` or ``none``. It tells which
download rate category the peers downloading this piece falls into. ``none`` means that no
peer is currently downloading any part of the piece. Peers prefer picking pieces from
the same category as themselves. The reason for this is to keep the number of partially
downloaded pieces down. Pieces set to ``none`` can be converted into any of ``fast``,
``medium`` or ``slow`` as soon as a peer want to download from it.

::

	struct block_info
	{
		enum block_state_t
		{ none, requested, writing, finished };

		tcp::endpoint peer;
		unsigned state:2;
		unsigned num_peers:14;
	};


The ``block_info`` array contains data for each individual block in the piece. Each block has
a state (``state``) which is any of:

* ``none`` - This block has not been downloaded or requested form any peer.
* ``requested`` - The block has been requested, but not completely downloaded yet.
* ``writing`` - The block has been downloaded and is currently queued for being written to disk.
* ``finished`` - The block has been written to disk.

The ``peer`` field is the ip address of the peer this block was downloaded from.
``num_peers`` is the number of peers that is currently requesting this block. Typically this
is 0 or 1, but at the end of the torrent blocks may be requested by more peers in parallel to
speed things up.

get_peer_info()
---------------

	::

		void get_peer_info(std::vector<peer_info>&) const;

``get_peer_info()`` takes a reference to a vector that will be cleared and filled
with one entry for each peer connected to this torrent, given the handle is valid. If the
torrent_handle_ is invalid, it will throw invalid_handle_ exception. Each entry in
the vector contains information about that particular peer. See peer_info_.


get_torrent_info()
------------------

	::

		torrent_info const& get_torrent_info() const;

Returns a const reference to the torrent_info_ object associated with this torrent.
This reference is valid as long as the torrent_handle_ is valid, no longer. If the
torrent_handle_ is invalid or if it doesn't have any metadata, invalid_handle_
exception will be thrown. The torrent may be in a state without metadata only if
it was started without a .torrent file, i.e. by using the libtorrent extension of
just supplying a tracker and info-hash.


is_valid()
----------

	::

		bool is_valid() const;

Returns true if this handle refers to a valid torrent and false if it hasn't been initialized
or if the torrent it refers to has been aborted. Note that a handle may become invalid after
it has been added to the session. Usually this is because the storage for the torrent is
somehow invalid or if the filenames are not allowed (and hence cannot be opened/created) on
your filesystem. If such an error occurs, a file_error_alert_ is generated and all handles
that refers to that torrent will become invalid.


torrent_status
==============

It contains the following fields::

	struct torrent_status
	{
		enum state_t
		{
			queued_for_checking,
			checking_files,
			downloading_metadata,
			downloading,
			finished,
			seeding,
			allocating
		};
	
		state_t state;
		bool paused;
		float progress;
		std::string error;

		boost::posix_time::time_duration next_announce;
		boost::posix_time::time_duration announce_interval;

		std::string current_tracker;

		size_type total_download;
		size_type total_upload;

		size_type total_payload_download;
		size_type total_payload_upload;

		size_type total_failed_bytes;
		size_type total_redundant_bytes;

		float download_rate;
		float upload_rate;

		float download_payload_rate;
		float upload_payload_rate;

		int num_peers;

		int num_complete;
		int num_incomplete;

		int list_seeds;
		int list_peers;

		int connect_candidates;

		bitfield pieces;
		int num_pieces;

		size_type total_done;
		size_type total_wanted_done;
		size_type total_wanted;

		int num_seeds;
		float distributed_copies;

		int block_size;

		int num_uploads;
		int num_connections;
		int uploads_limit;
		int connections_limit;

		storage_mode_t storage_mode;

		int up_bandwidth_queue;
		int down_bandwidth_queue;

		size_type all_time_upload;
		size_type all_time_download;

		int active_time;
		int seeding_time;

		int seed_rank;

		int last_scrape;

		bool has_incoming;
	};

``progress`` is a value in the range [0, 1], that represents the progress of the
torrent's current task. It may be checking files or downloading. The torrent's
current task is in the ``state`` member, it will be one of the following:

+--------------------------+----------------------------------------------------------+
|``queued_for_checking``   |The torrent is in the queue for being checked. But there  |
|                          |currently is another torrent that are being checked.      |
|                          |This torrent will wait for its turn.                      |
+--------------------------+----------------------------------------------------------+
|``checking_files``        |The torrent has not started its download yet, and is      |
|                          |currently checking existing files.                        |
+--------------------------+----------------------------------------------------------+
|``downloading_metadata``  |The torrent is trying to download metadata from peers.    |
|                          |This assumes the metadata_transfer extension is in use.   |
+--------------------------+----------------------------------------------------------+
|``downloading``           |The torrent is being downloaded. This is the state        |
|                          |most torrents will be in most of the time. The progress   |
|                          |meter will tell how much of the files that has been       |
|                          |downloaded.                                               |
+--------------------------+----------------------------------------------------------+
|``finished``              |In this state the torrent has finished downloading but    |
|                          |still doesn't have the entire torrent. i.e. some pieces   |
|                          |are filtered and won't get downloaded.                    |
+--------------------------+----------------------------------------------------------+
|``seeding``               |In this state the torrent has finished downloading and    |
|                          |is a pure seeder.                                         |
+--------------------------+----------------------------------------------------------+
|``allocating``            |If the torrent was started in full allocation mode, this  |
|                          |indicates that the (disk) storage for the torrent is      |
|                          |allocated.                                                |
+--------------------------+----------------------------------------------------------+


When downloading, the progress is ``total_wanted_done`` / ``total_wanted``.

``paused`` is set to true if the torrent is paused and false otherwise.

``error`` may be set to an error message describing why the torrent was paused, in
case it was paused by an error. If the torrent is not paused or if it's paused but
not because of an error, this string is empty.

``next_announce`` is the time until the torrent will announce itself to the tracker. And
``announce_interval`` is the time the tracker want us to wait until we announce ourself
again the next time.

``current_tracker`` is the URL of the last working tracker. If no tracker request has
been successful yet, it's set to an empty string.

``total_download`` and ``total_upload`` is the number of bytes downloaded and
uploaded to all peers, accumulated, *this session* only. The session is considered
to restart when a torrent is paused and restarted again. When a torrent is paused,
these counters are reset to 0. If you want complete, persistent, stats, see
``all_time_upload`` and ``all_time_download``.

``total_payload_download`` and ``total_payload_upload`` counts the amount of bytes
send and received this session, but only the actual payload data (i.e the interesting
data), these counters ignore any protocol overhead.

``total_failed_bytes`` is the number of bytes that has been downloaded and that
has failed the piece hash test. In other words, this is just how much crap that
has been downloaded.

``total_redundant_bytes`` is the number of bytes that has been downloaded even
though that data already was downloaded. The reason for this is that in some
situations the same data can be downloaded by mistake. When libtorrent sends
requests to a peer, and the peer doesn't send a response within a certain
timeout, libtorrent will re-request that block. Another situation when
libtorrent may re-request blocks is when the requests it sends out are not
replied in FIFO-order (it will re-request blocks that are skipped by an out of
order block). This is supposed to be as low as possible.

``pieces`` is the bitmask that represents which pieces we have (set to true) and
the pieces we don't have. It's a pointer and may be set to 0 if the torrent isn't
downloading or seeding.

``num_pieces`` is the number of pieces that has been downloaded. It is equivalent
to: ``std::accumulate(pieces->begin(), pieces->end())``. So you don't have to
count yourself. This can be used to see if anything has updated since last time
if you want to keep a graph of the pieces up to date.

``download_rate`` and ``upload_rate`` are the total rates for all peers for this
torrent. These will usually have better precision than summing the rates from
all peers. The rates are given as the number of bytes per second. The
``download_payload_rate`` and ``upload_payload_rate`` respectively is the
total transfer rate of payload only, not counting protocol chatter. This might
be slightly smaller than the other rates, but if projected over a long time
(e.g. when calculating ETA:s) the difference may be noticeable.

``num_peers`` is the number of peers this torrent currently is connected to.
Peer connections that are in the half-open state (is attempting to connect)
or are queued for later connection attempt do not count. Although they are
visible in the peer list when you call `get_peer_info()`_.

``num_complete`` and ``num_incomplete`` are set to -1 if the tracker did not
send any scrape data in its announce reply. This data is optional and may
not be available from all trackers. If these are not -1, they are the total
number of peers that are seeding (complete) and the total number of peers
that are still downloading (incomplete) this torrent.

``list_seeds`` and ``list_peers`` are the number of seeds in our peer list
and the total number of peers (including seeds) respectively. We are not
necessarily connected to all the peers in our peer list. This is the number
of peers we know of in total, including banned peers and peers that we have
failed to connect to.

``connect_candidates`` is the number of peers in this torrent's peer list
that is a candidate to be connected to. i.e. It has fewer connect attempts
than the max fail count, it is not a seed if we are a seed, it is not banned
etc. If this is 0, it means we don't know of any more peers that we can try.

``total_done`` is the total number of bytes of the file(s) that we have. All
this does not necessarily has to be downloaded during this session (that's
``total_payload_download``).

``total_wanted_done`` is the number of bytes we have downloaded, only counting the
pieces that we actually want to download. i.e. excluding any pieces that we have but
are filtered as not wanted.

``total_wanted`` is the total number of bytes we want to download. This is also
excluding pieces that have been filtered.

``num_seeds`` is the number of peers that are seeding that this client is
currently connected to.

``distributed_copies`` is the number of distributed copies of the torrent.
Note that one copy may be spread out among many peers. The integer part
tells how many copies there are currently of the rarest piece(s) among the
peers this client is connected to. The fractional part tells the share of
pieces that have more copies than the rarest piece(s). For example: 2.5 would
mean that the rarest pieces have only 2 copies among the peers this torrent is
connected to, and that 50% of all the pieces have more than two copies.

If we are a seed, the piece picker is deallocated as an optimization, and
piece availability is no longer tracked. In this case the distributed
copies is set to -1.

``block_size`` is the size of a block, in bytes. A block is a sub piece, it
is the number of bytes that each piece request asks for and the number of
bytes that each bit in the ``partial_piece_info``'s bitset represents
(see `get_download_queue()`_). This is typically 16 kB, but it may be
larger if the pieces are larger.

``num_uploads`` is the number of unchoked peers in this torrent.

``num_connections`` is the number of peer connections this torrent has, including
half-open connections that hasn't completed the bittorrent handshake yet. This is
always <= ``num_peers``.

``uploads_limit`` is the set limit of upload slots (unchoked peers) for this torrent.

``connections_limit`` is the set limit of number of connections for this torrent.

``storage_mode`` is one of ``storage_mode_allocate``, ``storage_mode_sparse`` or
``storage_mode_compact``. Identifies which storage mode this torrent is being saved
with. See `Storage allocation`_.

``up_bandwidth_queue`` and ``down_bandwidth_queue`` are the number of peers in this
torrent that are waiting for more bandwidth quota from the torrent rate limiter.
This can determine if the rate you get from this torrent is bound by the torrents
limit or not. If there is no limit set on this torrent, the peers might still be
waiting for bandwidth quota from the global limiter, but then they are counted in
the ``session_status`` object.

``all_time_upload`` and ``all_time_download`` are accumulated upload and download
payload byte counters. They are saved in and restored from resume data to keep totals
across sessions.

``active_time`` and ``seeding_time`` are second counters. They keep track of the
number of seconds this torrent has been active (not paused) and the number of
seconds it has been active while being a seed. ``seeding_time`` should be >=
``active_time`` They are saved in and restored from resume data, to keep totals
across sessions.

``seed_rank`` is a rank of how important it is to seed the torrent, it is used
to determine which torrents to seed and which to queue. It is based on the peer
to seed ratio from the tracker scrape. For more information, see queuing_.

``last_scrape`` is the number of seconds since this torrent acquired scrape data.
If it has never done that, this value is -1.

``has_incoming`` is true if there has ever been an incoming connection attempt
to this torrent.'


peer_info
=========

It contains the following fields::

	struct peer_info
	{
		enum
		{
			interesting = 0x1,
			choked = 0x2,
			remote_interested = 0x4,
			remote_choked = 0x8,
			supports_extensions = 0x10,
			local_connection = 0x20,
			handshake = 0x40,
			connecting = 0x80,
			queued = 0x100,
			on_parole = 0x200,
			seed = 0x400,
			optimistic_unchoke = 0x800,
			snubbed = 0x1000,
			upload_only = 0x2000,
			rc4_encrypted = 0x100000,
			plaintext_encrypted = 0x200000
		};

		unsigned int flags;

		enum peer_source_flags
		{
			tracker = 0x1,
			dht = 0x2,
			pex = 0x4,
			lsd = 0x8
		};

		int source;

		enum bw_state { bw_idle, bw_torrent, bw_global, bw_network };

		char read_state;
		char write_state;

		asio::ip::tcp::endpoint ip;
		float up_speed;
		float down_speed;
		float payload_up_speed;
		float payload_down_speed;
		size_type total_download;
		size_type total_upload;
		peer_id pid;
		bitfield pieces;
		int upload_limit;
		int download_limit;

		time_duration last_request;
		time_duration last_active;
		int request_timeout;

		int send_buffer_size;
		int used_send_buffer;

		int receive_buffer_size;
		int used_receive_buffer;

		int num_hashfails;

		char country[2];

		std::string inet_as_name;
		int inet_as;

		size_type load_balancing;

		int requests_in_buffer;
		int download_queue_length;
		int upload_queue_length;

		int failcount;

		int downloading_piece_index;
		int downloading_block_index;
		int downloading_progress;
		int downloading_total;

		std::string client;

		enum
		{
			standard_bittorrent = 0,
			web_seed = 1
		};
		int connection_type;

		int remote_dl_rate;

		int pending_disk_bytes;

		int send_quota;
		int receive_quota;

		int rtt;

		int download_rate_peak;
		int upload_rate_peak;

		float progress;
	};

The ``flags`` attribute tells you in which state the peer is. It is set to
any combination of the enums above. The following table describes each flag:

+-------------------------+-------------------------------------------------------+
| ``interesting``         | **we** are interested in pieces from this peer.       |
+-------------------------+-------------------------------------------------------+
| ``choked``              | **we** have choked this peer.                         |
+-------------------------+-------------------------------------------------------+
| ``remote_interested``   | the peer is interested in **us**                      |
+-------------------------+-------------------------------------------------------+
| ``remote_choked``       | the peer has choked **us**.                           |
+-------------------------+-------------------------------------------------------+
| ``support_extensions``  | means that this peer supports the                     |
|                         | `extension protocol`__.                               |
+-------------------------+-------------------------------------------------------+
| ``local_connection``    | The connection was initiated by us, the peer has a    |
|                         | listen port open, and that port is the same as in the |
|                         | address of this peer. If this flag is not set, this   |
|                         | peer connection was opened by this peer connecting to |
|                         | us.                                                   |
+-------------------------+-------------------------------------------------------+
| ``handshake``           | The connection is opened, and waiting for the         |
|                         | handshake. Until the handshake is done, the peer      |
|                         | cannot be identified.                                 |
+-------------------------+-------------------------------------------------------+
| ``connecting``          | The connection is in a half-open state (i.e. it is    |
|                         | being connected).                                     |
+-------------------------+-------------------------------------------------------+
| ``queued``              | The connection is currently queued for a connection   |
|                         | attempt. This may happen if there is a limit set on   |
|                         | the number of half-open TCP connections.              |
+-------------------------+-------------------------------------------------------+
| ``on_parole``           | The peer has participated in a piece that failed the  |
|                         | hash check, and is now "on parole", which means we're |
|                         | only requesting whole pieces from this peer until     |
|                         | it either fails that piece or proves that it doesn't  |
|                         | send bad data.                                        |
+-------------------------+-------------------------------------------------------+
| ``seed``                | This peer is a seed (it has all the pieces).          |
+-------------------------+-------------------------------------------------------+
| ``optimistic_unchoke``  | This peer is subject to an optimistic unchoke. It has |
|                         | been unchoked for a while to see if it might unchoke  |
|                         | us in return an earn an upload/unchoke slot. If it    |
|                         | doesn't within some period of time, it will be choked |
|                         | and another peer will be optimistically unchoked.     |
+-------------------------+-------------------------------------------------------+
| ``snubbed``             | This peer has recently failed to send a block within  |
|                         | the request timeout from when the request was sent.   |
|                         | We're currently picking one block at a time from this |
|                         | peer.                                                 |
+-------------------------+-------------------------------------------------------+
| ``upload_only``         | This peer has either explicitly (with an extension)   |
|                         | or implicitly (by becoming a seed) told us that it    |
|                         | will not downloading anything more, regardless of     |
|                         | which pieces we have.                                 |
+-------------------------+-------------------------------------------------------+

__ extension_protocol.html

``source`` is a combination of flags describing from which sources this peer
was received. The flags are:

+------------------------+--------------------------------------------------------+
| ``tracker``            | The peer was received from the tracker.                |
+------------------------+--------------------------------------------------------+
| ``dht``                | The peer was received from the kademlia DHT.           |
+------------------------+--------------------------------------------------------+
| ``pex``                | The peer was received from the peer exchange           |
|                        | extension.                                             |
+------------------------+--------------------------------------------------------+
| ``lsd``                | The peer was received from the local service           |
|                        | discovery (The peer is on the local network).          |
+------------------------+--------------------------------------------------------+
| ``resume_data``        | The peer was added from the fast resume data.          |
+------------------------+--------------------------------------------------------+

``read_state`` and ``write_state`` indicates what state this peer is in with regards
to sending and receiving data. The states are declared in the ``bw_state`` enum and
defines as follows:

+------------------------+--------------------------------------------------------+
| ``bw_idle``            | The peer is not waiting for any external events to     |
|                        | send or receive data.                                  |
|                        |                                                        |
+------------------------+--------------------------------------------------------+
| ``bw_torrent``         | The peer is waiting for the torrent to receive         |
|                        | bandwidth quota in order to forward the bandwidth      |
|                        | request to the global manager.                         |
|                        |                                                        |
+------------------------+--------------------------------------------------------+
| ``bw_global``          | The peer is waiting for the global bandwidth manager   |
|                        | to receive more quota in order to handle the request.  |
|                        |                                                        |
+------------------------+--------------------------------------------------------+
| ``bw_network``         | The peer has quota and is currently waiting for a      |
|                        | network read or write operation to complete. This is   |
|                        | the state all peers are in if there are no bandwidth   |
|                        | limits.                                                |
|                        |                                                        |
+------------------------+--------------------------------------------------------+

The ``ip`` field is the IP-address to this peer. The type is an asio endpoint. For
more info, see the asio_ documentation.

.. _asio: http://asio.sf.net

``up_speed`` and ``down_speed`` contains the current upload and download speed
we have to and from this peer (including any protocol messages). The transfer rates
of payload data only are found in ``payload_up_speed`` and ``payload_down_speed``.
These figures are updated approximately once every second.

``total_download`` and ``total_upload`` are the total number of bytes downloaded
from and uploaded to this peer. These numbers do not include the protocol chatter, but only
the payload data.

``pid`` is the peer's id as used in the bit torrent protocol. This id can be used to
extract 'fingerprints' from the peer. Sometimes it can tell you which client the peer
is using. See identify_client()_

``pieces`` is a bitfield, with one bit per piece in the torrent.
Each bit tells you if the peer has that piece (if it's set to 1)
or if the peer miss that piece (set to 0).

``seed`` is true if this peer is a seed.

``upload_limit`` is the number of bytes per second we are allowed to send to this
peer every second. It may be -1 if there's no local limit on the peer. The global
limit and the torrent limit is always enforced anyway.

``download_limit`` is the number of bytes per second this peer is allowed to
receive. -1 means it's unlimited.

``last_request`` and ``last_active`` is the time since we last sent a request
to this peer and since any transfer occurred with this peer, respectively.

``request_timeout`` is the number of seconds until the current front piece request
will time out. This timeout can be adjusted through ``session_settings::request_timeout``.
-1 means that there is not outstanding request.

``send_buffer_size`` and ``used_send_buffer`` is the number of bytes allocated
and used for the peer's send buffer, respectively.

``receive_buffer_size`` and ``used_receive_buffer`` are the number of bytes
allocated and used as receive buffer, respectively.

``num_hashfails`` is the number of pieces this peer has participated in
sending us that turned out to fail the hash check.

``country`` is the two letter `ISO 3166 country code`__ for the country the peer
is connected from. If the country hasn't been resolved yet, both chars are set
to 0. If the resolution failed for some reason, the field is set to "--". If the
resolution service returns an invalid country code, it is set to "!!".
The ``countries.nerd.dk`` service is used to look up countries. This field will
remain set to 0 unless the torrent is set to resolve countries, see `resolve_countries()`_.

__ http://www.iso.org/iso/en/prods-services/iso3166ma/02iso-3166-code-lists/list-en1.html

``inet_as_name`` is the name of the AS this peer is located in. This might be
an empty string if there is no name in the geo ip database.

``inet_as`` is the AS number the peer is located in.

``load_balancing`` is a measurement of the balancing of free download (that we get)
and free upload that we give. Every peer gets a certain amount of free upload, but
this member says how much *extra* free upload this peer has got. If it is a negative
number it means that this was a peer from which we have got this amount of free
download.

``requests_in_buffer`` is the number of requests messages that are currently in the
send buffer waiting to be sent.

``download_queue_length`` is the number of piece-requests we have sent to this peer
that hasn't been answered with a piece yet.

``upload_queue_length`` is the number of piece-requests we have received from this peer
that we haven't answered with a piece yet.

``failcount`` is the number of times this peer has "failed". i.e. failed to connect
or disconnected us. The failcount is decremented when we see this peer in a tracker
response or peer exchange message.

You can know which piece, and which part of that piece, that is currently being
downloaded from a specific peer by looking at the next four members.
``downloading_piece_index`` is the index of the piece that is currently being downloaded.
This may be set to -1 if there's currently no piece downloading from this peer. If it is
>= 0, the other three members are valid. ``downloading_block_index`` is the index of the
block (or sub-piece) that is being downloaded. ``downloading_progress`` is the number
of bytes of this block we have received from the peer, and ``downloading_total`` is
the total number of bytes in this block.

``client`` is a string describing the software at the other end of the connection.
In some cases this information is not available, then it will contain a string
that may give away something about which software is running in the other end.
In the case of a web seed, the server type and version will be a part of this
string.

``connection_type`` can currently be one of ``standard_bittorrent`` or
``web_seed``. These are currently the only implemented protocols.

``remote_dl_rate`` is an estimate of the rate this peer is downloading at, in
bytes per second.

``pending_disk_bytes`` is the number of bytes this peer has pending in the
disk-io thread. Downloaded and waiting to be written to disk. This is what
is capped by ``session_settings::max_outstanding_disk_bytes_per_connection``.

``send_quota`` and ``receive_quota`` are the number of bytes this peer has been
assigned to be allowed to send and receive until it has to request more quota
from the bandwidth manager.

``rtt`` is an estimated round trip time to this peer, in milliseconds. It is
estimated by timing the the tcp ``connect()``. It may be 0 for incoming connections.

``download_rate_peak`` and ``upload_rate_peak`` are the highest download and upload
rates seen on this connection. They are given in bytes per second. This number is
reset to 0 on reconnect.

``progress`` is the progress of the peer.

session_settings
================

You have some control over tracker requests through the ``session_settings`` object. You
create it and fill it with your settings and then use ``session::set_settings()``
to apply them. You have control over proxy and authorization settings and also the user-agent
that will be sent to the tracker. The user-agent is a good way to identify your client.

::

	struct session_settings
	{
		session_settings();
		std::string user_agent;
		int tracker_completion_timeout;
		int tracker_receive_timeout;
		int stop_tracker_timeout;
		int tracker_maximum_response_length;

		int piece_timeout;
		float request_queue_time;
		int max_allowed_in_request_queue;
		int max_out_request_queue;
		int whole_pieces_threshold;
		int peer_timeout;
		int urlseed_timeout;
		int urlseed_pipeline_size;
		int file_pool_size;
		bool allow_multiple_connections_per_ip;
		int max_failcount;
		int min_reconnect_time;
		int peer_connect_timeout;
		bool ignore_limits_on_local_network;
		int connection_speed;
		bool send_redundant_have;
		bool lazy_bitfields;
		int inactivity_timeout;
		int unchoke_interval;
		int optimistic_unchoke_multiplier;
		address announce_ip;
		int num_want;
		int initial_picker_threshold;
		int allowed_fast_set_size;
		int max_outstanding_disk_bytes_per_connection;
		int handshake_timeout;
		bool use_dht_as_fallback;
		bool free_torrent_hashes;
		bool upnp_ignore_nonrouters;
		int send_buffer_watermark;
		bool auto_upload_slots;
		bool use_parole_mode;
		int cache_size;
		int cache_expiry;
		std::pair<int, int> outgoing_ports;
		char peer_tos;

		int active_downloads;
		int active_seeds;
		int active_limit;
		bool dont_count_slow_torrents;
		int auto_manage_interval;
		float share_ratio_limit;
		float seed_time_ratio_limit;
		int seed_time_limit;
		bool close_redundant_connections;

		int auto_scrape_interval;
		int auto_scrape_min_interval;

		int max_peerlist_size;

		int min_announce_interval;

		bool prioritize_partial_pieces;
		int auto_manage_startup;

		bool rate_limit_ip_overhead;
	};

``user_agent`` this is the client identification to the tracker.
The recommended format of this string is:
"ClientName/ClientVersion libtorrent/libtorrentVersion".
This name will not only be used when making HTTP requests, but also when
sending extended headers to peers that support that extension.

``tracker_completion_timeout`` is the number of seconds the tracker
connection will wait from when it sent the request until it considers the
tracker to have timed-out. Default value is 60 seconds.

``tracker_receive_timeout`` is the number of seconds to wait to receive
any data from the tracker. If no data is received for this number of
seconds, the tracker will be considered as having timed out. If a tracker
is down, this is the kind of timeout that will occur. The default value
is 20 seconds.

``stop_tracker_timeout`` is the time to wait for tracker responses when
shutting down the session object. This is given in seconds. Default is
10 seconds.

``tracker_maximum_response_length`` is the maximum number of bytes in a
tracker response. If a response size passes this number it will be rejected
and the connection will be closed. On gzipped responses this size is measured
on the uncompressed data. So, if you get 20 bytes of gzip response that'll
expand to 2 megs, it will be interrupted before the entire response has been
uncompressed (given your limit is lower than 2 megs). Default limit is
1 megabyte.

``piece_timeout`` controls the number of seconds from a request is sent until
it times out if no piece response is returned.

``request_queue_time`` is the length of the request queue given in the number
of seconds it should take for the other end to send all the pieces. i.e. the
actual number of requests depends on the download rate and this number.
	
``max_allowed_in_request_queue`` is the number of outstanding block requests
a peer is allowed to queue up in the client. If a peer sends more requests
than this (before the first one has been handled) the last request will be
dropped. The higher this is, the faster upload speeds the client can get to a
single peer.

``max_out_request_queue`` is the maximum number of outstanding requests to
send to a peer. This limit takes precedence over ``request_queue_time``. i.e.
no matter the download speed, the number of outstanding requests will never
exceed this limit.

``whole_pieces_threshold`` is a limit in seconds. if a whole piece can be
downloaded in at least this number of seconds from a specific peer, the
peer_connection will prefer requesting whole pieces at a time from this peer.
The benefit of this is to better utilize disk caches by doing localized
accesses and also to make it easier to identify bad peers if a piece fails
the hash check.

``peer_timeout`` is the number of seconds the peer connection should
wait (for any activity on the peer connection) before closing it due
to time out. This defaults to 120 seconds, since that's what's specified
in the protocol specification. After half the time out, a keep alive message
is sent.

``urlseed_timeout`` is the same as ``peer_timeout`` but applies only to
url seeds. This value defaults to 20 seconds.

``urlseed_pipeline_size`` controls the pipelining with the web server. When
using persistent connections to HTTP 1.1 servers, the client is allowed to
send more requests before the first response is received. This number controls
the number of outstanding requests to use with url-seeds. Default is 5.

``file_pool_size`` is the the upper limit on the total number of files this
session will keep open. The reason why files are left open at all is that
some anti virus software hooks on every file close, and scans the file for
viruses. deferring the closing of the files will be the difference between
a usable system and a completely hogged down system. Most operating systems
also has a limit on the total number of file descriptors a process may have
open. It is usually a good idea to find this limit and set the number of
connections and the number of files limits so their sum is slightly below it.

``allow_multiple_connections_per_ip`` determines if connections from the
same IP address as existing connections should be rejected or not. Multiple
connections from the same IP address is not allowed by default, to prevent
abusive behavior by peers. It may be useful to allow such connections in
cases where simulations are run on the same machie, and all peers in a
swarm has the same IP address.

``max_failcount`` is the maximum times we try to connect to a peer before
stop connecting again. If a peer succeeds, the failcounter is reset. If
a peer is retrieved from a peer source (other than DHT) the failcount is
decremented by one, allowing another try.

``min_reconnect_time`` is the time to wait between connection attempts. If
the peer fails, the time is multiplied by fail counter.

``peer_connect_timeout`` the number of seconds to wait after a connection
attempt is initiated to a peer until it is considered as having timed out.
The default is 10 seconds. This setting is especially important in case
the number of half-open connections are limited, since stale half-open
connection may delay the connection of other peers considerably.

``ignore_limits_on_local_network``, if set to true, upload, download and
unchoke limits are ignored for peers on the local network.

``connection_speed`` is the number of connection attempts that
are made per second. If a number <= 0 is specified, it will default to
200 connections per second.

``send_redundant_have`` controls if have messages will be sent
to peers that already have the piece. This is typically not necessary,
but it might be necessary for collecting statistics in some cases.
Default is false.

``lazy_bitfields`` prevents outgoing bitfields from being full. If the
client is seed, a few bits will be set to 0, and later filled in with
have-messages. This is to prevent certain ISPs from stopping people
from seeding.

``inactivity_timeout``, if a peer is uninteresting and uninterested
for longer than this number of seconds, it will be disconnected.
Default is 10 minutes

``unchoke_interval`` is the number of seconds between chokes/unchokes.
On this interval, peers are re-evaluated for being choked/unchoked. This
is defined as 30 seconds in the protocol, and it should be significantly
longer than what it takes for TCP to ramp up to it's max rate.

``optimistic_unchoke_multiplier`` is the number of unchoke intervals between
each *optimistic* unchoke interval. On this timer, the currently optimistically
unchoked peer will change.

``announce_ip`` is the ip address passed along to trackers as the ``&ip=`` parameter.
If left as the default (default constructed), that parameter is ommited.

``num_want`` is the number of peers we want from each tracker request. It defines
what is sent as the ``&num_want=`` parameter to the tracker.

``initial_picker_threshold`` specifies the number of pieces we need before we
switch to rarest first picking. This defaults to 4, which means the 4 first
pieces in any torrent are picked at random, the following pieces are picked
in rarest first order.

``allowed_fast_set_size`` is the number of pieces we allow peers to download
from us without being unchoked.

``max_outstanding_disk_bytes_per_connection`` is the number of bytes each
connection is allowed to have waiting in the disk I/O queue before it is
throttled back. This limit is meant to stop fast internet connections to
queue up bufferes indefinitely on slow hard-drives or storage.

``handshake_timeout`` specifies the number of seconds we allow a peer to
delay responding to a protocol handshake. If no response is received within
this time, the connection is closed.

``use_dht_as_fallback`` determines how the DHT is used. If this is true
(which it is by default), the DHT will only be used for torrents where
all trackers in its tracker list has failed. Either by an explicit error
message or a time out.

``free_torrent_hashes`` determines whether or not the torrent's piece hashes
are kept in memory after the torrent becomes a seed or not. If it is set to
``true`` the hashes are freed once the torrent is a seed (they're not
needed anymore since the torrent won't download anything more). If it's set
to false they are not freed. If they are freed, the torrent_info_ returned
by get_torrent_info() will return an object that may be incomplete, that
cannot be passed back to `add_torrent()`_ for instance.

``upnp_ignore_nonrouters`` indicates whether or not the UPnP implementation
should ignore any broadcast response from a device whose address is not the
configured router for this machine. i.e. it's a way to not talk to other
people's routers by mistake.

``send_buffer_waterbark`` is the upper limit of the send buffer low-watermark.
if the send buffer has fewer bytes than this, we'll read another 16kB block
onto it. If set too small, upload rate capacity will suffer. If set too high,
memory will be wasted. The actual watermark may be lower than this in case
the upload rate is low, this is the upper limit.

``auto_upload_slots`` defaults to true. When true, if there is a global upload
limit set and the current upload rate is less than 90% of that, another upload
slot is opened. If the upload rate has been saturated for an extended period
of time, on upload slot is closed. The number of upload slots will never be
less than what has been set by ``session::set_max_uploads()``. To query the
current number of upload slots, see ``session_status::allowed_upload_slots``.

``use_parole_mode`` specifies if parole mode should be used. Parole mode means
that peers that participate in pieces that fail the hash check are put in a mode
where they are only allowed to download whole pieces. If the whole piece a peer
in parole mode fails the hash check, it is banned. If a peer participates in a
piece that passes the hash check, it is taken out of parole mode.

``cache_size`` is the disk write cache. It is specified in units of 16 KiB blocks.
It defaults to 512 (= 8 MB).

``cache_expiry`` is the number of seconds from the last cached write to a piece
in the write cache, to when it's forcefully flushed to disk. Default is 60 second.

``outgoing_ports``, if set to something other than (0, 0) is a range of ports
used to bind outgoing sockets to. This may be useful for users whose router
allows them to assign QoS classes to traffic based on its local port. It is
a range instead of a single port because of the problems with failing to reconnect
to peers if a previous socket to that peer and port is in ``TIME_WAIT`` state.

``peer_tos`` determines the TOS byte set in the IP header of every packet
sent to peers (including web seeds). The default value for this is ``0x0``
(no marking). One potentially useful TOS mark is ``0x20``, this represents
the *QBone scavenger service*. For more details, see QBSS_.

.. _`QBSS`: http://qbone.internet2.edu/qbss/

``active_downloads`` and ``active_seeds`` controls how many active seeding and
downloading torrents the queuing mechanism allows. The target number of active
torrents is ``min(active_downloads + active_seeds, active_limit)``.
``active_downloads`` and ``active_seeds`` are upper limits on the number of
downloading torrents and seeding torrents respectively. Setting the value to
-1 means unlimited.

For example if there are 10 seeding torrents and 10 downloading torrents, and
``active_downloads`` is 4 and ``active_seeds`` is 4, there will be 4 seeds
active and 4 downloading torrents. If the settings are ``active_downloads`` = 2
and ``active_seeds`` = 4, then there will be 2 downloading torrents and 4 seeding
torrents active. Torrents that are not auto managed are also counted against these
limits. If there are non-auto managed torrents that use up all the slots, no
auto managed torrent will be activated.

if ``dont_count_slow_torrents`` is true, torrents without any payload transfers are
not subject to the ``active_seeds`` and ``active_downloads`` limits. This is intended
to make it more likely to utilize all available bandwidth, and avoid having torrents
that don't transfer anything block the active slots.

``active_limit`` is a hard limit on the number of active torrents. This applies even to
slow torrents.

``auto_manage_interval`` is the number of seconds between the torrent queue
is updated, and rotated.

``share_ratio_limit`` is the upload / download ratio limit for considering a
seeding torrent have met the seed limit criteria. See queuing_.

``seed_time_ratio_limit`` is the seeding time / downloading time ratio limit
for considering a seeding torrent to have met the seed limit criteria. See queuing_.

``seed_time_limit`` is the limit on the time a torrent has been an active seed
(specified in seconds) before it is considered having met the seed limit criteria.
See queuing_.

``close_redundant_connections`` specifies whether libtorrent should close
connections where both ends have no utility in keeping the connection open.
For instance if both ends have completed their downloads, there's no point
in keeping it open. This defaults to ``true``.

``auto_scrape_interval`` is the number of seconds between scrapes of
queued torrents (auto managed and paused torrents). Auto managed
torrents that are paused, are scraped regularly in order to keep
track of their downloader/seed ratio. This ratio is used to determine
which torrents to seed and which to pause.

``auto_scrape_min_interval`` is the minimum number of seconds between any
automatic scrape (regardless of torrent). In case there are a large number
of paused auto managed torrents, this puts a limit on how often a scrape
request is sent.

``max_peerlist_size`` is the maximum number of peers in the list of
known peers. These peers are not necessarily connected, so this number
should be much greater than the maximum number of connected peers.
Peers are evicted from the cache when the list grows passed 90% of
this limit, and once the size hits the limit, peers are no longer
added to the list.

``min_announce_interval`` is the minimum allowed announce interval
for a tracker. This is specified in seconds, defaults to 5 minutes and
is used as a sanity check on what is returned from a tracker. It
mitigates hammering misconfigured trackers.

If ``prioritize_partial_pieces`` is true, partial pieces are picked
before pieces that are more rare. If false, rare pieces are always
prioritized, unless the number of partial pieces is growing out of
proportion.

``auto_manage_startup`` is the number of seconds a torrent is considered
active after it was started, regardless of upload and download speed. This
is so that newly started torrents are not considered inactive until they
have a fair chance to start downloading.

If ``rate_limit_ip_overhead`` is set to true, the estimated TCP/IP overhead is
drained from the rate limiters, to avoid exceeding the limits with the total traffic


pe_settings
===========

The ``pe_settings`` structure is used to control the settings related
to peer protocol encryption::

	struct pe_settings
	{
		pe_settings();

		enum enc_policy
		{
			forced,
			enabled,
			disabled
		};

		enum enc_level
		{
			plaintext,
			rc4, 
			both
		};

		enc_policy out_enc_policy;
		enc_policy in_enc_policy;
		enc_level allowed_enc_level;
		bool prefer_rc4;
	};


``in_enc_policy`` and ``out_enc_policy`` control the settings for incoming
and outgoing connections respectively. The settings for these are:

 * ``forced`` - Only encrypted connections are allowed. Incoming connections
   that are not encrypted are closed and if the encrypted outgoing connection
   fails, a non-encrypted retry will not be made.

 * ``enabled`` - encrypted connections are enabled, but non-encrypted
   connections are allowed. An incoming non-encrypted connection will
   be accepted, and if an outgoing encrypted connection fails, a non-
   encrypted connection will be tried.

 * ``disabled`` - only non-encrypted connections are allowed.

``allowed_enc_level`` determines the encryption level of the
connections.  This setting will adjust which encryption scheme is
offered to the other peer, as well as which encryption scheme is
selected by the client. The settings are:

 * ``plaintext`` - only the handshake is encrypted, the bulk of the traffic
   remains unchanged.

 * ``rc4`` - the entire stream is encrypted with RC4

 * ``both`` - both RC4 and plaintext connections are allowed.

``prefer_rc4`` can be set to true if you want to prefer the RC4 encrypted stream.


proxy_settings
==============

The ``proxy_settings`` structs contains the information needed to
direct certain traffic to a proxy.

	::

		struct proxy_settings
		{
			proxy_settings();

			std::string hostname;
			int port;

			std::string username;
			std::string password;

			enum proxy_type
			{
				none,
				socks4,
				socks5,
				socks5_pw,
				http,
				http_pw
			};
		
			proxy_type type;
		};

``hostname`` is the name or IP of the proxy server. ``port`` is the
port number the proxy listens to. If required, ``username`` and ``password``
can be set to authenticate with the proxy.

The ``type`` tells libtorrent what kind of proxy server it is. The following
options are available:

 * ``none`` - This is the default, no proxy server is used, all other fields
   are ignored.

 * ``socks4`` - The server is assumed to be a `SOCKS4 server`_ that
   requires a username.

 * ``socks5`` - The server is assumed to be a SOCKS5 server (`RFC 1928`_) that
   does not require any authentication. The username and password are ignored.

 * ``socks5_pw`` - The server is assumed to be a SOCKS5 server that supports
   plain text username and password authentication (`RFC 1929`_). The username
   and password specified may be sent to the proxy if it requires.

 * ``http`` - The server is assumed to be an HTTP proxy. If the transport used
   for the connection is non-HTTP, the server is assumed to support the
   CONNECT_ method. i.e. for web seeds and HTTP trackers, a plain proxy will
   suffice. The proxy is assumed to not require authorization. The username
   and password will not be used.

 * ``http_pw`` - The server is assumed to be an HTTP proxy that requires
   user authorization. The username and password will be sent to the proxy.

.. _`SOCKS4 server`: http://www.ufasoft.com/doc/socks4_protocol.htm
.. _`RFC 1928`: http://www.faqs.org/rfcs/rfc1928.html
.. _`RFC 1929`: http://www.faqs.org/rfcs/rfc1929.html
.. _CONNECT: draft-luotonen-web-proxy-tunneling-01.txt

ip_filter
=========

The ``ip_filter`` class is a set of rules that uniquely categorizes all
ip addresses as allowed or disallowed. The default constructor creates
a single rule that allows all addresses (0.0.0.0 - 255.255.255.255 for
the IPv4 range, and the equivalent range covering all addresses for the
IPv6 range).

	::

		template <class Addr>
		struct ip_range
		{
			Addr first;
			Addr last;
			int flags;
		};

		class ip_filter
		{
		public:
			enum access_flags { blocked = 1 };

			ip_filter();
			void add_rule(address first, address last, int flags);
			int access(address const& addr) const;

			typedef boost::tuple<std::vector<ip_range<address_v4> >
				, std::vector<ip_range<address_v6> > > filter_tuple_t;

			filter_tuple_t export_filter() const;
		};


ip_filter()
-----------

	::

		ip_filter()

Creates a default filter that doesn't filter any address.

postcondition:
``access(x) == 0`` for every ``x``


add_rule()
----------

	::

		void add_rule(address first, address last, int flags);

Adds a rule to the filter. ``first`` and ``last`` defines a range of
ip addresses that will be marked with the given flags. The ``flags``
can currently be 0, which means allowed, or ``ip_filter::blocked``, which
means disallowed.

precondition:
``first.is_v4() == last.is_v4() && first.is_v6() == last.is_v6()``

postcondition:
``access(x) == flags`` for every ``x`` in the range [``first``, ``last``]

This means that in a case of overlapping ranges, the last one applied takes
precedence.


access()
--------

	::

		int access(address const& addr) const;

Returns the access permissions for the given address (``addr``). The permission
can currently be 0 or ``ip_filter::blocked``. The complexity of this operation
is O(``log`` n), where n is the minimum number of non-overlapping ranges to describe
the current filter.


export_filter()
---------------

	::

		boost::tuple<std::vector<ip_range<address_v4> >
			, std::vector<ip_range<address_v6> > > export_filter() const;

This function will return the current state of the filter in the minimum number of
ranges possible. They are sorted from ranges in low addresses to high addresses. Each
entry in the returned vector is a range with the access control specified in its
``flags`` field.

The return value is a tuple containing two range-lists. One for IPv4 addresses
and one for IPv6 addresses.

      
big_number
==========

Both the ``peer_id`` and ``sha1_hash`` types are typedefs of the class
``big_number``. It represents 20 bytes of data. Its synopsis follows::

	class big_number
	{
	public:
		bool operator==(const big_number& n) const;
		bool operator!=(const big_number& n) const;
		bool operator<(const big_number& n) const;

		const unsigned char* begin() const;
		const unsigned char* end() const;

		unsigned char* begin();
		unsigned char* end();
	};

The iterators gives you access to individual bytes.


bitfield
========

The bitfiled type stores any number of bits as a bitfield in an array.

::

	class bitfield
	{
		bitfield();
		bitfield(int bits);
		bitfield(int bits, bool val);
		bitfield(char const* bytes, int bits);
		bitfield(bitfield const& rhs);

		void borrow_bytes(char* bytes, int bits);
		~bitfield();

		void assign(char const* bytes, int bits);

		bool operator[](int index) const;

		bool get_bit(int index) const;
	
		void clear_bit(int index);
		void set_bit(int index);

		std::size_t size() const;
		bool empty() const;

		char const* bytes() const;

		bitfield& operator=(bitfield const& rhs);

		int count() const;

		typedef const_iterator;
		const_iterator begin() const;
		const_iterator end() const;

		void resize(int bits, bool val);
		void set_all();
		void clear_all();
		void resize(int bits);
	};



hasher
======

This class creates sha1-hashes. Its declaration looks like this::

	class hasher
	{
	public:
		hasher();
		hasher(char const* data, unsigned int len);

		void update(char const* data, unsigned int len);
		sha1_hash final();
		void reset();
	};


You use it by first instantiating it, then call ``update()`` to feed it
with data. i.e. you don't have to keep the entire buffer of which you want to
create the hash in memory. You can feed the hasher parts of it at a time. When
You have fed the hasher with all the data, you call ``final()`` and it
will return the sha1-hash of the data.

The constructor that takes a ``char const*`` and an integer will construct the
sha1 context and feed it the data passed in.

If you want to reuse the hasher object once you have created a hash, you have to
call ``reset()`` to reinitialize it.

The sha1-algorithm used was implemented by Steve Reid and released as public domain.
For more info, see ``src/sha1.cpp``.


fingerprint
===========

The fingerprint class represents information about a client and its version. It is used
to encode this information into the client's peer id.

This is the class declaration::

	struct fingerprint
	{
		fingerprint(const char* id_string, int major, int minor
			, int revision, int tag);

		std::string to_string() const;

		char name[2];
		char major_version;
		char minor_version;
		char revision_version;
		char tag_version;

	};

The constructor takes a ``char const*`` that should point to a string constant containing
exactly two characters. These are the characters that should be unique for your client. Make
sure not to clash with anybody else. Here are some taken id's:

+----------+-----------------------+
| id chars | client                |
+==========+=======================+
| 'AZ'     | Azureus               |
+----------+-----------------------+
| 'LT'     | libtorrent (default)  |
+----------+-----------------------+
| 'BX'     | BittorrentX           |
+----------+-----------------------+
| 'MT'     | Moonlight Torrent     |
+----------+-----------------------+
| 'TS'     | Torrent Storm         |
+----------+-----------------------+
| 'SS'     | Swarm Scope           |
+----------+-----------------------+
| 'XT'     | Xan Torrent           |
+----------+-----------------------+

There's currently an informal directory of client id's here__.

__ http://wiki.theory.org/BitTorrentSpecification#peer_id


The ``major``, ``minor``, ``revision`` and ``tag`` parameters are used to identify the
version of your client. All these numbers must be within the range [0, 9].

``to_string()`` will generate the actual string put in the peer-id, and return it.


UPnP and NAT-PMP
================

The ``upnp`` and ``natpmp`` classes contains the state for all UPnP and NAT-PMP mappings,
by default 1 or two mappings are made by libtorrent, one for the listen port and one
for the DHT port (UDP).

::

	class upnp
	{
	public:

		enum protocol_type { none = 0, udp = 1, tcp = 2 };
		int add_mapping(protocol_type p, int external_port, int local_port);
		void delete_mapping(int mapping_index);
	
		void discover_device();
		void close();
	
		std::string router_model();
	};

	class natpmp
	{
	public:
	
		enum protocol_type { none = 0, udp = 1, tcp = 2 };
		int add_mapping(protocol_type p, int external_port, int local_port);
		void delete_mapping(int mapping_index);
	
		void close();
		void rebind(address const& listen_interface);
	};

``discover_device()``, ``close()`` and ``rebind()`` are for internal uses and should
not be called directly by clients.

add_mapping
-----------

	::

		int add_mapping(protocol_type p, int external_port, int local_port);

Attempts to add a port mapping for the specified protocol. Valid protocols are
``upnp::tcp`` and ``upnp::udp`` for the UPnP class and ``natpmp::tcp`` and
``natpmp::udp`` for the NAT-PMP class.

``external_port`` is the port on the external address that will be mapped. This
is a hint, you are not guaranteed that this port will be available, and it may
end up being something else. In the portmap_alert_ notification, the actual
external port is reported.

``local_port`` is the port in the local machine that the mapping should forward
to.

The return value is an index that identifies this port mapping. This is used
to refer to mappings that fails or succeeds in the portmap_error_alert_ and
portmap_alert_ respectively. If The mapping fails immediately, the return value
is -1, which means failure. There will not be any error alert notification for
mappings that fail with a -1 return value.

delete_mapping
--------------

	::

		void delete_mapping(int mapping_index);

This function removes a port mapping. ``mapping_index`` is the index that refers
to the mapping you want to remove, which was returned from add_mapping_.

router_model()
--------------

	::

		std::string router_model();

This is only available for UPnP routers. If the model is advertized by
the router, it can be queried through this function.


free functions
==============

identify_client()
-----------------

	::

		std::string identify_client(peer_id const& id);

This function is declared in the header ``<libtorrent/identify_client.hpp>``. It can can be used
to extract a string describing a client version from its peer-id. It will recognize most clients
that have this kind of identification in the peer-id.


client_fingerprint()
--------------------

	::

		boost::optional<fingerprint> client_fingerprint(peer_id const& p);

Returns an optional fingerprint if any can be identified from the peer id. This can be used
to automate the identification of clients. It will not be able to identify peers with non-
standard encodings. Only Azureus style, Shadow's style and Mainline style. This function is
declared in the header ``<libtorrent/identify_client.hpp>``.


bdecode() bencode()
-------------------

	::

		template<class InIt> entry bdecode(InIt start, InIt end);
		template<class OutIt> void bencode(OutIt out, const entry& e);


These functions will encode data to bencoded_ or decode bencoded_ data.

.. _bencoded: http://wiki.theory.org/index.php/BitTorrentSpecification

The entry_ class is the internal representation of the bencoded data
and it can be used to retrieve information, an entry_ can also be build by
the program and given to ``bencode()`` to encode it into the ``OutIt``
iterator.

The ``OutIt`` and ``InIt`` are iterators
(InputIterator_ and OutputIterator_ respectively). They
are templates and are usually instantiated as ostream_iterator_,
back_insert_iterator_ or istream_iterator_. These
functions will assume that the iterator refers to a character
(``char``). So, if you want to encode entry ``e`` into a buffer
in memory, you can do it like this::

	std::vector<char> buffer;
	bencode(std::back_inserter(buf), e);

.. _InputIterator: http://www.sgi.com/tech/stl/InputIterator.html
.. _OutputIterator: http://www.sgi.com/tech/stl/OutputIterator.html
.. _ostream_iterator: http://www.sgi.com/tech/stl/ostream_iterator.html
.. _back_insert_iterator: http://www.sgi.com/tech/stl/back_insert_iterator.html
.. _istream_iterator: http://www.sgi.com/tech/stl/istream_iterator.html

If you want to decode a torrent file from a buffer in memory, you can do it like this::

	std::vector<char> buffer;
	// ...
	entry e = bdecode(buf.begin(), buf.end());

Or, if you have a raw char buffer::

	const char* buf;
	// ...
	entry e = bdecode(buf, buf + data_size);

Now we just need to know how to retrieve information from the entry_.

If ``bdecode()`` encounters invalid encoded data in the range given to it
it will throw invalid_encoding_.

add_magnet_uri()
----------------

	::

		torrent_handle add_magnet_uri(session& ses, std::string const& uri
			add_torrent_params p);

This function parses the magnet URI (``uri``) as a bittorrent magnet link,
and adds the torrent to the specified session (``ses``). It returns the
handle to the newly added torrent, or an invalid handle in case parsing
failed. To control some initial settings of the torrent, sepcify those in
the ``add_torrent_params``, ``p``. See `add_torrent()`_.

For more information about magnet links, see `magnet links`_.

make_magnet_uri()
-----------------

	::

		std::string make_magnet_uri(torrent_handle const& handle);

Generates a magnet URI from the specified torrent. If the torrent
handle is invalid, an empty string is returned.

For more information about magnet links, see `magnet links`_.


alerts
======

The ``pop_alert()`` function on session is the interface for retrieving
alerts, warnings, messages and errors from libtorrent. If no alerts have
been posted by libtorrent ``pop_alert()`` will return a default initialized
``auto_ptr`` object. If there is an alert in libtorrent's queue, the alert
from the front of the queue is popped and returned.
You can then use the alert object and query

By default, only errors are reported. ``session::set_alert_mask()`` can be
used to specify which kinds of events should be reported. The alert mask
is a bitmask with the following bits:

+--------------------------------+---------------------------------------------------------------------+
| ``error_notification``         | Enables alerts that report an error. This includes:                 |
|                                |                                                                     |
|                                | * tracker errors                                                    |
|                                | * tracker warnings                                                  |
|                                | * file errors                                                       |
|                                | * resume data failures                                              |
|                                | * web seed errors                                                   |
|                                | * .torrent files errors                                             |
|                                | * listen socket errors                                              |
|                                | * port mapping errors                                               |
+--------------------------------+---------------------------------------------------------------------+
| ``peer_notification``          | Enables alerts when peers send invalid requests, get banned or      |
|                                | snubbed.                                                            |
+--------------------------------+---------------------------------------------------------------------+
| ``port_mapping_notification``  | Enables alerts for port mapping events. For NAT-PMP and UPnP.       |
+--------------------------------+---------------------------------------------------------------------+
| ``storage_notification``       | Enables alerts for events related to the storage. File errors and   |
|                                | synchronization events for moving the storage, renaming files etc.  |
+--------------------------------+---------------------------------------------------------------------+
| ``tracker_notification``       | Enables all tracker events. Includes announcing to trackers,        |
|                                | receiving responses, warnings and errors.                           |
+--------------------------------+---------------------------------------------------------------------+
| ``debug_notification``         | Low level alerts for when peers are connected and disconnected.     |
+--------------------------------+---------------------------------------------------------------------+
| ``status_notification``        | Enables alerts for when a torrent or the session changes state.     |
+--------------------------------+---------------------------------------------------------------------+
| ``progress_notification``      | Alerts for when blocks are requested and completed. Also when       |
|                                | pieces are completed.                                               |
+--------------------------------+---------------------------------------------------------------------+
| ``ip_block_notification``      | Alerts when a peer is blocked by the ip blocker or port blocker.    |
+--------------------------------+---------------------------------------------------------------------+
| ``performance_warning``        | Alerts when some limit is reached that might limit the download     |
|                                | or upload rate.                                                     |
+--------------------------------+---------------------------------------------------------------------+
| ``all_categories``             | The full bitmask, representing all available categories.            |
+--------------------------------+---------------------------------------------------------------------+

Every alert belongs to one or more category. There is a small cost involved in posting alerts. Only
alerts that belong to an enabled category are posted. Setting the alert bitmask to 0 will disable
all alerts

When you get an alert, you can use ``typeid()`` or ``dynamic_cast<>`` to get more detailed
information on exactly which type it is. i.e. what kind of error it is. You can also use a
dispatcher_ mechanism that's available in libtorrent.

All alert types are defined in the ``<libtorrent/alert_types.hpp>`` header file.

The ``alert`` class is the base class that specific messages are derived from. This
is its synopsis:

.. parsed-literal::

	class alert
	{
	public:

		enum category_t
		{
			error_notification = *implementation defined*,
			peer_notification = *implementation defined*,
			port_mapping_notification = *implementation defined*,
			storage_notification = *implementation defined*,
			tracker_notification = *implementation defined*,
			debug_notification = *implementation defined*,
			status_notification = *implementation defined*,
			progress_notification = *implementation defined*,
			ip_block_notification = *implementation defined*,
			performance_warning = *implementation defined*,

			all_categories = *implementation defined*
		};

		ptime timestamp() const;

		virtual ~alert();

		virtual std::string message() const = 0;
		virtual char const* what() const = 0;
		virtual int category() const = 0;
		virtual std::auto_ptr<alert> clone() const = 0;
	};

``what()`` returns a string literal describing the type of the alert. It does
not include any information that might be bundled with the alert.

``category()`` returns a bitmask specifying which categories this alert belong to.

``clone()`` returns a pointer to a copy of the alert.

``message()`` generate a string describing the alert and the information bundled
with it. This is mainly intended for debug and development use. It is not suitable
to use this for applications that may be localized. Instead, handle each alert
type individually and extract and render the information from the alert depending
on the locale.

There's another alert base class that most alerts derives from, all the
alerts that are generated for a specific torrent are derived from::

	struct torrent_alert: alert
	{
		// ...
		torrent_handle handle;
	};

There's also a base class for all alerts referring to tracker events::

	struct tracker_alert: torrent_alert
	{
		// ...
		std::string url;
	};

The specific alerts are:

external_ip_alert
-----------------

Whenever libtorrent learns about the machines external IP, this alert is
generated. The external IP address can be acquired from the tracker (if it
supports that) or from peers that supports the extension protocol.
The address can be accessed through the ``external_address`` member.

::

	struct external_ip_alert: alert
	{
		// ...
		address external_address;
	};


listen_failed_alert
-------------------

This alert is generated when none of the ports, given in the port range, to
session_ can be opened for listening. This alert doesn't have any extra
data members.


portmap_error_alert
-------------------

This alert is generated when a NAT router was successfully found but some
part of the port mapping request failed. It contains a text message that
may help the user figure out what is wrong. This alert is not generated in
case it appears the client is not running on a NAT:ed network or if it
appears there is no NAT router that can be remote controlled to add port
mappings.

``mapping`` refers to the mapping index of the port map that failed, i.e.
the index returned from add_mapping_.

``type`` is 0 for NAT-PMP and 1 for UPnP.

::

	struct portmap_error_alert: alert
	{
		// ...
		int mapping;
		int type;
	};

portmap_alert
-------------

This alert is generated when a NAT router was successfully found and
a port was successfully mapped on it. On a NAT:ed network with a NAT-PMP
capable router, this is typically generated once when mapping the TCP
port and, if DHT is enabled, when the UDP port is mapped.

``mapping`` refers to the mapping index of the port map that failed, i.e.
the index returned from add_mapping_.

``external_port`` is the external port allocated for the mapping.

``type`` is 0 for NAT-PMP and 1 for UPnP.

::

	struct portmap_alert: alert
	{
		// ...
		int mapping;
		int external_port;
		int type;
	};

file_error_alert
----------------

If the storage fails to read or write files that it needs access to, this alert is
generated and the torrent is paused.

``file`` is the path to the file that was accessed when the error occurred.

``msg`` is the error message received from the OS.

::

	struct file_error_alert: torrent_alert
	{
		// ...
		std::string file;
		std::string msg;
	};


tracker_announce_alert
----------------------

This alert is generated each time a tracker announce is sent (or attempted to be sent).
There are no extra data members in this alert. The url can be found in the base class
however.

::

	struct tracker_announce_alert: tracker_alert
	{
		// ...
		int event;
	};

Event specifies what event was sent to the tracker. It is defined as:

0. None
1. Completed
2. Started
3. Stopped


tracker_error_alert
-------------------

This alert is generated on tracker time outs, premature disconnects, invalid response or
a HTTP response other than "200 OK". From the alert you can get the handle to the torrent
the tracker belongs to.

The ``times_in_row`` member says how many times in a row this tracker has failed.
``status_code`` is the code returned from the HTTP server. 401 means the tracker needs
authentication, 404 means not found etc. If the tracker timed out, the code will be set
to 0.

::

	struct tracker_error_alert: tracker_alert
	{
		// ...
		int times_in_row;
		int status_code;
	};


tracker_reply_alert
-------------------

This alert is only for informational purpose. It is generated when a tracker announce
succeeds. It is generated regardless what kind of tracker was used, be it UDP, HTTP or
the DHT.

::

	struct tracker_reply_alert: tracker_alert
	{
		// ...
		int num_peers;
	};

The ``num_peers`` tells how many peers were returned from the tracker. This is
not necessarily all new peers, some of them may already be connected.

dht_reply_alert
---------------

This alert is generated each time the DHT receives peers from a node. ``num_peers``
is the number of peers we received in this packet. Typically these packets are
received from multiple DHT nodes, and so the alerts are typically generated
a few at a time.

::

	struct dht_reply_alert: tracker_alert
	{
		// ...
		int num_peers;
	};


tracker_warning_alert
---------------------

This alert is triggered if the tracker reply contains a warning field. Usually this
means that the tracker announce was successful, but the tracker has a message to
the client. The ``msg`` string in the alert contains the warning message from
the tracker.

::

	struct tracker_warning_alert: tracker_alert
	{
		// ...
		std::string msg;
	};

scrape_reply_alert
------------------

This alert is generated when a scrape request succeeds. ``incomplete``
and ``complete`` is the data returned in the scrape response. These numbers
may be -1 if the reponse was malformed.

::

	struct scrape_reply_alert: tracker_alert
	{
		// ...
		int incomplete;
		int complete;
	};


scrape_failed_alert
-------------------

If a scrape request fails, this alert is generated. This might be due
to the tracker timing out, refusing connection or returning an http response
code indicating an error. ``msg`` contains a message describing the error.

::

	struct scrape_failed_alert: tracker_alert
	{
		// ...
		std::string msg;
	};

url_seed_alert
--------------

This alert is generated when a HTTP seed name lookup fails.

It contains ``url`` to the HTTP seed that failed along with an error message.

::

	struct url_seed_alert: torrent_alert
	{
		// ...
		std::string url;
	};

   
hash_failed_alert
-----------------

This alert is generated when a finished piece fails its hash check. You can get the handle
to the torrent which got the failed piece and the index of the piece itself from the alert.

::

	struct hash_failed_alert: torrent_alert
	{
		// ...
		int piece_index;
	};


peer_ban_alert
--------------

This alert is generated when a peer is banned because it has sent too many corrupt pieces
to us. ``ip`` is the endpoint to the peer that was banned.

::

	struct peer_ban_alert: torrent_alert
	{
		// ...
		asio::ip::tcp::endpoint ip;
	};


peer_error_alert
----------------

This alert is generated when a peer sends invalid data over the peer-peer protocol. The peer
will be disconnected, but you get its ip address from the alert, to identify it.

::

	struct peer_error_alert: torrent_alert
	{
		// ...
		asio::ip::tcp::endpoint ip;
		peer_id id;
	};


invalid_request_alert
---------------------

This is a debug alert that is generated by an incoming invalid piece request.
``ìp`` is the address of the peer and the ``request`` is the actual incoming
request from the peer.

::

	struct invalid_request_alert: torrent_alert
	{
		invalid_request_alert(
			peer_request const& r
			, torrent_handle const& h
			, asio::ip::tcp::endpoint const& send
			, peer_id const& pid
			, std::string const& msg);

		virtual std::auto_ptr<alert> clone() const;

		asio::ip::tcp::endpoint ip;
		peer_request request;
		peer_id id;
	};


	struct peer_request
	{
		int piece;
		int start;
		int length;
		bool operator==(peer_request const& r) const;
	};


The ``peer_request`` contains the values the client sent in its ``request`` message. ``piece`` is
the index of the piece it want data from, ``start`` is the offset within the piece where the data
should be read, and ``length`` is the amount of data it wants.

torrent_finished_alert
----------------------

This alert is generated when a torrent switches from being a downloader to a seed.
It will only be generated once per torrent. It contains a torrent_handle to the
torrent in question.

There are no additional data members in this alert.


performance_alert
-----------------

This alert is generated when a limit is reached that might have a negative impact on
upload or download rate performance.

::

	struct performance_alert: torrent_alert
	{
		// ...

		enum performance_warning_t
		{
			outstanding_disk_buffer_limit_reached,
			outstanding_request_limit_reached,
		};

		performance_warning_t warning_code;
	};


metadata_failed_alert
---------------------

This alert is generated when the metadata has been completely received and the info-hash
failed to match it. i.e. the metadata that was received was corrupt. libtorrent will
automatically retry to fetch it in this case. This is only relevant when running a
torrent-less download, with the metadata extension provided by libtorrent.

There are no additional data members in this alert.


metadata_received_alert
-----------------------

This alert is generated when the metadata has been completely received and the torrent
can start downloading. It is not generated on torrents that are started with metadata, but
only those that needs to download it from peers (when utilizing the libtorrent extension).

There are no additional data members in this alert.


fastresume_rejected_alert
-------------------------

This alert is generated when a fastresume file has been passed to ``add_torrent`` but the
files on disk did not match the fastresume file. The string explains the reason why the
resume file was rejected.

::

	struct fastresume_rejected_alert: torrent_alert
	{
		// ...
		std::string msg;
	};


peer_blocked_alert
------------------

This alert is generated when a peer is blocked by the IP filter. The ``ip`` member is the
address that was blocked.

::

	struct peer_blocked_alert: alert
	{
		// ...
		address ip;
	};


storage_moved_alert
-------------------

The ``storage_moved_alert`` is generated when all the disk IO has completed and the
files have been moved, as an effect of a call to ``torrent_handle::move_storage``. This
is useful to synchronize with the actual disk. The ``path`` member is the new path of
the storage.

::

	struct storage_moved_alert: torrent_alert
	{
		// ...
		std::string path;
	};


torrent_paused_alert
--------------------

This alert is generated as a response to a ``torrent_handle::pause`` request. It is
generated once all disk IO is complete and the files in the torrent have been closed.
This is useful for synchronizing with the disk.

There are no additional data members in this alert.

torrent_resumed_alert
---------------------

This alert is generated as a response to a ``torrent_handle::resume`` request. It is
generated when a torrent goes from a paused state to an active state.

There are no additional data members in this alert.

save_resume_data_alert
----------------------

This alert is generated as a response to a ``torrent_handle::save_resume_data`` request.
It is generated once the disk IO thread is done writing the state for this torrent.
The ``resume_data`` member points to the resume data.

::

	struct save_resume_data_alert: torrent_alert
	{
		// ...
		boost::shared_ptr<entry> resume_data;
	};

save_resume_data_failed_alert
-----------------------------

This alert is generated instead of ``save_resume_data_alert`` if there was an error
generating the resume data. ``msg`` describes what went wrong.

::

	struct save_resume_data_failed_alert: torrent_alert
	{
		// ...
		std::string msg;
	};

dispatcher
----------

The ``handle_alert`` class is defined in ``<libtorrent/alert.hpp>``.

Examples usage::

	struct my_handler
	{
		void operator()(portmap_error_alert const& a)
		{
			std::cout << "Portmapper: " << a.msg << std::endl;
		}

		void operator()(tracker_warning_alert const& a)
		{
			std::cout << "Tracker warning: " << a.msg << std::endl;
		}

		void operator()(torrent_finished_alert const& a)
		{
			// write fast resume data
			// ...

			std::cout << a.handle.get_torrent_info().name() << "completed"
				<< std::endl;
		}
	};

::

	std::auto_ptr<alert> a;
	a = ses.pop_alert();
	my_handler h;
	while (a.get())
	{
		handle_alert<portmap_error_alert
			, tracker_warning_alert
			, torrent_finished_alert
		>::handle_alert(h, a);
		a = ses.pop_alert();
	}

In this example 3 alert types are used. You can use any number of template
parameters to select between more types. If the number of types are more than
15, you can define ``TORRENT_MAX_ALERT_TYPES`` to a greater number before
including ``<libtorrent/alert.hpp>``.


exceptions
==========

There are a number of exceptions that can be thrown from different places in libtorrent,
here's a complete list with description.


invalid_handle
--------------

This exception is thrown when querying information from a torrent_handle_ that hasn't
been initialized or that has become invalid.

::

	struct invalid_handle: std::exception
	{
		const char* what() const throw();
	};


duplicate_torrent
-----------------

This is thrown by `add_torrent()`_ if the torrent already has been added to
the session. Since `remove_torrent()`_ is asynchronous, this exception may
be thrown if the torrent is removed and then immediately added again.

::

	struct duplicate_torrent: std::exception
	{
		const char* what() const throw();
	};


invalid_encoding
----------------

This is thrown by ``bdecode()`` if the input data is not a valid bencoding.

::

	struct invalid_encoding: std::exception
	{
		const char* what() const throw();
	};


type_error
----------

This is thrown from the accessors of ``entry`` if the data type of the ``entry`` doesn't
match the type you want to extract from it.

::

	struct type_error: std::runtime_error
	{
		type_error(const char* error);
	};


invalid_torrent_file
--------------------

This exception is thrown from the constructor of ``torrent_info`` if the given bencoded information
doesn't meet the requirements on what information has to be present in a torrent file.

::

	struct invalid_torrent_file: std::exception
	{
		const char* what() const throw();
	};


storage_interface
=================

The storage interface is a pure virtual class that can be implemented to
change the behavior of the actual file storage. The interface looks like
this::

	struct storage_interface
	{
		virtual bool initialize(bool allocate_files) = 0;
		virtual int read(char* buf, int slot, int offset, int size) = 0;
		virtual int write(const char* buf, int slot, int offset, int size) = 0;
		virtual bool move_storage(fs::path save_path) = 0;
		virtual bool verify_resume_data(lazy_entry const& rd, std::string& error) = 0;
		virtual bool write_resume_data(entry& rd) const = 0;
		virtual bool move_slot(int src_slot, int dst_slot) = 0;
		virtual bool swap_slots(int slot1, int slot2) = 0;
		virtual bool swap_slots3(int slot1, int slot2, int slot3) = 0;
		virtual sha1_hash hash_for_slot(int slot, partial_hash& h, int piece_size) = 0;
		virtual bool rename_file(int file, std::string const& new_name) = 0;
		virtual bool release_files() = 0;
		virtual bool delete_files() = 0;
		virtual ~storage_interface() {}
	};


initialize()
------------

	::

		bool initialize(bool allocate_files) = 0;

This function is called when the storage is to be initialized. The default storage
will create directories and empty files at this point. If ``allocate_files`` is true,
it will also ``ftruncate`` all files to their target size.

Returning ``true`` indicates an error occurred.

read()
------

	::

		int read(char* buf, int slot, int offset, int size) = 0;

This function should read the data in the given slot and at the given offset
and ``size`` number of bytes. The data is to be copied to ``buf``.

The return value is the number of bytes actually read.


write()
-------

	::

		int write(const char* buf, int slot, int offset, int size) = 0;

This function should write the data in ``buf`` to the given slot (``slot``) at offset
``offset`` in that slot. The buffer size is ``size``.

The return value is the number of bytes actually written.


move_storage()
--------------

	::

		bool move_storage(fs::path save_path) = 0;

This function should move all the files belonging to the storage to the new save_path.
The default storage moves the single file or the directory of the torrent.

Before moving the files, any open file handles may have to be closed, like
``release_files()``.

Returning ``true`` indicates an error occurred.


verify_resume_data()
--------------------

	::

		bool verify_resume_data(lazy_entry const& rd, std::string& error) = 0;

This function should verify the resume data ``rd`` with the files
on disk. If the resume data seems to be up-to-date, return true. If
not, set ``error`` to a description of what mismatched and return false.

The default storage may compare file sizes and time stamps of the files.

Returning ``true`` indicates an error occurred.


write_resume_data()
-------------------

	::

		bool write_resume_data(entry& rd) const = 0;

This function should fill in resume data, the current state of the
storage, in ``rd``. The default storage adds file timestamps and
sizes.

Returning ``true`` indicates an error occurred.


move_slot()
-----------

	::

		bool move_slot(int src_slot, int dst_slot) = 0;

This function should copy or move the data in slot ``src_slot`` to
the slot ``dst_slot``. This is only used in compact mode.

If the storage caches slots, this could be implemented more
efficient than reading and writing the data.

Returning ``true`` indicates an error occurred.


swap_slots()
------------

	::

		bool swap_slots(int slot1, int slot2) = 0;

This function should swap the data in ``slot1`` and ``slot2``. The default
storage uses a scratch buffer to read the data into, then moving the other
slot and finally writing back the temporary slot's data

This is only used in compact mode.

Returning ``true`` indicates an error occurred.


swap_slots3()
-------------

	::

		bool swap_slots3(int slot1, int slot2, int slot3) = 0;

This function should do a 3-way swap, or shift of the slots. ``slot1``
should move to ``slot2``, which should be moved to ``slot3`` which in turn
should be moved to ``slot1``.

This is only used in compact mode.

Returning ``true`` indicates an error occurred.


hash_for_slot()
---------------

	::

		sha1_hash hash_for_slot(int slot, partial_hash& h, int piece_size) = 0;

The function should read the remaining bytes of the slot and hash it with the
sha-1 state in ``partion_hash``. The ``partial_hash`` struct looks like this::

	struct partial_hash
	{
		partial_hash();
		int offset;
		hasher h;
	};

``offset`` is the number of bytes in the slot that has already been hashed, and
``h`` is the sha-1 state of that hash. ``piece_size`` is the size of the piece
that is stored in the given slot.

The function should return the hash of the piece stored in the slot.

rename_file()
-------------

	::

		bool rename_file(int file, std::string const& new_name) = 0;

Rename file with index ``file`` to the thame ``new_name``. If there is an error,
``true`` should be returned.


release_files()
---------------

	::

		bool release_files() = 0;

This function should release all the file handles that it keeps open to files
belonging to this storage. The default implementation just calls
``file_pool::release_files(this)``.

Returning ``true`` indicates an error occurred.


delete_files()
--------------

	::

		bool delete_files() = 0;

This function should delete all files and directories belonging to this storage.

Returning ``true`` indicates an error occurred.


magnet links
============

Magnet links are URIs that includes an info-hash, a display name and optionally
a tracker url. The idea behind magnet links is that an end user can click on a
link in a browser and have it handled by a bittorrent application, to start a
download, without any .torrent file.

The format of the magnet URI is:

**magnet:?xt=urn:btih:** *Base32 encoded info-hash* [ **&dn=** *name of download* ] [ **&tr=** *tracker URL* ]*

queuing
=======

libtorrent supports *queuing*. Which means it makes sure that a limited number of
torrents are being downloaded at any given time, and once a torrent is completely
downloaded, the next in line is started.

Torrents that are *auto managed* are subject to the queuing and the active torrents
limits. To make a torrent auto managed, set ``auto_managed`` to true when adding the
torrent (see `add_torrent()`_).

The limits of the number of downloading and seeding torrents are controlled via
``active_downloads``, ``active_seeds`` and ``active_limit`` in session_settings_. 
These limits takes non auto managed torrents into account as well. If there are 
more non-auto managed torrents being downloaded than the ``active_downloads`` 
setting, any auto managed torrents will be queued until torrents are removed so 
that the number drops below the limit.

The default values are 8 active downloads and 5 active seeds.

At a regular interval, torrents are checked if there needs to be any re-ordering of
which torrents are active and which are queued. This interval can be controlled via
``auto_manage_interval`` in session_settings_. It defaults to every 30 seconds.

For queuing to work, resume data needs to be saved and restored for all torrents.
See `save_resume_data()`_.

downloading
-----------

Torrents that are currently being downloaded or incomplete (with bytes still to download)
are queued. The torrents in the front of the queue are started to be actively downloaded
and the rest are ordered with regards to their queue position. Any newly added torrent
is placed at the end of the queue. Once a torrent is removed or turns into a seed, its
queue position is -1 and all torrents that used to be after it in the queue, decreases their
position in order to fill the gap.

The queue positions are always in a sequence without any gaps.

Lower queue position means closer to the front of the queue, and will be started sooner than
torrents with higher queue positions.

To query a torrent for its position in the queue, or change its position, see:
`queue_position() queue_position_up() queue_position_down() queue_position_top() queue_position_bottom()`_.

seeding
-------

Auto managed seeding torrents are rotated, so that all of them are allocated a fair
amount of seeding. Torrents with fewer completed *seed cycles* are prioritized for
seeding. A seed cycle is completed when a torrent meets either the share ratio limit
(uploaded bytes / downloaded bytes), the share time ratio (time seeding / time
downloaing) or seed time limit (time seeded).

The relevant settings to control these limits are ``share_ratio_limit``,
``seed_time_ratio_limit`` and ``seed_time_limit`` in session_settings_.


fast resume
===========

The fast resume mechanism is a way to remember which pieces are downloaded
and where they are put between sessions. You can generate fast resume data by
calling `save_resume_data()`_ on torrent_handle_. You can
then save this data to disk and use it when resuming the torrent. libtorrent
will not check the piece hashes then, and rely on the information given in the
fast-resume data. The fast-resume data also contains information about which
blocks, in the unfinished pieces, were downloaded, so it will not have to
start from scratch on the partially downloaded pieces.

To use the fast-resume data you simply give it to `add_torrent()`_, and it
will skip the time consuming checks. It may have to do the checking anyway, if
the fast-resume data is corrupt or doesn't fit the storage for that torrent,
then it will not trust the fast-resume data and just do the checking.

file format
-----------

The file format is a bencoded dictionary containing the following fields:

+----------------------+--------------------------------------------------------------+
| ``file-format``      | string: "libtorrent resume file"                             |
|                      |                                                              |
+----------------------+--------------------------------------------------------------+
| ``file-version``     | integer: 1                                                   |
|                      |                                                              |
+----------------------+--------------------------------------------------------------+
| ``info-hash``        | string, the info hash of the torrent this data is saved for. |
|                      |                                                              |
+----------------------+--------------------------------------------------------------+
| ``blocks per piece`` | integer, the number of blocks per piece. Must be: piece_size |
|                      | / (16 * 1024). Clamped to be within the range [1, 256]. It   |
|                      | is the number of blocks per (normal sized) piece. Usually    |
|                      | each block is 16 * 1024 bytes in size. But if piece size is  |
|                      | greater than 4 megabytes, the block size will increase.      |
|                      |                                                              |
+----------------------+--------------------------------------------------------------+
| ``pieces``           | A string with piece flags, one character per piece.          |
|                      | Bit 1 means we have that piece.                              |
+----------------------+--------------------------------------------------------------+
| ``slots``            | list of integers. The list maps slots to piece indices. It   |
|                      | tells which piece is on which slot. If piece index is -2 it  |
|                      | means it is free, that there's no piece there. If it is -1,  |
|                      | means the slot isn't allocated on disk yet. The pieces have  |
|                      | to meet the following requirement:                           |
|                      |                                                              |
|                      | If there's a slot at the position of the piece index,        |
|                      | the piece must be located in that slot.                      |
|                      |                                                              |
+----------------------+--------------------------------------------------------------+
| ``peers``            | list of dictionaries. Each dictionary has the following      |
|                      | layout:                                                      |
|                      |                                                              |
|                      | +----------+-----------------------------------------------+ |
|                      | | ``ip``   | string, the ip address of the peer. This is   | |
|                      | |          | not a binary representation of the ip         | |
|                      | |          | address, but the string representation. It    | |
|                      | |          | may be an IPv6 string or an IPv4 string.      | |
|                      | +----------+-----------------------------------------------+ |
|                      | | ``port`` | integer, the listen port of the peer          | |
|                      | +----------+-----------------------------------------------+ |
|                      |                                                              |
|                      | These are the local peers we were connected to when this     |
|                      | fast-resume data was saved.                                  |
|                      |                                                              |
+----------------------+--------------------------------------------------------------+
| ``unfinished``       | list of dictionaries. Each dictionary represents an          |
|                      | piece, and has the following layout:                         |
|                      |                                                              |
|                      | +-------------+--------------------------------------------+ |
|                      | | ``piece``   | integer, the index of the piece this entry | |
|                      | |             | refers to.                                 | |
|                      | +-------------+--------------------------------------------+ |
|                      | | ``bitmask`` | string, a binary bitmask representing the  | |
|                      | |             | blocks that have been downloaded in this   | |
|                      | |             | piece.                                     | |
|                      | +-------------+--------------------------------------------+ |
|                      | | ``adler32`` | The adler32 checksum of the data in the    | |
|                      | |             | blocks specified by ``bitmask``.           | |
|                      | |             |                                            | |
|                      | +-------------+--------------------------------------------+ |
|                      |                                                              |
+----------------------+--------------------------------------------------------------+
| ``file sizes``       | list where each entry corresponds to a file in the file list |
|                      | in the metadata. Each entry has a list of two values, the    |
|                      | first value is the size of the file in bytes, the second     |
|                      | is the time stamp when the last time someone wrote to it.    |
|                      | This information is used to compare with the files on disk.  |
|                      | All the files must match exactly this information in order   |
|                      | to consider the resume data as current. Otherwise a full     |
|                      | re-check is issued.                                          |
+----------------------+--------------------------------------------------------------+
| ``allocation``       | The allocation mode for the storage. Can be either ``full``  |
|                      | or ``compact``. If this is full, the file sizes and          |
|                      | timestamps are disregarded. Pieces are assumed not to have   |
|                      | moved around even if the files have been modified after the  |
|                      | last resume data checkpoint.                                 |
+----------------------+--------------------------------------------------------------+

threads
=======

libtorrent starts 2 or 3 threads.

 * The first thread is the main thread that will sit
   idle in a ``select()`` call most of the time. This thread runs the main loop
   that will send and receive data on all connections.

 * The second thread is the disk I/O thread. All disk read and write operations
   are passed to this thread and messages are passed back to the main thread when
   the operation compeltes. The disk thread also verifies the piece hashes.

 * The third and forth threads are spawned by asio on systems that don't support
   non-blocking host name resolution to simulate non-blocking getaddrinfo().


storage allocation
==================

There are three modes in which storage (files on disk) are allocated in libtorrent.

1. The traditional *full allocation* mode, where the entire files are filled up with
   zeros before anything is downloaded. libtorrent will look for sparse files support
   in the filesystem that is used for storage, and use sparse files or file system
   zero fill support if present. This means that on NTFS, full allocation mode will
   only allocate storage for the downloaded pieces.

2. The *compact allocation* mode, where only files are allocated for actual
   pieces that have been downloaded.

3. The *sparse allocation*, sparse files are used, and pieces are downloaded directly
   to where they belong. This is the recommended (and default) mode.

The allocation mode is selected when a torrent is started. It is passed as an
argument to ``session::add_torrent()`` (see `add_torrent()`_).

The decision to use full allocation or compact allocation typically depends on whether
any files are filtered and if the filesystem supports sparse files.

sparse allocation
-----------------

On filesystems that supports sparse files, this allocation mode will only use
as much space as has been downloaded.

 * It does not require an allocation pass on startup.

 * It supports skipping files (setting prioirty to 0 to not download).

 * Fast resume data will remain valid even when file time stamps are out of date.


full allocation
---------------

When a torrent is started in full allocation mode, the disk-io thread (see threads_)
will make sure that the entire storage is allocated, and fill any gaps with zeros.
This will be skipped if the filesystem supports sparse files or automatic zero filling.
It will of course still check for existing pieces and fast resume data. The main
drawbacks of this mode are:

 * It may take longer to start the torrent, since it will need to fill the files
   with zeros on some systems. This delay is linearly dependent on the size of
   the download.

 * The download may occupy unnecessary disk space between download sessions. In case
   sparse files are not supported.

 * Disk caches usually perform extremely poorly with random access to large files
   and may slow down a download considerably.

The benefits of this mode are:

 * Downloaded pieces are written directly to their final place in the files and the
   total number of disk operations will be fewer and may also play nicer to
   filesystems' file allocation, and reduce fragmentation.

 * No risk of a download failing because of a full disk during download. Unless
   sparse files are being used.

 * The fast resume data will be more likely to be usable, regardless of crashes or
   out of date data, since pieces won't move around.

 * Can be used with the filter files feature.

compact allocation
------------------

The compact allocation will only allocate as much storage as it needs to keep the
pieces downloaded so far. This means that pieces will be moved around to be placed
at their final position in the files while downloading (to make sure the completed
download has all its pieces in the correct place). So, the main drawbacks are:

 * More disk operations while downloading since pieces are moved around.

 * Potentially more fragmentation in the filesystem.

 * Cannot be used while filtering files.

The benefits though, are:

 * No startup delay, since the files doesn't need allocating.

 * The download will not use unnecessary disk space.

 * Disk caches perform much better than in full allocation and raises the download
   speed limit imposed by the disk.

 * Works well on filesystems that doesn't support sparse files.

The algorithm that is used when allocating pieces and slots isn't very complicated.
For the interested, a description follows.

storing a piece:

1. let **A** be a newly downloaded piece, with index **n**.
2. let **s** be the number of slots allocated in the file we're
   downloading to. (the number of pieces it has room for).
3. if **n** >= **s** then allocate a new slot and put the piece there.
4. if **n** < **s** then allocate a new slot, move the data at
   slot **n** to the new slot and put **A** in slot **n**.

allocating a new slot:

1. if there's an unassigned slot (a slot that doesn't
   contain any piece), return that slot index.
2. append the new slot at the end of the file (or find an unused slot).
3. let **i** be the index of newly allocated slot
4. if we have downloaded piece index **i** already (to slot **j**) then

   1. move the data at slot **j** to slot **i**.
   2. return slot index **j** as the newly allocated free slot.

5. return **i** as the newly allocated slot.
                              
 
extensions
==========

These extensions all operates within the `extension protocol`__. The
name of the extension is the name used in the extension-list packets,
and the payload is the data in the extended message (not counting the
length-prefix, message-id nor extension-id).

__ extension_protocol.html

Note that since this protocol relies on one of the reserved bits in the
handshake, it may be incompatible with future versions of the mainline
bittorrent client.

These are the extensions that are currently implemented.

metadata from peers
-------------------

Extension name: "LT_metadata"

The point with this extension is that you don't have to distribute the
metadata (.torrent-file) separately. The metadata can be distributed
through the bittorrent swarm. The only thing you need to download such
a torrent is the tracker url and the info-hash of the torrent.

It works by assuming that the initial seeder has the metadata and that
the metadata will propagate through the network as more peers join.

There are three kinds of messages in the metadata extension. These packets
are put as payload to the extension message. The three packets are:

	* request metadata
	* metadata
	* don't have metadata

request metadata:

+-----------+---------------+----------------------------------------+
| size      | name          | description                            |
+===========+===============+========================================+
| uint8_t   | msg_type      | Determines the kind of message this is |
|           |               | 0 means 'request metadata'             |
+-----------+---------------+----------------------------------------+
| uint8_t   | start         | The start of the metadata block that   |
|           |               | is requested. It is given in 256:ths   |
|           |               | of the total size of the metadata,     |
|           |               | since the requesting client don't know |
|           |               | the size of the metadata.              |
+-----------+---------------+----------------------------------------+
| uint8_t   | size          | The size of the metadata block that is |
|           |               | requested. This is also given in       |
|           |               | 256:ths of the total size of the       |
|           |               | metadata. The size is given as size-1. |
|           |               | That means that if this field is set   |
|           |               | 0, the request wants one 256:th of the |
|           |               | metadata.                              |
+-----------+---------------+----------------------------------------+

metadata:

+-----------+---------------+----------------------------------------+
| size      | name          | description                            |
+===========+===============+========================================+
| uint8_t   | msg_type      | 1 means 'metadata'                     |
+-----------+---------------+----------------------------------------+
| int32_t   | total_size    | The total size of the metadata, given  |
|           |               | in number of bytes.                    |
+-----------+---------------+----------------------------------------+
| int32_t   | offset        | The offset of where the metadata block |
|           |               | in this message belongs in the final   |
|           |               | metadata. This is given in bytes.      |
+-----------+---------------+----------------------------------------+
| uint8_t[] | metadata      | The actual metadata block. The size of |
|           |               | this part is given implicit by the     |
|           |               | length prefix in the bittorrent        |
|           |               | protocol packet.                       |
+-----------+---------------+----------------------------------------+

Don't have metadata:

+-----------+---------------+----------------------------------------+
| size      | name          | description                            |
+===========+===============+========================================+
| uint8_t   | msg_type      | 2 means 'I don't have metadata'.       |
|           |               | This message is sent as a reply to a   |
|           |               | metadata request if the the client     |
|           |               | doesn't have any metadata.             |
+-----------+---------------+----------------------------------------+

HTTP seeding
------------

The HTTP seed extension implements `this specification`__.

The libtorrent implementation assumes that, if the URL ends with a slash
('/'), the filename should be appended to it in order to request pieces from
that file. The way this works is that if the torrent is a single-file torrent,
only that filename is appended. If the torrent is a multi-file torrent, the
torrent's name '/' the file name is appended. This is the same directory
structure that libtorrent will download torrents into.

__ http://www.getright.com/seedtorrent.html


filename checks
===============

Boost.Filesystem will by default check all its paths to make sure they conform
to filename requirements on many platforms. If you don't want this check, you can
set it to either only check for native filesystem requirements or turn it off
altogether. You can use::

	boost::filesystem::path::default_name_check(boost::filesystem::native);

for example. For more information, see the `Boost.Filesystem docs`__.

__ http://www.boost.org/libs/filesystem/doc/index.htm


acknowledgments
===============

Written by Arvid Norberg. Copyright |copy| 2003-2006

Contributions by Magnus Jonsson, Daniel Wallin and Cory Nelson

Lots of testing, suggestions and contributions by Massaroddel and Tianhao Qiu.

Big thanks to Michael Wojciechowski and Peter Koeleman for making the autotools
scripts.

Thanks to Reimond Retz for bugfixes, suggestions and testing

Thanks to `University of Umeå`__ for providing development and test hardware.

Project is hosted by sourceforge.

.. raw: html
	
	<a href="http://sourceforge.net"><img src="http://sourceforge.net/sflogo.php?group_id=7994"/></a>

.. |copy| unicode:: 0xA9 .. copyright sign
__ http://www.cs.umu.se

