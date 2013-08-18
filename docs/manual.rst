============================
libtorrent API Documentation
============================

:Author: Arvid Norberg, arvid@rasterbar.com
:Version: 0.16.11

.. contents:: Table of contents
  :depth: 1
  :backlinks: none

overview
========

The interface of libtorrent consists of a few classes. The main class is
the ``session``, it contains the main loop that serves all torrents.

The basic usage is as follows:

* construct a session
* load session state from settings file (see `load_state() save_state()`_)
* start extensions (see `add_extension()`_).
* start DHT, LSD, UPnP, NAT-PMP etc (see `start_dht() stop_dht() set_dht_settings() dht_state() is_dht_running()`_
  `start_lsd() stop_lsd()`_, `start_upnp() stop_upnp()`_ and `start_natpmp() stop_natpmp()`_)
* parse .torrent-files and add them to the session (see `bdecode() bencode()`_ and `async_add_torrent() add_torrent()`_)
* main loop (see session_)

	* query the torrent_handles for progress (see torrent_handle_)
	* query the session for information
	* add and remove torrents from the session at run-time

* save resume data for all torrent_handles (optional, see
  `save_resume_data()`_)
* save session state (see `load_state() save_state()`_)
* destruct session object

Each class and function is described in this manual.

For a description on how to create torrent files, see make_torrent_.

.. _make_torrent: make_torrent.html

things to keep in mind
======================

A common problem developers are facing is torrents stopping without explanation.
Here is a description on which conditions libtorrent will stop your torrents,
how to find out about it and what to do about it.

Make sure to keep track of the paused state, the error state and the upload
mode of your torrents. By default, torrents are auto-managed, which means
libtorrent will pause them, unpause them, scrape them and take them out
of upload-mode automatically.

Whenever a torrent encounters a fatal error, it will be stopped, and the
``torrent_status::error`` will describe the error that caused it. If a torrent
is auto managed, it is scraped periodically and paused or resumed based on
the number of downloaders per seed. This will effectively seed torrents that
are in the greatest need of seeds.

If a torrent hits a disk write error, it will be put into upload mode. This
means it will not download anything, but only upload. The assumption is that
the write error is caused by a full disk or write permission errors. If the
torrent is auto-managed, it will periodically be taken out of the upload
mode, trying to write things to the disk again. This means torrent will recover
from certain disk errors if the problem is resolved. If the torrent is not
auto managed, you have to call `set_upload_mode()`_ to turn
downloading back on again.

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
			, int flags = start_default_features
				| add_default_plugins
			, int alert_mask = alert::error_notification);

		session(
			fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = 0
			, int flags = start_default_features 
				| add_default_plugins
			, int alert_mask = alert::error_notification);

		enum save_state_flags_t
		{
			save_settings = 0x001,
			save_dht_settings = 0x002,
			save_dht_state = 0x004,
			save_proxy = 0x008,
			save_i2p_proxy = 0x010,
			save_encryption_settings = 0x020,
			save_as_map = 0x040,
			save_feeds = 0x080,
		};

		void load_state(lazy_entry const& e);
		void save_state(entry& e, boost::uint32_t flags) const;

		torrent_handle add_torrent(
			add_torrent_params const& params);
		torrent_handle add_torrent(
			add_torrent_params const& params
			, error_code& ec);

		void async_add_torrent(add_torrent_params const& params);

		void pause();
		void resume();

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

		void remove_torrent(torrent_handle const& h
			, int options = none);
		torrent_handle find_torrent(sha_hash const& ih);

		std::vector<torrent_handle> get_torrents() const;
		void get_torrent_status(std::vector<torrent_status>* ret
			, boost::function<bool(torrent_status const&)> const& pred
			, boost::uint32_t flags = 0) const;
		void refresh_torrent_status(std::vector<torrent_status>* ret
			, boost::uint32_t flags) const;

		void set_settings(session_settings const& settings);
		session_settings settings() const;
		void set_pe_settings(pe_settings const& settings);

		void set_proxy(proxy_settings const& s);
		proxy_settings proxy() const;

		int num_uploads() const;
		int num_connections() const;

		void load_asnum_db(char const* file);
		void load_asnum_db(wchar_t const* file);
		void load_country_db(char const* file);
		void load_country_db(wchar_t const* file);
		int as_for_ip(address const& adr);

		void set_ip_filter(ip_filter const& f);
		ip_filter get_ip_filter() const;

		session_status status() const;
		cache_status get_cache_status() const;

		bool is_listening() const;
		unsigned short listen_port() const;

		enum { 
			listen_reuse_address = 1,
			listen_no_system_port = 2
		};

		void listen_on(
			std::pair<int, int> const& port_range
			, error_code& ec
			, char const* interface = 0
			, int flags = 0);

		std::auto_ptr<alert> pop_alert();
		alert const* wait_for_alert(time_duration max_wait);
		void set_alert_mask(int m);
		size_t set_alert_queue_size_limit(
			size_t queue_size_limit_);
		void set_alert_dispatch(boost::function<void(std::auto_ptr<alert>)> const& fun);

		feed_handle add_feed(feed_settings const& feed);
		void remove_feed(feed_handle h);
		void get_feeds(std::vector<feed_handle>& f) const;

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
		bool is_dht_running() const;

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
			, int flags = start_default_features
				| add_default_plugins
			, int alert_mask = alert::error_notification);

		session(fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = 0
			, int flags = start_default_features
				| add_default_plugins
			, int alert_mask = alert::error_notification);

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

The ``alert_mask`` is the same mask that you would send to `set_alert_mask()`_.

~session()
----------

The destructor of session will notify all trackers that our torrents have been shut down.
If some trackers are down, they will time out. All this before the destructor of session
returns. So, it's advised that any kind of interface (such as windows) are closed before
destructing the session object. Because it can take a few second for it to finish. The
timeout can be set with ``set_settings()``.

load_state() save_state()
-------------------------

	::

		void load_state(lazy_entry const& e);
		void save_state(entry& e, boost::uint32_t flags) const;

loads and saves all session settings, including dht_settings, encryption settings and proxy
settings. ``save_state`` writes all keys to the ``entry`` that's passed in, which needs to
either not be initialized, or initialized as a dictionary.

``load_state`` expects a ``lazy_entry`` which can be built from a bencoded buffer with
`lazy_bdecode()`_.

The ``flags`` arguments passed in to ``save_state`` can be used to filter which parts
of the session state to save. By default, all state is saved (except for the individual
torrents). These are the possible flags. A flag that's set, means those settings are saved::

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
	};


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


async_add_torrent() add_torrent()
---------------------------------

	::

		typedef boost::function<storage_interface*(file_storage const&
			, file_storage const*, std::string const&, file_pool&
			, std::vector<boost::uint8_t> const&) storage_constructor_type;

		struct add_torrent_params
		{
			add_torrent_params(storage_constructor_type s);

			enum flags_t
			{
				flag_seed_mode = 0x001,
				flag_override_resume_data = 0x002,
				flag_upload_mode = 0x004,
				flag_share_mode = 0x008,
				flag_apply_ip_filter = 0x010,
				flag_paused = 0x020,
				flag_auto_managed = 0x040.
				flag_duplicate_is_error = 0x080,
				flag_merge_resume_trackers = 0x100,
				flag_update_subscribe = 0x200
			};

			int version;
			boost::intrusive_ptr<torrent_info> ti;
		#ifndef TORRENT_NO_DEPRECATE
			char const* tracker_url;
		#endif
			std::vector<std::string> trackers;
			std::vector<std::pair<std::string, int> > dht_nodes;
			sha1_hash info_hash;
			std::string name;
			std::string save_path;
			std::vector<char>* resume_data;
			storage_mode_t storage_mode;
			storage_constructor_type storage;
			void* userdata;
			std::vector<boost::uint8_t> const* file_priorities;
			std::string trackerid;
			std::string url;
			std::string uuid;
			std::string source_feed_url;
			boost::uint64_t flags;
		};

		torrent_handle add_torrent(add_torrent_params const& params);
		torrent_handle add_torrent(add_torrent_params const& params
			, error_code& ec);
		void async_add_torrent(add_torrent_params const& params);

You add torrents through the ``add_torrent()`` function where you give an
object with all the parameters. The ``add_torrent()`` overloads will block
until the torrent has been added (or failed to be added) and returns an
error code and a ``torrent_handle``. In order to add torrents more efficiently,
consider using ``async_add_torrent()`` which returns immediately, without
waiting for the torrent to add. Notification of the torrent being added is sent
as add_torrent_alert_.

The overload that does not take an ``error_code`` throws an exception on
error and is not available when building without exception support.

The only mandatory parameters are ``save_path`` which is the directory where you
want the files to be saved. You also need to specify either the ``ti`` (the
torrent file), the ``info_hash`` (the info hash of the torrent) or the ``url``
(the URL to where to download the .torrent file from). If you specify the
info-hash, the torrent file will be downloaded from peers, which requires them to
support the metadata extension. For the metadata extension to work, libtorrent must
be built with extensions enabled (``TORRENT_DISABLE_EXTENSIONS`` must not be
defined). It also takes an optional ``name`` argument. This may be left empty in case no
name should be assigned to the torrent. In case it's not, the name is used for
the torrent as long as it doesn't have metadata. See ``torrent_handle::name``.

If the torrent doesn't have a tracker, but relies on the DHT to find peers, the
``trackers`` (or the deprecated ``tracker_url``) can specify tracker urls that
for the torrent.

If you specify a ``url``, the torrent will be set in ``downloading_metadata`` state
until the .torrent file has been downloaded. If there's any error while downloading,
the torrent will be stopped and the torrent error state (``torrent_status::error``)
will indicate what went wrong. The ``url`` may refer to a magnet link or a regular
http URL.

``dht_nodes`` is a list of hostname and port pairs, representing DHT nodes to be
added to the session (if DHT is enabled). The hostname may be an IP address.

If the torrent you are trying to add already exists in the session (is either queued
for checking, being checked or downloading) ``add_torrent()`` will throw
libtorrent_exception_ which derives from ``std::exception`` unless ``duplicate_is_error``
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
	All pieces will be written to their final position, all files will be
	allocated in full when the torrent is first started. This is done with
	``fallocate()`` and similar calls. This mode minimizes fragmentation.

storage_mode_compact
	**this mode is deprecated and will be removed in future versions of libtorrent**
	The storage will grow as more pieces are downloaded, and pieces
	are rearranged to finally be in their correct places once the entire torrent has been
	downloaded.

For more information, see `storage allocation`_.

``storage`` can be used to customize how the data is stored. The default
storage will simply write the data to the files it belongs to, but it could be
overridden to save everything to a single file at a specific location or encrypt the
content on disk for instance. For more information about the ``storage_interface``
that needs to be implemented for a custom storage, see `storage_interface`_.

The ``userdata`` parameter is optional and will be passed on to the extension
constructor functions, if any (see `add_extension()`_).

The torrent_handle_ returned by ``add_torrent()`` can be used to retrieve information
about the torrent's progress, its peers etc. It is also used to abort a torrent.

``file_priorities`` can be set to control the initial file priorities when adding
a torrent. The semantics are the same as for ``torrent_handle::prioritize_files()``.

``version`` is filled in by the constructor and should be left untouched. It
is used for forward binary compatibility.

``trackerid`` is the default tracker id to be used when announcing to trackers. By default
this is empty, and no tracker ID is used, since this is an optional argument. If
a tracker returns a tracker ID, that ID is used instead of this.

if ``uuid`` is specified, it is used to find duplicates. If another torrent is already
running with the same UUID as the one being added, it will be considered a duplicate. This
is mainly useful for RSS feed items which has UUIDs specified.

``source_feed_url`` should point to the URL of the RSS feed this torrent comes from,
if it comes from an RSS feed.

``flags`` is a 64 bit integer used for flags controlling aspects of this torrent
and how it's added. These are the flags::

	enum flags_t
	{
		flag_seed_mode = 0x001,
		flag_override_resume_data = 0x002,
		flag_upload_mode = 0x004,
		flag_share_mode = 0x008,
		flag_apply_ip_filter = 0x010,
		flag_paused = 0x020,
		flag_auto_managed = 0x040.
		flag_duplicate_is_error = 0x080,
		flag_merge_resume_trackers = 0x100,
		flag_update_subscribe = 0x200
	}

``flag_apply_ip_filter`` determines if the IP filter should apply to this torrent or not. By
default all torrents are subject to filtering by the IP filter (i.e. this flag is set by
default). This is useful if certain torrents needs to be excempt for some reason, being
an auto-update torrent for instance.

``flag_merge_resume_trackers`` defaults to off and specifies whether tracker URLs loaded from
resume data should be added to the trackers in the torrent or replace the trackers.

``flag_update_subscribe`` is on by default and means that this torrent will be part of state
updates when calling `post_torrent_updates()`_.

``flag_paused`` specifies whether or not the torrent is to be started in a paused
state. I.e. it won't connect to the tracker or any of the peers until it's
resumed. This is typically a good way of avoiding race conditions when setting
configuration options on torrents before starting them.

If you pass in resume data, the paused state of the torrent when the resume data
was saved will override the paused state you pass in here. You can override this
by setting ``flag_override_resume_data``.

If the torrent is auto-managed (``flag_auto_managed``), the torrent may be resumed
at any point, regardless of how it paused. If it's important to manually control
when the torrent is paused and resumed, don't make it auto managed.

If ``flag_auto_managed`` is set, the torrent will be queued, started and seeded
automatically by libtorrent. When this is set, the torrent should also be started
as paused. The default queue order is the order the torrents were added. They
are all downloaded in that order. For more details, see queuing_.

If you pass in resume data, the auto_managed state of the torrent when the resume data
was saved will override the auto_managed state you pass in here. You can override this
by setting ``override_resume_data``.

If ``flag_seed_mode`` is set, libtorrent will assume that all files are present
for this torrent and that they all match the hashes in the torrent file. Each time
a peer requests to download a block, the piece is verified against the hash, unless
it has been verified already. If a hash fails, the torrent will automatically leave
the seed mode and recheck all the files. The use case for this mode is if a torrent
is created and seeded, or if the user already know that the files are complete, this
is a way to avoid the initial file checks, and significantly reduce the startup time.

Setting ``flag_seed_mode`` on a torrent without metadata (a .torrent file) is a no-op
and will be ignored.

If resume data is passed in with this torrent, the seed mode saved in there will
override the seed mode you set here.

If ``flag_override_resume_data`` is set, the ``paused`` and ``auto_managed``
state of the torrent are not loaded from the resume data, but the states requested
by the flags in ``add_torrent_params`` will override them.

If ``flag_upload_mode`` is set, the torrent will be initialized in upload-mode,
which means it will not make any piece requests. This state is typically entered
on disk I/O errors, and if the torrent is also auto managed, it will be taken out
of this state periodically. This mode can be used to avoid race conditions when
adjusting priorities of pieces before allowing the torrent to start downloading.

If the torrent is auto-managed (``flag_auto_managed``), the torrent will eventually
be taken out of upload-mode, regardless of how it got there. If it's important to
manually control when the torrent leaves upload mode, don't make it auto managed.

``flag_share_mode`` determines if the torrent should be added in *share mode* or not.
Share mode indicates that we are not interested in downloading the torrent, but
merlely want to improve our share ratio (i.e. increase it). A torrent started in
share mode will do its best to never download more than it uploads to the swarm.
If the swarm does not have enough demand for upload capacity, the torrent will
not download anything. This mode is intended to be safe to add any number of torrents
to, without manual screening, without the risk of downloading more than is uploaded.

A torrent in share mode sets the priority to all pieces to 0, except for the pieces
that are downloaded, when pieces are decided to be downloaded. This affects the progress
bar, which might be set to "100% finished" most of the time. Do not change file or piece
priorities for torrents in share mode, it will make it not work.

The share mode has one setting, the share ratio target, see ``session_settings::share_mode_target``
for more info.


remove_torrent()
----------------

	::

		void remove_torrent(torrent_handle const& h, int options = none);

``remove_torrent()`` will close all peer connections associated with the torrent and tell
the tracker that we've stopped participating in the swarm. The optional second argument
``options`` can be used to delete all the files downloaded by this torrent. To do this, pass
in the value ``session::delete_files``. The removal of the torrent is asyncronous, there is
no guarantee that adding the same torrent immediately after it was removed will not throw
a libtorrent_exception_ exception. Once the torrent is deleted, a torrent_deleted_alert_
is posted.

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

get_torrent_status() refresh_torrent_status()
---------------------------------------------

	::

		void get_torrent_status(std::vector<torrent_status>* ret
			, boost::function<bool(torrent_status const&)> const& pred
			, boost::uint32_t flags = 0) const;
		void refresh_torrent_status(std::vector<torrent_status>* ret
			, boost::uint32_t flags = 0) const;

.. note::
	these calls are potentially expensive and won't scale well
	with lots of torrents. If you're concerned about performance, consider
	using ``post_torrent_updates()`` instead.

``get_torrent_status`` returns a vector of the ``torrent_status`` for every
torrent which satisfies ``pred``, which is a predicate function which determines
if a torrent should be included in the returned set or not. Returning true means
it should be included and false means excluded. The ``flags`` argument is the same
as to ``torrent_handle::status()``. Since ``pred`` is guaranteed to be called for
every torrent, it may be used to count the number of torrents of different categories
as well.

``refresh_torrent_status`` takes a vector of ``torrent_status`` structs (for instance
the same vector that was returned by ``get_torrent_status()``) and refreshes the
status based on the ``handle`` member. It is possible to use this function by
first setting up a vector of default constructed ``torrent_status`` objects, only
initializing the ``handle`` member, in order to request the torrent status for
multiple torrents in a single call. This can save a significant amount of time
if you have a lot of torrents.

Any ``torrent_status`` object whose ``handle`` member is not referring to a
valid torrent are ignored.

post_torrent_updates()
----------------------

	::

		void post_torrent_updates();

This functions instructs the session to post the state_update_alert_, containing
the status of all torrents whose state changed since the last time this function
was called.

Only torrents who has the state subscription flag set will be included. This flag
is on by default. See ``add_torrent_params`` under `async_add_torrent() add_torrent()`_.


load_asnum_db() load_country_db() as_for_ip()
---------------------------------------------

	::

		void load_asnum_db(char const* file);
		void load_asnum_db(wchar_t const* file);
		void load_country_db(char const* file);
		void load_country_db(wchar_t const* file);
		int as_for_ip(address const& adr);

These functions are not available if ``TORRENT_DISABLE_GEO_IP`` is defined. They
expects a path to the `MaxMind ASN database`_ and `MaxMind GeoIP database`_
respectively. This will be used to look up which AS and country peers belong to.

``as_for_ip`` returns the AS number for the IP address specified. If the IP is not
in the database or the ASN database is not loaded, 0 is returned.

The ``wchar_t`` overloads are for wide character paths.

.. _`MaxMind ASN database`: http://www.maxmind.com/app/asnum
.. _`MaxMind GeoIP database`: http://www.maxmind.com/app/geolitecountry
		
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

get_ip_filter()
---------------

	::

		ip_filter get_ip_filter() const;
		
Returns the ip_filter currently in the session. See ip_filter_.


status()
--------

	::

		session_status status() const;

``status()`` returns session wide-statistics and status. The ``session_status``
struct has the following members::

	struct dht_lookup
	{
		char const* type;
		int outstanding_requests;
		int timeouts;
		int responses;
		int branch_factor;
		int nodes_left;
		int last_sent;
		int first_timeout;
	};

	struct dht_routing_bucket
	{
		int num_nodes;
		int num_replacements;
		int last_active;
	};

	struct utp_status
	{
		int num_idle;
		int num_syn_sent;
		int num_connected;
		int num_fin_sent;
		int num_close_wait;
	};

	struct session_status
	{
		bool has_incoming_connections;

		int upload_rate;
		int download_rate;
		size_type total_download;
		size_type total_upload;

		int payload_upload_rate;
		int payload_download_rate;
		size_type total_payload_download;
		size_type total_payload_upload;

		int ip_overhead_upload_rate;
		int ip_overhead_download_rate;
		size_type total_ip_overhead_download;
		size_type total_ip_overhead_upload;

		int dht_upload_rate;
		int dht_download_rate;
		size_type total_dht_download;
		size_type total_dht_upload;

		int tracker_upload_rate;
		int tracker_download_rate;
		size_type total_tracker_download;
		size_type total_tracker_upload;

		size_type total_redundant_bytes;
		size_type total_failed_bytes;

		int num_peers;
		int num_unchoked;
		int allowed_upload_slots;

		int up_bandwidth_queue;
		int down_bandwidth_queue;

		int up_bandwidth_bytes_queue;
		int down_bandwidth_bytes_queue;

		int optimistic_unchoke_counter;
		int unchoke_counter;

		int disk_write_queue;
		int disk_read_queue;

		int dht_nodes;
		int dht_node_cache;
		int dht_torrents;
		size_type dht_global_nodes;
		std::vector<dht_lookup> active_requests;
		std::vector<dht_routing_table> dht_routing_table;
		int dht_total_allocations;

		utp_status utp_stats;
	};

``has_incoming_connections`` is false as long as no incoming connections have been
established on the listening socket. Every time you change the listen port, this will
be reset to false.

``upload_rate``, ``download_rate`` are the total download and upload rates accumulated
from all torrents. This includes bittorrent protocol, DHT and an estimated TCP/IP
protocol overhead.

``total_download`` and ``total_upload`` are the total number of bytes downloaded and
uploaded to and from all torrents. This also includes all the protocol overhead.

``payload_download_rate`` and ``payload_upload_rate`` is the rate of the payload
down- and upload only.

``total_payload_download`` and ``total_payload_upload`` is the total transfers of payload
only. The payload does not include the bittorrent protocol overhead, but only parts of the
actual files to be downloaded.

``ip_overhead_upload_rate``, ``ip_overhead_download_rate``, ``total_ip_overhead_download``
and ``total_ip_overhead_upload`` is the estimated TCP/IP overhead in each direction.

``dht_upload_rate``, ``dht_download_rate``, ``total_dht_download`` and ``total_dht_upload``
is the DHT bandwidth usage.

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

``up_bandwidth_queue`` and ``down_bandwidth_queue`` are the number of peers that are
waiting for more bandwidth quota from the torrent rate limiter.
``up_bandwidth_bytes_queue`` and ``down_bandwidth_bytes_queue`` count the number of
bytes the connections are waiting for to be able to send and receive.

``optimistic_unchoke_counter`` and ``unchoke_counter`` tells the number of
seconds until the next optimistic unchoke change and the start of the next
unchoke interval. These numbers may be reset prematurely if a peer that is
unchoked disconnects or becomes notinterested.

``disk_write_queue`` and ``disk_read_queue`` are the number of peers currently
waiting on a disk write or disk read to complete before it receives or sends
any more data on the socket. It'a a metric of how disk bound you are.

``dht_nodes``, ``dht_node_cache`` and ``dht_torrents`` are only available when
built with DHT support. They are all set to 0 if the DHT isn't running. When
the DHT is running, ``dht_nodes`` is set to the number of nodes in the routing
table. This number only includes *active* nodes, not cache nodes. The
``dht_node_cache`` is set to the number of nodes in the node cache. These nodes
are used to replace the regular nodes in the routing table in case any of them
becomes unresponsive.

``dht_torrents`` are the number of torrents tracked by the DHT at the moment.

``dht_global_nodes`` is an estimation of the total number of nodes in the DHT
network.

``active_requests`` is a vector of the currently running DHT lookups.

``dht_routing_table`` contains information about every bucket in the DHT routing
table.

``dht_total_allocations`` is the number of nodes allocated dynamically for a
particular DHT lookup. This represents roughly the amount of memory used
by the DHT.

``utp_stats`` contains statistics on the uTP sockets.

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
			int total_used_buffers;
			int average_queue_time;
			int average_read_time;
			int average_write_time;
			int average_hash_time;
			int average_cache_time;
			int job_queue_length;
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

``total_used_buffers`` is the total number of buffers currently in use.
This includes the read/write disk cache as well as send and receive buffers
used in peer connections.

``average_queue_time`` is the number of microseconds an average disk I/O job
has to wait in the job queue before it get processed.

``average_read_time`` is the time read jobs takes on average to complete
(not including the time in the queue), in microseconds. This only measures
read cache misses. 

``average_write_time`` is the time write jobs takes to complete, on average,
in microseconds. This does not include the time the job sits in the disk job
queue or in the write cache, only blocks that are flushed to disk.

``average_hash_time`` is the time hash jobs takes to complete on average, in
microseconds. Hash jobs include running SHA-1 on the data (which for the most
part is done incrementally) and sometimes reading back parts of the piece. It
also includes checking files without valid resume data.

``average_cache_time`` is the average amuount of time spent evicting cached
blocks that have expired from the disk cache.

``job_queue_length`` is the number of jobs in the job queue.

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

		enum { 
			listen_reuse_address = 1,
			listen_no_system_port = 2
		};

		void listen_on(
			std::pair<int, int> const& port_range
			, error_code& ec
			, char const* interface = 0
			, int flags = 0);

``is_listening()`` will tell you whether or not the session has successfully
opened a listening port. If it hasn't, this function will return false, and
then you can use ``listen_on()`` to make another attempt.

``listen_port()`` returns the port we ended up listening on. Since you just pass
a port-range to the constructor and to ``listen_on()``, to know which port it
ended up using, you have to ask the session using this function.

``listen_on()`` will change the listen port and/or the listen interface. If the
session is already listening on a port, this socket will be closed and a new socket
will be opened with these new settings. The port range is the ports it will try
to listen on, if the first port fails, it will continue trying the next port within
the range and so on. The interface parameter can be left as 0, in that case the
os will decide which interface to listen on, otherwise it should be the ip-address
of the interface you want the listener socket bound to. ``listen_on()`` returns the
error code of the operation in ``ec``. If this indicates success, the session is
listening on a port within the specified range. If it fails, it will also
generate an appropriate alert (listen_failed_alert_).

If all ports in the specified range fails to be opened for listening, libtorrent will
try to use port 0 (which tells the operating system to pick a port that's free). If
that still fails you may see a listen_failed_alert_ with port 0 even if you didn't
ask to listen on it.

It is possible to prevent libtorrent from binding to port 0 by passing in the flag
``session::no_system_port`` in the ``flags`` argument.

If you don't specify an interface, libtorrent may attempt to
listen on multiple interfaces (typically 0.0.0.0 and ::). This means that if your
IPv6 interface doesn't work, you may still see a listen_failed_alert_, even though
the IPv4 port succeeded.

The ``flags`` parameter can either be 0 or ``session::listen_reuse_address``, which
will set the reuse address socket option on the listen socket(s). By default, the
listen socket does not use reuse address. If you're running a service that needs
to run on a specific port no matter if it's in use, set this flag.

If you're also starting the DHT, it is a good idea to do that after you've called
``listen_on()``, since the default listen port for the DHT is the same as the tcp
listen socket. If you start the DHT first, it will assume the tcp port is free and
open the udp socket on that port, then later, when ``listen_on()`` is called, it
may turn out that the tcp port is in use. That results in the DHT and the bittorrent
socket listening on different ports. If the DHT is active when ``listen_on`` is
called, the udp port will be rebound to the new port, if it was configured to use
the same port as the tcp socket, and if the listen_on call failed to bind to the
same port that the udp uses.

If you want the OS to pick a port for you, pass in 0 as both first and second.

The reason why it's a good idea to run the DHT and the bittorrent socket on the same
port is because that is an assumption that may be used to increase performance. One
way to accelerate the connecting of peers on windows may be to first ping all peers
with a DHT ping packet, and connect to those that responds first. On windows one
can only connect to a few peers at a time because of a built in limitation (in XP
Service pack 2).

set_alert_mask()
----------------

	::

		void set_alert_mask(int m);

Changes the mask of which alerts to receive. By default only errors are reported.
``m`` is a bitmask where each bit represents a category of alerts.

See alerts_ for mor information on the alert categories.

pop_alerts() pop_alert() wait_for_alert()
-----------------------------------------

	::

		std::auto_ptr<alert> pop_alert();
		void pop_alerts(std::deque<alert*>* alerts);
		alert const* wait_for_alert(time_duration max_wait);

``pop_alert()`` is used to ask the session if any errors or events has occurred. With
`set_alert_mask()`_ you can filter which alerts to receive through ``pop_alert()``.
For information about the alert categories, see alerts_.

``pop_alerts()`` pops all pending alerts in a single call. In high performance environments
with a very high alert churn rate, this can save significant amount of time compared to
popping alerts one at a time. Each call requires one round-trip to the network thread. If
alerts are produced in a higher rate than they can be popped (when popped one at a time)
it's easy to get stuck in an infinite loop, trying to drain the alert queue. Popping the entire
queue at once avoids this problem.

However, the ``pop_alerts`` function comes with significantly more responsibility. You pass
in an *empty* ``std::dequeue<alert*>`` to it. If it's not empty, all elements in it will
be deleted and then cleared. All currently pending alerts are returned by being swapped
into the passed in container. The responsibility of deleting the alerts is transferred
to the caller. This means you need to call delete for each item in the returned dequeue.
It's probably a good idea to delete the alerts as you handle them, to save one extra
pass over the dequeue.

Alternatively, you can pass in the same container the next time you call ``pop_alerts``.

``wait_for_alert`` blocks until an alert is available, or for no more than ``max_wait``
time. If ``wait_for_alert`` returns because of the time-out, and no alerts are available,
it returns 0. If at least one alert was generated, a pointer to that alert is returned.
The alert is not popped, any subsequent calls to ``wait_for_alert`` will return the
same pointer until the alert is popped by calling ``pop_alert``. This is useful for
leaving any alert dispatching mechanism independent of this blocking call, the dispatcher
can be called and it can pop the alert independently.

In the python binding, ``wait_for_alert`` takes the number of milliseconds to wait as an integer.

To control the max number of alerts that's queued by the session, see
``session_settings::alert_queue_size``.

``save_resume_data_alert`` and ``save_resume_data_failed_alert`` are always posted, regardelss
of the alert mask.

set_alert_dispatch()
--------------------

	::

		void set_alert_dispatch(boost::function<void(std::auto_ptr<alert>)> const& fun);

This sets a function to be called (from within libtorrent's netowrk thread) every time an alert
is posted. Since the function (``fun``) is run in libtorrent's internal thread, it may not call
any of libtorrent's external API functions. Doing so results in a dead lock.

The main intention with this function is to support integration with platform-dependent message
queues or signalling systems. For instance, on windows, one could post a message to an HNWD or
on linux, write to a pipe or an eventfd.


add_feed()
----------

	::

		feed_handle add_feed(feed_settings const& feed);

This adds an RSS feed to the session. The feed will be refreshed
regularly and optionally add all torrents from the feed, as they
appear. The feed is defined by the ``feed_settings`` object::

	struct feed_settings
	{
		feed_settings();
	
   	std::string url;
		bool auto_download;
		bool auto_map_handles;
		int default_ttl;
		add_torrent_params add_args;
	};

By default ``auto_download`` is true, which means all torrents in
the feed will be downloaded. Set this to false in order to manually
add torrents to the session. You may react to the rss_alert_ when
a feed has been updated to poll it for the new items in the feed
when adding torrents manually. When torrents are added automatically,
an add_torrent_alert_ is posted which includes the torrent handle
as well as the error code if it failed to be added. You may also call
``session::get_torrents()`` to get the handles to the new torrents.

Before adding the feed, you must set the ``url`` field to the
feed's url. It may point to an RSS or an atom feed.

``auto_map_handles`` defaults to true and determines whether or
not to set the ``handle`` field in the ``feed_item``, returned
as the feed status. If auto-download is enabled, this setting
is ignored. If auto-download is not set, setting this to false
will save one pass through all the feed items trying to find
corresponding torrents in the session.

The ``default_ttl`` is the default interval for refreshing a feed.
This may be overridden by the feed itself (by specifying the ``<ttl>``
tag) and defaults to 30 minutes. The field specifies the number of
minutes between refreshes.

If torrents are added automatically, you may want to set the
``add_args`` to appropriate values for download directory etc.
This object is used as a template for adding torrents from feeds,
but some torrent specific fields will be overridden by the
individual torrent being added. For more information on the
``add_torrent_params``, see `async_add_torrent() add_torrent()`_.

The returned feed_handle_ is a handle which is used to interact
with the feed, things like forcing a refresh or querying for
information about the items in the feed. For more information,
see feed_handle_.


remove_feed()
-------------

	::

		void remove_feed(feed_handle h);

Removes a feed from being watched by the session. When this
call returns, the feed handle is invalid and won't refer
to any feed.


get_feeds()
-----------

	::

		void get_feeds(std::vector<feed_handle>& f) const;

Returns a list of all RSS feeds that are being watched by the session.


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


set_proxy() proxy()
-------------------

	::

		void set_proxy(proxy_settings const& s);
		proxy_setting proxy() const;

These functions sets and queries the proxy settings to be used for the session.

For more information on what settings are available for proxies, see
`proxy_settings`_.


set_i2p_proxy() i2p_proxy()
---------------------------

	::

		void set_i2p_proxy(proxy_settings const&);
		proxy_settings const& i2p_proxy();

``set_i2p_proxy`` sets the i2p_ proxy, and tries to open a persistant
connection to it. The only used fields in the proxy settings structs
are ``hostname`` and ``port``.

``i2p_proxy`` returns the current i2p proxy in use.

.. _i2p: http://www.i2p2.de


start_dht() stop_dht() set_dht_settings() dht_state() is_dht_running()
----------------------------------------------------------------------

	::

		void start_dht(entry const& startup_state);
		void stop_dht();
		void set_dht_settings(dht_settings const& settings);
		entry dht_state() const;
		bool is_dht_running() const;

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
		int max_fail_count;
		int max_torrents;
		bool restrict_routing_ips;
		bool restrict_search_ips;
	};

``max_peers_reply`` is the maximum number of peers the node will send in
response to a ``get_peers`` message from another node.

``search_branching`` is the number of concurrent search request the node will
send when announcing and refreshing the routing table. This parameter is
called alpha in the kademlia paper.

``max_fail_count`` is the maximum number of failed tries to contact a node
before it is removed from the routing table. If there are known working nodes
that are ready to replace a failing node, it will be replaced immediately,
this limit is only used to clear out nodes that don't have any node that can
replace them.

``max_torrents`` is the total number of torrents to track from the DHT. This
is simply an upper limit to make sure malicious DHT nodes cannot make us allocate
an unbounded amount of memory.

``max_feed_items`` is the total number of feed items to store from the DHT. This
is simply an upper limit to make sure malicious DHT nodes cannot make us allocate
an unbounded amount of memory.

``restrict_routing_ips`` determines if the routing table entries should restrict
entries to one per IP. This defaults to true, which helps mitigate some attacks
on the DHT. It prevents adding multiple nodes with IPs with a very close CIDR
distance.

``restrict_search_ips`` determines if DHT searches should prevent adding nodes
with IPs with very close CIDR distance. This also defaults to true and helps
mitigate certain attacks on the DHT.

The ``dht_settings`` struct used to contain a ``service_port`` member to control
which port the DHT would listen on and send messages from. This field is deprecated
and ignored. libtorrent always tries to open the UDP socket on the same port
as the TCP socket.

``is_dht_running()`` returns true if the DHT support has been started and false
otherwise.


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
type you request, the accessor will throw libtorrent_exception_ (which derives from
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

		// these constructors throws exceptions on error
		torrent_info(sha1_hash const& info_hash, int flags = 0);
		torrent_info(lazy_entry const& torrent_file, int flags = 0);
		torrent_info(char const* buffer, int size, int flags = 0);
		torrent_info(std::string const& filename, int flags = 0);
		torrent_info(std::wstring const& filename, int flags = 0);

		// these constructors sets the error code on error
		torrent_info(sha1_hash const& info_hash, error_code& ec, int flags = 0);
		torrent_info(lazy_entry const& torrent_file, error_code& ec, int flags = 0);
		torrent_info(char const* buffer, int size, error_code& ec, int flags = 0);
		torrent_info(fs::path const& filename, error_code& ec, int flags = 0);
		torrent_info(fs::wpath const& filename, error_code& ec, int flags = 0);

		void add_tracker(std::string const& url, int tier = 0);
		std::vector<announce_entry> const& trackers() const;

		file_storage const& files() const;
		file_storage const& orig_files() const;

		void remap_files(file_storage const& f);

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

		void add_url_seed(std::string const& url);
		void add_http_seed(std::string const& url);
		std::vector<web_seed_entry> const& web_seeds() const;

		size_type total_size() const;
		int piece_length() const;
		int num_pieces() const;
		sha1_hash const& info_hash() const;
		std::string const& name() const;
		std::string const& comment() const;
		std::string const& creator() const;

		std::vector<std::pair<std::string, int> > const& nodes() const;
		void add_node(std::pair<std::string, int> const& node);

		boost::optional<time_t> creation_date() const;

		int piece_size(unsigned int index) const;
		sha1_hash const& hash_for_piece(unsigned int index) const;
		char const* hash_for_piece_ptr(unsigned int index) const;

		std::vector<sha1_hash> const& merkle_tree() const;
		void set_merkle_tree(std::vector<sha1_hash>& h);

		boost::shared_array<char> metadata() const;
		int metadata_size() const;
	};

torrent_info()
--------------
   
	::

		torrent_info(sha1_hash const& info_hash, int flags = 0);
		torrent_info(lazy_entry const& torrent_file, int flags = 0);
		torrent_info(char const* buffer, int size, int flags = 0);
		torrent_info(std::string const& filename, int flags = 0);
		torrent_info(std::wstring const& filename, int flags = 0);

		torrent_info(sha1_hash const& info_hash, error_code& ec, int flags = 0);
		torrent_info(lazy_entry const& torrent_file, error_code& ec, int flags = 0);
		torrent_info(char const* buffer, int size, error_code& ec, int flags = 0);
		torrent_info(fs::path const& filename, error_code& ec, int flags = 0);
		torrent_info(fs::wpath const& filename, error_code& ec, int flags = 0);

The constructor that takes an info-hash  will initialize the info-hash to the given value,
but leave all other fields empty. This is used internally when downloading torrents without
the metadata. The metadata will be created by libtorrent as soon as it has been downloaded
from the swarm.

The constructor that takes a ``lazy_entry`` will create a ``torrent_info`` object from the
information found in the given torrent_file. The ``lazy_entry`` represents a tree node in
an bencoded file. To load an ordinary .torrent file
into a ``lazy_entry``, use `lazy_bdecode()`_.

The version that takes a buffer pointer and a size will decode it as a .torrent file and
initialize the torrent_info object for you.

The version that takes a filename will simply load the torrent file and decode it inside
the constructor, for convenience. This might not be the most suitable for applications that
want to be able to report detailed errors on what might go wrong.

The overloads that takes an ``error_code const&`` never throws if an error occur, they
will simply set the error code to describe what went wrong and not fully initialize the
torrent_info object. The overloads that do not take the extra error_code_ parameter will
always throw if an error occurs. These overloads are not available when building without
exception support.

The ``flags`` argument is currently unused.


add_tracker()
-------------

	::

		void add_tracker(std::string const& url, int tier = 0);

``add_tracker()`` adds a tracker to the announce-list. The ``tier`` determines the order in
which the trackers are to be tried. For more information see `trackers()`_.

files() orig_files()
--------------------

	::

		file_storage const& files() const;
		file_storage const& orig_files() const;

The ``file_storage`` object contains the information on how to map the pieces to
files. It is separated from the ``torrent_info`` object because when creating torrents
a storage object needs to be created without having a torrent file. When renaming files
in a storage, the storage needs to make its own copy of the ``file_storage`` in order
to make its mapping differ from the one in the torrent file.

``orig_files()`` returns the original (unmodified) file storage for this torrent. This
is used by the web server connection, which needs to request files with the original
names. Filename may be chaged using ``torrent_info::rename_file()``.

For more information on the ``file_storage`` object, see the separate document on how
to create torrents.

remap_files()
-------------

	::

		void remap_files(file_storage const& f);

Remaps the file storage to a new file layout. This can be used to, for instance,
download all data in a torrent to a single file, or to a number of fixed size
sector aligned files, regardless of the number and sizes of the files in the torrent.

The new specified ``file_storage`` must have the exact same size as the current one.

rename_file()
-------------

	::

		void rename_file(int index, std::string const& new_filename);
		void rename_file(int index, std::wstring const& new_filename);

Renames a the file with the specified index to the new name. The new filename is
reflected by the ``file_storage`` returned by ``files()`` but not by the one
returned by ``orig_files()``.

If you want to rename the base name of the torrent (for a multifile torrent), you
can copy the ``file_storage`` (see `files() orig_files()`_), change the name, and
then use `remap_files()`_.


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
iterators with the type ``internal_file_entry``, which is an internal type.

You can resolve it into the public representation of a file (``file_entry``)
using the ``file_storage::at`` function, which takes an index and an iterator;

::

	struct file_entry
	{
		std::string path;
		size_type offset;
		size_type size;
		size_type file_base;
		time_t mtime;
		sha1_hash filehash;
		bool pad_file:1;
		bool hidden_attribute:1;
		bool executable_attribute:1;
		bool symlink_attribute:1;
	};

The ``path`` is the full path of this file. The paths are unicode strings
encoded in UTF-8.

``size`` is the size of the file (in bytes) and ``offset`` is the byte offset
of the file within the torrent. i.e. the sum of all the sizes of the files
before it in the list.

``file_base`` is the offset in the file where the storage should start. The normal
case is to have this set to 0, so that the storage starts saving data at the start
if the file. In cases where multiple files are mapped into the same file though,
the ``file_base`` should be set to an offset so that the different regions do
not overlap. This is used when mapping "unselected" files into a so-called part
file.

``mtime`` is the modification time of this file specified in posix time.

``symlink_path`` is the path which this is a symlink to, or empty if this is
not a symlink. This field is only used if the ``symlink_attribute`` is set.

``filehash`` is a sha-1 hash of the content of the file, or zeroes, if no
file hash was present in the torrent file. It can be used to potentially
find alternative sources for the file.

``pad_file`` is set to true for files that are not part of the data of the torrent.
They are just there to make sure the next file is aligned to a particular byte offset
or piece boundry. These files should typically be hidden from an end user. They are
not written to disk.

``hidden_attribute`` is true if the file was marked as hidden (on windows).

``executable_attribute`` is true if the file was marked as executable (posix)

``symlink_attribute`` is true if the file was a symlink. If this is the case
the ``symlink_index`` refers to a string which specifies the original location
where the data for this file was found.

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


add_url_seed() add_http_seed()
------------------------------

	::

		void add_url_seed(std::string const& url
			, std::string const& extern_auth = std::string()
			, web_seed_entry::headers_t const& extra_headers = web_seed_entry::headers_t());
		void add_http_seed(std::string const& url
			, std::string const& extern_auth = std::string()
			, web_seed_entry::headers_t const& extra_headers = web_seed_entry::headers_t());
		std::vector<web_seed_entry> const& web_seeds() const;

``web_seeds()`` returns all url seeds and http seeds in the torrent. Each entry
is a ``web_seed_entry`` and may refer to either a url seed or http seed.
		
``add_url_seed()`` and ``add_http_seed()`` adds one url to the list of
url/http seeds. Currently, the only transport protocol supported for the url
is http.

The ``extern_auth`` argument can be used for other athorization schemese than
basic HTTP authorization. If set, it will override any username and password
found in the URL itself. The string will be sent as the HTTP authorization header's
value (without specifying "Basic").

The ``extra_headers`` argument defaults to an empty list, but can be used to
insert custom HTTP headers in the requests to a specific web seed.

See `HTTP seeding`_ for more information.

The ``web_seed_entry`` has the following members::

	struct web_seed_entry
	{
		enum type_t { url_seed, http_seed };

		typedef std::vector<std::pair<std::string, std::string> > headers_t;

		web_seed_entry(std::string const& url_, type_t type_
			, std::string const& auth_ = std::string()
			, headers_t const& extra_headers_ = headers_t());

		bool operator==(web_seed_entry const& e) const;
		bool operator<(web_seed_entry const& e) const;

		std::string url;
		type_t type;
		std::string auth;
		headers_t extra_headers;

		// ...
	};


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

		int next_announce_in() const;
		int min_announce_in() const;

		error_code last_error;

		std::string message;

		boost::uint8_t tier;
		boost::uint8_t fail_limit;
		boost::uint8_t fails;

		enum tracker_source
		{
			source_torrent = 1,
			source_client = 2,
			source_magnet_link = 4,
			source_tex = 8
		};
		boost::uint8_t source;

		bool verified:1;
		bool updating:1;
		bool start_sent:1;
		bool complete_sent:1;
	};

``next_announce_in()`` returns the number of seconds to the next announce on
this tracker. ``min_announce_in()`` returns the number of seconds until we are
allowed to force another tracker update with this tracker.

If the last time this tracker was contacted failed, ``last_error`` is the error
code describing what error occurred.

If the last time this tracker was contacted, the tracker returned a warning
or error message, ``message`` contains that message.

``fail_limit`` is the max number of failures to announce to this tracker in
a row, before this tracker is not used anymore.

``fails`` is the number of times in a row we have failed to announce to this
tracker.

``source`` is a bitmask specifying which sources we got this tracker from.

``verified`` is set to true the first time we receive a valid response
from this tracker.

``updating`` is true while we're waiting for a response from the tracker.

``start_sent`` is set to true when we get a valid response from an announce
with event=started. If it is set, we won't send start in the subsequent
announces.

``complete_sent`` is set to true when we send a event=completed.


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

merkle_tree() set_merkle_tree()
-------------------------------

	::

		std::vector<sha1_hash> const& merkle_tree() const;
		void set_merkle_tree(std::vector<sha1_hash>& h);

``merkle_tree()`` returns a reference to the merkle tree for this torrent, if any.

``set_merkle_tree()`` moves the passed in merkle tree into the torrent_info object.
i.e. ``h`` will not be identical after the call. You need to set the merkle tree for
a torrent that you've just created (as a merkle torrent). The merkle tree is retrieved
from the ``create_torrent::merkle_tree()`` function, and need to be saved separately
from the torrent file itself. Once it's added to libtorrent, the merkle tree will be
persisted in the resume data.


name() comment() creation_date() creator()
------------------------------------------

	::

		std::string const& name() const;
		std::string const& comment() const;
		std::string const& creator() const;
		boost::optional<time_t> creation_date() const;

``name()`` returns the name of the torrent.

``comment()`` returns the comment associated with the torrent. If there's no comment,
it will return an empty string. ``creation_date()`` returns the creation date of
the torrent as time_t (`posix time`_). If there's no time stamp in the torrent file,
the optional object will be uninitialized.

Both the name and the comment is UTF-8 encoded strings.

``creator()`` returns the creator string in the torrent. If there is no creator string
it will return an empty string.

.. _`posix time`: http://www.opengroup.org/onlinepubs/009695399/functions/time.html

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

.. warning::
	Any member function that returns a value or fills in a value has to
	be made synchronously. This means it has to wait for the main thread
	to complete the query before it can return. This might potentially be
	expensive if done from within a GUI thread that needs to stay responsive.
	Try to avoid quering for information you don't need, and try to do it
	in as few calls as possible. You can get most of the interesting information
	about a torrent from the ``torrent_handle::status()`` call.

Its declaration looks like this::

	struct torrent_handle
	{
		torrent_handle();

		enum status_flags_t
		{
			query_distributed_copies = 1,
			query_accurate_download_counters = 2,
			query_last_seen_complete = 4,
			query_pieces = 8,
			query_verified_pieces = 16
		};

		torrent_status status(boost::uint32_t flags = 0xffffffff);
		void file_progress(std::vector<size_type>& fp, int flags = 0);
		void get_download_queue(std::vector<partial_piece_info>& queue) const;
		void get_peer_info(std::vector<peer_info>& v) const;
		torrent_info const& get_torrent_info() const;
		bool is_valid() const;

		std::string name() const;

		enum save_resume_flags_t { flush_disk_cache = 1, save_info_dict = 2 };
		void save_resume_data(int flags = 0) const;
		bool need_save_resume_data() const;
		void force_reannounce() const;
		void force_dht_announce() const;
		void force_reannounce(boost::posix_time::time_duration) const;
		void scrape_tracker() const;
		void connect_peer(asio::ip::tcp::endpoint const& adr, int source = 0) const;

		void set_tracker_login(std::string const& username
			, std::string const& password) const;

		std::vector<announce_entry> trackers() const;
		void replace_trackers(std::vector<announce_entry> const&);
		void add_tracker(announce_entry const& url);

		void add_url_seed(std::string const& url);
		void remove_url_seed(std::string const& url);
		std::set<std::string> url_seeds() const;

		void add_http_seed(std::string const& url);
		void remove_http_seed(std::string const& url);
		std::set<std::string> http_seeds() const;

		int max_uploads() const;
		void set_max_uploads(int max_uploads) const;
		void set_max_connections(int max_connections) const;
		int max_connections() const;
		void set_upload_limit(int limit) const;
		int upload_limit() const;
		void set_download_limit(int limit) const;
		int download_limit() const;
		void set_sequential_download(bool sd) const;
		bool is_sequential_download() const;

		int queue_position() const;
		void queue_position_up() const;
		void queue_position_down() const;
		void queue_position_top() const;
		void queue_position_bottom() const;

		void set_priority(int prio) const;

		void use_interface(char const* net_interface) const;

		enum pause_flags_t { graceful_pause = 1 };
		void pause(int flags = 0) const;
		void resume() const;
		bool is_seed() const;
		void force_recheck() const;
		void clear_error() const;
		void set_upload_mode(bool m) const;
		void set_share_mode(bool m) const;

		void apply_ip_filter(bool b) const;

		void flush_cache() const;

		void resolve_countries(bool r);
		bool resolve_countries() const;

		enum deadline_flags { alert_when_available = 1 };
		void set_piece_deadline(int index, int deadline, int flags = 0) const;
		void reset_piece_deadline(int index) const;

		void piece_availability(std::vector<int>& avail) const;
		void piece_priority(int index, int priority) const;
		int piece_priority(int index) const;
		void prioritize_pieces(std::vector<int> const& pieces) const;
		std::vector<int> piece_priorities() const;

		void file_priority(int index, int priority) const;
		int file_priority(int index) const;
		void prioritize_files(std::vector<int> const& files) const;
		std::vector<int> file_priorities() const;

		void auto_managed(bool m) const;

		bool set_metadata(char const* buf, int size) const;

		std::string save_path() const;
		void move_storage(std::string const& save_path) const;
		void move_storage(std::wstring const& save_path) const;
		void rename_file(int index, std::string) const;
		void rename_file(int index, std::wstring) const;
		storage_interface* get_storage_impl() const;

		void super_seeding(bool on) const;

		enum flags_t { overwrite_existing = 1 };
		void add_piece(int piece, char const* data, int flags = 0) const;
		void read_piece(int piece) const;
		bool have_piece(int piece) const;

		sha1_hash info_hash() const;

		void set_ssl_certificate(std::string const& cert
			, std::string const& private_key
			, std::string const& dh_params
			, std::string const& passphrase = "");

		bool operator==(torrent_handle const&) const;
		bool operator!=(torrent_handle const&) const;
		bool operator<(torrent_handle const&) const;

		boost::shared_ptr<torrent> native_handle() const;
	};

The default constructor will initialize the handle to an invalid state. Which
means you cannot perform any operation on it, unless you first assign it a
valid handle. If you try to perform any operation on an uninitialized handle,
it will throw ``invalid_handle``.

.. warning:: All operations on a ``torrent_handle`` may throw libtorrent_exception_
	exception, in case the handle is no longer refering to a torrent. There is
	one exception ``is_valid()`` will never throw.
	Since the torrents are processed by a background thread, there is no
	guarantee that a handle will remain valid between two calls.

set_piece_deadline() reset_piece_deadline()
-------------------------------------------

	::

		enum deadline_flags { alert_when_available = 1 };
		void set_piece_deadline(int index, int deadline, int flags = 0) const;
		void reset_piece_deadline(int index) const;

This function sets or resets the deadline associated with a specific piece
index (``index``). libtorrent will attempt to download this entire piece before
the deadline expires. This is not necessarily possible, but pieces with a more
recent deadline will always be prioritized over pieces with a deadline further
ahead in time. The deadline (and flags) of a piece can be changed by calling this
function again.

The ``flags`` parameter can be used to ask libtorrent to send an alert once the
piece has been downloaded, by passing ``alert_when_available``. When set, the
read_piece_alert_ alert will be delivered, with the piece data, when it's downloaded.

If the piece is already downloaded when this call is made, nothing happens, unless
the ``alert_when_available`` flag is set, in which case it will do the same thing
as calling `read_piece()`_ for ``index``.

``deadline`` is the number of milliseconds until this piece should be completed.

``reset_piece_deadline`` removes the deadline from the piece. If it hasn't already
been downloaded, it will no longer be considered a priority.

piece_availability()
--------------------

	::

		void piece_availability(std::vector<int>& avail) const;

Fills the specified ``std::vector<int>`` with the availability for each
piece in this torrent. libtorrent does not keep track of availability for
seeds, so if the torrent is seeding the availability for all pieces is
reported as 0.

The piece availability is the number of peers that we are connected that has
advertized having a particular piece. This is the information that libtorrent
uses in order to prefer picking rare pieces.


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

You cannot set the file priorities on a torrent that does not yet
have metadata or a torrent that is a seed. ``file_priority(int, int)`` and
``prioritize_files()`` are both no-ops for such torrents.

file_progress()
---------------

	::

		void file_progress(std::vector<size_type>& fp, int flags = 0);

This function fills in the supplied vector with the the number of bytes downloaded
of each file in this torrent. The progress values are ordered the same as the files
in the `torrent_info`_. This operation is not very cheap. Its complexity is *O(n + mj)*.
Where *n* is the number of files, *m* is the number of downloading pieces and *j*
is the number of blocks in a piece.

The ``flags`` parameter can be used to specify the granularity of the file progress. If
left at the default value of 0, the progress will be as accurate as possible, but also
more expensive to calculate. If ``torrent_handle::piece_granularity`` is specified,
the progress will be specified in piece granularity. i.e. only pieces that have been
fully downloaded and passed the hash check count. When specifying piece granularity,
the operation is a lot cheaper, since libtorrent already keeps track of this internally
and no calculation is required.


save_path()
-----------

	::

		std::string save_path() const;

``save_path()`` returns the path that was given to `async_add_torrent() add_torrent()`_ when this torrent
was started.

move_storage()
--------------

	::

		void move_storage(std::string const& save_path) const;
		void move_storage(std::wstring const& save_path) const;

Moves the file(s) that this torrent are currently seeding from or downloading to. If
the given ``save_path`` is not located on the same drive as the original save path,
The files will be copied to the new drive and removed from their original location.
This will block all other disk IO, and other torrents download and upload rates may
drop while copying the file.

Since disk IO is performed in a separate thread, this operation is also asynchronous.
Once the operation completes, the ``storage_moved_alert`` is generated, with the new
path as the message. If the move fails for some reason, ``storage_moved_failed_alert``
is generated instead, containing the error message.

rename_file()
-------------

	::

		void rename_file(int index, std::string) const;
		void rename_file(int index, std::wstring) const;

Renames the file with the given index asynchronously. The rename operation is complete
when either a ``file_renamed_alert`` or ``file_rename_failed_alert`` is posted.

get_storage_impl()
------------------

	::

		storage_interface* get_storage_impl() const;

Returns the storage implementation for this torrent. This depends on the
storage contructor function that was passed to ``session::add_torrent``.

super_seeding()
---------------

	::

		void super_seeding(bool on) const;

Enables or disabled super seeding/initial seeding for this torrent. The torrent
needs to be a seed for this to take effect.

add_piece()
-----------

	::

		enum flags_t { overwrite_existing = 1 };
		void add_piece(int piece, char const* data, int flags = 0) const;

This function will write ``data`` to the storage as piece ``piece``, as if it had
been downloaded from a peer. ``data`` is expected to point to a buffer of as many
bytes as the size of the specified piece. The data in the buffer is copied and
passed on to the disk IO thread to be written at a later point.

By default, data that's already been downloaded is not overwritten by this buffer. If
you trust this data to be correct (and pass the piece hash check) you may pass the
``overwrite_existing`` flag. This will instruct libtorrent to overwrite any data that
may already have been downloaded with this data.

Since the data is written asynchronously, you may know that is passed or failed the
hash check by waiting for ``piece_finished_alert`` or ``has_failed_alert``.

read_piece()
------------

	::

		void read_piece(int piece) const;

This function starts an asynchronous read operation of the specified piece from
this torrent. You must have completed the download of the specified piece before
calling this function.

When the read operation is completed, it is passed back through an alert,
read_piece_alert_. Since this alert is a reponse to an explicit call, it will
always be posted, regardless of the alert mask.

Note that if you read multiple pieces, the read operations are not guaranteed to
finish in the same order as you initiated them.

have_piece()
------------

	::

		bool have_piece(int piece) const;

Returns true if this piece has been completely downloaded, and false otherwise.

force_reannounce() force_dht_announce()
---------------------------------------

	::

		void force_reannounce() const;
		void force_reannounce(boost::posix_time::time_duration) const;
		void force_dht_announce() const;

``force_reannounce()`` will force this torrent to do another tracker request, to receive new
peers. The second overload of ``force_reannounce`` that takes a ``time_duration`` as
argument will schedule a reannounce in that amount of time from now.

If the tracker's ``min_interval`` has not passed since the last announce, the forced
announce will be scheduled to happen immediately as the ``min_interval`` expires. This is
to honor trackers minimum re-announce interval settings.

``force_dht_announce`` will announce the torrent to the DHT immediately.

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
will throw libtorrent_exception_. The second (optional) argument will be bitwised ORed into
the source mask of this peer. Typically this is one of the source flags in peer_info_.
i.e. ``tracker``, ``pex``, ``dht`` etc.


name()
------

	::

		std::string name() const;

Returns the name of the torrent. i.e. the name from the metadata associated with it. In
case the torrent was started without metadata, and hasn't completely received it yet,
it returns the name given to it when added to the session. See ``session::add_torrent``.


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
Note that setting a higher limit on a torrent then the global limit (``session_settings::upload_rate_limit``)
will not override the global rate limit. The torrent can never upload more than the global rate
limit.

``upload_limit`` and ``download_limit`` will return the current limit setting, for upload and
download, respectively.


set_sequential_download()
-------------------------

	::

		void set_sequential_download(bool sd);

``set_sequential_download()`` enables or disables *sequential download*. When enabled, the piece
picker will pick pieces in sequence instead of rarest first.

Enabling sequential download will affect the piece distribution negatively in the swarm. It should be
used sparingly.

pause() resume()
----------------

	::

		enum pause_flags_t { graceful_pause = 1 };
		void pause(int flags) const;
		void resume() const;

``pause()``, and ``resume()`` will disconnect all peers and reconnect all peers respectively.
When a torrent is paused, it will however remember all share ratios to all peers and remember
all potential (not connected) peers. Torrents may be paused automatically if there is a file
error (e.g. disk full) or something similar. See file_error_alert_.

To know if a torrent is paused or not, call ``torrent_handle::status()`` and inspect
``torrent_status::paused``.

The ``flags`` argument to pause can be set to ``torrent_handle::graceful_pause`` which will
delay the disconnect of peers that we're still downloading outstanding requests from. The torrent
will not accept any more requests and will disconnect all idle peers. As soon as a peer is
done transferring the blocks that were requested from it, it is disconnected. This is a graceful
shut down of the torrent in the sense that no downloaded bytes are wasted.

torrents that are auto-managed may be automatically resumed again. It does not make sense to
pause an auto-managed torrent without making it not automanaged first. Torrents are auto-managed
by default when added to the session. For more information, see queuing_.

flush_cache()
-------------

	::

		void flush_cache() const;

Instructs libtorrent to flush all the disk caches for this torrent and close all
file handles. This is done asynchronously and you will be notified that it's complete
through cache_flushed_alert_.

Note that by the time you get the alert, libtorrent may have cached more data for the
torrent, but you are guaranteed that whatever cached data libtorrent had by the time
you called ``torrent_handle::flush_cache()`` has been written to disk.

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

set_upload_mode()
-----------------

::

		void set_upload_mode(bool m) const;

Explicitly sets the upload mode of the torrent. In upload mode, the torrent will not
request any pieces. If the torrent is auto managed, it will automatically be taken out
of upload mode periodically (see ``session_settings::optimistic_disk_retry``). Torrents
are automatically put in upload mode whenever they encounter a disk write error.

``m`` should be true to enter upload mode, and false to leave it.

To test if a torrent is in upload mode, call ``torrent_handle::status()`` and inspect
``torrent_status::upload_mode``.

set_share_mode()
----------------

	::

		void set_share_mode(bool m) const;

Enable or disable share mode for this torrent. When in share mode, the torrent will
not necessarily be downloaded, especially not the whole of it. Only parts that are likely
to be distributed to more than 2 other peers are downloaded, and only if the previous
prediction was correct.

apply_ip_filter()
-----------------

::

		void apply_ip_filter(bool b) const;

Set to true to apply the session global IP filter to this torrent (which is the
default). Set to false to make this torrent ignore the IP filter.

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

auto_managed()
--------------

	::

		void auto_managed(bool m) const;

``auto_managed()`` changes whether the torrent is auto managed or not. For more info,
see queuing_.

set_metadata()
--------------

	::

		bool set_metadata(char const* buf, int size) const;

``set_metadata`` expects the *info* section of metadata. i.e. The buffer passed in will be
hashed and verified against the info-hash. If it fails, a ``metadata_failed_alert`` will be
generated. If it passes, a ``metadata_received_alert`` is generated. The function returns
true if the metadata is successfully set on the torrent, and false otherwise. If the torrent
already has metadata, this function will not affect the torrent, and false will be returned.


set_tracker_login()
-------------------

	::

		void set_tracker_login(std::string const& username
			, std::string const& password) const;

``set_tracker_login()`` sets a username and password that will be sent along in the HTTP-request
of the tracker announce. Set this if the tracker requires authorization.


trackers() replace_trackers() add_tracker()
-------------------------------------------

  ::

		std::vector<announce_entry> trackers() const;
		void replace_trackers(std::vector<announce_entry> const&) const;
		void add_tracker(announc_entry const& url);

``trackers()`` will return the list of trackers for this torrent. The
announce entry contains both a string ``url`` which specify the announce url
for the tracker as well as an int ``tier``, which is specifies the order in
which this tracker is tried. If you want libtorrent to use another list of
trackers for this torrent, you can use ``replace_trackers()`` which takes
a list of the same form as the one returned from ``trackers()`` and will
replace it. If you want an immediate effect, you have to call
`force_reannounce() force_dht_announce()`_. See `trackers()`_ for the definition of ``announce_entry``.

``add_tracker()`` will look if the specified tracker is already in the set.
If it is, it doesn't do anything. If it's not in the current set of trackers,
it will insert it in the tier specified in the announce_entry.

The updated set of trackers will be saved in the resume data, and when a torrent
is started with resume data, the trackers from the resume data will replace the
original ones.


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

add_http_seed() remove_http_seed() http_seeds()
-----------------------------------------------

	::

		void add_http_seed(std::string const& url);
		void remove_http_seed(std::string const& url);
		std::set<std::string> http_seeds() const;

These functions are identical as the ``*_url_seed()`` variants, but they
operate on BEP 17 web seeds instead of BEP 19.

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

The queue position is also available in the ``torrent_status``.

The ``queue_position_*()`` functions adjust the torrents position in the queue. Up means
closer to the front and down means closer to the back of the queue. Top and bottom refers
to the front and the back of the queue respectively.

set_priority()
--------------

	::

		void set_priority(int prio) const;

This sets the bandwidth priority of this torrent. The priority of a torrent determines
how much bandwidth its peers are assigned when distributing upload and download rate quotas.
A high number gives more bandwidth. The priority must be within the range [0, 255].

The default priority is 0, which is the lowest priority.

To query the priority of a torrent, use the ``torrent_handle::status()`` call.

Torrents with higher priority will not nececcarily get as much bandwidth as they can
consume, even if there's is more quota. Other peers will still be weighed in when
bandwidth is being distributed. With other words, bandwidth is not distributed strictly
in order of priority, but the priority is used as a weight.

Peers whose Torrent has a higher priority will take precedence when distributing unchoke slots.
This is a strict prioritization where every interested peer on a high priority torrent will
be unchoked before any other, lower priority, torrents have any peers unchoked.

use_interface()
---------------

	::

		void use_interface(char const* net_interface) const;

``use_interface()`` sets the network interface this torrent will use when it opens outgoing
connections. By default, it uses the same interface as the session_ uses to listen on. The
parameter must be a string containing one or more, comma separated, ip-address (either an
IPv4 or IPv6 address). When specifying multiple interfaces, the torrent will round-robin
which interface to use for each outgoing conneciton. This is useful for clients that are
multi-homed.


info_hash()
-----------

	::

		sha1_hash info_hash() const;

``info_hash()`` returns the info-hash for the torrent.


set_max_uploads() max_uploads()
-------------------------------

	::

		void set_max_uploads(int max_uploads) const;
		int max_uploads() const;

``set_max_uploads()`` sets the maximum number of peers that's unchoked at the same time on this
torrent. If you set this to -1, there will be no limit. This defaults to infinite. The primary
setting controlling this is the global unchoke slots limit, set by ``unchoke_slots_limit``
in session_settings_.

``max_uploads()`` returns the current settings.


set_max_connections() max_connections()
---------------------------------------

	::

		void set_max_connections(int max_connections) const;
		int max_connections() const;

``set_max_connections()`` sets the maximum number of connection this torrent will open. If all
connections are used up, incoming connections may be refused or poor connections may be closed.
This must be at least 2. The default is unlimited number of connections. If -1 is given to the
function, it means unlimited. There is also a global limit of the number of connections, set
by ``connections_limit`` in session_settings_.

``max_connections()`` returns the current settings.


save_resume_data()
------------------

	::

		enum save_resume_flags_t { flush_disk_cache = 1, save_info_dict = 2 };
		void save_resume_data(int flags = 0) const;

``save_resume_data()`` generates fast-resume data and returns it as an entry_. This entry_
is suitable for being bencoded. For more information about how fast-resume works, see `fast resume`_.

The ``flags`` argument is a bitmask of flags ORed together. If the flag ``torrent_handle::flush_cache``
is set, the disk cache will be flushed before creating the resume data. This avoids a problem with
file timestamps in the resume data in case the cache hasn't been flushed yet.

If the flag ``torrent_handle::save_info_dict`` is set, the resume data will contain the metadata
from the torrent file as well. This is default for any torrent that's added without a torrent
file (such as a magnet link or a URL).

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

.. warning:: The resume data contains the modification timestamps for all files. If one file has
	been modified when the torrent is added again, the will be rechecked. When shutting down, make
	sure to flush the disk cache before saving the resume data. This will make sure that the file
	timestamps are up to date and won't be modified after saving the resume data. The recommended way
	to do this is to pause the torrent, which will flush the cache and disconnect all peers.

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

	extern int outstanding_resume_data; // global counter of outstanding resume data
	std::vector<torrent_handle> handles = ses.get_torrents();
	ses.pause();
	for (std::vector<torrent_handle>::iterator i = handles.begin();
		i != handles.end(); ++i)
	{
		torrent_handle& h = *i;
		if (!h.is_valid()) continue;
		torrent_status s = h.status();
		if (!s.has_metadata) continue;
		if (!s.need_save_resume_data()) continue;

		h.save_resume_data();
		++outstanding_resume_data;
	}

	while (outstanding_resume_data > 0)
	{
		alert const* a = ses.wait_for_alert(seconds(10));

		// if we don't get an alert within 10 seconds, abort
		if (a == 0) break;
		
		std::auto_ptr<alert> holder = ses.pop_alert();

		if (alert_cast<save_resume_data_failed_alert>(a))
		{
			process_alert(a);
			--outstanding_resume_data;
			continue;
		}

		save_resume_data_alert const* rd = alert_cast<save_resume_data_alert>(a);
		if (rd == 0)
		{
			process_alert(a);
			continue;
		}
		
		torrent_handle h = rd->handle;
		std::ofstream out((h.save_path() + "/" + h.get_torrent_info().name() + ".fastresume").c_str()
			, std::ios_base::binary);
		out.unsetf(std::ios_base::skipws);
		bencode(std::ostream_iterator<char>(out), *rd->resume_data);
		--outstanding_resume_data;
	}

.. note:: Note how ``outstanding_resume_data`` is a global counter in this example.
	This is deliberate, otherwise there is a race condition for torrents that
	was just asked to save their resume data, they posted the alert, but it has
	not been received yet. Those torrents would report that they don't need to
	save resume data again, and skipped by the initial loop, and thwart the counter
	otherwise.
	

need_save_resume_data()
-----------------------

	::

		bool need_save_resume_data() const;

This function returns true if any whole chunk has been downloaded since the
torrent was first loaded or since the last time the resume data was saved. When
saving resume data periodically, it makes sense to skip any torrent which hasn't
downloaded anything since the last time.

.. note:: A torrent's resume data is considered saved as soon as the alert
	is posted. It is important to make sure this alert is received and handled
	in order for this function to be meaningful.


status()
--------

	::

		torrent_status status(boost::uint32_t flags = 0xffffffff) const;

``status()`` will return a structure with information about the status of this
torrent. If the torrent_handle_ is invalid, it will throw libtorrent_exception_ exception.
See torrent_status_. The ``flags`` argument filters what information is returned
in the torrent_status. Some information in there is relatively expensive to calculate, and
if you're not interested in it (and see performance issues), you can filter them out.

By default everything is included. The flags you can use to decide what to *include* are:

* ``query_distributed_copies``
	calculates ``distributed_copies``, ``distributed_full_copies`` and ``distributed_fraction``.

* ``query_accurate_download_counters``
	includes partial downloaded blocks in ``total_done`` and ``total_wanted_done``.

* ``query_last_seen_complete``
	includes ``last_seen_complete``.

* ``query_pieces``
	includes ``pieces``.

* ``query_verified_pieces``
	includes ``verified_pieces`` (only applies to torrents in *seed mode*).


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
		enum state_t { none, slow, medium, fast };
		state_t piece_state;
		block_info* blocks;
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

		void set_peer(tcp::endpoint const& ep);
		tcp::endpoint peer() const;

		unsigned bytes_progress:15;
		unsigned block_size:15;
		unsigned state:2;
		unsigned num_peers:14;
	};


The ``blocks`` field points to an array of ``blocks_in_piece`` elements. This pointer is
only valid until the next call to ``get_download_queue()`` for any torrent in the same session.
They all share the storaga for the block arrays in their session object.

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
``bytes_progress`` is the number of bytes that have been received for this block, and
``block_size`` is the total number of bytes in this block.

get_peer_info()
---------------

	::

		void get_peer_info(std::vector<peer_info>&) const;

``get_peer_info()`` takes a reference to a vector that will be cleared and filled
with one entry for each peer connected to this torrent, given the handle is valid. If the
torrent_handle_ is invalid, it will throw libtorrent_exception_ exception. Each entry in
the vector contains information about that particular peer. See peer_info_.


get_torrent_info()
------------------

	::

		torrent_info const& get_torrent_info() const;

Returns a const reference to the torrent_info_ object associated with this torrent.
This reference is valid as long as the torrent_handle_ is valid, no longer. If the
torrent_handle_ is invalid or if it doesn't have any metadata, libtorrent_exception_
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

set_ssl_certificate()
---------------------

	::

		void set_ssl_certificate(std::string const& cert, std::string const& private_key
			, std::string const& dh_params, std::string const& passphrase = "");

For SSL torrents, use this to specify a path to a .pem file to use as this client's certificate.
The certificate must be signed by the certificate in the .torrent file to be valid.

``cert`` is a path to the (signed) certificate in .pem format corresponding to this torrent.

``private_key`` is a path to the private key for the specified certificate. This must be in .pem
format.

``dh_params`` is a path to the Diffie-Hellman parameter file, which needs to be in .pem format.
You can generate this file using the openssl command like this:
``openssl dhparam -outform PEM -out dhparams.pem 512``.

``passphrase`` may be specified if the private key is encrypted and requires a passphrase to
be decrypted.

Note that when a torrent first starts up, and it needs a certificate, it will suspend connecting
to any peers until it has one. It's typically desirable to resume the torrent after setting the
ssl certificate.

If you receive a torrent_need_cert_alert_, you need to call this to provide a valid cert. If you
don't have a cert you won't be allowed to connect to any peers.

native_handle()
---------------

	::

		boost::shared_ptr<torrent> native_handle() const;

This function is intended only for use by plugins and the alert dispatch function. Any code
that runs in libtorrent's network thread may not use the public API of ``torrent_handle``.
Doing so results in a dead-lock. For such routines, the ``native_handle`` gives access to the
underlying type representing the torrent. This type does not have a stable API and should
be relied on as little as possible.


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
			allocating,
			checking_resume_data
		};

		torrent_handle handle;
	
		state_t state;
		bool paused;
		bool auto_managed;
		bool sequential_download;
		bool seeding;
		bool finished;
		float progress;
		int progress_ppm;
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

		int download_rate;
		int upload_rate;

		int download_payload_rate;
		int upload_payload_rate;

		int num_peers;

		int num_complete;
		int num_incomplete;

		int list_seeds;
		int list_peers;

		int connect_candidates;

		bitfield pieces;
		bitfield verified_pieces;

		int num_pieces;

		size_type total_done;
		size_type total_wanted_done;
		size_type total_wanted;

		int num_seeds;

		int distributed_full_copies;
		int distributed_fraction;

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
		int finished_time;
		int seeding_time;

		int seed_rank;

		int last_scrape;

		bool has_incoming;

		int sparse_regions;

		bool seed_mode;
		bool upload_mode;
		bool share_mode;
		bool super_seeding;

		int priority;

		time_t added_time;
		time_t completed_time;
		time_t last_seen_complete;

		int time_since_upload;
		int time_since_download;

		int queue_position;
		bool need_save_resume;
		bool ip_filter_applies;

		sha1_hash info_hash;

		int listen_port;
	};

``handle`` is a handle to the torrent whose status the object represents.

``progress`` is a value in the range [0, 1], that represents the progress of the
torrent's current task. It may be checking files or downloading.

``progress_ppm`` reflects the same value as ``progress``, but instead in a range
[0, 1000000] (ppm = parts per million). When floating point operations are disabled,
this is the only alternative to the floating point value in ``progress``.

The torrent's current task is in the ``state`` member, it will be one of the following:

+--------------------------+----------------------------------------------------------+
|``checking_resume_data``  |The torrent is currently checking the fastresume data and |
|                          |comparing it to the files on disk. This is typically      |
|                          |completed in a fraction of a second, but if you add a     |
|                          |large number of torrents at once, they will queue up.     |
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


When downloading, the progress is ``total_wanted_done`` / ``total_wanted``. This takes
into account files whose priority have been set to 0. They are not considered.

``paused`` is set to true if the torrent is paused and false otherwise. It's only true
if the torrent itself is paused. If the torrent is not running because the session is
paused, this is still false. To know if a torrent is active or not, you need to inspect
both ``torrent_status::paused`` and ``session::is_paused()``.

``auto_managed`` is set to true if the torrent is auto managed, i.e. libtorrent is
responsible for determining whether it should be started or queued. For more info
see queuing_

``sequential_download`` is true when the torrent is in sequential download mode. In
this mode pieces are downloaded in order rather than rarest first.

``is_seeding`` is true if all pieces have been downloaded.

``is_finished`` is true if all pieces that have a priority > 0 are downloaded. There is
only a distinction between finished and seeding if some pieces or files have been
set to priority 0, i.e. are not downloaded.

``has_metadata`` is true if this torrent has metadata (either it was started from a
.torrent file or the metadata has been downloaded). The only scenario where this can be
false is when the torrent was started torrent-less (i.e. with just an info-hash and tracker
ip, a magnet link for instance). Note that if the torrent doesn't have metadata, the member
`get_torrent_info()`_ will throw.

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

``verified_pieces`` is a bitmask representing which pieces has had their hash
checked. This only applies to torrents in *seed mode*. If the torrent is not
in seed mode, this bitmask may be empty.

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
have priority 0 (i.e. not wanted).

``total_wanted`` is the total number of bytes we want to download. This is also
excluding pieces whose priorities have been set to 0.

``num_seeds`` is the number of peers that are seeding that this client is
currently connected to.

``distributed_full_copies`` is the number of distributed copies of the torrent.
Note that one copy may be spread out among many peers. It tells how many copies
there are currently of the rarest piece(s) among the peers this client is
connected to.

``distributed_fraction`` tells the share of pieces that have more copies than
the rarest piece(s). Divide this number by 1000 to get the fraction.

For example, if ``distributed_full_copies`` is 2 and ``distrbuted_fraction``
is 500, it means that the rarest pieces have only 2 copies among the peers
this torrent is connected to, and that 50% of all the pieces have more than
two copies.

If we are a seed, the piece picker is deallocated as an optimization, and
piece availability is no longer tracked. In this case the distributed
copies members are set to -1.

``distributed_copies`` is a floating point representation of the
``distributed_full_copies`` as the integer part and ``distributed_fraction``
/ 1000 as the fraction part. If floating point operations are disabled
this value is always -1.

``block_size`` is the size of a block, in bytes. A block is a sub piece, it
is the number of bytes that each piece request asks for and the number of
bytes that each bit in the ``partial_piece_info``'s bitset represents
(see `get_download_queue()`_). This is typically 16 kB, but it may be
larger if the pieces are larger.

``num_uploads`` is the number of unchoked peers in this torrent.

``num_connections`` is the number of peer connections this torrent has, including
half-open connections that hasn't completed the bittorrent handshake yet. This is
always >= ``num_peers``.

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

``active_time``, ``finished_time`` and ``seeding_time`` are second counters.
They keep track of the number of seconds this torrent has been active (not
paused) and the number of seconds it has been active while being finished and
active while being a seed. ``seeding_time`` should be <= ``finished_time`` which
should be <= ``active_time``. They are all saved in and restored from resume data,
to keep totals across sessions.

``seed_rank`` is a rank of how important it is to seed the torrent, it is used
to determine which torrents to seed and which to queue. It is based on the peer
to seed ratio from the tracker scrape. For more information, see queuing_.

``last_scrape`` is the number of seconds since this torrent acquired scrape data.
If it has never done that, this value is -1.

``has_incoming`` is true if there has ever been an incoming connection attempt
to this torrent.'

``sparse_regions`` the number of regions of non-downloaded pieces in the
torrent. This is an interesting metric on windows vista, since there is
a limit on the number of sparse regions in a single file there.

``seed_mode`` is true if the torrent is in seed_mode. If the torrent was
started in seed mode, it will leave seed mode once all pieces have been
checked or as soon as one piece fails the hash check.

``upload_mode`` is true if the torrent is blocked from downloading. This
typically happens when a disk write operation fails. If the torrent is
auto-managed, it will periodically be taken out of this state, in the
hope that the disk condition (be it disk full or permission errors) has
been resolved. If the torrent is not auto-managed, you have to explicitly
take it out of the upload mode by calling `set_upload_mode()`_ on the
torrent_handle_.

``share_mode`` is true if the torrent is currently in share-mode, i.e.
not downloading the torrent, but just helping the swarm out.

``super_seeding`` is true if the torrent is in super seeding mode.

``added_time`` is the posix-time when this torrent was added. i.e. what
``time(NULL)`` returned at the time.

``completed_time`` is the posix-time when this torrent was finished. If
the torrent is not yet finished, this is 0.

``last_seen_complete`` is the time when we, or one of our peers, last
saw a complete copy of this torrent.

``time_since_upload`` and ``time_since_download`` are the number of
seconds since any peer last uploaded from this torrent and the last
time a downloaded piece passed the hash check, respectively.

``queue_position`` is the position this torrent has in the download
queue. If the torrent is a seed or finished, this is -1.

``need_save_resume`` is true if this torrent has unsaved changes
to its download state and statistics since the last resume data
was saved.

``ip_filter_applies`` is true if the session global IP filter applies
to this torrent. This defaults to true.

``info_hash`` is the info-hash of the torrent.

``listen_port`` is the listen port this torrent is listening on for new
connections, if the torrent has its own listen socket. Only SSL torrents
have their own listen sockets. If the torrent doesn't have one, and is
accepting connections on the single listen socket, this is 0.

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
			endgame_mode = 0x4000,
			holepunched = 0x8000,
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

		// bitmask representing socket state
		enum bw_state { bw_idle = 0, bw_limit = 1, bw_network = 2, bw_disk = 4 };

		char read_state;
		char write_state;

		asio::ip::tcp::endpoint ip;
		int up_speed;
		int down_speed;
		int payload_up_speed;
		int payload_down_speed;
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

		int num_pieces;

		int download_rate_peak;
		int upload_rate_peak;

		float progress;
		int progress_ppm;

		tcp::endpoint local_endpoint;
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
| ``endgame_mode``        | This means the last time this peer picket a piece,    |
|                         | it could not pick as many as it wanted because there  |
|                         | were not enough free ones. i.e. all pieces this peer  |
|                         | has were already requested from other peers.          |
+-------------------------+-------------------------------------------------------+
| ``holepunched``         | This flag is set if the peer was in holepunch mode    |
|                         | when the connection succeeded. This typically only    |
|                         | happens if both peers are behind a NAT and the peers  |
|                         | connect via the NAT holepunch mechanism.              |
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

``read_state`` and ``write_state`` are bitmasks indicating what state this peer
is in with regards to sending and receiving data. The states are declared in the
``bw_state`` enum and defines as follows:

+------------------------+--------------------------------------------------------+
| ``bw_idle``            | The peer is not waiting for any external events to     |
|                        | send or receive data.                                  |
|                        |                                                        |
+------------------------+--------------------------------------------------------+
| ``bw_limit``           | The peer is waiting for the rate limiter.              |
|                        |                                                        |
+------------------------+--------------------------------------------------------+
| ``bw_network``         | The peer has quota and is currently waiting for a      |
|                        | network read or write operation to complete. This is   |
|                        | the state all peers are in if there are no bandwidth   |
|                        | limits.                                                |
|                        |                                                        |
+------------------------+--------------------------------------------------------+
| ``bw_disk``            | The peer is waiting for the disk I/O thread to catch   |
|                        | up writing buffers to disk before downloading more.    |
|                        |                                                        |
+------------------------+--------------------------------------------------------+

Note that ``read_state`` and ``write_state`` are bitmasks. A peer may be waiting
on disk and on the network at the same time. ``bw_idle`` does not represent a bit,
but is simply a name for no bit being set in the bitmask.

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

``connection_type`` can currently be one of:

+---------------------------------------+-------------------------------------------------------+
| type                                  | meaning                                               |
+=======================================+=======================================================+
| ``peer_info::standard_bittorrent``    | Regular bittorrent connection over TCP                |
+---------------------------------------+-------------------------------------------------------+
| ``peer_info::bittorrent_utp``         | Bittorrent connection over uTP                        |
+---------------------------------------+-------------------------------------------------------+
| ``peer_info::web_sesed``              | HTTP connection using the `BEP 19`_ protocol          |
+---------------------------------------+-------------------------------------------------------+
| ``peer_info::http_seed``              | HTTP connection using the `BEP 17`_ protocol          |
+---------------------------------------+-------------------------------------------------------+

``remote_dl_rate`` is an estimate of the rate this peer is downloading at, in
bytes per second.

``pending_disk_bytes`` is the number of bytes this peer has pending in the
disk-io thread. Downloaded and waiting to be written to disk. This is what
is capped by ``session_settings::max_queued_disk_bytes``.

``send_quota`` and ``receive_quota`` are the number of bytes this peer has been
assigned to be allowed to send and receive until it has to request more quota
from the bandwidth manager.

``rtt`` is an estimated round trip time to this peer, in milliseconds. It is
estimated by timing the the tcp ``connect()``. It may be 0 for incoming connections.

``num_pieces`` is the number of pieces this peer has.

``download_rate_peak`` and ``upload_rate_peak`` are the highest download and upload
rates seen on this connection. They are given in bytes per second. This number is
reset to 0 on reconnect.

``progress`` is the progress of the peer in the range [0, 1]. This is always 0 when
floating point operations are diabled, instead use ``progress_ppm``.

``progress_ppm`` indicates the download progress of the peer in the range [0, 1000000]
(parts per million).

``local_endpoint`` is the IP and port pair the socket is bound to locally. i.e. the IP
address of the interface it's going out over. This may be useful for multi-homed
clients with multiple interfaces to the internet.

feed_handle
===========

The ``feed_handle`` refers to a specific RSS feed which is watched by the session.
The ``feed_item`` struct is defined in ``<libtorrent/rss.hpp>``. It has the following
functions::

	struct feed_handle
	{
		feed_handle();
		void update_feed();
		feed_status get_feed_status() const;
		void set_settings(feed_settings const& s);
		feed_settings settings() const;
	};

update_feed()
-------------

	::

		void update_feed();

Forces an update/refresh of the feed. Regular updates of the feed is managed
by libtorrent, be careful to not call this too frequently since it may
overload the RSS server.

get_feed_status()
-----------------

	::

		feed_status get_feed_status() const;

Queries the RSS feed for information, including all the items in the feed.
The ``feed_status`` object has the following fields::

	struct feed_status
	{
		std::string url;
		std::string title;
		std::string description;
		time_t last_update;
		int next_update;
		bool updating;
		std::vector<feed_item> items;
		error_code error;
		int ttl;
	};

``url`` is the URL of the feed.

``title`` is the name of the feed (as specified by the feed itself). This
may be empty if we have not recevied a response from the RSS server yet,
or if the feed does not specify a title.

``description`` is the feed description (as specified by the feed itself).
This may be empty if we have not received a response from the RSS server
yet, or if the feed does not specify a description.

``last_update`` is the posix time of the last successful response from the feed.

``next_update`` is the number of seconds, from now, when the feed will be
updated again.

``updating`` is true if the feed is currently being updated (i.e. waiting for
DNS resolution, connecting to the server or waiting for the response to the
HTTP request, or receiving the response).

``items`` is a vector of all items that we have received from the feed. See
feed_item_ for more information.

``error`` is set to the appropriate error code if the feed encountered an
error.

``ttl`` is the current refresh time (in minutes). It's either the configured
default ttl, or the ttl specified by the feed.


set_settings() settings()
-------------------------

	::

		void set_settings(feed_settings const& s);
		feed_settings settings() const;

Sets and gets settings for this feed. For more information on the
available settings, see `add_feed()`_.

feed_item
=========

The ``feed_item`` struct is defined in ``<libtorrent/rss.hpp>``.

	::

		struct feed_item
		{
			feed_item();
			std::string url;
			std::string uuid;
			std::string title;
			std::string description;
			std::string comment;
			std::string category;
			size_type size;
			torrent_handle handle;
			sha1_hash info_hash;
		};

``size`` is the total size of the content the torrent refers to, or -1
if no size was specified by the feed.

``handle`` is the handle to the torrent, if the session is already downloading
this torrent.

``info_hash`` is the info-hash of the torrent, or cleared (i.e. all zeroes) if
the feed does not specify the info-hash.

All the strings are self explanatory and may be empty if the feed does not specify
those fields.

session customization
=====================

You have some control over session configuration through the ``session_settings`` object. You
create it and fill it with your settings and then use ``session::set_settings()``
to apply them.

You have control over proxy and authorization settings and also the user-agent
that will be sent to the tracker. The user-agent will also be used to identify the
client with other peers.

presets
-------

The default values of the session settings are set for a regular bittorrent client running
on a desktop system. There are functions that can set the session settings to pre set
settings for other environments. These can be used for the basis, and should be tweaked to
fit your needs better.

::

	session_settings min_memory_usage();
	session_settings high_performance_seed();

``min_memory_usage`` returns settings that will use the minimal amount of RAM, at the
potential expense of upload and download performance. It adjusts the socket buffer sizes,
disables the disk cache, lowers the send buffer watermarks so that each connection only has
at most one block in use at any one time. It lowers the outstanding blocks send to the disk
I/O thread so that connections only have one block waiting to be flushed to disk at any given
time. It lowers the max number of peers in the peer list for torrents. It performs multiple
smaller reads when it hashes pieces, instead of reading it all into memory before hashing.

This configuration is inteded to be the starting point for embedded devices. It will
significantly reduce memory usage.

``high_performance_seed`` returns settings optimized for a seed box, serving many peers
and that doesn't do any downloading. It has a 128 MB disk cache and has a limit of 400 files
in its file pool. It support fast upload rates by allowing large send buffers.


session_settings
----------------

::

	struct session_settings
	{
		session_settings();
		int version;
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
		int optimistic_unchoke_interval;
		std::string announce_ip;
		int num_want;
		int initial_picker_threshold;
		int allowed_fast_set_size;

		enum { no_piece_suggestions = 0, suggest_read_cache = 1 };
		int suggest_mode;
		int max_queued_disk_bytes;
		int handshake_timeout;
		bool use_dht_as_fallback;
		bool free_torrent_hashes;
		bool upnp_ignore_nonrouters;
		int send_buffer_watermark;
		int send_buffer_watermark_factor;

	#ifndef TORRENT_NO_DEPRECATE
		bool auto_upload_slots;
		bool auto_upload_slots_rate_based;
	#endif

		enum choking_algorithm_t
		{
			fixed_slots_choker,
			auto_expand_choker,
			rate_based_choker,
			bittyrant_choker
		};

		int choking_algorithm;
		
		enum seed_choking_algorithm_t
		{
			round_robin,
			fastest_upload,
			anti_leech
		};

		int seed_choking_algorithm;

		bool use_parole_mode;
		int cache_size;
		int cache_buffer_chunk_size;
		int cache_expiry;
		bool use_read_cache;
		bool explicit_read_cache;
		int explicit_cache_interval;

		enum io_buffer_mode_t
		{
			enable_os_cache = 0,
			disable_os_cache_for_aligned_files = 1,
			disable_os_cache = 2
		};
		int disk_io_write_mode;
		int disk_io_read_mode;

		std::pair<int, int> outgoing_ports;
		char peer_tos;

		int active_downloads;
		int active_seeds;
		int active_dht_limit;
		int active_tracker_limit;
		int active_limit;
		bool auto_manage_prefer_seeds;
		bool dont_count_slow_torrents;
		int auto_manage_interval;
		float share_ratio_limit;
		float seed_time_ratio_limit;
		int seed_time_limit;
		int peer_turnover_interval;
		float peer_turnover;
		float peer_turnover_cutoff;
		bool close_redundant_connections;

		int auto_scrape_interval;
		int auto_scrape_min_interval;

		int max_peerlist_size;

		int min_announce_interval;

		bool prioritize_partial_pieces;
		int auto_manage_startup;

		bool rate_limit_ip_overhead;

		bool announce_to_all_trackers;
		bool announce_to_all_tiers;

		bool prefer_udp_trackers;
		bool strict_super_seeding;

		int seeding_piece_quota;

		int max_sparse_regions;

		bool lock_disk_cache;

		int max_rejects;

		int recv_socket_buffer_size;
		int send_socket_buffer_size;

		bool optimize_hashing_for_speed;

		int file_checks_delay_per_block;

		enum disk_cache_algo_t
		{ lru, largest_contiguous, avoid_readback };

		disk_cache_algo_t disk_cache_algorithm;

		int read_cache_line_size;
		int write_cache_line_size;

		int optimistic_disk_retry;
		bool disable_hash_checks;

		int max_suggest_pieces;

		bool drop_skipped_requests;

		bool low_prio_disk;
		int local_service_announce_interval;
		int dht_announce_interval;

		int udp_tracker_token_expiry;
		bool volatile_read_cache;
		bool guided_read_cache;
		bool default_cache_min_age;

		int num_optimistic_unchoke_slots;
		bool no_atime_storage;
		int default_est_reciprocation_rate;
		int increase_est_reciprocation_rate;
		int decrease_est_reciprocation_rate;
		bool incoming_starts_queued_torrents;
		bool report_true_downloaded;
		bool strict_end_game_mode;

		bool broadcast_lsd;

		bool enable_outgoing_utp;
		bool enable_incoming_utp;
		bool enable_outgoing_tcp;
		bool enable_incoming_tcp;
		int max_pex_peers;
		bool ignore_resume_timestamps;
		bool no_recheck_incomplete_resume;
		bool anonymous_mode;
		int tick_interval;
		int share_mode_target;

		int upload_rate_limit;
		int download_rate_limit;
		int local_upload_rate_limit;
		int local_download_rate_limit;
		int dht_upload_rate_limit;
		int unchoke_slots_limit;
		int half_open_limit;
		int connections_limit;

		int utp_target_delay;
		int utp_gain_factor;
		int utp_min_timeout;
		int utp_syn_resends;
		int utp_num_resends;
		int utp_connect_timeout;
		int utp_delayed_ack;
		bool utp_dynamic_sock_buf;
		int utp_loss_multiplier;

		enum bandwidth_mixed_algo_t
		{
			prefer_tcp = 0,
			peer_proportional = 1

		};
		int mixed_mode_algorithm;
		bool rate_limit_utp;

		int listen_queue_size;

		bool announce_double_nat;

		int torrent_connect_boost;
		bool seeding_outgoing_connections;

		bool no_connect_privileged_ports;
		int alert_queue_size;
		int max_metadata_size;
		bool smooth_connects;
		bool always_send_user_agent;
		bool apply_ip_filter_to_trackers;
		int read_job_every;
		bool use_disk_read_ahead;
		bool lock_files;

		int ssl_listen;

		int tracker_backoff;

		bool ban_web_seeds;
	};

``version`` is automatically set to the libtorrent version you're using
in order to be forward binary compatible. This field should not be changed.

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
are made per second. If a number < 0 is specified, it will default to
200 connections per second. If 0 is specified, it means don't make
outgoing connections at all.

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

``optimistic_unchoke_interval`` is the number of seconds between
each *optimistic* unchoke. On this timer, the currently optimistically
unchoked peer will change.

``announce_ip`` is the ip address passed along to trackers as the ``&ip=`` parameter.
If left as the default (an empty string), that parameter is omitted.

``num_want`` is the number of peers we want from each tracker request. It defines
what is sent as the ``&num_want=`` parameter to the tracker.

``initial_picker_threshold`` specifies the number of pieces we need before we
switch to rarest first picking. This defaults to 4, which means the 4 first
pieces in any torrent are picked at random, the following pieces are picked
in rarest first order.

``allowed_fast_set_size`` is the number of pieces we allow peers to download
from us without being unchoked.

``suggest_mode`` controls whether or not libtorrent will send out suggest
messages to create a bias of its peers to request certain pieces. The modes
are:

* ``no_piece_suggestsions`` which is the default and will not send out suggest
  messages.
* ``suggest_read_cache`` which will send out suggest messages for the most
  recent pieces that are in the read cache.

``max_queued_disk_bytes`` is the number maximum number of bytes, to be
written to disk, that can wait in the disk I/O thread queue. This queue
is only for waiting for the disk I/O thread to receive the job and either
write it to disk or insert it in the write cache. When this limit is reached,
the peer connections will stop reading data from their sockets, until the disk
thread catches up. Setting this too low will severly limit your download rate.

``handshake_timeout`` specifies the number of seconds we allow a peer to
delay responding to a protocol handshake. If no response is received within
this time, the connection is closed.

``use_dht_as_fallback`` determines how the DHT is used. If this is true,
the DHT will only be used for torrents where all trackers in its tracker
list has failed. Either by an explicit error message or a time out. This
is false by default, which means the DHT is used by default regardless of
if the trackers fail or not.

``free_torrent_hashes`` determines whether or not the torrent's piece hashes
are kept in memory after the torrent becomes a seed or not. If it is set to
``true`` the hashes are freed once the torrent is a seed (they're not
needed anymore since the torrent won't download anything more). If it's set
to false they are not freed. If they are freed, the torrent_info_ returned
by get_torrent_info() will return an object that may be incomplete, that
cannot be passed back to `async_add_torrent() add_torrent()`_ for instance.

``upnp_ignore_nonrouters`` indicates whether or not the UPnP implementation
should ignore any broadcast response from a device whose address is not the
configured router for this machine. i.e. it's a way to not talk to other
people's routers by mistake.

``send_buffer_watermark`` is the upper limit of the send buffer low-watermark.
if the send buffer has fewer bytes than this, we'll read another 16kB block
onto it. If set too small, upload rate capacity will suffer. If set too high,
memory will be wasted. The actual watermark may be lower than this in case
the upload rate is low, this is the upper limit.

``send_buffer_watermark_factor`` is multiplied to the peer's upload rate
to determine the low-watermark for the peer. It is specified as a percentage,
which means 100 represents a factor of 1.
The low-watermark is still clamped to not exceed the ``send_buffer_watermark``
upper limit. This defaults to 50. For high capacity connections, setting this
higher can improve upload performance and disk throughput. Setting it too
high may waste RAM and create a bias towards read jobs over write jobs.

``auto_upload_slots`` defaults to true. When true, if there is a global upload
limit set and the current upload rate is less than 90% of that, another upload
slot is opened. If the upload rate has been saturated for an extended period
of time, on upload slot is closed. The number of upload slots will never be
less than what has been set by ``session::set_max_uploads()``. To query the
current number of upload slots, see ``session_status::allowed_upload_slots``.

When ``auto_upload_slots_rate_based`` is set, and ``auto_upload_slots`` is set,
the max upload slots setting is used as a minimum number of unchoked slots.
This algorithm is designed to prevent the peer from spreading its upload
capacity too thin, but still open more slots in order to utilize the full capacity.

``choking_algorithm`` specifies which algorithm to use to determine which peers
to unchoke. This setting replaces the deprecated settings ``auto_upload_slots``
and ``auto_upload_slots_rate_based``.

The options for choking algorithms are:

* ``fixed_slots_choker`` is the traditional choker with a fixed number of unchoke
  slots (as specified by ``session::set_max_uploads()``).

* ``auto_expand_choker`` opens at least the number of slots as specified by
  ``session::set_max_uploads()`` but opens up more slots if the upload capacity
  is not saturated. This unchoker will work just like the ``fixed_slot_choker``
  if there's no global upload rate limit set.

* ``rate_based_choker`` opens up unchoke slots based on the upload rate
  achieved to peers. The more slots that are opened, the marginal upload
  rate required to open up another slot increases.

* ``bittyrant_choker`` attempts to optimize download rate by finding the
  reciprocation rate of each peer individually and prefers peers that gives
  the highest *return on investment*. It still allocates all upload capacity,
  but shuffles it around to the best peers first. For this choker to be
  efficient, you need to set a global upload rate limit
  (``session_settings::upload_rate_limit``). For more information about this
  choker, see the paper_.

.. _paper: http://bittyrant.cs.washington.edu/#papers

``seed_choking_algorithm`` controls the seeding unchoke behavior. The available
options are:

* ``round_robin`` which round-robins the peers that are unchoked when seeding. This
  distributes the upload bandwidht uniformly and fairly. It minimizes the ability
  for a peer to download everything without redistributing it.

* ``fastest_upload`` unchokes the peers we can send to the fastest. This might be
  a bit more reliable in utilizing all available capacity.

* ``anti_leech`` prioritizes peers who have just started or are just about to finish
  the download. The intention is to force peers in the middle of the download to
  trade with each other.

``use_parole_mode`` specifies if parole mode should be used. Parole mode means
that peers that participate in pieces that fail the hash check are put in a mode
where they are only allowed to download whole pieces. If the whole piece a peer
in parole mode fails the hash check, it is banned. If a peer participates in a
piece that passes the hash check, it is taken out of parole mode.

``cache_size`` is the disk write and read  cache. It is specified in units of
16 KiB blocks. Buffers that are part of a peer's send or receive buffer also
count against this limit. Send and receive buffers will never be denied to be
allocated, but they will cause the actual cached blocks to be flushed or evicted.
If this is set to -1, the cache size is automatically set to the amount
of physical RAM available in the machine divided by 8. If the amount of physical
RAM cannot be determined, it's set to 1024 (= 16 MiB).

Disk buffers are allocated using a pool allocator, the number of blocks that
are allocated at a time when the pool needs to grow can be specified in
``cache_buffer_chunk_size``. This defaults to 16 blocks. Lower numbers
saves memory at the expense of more heap allocations. It must be at least 1.

``cache_expiry`` is the number of seconds from the last cached write to a piece
in the write cache, to when it's forcefully flushed to disk. Default is 60 second.

``use_read_cache``, is set to true (default), the disk cache is also used to
cache pieces read from disk. Blocks for writing pieces takes presedence.

``explicit_read_cache`` defaults to 0. If set to something greater than 0, the
disk read cache will not be evicted by cache misses and will explicitly be
controlled based on the rarity of pieces. Rare pieces are more likely to be
cached. This would typically be used together with ``suggest_mode`` set to
``suggest_read_cache``. The value is the number of pieces to keep in the read
cache. If the actual read cache can't fit as many, it will essentially be clamped.

``explicit_cache_interval`` is the number of seconds in between each refresh of
a part of the explicit read cache. Torrents take turns in refreshing and this
is the time in between each torrent refresh. Refreshing a torrent's explicit
read cache means scanning all pieces and picking a random set of the rarest ones.
There is an affinity to pick pieces that are already in the cache, so that
subsequent refreshes only swaps in pieces that are rarer than whatever is in
the cache at the time.

``disk_io_write_mode`` and ``disk_io_read_mode`` determines how files are
opened when they're in read only mode versus read and write mode. The options
are:

	* enable_os_cache
		This is the default and files are opened normally, with the OS caching
		reads and writes.
	* disable_os_cache_for_aligned_files
		This will open files in unbuffered mode for files where every read and
		write would be sector aligned. Using aligned disk offsets is a requirement
		on some operating systems.
	* disable_os_cache
		This opens all files in unbuffered mode (if allowed by the operating system).
		Linux and Windows, for instance, require disk offsets to be sector aligned,
		and in those cases, this option is the same as ``disable_os_caches_for_aligned_files``.

One reason to disable caching is that it may help the operating system from growing
its file cache indefinitely. Since some OSes only allow aligned files to be opened
in unbuffered mode, It is recommended to make the largest file in a torrent the first
file (with offset 0) or use pad files to align all files to piece boundries.

``outgoing_ports``, if set to something other than (0, 0) is a range of ports
used to bind outgoing sockets to. This may be useful for users whose router
allows them to assign QoS classes to traffic based on its local port. It is
a range instead of a single port because of the problems with failing to reconnect
to peers if a previous socket to that peer and port is in ``TIME_WAIT`` state.

.. warning:: setting outgoing ports will limit the ability to keep multiple
	connections to the same client, even for different torrents. It is not
	recommended to change this setting. Its main purpose is to use as an
	escape hatch for cheap routers with QoS capability but can only classify
	flows based on port numbers.

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

``auto_manage_prefer_seeds`` specifies if libtorrent should prefer giving seeds
active slots or downloading torrents.  The default is ``false``.

if ``dont_count_slow_torrents`` is true, torrents without any payload transfers are
not subject to the ``active_seeds`` and ``active_downloads`` limits. This is intended
to make it more likely to utilize all available bandwidth, and avoid having torrents
that don't transfer anything block the active slots.

``active_limit`` is a hard limit on the number of active torrents. This applies even to
slow torrents.

``active_dht_limit`` is the max number of torrents to announce to the DHT. By default
this is set to 88, which is no more than one DHT announce every 10 seconds.

``active_tracker_limit`` is the max number of torrents to announce to their trackers.
By default this is 360, which is no more than one announce every 5 seconds.

``active_lsd_limit`` is the max number of torrents to announce to the local network
over the local service discovery protocol. By default this is 80, which is no more
than one announce every 5 seconds (assuming the default announce interval of 5 minutes).

You can have more torrents *active*, even though they are not announced to the DHT,
lsd or their tracker. If some peer knows about you for any reason and tries to connect,
it will still be accepted, unless the torrent is paused, which means it won't accept
any connections.

``auto_manage_interval`` is the number of seconds between the torrent queue
is updated, and rotated.

``share_ratio_limit`` is the upload / download ratio limit for considering a
seeding torrent have met the seed limit criteria. See queuing_.

``seed_time_ratio_limit`` is the seeding time / downloading time ratio limit
for considering a seeding torrent to have met the seed limit criteria. See queuing_.

``seed_time_limit`` is the limit on the time a torrent has been an active seed
(specified in seconds) before it is considered having met the seed limit criteria.
See queuing_.

``peer_turnover_interval`` controls a feature where libtorrent periodically can disconnect
the least useful peers in the hope of connecting to better ones. This settings controls
the interval of this optimistic disconnect. It defaults to every 5 minutes, and
is specified in seconds.

``peer_turnover`` Is the fraction of the peers that are disconnected. This is
a float where 1.f represents all peers an 0 represents no peers. It defaults to
4% (i.e. 0.04f)

``peer_turnover_cutoff`` is the cut off trigger for optimistic unchokes. If a torrent
has more than this fraction of its connection limit, the optimistic unchoke is
triggered. This defaults to 90% (i.e. 0.9f).

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
added to the list. If this limit is set to 0, there is no limit on
how many peers we'll keep in the peer list.

``max_paused_peerlist_size`` is the max peer list size used for torrents
that are paused. This default to the same as ``max_peerlist_size``, but
can be used to save memory for paused torrents, since it's not as
important for them to keep a large peer list.

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

``announce_to_all_trackers`` controls how multi tracker torrents are
treated. If this is set to true, all trackers in the same tier are
announced to in parallel. If all trackers in tier 0 fails, all trackers
in tier 1 are announced as well. If it's set to false, the behavior is as
defined by the multi tracker specification. It defaults to false, which
is the same behavior previous versions of libtorrent has had as well.

``announce_to_all_tiers`` also controls how multi tracker torrents are
treated. When this is set to true, one tracker from each tier is announced
to. This is the uTorrent behavior. This is false by default in order
to comply with the multi-tracker specification.

``prefer_udp_trackers`` is true by default. It means that trackers may
be rearranged in a way that udp trackers are always tried before http
trackers for the same hostname. Setting this to fails means that the
trackers' tier is respected and there's no preference of one protocol
over another.

``strict_super_seeding`` when this is set to true, a piece has to
have been forwarded to a third peer before another one is handed out.
This is the traditional definition of super seeding.

``seeding_piece_quota`` is the number of pieces to send to a peer,
when seeding, before rotating in another peer to the unchoke set.
It defaults to 3 pieces, which means that when seeding, any peer we've
sent more than this number of pieces to will be unchoked in favour of
a choked peer.

``max_sparse_regions`` is a limit of the number of *sparse regions* in
a torrent. A sparse region is defined as a hole of pieces we have not
yet downloaded, in between pieces that have been downloaded. This is
used as a hack for windows vista which has a bug where you cannot
write files with more than a certain number of sparse regions. This
limit is not hard, it will be exceeded. Once it's exceeded, pieces
that will maintain or decrease the number of sparse regions are
prioritized. To disable this functionality, set this to 0. It defaults
to 0 on all platforms except windows.

``lock_disk_cache`` if lock disk cache is set to true the disk cache
that's in use, will be locked in physical memory, preventing it from
being swapped out.

``max_rejects`` is the number of piece requests we will reject in a row
while a peer is choked before the peer is considered abusive and is
disconnected.


``recv_socket_buffer_size`` and ``send_socket_buffer_size`` specifies
the buffer sizes set on peer sockets. 0 (which is the default) means
the OS default (i.e. don't change the buffer sizes). The socket buffer
sizes are changed using setsockopt() with SOL_SOCKET/SO_RCVBUF and
SO_SNDBUFFER.

``optimize_hashing_for_speed`` chooses between two ways of reading back
piece data from disk when its complete and needs to be verified against
the piece hash. This happens if some blocks were flushed to the disk
out of order. Everything that is flushed in order is hashed as it goes
along. Optimizing for speed will allocate space to fit all the the
remaingin, unhashed, part of the piece, reads the data into it in a single
call and hashes it. This is the default. If ``optimizing_hashing_for_speed``
is false, a single block will be allocated (16 kB), and the unhashed parts
of the piece are read, one at a time, and hashed in this single block. This
is appropriate on systems that are memory constrained.

``file_checks_delay_per_block`` is the number of milliseconds to sleep
in between disk read operations when checking torrents. This defaults
to 0, but can be set to higher numbers to slow down the rate at which
data is read from the disk while checking. This may be useful for
background tasks that doesn't matter if they take a bit longer, as long
as they leave disk I/O time for other processes.

``disk_cache_algorithm`` tells the disk I/O thread which cache flush
algorithm to use. The default algorithm is largest_contiguous. This
flushes the entire piece, in the write cache, that was least recently
written to. This is specified by the ``session_settings::lru`` enum
value. ``session_settings::largest_contiguous`` will flush the largest
sequences of contiguous blocks from the write cache, regarless of the
piece's last use time. ``session_settings::avoid_readback`` will prioritize
flushing blocks that will avoid having to read them back in to verify
the hash of the piece once it's done. This is especially useful for high
throughput setups, where reading from the disk is especially expensive.

``read_cache_line_size`` is the number of blocks to read into the read
cache when a read cache miss occurs. Setting this to 0 is essentially
the same thing as disabling read cache. The number of blocks read
into the read cache is always capped by the piece boundry.

When a piece in the write cache has ``write_cache_line_size`` contiguous
blocks in it, they will be flushed. Setting this to 1 effectively
disables the write cache.

``optimistic_disk_retry`` is the number of seconds from a disk write
errors occur on a torrent until libtorrent will take it out of the
upload mode, to test if the error condition has been fixed.

libtorrent will only do this automatically for auto managed torrents.

You can explicitly take a torrent out of upload only mode using
`set_upload_mode()`_.

``disable_hash_checks`` controls if downloaded pieces are verified against
the piece hashes in the torrent file or not. The default is false, i.e.
to verify all downloaded data. It may be useful to turn this off for performance
profiling and simulation scenarios. Do not disable the hash check for regular
bittorrent clients.

``max_suggest_pieces`` is the max number of suggested piece indices received
from a peer that's remembered. If a peer floods suggest messages, this limit
prevents libtorrent from using too much RAM. It defaults to 10.

If ``drop_skipped_requests`` is set to true (it defaults to false), piece
requests that have been skipped enough times when piece messages
are received, will be considered lost. Requests are considered skipped
when the returned piece messages are re-ordered compared to the order
of the requests. This was an attempt to get out of dead-locks caused by
BitComet peers silently ignoring some requests. It may cause problems
at high rates, and high level of reordering in the uploading peer, that's
why it's disabled by default.

``low_prio_disk`` determines if the disk I/O should use a normal
or low priority policy. This defaults to true, which means that
it's low priority by default. Other processes doing disk I/O will
normally take priority in this mode. This is meant to improve the
overall responsiveness of the system while downloading in the
background. For high-performance server setups, this might not
be desirable.

``local_service_announce_interval`` is the time between local
network announces for a torrent. By default, when local service
discovery is enabled a torrent announces itself every 5 minutes.
This interval is specified in seconds.

``dht_announce_interval`` is the number of seconds between announcing
torrents to the distributed hash table (DHT). This is specified to
be 15 minutes which is its default.

``dht_max_torrents`` is the max number of torrents we will track
in the DHT.

``udp_tracker_token_expiry`` is the number of seconds libtorrent
will keep UDP tracker connection tokens around for. This is specified
to be 60 seconds, and defaults to that. The higher this value is, the
fewer packets have to be sent to the UDP tracker. In order for higher
values to work, the tracker needs to be configured to match the
expiration time for tokens.

``volatile_read_cache``, if this is set to true, read cache blocks
that are hit by peer read requests are removed from the disk cache
to free up more space. This is useful if you don't expect the disk
cache to create any cache hits from other peers than the one who
triggered the cache line to be read into the cache in the first place.

``guided_read_cache`` enables the disk cache to adjust the size
of a cache line generated by peers to depend on the upload rate
you are sending to that peer. The intention is to optimize the RAM
usage of the cache, to read ahead further for peers that you're
sending faster to.

``default_cache_min_age`` is the minimum number of seconds any read
cache line is kept in the cache. This defaults to one second but
may be greater if ``guided_read_cache`` is enabled. Having a lower
bound on the time a cache line stays in the cache is an attempt
to avoid swapping the same pieces in and out of the cache in case
there is a shortage of spare cache space.

``num_optimistic_unchoke_slots`` is the number of optimistic unchoke
slots to use. It defaults to 0, which means automatic. Having a higher
number of optimistic unchoke slots mean you will find the good peers
faster but with the trade-off to use up more bandwidth. When this is
set to 0, libtorrent opens up 20% of your allowed upload slots as
optimistic unchoke slots.

``no_atime_storage`` this is a linux-only option and passes in the
``O_NOATIME`` to ``open()`` when opening files. This may lead to
some disk performance improvements.

``default_est_reciprocation_rate`` is the assumed reciprocation rate
from peers when using the BitTyrant choker. This defaults to 14 kiB/s.
If set too high, you will over-estimate your peers and be more altruistic
while finding the true reciprocation rate, if it's set too low, you'll
be too stingy and waste finding the true reciprocation rate.

``increase_est_reciprocation_rate`` specifies how many percent the
extimated reciprocation rate should be increased by each unchoke
interval a peer is still choking us back. This defaults to 20%.
This only applies to the BitTyrant choker.

``decrease_est_reciprocation_rate`` specifies how many percent the
estimated reciprocation rate should be decreased by each unchoke
interval a peer unchokes us. This default to 3%.
This only applies to the BitTyrant choker.

``incoming_starts_queued_torrents`` defaults to false. If a torrent
has been paused by the auto managed feature in libtorrent, i.e.
the torrent is paused and auto managed, this feature affects whether
or not it is automatically started on an incoming connection. The
main reason to queue torrents, is not to make them unavailable, but
to save on the overhead of announcing to the trackers, the DHT and to
avoid spreading one's unchoke slots too thin. If a peer managed to
find us, even though we're no in the torrent anymore, this setting
can make us start the torrent and serve it.

When ``report_true_downloaded`` is true, the ``&downloaded=`` argument
sent to trackers will include redundant downloaded bytes. It defaults
to ``false``, which means redundant bytes are not reported to the tracker.

``strict_end_game_mode`` defaults to true, and controls when a block
may be requested twice. If this is ``true``, a block may only be requested
twice when there's ay least one request to every piece that's left to
download in the torrent. This may slow down progress on some pieces
sometimes, but it may also avoid downloading a lot of redundant bytes.
If this is ``false``, libtorrent attempts to use each peer connection
to its max, by always requesting something, even if it means requesting
something that has been requested from another peer already.

if ``broadcast_lsd`` is set to true, the local peer discovery
(or Local Service Discovery) will not only use IP multicast, but also
broadcast its messages. This can be useful when running on networks
that don't support multicast. Since broadcast messages might be
expensive and disruptive on networks, only every 8th announce uses
broadcast.

``enable_outgoing_utp``, ``enable_incoming_utp``, ``enable_outgoing_tcp``,
``enable_incoming_tcp`` all determines if libtorrent should attempt to make
outgoing connections of the specific type, or allow incoming connection. By
default all of them are enabled.

``ignore_resume_timestamps`` determines if the storage, when loading
resume data files, should verify that the file modification time
with the timestamps in the resume data. This defaults to false, which
means timestamps are taken into account, and resume data is less likely
to accepted (torrents are more likely to be fully checked when loaded).
It might be useful to set this to true if your network is faster than your
disk, and it would be faster to redownload potentially missed pieces than
to go through the whole storage to look for them.

``no_recheck_incomplete_resume`` determines if the storage should check
the whole files when resume data is incomplete or missing or whether
it should simply assume we don't have any of the data. By default, this
is determined by the existance of any of the files. By setting this setting
to true, the files won't be checked, but will go straight to download
mode.

``anonymous_mode`` defaults to false. When set to true, the client tries
to hide its identity to a certain degree. The peer-ID will no longer
include the client's fingerprint. The user-agent will be reset to an
empty string. Trackers will only be used if they are using a proxy
server. The listen sockets are closed, and incoming connections will
only be accepted through a SOCKS5 or I2P proxy (if a peer proxy is set up and
is run on the same machine as the tracker proxy). Since no incoming connections
are accepted, NAT-PMP, UPnP, DHT and local peer discovery are all turned off
when this setting is enabled.

If you're using I2P, it might make sense to enable anonymous mode as well.

``tick_interval`` specifies the number of milliseconds between internal
ticks. This is the frequency with which bandwidth quota is distributed to
peers. It should not be more than one second (i.e. 1000 ms). Setting this
to a low value (around 100) means higher resolution bandwidth quota distribution,
setting it to a higher value saves CPU cycles.

``share_mode_target`` specifies the target share ratio for share mode torrents.
This defaults to 3, meaning we'll try to upload 3 times as much as we download.
Setting this very high, will make it very conservative and you might end up
not downloading anything ever (and not affecting your share ratio). It does
not make any sense to set this any lower than 2. For instance, if only 3 peers
need to download the rarest piece, it's impossible to download a single piece
and upload it more than 3 times. If the share_mode_target is set to more than 3,
nothing is downloaded.

``upload_rate_limit``, ``download_rate_limit``, ``local_upload_rate_limit``
and ``local_download_rate_limit`` sets the session-global limits of upload
and download rate limits, in bytes per second. The local rates refer to peers
on the local network. By default peers on the local network are not rate limited.

These rate limits are only used for local peers (peers within the same subnet as
the client itself) and it is only used when ``session_settings::ignore_limits_on_local_network``
is set to true (which it is by default). These rate limits default to unthrottled,
but can be useful in case you want to treat local peers preferentially, but not
quite unthrottled.

A value of 0 means unlimited.

``dht_upload_rate_limit`` sets the rate limit on the DHT. This is specified in
bytes per second and defaults to 4000. For busy boxes with lots of torrents
that requires more DHT traffic, this should be raised.

``unchoke_slots_limit`` is the max number of unchoked peers in the session. The
number of unchoke slots may be ignored depending on what ``choking_algorithm``
is set to. A value of -1 means infinite.

``half_open_limit`` sets the maximum number of half-open connections
libtorrent will have when connecting to peers. A half-open connection is one
where connect() has been called, but the connection still hasn't been established
(nor failed). Windows XP Service Pack 2 sets a default, system wide, limit of
the number of half-open connections to 10. So, this limit can be used to work
nicer together with other network applications on that system. The default is
to have no limit, and passing -1 as the limit, means to have no limit. When
limiting the number of simultaneous connection attempts, peers will be put in
a queue waiting for their turn to get connected.

``connections_limit`` sets a global limit on the number of connections
opened. The number of connections is set to a hard minimum of at least two per
torrent, so if you set a too low connections limit, and open too many torrents,
the limit will not be met.

``utp_target_delay`` is the target delay for uTP sockets in milliseconds. A high
value will make uTP connections more aggressive and cause longer queues in the upload
bottleneck. It cannot be too low, since the noise in the measurements would cause
it to send too slow. The default is 50 milliseconds.

``utp_gain_factor`` is the number of bytes the uTP congestion window can increase
at the most in one RTT. This defaults to 300 bytes. If this is set too high,
the congestion controller reacts too hard to noise and will not be stable, if it's
set too low, it will react slow to congestion and not back off as fast.

``utp_min_timeout`` is the shortest allowed uTP socket timeout, specified in milliseconds.
This defaults to 500 milliseconds. The timeout depends on the RTT of the connection, but
is never smaller than this value. A connection times out when every packet in a window
is lost, or when a packet is lost twice in a row (i.e. the resent packet is lost as well).

The shorter the timeout is, the faster the connection will recover from this situation,
assuming the RTT is low enough.

``utp_syn_resends`` is the number of SYN packets that are sent (and timed out) before
giving up and closing the socket.

``utp_num_resends`` is the number of times a packet is sent (and lossed or timed out)
before giving up and closing the connection.

``utp_connect_timeout`` is the number of milliseconds of timeout for the initial SYN
packet for uTP connections. For each timed out packet (in a row), the timeout is doubled.

``utp_delayed_ack`` is the number of milliseconds to delay ACKs the most. Delaying ACKs
significantly helps reducing the amount of protocol overhead in the reverse direction
from downloads. It defaults to 100 milliseconds. If set to 0, delayed ACKs are disabled
and every incoming payload packet is ACKed. The granularity of this timer is capped by
the tick interval (as specified by ``tick_interval``).

``utp_dynamic_sock_buf`` controls if the uTP socket manager is allowed to increase
the socket buffer if a network interface with a large MTU is used (such as loopback
or ethernet jumbo frames). This defaults to true and might improve uTP throughput.
For RAM constrained systems, disabling this typically saves around 30kB in user space
and probably around 400kB in kernel socket buffers (it adjusts the send and receive
buffer size on the kernel socket, both for IPv4 and IPv6).

``utp_loss_multiplier`` controls how the congestion window is changed when a packet
loss is experienced. It's specified as a percentage multiplier for ``cwnd``. By default
it's set to 50 (i.e. cut in half). Do not change this value unless you know what
you're doing. Never set it higher than 100.

The ``mixed_mode_algorithm`` determines how to treat TCP connections when there are
uTP connections. Since uTP is designed to yield to TCP, there's an inherent problem
when using swarms that have both TCP and uTP connections. If nothing is done, uTP
connections would often be starved out for bandwidth by the TCP connections. This mode
is ``prefer_tcp``. The ``peer_proportional`` mode simply looks at the current throughput
and rate limits all TCP connections to their proportional share based on how many of
the connections are TCP. This works best if uTP connections are not rate limited by
the global rate limiter (which they aren't by default).

``rate_limit_utp`` determines if uTP connections should be throttled by the global rate
limiter or not. By default they are.

``listen_queue_size`` is the value passed in to listen() for the listen socket.
It is the number of outstanding incoming connections to queue up while we're not
actively waiting for a connection to be accepted. The default is 5 which should
be sufficient for any normal client. If this is a high performance server which
expects to receive a lot of connections, or used in a simulator or test, it
might make sense to raise this number. It will not take affect until listen_on()
is called again (or for the first time).

if ``announce_double_nat`` is true, the ``&ip=`` argument in tracker requests
(unless otherwise specified) will be set to the intermediate IP address, if the
user is double NATed. If ther user is not double NATed, this option has no affect.

``torrent_connect_boost`` is the number of peers to try to connect to immediately
when the first tracker response is received for a torrent. This is a boost to
given to new torrents to accelerate them starting up. The normal connect scheduler
is run once every second, this allows peers to be connected immediately instead
of waiting for the session tick to trigger connections.

``seeding_outgoing_connections`` determines if seeding (and finished) torrents
should attempt to make outgoing connections or not. By default this is true. It
may be set to false in very specific applications where the cost of making
outgoing connections is high, and there are no or small benefits of doing so.
For instance, if no nodes are behind a firewall or a NAT, seeds don't need to
make outgoing connections.

if ``no_connect_privileged_ports`` is true (which is the default), libtorrent
will not connect to any peers on priviliged ports (<= 1023). This can mitigate
using bittorrent swarms for certain DDoS attacks.

``alert_queue_size`` is the maximum number of alerts queued up internally. If
alerts are not popped, the queue will eventually fill up to this level. This
defaults to 1000.

``max_metadata_size`` is the maximum allowed size (in bytes) to be received
by the metadata extension, i.e. magnet links. It defaults to 1 MiB.

``smooth_connects`` is true by default, which means the number of connection
attempts per second may be limited to below the ``connection_speed``, in case
we're close to bump up against the limit of number of connections. The intention
of this setting is to more evenly distribute our connection attempts over time,
instead of attempting to connectin in batches, and timing them out in batches.

``always_send_user_agent`` defaults to false. When set to true, web connections
will include a user-agent with every request, as opposed to just the first
request in a connection.

``apply_ip_filter_to_trackers`` defaults to true. It determines whether the
IP filter applies to trackers as well as peers. If this is set to false,
trackers are exempt from the IP filter (if there is one). If no IP filter
is set, this setting is irrelevant.

``read_job_every`` is used to avoid starvation of read jobs in the disk I/O
thread. By default, read jobs are deferred, sorted by physical disk location
and serviced once all write jobs have been issued. In scenarios where the
download rate is enough to saturate the disk, there's a risk the read jobs will
never be serviced. With this setting, every *x* write job, issued in a row, will
instead pick one read job off of the sorted queue, where *x* is ``read_job_every``.

``use_disk_read_ahead`` defaults to true and will attempt to optimize disk reads
by giving the operating system heads up of disk read requests as they are queued
in the disk job queue. This gives a significant performance boost for seeding.

``lock_files`` determines whether or not to lock files which libtorrent is downloading
to or seeding from. This is implemented using ``fcntl(F_SETLK)`` on unix systems and
by not passing in ``SHARE_READ`` and ``SHARE_WRITE`` on windows. This might prevent
3rd party processes from corrupting the files under libtorrent's feet.

``ssl_listen`` sets the listen port for SSL connections. If this is set to 0,
no SSL listen port is opened. Otherwise a socket is opened on this port. This
setting is only taken into account when opening the regular listen port, and
won't re-open the listen socket simply by changing this setting.

It defaults to port 4433.

``tracker_backoff`` determines how aggressively to back off from retrying
failing trackers. This value determines *x* in the following formula, determining
the number of seconds to wait until the next retry:

	delay = 5 + 5 * x / 100 * fails^2

It defaults to 250.

This setting may be useful to make libtorrent more or less aggressive in hitting
trackers.

``ban_web_seeds`` enables banning web seeds. By default, web seeds that send
corrupt data are banned.

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
			bool proxy_hostnames;
			bool proxy_peer_connections;
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

``proxy_hostnames`` defaults to true. It means that hostnames should be
attempted to be resolved through the proxy instead of using the local DNS
service. This is only supported by SOCKS5 and HTTP.

``proxy_peer_connections`` determines whether or not to excempt peer and
web seed connections from using the proxy. This defaults to true, i.e. peer
connections are proxied by default.

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

add_mapping()
-------------

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

delete_mapping()
----------------

	::

		void delete_mapping(int mapping_index);

This function removes a port mapping. ``mapping_index`` is the index that refers
to the mapping you want to remove, which was returned from `add_mapping()`_.

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


lazy_bdecode()
--------------

	::

		int lazy_bdecode(char const* start, char const* end, lazy_entry& ret
			, error_code& ec, int* error_pos = 0, int depth_limit = 1000
			, int item_limit = 1000000);

This function decodes bencoded_ data.

.. _bencoded: http://wiki.theory.org/index.php/BitTorrentSpecification

Whenever possible, ``lazy_bdecode()`` should be preferred over ``bdecode()``.
It is more efficient and more secure. It supports having constraints on the
amount of memory is consumed by the parser.

*lazy* refers to the fact that it doesn't copy any actual data out of the
bencoded buffer. It builds a tree of ``lazy_entry`` which has pointers into
the bencoded buffer. This makes it very fast and efficient. On top of that,
it is not recursive, which saves a lot of stack space when parsing deeply
nested trees. However, in order to protect against potential attacks, the
``depth_limit`` and ``item_limit`` control how many levels deep the tree is
allowed to get. With recursive parser, a few thousand levels would be enough
to exhaust the threads stack and terminate the process. The ``item_limit``
protects against very large structures, not necessarily deep. Each bencoded
item in the structure causes the parser to allocate some amount of memory,
this memory is constant regardless of how much data actually is stored in
the item. One potential attack is to create a bencoded list of hundreds of
thousands empty strings, which would cause the parser to allocate a significant
amount of memory, perhaps more than is available on the machine, and effectively
provide a denial of service. The default item limit is set as a reasonable
upper limit for desktop computers. Very few torrents have more items in them.
The limit corresponds to about 25 MB, which might be a bit much for embedded
systems.

``start`` and ``end`` defines the bencoded buffer to be decoded. ``ret`` is
the ``lazy_entry`` which is filled in with the whole decoded tree. ``ec``
is a reference to an ``error_code`` which is set to describe the error encountered
in case the function fails. ``error_pos`` is an optional pointer to an int,
which will be set to the byte offset into the buffer where an error occurred,
in case the function fails.

bdecode() bencode() 
--------------------

	::

		template<class InIt> entry bdecode(InIt start, InIt end);
		template<class OutIt> void bencode(OutIt out, const entry& e);

These functions will encode data to bencoded_ or decode bencoded_ data.

If possible, `lazy_bdecode()`_ should be preferred over ``bdecode()``.

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
it will throw libtorrent_exception_.

add_magnet_uri()
----------------

*deprecated*

	::

		torrent_handle add_magnet_uri(session& ses, std::string const& uri
			add_torrent_params p);
		torrent_handle add_magnet_uri(session& ses, std::string const& uri
			add_torrent_params p, error_code& ec);

This function parses the magnet URI (``uri``) as a bittorrent magnet link,
and adds the torrent to the specified session (``ses``). It returns the
handle to the newly added torrent, or an invalid handle in case parsing
failed. To control some initial settings of the torrent, sepcify those in
the ``add_torrent_params``, ``p``. See `async_add_torrent() add_torrent()`_.

The overload that does not take an ``error_code`` throws an exception on
error and is not available when building without exception support.

A simpler way to add a magnet link to a session is to pass in the
link through ``add_torrent_params::url`` argument to ``session::add_torrent()``.

For more information about magnet links, see `magnet links`_.

parse_magnet_uri()
------------------

	::

		void parse_magnet_uri(std::string const& uri, add_torrent_params& p, error_code& ec);

This function parses out information from the magnet link and populates the
``add_torrent_params`` object.

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

By default, only errors are reported. `set_alert_mask()`_ can be
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
| ``stats_notification``         | If you enable these alerts, you will receive a ``stats_alert``      |
|                                | approximately once every second, for every active torrent.          |
|                                | These alerts contain all statistics counters for the interval since |
|                                | the lasts stats alert.                                              |
+--------------------------------+---------------------------------------------------------------------+
| ``dht_notification``           | Alerts on events in the DHT node. For incoming searches or          |
|                                | bootstrapping being done etc.                                       |
+--------------------------------+---------------------------------------------------------------------+
| ``rss_notification``           | Alerts on RSS related events, like feeds being updated, feed error  |
|                                | conditions and successful RSS feed updates. Enabling this categoty  |
|                                | will make you receive ``rss_alert`` alerts.                         |
+--------------------------------+---------------------------------------------------------------------+
| ``all_categories``             | The full bitmask, representing all available categories.            |
+--------------------------------+---------------------------------------------------------------------+

Every alert belongs to one or more category. There is a small cost involved in posting alerts. Only
alerts that belong to an enabled category are posted. Setting the alert bitmask to 0 will disable
all alerts

When you get an alert, you can use ``alert_cast<>`` to attempt to cast the pointer to a
more specific alert type, to be queried for more information about the alert. ``alert_cast``
has the followinf signature::

	template <T> T* alert_cast(alert* a);
	template <T> T const* alert_cast(alert const* a);

You can also use a `alert dispatcher`_ mechanism that's available in libtorrent.

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
			dht_notification = *implementation defined*,
			stats_notification = *implementation defined*,

			all_categories = *implementation defined*
		};

		ptime timestamp() const;

		virtual ~alert();

		virtual int type() const = 0;
		virtual std::string message() const = 0;
		virtual char const* what() const = 0;
		virtual int category() const = 0;
		virtual bool discardable() const;
		virtual std::auto_ptr<alert> clone() const = 0;
	};

``type()`` returns an integer that is unique to this alert type. It can be
compared against a specific alert by querying a static constant called ``alert_type``
in the alert. It can be used to determine the run-time type of an alert* in
order to cast to that alert type and access specific members.

e.g::

	std::auto_ptr<alert> a = ses.pop_alert();
	switch (a->type())
	{
		case read_piece_alert::alert_type:
		{
			read_piece_alert* p = (read_piece_alert*)a.get();
			// use p
			break;
		}
		case file_renamed_alert::alert_type:
		{
			// etc...
		}
	}

``what()`` returns a string literal describing the type of the alert. It does
not include any information that might be bundled with the alert.

``category()`` returns a bitmask specifying which categories this alert belong to.

``clone()`` returns a pointer to a copy of the alert.

``discardable()`` determines whether or not an alert is allowed to be discarded
when the alert queue is full. There are a few alerts which may not be discared,
since they would break the user contract, such as ``save_resume_data_alert``.

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

torrent_added_alert
-------------------

The ``torrent_added_alert`` is posted once every time a torrent is added.
It doesn't contain any members of its own, but inherits the torrent handle
from its base class.
It's posted when the ``status_notification`` bit is set in the alert mask.

::

	struct torrent_added_alert: torrent_alert
	{
		// ...
	};


add_torrent_alert
-----------------

This alert is always posted when a torrent was added via ``async_add_torrent()``
and contains the return status of the add operation. The torrent handle of the new
torrent can be found in the base class' ``handle`` member.

::

	struct add_torrent_alert: torrent_alert
	{
		// ...
		add_torrent_params params;
		error_code error;
	};

``params`` is a copy of the parameters used when adding the torrent, it can be used
to identify which invocation to ``async_add_torrent()`` caused this alert.

``error`` is set to the error, if any, adding the torrent.


torrent_removed_alert
---------------------

The ``torrent_removed_alert`` is posted whenever a torrent is removed. Since
the torrent handle in its baseclass will always be invalid (since the torrent
is already removed) it has the info hash as a member, to identify it.
It's posted when the ``status_notification`` bit is set in the alert mask.

::

	struct torrent_removed_alert: torrent_alert
	{
		// ...
		sha1_hash info_hash;
	};

read_piece_alert
----------------

This alert is posted when the asynchronous read operation initiated by
a call to `read_piece()`_ is completed. If the read failed, the torrent
is paused and an error state is set and the buffer member of the alert
is 0. If successful, ``buffer`` points to a buffer containing all the data
of the piece. ``piece`` is the piece index that was read. ``size`` is the
number of bytes that was read.

::

	struct read_piece_alert: torrent_alert
	{
		// ...
		boost::shared_ptr<char> buffer;
		int piece;
		int size;
	};

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
session_ can be opened for listening. The ``endpoint`` member is the
interface and port that failed, ``error`` is the error code describing
the failure.

libtorrent may sometimes try to listen on port 0, if all other ports failed.
Port 0 asks the operating system to pick a port that's free). If that fails
you may see a listen_failed_alert_ with port 0 even if you didn't ask to
listen on it.

::

	struct listen_failed_alert: alert
	{
		// ...
		tcp::endpoint endpoint;
		error_code error;
	};

listen_succeeded_alert
----------------------

This alert is posted when the listen port succeeds to be opened on a
particular interface. ``endpoint`` is the endpoint that successfully
was opened for listening.

::

	struct listen_succeeded_alert: alert
	{
		// ...
		tcp::endpoint endpoint;
	};


portmap_error_alert
-------------------

This alert is generated when a NAT router was successfully found but some
part of the port mapping request failed. It contains a text message that
may help the user figure out what is wrong. This alert is not generated in
case it appears the client is not running on a NAT:ed network or if it
appears there is no NAT router that can be remote controlled to add port
mappings.

``mapping`` refers to the mapping index of the port map that failed, i.e.
the index returned from `add_mapping()`_.

``map_type`` is 0 for NAT-PMP and 1 for UPnP.

``error`` tells you what failed.
::

	struct portmap_error_alert: alert
	{
		// ...
		int mapping;
		int type;
		error_code error;
	};

portmap_alert
-------------

This alert is generated when a NAT router was successfully found and
a port was successfully mapped on it. On a NAT:ed network with a NAT-PMP
capable router, this is typically generated once when mapping the TCP
port and, if DHT is enabled, when the UDP port is mapped.

``mapping`` refers to the mapping index of the port map that failed, i.e.
the index returned from `add_mapping()`_.

``external_port`` is the external port allocated for the mapping.

``type`` is 0 for NAT-PMP and 1 for UPnP.

::

	struct portmap_alert: alert
	{
		// ...
		int mapping;
		int external_port;
		int map_type;
	};

portmap_log_alert
-----------------

This alert is generated to log informational events related to either
UPnP or NAT-PMP. They contain a log line and the type (0 = NAT-PMP
and 1 = UPnP). Displaying these messages to an end user is only useful
for debugging the UPnP or NAT-PMP implementation.

::

	struct portmap_log_alert: alert
	{
		//...
		int map_type;
		std::string msg;
	};

file_error_alert
----------------

If the storage fails to read or write files that it needs access to, this alert is
generated and the torrent is paused.

``file`` is the path to the file that was accessed when the error occurred.

``error`` is the error code describing the error.

::

	struct file_error_alert: torrent_alert
	{
		// ...
		std::string file;
		error_code error;
	};

torrent_error_alert
-------------------

This is posted whenever a torrent is transitioned into the error state.

::

	struct torrent_error_alert: torrent_alert
	{
		// ...
		error_code error;
	};

The ``error`` specifies which error the torrent encountered.

file_renamed_alert
------------------

This is posted as a response to a ``torrent_handle::rename_file`` call, if the rename
operation succeeds.

::

	struct file_renamed_alert: torrent_alert
	{
		// ...
		std::string name;
		int index;
	};

The ``index`` member refers to the index of the file that was renamed,
``name`` is the new name of the file.


file_rename_failed_alert
------------------------

This is posted as a response to a ``torrent_handle::rename_file`` call, if the rename
operation failed.

::

	struct file_rename_failed_alert: torrent_alert
	{
		// ...
		int index;
		error_code error;
	};

The ``index`` member refers to the index of the file that was supposed to be renamed,
``error`` is the error code returned from the filesystem.

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


peer_alert
----------

The peer alert is a base class for alerts that refer to a specific peer. It includes all
the information to identify the peer. i.e. ``ip`` and ``peer-id``.

::

	struct peer_alert: torrent_alert
	{
		// ...
		tcp::endpoint ip;
		peer_id pid;
	};

peer_connect_alert
------------------

This alert is posted every time an outgoing peer connect attempts succeeds.

::

	struct peer_connect_alert: peer_alert
	{
		// ...
	};

peer_ban_alert
--------------

This alert is generated when a peer is banned because it has sent too many corrupt pieces
to us. ``ip`` is the endpoint to the peer that was banned.

::

	struct peer_ban_alert: peer_alert
	{
		// ...
	};


peer_snubbed_alert
------------------

This alert is generated when a peer is snubbed, when it stops sending data when we request
it.

::

	struct peer_snubbed_alert: peer_alert
	{
		// ...
	};


peer_unsnubbed_alert
--------------------

This alert is generated when a peer is unsnubbed. Essentially when it was snubbed for stalling
sending data, and now it started sending data again.

::

	struct peer_unsnubbed_alert: peer_alert
	{
		// ...
	};


peer_error_alert
----------------

This alert is generated when a peer sends invalid data over the peer-peer protocol. The peer
will be disconnected, but you get its ip address from the alert, to identify it.

The ``error_code`` tells you what error caused this alert.

::

	struct peer_error_alert: peer_alert
	{
		// ...
		error_code error;
	};


peer_connected_alert
--------------------

This alert is generated when a peer is connected.

::

	struct peer_connected_alert: peer_alert
	{
		// ...
	};


peer_disconnected_alert
-----------------------

This alert is generated when a peer is disconnected for any reason (other than the ones
covered by ``peer_error_alert``).

The ``error_code`` tells you what error caused peer to disconnect.

::

	struct peer_disconnected_alert: peer_alert
	{
		// ...
		error_code error;
	};


invalid_request_alert
---------------------

This is a debug alert that is generated by an incoming invalid piece request.
``ìp`` is the address of the peer and the ``request`` is the actual incoming
request from the peer.

::

	struct invalid_request_alert: peer_alert
	{
		// ...
		peer_request request;
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

request_dropped_alert
---------------------

This alert is generated when a peer rejects or ignores a piece request.

::

	struct request_dropped_alert: peer_alert
	{
		// ...
		int block_index;
		int piece_index;
	};


block_timeout_alert
-------------------

This alert is generated when a block request times out.

::

	struct block_timeout_alert: peer_alert
	{
		// ...
		int block_index;
		int piece_index;
	};


block_finished_alert
--------------------

This alert is generated when a block request receives a response.

::

	struct block_finished_alert: peer_alert
	{
		// ...
		int block_index;
		int piece_index;
	};


lsd_peer_alert
--------------

This alert is generated when we receive a local service discovery message from a peer
for a torrent we're currently participating in.

::

	struct lsd_peer_alert: peer_alert
	{
		// ...
	};


file_completed_alert
--------------------

This is posted whenever an individual file completes its download. i.e.
All pieces overlapping this file have passed their hash check.

::

	struct file_completed_alert: torrent_alert
	{
		// ...
		int index;
	};

The ``index`` member refers to the index of the file that completed.


block_downloading_alert
-----------------------

This alert is generated when a block request is sent to a peer.

::

	struct block_downloading_alert: peer_alert
	{
		// ...
		int block_index;
		int piece_index;
	};


unwanted_block_alert
--------------------

This alert is generated when a block is received that was not requested or
whose request timed out.

::

	struct unwanted_block_alert: peer_alert
	{
		// ...
		int block_index;
		int piece_index;
	};


torrent_delete_failed_alert
---------------------------

This alert is generated when a request to delete the files of a torrent fails.

The ``error_code`` tells you why it failed.

::

	struct torrent_delete_failed_alert: torrent_alert
	{
		// ...
		error_code error;
	};

torrent_deleted_alert
---------------------

This alert is generated when a request to delete the files of a torrent complete.

The ``info_hash`` is the info-hash of the torrent that was just deleted. Most of
the time the torrent_handle in the ``torrent_alert`` will be invalid by the time
this alert arrives, since the torrent is being deleted. The ``info_hash`` member
is hence the main way of identifying which torrent just completed the delete.

This alert is posted in the ``storage_notification`` category, and that bit
needs to be set in the alert mask.

::

	struct torrent_deleted_alert: torrent_alert
	{
		// ...
		sha1_hash info_hash;
	};

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
			upload_limit_too_low,
			download_limit_too_low,
			send_buffer_watermark_too_low,
			too_many_optimistic_unchoke_slots,
			too_high_disk_queue_limit,
			too_few_outgoing_ports
		};

		performance_warning_t warning_code;
	};

outstanding_disk_buffer_limit_reached
	This warning means that the number of bytes queued to be written to disk
	exceeds the max disk byte queue setting (``session_settings::max_queued_disk_bytes``).
	This might restrict the download rate, by not queuing up enough write jobs
	to the disk I/O thread. When this alert is posted, peer connections are
	temporarily stopped from downloading, until the queued disk bytes have fallen
	below the limit again. Unless your ``max_queued_disk_bytes`` setting is already
	high, you might want to increase it to get better performance.

outstanding_request_limit_reached
	This is posted when libtorrent would like to send more requests to a peer,
	but it's limited by ``session_settings::max_out_request_queue``. The queue length
	libtorrent is trying to achieve is determined by the download rate and the
	assumed round-trip-time (``session_settings::request_queue_time``). The assumed
	rount-trip-time is not limited to just the network RTT, but also the remote disk
	access time and message handling time. It defaults to 3 seconds. The target number
	of outstanding requests is set to fill the bandwidth-delay product (assumed RTT
	times download rate divided by number of bytes per request). When this alert
	is posted, there is a risk that the number of outstanding requests is too low
	and limits the download rate. You might want to increase the ``max_out_request_queue``
	setting.

upload_limit_too_low
	This warning is posted when the amount of TCP/IP overhead is greater than the
	upload rate limit. When this happens, the TCP/IP overhead is caused by a much
	faster download rate, triggering TCP ACK packets. These packets eat into the
	rate limit specified to libtorrent. When the overhead traffic is greater than
	the rate limit, libtorrent will not be able to send any actual payload, such
	as piece requests. This means the download rate will suffer, and new requests
	can be sent again. There will be an equilibrium where the download rate, on
	average, is about 20 times the upload rate limit. If you want to maximize the
	download rate, increase the upload rate limit above 5% of your download capacity.

download_limit_too_low
	This is the same warning as ``upload_limit_too_low`` but referring to the download
	limit instead of upload. This suggests that your download rate limit is mcuh lower
	than your upload capacity. Your upload rate will suffer. To maximize upload rate,
	make sure your download rate limit is above 5% of your upload capacity.

send_buffer_watermark_too_low
	We're stalled on the disk. We want to write to the socket, and we can write
	but our send buffer is empty, waiting to be refilled from the disk.
	This either means the disk is slower than the network connection
	or that our send buffer watermark is too small, because we can
	send it all before the disk gets back to us.
	The number of bytes that we keep outstanding, requested from the disk, is calculated
	as follows::

		min(512, max(upload_rate * send_buffer_watermark_factor / 100, send_buffer_watermark))

	If you receive this alert, you migth want to either increase your ``send_buffer_watermark``
	or ``send_buffer_watermark_factor``.

too_many_optimistic_unchoke_slots
	If the half (or more) of all upload slots are set as optimistic unchoke slots, this
	warning is issued. You probably want more regular (rate based) unchoke slots.

too_high_disk_queue_limit
	If the disk write queue ever grows larger than half of the cache size, this warning
	is posted. The disk write queue eats into the total disk cache and leaves very little
	left for the actual cache. This causes the disk cache to oscillate in evicting large
	portions of the cache before allowing peers to download any more, onto the disk write
	queue. Either lower ``max_queued_disk_bytes`` or increase ``cache_size``.

too_few_outgoing_ports
	This is generated if outgoing peer connections are failing because of *address in use*
	errors, indicating that ``session_settings::outgoing_ports`` is set and is too small of
	a range. Consider not using the ``outgoing_ports`` setting at all, or widen the range to
	include more ports.

state_changed_alert
-------------------

Generated whenever a torrent changes its state.

::	

	struct state_changed_alert: torrent_alert
	{
		// ...

		torrent_status::state_t state;
		torrent_status::state_t prev_state;
	};

``state`` is the new state of the torrent. ``prev_state`` is the previous state.


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

Typically, when receiving this alert, you would want to save the torrent file in order
to load it back up again when the session is restarted. Here's an example snippet of
code to do that::

	torrent_handle h = alert->handle();
	if (h.is_valid()) {
		torrent_info const& ti = h.get_torrent_info();
		create_torrent ct(ti);
		entry te = ct.generate();
		std::vector<char> buffer;
		bencode(std::back_inserter(buffer), te);
		FILE* f = fopen((to_hex(ti.info_hash().to_string()) + ".torrent").c_str(), "wb+");
		if (f) {
			fwrite(&buffer[0], 1, buffer.size(), f);
			fclose(f);
		}
	}

fastresume_rejected_alert
-------------------------

This alert is generated when a fastresume file has been passed to ``add_torrent`` but the
files on disk did not match the fastresume file. The ``error_code`` explains the reason why the
resume file was rejected.

::

	struct fastresume_rejected_alert: torrent_alert
	{
		// ...
		error_code error;
	};


peer_blocked_alert
------------------

This alert is posted when an incoming peer connection, or a peer that's about to be added
to our peer list, is blocked for some reason. This could be any of:

* the IP filter
* i2p mixed mode restrictions (a normal peer is not allowed on an i2p swarm)
* the port filter
* the peer has a low port and ``no_connect_privileged_ports`` is enabled
* the protocol of the peer is blocked (uTP/TCP blocking)

The ``ip`` member is the address that was blocked.

::

	struct peer_blocked_alert: torrent_alert
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


storage_moved_failed_alert
--------------------------

The ``storage_moved_failed_alert`` is generated when an attempt to move the storage
(via torrent_handle::move_storage()) fails.

::

	struct storage_moved_failed_alert: torrent_alert
	{
		// ...
		error_code error;
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
generating the resume data. ``error`` describes what went wrong.

::

	struct save_resume_data_failed_alert: torrent_alert
	{
		// ...
		error_code error;
	};

stats_alert
-----------

This alert is posted approximately once every second, and it contains
byte counters of most statistics that's tracked for torrents. Each active
torrent posts these alerts regularly.

::

	struct stats_alert: torrent_alert
	{
		// ...
		enum stats_channel
		{
			upload_payload,
			upload_protocol,
			upload_ip_protocol,
			upload_dht_protocol,
			upload_tracker_protocol,
			download_payload,
			download_protocol,
			download_ip_protocol,
			download_dht_protocol,
			download_tracker_protocol,
			num_channels
		};

		int transferred[num_channels];
		int interval;
	};

``transferred`` this is an array of samples. The enum describes what each
sample is a measurement of. All of these are raw, and not smoothing is performed.

``interval`` the number of milliseconds during which these stats
were collected. This is typically just above 1000, but if CPU is
limited, it may be higher than that.


cache_flushed_alert
-------------------

This alert is posted when the disk cache has been flushed for a specific torrent
as a result of a call to `flush_cache()`_. This alert belongs to the
``storage_notification`` category, which must be enabled to let this alert through.
The alert is also posted when removing a torrent from the session, once the outstanding
cache flush is complete and the torrent does no longer have any files open.

::

	struct flush_cached_alert: torrent_alert
	{
		// ...
	};

torrent_need_cert_alert
-----------------------

This is always posted for SSL torrents. This is a reminder to the client that
the torrent won't work unless torrent_handle::set_ssl_certificate() is called with
a valid certificate. Valid certificates MUST be signed by the SSL certificate
in the .torrent file.

::

	struct torrent_need_cert_alert: tracker_alert
	{
		// ...
	};

dht_announce_alert
------------------

This alert is generated when a DHT node announces to an info-hash on our DHT node. It belongs
to the ``dht_notification`` category.

::

	struct dht_announce_alert: alert
	{
		// ...
		address ip;
		int port;
		sha1_hash info_hash;
	};

dht_get_peers_alert
-------------------

This alert is generated when a DHT node sends a ``get_peers`` message to our DHT node.
It belongs to the ``dht_notification`` category.

::

	struct dht_get_peers_alert: alert
	{
		// ...
		sha1_hash info_hash;
	};

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

dht_bootstrap_alert
-------------------

This alert is posted when the initial DHT bootstrap is done. There's no any other
relevant information associated with this alert.

::
	
	struct dht_bootstrap_alert: alert
	{
		// ...
	};

anonymous_mode_alert
--------------------

This alert is posted when a bittorrent feature is blocked because of the
anonymous mode. For instance, if the tracker proxy is not set up, no
trackers will be used, because trackers can only be used through proxies
when in anonymous mode.

::

	struct anonymous_mode_alert: tracker_alert
	{
		// ...
		enum kind_t
		{
			tracker_not_anonymous = 1
		};
		int kind;
		std::string str;
	};

``kind`` specifies what error this is, it's one of:

``tracker_not_anonymous`` means that there's no proxy set up for tracker
communication and the tracker will not be contacted. The tracker which
this failed for is specified in the ``str`` member.

rss_alert
---------

This alert is posted on RSS feed events such as start of RSS feed updates,
successful completed updates and errors during updates.

This alert is only posted if the ``rss_notifications`` category is enabled
in the alert mask.

::

	struct rss_alert: alert
	{
		// ..
		virtual std::string message() const;

		enum state_t
		{
			state_updating, state_updated, state_error
		};

		feed_handle handle;
		std::string url;
		int state;
		error_code error;
	};

``handle`` is the handle to the feed which generated this alert.

``url`` is a short cut to access the url of the feed, without
having to call ``get_settings()``.

``state`` is one of:

``rss_alert::state_updating``
	An update of this feed was just initiated, it will either succeed
	or fail soon.

``rss_alert::state_updated``
	The feed just completed a successful update, there may be new items
	in it. If you're adding torrents manually, you may want to request
	the feed status of the feed and look through the ``items`` vector.

``rss_akert::state_error``
	An error just occurred. See the ``error`` field for information on
	what went wrong.

``error`` is an error code used for when an error occurs on the feed.

incoming_connection_alert
-------------------------

The incoming connection alert is posted every time we successfully accept
an incoming connection, through any mean. The most straigh-forward ways
of accepting incoming connections are through the TCP listen socket and
the UDP listen socket for uTP sockets. However, connections may also be
accepted ofer a Socks5 or i2p listen socket, or via a torrent specific
listen socket for SSL torrents.

::

	struct incoming_connection_alert: alert
	{
		// ...
		virtual std::string message() const;

		int socket_type;
		tcp::endpoint ip;
	};

``socket_type`` tells you what kind of socket the connection was accepted
as:

+----------+-------------------------------------+
| value    | type                                |
+==========+=====================================+
| 0        | none (no socket instantiated)       |
+----------+-------------------------------------+
| 1        | TCP                                 |
+----------+-------------------------------------+
| 2        | Socks5                              |
+----------+-------------------------------------+
| 3        | HTTP                                |
+----------+-------------------------------------+
| 4        | uTP                                 |
+----------+-------------------------------------+
| 5        | i2p                                 |
+----------+-------------------------------------+
| 6        | SSL/TCP                             |
+----------+-------------------------------------+
| 7        | SSL/Socks5                          |
+----------+-------------------------------------+
| 8        | HTTPS (SSL/HTTP)                    |
+----------+-------------------------------------+
| 9        | SSL/uTP                             |
+----------+-------------------------------------+

``ip`` is the IP address and port the connection came from.

state_update_alert
------------------

This alert is only posted when requested by the user, by calling `post_torrent_updates()`_
on the session. It contains the torrent status of all torrents that changed
since last time this message was posted. Its category is ``status_notification``, but
it's not subject to filtering, since it's only manually posted anyway.

::

	
	struct state_update_alert: alert
	{
		// ...
		std::vector<torrent_status> status;
	};

``status`` contains the torrent status of all torrents that changed since last time
this message was posted. Note that you can map a torrent status to a specific torrent
via its ``handle`` member. The receiving end is suggested to have all torrents sorted
by the ``torrent_handle`` or hashed by it, for efficient updates.


alert dispatcher
================

The ``handle_alert`` class is defined in ``<libtorrent/alert.hpp>``.

Examples usage::

	struct my_handler
	{
		void operator()(portmap_error_alert const& a) const
		{
			std::cout << "Portmapper: " << a.msg << std::endl;
		}

		void operator()(tracker_warning_alert const& a) const
		{
			std::cout << "Tracker warning: " << a.msg << std::endl;
		}

		void operator()(torrent_finished_alert const& a) const
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

Many functions in libtorrent have two versions, one that throws exceptions on
errors and one that takes an ``error_code`` reference which is filled with the
error code on errors.

There is one exception class that is used for errors in libtorrent, it is based
on boost.system's ``error_code`` class to carry the error code.

libtorrent_exception
--------------------

::

	struct libtorrent_exception: std::exception
	{
		libtorrent_exception(error_code const& s);
		virtual const char* what() const throw();
		virtual ~libtorrent_exception() throw() {}
		boost::system::error_code error() const;
	};


error_code
==========

libtorrent uses boost.system's ``error_code`` class to represent errors. libtorrent has
its own error category (``libtorrent::get_libtorrent_category()``) whith the following error
codes:

====== ========================================= =================================================================
code   symbol                                    description
====== ========================================= =================================================================
0      no_error                                  Not an error
------ ----------------------------------------- -----------------------------------------------------------------
1      file_collision                            Two torrents has files which end up overwriting each other
------ ----------------------------------------- -----------------------------------------------------------------
2      failed_hash_check                         A piece did not match its piece hash
------ ----------------------------------------- -----------------------------------------------------------------
3      torrent_is_no_dict                        The .torrent file does not contain a bencoded dictionary at
                                                 its top level
------ ----------------------------------------- -----------------------------------------------------------------
4      torrent_missing_info                      The .torrent file does not have an ``info`` dictionary
------ ----------------------------------------- -----------------------------------------------------------------
5      torrent_info_no_dict                      The .torrent file's ``info`` entry is not a dictionary
------ ----------------------------------------- -----------------------------------------------------------------
6      torrent_missing_piece_length              The .torrent file does not have a ``piece length`` entry
------ ----------------------------------------- -----------------------------------------------------------------
7      torrent_missing_name                      The .torrent file does not have a ``name`` entry
------ ----------------------------------------- -----------------------------------------------------------------
8      torrent_invalid_name                      The .torrent file's name entry is invalid
------ ----------------------------------------- -----------------------------------------------------------------
9      torrent_invalid_length                    The length of a file, or of the whole .torrent file is invalid.
                                                 Either negative or not an integer
------ ----------------------------------------- -----------------------------------------------------------------
10     torrent_file_parse_failed                 Failed to parse a file entry in the .torrent
------ ----------------------------------------- -----------------------------------------------------------------
11     torrent_missing_pieces                    The ``pieces`` field is missing or invalid in the .torrent file
------ ----------------------------------------- -----------------------------------------------------------------
12     torrent_invalid_hashes                    The ``pieces`` string has incorrect length
------ ----------------------------------------- -----------------------------------------------------------------
13     too_many_pieces_in_torrent                The .torrent file has more pieces than is supported by libtorrent
------ ----------------------------------------- -----------------------------------------------------------------
14     invalid_swarm_metadata                    The metadata (.torrent file) that was received from the swarm
                                                 matched the info-hash, but failed to be parsed
------ ----------------------------------------- -----------------------------------------------------------------
15     invalid_bencoding                         The file or buffer is not correctly bencoded
------ ----------------------------------------- -----------------------------------------------------------------
16     no_files_in_torrent                       The .torrent file does not contain any files
------ ----------------------------------------- -----------------------------------------------------------------
17     invalid_escaped_string                    The string was not properly url-encoded as expected
------ ----------------------------------------- -----------------------------------------------------------------
18     session_is_closing                        Operation is not permitted since the session is shutting down
------ ----------------------------------------- -----------------------------------------------------------------
19     duplicate_torrent                         There's already a torrent with that info-hash added to the
                                                 session
------ ----------------------------------------- -----------------------------------------------------------------
20     invalid_torrent_handle                    The supplied torrent_handle is not referring to a valid torrent
------ ----------------------------------------- -----------------------------------------------------------------
21     invalid_entry_type                        The type requested from the entry did not match its type
------ ----------------------------------------- -----------------------------------------------------------------
22     missing_info_hash_in_uri                  The specified URI does not contain a valid info-hash
------ ----------------------------------------- -----------------------------------------------------------------
23     file_too_short                            One of the files in the torrent was unexpectadly small. This
                                                 might be caused by files being changed by an external process
------ ----------------------------------------- -----------------------------------------------------------------
24     unsupported_url_protocol                  The URL used an unknown protocol. Currently ``http`` and
                                                 ``https`` (if built with openssl support) are recognized. For
                                                 trackers ``udp`` is recognized as well.
------ ----------------------------------------- -----------------------------------------------------------------
25     url_parse_error                           The URL did not conform to URL syntax and failed to be parsed
------ ----------------------------------------- -----------------------------------------------------------------
26     peer_sent_empty_piece                     The peer sent a 'piece' message of length 0
------ ----------------------------------------- -----------------------------------------------------------------
27     parse_failed                              A bencoded structure was currupt and failed to be parsed
------ ----------------------------------------- -----------------------------------------------------------------
28     invalid_file_tag                          The fast resume file was missing or had an invalid file version
                                                 tag
------ ----------------------------------------- -----------------------------------------------------------------
29     missing_info_hash                         The fast resume file was missing or had an invalid info-hash
------ ----------------------------------------- -----------------------------------------------------------------
30     mismatching_info_hash                     The info-hash in the resume file did not match the torrent
------ ----------------------------------------- -----------------------------------------------------------------
31     invalid_hostname                          The URL contained an invalid hostname
------ ----------------------------------------- -----------------------------------------------------------------
32     invalid_port                              The URL had an invalid port
------ ----------------------------------------- -----------------------------------------------------------------
33     port_blocked                              The port is blocked by the port-filter, and prevented the
                                                 connection
------ ----------------------------------------- -----------------------------------------------------------------
34     expected_close_bracket_in_address         The IPv6 address was expected to end with ']'
------ ----------------------------------------- -----------------------------------------------------------------
35     destructing_torrent                       The torrent is being destructed, preventing the operation to
                                                 succeed
------ ----------------------------------------- -----------------------------------------------------------------
36     timed_out                                 The connection timed out
------ ----------------------------------------- -----------------------------------------------------------------
37     upload_upload_connection                  The peer is upload only, and we are upload only. There's no point
                                                 in keeping the connection
------ ----------------------------------------- -----------------------------------------------------------------
38     uninteresting_upload_peer                 The peer is upload only, and we're not interested in it. There's
                                                 no point in keeping the connection
------ ----------------------------------------- -----------------------------------------------------------------
39     invalid_info_hash                         The peer sent an unknown info-hash
------ ----------------------------------------- -----------------------------------------------------------------
40     torrent_paused                            The torrent is paused, preventing the operation from succeeding
------ ----------------------------------------- -----------------------------------------------------------------
41     invalid_have                              The peer sent an invalid have message, either wrong size or
                                                 referring to a piece that doesn't exist in the torrent
------ ----------------------------------------- -----------------------------------------------------------------
42     invalid_bitfield_size                     The bitfield message had the incorrect size
------ ----------------------------------------- -----------------------------------------------------------------
43     too_many_requests_when_choked             The peer kept requesting pieces after it was choked, possible
                                                 abuse attempt.
------ ----------------------------------------- -----------------------------------------------------------------
44     invalid_piece                             The peer sent a piece message that does not correspond to a
                                                 piece request sent by the client
------ ----------------------------------------- -----------------------------------------------------------------
45     no_memory                                 memory allocation failed
------ ----------------------------------------- -----------------------------------------------------------------
46     torrent_aborted                           The torrent is aborted, preventing the operation to succeed
------ ----------------------------------------- -----------------------------------------------------------------
47     self_connection                           The peer is a connection to ourself, no point in keeping it
------ ----------------------------------------- -----------------------------------------------------------------
48     invalid_piece_size                        The peer sent a piece message with invalid size, either negative
                                                 or greater than one block
------ ----------------------------------------- -----------------------------------------------------------------
49     timed_out_no_interest                     The peer has not been interesting or interested in us for too
                                                 long, no point in keeping it around
------ ----------------------------------------- -----------------------------------------------------------------
50     timed_out_inactivity                      The peer has not said anything in a long time, possibly dead
------ ----------------------------------------- -----------------------------------------------------------------
51     timed_out_no_handshake                    The peer did not send a handshake within a reasonable amount of
                                                 time, it might not be a bittorrent peer
------ ----------------------------------------- -----------------------------------------------------------------
52     timed_out_no_request                      The peer has been unchoked for too long without requesting any
                                                 data. It might be lying about its interest in us
------ ----------------------------------------- -----------------------------------------------------------------
53     invalid_choke                             The peer sent an invalid choke message
------ ----------------------------------------- -----------------------------------------------------------------
54     invalid_unchoke                           The peer send an invalid unchoke message
------ ----------------------------------------- -----------------------------------------------------------------
55     invalid_interested                        The peer sent an invalid interested message
------ ----------------------------------------- -----------------------------------------------------------------
56     invalid_not_interested                    The peer sent an invalid not-interested message
------ ----------------------------------------- -----------------------------------------------------------------
57     invalid_request                           The peer sent an invalid piece request message
------ ----------------------------------------- -----------------------------------------------------------------
58     invalid_hash_list                         The peer sent an invalid hash-list message (this is part of the
                                                 merkle-torrent extension)
------ ----------------------------------------- -----------------------------------------------------------------
59     invalid_hash_piece                        The peer sent an invalid hash-piece message (this is part of the
                                                 merkle-torrent extension)
------ ----------------------------------------- -----------------------------------------------------------------
60     invalid_cancel                            The peer sent an invalid cancel message
------ ----------------------------------------- -----------------------------------------------------------------
61     invalid_dht_port                          The peer sent an invalid DHT port-message
------ ----------------------------------------- -----------------------------------------------------------------
62     invalid_suggest                           The peer sent an invalid suggest piece-message
------ ----------------------------------------- -----------------------------------------------------------------
63     invalid_have_all                          The peer sent an invalid have all-message
------ ----------------------------------------- -----------------------------------------------------------------
64     invalid_have_none                         The peer sent an invalid have none-message
------ ----------------------------------------- -----------------------------------------------------------------
65     invalid_reject                            The peer sent an invalid reject message
------ ----------------------------------------- -----------------------------------------------------------------
66     invalid_allow_fast                        The peer sent an invalid allow fast-message
------ ----------------------------------------- -----------------------------------------------------------------
67     invalid_extended                          The peer sent an invalid extesion message ID
------ ----------------------------------------- -----------------------------------------------------------------
68     invalid_message                           The peer sent an invalid message ID
------ ----------------------------------------- -----------------------------------------------------------------
69     sync_hash_not_found                       The synchronization hash was not found in the encrypted handshake
------ ----------------------------------------- -----------------------------------------------------------------
70     invalid_encryption_constant               The encryption constant in the handshake is invalid
------ ----------------------------------------- -----------------------------------------------------------------
71     no_plaintext_mode                         The peer does not support plaintext, which is the selected mode
------ ----------------------------------------- -----------------------------------------------------------------
72     no_rc4_mode                               The peer does not support rc4, which is the selected mode
------ ----------------------------------------- -----------------------------------------------------------------
73     unsupported_encryption_mode               The peer does not support any of the encryption modes that the
                                                 client supports
------ ----------------------------------------- -----------------------------------------------------------------
74     unsupported_encryption_mode_selected      The peer selected an encryption mode that the client did not
                                                 advertise and does not support
------ ----------------------------------------- -----------------------------------------------------------------
75     invalid_pad_size                          The pad size used in the encryption handshake is of invalid size
------ ----------------------------------------- -----------------------------------------------------------------
76     invalid_encrypt_handshake                 The encryption handshake is invalid
------ ----------------------------------------- -----------------------------------------------------------------
77     no_incoming_encrypted                     The client is set to not support incoming encrypted connections
                                                 and this is an encrypted connection
------ ----------------------------------------- -----------------------------------------------------------------
78     no_incoming_regular                       The client is set to not support incoming regular bittorrent
                                                 connections, and this is a regular connection
------ ----------------------------------------- -----------------------------------------------------------------
79     duplicate_peer_id                         The client is already connected to this peer-ID
------ ----------------------------------------- -----------------------------------------------------------------
80     torrent_removed                           Torrent was removed
------ ----------------------------------------- -----------------------------------------------------------------
81     packet_too_large                          The packet size exceeded the upper sanity check-limit
------ ----------------------------------------- -----------------------------------------------------------------
82     reserved
------ ----------------------------------------- -----------------------------------------------------------------
83     http_error                                The web server responded with an error
------ ----------------------------------------- -----------------------------------------------------------------
84     missing_location                          The web server response is missing a location header
------ ----------------------------------------- -----------------------------------------------------------------
85     invalid_redirection                       The web seed redirected to a path that no longer matches the
                                                 .torrent directory structure
------ ----------------------------------------- -----------------------------------------------------------------
86     redirecting                               The connection was closed becaused it redirected to a different
                                                 URL
------ ----------------------------------------- -----------------------------------------------------------------
87     invalid_range                             The HTTP range header is invalid
------ ----------------------------------------- -----------------------------------------------------------------
88     no_content_length                         The HTTP response did not have a content length
------ ----------------------------------------- -----------------------------------------------------------------
89     banned_by_ip_filter                       The IP is blocked by the IP filter
------ ----------------------------------------- -----------------------------------------------------------------
90     too_many_connections                      At the connection limit
------ ----------------------------------------- -----------------------------------------------------------------
91     peer_banned                               The peer is marked as banned
------ ----------------------------------------- -----------------------------------------------------------------
92     stopping_torrent                          The torrent is stopping, causing the operation to fail
------ ----------------------------------------- -----------------------------------------------------------------
93     too_many_corrupt_pieces                   The peer has sent too many corrupt pieces and is banned
------ ----------------------------------------- -----------------------------------------------------------------
94     torrent_not_ready                         The torrent is not ready to receive peers
------ ----------------------------------------- -----------------------------------------------------------------
95     peer_not_constructed                      The peer is not completely constructed yet
------ ----------------------------------------- -----------------------------------------------------------------
96     session_closing                           The session is closing, causing the operation to fail
------ ----------------------------------------- -----------------------------------------------------------------
97     optimistic_disconnect                     The peer was disconnected in order to leave room for a
                                                 potentially better peer
------ ----------------------------------------- -----------------------------------------------------------------
98     torrent_finished                          The torrent is finished
------ ----------------------------------------- -----------------------------------------------------------------
99     no_router                                 No UPnP router found
------ ----------------------------------------- -----------------------------------------------------------------
100    metadata_too_large                        The metadata message says the metadata exceeds the limit
------ ----------------------------------------- -----------------------------------------------------------------
101    invalid_metadata_request                  The peer sent an invalid metadata request message
------ ----------------------------------------- -----------------------------------------------------------------
102    invalid_metadata_size                     The peer advertised an invalid metadata size
------ ----------------------------------------- -----------------------------------------------------------------
103    invalid_metadata_offset                   The peer sent a message with an invalid metadata offset
------ ----------------------------------------- -----------------------------------------------------------------
104    invalid_metadata_message                  The peer sent an invalid metadata message
------ ----------------------------------------- -----------------------------------------------------------------
105    pex_message_too_large                     The peer sent a peer exchange message that was too large
------ ----------------------------------------- -----------------------------------------------------------------
106    invalid_pex_message                       The peer sent an invalid peer exchange message
------ ----------------------------------------- -----------------------------------------------------------------
107    invalid_lt_tracker_message                The peer sent an invalid tracker exchange message
------ ----------------------------------------- -----------------------------------------------------------------
108    too_frequent_pex                          The peer sent an pex messages too often. This is a possible
                                                 attempt of and attack
------ ----------------------------------------- -----------------------------------------------------------------
109    no_metadata                               The operation failed because it requires the torrent to have
                                                 the metadata (.torrent file) and it doesn't have it yet.
                                                 This happens for magnet links before they have downloaded the
                                                 metadata, and also torrents added by URL.
------ ----------------------------------------- -----------------------------------------------------------------
110    invalid_dont_have                         The peer sent an invalid ``dont_have`` message. The dont have
                                                 message is an extension to allow peers to advertise that the
                                                 no longer has a piece they previously had.                      
------ ----------------------------------------- -----------------------------------------------------------------
111    requires_ssl_connection                   The peer tried to connect to an SSL torrent without connecting
                                                 over SSL.
------ ----------------------------------------- -----------------------------------------------------------------
112    invalid_ssl_cert                          The peer tried to connect to a torrent with a certificate
                                                 for a different torrent.
====== ========================================= =================================================================

NAT-PMP errors:

====== ========================================= =================================================================
code   symbol                                    description
====== ========================================= =================================================================
120    unsupported_protocol_version              The NAT-PMP router responded with an unsupported protocol version
------ ----------------------------------------- -----------------------------------------------------------------
121    natpmp_not_authorized                     You are not authorized to map ports on this NAT-PMP router
------ ----------------------------------------- -----------------------------------------------------------------
122    network_failure                           The NAT-PMP router failed because of a network failure
------ ----------------------------------------- -----------------------------------------------------------------
123    no_resources                              The NAT-PMP router failed because of lack of resources
------ ----------------------------------------- -----------------------------------------------------------------
124    unsupported_opcode                        The NAT-PMP router failed because an unsupported opcode was sent
====== ========================================= =================================================================

fastresume data errors:

====== ========================================= =================================================================
code   symbol                                    description
====== ========================================= =================================================================
130    missing_file_sizes                        The resume data file is missing the 'file sizes' entry
------ ----------------------------------------- -----------------------------------------------------------------
131    no_files_in_resume_data                   The resume data file 'file sizes' entry is empty
------ ----------------------------------------- -----------------------------------------------------------------
132    missing_pieces                            The resume data file is missing the 'pieces' and 'slots' entry
------ ----------------------------------------- -----------------------------------------------------------------
133    mismatching_number_of_files               The number of files in the resume data does not match the number
                                                 of files in the torrent
------ ----------------------------------------- -----------------------------------------------------------------
134    mismatching_files_size                    One of the files on disk has a different size than in the fast
                                                 resume file
------ ----------------------------------------- -----------------------------------------------------------------
135    mismatching_file_timestamp                One of the files on disk has a different timestamp than in the
                                                 fast resume file
------ ----------------------------------------- -----------------------------------------------------------------
136    not_a_dictionary                          The resume data file is not a dictionary
------ ----------------------------------------- -----------------------------------------------------------------
137    invalid_blocks_per_piece                  The 'blocks per piece' entry is invalid in the resume data file
------ ----------------------------------------- -----------------------------------------------------------------
138    missing_slots                             The resume file is missing the 'slots' entry, which is required
                                                 for torrents with compact allocation
------ ----------------------------------------- -----------------------------------------------------------------
139    too_many_slots                            The resume file contains more slots than the torrent
------ ----------------------------------------- -----------------------------------------------------------------
140    invalid_slot_list                         The 'slot' entry is invalid in the resume data
------ ----------------------------------------- -----------------------------------------------------------------
141    invalid_piece_index                       One index in the 'slot' list is invalid
------ ----------------------------------------- -----------------------------------------------------------------
142    pieces_need_reorder                       The pieces on disk needs to be re-ordered for the specified
                                                 allocation mode. This happens if you specify sparse allocation
                                                 and the files on disk are using compact storage. The pieces needs
                                                 to be moved to their right position
====== ========================================= =================================================================

HTTP errors:

====== ========================================= =================================================================
150    http_parse_error                          The HTTP header was not correctly formatted
------ ----------------------------------------- -----------------------------------------------------------------
151    http_missing_location                     The HTTP response was in the 300-399 range but lacked a location
                                                 header
------ ----------------------------------------- -----------------------------------------------------------------
152    http_failed_decompress                    The HTTP response was encoded with gzip or deflate but
                                                 decompressing it failed
====== ========================================= =================================================================

I2P errors:

====== ========================================= =================================================================
160    no_i2p_router                             The URL specified an i2p address, but no i2p router is configured
====== ========================================= =================================================================

tracker errors:

====== ========================================= =================================================================
170    scrape_not_available                      The tracker URL doesn't support transforming it into a scrape
                                                 URL. i.e. it doesn't contain "announce.
------ ----------------------------------------- -----------------------------------------------------------------
171    invalid_tracker_response                  invalid tracker response
------ ----------------------------------------- -----------------------------------------------------------------
172    invalid_peer_dict                         invalid peer dictionary entry. Not a dictionary
------ ----------------------------------------- -----------------------------------------------------------------
173    tracker_failure                           tracker sent a failure message
------ ----------------------------------------- -----------------------------------------------------------------
174    invalid_files_entry                       missing or invalid 'files' entry
------ ----------------------------------------- -----------------------------------------------------------------
175    invalid_hash_entry                        missing or invalid 'hash' entry
------ ----------------------------------------- -----------------------------------------------------------------
176    invalid_peers_entry                       missing or invalid 'peers' and 'peers6' entry
------ ----------------------------------------- -----------------------------------------------------------------
177    invalid_tracker_response_length           udp tracker response packet has invalid size
------ ----------------------------------------- -----------------------------------------------------------------
178    invalid_tracker_transaction_id            invalid transaction id in udp tracker response
------ ----------------------------------------- -----------------------------------------------------------------
179    invalid_tracker_action                    invalid action field in udp tracker response
------ ----------------------------------------- -----------------------------------------------------------------
190    expected_string                           expected string in bencoded string
------ ----------------------------------------- -----------------------------------------------------------------
191    expected_colon                            expected colon in bencoded string
------ ----------------------------------------- -----------------------------------------------------------------
192    unexpected_eof                            unexpected end of file in bencoded string
------ ----------------------------------------- -----------------------------------------------------------------
193    expected_value                            expected value (list, dict, int or string) in bencoded string
------ ----------------------------------------- -----------------------------------------------------------------
194    depth_exceeded                            bencoded recursion depth limit exceeded
------ ----------------------------------------- -----------------------------------------------------------------
195    item_limit_exceeded                       bencoded item count limit exceeded
====== ========================================= =================================================================

The names of these error codes are declared in then ``libtorrent::errors`` namespace.

There is also another error category, ``libtorrent::upnp_category``, defining errors
retrned by UPnP routers. Here's a (possibly incomplete) list of UPnP error codes:

====== ========================================= ====================================================
code   symbol                                    description
====== ========================================= ====================================================
0      no_error                                  No error
------ ----------------------------------------- ----------------------------------------------------
402    invalid_argument                          One of the arguments in the request is invalid
------ ----------------------------------------- ----------------------------------------------------
501    action_failed                             The request failed
------ ----------------------------------------- ----------------------------------------------------
714    value_not_in_array                        The specified value does not exist in the array
------ ----------------------------------------- ----------------------------------------------------
715    source_ip_cannot_be_wildcarded            The source IP address cannot be wild-carded, but
                                                 must be fully specified
------ ----------------------------------------- ----------------------------------------------------
716    external_port_cannot_be_wildcarded        The external port cannot be wildcarded, but must
                                                 be specified
------ ----------------------------------------- ----------------------------------------------------
718    port_mapping_conflict                     The port mapping entry specified conflicts with a
                                                 mapping assigned previously to another client
------ ----------------------------------------- ----------------------------------------------------
724    internal_port_must_match_external         Internal and external port value must be the same
------ ----------------------------------------- ----------------------------------------------------
725    only_permanent_leases_supported           The NAT implementation only supports permanent
                                                 lease times on port mappings
------ ----------------------------------------- ----------------------------------------------------
726    remote_host_must_be_wildcard              RemoteHost must be a wildcard and cannot be a
                                                 specific IP addres or DNS name
------ ----------------------------------------- ----------------------------------------------------
727    external_port_must_be_wildcard            ExternalPort must be a wildcard and cannot be a
                                                 specific port
====== ========================================= ====================================================

The UPnP errors are declared in the ``libtorrent::upnp_errors`` namespace.

HTTP errors are reported in the ``libtorrent::http_category``, with error code enums in
the ``libtorrent::errors`` namespace.

====== =========================================
code   symbol                                   
====== =========================================
100    cont                                     
------ -----------------------------------------
200    ok                                       
------ -----------------------------------------
201    created                                  
------ -----------------------------------------
202    accepted                                 
------ -----------------------------------------
204    no_content                               
------ -----------------------------------------
300    multiple_choices                         
------ -----------------------------------------
301    moved_permanently                        
------ -----------------------------------------
302    moved_temporarily                        
------ -----------------------------------------
304    not_modified                             
------ -----------------------------------------
400    bad_request                              
------ -----------------------------------------
401    unauthorized                             
------ -----------------------------------------
403    forbidden                                
------ -----------------------------------------
404    not_found                                
------ -----------------------------------------
500    internal_server_error                    
------ -----------------------------------------
501    not_implemented                          
------ -----------------------------------------
502    bad_gateway                              
------ -----------------------------------------
503    service_unavailable                      
====== =========================================

translating error codes
-----------------------

The error_code::message() function will typically return a localized error string,
for system errors. That is, errors that belong to the generic or system category.

Errors that belong to the libtorrent error category are not localized however, they
are only available in english. In order to translate libtorrent errors, compare the
error category of the ``error_code`` object against ``libtorrent::get_libtorrent_category()``,
and if matches, you know the error code refers to the list above. You can provide
your own mapping from error code to string, which is localized. In this case, you
cannot rely on ``error_code::message()`` to generate your strings.

The numeric values of the errors are part of the API and will stay the same, although
new error codes may be appended at the end.

Here's a simple example of how to translate error codes::

	std::string error_code_to_string(boost::system::error_code const& ec)
	{
		if (ec.category() != libtorrent::get_libtorrent_category())
		{
			return ec.message();
		}
		// the error is a libtorrent error

		int code = ec.value();
		static const char const* swedish[] =
		{
			"inget fel",
			"en fil i torrenten kolliderar med en fil från en annan torrent",
			"hash check misslyckades",
			"torrent filen är inte en dictionary",
			"'info'-nyckeln saknas eller är korrupt i torrentfilen",
			"'info'-fältet är inte en dictionary",
			"'piece length' fältet saknas eller är korrupt i torrentfilen",
			"torrentfilen saknar namnfältet",
			"ogiltigt namn i torrentfilen (kan vara en attack)",
			// ... more strings here
		};

		// use the default error string in case we don't have it
		// in our translated list
		if (code < 0 || code >= sizeof(swedish)/sizeof(swedish[0]))
			return ec.message();

		return swedish[code];
	}

storage_interface
=================

The storage interface is a pure virtual class that can be implemented to
customize how and where data for a torrent is stored. The default storage
implementation uses regular files in the filesystem, mapping the files in the
torrent in the way one would assume a torrent is saved to disk. Implementing
your own storage interface makes it possible to store all data in RAM, or in
some optimized order on disk (the order the pieces are received for instance),
or saving multifile torrents in a single file in order to be able to take
advantage of optimized disk-I/O.

It is also possible to write a thin class that uses the default storage but
modifies some particular behavior, for instance encrypting the data before
it's written to disk, and decrypting it when it's read again.

The storage interface is based on slots, each slot is 'piece_size' number
of bytes. All access is done by writing and reading whole or partial
slots. One slot is one piece in the torrent, but the data in the slot
does not necessarily correspond to the piece with the same index (in
compact allocation mode it won't).

libtorrent comes with two built-in storage implementations; ``default_storage``
and ``disabled_storage``. Their constructor functions are called ``default_storage_constructor``
and ``disabled_storage_constructor`` respectively. The disabled storage does
just what it sounds like. It throws away data that's written, and it
reads garbage. It's useful mostly for benchmarking and profiling purpose.


The interface looks like this::

	struct storage_interface
	{
		virtual bool initialize(bool allocate_files) = 0;
		virtual bool has_any_file() = 0;
		virtual void hint_read(int slot, int offset, int len);
		virtual int readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs) = 0;
		virtual int writev(file::iovec_t const* bufs, int slot, int offset, int num_bufs) = 0;
		virtual int sparse_end(int start) const;
		virtual bool move_storage(fs::path save_path) = 0;
		virtual bool verify_resume_data(lazy_entry const& rd, error_code& error) = 0;
		virtual bool write_resume_data(entry& rd) const = 0;
		virtual bool move_slot(int src_slot, int dst_slot) = 0;
		virtual bool swap_slots(int slot1, int slot2) = 0;
		virtual bool swap_slots3(int slot1, int slot2, int slot3) = 0;
		virtual bool rename_file(int file, std::string const& new_name) = 0;
		virtual bool release_files() = 0;
		virtual bool delete_files() = 0;
		virtual void finalize_file(int index) {}
		virtual ~storage_interface() {}

		// non virtual functions

		disk_buffer_pool* disk_pool();
		void set_error(std::string const& file, error_code const& ec) const;
		error_code const& error() const;
		std::string const& error_file() const;
		void clear_error();
	};


initialize()
------------

	::

		bool initialize(bool allocate_files) = 0;

This function is called when the storage is to be initialized. The default storage
will create directories and empty files at this point. If ``allocate_files`` is true,
it will also ``ftruncate`` all files to their target size.

Returning ``true`` indicates an error occurred.

has_any_file()
--------------

	::

		virtual bool has_any_file() = 0;

This function is called when first checking (or re-checking) the storage for a torrent.
It should return true if any of the files that is used in this storage exists on disk.
If so, the storage will be checked for existing pieces before starting the download.

hint_read()
-----------

	::

		void hint_read(int slot, int offset, int len);

This function is called when a read job is queued. It gives the storage wrapper an
opportunity to hint the operating system about this coming read. For instance, the
storage may call ``posix_fadvise(POSIX_FADV_WILLNEED)`` or ``fcntl(F_RDADVISE)``.

readv() writev()
----------------

	::

		int readv(file::iovec_t const* buf, int slot, int offset, int num_bufs) = 0;
		int write(const char* buf, int slot, int offset, int size) = 0;

These functions should read or write the data in or to the given ``slot`` at the given ``offset``.
It should read or write ``num_bufs`` buffers sequentially, where the size of each buffer
is specified in the buffer array ``bufs``. The file::iovec_t type has the following members::

	struct iovec_t
	{
		void* iov_base;
		size_t iov_len;
	};

The return value is the number of bytes actually read or written, or -1 on failure. If
it returns -1, the error code is expected to be set to

Every buffer in ``bufs`` can be assumed to be page aligned and be of a page aligned size,
except for the last buffer of the torrent. The allocated buffer can be assumed to fit a
fully page aligned number of bytes though. This is useful when reading and writing the
last piece of a file in unbuffered mode.

The ``offset`` is aligned to 16 kiB boundries  *most of the time*, but there are rare
exceptions when it's not. Specifically if the read cache is disabled/or full and a
client requests unaligned data, or the file itself is not aligned in the torrent.
Most clients request aligned data.

sparse_end()
------------

	::

		int sparse_end(int start) const;

This function is optional. It is supposed to return the first piece, starting at
``start`` that is fully contained within a data-region on disk (i.e. non-sparse
region). The purpose of this is to skip parts of files that can be known to contain
zeros when checking files.

move_storage()
--------------

	::

		bool move_storage(fs::path save_path) = 0;

This function should move all the files belonging to the storage to the new save_path.
The default storage moves the single file or the directory of the torrent.

Before moving the files, any open file handles may have to be closed, like
``release_files()``.

Returning ``false`` indicates an error occurred.


verify_resume_data()
--------------------

	::

		bool verify_resume_data(lazy_entry const& rd, error_code& error) = 0;

This function should verify the resume data ``rd`` with the files
on disk. If the resume data seems to be up-to-date, return true. If
not, set ``error`` to a description of what mismatched and return false.

The default storage may compare file sizes and time stamps of the files.

Returning ``false`` indicates an error occurred.


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

The ``disk_buffer_pool`` is used to allocate and free disk buffers. It has the
following members::

	struct disk_buffer_pool : boost::noncopyable
	{
		char* allocate_buffer(char const* category);
		void free_buffer(char* buf);

		char* allocate_buffers(int blocks, char const* category);
		void free_buffers(char* buf, int blocks);

		int block_size() const { return m_block_size; }

		void release_memory();
	};

finalize_file()
---------------

	::

		virtual void finalize_file(int index);

This function is called each time a file is completely downloaded. The
storage implementation can perform last operations on a file. The file will
not be opened for writing after this.

``index`` is the index of the file that completed.

On windows the default storage implementation clears the sparse file flag
on the specified file.

example
-------

This is an example storage implementation that stores all pieces in a ``std::map``,
i.e. in RAM. It's not necessarily very useful in practice, but illustrates the
basics of implementing a custom storage.

::

	struct temp_storage : storage_interface
	{
		temp_storage(file_storage const& fs) : m_files(fs) {}
		virtual bool initialize(bool allocate_files) { return false; }
		virtual bool has_any_file() { return false; }
		virtual int read(char* buf, int slot, int offset, int size)
		{
			std::map<int, std::vector<char> >::const_iterator i = m_file_data.find(slot);
			if (i == m_file_data.end()) return 0;
			int available = i->second.size() - offset;
			if (available <= 0) return 0;
			if (available > size) available = size;
			memcpy(buf, &i->second[offset], available);
			return available;
		}
		virtual int write(const char* buf, int slot, int offset, int size)
		{
			std::vector<char>& data = m_file_data[slot];
			if (data.size() < offset + size) data.resize(offset + size);
			std::memcpy(&data[offset], buf, size);
			return size;
		}
		virtual bool rename_file(int file, std::string const& new_name)
		{ assert(false); return false; }
		virtual bool move_storage(std::string const& save_path) { return false; }
		virtual bool verify_resume_data(lazy_entry const& rd, error_code& error) { return false; }
		virtual bool write_resume_data(entry& rd) const { return false; }
		virtual bool move_slot(int src_slot, int dst_slot) { assert(false); return false; }
		virtual bool swap_slots(int slot1, int slot2) { assert(false); return false; }
		virtual bool swap_slots3(int slot1, int slot2, int slot3) { assert(false); return false; }
		virtual size_type physical_offset(int slot, int offset)
		{ return slot * m_files.piece_length() + offset; };
		virtual sha1_hash hash_for_slot(int slot, partial_hash& ph, int piece_size)
		{
			int left = piece_size - ph.offset;
			assert(left >= 0);
			if (left > 0)
			{
				std::vector<char>& data = m_file_data[slot];
				// if there are padding files, those blocks will be considered
				// completed even though they haven't been written to the storage.
				// in this case, just extend the piece buffer to its full size
				// and fill it with zeroes.
				if (data.size() < piece_size) data.resize(piece_size, 0);
				ph.h.update(&data[ph.offset], left);
			}
			return ph.h.final();
		}
		virtual bool release_files() { return false; }
		virtual bool delete_files() { return false; }
	
		std::map<int, std::vector<char> > m_file_data;
		file_storage m_files;
	};

	storage_interface* temp_storage_constructor(
		file_storage const& fs, file_storage const* mapped
		, std::string const& path, file_pool& fp
		, std::vector<boost::uint8_t> const& prio)
	{
		return new temp_storage(fs);
	}

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
torrent (see `async_add_torrent() add_torrent()`_).

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

To use the fast-resume data you simply give it to `async_add_torrent() add_torrent()`_, and it
will skip the time consuming checks. It may have to do the checking anyway, if
the fast-resume data is corrupt or doesn't fit the storage for that torrent,
then it will not trust the fast-resume data and just do the checking.

file format
-----------

The file format is a bencoded dictionary containing the following fields:

+--------------------------+--------------------------------------------------------------+
| ``file-format``          | string: "libtorrent resume file"                             |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``file-version``         | integer: 1                                                   |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``info-hash``            | string, the info hash of the torrent this data is saved for. |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``blocks per piece``     | integer, the number of blocks per piece. Must be: piece_size |
|                          | / (16 * 1024). Clamped to be within the range [1, 256]. It   |
|                          | is the number of blocks per (normal sized) piece. Usually    |
|                          | each block is 16 * 1024 bytes in size. But if piece size is  |
|                          | greater than 4 megabytes, the block size will increase.      |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``pieces``               | A string with piece flags, one character per piece.          |
|                          | Bit 1 means we have that piece.                              |
|                          | Bit 2 means we have verified that this piece is correct.     |
|                          | This only applies when the torrent is in seed_mode.          |
+--------------------------+--------------------------------------------------------------+
| ``slots``                | list of integers. The list maps slots to piece indices. It   |
|                          | tells which piece is on which slot. If piece index is -2 it  |
|                          | means it is free, that there's no piece there. If it is -1,  |
|                          | means the slot isn't allocated on disk yet. The pieces have  |
|                          | to meet the following requirement:                           |
|                          |                                                              |
|                          | If there's a slot at the position of the piece index,        |
|                          | the piece must be located in that slot.                      |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``total_uploaded``       | integer. The number of bytes that have been uploaded in      |
|                          | total for this torrent.                                      |
+--------------------------+--------------------------------------------------------------+
| ``total_downloaded``     | integer. The number of bytes that have been downloaded in    |
|                          | total for this torrent.                                      |
+--------------------------+--------------------------------------------------------------+
| ``active_time``          | integer. The number of seconds this torrent has been active. |
|                          | i.e. not paused.                                             |
+--------------------------+--------------------------------------------------------------+
| ``seeding_time``         | integer. The number of seconds this torrent has been active  |
|                          | and seeding.                                                 |
+--------------------------+--------------------------------------------------------------+
| ``num_seeds``            | integer. An estimate of the number of seeds on this torrent  |
|                          | when the resume data was saved. This is scrape data or based |
|                          | on the peer list if scrape data is unavailable.              |
+--------------------------+--------------------------------------------------------------+
| ``num_downloaders``      | integer. An estimate of the number of downloaders on this    |
|                          | torrent when the resume data was last saved. This is used as |
|                          | an initial estimate until we acquire up-to-date scrape info. |
+--------------------------+--------------------------------------------------------------+
| ``upload_rate_limit``    | integer. In case this torrent has a per-torrent upload rate  |
|                          | limit, this is that limit. In bytes per second.              |
+--------------------------+--------------------------------------------------------------+
| ``download_rate_limit``  | integer. The download rate limit for this torrent in case    |
|                          | one is set, in bytes per second.                             |
+--------------------------+--------------------------------------------------------------+
| ``max_connections``      | integer. The max number of peer connections this torrent     |
|                          | may have, if a limit is set.                                 |
+--------------------------+--------------------------------------------------------------+
| ``max_uploads``          | integer. The max number of unchoked peers this torrent may   |
|                          | have, if a limit is set.                                     |
+--------------------------+--------------------------------------------------------------+
| ``seed_mode``            | integer. 1 if the torrent is in seed mode, 0 otherwise.      |
+--------------------------+--------------------------------------------------------------+
| ``file_priority``        | list of integers. One entry per file in the torrent. Each    |
|                          | entry is the priority of the file with the same index.       |
+--------------------------+--------------------------------------------------------------+
| ``piece_priority``       | string of bytes. Each byte is interpreted as an integer and  |
|                          | is the priority of that piece.                               |
+--------------------------+--------------------------------------------------------------+
| ``auto_managed``         | integer. 1 if the torrent is auto managed, otherwise 0.      |
+--------------------------+--------------------------------------------------------------+
| ``sequential_download``  | integer. 1 if the torrent is in sequential download mode,    |
|                          | 0 otherwise.                                                 |
+--------------------------+--------------------------------------------------------------+
| ``paused``               | integer. 1 if the torrent is paused, 0 otherwise.            |
+--------------------------+--------------------------------------------------------------+
| ``trackers``             | list of lists of strings. The top level list lists all       |
|                          | tracker tiers. Each second level list is one tier of         |
|                          | trackers.                                                    |
+--------------------------+--------------------------------------------------------------+
| ``mapped_files``         | list of strings. If any file in the torrent has been         |
|                          | renamed, this entry contains a list of all the filenames.    |
|                          | In the same order as in the torrent file.                    |
+--------------------------+--------------------------------------------------------------+
| ``url-list``             | list of strings. List of url-seed URLs used by this torrent. |
|                          | The urls are expected to be properly encoded and not contain |
|                          | any illegal url characters.                                  |
+--------------------------+--------------------------------------------------------------+
| ``httpseeds``            | list of strings. List of httpseed URLs used by this torrent. |
|                          | The urls are expected to be properly encoded and not contain |
|                          | any illegal url characters.                                  |
+--------------------------+--------------------------------------------------------------+
| ``merkle tree``          | string. In case this torrent is a merkle torrent, this is a  |
|                          | string containing the entire merkle tree, all nodes,         |
|                          | including the root and all leaves. The tree is not           |
|                          | necessarily complete, but complete enough to be able to send |
|                          | any piece that we have, indicated by the have bitmask.       |
+--------------------------+--------------------------------------------------------------+
| ``peers``                | list of dictionaries. Each dictionary has the following      |
|                          | layout:                                                      |
|                          |                                                              |
|                          | +----------+-----------------------------------------------+ |
|                          | | ``ip``   | string, the ip address of the peer. This is   | |
|                          | |          | not a binary representation of the ip         | |
|                          | |          | address, but the string representation. It    | |
|                          | |          | may be an IPv6 string or an IPv4 string.      | |
|                          | +----------+-----------------------------------------------+ |
|                          | | ``port`` | integer, the listen port of the peer          | |
|                          | +----------+-----------------------------------------------+ |
|                          |                                                              |
|                          | These are the local peers we were connected to when this     |
|                          | fast-resume data was saved.                                  |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``unfinished``           | list of dictionaries. Each dictionary represents an          |
|                          | piece, and has the following layout:                         |
|                          |                                                              |
|                          | +-------------+--------------------------------------------+ |
|                          | | ``piece``   | integer, the index of the piece this entry | |
|                          | |             | refers to.                                 | |
|                          | +-------------+--------------------------------------------+ |
|                          | | ``bitmask`` | string, a binary bitmask representing the  | |
|                          | |             | blocks that have been downloaded in this   | |
|                          | |             | piece.                                     | |
|                          | +-------------+--------------------------------------------+ |
|                          | | ``adler32`` | The adler32 checksum of the data in the    | |
|                          | |             | blocks specified by ``bitmask``.           | |
|                          | |             |                                            | |
|                          | +-------------+--------------------------------------------+ |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``file sizes``           | list where each entry corresponds to a file in the file list |
|                          | in the metadata. Each entry has a list of two values, the    |
|                          | first value is the size of the file in bytes, the second     |
|                          | is the time stamp when the last time someone wrote to it.    |
|                          | This information is used to compare with the files on disk.  |
|                          | All the files must match exactly this information in order   |
|                          | to consider the resume data as current. Otherwise a full     |
|                          | re-check is issued.                                          |
+--------------------------+--------------------------------------------------------------+
| ``allocation``           | The allocation mode for the storage. Can be either ``full``  |
|                          | or ``compact``. If this is full, the file sizes and          |
|                          | timestamps are disregarded. Pieces are assumed not to have   |
|                          | moved around even if the files have been modified after the  |
|                          | last resume data checkpoint.                                 |
+--------------------------+--------------------------------------------------------------+

storage allocation
==================

There are two modes in which storage (files on disk) are allocated in libtorrent.

1. The traditional *full allocation* mode, where the entire files are filled up with
   zeros before anything is downloaded. libtorrent will look for sparse files support
   in the filesystem that is used for storage, and use sparse files or file system
   zero fill support if present. This means that on NTFS, full allocation mode will
   only allocate storage for the downloaded pieces.

2. The *sparse allocation*, sparse files are used, and pieces are downloaded directly
   to where they belong. This is the recommended (and default) mode.

In previous versions of libtorrent, a 3rd mode was supported, *compact allocation*.
Support for this is deprecated and will be removed in future versions of libtorrent.
It's still described in here for completeness.

The allocation mode is selected when a torrent is started. It is passed as an
argument to ``session::add_torrent()`` (see `async_add_torrent() add_torrent()`_).

The decision to use full allocation or compact allocation typically depends on whether
any files have priority 0 and if the filesystem supports sparse files.

sparse allocation
-----------------

On filesystems that supports sparse files, this allocation mode will only use
as much space as has been downloaded.

 * It does not require an allocation pass on startup.

 * It supports skipping files (setting prioirty to 0 to not download).

 * Fast resume data will remain valid even when file time stamps are out of date.


full allocation
---------------

When a torrent is started in full allocation mode, the disk-io thread
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

 * Can be used with prioritizing files to 0.

compact allocation
------------------

Note that support for compact allocation is deprecated in libttorrent, and will
be removed in future versions.

The compact allocation will only allocate as much storage as it needs to keep the
pieces downloaded so far. This means that pieces will be moved around to be placed
at their final position in the files while downloading (to make sure the completed
download has all its pieces in the correct place). So, the main drawbacks are:

 * More disk operations while downloading since pieces are moved around.

 * Potentially more fragmentation in the filesystem.

 * Cannot be used while having files with priority 0.

The benefits though, are:

 * No startup delay, since the files don't need allocating.

 * The download will not use unnecessary disk space.

 * Disk caches perform much better than in full allocation and raises the download
   speed limit imposed by the disk.

 * Works well on filesystems that don't support sparse files.

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

This extension is deprecated in favor of the more widely supported ``ut_metadata``
extension, see `BEP 9`_.
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

.. _`BEP 9`: http://bittorrent.org/beps/bep_0009.html

dont_have
---------

Extension name: "lt_dont_have"

The ``dont_have`` extension message is used to tell peers that the client no longer
has a specific piece. The extension message should be advertised in the ``m`` dictionary
as ``lt_dont_have``. The message format mimics the regular ``HAVE`` bittorrent message.

Just like all extension messages, the first 2 bytes in the mssage itself are 20 (the
bittorrent extension message) and the message ID assigned to this extension in the ``m``
dictionary in the handshake.

+-----------+---------------+----------------------------------------+
| size      | name          | description                            |
+===========+===============+========================================+
| uint32_t  | piece         | index of the piece the peer no longer  |
|           |               | has.                                   |
+-----------+---------------+----------------------------------------+

The length of this message (including the extension message prefix) is
6 bytes, i.e. one byte longer than the normal ``HAVE`` message, because
of the extension message wrapping.

HTTP seeding
------------

There are two kinds of HTTP seeding. One with that assumes a smart
(and polite) client and one that assumes a smart server. These
are specified in `BEP 19`_ and `BEP 17`_ respectively.

libtorrent supports both. In the libtorrent source code and API,
BEP 19 urls are typically referred to as *url seeds* and BEP 17
urls are typically referred to as *HTTP seeds*.

The libtorrent implementation of `BEP 19`_ assumes that, if the URL ends with a slash
('/'), the filename should be appended to it in order to request pieces from
that file. The way this works is that if the torrent is a single-file torrent,
only that filename is appended. If the torrent is a multi-file torrent, the
torrent's name '/' the file name is appended. This is the same directory
structure that libtorrent will download torrents into.

.. _`BEP 17`: http://bittorrent.org/beps/bep_0017.html
.. _`BEP 19`: http://bittorrent.org/beps/bep_0019.html

piece picker
============

The piece picker in libtorrent has the following features:

* rarest first
* sequential download
* random pick
* reverse order picking
* parole mode
* prioritize partial pieces
* prefer whole pieces
* piece affinity by speed category
* piece priorities

internal representation
-----------------------

It is optimized by, at all times, keeping a list of pieces ordered
by rarity, randomly shuffled within each rarity class. This list
is organized as a single vector of contigous memory in RAM, for
optimal memory locality and to eliminate heap allocations and frees
when updating rarity of pieces.

Expensive events, like a peer joining or leaving, are evaluated
lazily, since it's cheaper to rebuild the whole list rather than
updating every single piece in it. This means as long as no blocks
are picked, peers joining and leaving is no more costly than a single
peer joining or leaving. Of course the special cases of peers that have
all or no pieces are optimized to not require rebuilding the list.

picker strategy
---------------

The normal mode of the picker is of course *rarest first*, meaning
pieces that few peers have are preferred to be downloaded over pieces
that more peers have. This is a fundamental algorithm that is the
basis of the performance of bittorrent. However, the user may set the
piece picker into sequential download mode. This mode simply picks
pieces sequentially, always preferring lower piece indices.

When a torrent starts out, picking the rarest pieces means increased
risk that pieces won't be completed early (since there are only a few
peers they can be downloaded from), leading to a delay of having any
piece to offer to other peers. This lack of pieces to trade, delays
the client from getting started into the normal tit-for-tat mode of
bittorrent, and will result in a long ramp-up time. The heuristic to
mitigate this problem is to, for the first few pieces, pick random pieces
rather than rare pieces. The threshold for when to leave this initial
picker mode is determined by ``session_settings::initial_picker_threshold``.

reverse order
-------------

An orthogonal setting is *reverse order*, which is used for *snubbed*
peers. Snubbed peers are peers that appear very slow, and might have timed
out a piece request. The idea behind this is to make all snubbed peers
more likely to be able to do download blocks from the same piece,
concentrating slow peers on as few pieces as possible. The reverse order
means that the most common pieces are picked, instead of the rarest pieces
(or in the case of sequential download, the last pieces, intead of the first).

parole mode
-----------

Peers that have participated in a piece that failed the hash check, may be
put in *parole mode*. This means we prefer downloading a full piece  from this
peer, in order to distinguish which peer is sending corrupt data. Whether to
do this is or not is controlled by ``session_settings::use_parole_mode``.

In parole mode, the piece picker prefers picking one whole piece at a time for
a given peer, avoiding picking any blocks from a piece any other peer has
contributed to (since that would defeat the purpose of parole mode).

prioritize partial pieces
-------------------------

This setting determines if partially downloaded or requested pieces should always
be preferred over other pieces. The benefit of doing this is that the number of
partial pieces is minimized (and hence the turn-around time for downloading a block
until it can be uploaded to others is minimized). It also puts less stress on the
disk cache, since fewer partial pieces need to be kept in the cache. Whether or
not to enable this is controlled by ``session_settings::prioritize_partial_pieces``.

The main benefit of not prioritizing partial pieces is that the rarest first
algorithm gets to have more influence on which pieces are picked. The picker is
more likely to truly pick the rarest piece, and hence improving the performance
of the swarm.

This setting is turned on automatically whenever the number of partial pieces
in the piece picker exceeds the number of peers we're connected to times 1.5.
This is in order to keep the waste of partial pieces to a minimum, but still
prefer rarest pieces.

prefer whole pieces
-------------------

The *prefer whole pieces* setting makes the piece picker prefer picking entire
pieces at a time. This is used by web connections (both http seeding
standards), in order to be able to coalesce the small bittorrent requests
to larger HTTP requests. This significantly improves performance when
downloading over HTTP.

It is also used by peers that are downloading faster than a certain
threshold. The main advantage is that these peers will better utilize the
other peer's disk cache, by requesting all blocks in a single piece, from
the same peer.

This threshold is controlled by ``session_settings::whole_pieces_threshold``.

*TODO: piece affinity by speed category*
*TODO: piece priorities*

SSL torrents
============

Torrents may have an SSL root (CA) certificate embedded in them. Such torrents
are called *SSL torrents*. An SSL torrent talks to all bittorrent peers over SSL.
The protocols are layered like this::

	+-----------------------+
	| BitTorrent protocol   |
	+-----------------------+
	| SSL                   |
	+-----------+-----------+
	| TCP       | uTP       |
	|           +-----------+
	|           | UDP       |
	+-----------+-----------+

During the SSL handshake, both peers need to authenticate by providing a certificate
that is signed by the CA certificate found in the .torrent file. These peer
certificates are expected to be privided to peers through some other means than 
bittorrent. Typically by a peer generating a certificate request which is sent to
the publisher of the torrent, and the publisher returning a signed certificate.

In libtorrent, `set_ssl_certificate()`_ in torrent_handle_ is used to tell libtorrent where
to find the peer certificate and the private key for it. When an SSL torrent is loaded,
the torrent_need_cert_alert_ is posted to remind the user to provide a certificate.

A peer connecting to an SSL torrent MUST provide the *SNI* TLS extension (server name
indication). The server name is the hex encoded info-hash of the torrent to connect to.
This is required for the client accepting the connection to know which certificate to
present.

SSL connections are accepted on a separate socket from normal bittorrent connections. To
pick which port the SSL socket should bind to, set ``session_settings::ssl_listen`` to a
different port. It defaults to port 4433. This setting is only taken into account when the
normal listen socket is opened (i.e. just changing this setting won't necessarily close
and re-open the SSL socket). To not listen on an SSL socket at all, set ``ssl_listen`` to 0.

This feature is only available if libtorrent is build with openssl support (``TORRENT_USE_OPENSSL``)
and requires at least openSSL version 1.0, since it needs SNI support.

Peer certificates must have at least one *SubjectAltName* field of type dNSName. At least
one of the fields must *exactly* match the name of the torrent. This is a byte-by-byte comparison,
the UTF-8 encoding must be identical (i.e. there's no unicode normalization going on). This is
the recommended way of verifying certificates for HTTPS servers according to `RFC 2818`_. Note
the difference that for torrents only *dNSName* fields are taken into account (not IP address fields).
The most specific (i.e. last) *Common Name* field is also taken into account if no *SubjectAltName*
did not match.

If any of these fields contain a single asterisk ("*"), the certificate is considered covering
any torrent, allowing it to be reused for any torrent.

The purpose of matching the torrent name with the fields in the peer certificate is to allow
a publisher to have a single root certificate for all torrents it distributes, and issue
separate peer certificates for each torrent. A peer receiving a certificate will not necessarily
be able to access all torrents published by this root certificate (only if it has a "star cert").

.. _`RFC 2818`: http://www.ietf.org/rfc/rfc2818.txt

testing
-------

To test incoming SSL connections to an SSL torrent, one can use the following *openssl* command::

	openssl s_client -cert <peer-certificate>.pem -key <peer-private-key>.pem -CAfile <torrent-cert>.pem -debug -connect 127.0.0.1:4433 -tls1 -servername <info-hash>

To create a root certificate, the Distinguished Name (*DN*) is not taken into account
by bittorrent peers. You still need to specify something, but from libtorrent's point of
view, it doesn't matter what it is. libtorrent only makes sure the peer certificates are
signed by the correct root certificate.

One way to create the certificates is to use the ``CA.sh`` script that comes with openssl, like thisi (don't forget to enter a common Name for the certificate)::

	CA.sh -newca
	CA.sh -newreq
	CA.sh -sign

The torrent certificate is located in ``./demoCA/private/demoCA/cacert.pem``, this is
the pem file to include in the .torrent file.

The peer's certificate is located in ``./newcert.pem`` and the certificate's
private key in ``./newkey.pem``.

