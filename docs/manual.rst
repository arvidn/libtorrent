============================
libtorrent API Documentation
============================

:Author: Arvid Norberg, arvid@rasterbar.com
:Version: 0.13

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

overview
========

The interface of libtorrent consists of a few classes. The main class is
the ``session``, it contains the main loop that serves all torrents.

The basic usage is as follows:

* construct a session
* parse .torrent-files and add them to the session (see `bdecode() bencode()`_)
* main loop (see session_)

	* query the torrent_handles for progress (see torrent_handle_)
	* query the session for information
	* add and remove torrents from the session at run-time

* save resume data for all torrent_handles (optional, see
  `write_resume_data()`_)
* destruct session object

Each class and function is described in this manual.

primitive network types
=======================

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

session
=======

The ``session`` class has the following synopsis::

	class session: public boost::noncopyable
	{

		session(fingerprint const& print
			= libtorrent::fingerprint(
			"LT", 0, 1, 0, 0));

		session(
			fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = 0);

		torrent_handle add_torrent(
			torrent_info const& ti
			, boost::filesystem::path const& save_path
			, entry const& resume_data = entry()
			, bool compact_mode = true
			, int block_size = 16 * 1024);

		torrent_handle add_torrent(
			char const* tracker_url
			, sha1_hash const& info_hash
			, char const* name
			, boost::filesystem::path const& save_path
			, entry const& resume_data = entry()
			, bool compact_mode = true
			, int block_size = 16 * 1024);

		session_proxy abort();

		void remove_torrent(torrent_handle const& h);
		torrent_handle find_torrent(sha_hash const& ih);
		std::vector<torrent_handle> get_torrents() const;

		void set_settings(
			session_settings const& settings);

		void set_upload_rate_limit(int bytes_per_second);
		int upload_rate_limit() const;
		void set_download_rate_limit(int bytes_per_second);
		int download_rate_limit() const;
		void set_max_uploads(int limit);
		void set_max_connections(int limit);
		void set_max_half_open_connections(int limit);

		int num_uploads() const;
		int num_connections() const;

		void set_ip_filter(ip_filter const& f);
      
		session_status status() const;

		bool is_listening() const;
		unsigned short listen_port() const;
		bool listen_on(
			std::pair<int, int> const& port_range
			, char const* interface = 0);

		std::auto_ptr<alert> pop_alert();
		void set_severity_level(alert::severity_t s);

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
	};

Once it's created, the session object will spawn the main thread that will do all the work.
The main thread will be idle as long it doesn't have any torrents to participate in.

session()
---------

	::

		session(fingerprint const& print
			= libtorrent::fingerprint("LT", 0, 1, 0, 0));
		session(fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = 0);

If the fingerprint in the first overload is omited, the client will get a default
fingerprint stating the version of libtorrent. The fingerprint is a short string that will be
used in the peer-id to identify the client and the client's version. For more details see the
fingerprint_ class. The constructor that only takes a fingerprint will not open a
listen port for the session, to get it running you'll have to call ``session::listen_on()``.
The other constructor, that takes a port range and an interface as well as the fingerprint
will automatically try to listen on a port on the given interface. For more information about
the parameters, see ``listen_on()`` function.

~session()
----------

The destructor of session will notify all trackers that our torrents have been shut down.
If some trackers are down, they will time out. All this before the destructor of session
returns. So, it's advised that any kind of interface (such as windows) are closed before
destructing the session object. Because it can take a few second for it to finish. The
timeout can be set with ``set_settings()``.

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

		torrent_handle add_torrent(
			torrent_info const& ti
			, boost::filesystem::path const& save_path
			, entry const& resume_data = entry()
			, bool compact_mode = true
			, int block_size = 16 * 1024);

		torrent_handle add_torrent(
			char const* tracker_url
			, sha1_hash const& info_hash
			, char const* name
			, boost::filesystem::path const& save_path
			, entry const& resume_data = entry()
			, bool compact_mode = true
			, int block_size = 16 * 1024);

You add torrents through the ``add_torrent()`` function where you give an
object representing the information found in the torrent file and the path where you
want to save the files. The ``save_path`` will be prepended to the directory
structure in the torrent-file.

If the torrent you are trying to add already exists in the session (is either queued
for checking, being checked or downloading) ``add_torrent()`` will throw
duplicate_torrent_ which derives from ``std::exception``.

The optional parameter, ``resume_data`` can be given if up to date fast-resume data
is available. The fast-resume data can be acquired from a running torrent by calling
``torrent_handle::write_resume_data()``. See `fast resume`_.

The ``compact_mode`` parameter refers to the layout of the storage for this torrent. If
set to true (default), the storage will grow as more pieces are downloaded, and pieces
are rearranged to finally be in their correct places once the entire torrent has been
downloaded. If it is false, the entire storage is allocated before download begins. I.e.
the files contained in the torrent are filled with zeros, and each downloaded piece
is put in its final place directly when downloaded. For more info, see `storage allocation`_.

``block_size`` sets the preferred request size, i.e. the number of bytes to request from
a peer at a time. This block size must be a divisor of the piece size, and since the piece
size is an even power of 2, so must the block size be. If the block size given here turns
out to be greater than the piece size, it will simply be clamped to the piece size.

The torrent_handle_ returned by ``add_torrent()`` can be used to retrieve information
about the torrent's progress, its peers etc. It is also used to abort a torrent.

The second overload that takes a tracker url and an info-hash instead of metadata
(``torrent_info``) can be used with torrents where (at least some) peers support
the metadata extension. For the overload to be available, libtorrent must be built
with extensions enabled (``TORRENT_ENABLE_EXTENSIONS`` defined). It also takes an
optional ``name`` argument. This may be 0 in case no name should be assigned to the
torrent. In case it's not 0, the name is used for the torrent as long as it doesn't
have metadata. See ``torrent_handle::name``.

remove_torrent() find_torrent() get_torrents()
----------------------------------------------

	::

		void remove_torrent(torrent_handle const& h);
		torrent_handle find_torrent(sha_hash const& ih);
		std::vector<torrent_handle> get_torrents() const;

``remove_torrent()`` will close all peer connections associated with the torrent and tell
the tracker that we've stopped participating in the swarm.

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


set_max_half_open_connections()
-------------------------------

	::
		
		void set_max_half_open_connections(int limit);

Sets the maximum number of half-open connections libtorrent will have when
connecting to peers. A half-open connection is one where connect() has been
called, but the connection still hasn't been established (nor failed). Windows
XP Service Pack 2 sets a default, system wide, limit of the number of half-open
connections to 10. So, this limit can be used to work nicer together with
other network applications on that system. The default is to have no limit,
and passing -1 as the limit, means to have no limit. When limiting the number
of simultaneous connection attempts, peers will be put in a queue waiting for
their turn to get connected.


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

		size_type total_payload_download;
		size_type total_payload_upload;

		int num_peers;

		int dht_nodes;
		int dht_cache_nodes;
		int dht_torrents;
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

``num_peers`` is the total number of peer connections this session have.

``dht_nodes``, ``dht_cache_nodes`` and ``dht_torrents`` are only available when
built with DHT support. They are all set to 0 if the DHT isn't running. When
the DHT is running, ``dht_nodes`` is set to the number of nodes in the routing
table. This number only includes *active* nodes, not cache nodes. The
``dht_cache_nodes`` is set to the number of nodes in the node cache. These nodes
are used to replace the regular nodes in the routing table in case any of them
becomes unresponsive.

``dht_torrents`` are the number of torrents tracked by the DHT at the moment.


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

pop_alert() set_severity_level()
--------------------------------

	::

		std::auto_ptr<alert> pop_alert();
		void set_severity_level(alert::severity_t s);

``pop_alert()`` is used to ask the session if any errors or events has occurred. With
``set_severity_level()`` you can filter how serious the event has to be for you to
receive it through ``pop_alert()``. For information, see alerts_.


add_extension()
---------------

	::

		void add_extension(boost::function<
			boost::shared_ptr<torrent_plugin>(torrent*)> ext);

This function adds an extension to this session. The argument is a function
object that is called with a ``torrent*`` and which should return a
``boost::shared_ptr<torrent_plugin>``. To write custom plugins, see
`libtorrent plugins`_. The main plugins implemented in libtorrent are:

metadata extension
	Allows peers to download the metadata (.torren files) from the swarm
	directly. Makes it possible to join a swarm with just a tracker and
	info-hash.

uTorrent peer exchange
	Exchanges peers between clients.

To use these, imclude ``<libtorrent/extensions/metadata_transfer.hpp>``
or ``<libtorrent/extensions/ut_pex.hpp>``. The functions to pass in to
``add_extension()`` are ``libtorrent::create_metadata_plugin`` and
``libtorrent::create_ut_pex_plugin`` respectively.

e.g.

::

	ses.add_extension(&libtorrent::create_metadata_plugin);
	ses.add_extension(&libtorrent::create_ut_pex_plugin);

.. _`libtorrent plugins`: libtorrent_plugins.html

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

The ``torrent_info`` has the following synopsis::

	class torrent_info
	{
	public:

		torrent_info();
		torrent_info(sha1_hash const& info_hash);
		torrent_info(entry const& torrent_file);

		entry create_torrent() const;
		void set_comment(char const* str);
		void set_piece_size(int size);
		void set_creator(char const* str);
		void set_hash(int index, sha1_hash const& h);
		void add_tracker(std::string const& url, int tier = 0);
		void add_file(boost::filesystem::path file, size_type size);
		void add_url_seed(std::string const& url);

		typedef std::vector<file_entry>::const_iterator file_iterator;
		typedef std::vector<file_entry>::const_reverse_iterator
			reverse_file_iterator;

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

		std::vector<announce_entry> const& trackers() const;

		bool priv() const;
		void set_priv(bool v);

		std::vector<std::string> const& url_seeds() const;

		size_type total_size() const;
		size_type piece_length() const;
		int num_pieces() const;
		sha1_hash const& info_hash() const;
		std::string const& name() const;
		std::string const& comment() const;
		std::string const& creator() const;

		std::vector<std::pair<std::string, int> > const& nodes() const;
		void add_node(std::pair<std::string, int> const& node);

		boost::optional<boost::posix_time::ptime>
		creation_date() const;

		void print(std::ostream& os) const;
	
		size_type piece_size(unsigned int index) const;
		sha1_hash const& hash_for_piece(unsigned int index) const;
	};

torrent_info()
--------------
   
	::

		torrent_info();
		torrent_info(sha1_hash const& info_hash);
		torrent_info(entry const& torrent_file);

The default constructor of ``torrent_info`` is used when creating torrent files. It will
initialize the object to an empty torrent, containing no files. The info hash will be set
to 0 when this constructor is used. To use the empty ``torrent_info`` object, add files
and piece hashes, announce URLs and optionally a creator tag and comment. To do this you
use the members ``set_comment()``, ``set_piece_size()``, ``set_creator()``, ``set_hash()``
etc.

The constructor that takes an info-hash is identical to the default constructor with the
exception that it will initialize the info-hash to the given value. This is used internally
when downloading torrents without the metadata. The metadata will be created by libtorrent
as soon as it has been downloaded from the swarm.

The last constructor is the one that is used in most cases. It will create a ``torrent_info``
object from the information found in the given torrent_file. The ``entry`` represents a tree
node in an bencoded file. To load an ordinary .torrent file into an ``entry``, use bdecode(),
see `bdecode() bencode()`_.

set_comment() set_piece_size() set_creator() set_hash() add_tracker() add_file()
--------------------------------------------------------------------------------

	::

		void set_comment(char const* str);
		void set_piece_size(int size);
		void set_creator(char const* str);
		void set_hash(int index, sha1_hash const& h);
		void add_tracker(std::string const& url, int tier = 0);
		void add_file(boost::filesystem::path file, size_type size);

These files are used when creating a torrent file. ``set_comment()`` will simply set
the comment that belongs to this torrent. The comment can be retrieved with the
``comment()`` member. The string should be UTF-8 encoded.

``set_piece_size()`` will set the size of each piece in this torrent. The piece size must
be an even multiple of 2. i.e. usually something like 256 kiB, 512 kiB, 1024 kiB etc. The
size is given in number of bytes.

``set_creator()`` is an optional attribute that can be used to identify your application
that was used to create the torrent file. The string should be UTF-8 encoded.

``set_hash()`` writes the hash for the piece with the given piece-index. You have to call
this function for every piece in the torrent. Usually the hasher_ is used to calculate
the sha1-hash for a piece.

``add_tracker()`` adds a tracker to the announce-list. The ``tier`` determines the order in
which the trackers are to be tried. For more information see `trackers()`_.

``add_file()`` adds a file to the torrent. The order in which you add files will determine
the order in which they are placed in the torrent file. You have to add at least one file
to the torrent. The ``path`` you give has to be a relative path from the root directory
of the torrent. The ``size`` is given in bytes.

When you have added all the files and hashes to your torrent, you can generate an ``entry``
which then can be encoded as a .torrent file. You do this by calling `create_torrent()`_.

For a complete example of how to create a torrent from a file structure, see make_torrent_.
      

create_torrent()
----------------

	::

		entry create_torrent();

Returns an ``entry`` representing the bencoded tree of data that makes up a .torrent file.
You can save this data as a torrent file with bencode() (see `bdecode() bencode()`_), for a
complete example, see make_torrent_.

.. _make_torrent: examples.html#make_torrent

This function is not const because it will also set the info-hash of the ``torrent_info``
object.

Note that a torrent file must include at least one file, and it must have at
least one tracker url or at least one DHT node.


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

The ``path`` is the full (relative) path of each file. i.e. if it is a multi-file
torrent, all the files starts with a directory with the same name as ``torrent_info::name()``.
The filenames are encoded with UTF-8.

``size`` is the size of the file (in bytes) and ``offset`` is the byte offset
of the file within the torrent. i.e. the sum of all the sizes of the files
before this one in the file list this one in the file list.

``orig_path`` is set to 0 in case the path element is an exact copy of that
found in the metadata. In case the path in the original metadata was
incorrectly encoded, and had to be fixed in order to be acceptable utf-8,
the original string is preserved in ``orig_path``. The reason to keep it
is to be able to reproduce the info-section exactly, with the correct
info-hash.

::

	struct file_entry
	{
		boost::filesystem::path path;
		size_type offset;
		size_type size;
		boost::shared_ptr<const boost::filesystem::path> orig_path;
	};


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


url_seeds()
-----------

	::

		std::vector<std::string> const& url_seeds() const;
		void add_url_seed(std::string const& url);

If there are any url-seeds in this torrent, ``url_seeds()`` will return a
vector of those urls. If you're creating a torrent file, ``add_url_seed()``
adds one url to the list of url-seeds. Currently, the only transport protocol
supported for the url is http.

See `HTTP seeding`_ for more information.


print()
-------

	::

		void print(std::ostream& os) const;

The ``print()`` function is there for debug purposes only. It will print the info from
the torrent file to the given outstream. This function has been deprecated and will
be removed from future releases.


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
		size_type piece_length() const;
		size_type piece_size(unsigned int index) const;
		int num_pieces() const;


``total_size()``, ``piece_length()`` and ``num_pieces()`` returns the total
number of bytes the torrent-file represents (all the files in it), the number of byte for
each piece and the total number of pieces, respectively. The difference between
``piece_size()`` and ``piece_length()`` is that ``piece_size()`` takes
the piece index as argument and gives you the exact size of that piece. It will always
be the same as ``piece_length()`` except in the case of the last piece, which may
be smaller.


hash_for_piece() info_hash()
----------------------------

	::
	
		size_type piece_size(unsigned int index) const;
		sha1_hash const& hash_for_piece(unsigned int index) const;

``hash_for_piece()`` takes a piece-index and returns the 20-bytes sha1-hash for that
piece and ``info_hash()`` returns the 20-bytes sha1-hash for the info-section of the
torrent file. For more information on the ``sha1_hash``, see the big_number_ class.
``info_hash()`` will only return a valid hash if the torrent_info was read from a
``.torrent`` file or if an ``entry`` was created from it (through ``create_torrent``).


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


priv() set_priv()
-----------------

	::

		bool priv() const;
		void set_priv(bool v);

``priv()`` returns true if this torrent is private. i.e., it should not be
distributed on the trackerless network (the kademlia DHT).

``set_priv()`` sets or clears the private flag on this torrent.


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


torrent_handle
==============

You will usually have to store your torrent handles somewhere, since it's the
object through which you retrieve information about the torrent and aborts the torrent.
Its declaration looks like this::

	struct torrent_handle
	{
		torrent_handle();

		torrent_status status();
		void file_progress(std::vector<float>& fp);
		void get_download_queue(std::vector<partial_piece_info>& queue) const;
		void get_peer_info(std::vector<peer_info>& v) const;
		torrent_info const& get_torrent_info() const;
		bool is_valid() const;

		std::string name() const;

		entry write_resume_data() const;
		void force_reannounce() const;
		void connect_peer(asio::ip::tcp::endpoint const& adr, int source = 0) const;

		void set_tracker_login(std::string const& username
			, std::string const& password) const;

		std::vector<announce_entry> const& trackers() const;
		void replace_trackers(std::vector<announce_entry> const&);

		void add_url_seed(std::string const& url);

		void set_ratio(float ratio) const;
		void set_max_uploads(int max_uploads) const;
		void set_max_connections(int max_connections) const;
		void set_upload_limit(int limit) const;
		int upload_limit() const;
		void set_download_limit(int limit) const;
		int download_limit() const;
		void set_sequenced_download_threshold(int threshold) const;

		void set_peer_upload_limit(asio::ip::tcp::endpoint ip, int limit) const;
		void set_peer_download_limit(asio::ip::tcp::endpoint ip, int limit) const;

		void use_interface(char const* net_interface) const;

		void pause() const;
		void resume() const;
		bool is_paused() const;
		bool is_seed() const;

		void resolve_countries(bool r);
		bool resolve_countries() const;

		void piece_priority(int index, int priority) const;
		int piece_priority(int index) const;

		void prioritize_pieces(std::vector<int> const& pieces) const;
		std::vector<int> piece_priorities() const;

		void prioritize_files(std::vector<int> const& files) const;

		// these functions are deprecated
		void filter_piece(int index, bool filter) const;
		void filter_pieces(std::vector<bool> const& bitmask) const;
		bool is_piece_filtered(int index) const;
		std::vector<bool> filtered_pieces() const;
		void filter_files(std::vector<bool> const& files) const;
      
		bool has_metadata() const;

		boost::filesystem::path save_path() const;
		bool move_storage(boost::filesystem::path const& save_path) const;

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
	exception, in case the handle is no longer refering to a torrent. There are
	two exceptions, ``info_hash()`` and ``is_valid()`` will never throw.
	Since the torrents are processed by a background thread, there is no
	guarantee that a handle will remain valid between two calls.


piece_priority() prioritize_pieces() piece_priorities() prioritize_files()
--------------------------------------------------------------------------

	::

		void piece_priority(int index, int priority) const;
		int piece_priority(int index) const;
		void prioritize_pieces(std::vector<int> const& pieces) const;
		std::vector<int> piece_priorities() const;
		void prioritize_files(std::vector<int> const& files) const;

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

``prioritize_files`` takes a vector that has at as many elements as there are
files in the torrent. Each entry is the priority of that file. The function
sets the priorities of all the pieces in the torrent based on the vector.


file_progress()
---------------

	::

		void file_progress(std::vector<float>& fp);

This function fills in the supplied vector with the progress (a value in the
range [0, 1]) describing the download progress of each file in this torrent.
The progress values are ordered the same as the files in the `torrent_info`_.
This operation is not very cheap.


save_path()
-----------

	::

		boost::filesystem::path save_path() const;

``save_path()`` returns the path that was given to `add_torrent()`_ when this torrent
was started.

move_storage()
--------------

	::

		bool move_storage(boost::filesystem::path const& save_path) const;

Moves the file(s) that this torrent are currently seeding from or downloading to. This
operation will only have the desired effect if the given ``save_path`` is located on
the same drive as the original save path. If the move operation fails, this function
returns false, otherwise true. Post condition for successful operation is:
``save_path() == save_path``.


force_reannounce()
------------------

	::

		void force_reannounce() const;

``force_reannounce()`` will force this torrent to do another tracker request, to receive new
peers. If the torrent is invalid, queued or in checking mode, this functions will throw
invalid_handle_.


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


set_sequenced_download_threshold()
----------------------------------

	::

		void set_sequenced_download_threshold(int threshold);

sequenced-download threshold is the limit on how popular a piece has to be
(popular == inverse of rarity) to be downloaded in sequence instead of in
random (rarest first) order. It can be used to tweak disk performance in
settings where the random download property is less necessary. For example, if
the threshold is 10, all pieces which 10 or more peers have, will be downloaded
in index order. This setting defaults to 100, which means that it is disabled
in practice.

Setting this threshold to a very small value will affect the piece distribution
negatively in the swarm. It should basically only be used in situations where
the random seeks on the disk is the download bottleneck.


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


add_url_seed()
--------------

	::

		void add_url_seed(std::string const& url);

``add_url_seed()`` adds another url to the torrent's list of url seeds. If the
given url already exists in that list, the call has no effect. The torrent
will connect to the server and try to download pieces from it, unless it's
paused, queued, checking or seeding.

See `HTTP seeding`_ for more information.


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


write_resume_data()
-------------------

	::

		entry write_resume_data() const;

``write_resume_data()`` generates fast-resume data and returns it as an entry_. This entry_
is suitable for being bencoded. For more information about how fast-resume works, see `fast resume`_.

There are three cases where this function will just return an empty ``entry``:

	1. The torrent handle is invalid.
	2. The torrent is checking (or is queued for checking) its storage, it will obviously
	   not be ready to write resume data.
	3. The torrent hasn't received valid metadata and was started without metadata
	   (see libtorrent's `metadata from peers`_ extension)

Note that by the time this function returns, the resume data may already be invalid if the torrent
is still downloading! The recommended practice is to first pause the torrent, then generate the
fast resume data, and then close it down.

It is still a good idea to save resume data periodically during download as well as when
closing down. In full allocation mode the reume data is never invalidated by subsequent
writes to the files, since pieces won't move around.


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
		enum { max_blocks_per_piece };
		int piece_index;
		int blocks_in_piece;
		std::bitset<max_blocks_per_piece> requested_blocks;
		std::bitset<max_blocks_per_piece> finished_blocks;
		address peer[max_blocks_per_piece];
		int num_downloads[max_blocks_per_piece];
	};

``piece_index`` is the index of the piece in question. ``blocks_in_piece`` is the
number of blocks in this particular piece. This number will be the same for most pieces, but
the last piece may have fewer blocks than the standard pieces.

``requested_blocks`` is a bitset with one bit per block in the piece. If a bit is set, it
means that that block has been requested, but not necessarily fully downloaded yet. To know
from whom the block has been requested, have a look in the ``peer`` array. The bit-index
in the ``requested_blocks`` and ``finished_blocks`` corresponds to the array-index into
``peers`` and ``num_downloads``. The array of peers is contains the address of the
peer the piece was requested from. If a piece hasn't been requested (the bit in
``requested_blocks`` is not set) the peer array entry will be undefined.

The ``finished_blocks`` is a bitset where each bit says if the block is fully downloaded
or not. And the ``num_downloads`` array says how many times that block has been downloaded.
When a piece fails a hash verification, single blocks may be re-downloaded to
see if the hash test may pass then.


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

*TODO: document storage*


torrent_status
==============

It contains the following fields::

	struct torrent_status
	{
		enum state_t
		{
			queued_for_checking,
			checking_files,
			connecting_to_tracker,
			downloading,
			finished,
			seeding,
			allocating
		};
	
		state_t state;
		bool paused;
		float progress;
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

		const std::vector<bool>* pieces;
		int num_pieces;

		size_type total_done;
		size_type total_wanted_done;
		size_type total_wanted;

		int num_seeds;
		float distributed_copies;

		int block_size;
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
|``connecting_to_tracker`` |The torrent has sent a request to the tracker and is      |
|                          |currently waiting for a response                          |
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

``next_announce`` is the time until the torrent will announce itself to the tracker. And
``announce_interval`` is the time the tracker want us to wait until we announce ourself
again the next time.

``current_tracker`` is the URL of the last working tracker. If no tracker request has
been successful yet, it's set to an empty string.

``total_download`` and ``total_upload`` is the number of bytes downloaded and
uploaded to all peers, accumulated, *this session* only.

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

``total_done`` is the total number of bytes of the file(s) that we have. All
this does not necessarily has to be downloaded during this session (that's
``total_download_payload``).

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
			queued = 0x100
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

		asio::ip::tcp::endpoint ip;
		float up_speed;
		float down_speed;
		float payload_up_speed;
		float payload_down_speed;
		size_type total_download;
		size_type total_upload;
		peer_id pid;
		std::vector<bool> pieces;
		bool seed;
		int upload_limit;
		int download_limit;

		char country[2];

		size_type load_balancing;

		int download_queue_length;
		int upload_queue_length;

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

``pieces`` is a vector of booleans that has as many entries as there are pieces
in the torrent. Each boolean tells you if the peer has that piece (if it's set to true)
or if the peer miss that piece (set to false).

``seed`` is true if this peer is a seed.

``upload_limit`` is the number of bytes per second we are allowed to send to this
peer every second. It may be -1 if there's no local limit on the peer. The global
limit and the torrent limit is always enforced anyway.

``download_limit`` is the number of bytes per second this peer is allowed to
receive. -1 means it's unlimited.

``country`` is the two letter `ISO 3166 country code`__ for the country the peer
is connected from. If the country hasn't been resolved yet, both chars are set
to 0. If the resolution failed for some reason, the field is set to "--". If the
resolution service returns an invalid country code, it is set to "!!".
The ``countries.nerd.dk`` service is used to look up countries. This field will
remain set to 0 unless the torrent is set to resolve countries, see `resolve_countries()`_.

__ http://www.iso.org/iso/en/prods-services/iso3166ma/02iso-3166-code-lists/list-en1.html

``load_balancing`` is a measurement of the balancing of free download (that we get)
and free upload that we give. Every peer gets a certain amount of free upload, but
this member says how much *extra* free upload this peer has got. If it is a negative
number it means that this was a peer from which we have got this amount of free
download.

``download_queue_length`` is the number of piece-requests we have sent to this peer
that hasn't been answered with a piece yet.

``upload_queue_length`` is the number of piece-requests we have received from this peer
that we haven't answered with a piece yet.

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
		std::string proxy_ip;
		int proxy_port;
		std::string proxy_login;
		std::string proxy_password;
		std::string user_agent;
		int tracker_completion_timeout;
		int tracker_receive_timeout;
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
		bool use_dht_as_fallback;
	};

``proxy_ip`` may be a hostname or ip to a http proxy to use. If this is
an empty string, no http proxy will be used.

``proxy_port`` is the port on which the http proxy listens. If ``proxy_ip``
is empty, this will be ignored.

``proxy_login`` should be the login username for the http proxy, if this
empty, the http proxy will be tried to be used without authentication.

``proxy_password`` the password string for the http proxy.

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

``use_dht_as_fallback`` determines how the DHT is used. If this is true
(which it is by default), the DHT will only be used for torrents where
all trackers in its tracker list has failed. Either by an explicit error
message or a time out.

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



alerts
======

The ``pop_alert()`` function on session is the interface for retrieving
alerts, warnings, messages and errors from libtorrent. If there hasn't
occurred any errors (matching your severity level) ``pop_alert()`` will
return a zero pointer. If there has been some error, it will return a pointer
to an alert object describing it. You can then use the alert object and query
it for information about the error or message. To retrieve any alerts, you have
to select a severity level using ``session::set_severity_level()``. It defaults to
``alert::none``, which means that you don't get any messages at all, ever.
You have the following levels to select among:

+--------------+----------------------------------------------------------+
| ``none``     | No alert will ever have this severity level, which       |
|              | effectively filters all messages.                        |
|              |                                                          |
+--------------+----------------------------------------------------------+
| ``fatal``    | Fatal errors will have this severity level. Examples can |
|              | be disk full or something else that will make it         |
|              | impossible to continue normal execution.                 |
|              |                                                          |
+--------------+----------------------------------------------------------+
| ``critical`` | Signals errors that requires user interaction or         |
|              | messages that almost never should be ignored. For        |
|              | example, a chat message received from another peer is    |
|              | announced as severity ``critical``.                      |
|              |                                                          |
+--------------+----------------------------------------------------------+
| ``warning``  | Messages with the warning severity can be a tracker that |
|              | times out or responds with invalid data. It will be      |
|              | retried automatically, and the possible next tracker in  |
|              | a multitracker sequence will be tried. It does not       |
|              | require any user interaction.                            |
|              |                                                          |
+--------------+----------------------------------------------------------+
| ``info``     | Events that can be considered normal, but still deserves |
|              | an event. This could be a piece hash that fails.         |
|              |                                                          |
+--------------+----------------------------------------------------------+
| ``debug``    | This will include a lot of debug events that can be used |
|              | both for debugging libtorrent but also when debugging    |
|              | other clients that are connected to libtorrent. It will  |
|              | report strange behaviors among the connected peers.      |
|              |                                                          |
+--------------+----------------------------------------------------------+

When setting a severity level, you will receive messages of that severity and all
messages that are more sever. If you set ``alert::none`` (the default) you will not receive
any events at all.

When you set a severity level other than ``none``, you have the responsibility to call
``pop_alert()`` from time to time. If you don't do that, the alert queue will just grow.

When you get an alert, you can use ``typeid()`` or ``dynamic_cast<>`` to get more detailed
information on exactly which type it is. i.e. what kind of error it is. You can also use a
dispatcher_ mechanism that's available in libtorrent.

All alert types are defined in the ``<libtorrent/alert_types.hpp>`` header file.

The ``alert`` class is the base class that specific messages are derived from. This
is its synopsis::

	class alert
	{
	public:

		enum severity_t { debug, info, warning, critical, fatal, none };

		alert(severity_t severity, std::string const& msg);
		virtual ~alert();

		std::string const& msg() const;
		severity_t severity() const;

		virtual std::auto_ptr<alert> clone() const = 0;
	};

This means that all alerts have at least a string describing it. They also
have a severity level that can be used to sort them or present them to the
user in different ways.

There's another alert base class that all most alerts derives from, all the
alerts that are generated for a specific torrent are derived from::

	struct torrent_alert: alert
	{
		torrent_alert(torrent_handle const& h, severity_t s, std::string const& msg);

		torrent_handle handle;
	};

The specific alerts, that all derives from ``alert``, are:


listen_failed_alert
-------------------

This alert is generated when none of the ports, given in the port range, to
session_ can be opened for listening. This alert is generated as severity
level ``fatal``.

::

	struct listen_failed_alert: alert
	{
		listen_failed_alert(const std::string& msg);
		virtual std::auto_ptr<alert> clone() const;
	};

portmap_error_alert
-------------------

This alert is generated when a NAT router was successfully found but some
part of the port mapping request failed. It contains a text message that
may help the user figure out what is wrong. This alert is not generated in
case it appears the client is not running on a NAT:ed network or if it
appears there is no NAT router that can be remote controlled to add port
mappings.

The alert is generated as severity ``warning``, since it should be displayed
to the user somehow, and could mean reduced preformance.

::

	struct portmap_error_alert: alert
	{
		portmap_error_alert(const std::string& msg);
		virtual std::auto_ptr<alert> clone() const;
	};

portmap_alert
-------------

This alert is generated when a NAT router was successfully found and
a port was successfully mapped on it. On a NAT:ed network with a NAT-PMP
capable router, this is typically generated once when mapping the TCP
port and, if DHT is enabled, when the UDP port is mapped. This is merely
an informational alert, and is generated at severity level ``info``.

::

	struct portmap_alert: alert
	{
		portmap_alert(const std::string& msg);
		virtual std::auto_ptr<alert> clone() const;
	};

file_error_alert
----------------

If the storage fails to read or write files that it needs access to, this alert is
generated and the torrent is paused. It is generated as severity level ``fatal``.

::

	struct file_error_alert: torrent_alert
	{
		file_error_alert(
			const torrent_handle& h
			, const std::string& msg);
			
		virtual std::auto_ptr<alert> clone() const;
	};


tracker_announce_alert
----------------------

This alert is generated each time a tracker announce is sent (or attempted to be sent).
It is generated at severity level ``info``.

::

	struct tracker_announce_alert: torrent_alert
	{
		tracker_announce_alert(
			const torrent_handle& h
			, const std::string& msg);
			
		virtual std::auto_ptr<alert> clone() const;
	};


tracker_alert
-------------

This alert is generated on tracker time outs, premature disconnects, invalid response or
a HTTP response other than "200 OK". From the alert you can get the handle to the torrent
the tracker belongs to. This alert is generated as severity level ``warning``.

The ``times_in_row`` member says how many times in a row this tracker has failed.
``status_code`` is the code returned from the HTTP server. 401 means the tracker needs
authentication, 404 means not found etc. If the tracker timed out, the code will be set
to 0.

::

	struct tracker_alert: torrent_alert
	{
		tracker_alert(torrent_handle const& h, int times, int status
			, const std::string& msg);
		virtual std::auto_ptr<alert> clone() const;

		int times_in_row;
		int status_code;
	};


tracker_reply_alert
-------------------

This alert is only for informational purpose. It is generated when a tracker announce
succeeds. It is generated regardless what kind of tracker was used, be it UDP, HTTP or
the DHT. It is generated with severity level ``info``.

::

	struct tracker_reply_alert: torrent_alert
	{
		tracker_reply_alert(const torrent_handle& h
			, int num_peers
			, const std::string& msg);

		int num_peers;

		virtual std::auto_ptr<alert> clone() const;
	};

The ``num_peers`` tells how many peers were returned from the tracker. This is
not necessarily all new peers, some of them may already be connected.
	
tracker_warning_alert
---------------------

This alert is triggered if the tracker reply contains a warning field. Usually this
means that the tracker announce was successful, but the tracker has a message to
the client. The message string in the alert will contain the warning message from
the tracker. It is generated with severity level ``warning``.

::

	struct tracker_warning_alert: torrent_alert
	{
		tracker_warning_alert(torrent_handle const& h
			, std::string const& msg);

		virtual std::auto_ptr<alert> clone() const;
	};


url_seed_alert
--------------

This alert is generated when a HTTP seed name lookup fails. This alert is
generated as severity level ``warning``.

It contains ``url`` to the HTTP seed that failed along with an error message.

::

	struct url_seed_alert: torrent_alert
	{
		url_seed_alert(torrent_handle const& h, std::string const& url
			, const std::string& msg);
		virtual std::auto_ptr<alert> clone() const;

		std::string url;
	};

   
hash_failed_alert
-----------------

This alert is generated when a finished piece fails its hash check. You can get the handle
to the torrent which got the failed piece and the index of the piece itself from the alert.
This alert is generated as severity level ``info``.

::

	struct hash_failed_alert: torrent_alert
	{
		hash_failed_alert(
			torrent_handle const& h
			, int index
			, const std::string& msg);

		virtual std::auto_ptr<alert> clone() const;

		int piece_index;
	};


peer_ban_alert
--------------

This alert is generated when a peer is banned because it has sent too many corrupt pieces
to us. It is generated at severity level ``info``. The ``handle`` member is a torrent_handle_
to the torrent that this peer was a member of.

::

	struct peer_ban_alert: torrent_alert
	{
		peer_ban_alert(
			asio::ip::tcp::endpoint const& pip
			, torrent_handle h
			, const std::string& msg);

		virtual std::auto_ptr<alert> clone() const;

		asio::ip::tcp::endpoint ip;
	};


peer_error_alert
----------------

This alert is generated when a peer sends invalid data over the peer-peer protocol. The peer
will be disconnected, but you get its ip address from the alert, to identify it. This alert
is generated as severity level ``debug``.

::

	struct peer_error_alert: alert
	{
		peer_error_alert(
			asio::ip::tcp::endpoint const& pip
			, peer_id const& pid
			, const std::string& msg);

		virtual std::auto_ptr<alert> clone() const;
		asio::ip::tcp::endpoint ip;
		peer_id id;
	};


invalid_request_alert
---------------------

This is a debug alert that is generated by an incoming invalid piece request. The ``handle``
is a handle to the torrent the peer is a member of. ``ìp`` is the address of the peer and the
``request`` is the actual incoming request from the peer. The alert is generated as severity level
``debug``.

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
torrent in question. This alert is generated as severity level ``info``.

::

	struct torrent_finished_alert: torrent_alert
	{
		torrent_finished_alert(
			const torrent_handle& h
			, const std::string& msg);

		virtual std::auto_ptr<alert> clone() const;
	};


metadata_failed_alert
---------------------

This alert is generated when the metadata has been completely received and the info-hash
failed to match it. i.e. the metadata that was received was corrupt. libtorrent will
automatically retry to fetch it in this case. This is only relevant when running a
torrent-less download, with the metadata extension provided by libtorrent.
It is generated at severity level ``info``.

::

	struct metadata_failed_alert: torrent_alert
	{
		metadata_failed_alert(
			torrent_handle const& h
			, std::string const& msg);
			
		virtual std::auto_ptr<alert> clone() const;
	};


metadata_received_alert
-----------------------

This alert is generated when the metadata has been completely received and the torrent
can start downloading. It is not generated on torrents that are started with metadata, but
only those that needs to download it from peers (when utilizing the libtorrent extension).
It is generated at severity level ``info``.

::

	struct metadata_received_alert: torrent_alert
	{
		metadata_received_alert(
			torrent_handle const_& h
			, std::string const& msg);
			
		virtual std::auto_ptr<alert> clone() const;
	};


fastresume_rejected_alert
-------------------------

This alert is generated when a fastresume file has been passed to ``add_torrent`` but the
files on disk did not match the fastresume file. The string explains the reason why the
resume file was rejected. It is generated at severity level ``warning``.

::

	struct fastresume_rejected_alert: torrent_alert
	{
		fastresume_rejected_alert(torrent_handle const& h
			, std::string const& msg);

		virtual std::auto_ptr<alert> clone() const;
	};


peer_blocked_alert
------------------

This alert is generated when a peer is blocked by the IP filter. It has the severity leve
``info``. The ``ip`` member is the address that was blocked.

::

	struct peer_blocked_alert: alert
	{
		peer_blocked_alert(address const& ip_
			, std::string const& msg);
		
		address ip;

		virtual std::auto_ptr<alert> clone() const;
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
the session.

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


fast resume
===========

The fast resume mechanism is a way to remember which pieces are downloaded
and where they are put between sessions. You can generate fast resume data by
calling ``torrent_handle::write_resume_data()`` on torrent_handle_. You can
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
   
 * The second thread is a hash-check thread. Whenever a torrent is added it will
   first be passed to this thread for checking the files that may already have been
   downloaded. If there is any resume data this thread will make sure it is valid
   and matches the files. Once the torrent has been checked, it is passed on to the
   main thread that will start it. The hash-check thread has a queue of torrents,
   it will only check one torrent at a time.

 * The third thread is spawned by asio on systems that don't support
   non-blocking host name resolution to simulate non-blocking behavior.


storage allocation
==================

There are two modes in which storage (files on disk) are allocated in libtorrent.

 * The traditional *full allocation* mode, where the entire files are filled up with
   zeros before anything is downloaded. libtorrent will look for sparse files support
	in the filesystem that is used for storage, and use sparse files or file system
	zaero fill support if present. This means that on NTFS, full allocation mode will
	only allocate storage for the downloaded pieces.

 * And the *compact allocation* mode, where only files are allocated for actual
   pieces that have been downloaded. This is the default allocation mode in libtorrent.

The allocation mode is selected when a torrent is started. It is passed as a boolean
argument to ``session::add_torrent()`` (see `add_torrent()`_). These two modes have
different drawbacks and benefits.

full allocation
---------------

When a torrent is started in full allocation mode, the checker thread (see threads_)
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

|sf_logo|__

.. |copy| unicode:: 0xA9 .. copyright sign
__ http://www.cs.umu.se
.. |sf_logo| image:: http://sourceforge.net/sflogo.php?group_id=7994
__ http://sourceforge.net


