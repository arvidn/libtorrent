==========
libtorrent
==========


=================== ===============
`sourceforge page`_ `mailing list`_
=================== ===============

.. _sourceforge page: http://www.sourceforge.net/projects/libtorrent
.. _mailing list: http://lists.sourceforge.net/lists/listinfo/libtorrent-discuss

.. contents::

introduction
============

libtorrent is a C++ library that aims to be a good alternative to all the
`other bittorrent implementations`__ around. It is a
library and not a full featured client, although it comes with a working
example client.

__ links.html

The main goals of libtorrent are:

	* to be cpu efficient
	* to be memory efficient
	* to be very easy to use

libtorrent is not finished. It is an ongoing project (including this documentation).
The current state includes the following features:

	* multitracker extension support (as `described by TheShadow`__)
	* serves multiple torrents on a single port and a single thread
	* supports http proxies and proxy authentication
	* gzipped tracker-responses
	* piece picking on block-level (as opposed to piece-level) like in Azureus_
	* queues torrents for file check, instead of checking all of them in parallel.
	* uses separate threads for checking files and for main downloader, with a fool-proof
	  thread-safe library interface. (i.e. There's no way for the user to cause a deadlock).
	* can limit the upload bandwidth usage and the maximum number of unchoked peers
	* piece-wise file allocation
	* tries to maintain a 1:1 share ratio between all peers but also shifts free
	  download to peers as free upload. To maintain a global 1:1 ratio.
	* fast resume support, a way to get rid of the costly piece check at the start
	  of a resumed torrent. Saves the storage state in a separate fast-resume file.

__ http://home.elp.rr.com/tur/multitracker-spec.txt
.. _Azureus: http://azureus.sourceforge.net

Functions that are yet to be implemented:

	* choke/unchoke policy for seed-mode
	* number of connections limit
	* better handling of peers that send bad data
	* ip-filters
	* file-level piece priority

libtorrent is portable at least among windows, macosx, and UNIX-systems. It uses boost.thread,
boost.filesystem boost.date_time and various other boost libraries and zlib.

libtorrent has been successfully compiled and tested on:

	* Cygwin GCC 3.3.1
	* Windows 2000 vc7.1
	* Linux x86 (debian) GCC 3.0

It does not compile on

	* GCC 2.95

building
========

To build libtorrent you need boost_ and bjam installed.
Then you can use ``bjam`` to build libtorrent.

.. _boost: http://www.boost.org

To make bjam work, you need to set the environment variable ``BOOST_ROOT`` to the
path where boost is installed (e.g. c:\boost_1_30_2 on windows). Then you can just run
``bjam`` in the libtorrent directory.

The Jamfile doesn't work yet. On unix-systems you can use the makefile however. You
first have to build boost.thread and boost.filesystem. You do this by, in the directory
'boost-1.30.2/tools/build/jam_src' run the build script ``./build.sh``. This should
produce at least one folder with the 'bin' prefix (and the rest of the name describes
your platform). Put the files in that folder somewhere in your path.

You can then invoke ``bjam`` in the directories 'boost-1.30.2/libs/thread/build',
'boost-1.30.2/libs/date_time/build' and 'boost-1.30.2/libs/filesystem/build'. That will
produce the needed libraries. Put these libraries in the libtorrent root directory.
You then have to modify the makefile to use you prefered compiler and to have the
correct path to your boost istallation.

Then the makefile should be able to do the rest.

When building (with boost 1.30.2) on linux and solaris however, I found that I had to make the following
modifications to the boost.date-time library. In the file:
'boost-1.30.2/boost/date_time/gregorian_calendar.hpp' line 59. Prepend 'boost/date_time/'
to the include path.

And the second modification was in the file:
'boost-1.30.2/boost/date_time/microsec_time_clock.hpp' add the following include at the top
of the file::

	#include "boost/cstdint.hpp"

In developer studio, you may have to set the compiler options "force conformance in for
loop scope" and "treat wchar_t as built-in type" to Yes.

TODO: more detailed build instructions.





using
=====

The interface of libtorrent consists of a few classes. The main class is
the ``session``, it contains the main loop that serves all torrents.



session
-------

The ``session`` class has the following synopsis::

	class session: public boost::noncopyable
	{
		session(int listen_port, const fingerprint& print);
		session(int listen_port);

		torrent_handle add_torrent(const torrent_info& t, const std::string& save_path);
		torrent_handle add_torrent(
			const torrent_info& t
			, const std::string& save_path
			, const std::vector<char>& resume_data);

		void remove_torrent(const torrent_handle& h);

		void set_http_settings(const http_settings& settings);
		void set_upload_rate_limit(int bytes_per_second);

		std::auto_ptr<alert> pop_alert();
		void set_severity_level(alert::severity_t s);

	};

Once it's created, it will spawn the main thread that will do all the work.
The main thread will be idle as long it doesn't have any torrents to participate in.
You add torrents through the ``add_torrent()``-function where you give an
object representing the information found in the torrent file and the path where you
want to save the files. The ``save_path`` will be prepended to the directory-
structure in the torrent-file. ``add_torrent`` will throw ``duplicate_torrent`` exception
if the torrent already exists in the session.

The optional last parameter, ``resume_data`` can be given if up to date fast-resume data
is available. The fast-resume data can be acquired from a running torrent by calling
``torrent_handle::write_resume_data()``. See `fast resume`_.

``remove_torrent()`` will close all peer connections associated with the torrent and tell
the tracker that we've stopped participating in the swarm.

If the torrent you are trying to add already exists in the session (is either queued
for checking, being checked or downloading) ``add_torrent()`` will throw
``duplicate_torrent`` which derives from ``std::exception``.

The difference between the two constructors is that one of them takes a fingerprint
as argument. If this is ommited, the client will get a default fingerprint stating
the version of libtorrent. The fingerprint is a short string that will be used in
the peer-id to identify the client and the client's version. For more details see the
fingerprint class.

``set_upload_rate_limit()`` set the maximum number of bytes allowed to be
sent to peers per second. This bandwidth is distributed among all the peers. If
you don't want to limit upload rate, you can set this to -1 (the default).

The destructor of session will notify all trackers that our torrents has been shut down.
If some trackers are down, they will timout. All this before the destructor of session
returns. So, it's adviced that any kind of interface (such as windows) are closed before
destructing the sessoin object. Because it can take a few second for it to finish. The
timeout can be set with ``set_http_settings()``.

How to parse a torrent file and create a ``torrent_info`` object is described below.

The torrent_handle_ returned by ``add_torrent`` can be used to retrieve information
about the torrent's progress, its peers etc. It is also used to abort a torrent.

The constructor takes a listen port as argument, if the given port is busy it will
increase the port number by one and try again. If it still fails it will continue
increasing the port number until it succeeds or has failed 9 ports. *This will
change in the future to give more control of the listen-port.*

The ``pop_alert()`` function is the interface for retrieving alerts, warnings and
errors from libtorrent. If there hasn't occured any errors (matching your severity
level) ``pop_alert()`` will return a zero pointer. If there has been some error, it will
return a pointer to an alert object describing it. You can then use the query the
alert_ object for information about the error or message. To retrieve any alerts, you
have to select a severity level using ``set_severity_level()``. It defaults to
``alert::none``, which means that you don't get any messages at all, ever. You have
the following levels to select among:

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
| ``critical`` | Signals errors that requires user interaction.           |
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
| ``debug``    | This will include alot of debug events that can be used  |
|              | both for debugging libtorrent but also when debugging    |
|              | other clients that are connected to libtorrent. It will  |
|              | report strange behaviors among the connected peers.      |
|              |                                                          |
+--------------+----------------------------------------------------------+

When setting a severity level, you will receive messages of that severity and all
messages that are more sever. If you set ``alert::none`` (the default) you will not recieve
any events at all.

When you get an alert, you can use ``typeid()`` or ``dynamic_cast<>`` to get more detailed
information on exactly which type it is. i.e. what kind of error it is. You can also use a
dispatcher mechanism that's available in libtorrent.

TODO: describe the type dispatching mechanism

The currently available alert types are:

	* tracker_alert
	* hash_failed_alert

You can try a ``dynamic_cast`` to these types to get more message-pecific information. Here
are their definitions::

	struct tracker_alert: alert
	{
		tracker_alert(const torrent_handle& h, const std::string& msg);
		virtual std::auto_ptr<alert> clone() const;

		torrent_handle handle;
	};

	struct hash_failed_alert: alert
	{
		hash_failed_alert(
			const torrent_handle& h
			, int index
			, const std::string& msg);

		virtual std::auto_ptr<alert> clone() const;

		torrent_handle handle;
		int piece_index;
	};



parsing torrent files
---------------------

The torrent files are bencoded__. There are two functions in libtorrent that can encode and decode
bencoded data. They are::

	template<class InIt> entry bdecode(InIt start, InIt end);
	template<class OutIt> void bencode(OutIt out, const entry& e);

__ http://bitconjurer.org/BitTorrent/protocol.html


The ``entry`` class is the internal representation of the bencoded data
and it can be used to retreive information, an entry can also be build by
the program and given to ``bencode()`` to encode it into the ``OutIt``
iterator.

The ``OutIt`` and ``InIt`` are iterators
(``InputIterator_`` and ``OutputIterator_`` respectively). They
are templates and are usually instantiated as ``ostream_iterator_``,
``back_insert_iterator_`` or ``istream_iterator_``. These
functions will assume that the iterator refers to a character
(``char``). So, if you want to encode entry ``e`` into a buffer
in memory, you can do it like this::

	std::vector<char> buffer;
	bencode(std::back_insert_iterator<std::vector<char> >(buf), e);

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

Now we just need to know how to retrieve information from the ``entry``.




entry
-----

The ``entry`` class represents one node in a bencoded hierarchy. It works as a
variant type, it can be either a list, a dictionary (``std::map``), an integer
or a string. This is its synopsis::

	class entry
	{
	public:

		typedef std::map<std::string, entry> dictionary_type;
		typedef std::string string_type;
		typedef std::vector<entry> list_type;
		typedef implementation-defined integer_type;

		enum data_type
		{
			int_t,
			string_t,
			list_t,
			dictionary_t,
			undefined_t
		};

		data_type type() const;

		entry();
		entry(data_type t);
		entry(const entry& e);

		void operator=(const entry& e);

		integer_type& integer()
		const integer_type& integer() const;
		string_type& string();
		const string_type& string() const;
		list_type& list();
		const list_type& list() const;
		dictionary_type& dict();
		const dictionary_type& dict() const;

		void print(std::ostream& os, int indent = 0) const;
	};

The ``integer()``, ``string()``, ``list()`` and ``dict()`` functions
are accessorts that return the respecive type. If the ``entry`` object isn't of the
type you request, the accessor will throw ``type_error`` (which derives from
``std::runtime_error``). You can ask an ``entry`` for its type through the
``type()`` function.

The ``print()`` function is there for debug purposes only.

If you want to create an ``entry`` you give it the type you want it to have in its
constructor, and then use one of the non-const accessors to get a reference which you then
can assign the value you want it to have.

The typical code to get info from a torrent file will then look like this::

	entry torrent_file;
	// ...

	const entry::dictionary_type& dict = torrent_file.dict();
	entry::dictionary_type::const_iterator i;
	i = dict.find("announce");
	if (i != dict.end())
	{
		std::string tracker_url= i->second.string();
		std::cout << tracker_url << "\n";
	}

To make it easier to extract information from a torren file, the class ``torrent_info``
exists.

torrent_info
------------

The ``torrent_info`` has the following synopsis::

	class torrent_info
	{
	public:

		torrent_info(const entry& torrent_file)

		typedef std::vector>file>::const_iterator file_iterator;
		typedef std::vector<file>::const_reverse_iterator reverse_file_iterator;

		file_iterator begin_files() const;
		file_iterator end_files() const;
		reverse_file_iterator rbegin_files() const;
		reverse_file_iterator rend_files() const;

		std::size_t num_files() const;
		const file& file_at(int index) const;

		const std::vector<announce_entry>& trackers() const;

		int prioritize_tracker(int index);

		entry::integer_type total_size() const;
		entry::integer_type piece_length() const;
		std::size_t num_pieces() const;
		const sha1_hash& info_hash() const;
		const std::stirng& name() const;
		const std::string& comment() const;
		boost::posiz_time::ptime creation_date() const;


		void print(std::ostream& os) const;
	
		entry::integer_type piece_size(unsigned int index) const;
		const sha1_hash& hash_for_piece(unsigned int index) const;
	};

This class will need some explanation. First of all, to get a list of all files
in the torrent, you can use ``begin_files()``, ``end_files()``,
``rbegin_files()`` and ``rend_files()``. These will give you standard vector
iterators with the type ``file``.

::

	struct file
	{
		std::string path;
		std::string filename;
		entry::integer_type size;
	};

If you need index-access to files you can use the ``num_files()`` and ``file_at()``
to access files using indices.

The ``print()`` function is there for debug purposes only. It will print the info from
the torrent file to the given outstream.

``name()`` returns the name of the torrent.

The ``trackers()`` function will return a sorted vector of ``announce_entry``.
Each announce entry contains a string, which is the tracker url, and a tier index. The
tier index is the high-level priority. No matter which trackers that works or not, the
ones with lower tier will always be tried before the one with higher tier number.

::

	struct announce_entry
	{
		std::string url;
		int tier;
	};

The ``prioritize_tracker()`` is used internally to move a tracker to the front
of its tier group. i.e. It will never be moved pass a tracker with a different tier
number. For more information about how multiple trackers are dealt with, see the
specification_.

.. _specification: http://home.elp.rr.com/tur/multitracker-spec.txt


``total_size()``, ``piece_length()`` and ``num_pieces()`` returns the total
number of bytes the torrent-file represents (all the files in it), the number of byte for
each piece and the total number of pieces, respectively. The difference between
``piece_size()`` and ``piece_length()`` is that ``piece_size()`` takes
the piece index as argument and gives you the exact size of that piece. It will always
be the same as ``piece_length()`` except in the case of the last piece, which may
be smaller.

``hash_for_piece()`` takes a piece-index and returns the 20-bytes sha1-hash for that
piece and ``info_hash()`` returns the 20-bytes sha1-hash for the info-section of the
torrent file. For more information on the ``sha1_hash``, see the big_number_ class.

``comment()`` returns the comment associated with the torrent. If there's no comment,
it will return an empty string. ``creation_date()`` returns a `boost::posix_time::ptime`__
object, representing the time when this torrent file was created. If there's no timestamp
in the torrent file, this will return a date of january 1:st 1970.

__ http://www.boost.org/libs/date_time/doc/class_ptime.html




torrent_handle
--------------

You will usually have to store your torrent handles somewhere, since it's the
object through which you retrieve infromation about the torrent and aborts the torrent.
Its declaration looks like this::

	struct torrent_handle
	{
		torrent_handle();

		torrent_status status();
		void get_download_queue(std::vector<partial_piece_info>& queue);
		void get_peer_info(std::vector<peer_info>& v);
		const torrent_info& get_torrent_info();
		bool is_valid();

		void write_resume_data(std::vector<char>& data);

		boost::filsystem::path save_path() const;

		void set_max_uploads(int max_uploads);

		sha1_hash info_hash() const;

		bool operator==(const torrent_handle&) const;
		bool operator!=(const torrent_handle&) const;
		bool operator<(const torrent_handle&) const;
	};

The default constructor will initialize the handle to an invalid state. Which means you cannot
perform any operation on it, unless you first assign it a valid handle. If you try to perform
any operation on an uninitialized handle, it will throw ``invalid_handle``.

``save_path()`` returns the path that was given to ``add_torrent()`` when this torrent
was started.

``info_hash()`` returns the info hash for the torrent.

``set_max_uploads()`` sets the maximum number of peers that's unchoked at the same time on this
torrent. If you set this to -1, there will be no limit.

``write_resume_data()`` takes a non-const reference to a char-vector, that vector will be filled
with the fast-resume data. For more information about hpw fast-resume works, see `fast resume`_.

status()
~~~~~~~~

``status()`` will return a structure with information about the status of this
torrent. If the torrent_handle_ is invalid, it will throw invalid_handle_ exception.
It contains the following fields::

	struct torrent_status
	{
		enum state_t
		{
			invalid_handle,
			queued_for_checking,
			checking_files,
			connecting_to_tracker,
			downloading,
			seeding
		};
	
		state_t state;
		float progress;
		boost::posix_time::time_duration next_announce;

		std::size_t total_download;
		std::size_t total_upload;

		std::size_t total_payload_download;
		std::size_t total_payload_upload;

		float download_rate;
		float upload_rate;

		std::vector<bool> pieces;
		std::size_t total_done;
	};

``progress`` is a value in the range [0, 1], that represents the progress of the
torrent's current task. It may be checking files or downloading. The torrent's
current task is in the ``state`` member, it will be one of the following:

+--------------------------+----------------------------------------------------------+
|``queued_for_checking``   |The torrent is in the queue for being checked. But there  |
|                          |currently is another torrent that are being checked.      |
|                          |This torrent will wait for its turn.                      |
|                          |                                                          |
+--------------------------+----------------------------------------------------------+
|``checking_files``        |The torrent has not started its download yet, and is      |
|                          |currently checking existing files.                        |
|                          |                                                          |
+--------------------------+----------------------------------------------------------+
|``connecting_to_tracker`` |The torrent has sent a request to the tracker and is      |
|                          |currently waiting for a response                          |
|                          |                                                          |
+--------------------------+----------------------------------------------------------+
|``downloading``           |The torrent is being downloaded. This is the state        |
|                          |most torrents will be in most of the time. The progress   |
|                          |meter will tell how much of the files that has been       |
|                          |downloaded.                                               |
|                          |                                                          |
+--------------------------+----------------------------------------------------------+
|``seeding``               |In this state the torrent has finished downloading and    |
|                          |is a pure seeder.                                         |
|                          |                                                          |
+--------------------------+----------------------------------------------------------+

``next_announce`` is the time until the torrent will announce itself to the tracker.

``total_download`` and ``total_upload`` is the number of bytes downloaded and
uploaded to all peers, accumulated, *this session* only.

``total_payload_download`` and ``total_payload_upload`` counts the amount of bytes
send and received this session, but only the actual oayload data (i.e the interesting
data), these counters ignore any protocol overhead.

``pieces`` is the bitmask that representw which pieces we have (set to true) and
the pieces we don't have.

``download_rate`` and ``upload_rate`` are the total rates for all peers for this
torrent. These will usually have better precision than summing the rates from
all peers. The rates are given as the number of bytes per second.

``total_done`` is the total number of bytes of the file(s) that we have.

get_download_queue()
~~~~~~~~~~~~~~~~~~~~

``get_download_queue()`` takes a non-const reference to a vector which it will fill
information about pieces that are partially downloaded or not downloaded at all but partially
requested. The entry in the vector (``partial_piece_info``) looks like this::

	struct partial_piece_info
	{
		enum { max_blocks_per_piece };
		int piece_index;
		int blocks_in_piece;
		std::bitset<max_blocks_per_piece> requested_blocks;
		std::bitset<max_blocks_per_piece> finished_blocks;
		peer_id peer[max_blocks_per_piece];
		int num_downloads[max_blocks_per_piece];
	};

``piece_index`` is the index of the piece in question. ``blocks_in_piece`` is the
number of blocks in this particular piece. This number will be the same for most pieces, but
the last piece may have fewer blocks than the standard pieces.

``requested_blocks`` is a bitset with one bit per block in the piece. If a bit is set, it
means that that block has been requested, but not necessarily fully downloaded yet. To know
from whom the block has been requested, have a look in the ``peer`` array. The bit-index
in the ``requested_blocks`` and ``finished_blocks`` correspons to the array-index into
``peers`` and ``num_downloads``. The array of peers is contains the id of the
peer the piece was requested from. If a piece hasn't been requested (the bit in
``requested_blocks`` is not set) the peer array entry will be undefined.

The ``finished_blocks`` is a bitset where each bit says if the block is fully downloaded
or not. And the ``num_downloads`` array says how many times that block has been downloaded.
When a piece fails a hash verification, single blocks may be redownloaded to see if the hash teast
may pass then.


get_peer_info()
~~~~~~~~~~~~~~~

``get_peer_info()`` takes a reference to a vector that will be cleared and filled
with one entry for each peer connected to this torrent, given the handle is valid. If the
torrent_handle_ is invalid, it will throw ``invalid_handle`` exception. Each entry in
the vector contains information about that particular peer. It contains the following
fields::

	struct peer_info
	{
		enum
		{
			interesting = 0x1,
			choked = 0x2,
			remote_interested = 0x4,
			remote_choked = 0x8
		};
		unsigned int flags;
		address ip;
		float up_speed;
		float down_speed;
		unsigned int total_download;
		unsigned int total_upload;
		peer_id id;
		std::vector<bool> pieces;
		int upload_limit;
		int upload_ceiling;

		int load_balancing;

		int downloading_piece_index;
		int downloading_block_index;
		int downloading_progress;
		int downloading_total;
	};

The ``flags`` attribute tells you in which state the peer is. It is set to
any combination of the four enums above. Where ``interesting`` means that we
are interested in pieces from this peer. ``choked`` means that **we** have
choked this peer. ``remote_interested`` and ``remote_choked`` means the
same thing but that the peer is interested in pieces from us and the peer has choked
**us**.

The ``ip`` field is the IP-address to this peer. Its type is a wrapper around the
actual address and the port number. See address_ class.

``up_speed`` and ``down_speed`` is the current upload and download speed
we have to and from this peer. These figures are updated aproximately once every second.

``total_download`` and ``total_upload`` are the total number of bytes downloaded
from and uploaded to this peer. These numbers do not include the protocol chatter, but only
the payload data.

``id`` is the peer's id as used in the bit torrent protocol. This id can be used to
extract 'fingerprints' from the peer. Sometimes it can tell you which client the peer
is using.

``pieces`` is a vector of booleans that has as many entries as there are pieces
in the torrent. Each boolean tells you if the peer has that piece (if it's set to true)
or if the peer miss that piece (set to false).

``upload_limit`` is the number of bytes per second we are allowed to send to this
peer every second. It may be -1 if there's no limit. The upload limits of all peers
should sum up to the upload limit set by ``session::set_upload_limit``.

``upload_ceiling`` is the current maximum allowed upload rate given the cownload
rate and share ratio. If the global upload rate is inlimited, the ``upload_limit``
for every peer will be the same as their ``upload_ceiling``.

``load_balancing`` is a measurment of the balancing of free download (that we get)
and free upload that we give. Every peer gets a certain amount of free upload, but
this member says how much *extra* free upload this peer has got. If it is a negative
number it means that this was a peer from which we have got this amount of free
download.

You can know which piece, and which part of that piece, that is currently being
downloaded from a specific peer by looking at the next four members.
``downloading_piece_index`` is the index of the piece that is currently being downloaded.
This may be set to -1 if there's currently no piece downloading from this peer. If it is
>= 0, the other three members are valid. ``downloading_block_index`` is the index of the
block (or sub-piece) that is being downloaded. ``downloading_progress`` is the number
of bytes of this block we have received from the peer, and ``downloading_total`` is
the total number of bytes in this block.


get_torrent_info()
~~~~~~~~~~~~~~~~~~

Returns a const reference to the ``torrent_info`` object associated with this torrent.
This reference is valid as long as the torrent_handle_ is valid, no longer. If the
torrent_handle_ is invalid, invalid_handle_ exception will be thrown.


is_valid()
~~~~~~~~~~

Returns true if this handle refers to a valid torrent and false if it hasn't been initialized
or if the torrent it refers to has been aborted.


address
-------

The ``address`` class represents a name of a network endpoint (usually referred to as
IP-address) and a port number. This is the same thing as a ``sockaddr_in`` would contain.
Its declaration looks like this::

	class address
	{
	public:
		address();
		address(unsigned char a
			, unsigned char b
			, unsigned char c
			, unsigned char d
			, unsigned short  port);
		address(unsigned int addr, unsigned short port);
		address(const std::string& addr, unsigned short port);
		address(const address& a);
		~address();

		std::string as_string() const;
		unsigned int ip() const;
		unsigned short port() const;

		bool operator<(const address& a) const;
		bool operator!=(const address& a) const;
		bool operator==(const address& a) const;
	};

It is less-than comparable to make it possible to use it as a key in a map. ``as_string()`` may block
while it does the DNS lookup, it returns a string that points to the address represented by the object.

``ip()`` will return the 32-bit ip-address as an integer. ``port()`` returns the port number.




http_settings
-------------

You have some control over tracker requests through the ``http_settings`` object. You
create it and fill it with your settings and the use ``session::set_http_settings()``
to apply them. You have control over proxy and authorization settings and also the user-agent
that will be sent to the tracker. The user-agent is a good way to identify your client.

::

	struct http_settings
	{
		http_settings();
		std::string proxy_ip;
		int proxy_port;
		std::string proxy_login;
		std::string proxy_password;
		std::string user_agent;
		int tracker_timeout;
		int tracker_maximum_response_length;
	};

``proxy_ip`` may be a hostname or ip to a http proxy to use. If this is
an empty string, no http proxy will be used.

``proxy_port`` is the port on which the http proxy listens. If ``proxy_ip``
is empty, this will be ignored.

``proxy_login`` should be the login username for the http proxy, if this
empty, the http proxy will be trid to be used without authentication.

``proxy_password`` the password string for the http proxy.

``user_agent`` this is the client identification to the tracker. It will
be followed by the string "(libtorrent)" to identify that this library
is being used. This should be set to your client's name and version number.

``tracker_timeout`` is the number of seconds the tracker connection will
wait until it considers the tracker to have timed-out. Default value is 10
seconds.

``tracker_maximum_response_length`` is the maximum number of bytes in a
tracker response. If a response size passes this number it will be rejected
and the connection will be closed. On gzipped responses this size is measured
on the uncompressed data. So, if you get 20 bytes of gzip response that'll
expand to 2 megs, it will be interrupted before the entire response has been
uncompressed (given your limit is lower than 2 megs). Default limit is
1 megabyte.


big_number
----------

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
------

This class creates sha1-hashes. Its declaration looks like this::

	class hasher
	{
	public:
		hasher();

		void update(const char* data, unsigned int len);
		sha1_hash final();
		void reset();
	};


You use it by first instantiating it, then call ``update()`` to feed it
with data. i.e. you don't have to keep the entire buffer of which you want to
create the hash in memory. You can feed the hasher parts of it at a time. When
You have fed the hasher with all the data, you call ``final()`` and it
will return the sha1-hash of the data.

If you want to reuse the hasher object once you have created a hash, you have to
call ``reset()`` to reinitialize it.

The sha1-algorithm used was implemented by Steve Reid and released as public domain.
For more info, see ``src/sha1.c``.


fingerprint
-----------

The fingerprint class represents information about a client and its version. It is used
to encode this information into the client's peer id.

This is the class declaration::

	struct fingerprint
	{
		fingerprint(const char* id_string, int major, int minor, int revision, int tag);

		std::string to_string() const;

		char id[2];
		char major_version;
		char minor_version;
		char revision_version;
		char tag_version;

	};

The constructor takes a ``const char*`` that should point to a string constant containing
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

The ``major``, ``minor``, ``revision`` and ``tag`` parameters are used to identify the
version of your client. All these numbers must be within the range [0, 9].

``to_string()`` will generate the actual string put in the peer-id, and return it.

alert
-----

The ``alert`` class is used to pass messages of events from the libtorrent code
to the user. It is a base class that specific messages are derived from. This
is its synopsis::

	class alert
	{
	public:

		enum severity_t { debug, info, warning, critital, fatal, none };

		alert(severity_t severity, const std::string& msg);

		virtual ~alert() {}

		const std::string& msg() const;
		severity_t severity() const;

		virtual std::auto_ptr<alert> clone() const = 0;
	};




exceptions
----------

There are a number of exceptions that can be thrown from different places in libtorrent,
here's a complete list with description.


invalid_handle
~~~~~~~~~~~~~~

This exception is thrown when querying information from a torrent_handle_ that hasn't
been initialized or that has become invalid.

::

	struct invalid_handle: std::exception
	{
		const char* what() const throw();
	};


duplicate_torrent
~~~~~~~~~~~~~~~~~

This is thrown by ``session::add_torrent()`` if the torrent already has been added to
the session.

::

	struct duplicate_torrent: std::exception
	{
		const char* what() const throw();
	};


invalid_encoding
~~~~~~~~~~~~~~~~

This is thrown by ``bdecode()`` if the input data is not a valid bencoding.

::

	struct invalid_encoding: std::exception
	{
		const char* what() const throw();
	};


type_error
~~~~~~~~~~

This is thrown from the accessors of ``entry`` if the data type of the ``entry`` doesn't
match the type you want to extract from it.

::

	struct type_error: std::runtime_error
	{
		type_error(const char* error);
	};


invalid_torrent_file
~~~~~~~~~~~~~~~~~~~~

This exception is thrown from the constructor of ``torrent_info`` if the given bencoded information
doesn't meet the requirements on what information has to be present in a torrent file.

::

	struct invalid_torrent_file: std::exception
	{
		const char* what() const throw();
	};


example usage
-------------

dump_torrent
~~~~~~~~~~~~

This is an example of a program that will take a torrent-file as a parameter and
print information about it to std out::

	#include <iostream>
	#include <fstream>
	#include <iterator>
	#include <exception>
	#include <iomanip>

	#include "libtorrent/entry.hpp"
	#include "libtorrent/bencode.hpp"
	#include "libtorrent/torrent_info.hpp"


	int main(int argc, char* argv[])
	{
		using namespace libtorrent;
	
		if (argc != 2)
		{
			std::cerr << "usage: dump_torrent torrent-file\n";
			return 1;
		}

		try
		{
			std::ifstream in(argv[1], std::ios_base::binary);
			in.unsetf(std::ios_base::skipws);
			entry e = bdecode(std::istream_iterator<char>(in), std::istream_iterator<char>());
			torrent_info t(e);

			// print info about torrent
			std::cout << "\n\n----- torrent file info -----\n\n";
			std::cout << "trackers:\n";
			for (std::vector<announce_entry>::const_iterator i = t.trackers().begin();
				i != t.trackers().end();
				++i)
			{
				std::cout << i->tier << ": " << i->url << "\n";
			}

			std::cout << "number of pieces: " << t.num_pieces() << "\n";
			std::cout << "piece length: " << t.piece_length() << "\n";
			std::cout << "files:\n";
			for (torrent_info::file_iterator i = t.begin_files();
				i != t.end_files();
				++i)
			{
				std::cout << "  " << std::setw(11) << i->size
				<< "  " << i->path << " " << i->filename << "\n";
			}
			
		}
		catch (std::exception& e)
		{
	  		std::cout << e.what() << "\n";
		}

		return 0;
	}


simple client
~~~~~~~~~~~~~

This is a simple client. It doesn't have much output to keep it simple::

	#include <iostream>
	#include <fstream>
	#include <iterator>
	#include <exception>

	#include <boost/format.hpp>
	#include <boost/date_time/posix_time/posix_time.hpp>

	#include "libtorrent/entry.hpp"
	#include "libtorrent/bencode.hpp"
	#include "libtorrent/session.hpp"
	#include "libtorrent/http_settings.hpp"

	int main(int argc, char* argv[])
	{
		using namespace libtorrent;
	
		if (argc != 2)
		{
			std::cerr << "usage: ./simple_cient torrent-file\n"
				"to stop the client, press return.\n";
			return 1;
		}

		try
		{
			session s(6881);
	
			std::ifstream in(argv[1], std::ios_base::binary);
			in.unsetf(std::ios_base::skipws);
			entry e = bdecode(std::istream_iterator<char>(in), std::istream_iterator<char>());
			torrent_info t(e);
			s.add_torrent(t, "");
				
			// wait for the user to end
			char a;
			std::cin.unsetf(std::ios_base::skipws);
			std::cin >> a;
		}
		catch (std::exception& e)
		{
	  		std::cout << e.what() << "\n";
		}
		return 0;
	}


fast resume
-----------

The fast resume mechanism is a way to remember which pieces are downloaded and where they
are put between sessions. You can generate fast resume data by calling
``torrent_handle::write_resume_data()`` on torrent_handle_. You can then save this data
to disk and use it when resuming the torrent. libtorrent will not check the piece hashes
then, and rely on the information given in the fast-resume data. The fast-resume data
also contains information about which bocks in the unfinished pieces were downloaded, so
it will not have to start from scratch on the partially downloaded pieces.

To use the fast-resume data you simply give it to ``session::add_torrent()``, and it
will skip the time consuming checks. It may have to do the checking anyway, if the
fast-resume data is corrupt or doesn't fit the storage for that torrent, then it will
not trust the fast-resume data and just do the checking.

file format
~~~~~~~~~~~

The format of the fast-resume data is as follows, given that all
4-byte integers are stored as big-endian::

	20 bytes, the info_hash for the torrent
	4 bytes, the number of allocated slots in the storage
	for each slot
		4 bytes, piece index in this slot,
		         -1 means there's no storage for the slot
		         -2 means there's no piece at this slot, it's free
	4 bytes, the number of blocks per piece.
	         this must be piece_size / 16k or 1 if piece_size is < 16k
	         and can be 128 at max.
	4 bytes, the number of unfinished pieces
	for each unfinished piece
		4 bytes, index of the unfinished piece
		blocks_per_piece / 32 bytes, the bitmask describing which
		                             blocks are finished in this piece.

Feedback
========

There's a `mailing list`__.

__ http://lists.sourceforge.net/lists/listinfo/libtorrent-discuss

You can usually find me as hydri in ``#btports @ irc.freenode.net``.



Aknowledgements
===============

Written by Arvid Norberg and Daniel Wallin. Copyright (c) 2003

Contributions by Magnus Jonsson

Thanks to Reimond Retz for bugfixes, suggestions and testing

Project is hosted by sourceforge.

|sf_logo|__

.. |sf_logo| image:: http://sourceforge.net/sflogo.php?group_id=7994
__ http://sourceforge.net


