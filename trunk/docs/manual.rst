=================
libtorrent manual
=================

:Author: Arvid Norberg, arvid@rasterbar.com

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

introduction
============

libtorrent is a C++ library that aims to be a good alternative to all the
other bittorrent implementations around. It is a
library and not a full featured client, although it comes with a working
example client.

The main goals of libtorrent are:

	* to be cpu efficient
	* to be memory efficient
	* to be very easy to use

libtorrent is still being developed, however it is stable. It is an ongoing
project (including this documentation). The current state includes the
following features:

	* multitracker extension support (as `specified by John Hoffman`__)
	* serves multiple torrents on a single port and a single thread
	* supports http proxies and proxy authentication
	* gzipped tracker-responses
	* piece picking on block-level like in Azureus_ (as opposed to piece-level).
	  This means it can download parts of the same piece from different peers.
	  It will also prefer to download whole pieces from single peers if the
	  download speed is high enough from that particular peer.
	* queues torrents for file check, instead of checking all of them in parallel.
	* uses separate threads for checking files and for main downloader, with a
	  fool-proof thread-safe library interface. (i.e. There's no way for the
	  user to cause a deadlock). (see threads_)
	* can limit the upload and download bandwidth usage and the maximum number of
	  unchoked peers
	* piece-wise, unordered, incremental file allocation
	* implements fair trade. User settable trade-ratio, must at least be 1:1,
	  but one can choose to trade 1 for 2 or any other ratio that isn't unfair
	  to the other party.
	* fast resume support, a way to get rid of the costly piece check at the
	  start of a resumed torrent. Saves the storage state, piece_picker state
	  as well as all local peers in a separate fast-resume file.
	* supports an `extension protocol`__. See extensions_.
	* supports files > 2 gigabytes.
	* supports the ``no_peer_id=1`` extension that will ease the load off trackers.
	* supports the `udp-tracker protocol`__ by Olaf van der Spek.
	* possibility to limit the number of connections.
	* delays have messages if there's no other outgoing traffic to the peer, and
	  doesn't send have messages to peers that already has the piece. This saves
	  bandwidth.
	* does not have any requirements on the piece order in a torrent that it
	  resumes. This means it can resume a torrent downloaded by any client.
	* adjusts the length of the request queue depending on download rate.
	* supports the ``compact=1`` tracker parameter.
	* selective downloading. The ability to select which parts of a torrent you
	  want to download.
	* ip filter

__ http://home.elp.rr.com/tur/multitracker-spec.txt
.. _Azureus: http://azureus.sourceforge.net
__ extension_protocol.html
__ udp_tracker_protocol.html


libtorrent is portable at least among Windows, MacOS X and other UNIX-systems. It uses Boost.Thread,
Boost.Filesystem, Boost.Date_time and various other boost libraries as well as zlib.

libtorrent has been successfully compiled and tested on:

	* Windows 2000 vc7.1
	* Linux x86 GCC 3.0.4, GCC 3.2.3, GCC 3.4.2
	* MacOS X (darwin), (Apple's) GCC 3.3, (Apple's) GCC 4.0
	* SunOS 5.8 GCC 3.1
	* Cygwin GCC 3.3.3

Fails on:

	* GCC 2.95.4
	* msvc6 sp5

libtorrent is released under the BSD-license_.

.. _BSD-license: http://www.opensource.org/licenses/bsd-license.php


downloading and building
========================

To acquire the latest version of libtorrent, you'll have to grab it from CVS.
You'll find instructions on how to do this here__ (see Anonymous CVS access).

__ http://sourceforge.net/cvs/?group_id=79942

The build systems supported "out of the box" in libtorrent are boost-build v2
(BBv2) and autotools (for unix-like systems). If you still can't build after
following these instructions, you can usually get help in the ``#libtorrent``
IRC channel on ``irc.freenode.net``.


building with BBv2
------------------

The primary reason to use boost-build is that it will automatically build the
dependent boost libraries with the correct compiler settings, in order to
ensure that the build targets are link compatible (see `boost guidelines`__
for some details on this issue).

__ http://boost.org/more/separate_compilation.html

Since BBv2 will build the boost libraries for you, you need the full boost
source package. Having boost installed via some package system is usually not
enough (and even if it is enough, the necessary environment variables are
usually not set by the package installer).


Step 1: Download boost
~~~~~~~~~~~~~~~~~~~~~~

You'll find boost here__.

__ http://sourceforge.net/project/showfiles.php?group_id=7586

Extract the archive to some directory where you want it. For the sake of this
guide, let's assume you extract the package to ``c:\boost_1_33_0`` (I'm using
a windows path in this example since if you're on linux/unix you're more likely
to use the autotools). You'll need at least version 1.32 of the boost library
in order to build libtorrent.

If you use 1.32, you need to download BBv2 separately, so for now, let's
assume you will use version 1.33.


Step 2: Setup BBv2
~~~~~~~~~~~~~~~~~~

First you need to build ``bjam``. You do this by opening a terminal (In
windows, run ``cmd``). Change directory to
``c:\boost_1_33_0\tools\build\jam_src``. Then run the script called
``build.bat`` or ``build.sh`` on a unix system. This will build ``bjam`` and
place it in a directory starting with ``bin.`` and then have the name of your
platform. Copy the ``bjam.exe`` (or ``bjam`` on a unix system) to a place
that's in you shell's ``PATH``. On linux systems a place commonly used may be
``/usr/local/bin`` or on windows ``c:\windows`` (you can also add directories
to the search paths by modifying the environment variable called ``PATH``).

Now you have ``bjam`` installed. ``bjam`` can be considered an interpreter
that the boost-build system is implemented on. So boost-build uses ``bjam``.
So, to complete the installation you need to make two more things. You need to
set the environment variable ``BOOST_BUILD_PATH``. This is the path that tells
``bjam`` where it can find boost-build, your configuration file and all the
toolsets (descriptions used by boost-build to know how to use different
compilers on different platforms). Assuming the boost install path above, set
it to ``c:\boost_1_33_0\tools\build\v2``.

The last thing to do to complete the setup of BBv2 is to modify your
``user-config.jam`` file. It is located in ``c:\boost_1_33\tools\build\v2``.
Depending on your platform and which compiler you're using, you should add a
line for each compiler and compiler version you have installed on your system
that you want to be able to use with BBv2. For example, if you're using
Microsoft Visual Studio 7.1 (2003), just add a line::

  using msvc : 7.1 ;

If you use GCC, add the line::

  using gcc ;

If you have more than one version of GCC installed, you can add the
commandline used to invoke g++ after the version number, like this::

  using gcc : 3.3 : g++-3.3 ;
  using gcc : 4.0 : g++-4.0 ;

Another toolset worth mentioning is the ``darwin`` toolset (For MacOS X).
From Tiger (10.4) MacOS X comes with both GCC 3.3 and GCC 4.0. Then you can
use the following toolsets::

  using darwin : 3.3 : g++-3.3 ;
  using darwin : 4.0 : g++-4.0 ;

Note that the spaces around the semi-colons and colons are important!


Step 3: Building libtorrent
~~~~~~~~~~~~~~~~~~~~~~~~~~~

When building libtorrent, the ``Jamfile`` expects the environment variable
``BOOST_ROOT`` to be set to the boost installation directory. It uses this to
find the boost libraries it depends on, so they can be built and their headers
files found. So, set this to ``c:\boost_1_33_0``.

Then the only thing left is simply to invoke ``bjam``. If you want to specify
a specific toolset to use (compiler) you can just add that to the commandline.
For example::

  bjam msvc-7.1 link=static
  bjam gcc-3.3 link=static
  bjam darwin-4.0 link=static

To build different versions you can also just add the name of the build
variant. Some default build variants in BBv2 are ``release``, ``debug``,
``profile``.

If you're building on a platform where dlls share the same heap, you can build
libtorrent as a dll too, by typing ``link=shared``, or ``link=static`` to
explicitly build a static library.

If you want to explicitly say how to link against the runtime library, you
can set the ``runtime-link`` feature on the commandline, either to ``shared``
or ``static``. Most operating systems will only allow linking shared against
the runtime, but on windows you can do both. Example::

  bjam msvc-7.1 link=static runtime-link=staitc

The build targets are put in a directory called bin, and under it they are
sorted in directories depending on the toolset and build variant used.

To build the examples, just change directory to the examples directory and
invoke ``bjam`` from there. To build and run the tests, go to the test
directory and run ``bjam``.

Note that if you're building on windows using the ``msvc`` toolset, you cannot run it
from a cygwin terminal, you'll have to run it from a ``cmd`` terminal. The same goes for
cygwin, if you're building with gcc in cygwin you'll have to run it from a cygwin terminal.
Also, make sure the paths are correct in the different environments. In cygwin, the paths
(``BOOST_BUILD_PATH`` and ``BOOST_ROOT``) should be in the typical unix-format (e.g.
``/cygdrive/c/boost_1_33_0``). In the windows environment, they should have the typical
windows format (``c:/boost_1_33_0``).

The ``Jamfile`` will define ``NDEBUG`` when it's building a release build.
There are two other build variants available in the ``Jamfile``. debug_log
and release_log, these two variants inherits from the debug and release
variants respectively, but adds extra logging (``TORRENT_VERBOSE_LOGGING``).
For more build configuration flags see `Build configurations`_.

The ``Jamfile`` has the following build variants:

 * ``release`` - release version without any logging
 * ``release_log`` - release version with standard logging
 * ``release_vlog`` - release version with verbose logging (all peer connections are logged)
 * ``debug`` - debug version without any logging
 * ``debug_log`` - debug version with standard logging
 * ``debug_vlog`` - debug version with verbose logging


building with autotools
-----------------------

First of all, you need to install ``automake`` and ``autoconf``. Many
unix/linux systems comes with these preinstalled.

Step 1: Running configure
~~~~~~~~~~~~~~~~~~~~~~~~~

In your shell, change directory to the libtorrent directory and run
``./configure``. This will look for libraries and C++ features that libtorrent
is dependent on. If something is missing or can't be found it will print an
error telling you what failed.

The most likely problem you may encounter is that the configure script won't
find the boost libraries. Make sure you have boost installed on your system.
The easiest way to install boost is usually to use the preferred package
system on your platform. Usually libraries and headers are installed in
standard directories where the compiler will find them, but sometimes that
may not be the case. For example when installing boost on darwin using
darwinports (the package system based on BSD ports) all libraries are
installed to ``/opt/local/lib`` and headers are installed to
``/opt/local/include``. By default the compiler will not look in these
directories. You have to set the enviornment variables ``LDFLAGS`` and
``CXXFLAGS`` in order to make the compiler find those libs. In this example
you'd set them like this::

  export LDFLAGS=-L/opt/local/lib
  export CXXFLAGS=-I/opt/local/include

If you need to set these variables, it may be a good idea to add those lines
to your ``~/.profile`` or ``~/.tcshrc`` depending on your shell.

You know that the boost libraries were found if you see the following output
from the configure script::

  checking whether the Boost::DateTime library is available... yes
  checking for main in -lboost_date_time... yes
  checking whether the Boost::Filesystem library is available... yes
  checking for main in -lboost_filesystem... yes
  checking whether the Boost::Thread library is available... yes
  checking for main in -lboost_thread... yes

Another possible source of problems may be if the path to your libtorrent
directory contains spaces. Make sure you either rename the directories with
spaces in their names to remove the spaces or move the libtorrent directory.

Creating a debug build
~~~~~~~~~~~~~~~~~~~~~~

To tell configure to build a debug version (with debug info, asserts
and invariant checks enabled), you have to run the configure script
with the following option::

  ./configure --enable-debug=yes

Creating a release build
~~~~~~~~~~~~~~~~~~~~~~~~

To tell the configure to build a release version (without debug info,
asserts and invariant checks), you have to run the configure script
with the following option::

  ./configure --enable-debug=no

The above option make use of -DNDEBUG, which is used throughout libtorrent.

Step 2: Building libtorrent
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Once the configure script is run successfully, you just type ``make`` and
libtorrent, the examples and the tests will be built.

When libtorrent is built it may be a good idea to run the tests, you do this
my running ``make check``.

If you want to build a release version (without debug info, asserts and
invariant checks), you have to rerun the configure script and rebuild, like this::

  ./configure --disable-debug
  make clean
  make


building with other build systems
---------------------------------
  
If you're making your own project file, note that there are two versions of
the file abstraction. There's one ``file_win.cpp`` which relies on windows
file API that supports files larger than 2 Gigabytes. This does not work in
vc6 for some reason, possibly because it may require windows NT and above.
The other file, ``file.cpp`` is the default implementation that simply relies
on the standard low level io routines (``read()``, ``write()``, ``open()``
etc.), this implementation doesn't do anything special to support unicode
filenames, so if your target is Windows 2000 and up, you may want to use
``file_win.cpp`` which supports unicode filenames.

If you're building in MS Visual Studio, you may have to set the compiler
options "force conformance in for loop scope", "treat wchar_t as built-in
type" and "Enable Run-Time Type Info" to Yes.


build configurations
--------------------

By default libtorrent is built In debug mode, and will have pretty expensive
invariant checks and asserts built into it. If you want to disable such checks
(you want to do that in a release build) you can see the table below for which
defines you can use to control the build.

+--------------------------------+-------------------------------------------------+
| macro                          | description                                     |
+================================+=================================================+
| ``NDEBUG``                     | If you define this macro, all asserts,          |
|                                | invariant checks and general debug code will be |
|                                | removed. This option takes precedence over      |
|                                | other debug settings.                           |
+--------------------------------+-------------------------------------------------+
| ``TORRENT_LOGGING``            | This macro will enable logging of the session   |
|                                | events, such as tracker announces and incoming  |
|                                | connections (as well as blocked connections).   |
+--------------------------------+-------------------------------------------------+
| ``TORRENT_VERBOSE_LOGGING``    | If you define this macro, every peer connection |
|                                | will log its traffic to a log file as well as   |
|                                | the session log.                                |
+--------------------------------+-------------------------------------------------+
| ``TORRENT_STORAGE_DEBUG``      | This will enable extra expensive invariant      |
|                                | checks in the storage, including logging of     |
|                                | piece sorting.                                  |
+--------------------------------+-------------------------------------------------+
| ``UNICODE``                    | If building on windows this will make sure the  |
|                                | UTF-8 strings in pathnames are converted into   |
|                                | UTF-16 before they are passed to the file       |
|                                | operations.                                     |
+--------------------------------+-------------------------------------------------+
| ``LITTLE_ENDIAN``              | This will use the little endian version of the  |
|                                | sha-1 code. If defined on a big-endian system   |
|                                | the sha-1 hashes will be incorrect and fail.    |
|                                | If it is not defined and ``__BIG_ENDIAN__``     |
|                                | isn't defined either (it is defined by Apple's  |
|                                | GCC) both little-endian and big-endian versions |
|                                | will be built and the correct code will be      |
|                                | chosen at run-time.                             |
+--------------------------------+-------------------------------------------------+
| ``TORRENT_LINKING_SHARED``     | If this is defined when including the           |
|                                | libtorrent headers, the classes and functions   |
|                                | will be tagged with ``__declspec(dllimport)``   |
|                                | on msvc and default visibility on GCC 4 and     |
|                                | later. Set this in your project if you're       |
|                                | linking against libtorrent as a shared library. |
|                                | (This is set by the Jamfile when                |
|                                | ``link=shared`` is set).                        |
+--------------------------------+-------------------------------------------------+
| ``TORRENT_BUILDING_SHARED``    | If this is defined, the functions and classes   |
|                                | in libtorrent are marked with                   |
|                                | ``__declspec(dllexport)`` on msvc, or with      |
|                                | default visibility on GCC 4 and later. This     |
|                                | should be defined when building libtorrent as   |
|                                | a shared library. (This is set by the Jamfile   |
|                                | when ``link=shared`` is set).                   |
+--------------------------------+-------------------------------------------------+


If you experience that libtorrent uses unreasonable amounts of cpu, it will
definately help to define ``NDEBUG``, since it will remove the invariant checks
within the library.

overview
========

The interface of libtorrent consists of a few classes. The main class is
the ``session``, it contains the main loop that serves all torrents.

The basic usage is as follows:

* conststruct a session
* parse .torrent-files and add them to the session (see `bdecode() bencode()`_)
* main loop (see session_)

	* query the torrent_handles for progress (see torrent_handle_)
	* query the session for information
	* add and remove torrents from the session at run-time

* save resume data for all torrent_handles (optional, see
  `write_resume_data()`_)
* destruct session object

Each class and function is described in this manual.



session
=======

The ``session`` class has the following synopsis::

	class session: public boost::noncopyable
	{

		session(const fingerprint& print
			= libtorrent::fingerprint("LT", 0, 1, 0, 0));

		session(
			const fingerprint& print
			, std::pair<int, int> listen_port_range
			, const char* listen_interface = 0);

		torrent_handle add_torrent(
			torrent_info const& ti
			, boost::filesystem::path const& save_path
			, entry const& resume_data = entry()
			, bool compact_mode = true
			, int block_size = 16 * 1024);

		torrent_handle add_torrent(
			char const* tracker_url
			, sha1_hash const& info_hash
			, boost::filesystem::path const& save_path
			, entry const& resume_data = entry()
			, bool compact_mode = true
			, int block_size = 16 * 1024);

		void remove_torrent(torrent_handle const& h);

		void disable_extensions();
		void enable_extension(peer_connection::extension_index);

		void set_http_settings(const http_settings& settings);

		void set_upload_rate_limit(int bytes_per_second);
		void set_download_rate_limit(int bytes_per_second);
		void set_max_uploads(int limit);
		void set_max_connections(int limit);

		void set_ip_filter(ip_filter const& f);
      
		session_status status() const;

		bool is_listening() const;
		unsigned short listen_port() const;
		bool listen_on(
			std::pair<int, int> const& port_range
			, char const* interface = 0);


		std::auto_ptr<alert> pop_alert();
		void set_severity_level(alert::severity_t s);
	};

Once it's created, the session object will spawn the main thread that will do all the work.
The main thread will be idle as long it doesn't have any torrents to participate in.

session()
---------

	::

		session(fingerprint const& print = libtorrent::fingerprint("LT", 0, 1, 0, 0));
		session(fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = 0);

If the fingerprint in the first overload is ommited, the client will get a default
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
returns. So, it's adviced that any kind of interface (such as windows) are closed before
destructing the sessoin object. Because it can take a few second for it to finish. The
timeout can be set with ``set_http_settings()``.


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

The ``compact_mode`` paramater refers to the layout of the storage for this torrent. If
set to true (default), the storage will grow as more pieces are downloaded, and pieces
are rearranged to finally be in their correct places once the entire torrent has been
downloaded. If it is false, the entire storage is allocated before download begins. I.e.
the files contained in the torrent are filled with zeroes, and each downloaded piece
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
with extensions enabled (``TORRENT_ENABLE_EXTENSIONS`` defined).

remove_torrent()
----------------

	::

		void remove_torrent(torrent_handle const& h);

``remove_torrent()`` will close all peer connections associated with the torrent and tell
the tracker that we've stopped participating in the swarm.


disable_extensions() enable_extension()
---------------------------------------

	::

		void disable_extensions();
		void enable_extension(peer_connection::extension_index);

``disable_extensions()`` will disable all extensions available in libtorrent.
``enable_extension()`` will enable a single extension. The available extensions
are enumerated in the ``peer_connection`` class. These are the available extensions::

	enum extension_index
	{
		extended_chat_message,
		extended_metadata_message,
		extended_peer_exchange_message,
		extended_listen_port_message,
		num_supported_extensions
	};

*peer_exchange is not implemented yet*

By default, all extensions are enabled.
For more information about the extensions, see the extensions_ section.

set_upload_rate_limit() set_download_rate_limit()
-------------------------------------------------

	::

		void set_upload_rate_limit(int bytes_per_second);
		void set_download_rate_limit(int bytes_per_second);

``set_upload_rate_limit()`` set the maximum number of bytes allowed to be
sent to peers per second. This bandwidth is distributed among all the peers. If
you don't want to limit upload rate, you can set this to -1 (the default).
``set_download_rate_limit()`` works the same way but for download rate instead
of upload rate.


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


set_ip_filter()
---------------

	::

		void set_ip_filter(ip_filter const& filter);

Sets a filter that will be used to reject and accept incoming as well as outgoing
connections based on their originating ip address. The default filter will allow
connections to any ip address. To build a set of rules for which addresses are
accepted and not, see ip_filter_.


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


is_listening() listen_port() listen_on()
----------------------------------------

	::

		bool is_listening() const;
		unsigned short listen_port() const;
		bool listen_on(
			std::pair<int, int> const& port_range
			, char const* interface = 0);

``is_listening()`` will tell you wether or not the session has successfully
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


pop_alert() set_severity_level()
--------------------------------

	::

		std::auto_ptr<alert> pop_alert();
		void set_severity_level(alert::severity_t s);

``pop_alert()`` is used to ask the session if any errors or events has occured. With
``set_severity_level()`` you can filter how serious the event has to be for you to
receive it through ``pop_alert()``. For information, see alerts_.



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
are accessors that return the respecive type. If the ``entry`` object isn't of the
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

		typedef std::vector<file_entry>::const_iterator file_iterator;
		typedef std::vector<file_entry>::const_reverse_iterator
			reverse_file_iterator;

		file_iterator begin_files() const;
		file_iterator end_files() const;
		reverse_file_iterator rbegin_files() const;
		reverse_file_iterator rend_files() const;

		int num_files() const;
		file_entry const& file_at(int index) const;

		std::vector<announce_entry> const& trackers() const;

		size_type total_size() const;
		size_type piece_length() const;
		int num_pieces() const;
		sha1_hash const& info_hash() const;
		std::stirng const& name() const;
		std::string const& comment() const;
		std::string const& creator() const;

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

The contructor that takes an info-hash is identical to the default constructor with the
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
which the trackers are to be tried. For more iformation see `trackers()`_.

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

This function is not const because it will also set the info-hash of the ``torrent_info``
object.


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

::

	struct file_entry
	{
		boost::filesystem::path path;
		size_type size;
	};


num_files() file_at()
---------------------

	::
	
		int num_files() const;
		file_entry const& file_at(int index) const;

If you need index-access to files you can use the ``num_files()`` and ``file_at()``
to access files using indices.


print()
-------

	::

		void print(std::ostream& os) const;

The ``print()`` function is there for debug purposes only. It will print the info from
the torrent file to the given outstream.


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


name() comment() creation_date() creator()
------------------------------------------

	::

		std::string const& name() const;
		std::string const& comment() const;
		boost::optional<boost::posix_time::ptime> creation_date() const;

``name()`` returns the name of the torrent.

``comment()`` returns the comment associated with the torrent. If there's no comment,
it will return an empty string. ``creation_date()`` returns a `boost::posix_time::ptime`__
object, representing the time when this torrent file was created. If there's no timestamp
in the torrent file, this will return a date of january 1:st 1970.

Both the name and the comment is UTF-8 encoded strings.

``creator()`` returns the creator string in the torrent. If there is no creator string
it will return an empty string.

__ http://www.boost.org/libs/date_time/doc/class_ptime.html




torrent_handle
==============

You will usually have to store your torrent handles somewhere, since it's the
object through which you retrieve infromation about the torrent and aborts the torrent.
Its declaration looks like this::

	struct torrent_handle
	{
		torrent_handle();

		torrent_status status();
		void get_download_queue(std::vector<partial_piece_info>& queue) const;
		void get_peer_info(std::vector<peer_info>& v) const;
		torrent_info const& get_torrent_info() const;
		bool is_valid() const;

		entry write_resume_data() const;
		std::vector<char> const& metadata() const;
		void force_reannounce() const;
		void connect_peer(address const& adr) const;

		void set_tracker_login(std::string const& username
			, std::string const& password) const;

		std::vector<announce_entry> const& trackers() const;
		void replace_trackers(std::vector<announce_entry> const&);

		void set_ratio(float ratio) const;
		void set_max_uploads(int max_uploads) const;
		void set_max_connections(int max_connections) const;
		void set_upload_limit(int limit) const;
		void set_download_limit(int limit) const;
		void use_interface(char const* net_interface) const;

		void pause() const;
		void resume() const;
		bool is_paused() const;
		bool is_seed() const;

		void filter_piece(int index, bool filter) const;
		void filter_pieces(std::vector<bool> const& bitmask) const;
		bool is_piece_filtered(int index) const;
		std::vector<bool> filtered_pieces() const;

		void filter_files(std::vector<bool> const& files) const;
      
		bool has_metadata() const;

		boost::filsystem::path save_path() const;
		bool move_storage(boost::filesystem::path const& save_path) const;

		sha1_hash info_hash() const;

		bool operator==(torrent_handle const&) const;
		bool operator!=(torrent_handle const&) const;
		bool operator<(torrent_handle const&) const;
	};

The default constructor will initialize the handle to an invalid state. Which means you cannot
perform any operation on it, unless you first assign it a valid handle. If you try to perform
any operation on an uninitialized handle, it will throw ``invalid_handle``.

*TODO: document filter_piece(), filter_pieces(), is_piece_filtered(), filtered_pieces() and filter_files()*

save_path()
-----------

	::

		boost::filsystem::path save_path() const;

``save_path()`` returns the path that was given to `add_torrent()`_ when this torrent
was started.

move_storage()
--------------

	::

		bool move_storage(boost::filsystem::path const& save_path) const;

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

		void connect_peer(address const& adr) const;

``connect_peer()`` is a way to manually connect to peers that one believe is a part of the
torrent. If the peer does not respond, or is not a member of this torrent, it will simply
be disconnected. No harm can be done by using this other than an unnecessary connection
attempt is made. If the torrent is uninitialized or in queued or checking mode, this
will throw invalid_handle_.


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


set_upload_limit() set_download_limit()
---------------------------------------

	::

		void set_upload_limit(int limit) const;
		void set_download_limit(int limit) const;

``set_upload_limit`` will limit the upload bandwidth used by this particular torrent to the
limit you set. It is given as the number of bytes per second the torrent is allowed to upload.
``set_download_limit`` works the same way but for download bandwidth instead of upload bandwidth.
Note that setting a higher limit on a torrent then the global limit (``session::set_upload_rate_limit``)
will not override the global rate limit. The torrent can never upload more than the global rate
limit.


pause() resume() is_paused()
----------------------------

	::

		void pause() const;
		void resume() const;
		bool is_paused() const;

``pause()``, and ``resume()`` will disconnect all peers and reconnect all peers respectively.
When a torrent is paused, it will however remember all share ratios to all peers and remember
all potential (not connected) peers. You can use ``is_paused()`` to determine if a torrent
is currently paused. Torrents may be paused automatically if there is a file error (eg. disk full)
or something similar. See file_error_alert_.

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
announce entry contains both a string ``url`` which specifu the announce url
for the tracker as well as an int ``tier``, which is specifies the order in
which this tracker is tried. If you want libtorrent to use another list of
trackers for this torrent, you can use ``replace_trackers()`` which takes
a list of the same form as the one returned from ``trackers()`` and will
replace it. If you want an immediate effect, you have to call
`force_reannounce()`_.


use_interface()
---------------

	::

		void use_interface(char const* net_interface) const;

``use_interface()`` sets the network interface this torrent will use when it opens outgoing
connections. By default, it uses the same interface as the session_ uses to listen on. The
parameter can be a string containing an ip-address or a hostname.


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


metadata()
-------------------

	::

		std::vector<char> const& metadata() const;

``metadata()`` will return a reference to a buffer containing the exact info part of the
.torrent file. This buffer will be valid as long as the torrent is still running. When hashed,
it will produce the same hash as the info-hash.


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
in the ``requested_blocks`` and ``finished_blocks`` correspons to the array-index into
``peers`` and ``num_downloads``. The array of peers is contains the address of the
peer the piece was requested from. If a piece hasn't been requested (the bit in
``requested_blocks`` is not set) the peer array entry will be undefined.

The ``finished_blocks`` is a bitset where each bit says if the block is fully downloaded
or not. And the ``num_downloads`` array says how many times that block has been downloaded.
When a piece fails a hash verification, single blocks may be redownloaded to see if the hash teast
may pass then.


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

		float download_rate;
		float upload_rate;

		float download_payload_rate;
		float upload_payload_rate;

		int num_peers;

		int num_complete;
		int num_incomplete;

		const std::vector<bool>* pieces;
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
send and received this session, but only the actual oayload data (i.e the interesting
data), these counters ignore any protocol overhead.

``total_failed_bytes`` is the number of bytes that has been downloaded and that
has failed the piece hash test. In other words, this is just how much crap that
has been downloaded.

``pieces`` is the bitmask that represents which pieces we have (set to true) and
the pieces we don't have. It's a pointer and may be set to 0 if the torrent isn't
downloading or seeding.

``download_rate`` and ``upload_rate`` are the total rates for all peers for this
torrent. These will usually have better precision than summing the rates from
all peers. The rates are given as the number of bytes per second. The
``download_payload_rate`` and ``upload_payload_rate`` respectively is the
total transfer rate of payload only, not counting protocol chatter. This might
be slightly smaller than the other rates, but if projected over a long time
(e.g. when calculating ETA:s) the difference may be noticable.

``num_peers`` is the number of peers this torrent currently is connected to.

``num_complete`` and ``num_incomplete`` are set to -1 if the tracker did not
send any scrape data in its announce reply. This data is optional and may
not be available from all trackers. If these are not -1, they are the total
number of peers that are seeding (complete) and the total number of peers
that are still downloading (incomplete) this torrent.

``total_done`` is the total number of bytes of the file(s) that we have. All
this does not necessarily has to be downloaded during this session (that's
``total_download_payload``).

``total_wanted_done`` is the number of bytes we have downloadd, only counting the
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
			connecting = 0x40,
			queued = 0x80
		};
		unsigned int flags;
		address ip;
		float up_speed;
		float down_speed;
		float payload_up_speed;
		float payload_down_speed;
		size_type total_download;
		size_type total_upload;
		peer_id id;
		std::vector<bool> pieces;
		bool seed;
		int upload_limit;
		int upload_ceiling;

		size_type load_balancing;

		int download_queue_length;
		int upload_queue_length;

		int downloading_piece_index;
		int downloading_block_index;
		int downloading_progress;
		int downloading_total;
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
|                         | address_ of this peer. If this flag is not set, this  |
|                         | peer connection was opened by this peer connecting to |
|                         | us.                                                   |
+-------------------------+-------------------------------------------------------+
| ``connecting``          | The connection is in a half-open state (i.e. it is    |
|                         | being connected).                                     |
+-------------------------+-------------------------------------------------------+
| ``queued``              | The connection is currently queued for a connection   |
|                         | attempt. This may happen if there is a limit set on   |
|                         | the number of half-open TCP connections.              |
+-------------------------+-------------------------------------------------------+

__ extension_protocol.html

The ``ip`` field is the IP-address to this peer. Its type is a wrapper around the
actual address and the port number. See address_ class.

``up_speed`` and ``down_speed`` contains the current upload and download speed
we have to and from this peer (including any protocol messages). The transfer rates
of payload data only are found in ``payload_up_speed`` and ``payload_down_speed``.
These figures are updated aproximately once every second.

``total_download`` and ``total_upload`` are the total number of bytes downloaded
from and uploaded to this peer. These numbers do not include the protocol chatter, but only
the payload data.

``id`` is the peer's id as used in the bit torrent protocol. This id can be used to
extract 'fingerprints' from the peer. Sometimes it can tell you which client the peer
is using. See identify_client()_

``pieces`` is a vector of booleans that has as many entries as there are pieces
in the torrent. Each boolean tells you if the peer has that piece (if it's set to true)
or if the peer miss that piece (set to false).

``seed`` is true if this peer is a seed.

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



address
=======

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
=============

You have some control over tracker requests through the ``http_settings`` object. You
create it and fill it with your settings and then use ``session::set_http_settings()``
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
empty, the http proxy will be tried to be used without authentication.

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


ip_filter
=========

The ``ip_filter`` class is a set of rules that uniquely categorizes all
ip addresses as allowed or disallowed. The default constructor creates
a single rule that allowes all addresses (0.0.0.0 - 255.255.255.255).

	::

		class ip_filter
		{
		public:
			enum access_flags { blocked = 1 };

			ip_filter();
			void add_rule(address first, address last, int flags);
			int access(address const& addr) const;

			struct ip_range
			{
				address first;
				address last;
				int flags;
			};

			std::vector<ip_range> export_filter() const;
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
can currenly be 0, which means allowed, or ``ip_filter::blocked``, which
means disallowed.

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

		std::vector<ip_range> export_filter() const;

This function will return the current state of the filter in the minimum number of
ranges possible. They are sorted from ranges in low addresses to high addresses. Each
entry in the returned vector is a range with the access control specified in its
``flags`` field.

      
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

		char id[2];
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

bdecode() bencode()
-------------------

	::

		template<class InIt> entry bdecode(InIt start, InIt end);
		template<class OutIt> void bencode(OutIt out, const entry& e);


These functions will encode data to bencoded_ or decode bencoded_ data.

.. _bencoded: http://wiki.theory.org/index.php/BitTorrentSpecification

The entry_ class is the internal representation of the bencoded data
and it can be used to retreive information, an entry_ can also be build by
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
occured any errors (matching your severity level) ``pop_alert()`` will
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
| ``debug``    | This will include alot of debug events that can be used  |
|              | both for debugging libtorrent but also when debugging    |
|              | other clients that are connected to libtorrent. It will  |
|              | report strange behaviors among the connected peers.      |
|              |                                                          |
+--------------+----------------------------------------------------------+

When setting a severity level, you will receive messages of that severity and all
messages that are more sever. If you set ``alert::none`` (the default) you will not recieve
any events at all.

When you set a severuty level other than ``none``, you have the responsibility to call
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

		enum severity_t { debug, info, warning, critital, fatal, none };

		alert(severity_t severity, const std::string& msg);
		virtual ~alert();

		std::string const& msg() const;
		severity_t severity() const;

		virtual std::auto_ptr<alert> clone() const = 0;
	};

This means that all alerts have at least a string describing it. They also
have a severity level that can be used to sort them or present them to the
user in different ways.

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


file_error_alert
----------------

If the storage fails to read or write files that it needs access to, this alert is
generated and the torrent is paused. It is generated as severity level ``fatal``.

::

	struct file_error_alert: alert
	{
		file_error_alert(
			const torrent_handle& h
			, const std::string& msg);
			
		virtual std::auto_ptr<alert> clone() const;
		torrent_handle handle;
	};


tracker_announce_alert
----------------------

This alert is generated each time a tracker announce is sent (or attempted to be sent).
It is generated at severity level ``info``.

::

	struct tracker_announce_alert: alert
	{
		tracker_announce_alert(
			const torrent_handle& h
			, const std::string& msg);
			
		virtual std::auto_ptr<alert> clone() const;
		torrent_handle handle;
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

	struct tracker_alert: alert
	{
		tracker_alert(const torrent_handle& h, int times, int status
			, const std::string& msg);
		virtual std::auto_ptr<alert> clone() const;

		torrent_handle handle;
		int times_in_row;
		int status_code;
	};


tracker_reply_alert
-------------------

This alert is only for informational purpose. It is generated when a tracker announce
succeeds. It is generated with severity level ``info``.

::

	struct tracker_reply_alert: alert
	{
		tracker_reply_alert(const torrent_handle& h
			, const std::string& msg);

		virtual std::auto_ptr<alert> clone() const;
		torrent_handle handle;
	};
	
tracker_warning_alert
---------------------

This alert is triggered if the tracker reply contains a warning field. Usually this
means that the tracker announce was successful, but the tracker has a message to
the client. The message string in the alert will contain the warning message from
the tracker. It is generated with severity level ``warning``.

::

	struct tracker_warning_alert: alert
	{
		tracker_warning_alert(torrent_handle const& h
			, std::string const& msg);

		virtual std::auto_ptr<alert> clone() const;
		torrent_handle handle;
	};


   
hash_failed_alert
-----------------

This alert is generated when a finished piece fails its hash check. You can get the handle
to the torrent which got the failed piece and the index of the piece itself from the alert.
This alert is generated as severity level ``info``.

::

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


peer_ban_alert
--------------

This alert is generated when a peer is banned because it has sent too many corrupt pieces
to us. It is generated at severity level ``info``. The ``handle`` member is a torrent_handle_
to the torrent that this peer was a member of.

::

	struct peer_ban_alert: alert
	{
		peer_ban_alert(
			address const& pip
			, torrent_handle h
			, const std::string& msg);

		virtual std::auto_ptr<alert> clone() const;
		address ip;
		torrent_handle handle;
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
			address const& pip
			, peer_id const& pid
			, const std::string& msg);

		virtual std::auto_ptr<alert> clone() const;
		address ip;
		peer_id id;
	};


invalid_request_alert
---------------------

This is a debug alert that is generated by an incoming invalid piece request. The ``handle``
is a handle to the torrent the peer is a member of. ``ìp`` is the address of the peer and the
``request`` is the actual incoming request from the peer. The alert is generated as severity level
``debug``.

::

	struct invalid_request_alert: alert
	{
		invalid_request_alert(
			peer_request const& r
			, torrent_handle const& h
			, address const& send
			, peer_id const& pid
			, std::string const& msg);

		virtual std::auto_ptr<alert> clone() const;
		torrent_handle handle;
		address ip;
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

	struct torrent_finished_alert: alert
	{
		torrent_finished_alert(
			const torrent_handle& h
			, const std::string& msg);

		virtual std::auto_ptr<alert> clone() const;
		torrent_handle handle;
	};


metadata_failed_alert
---------------------

This alert is generated when the metadata has been completely received and the info-hash
failed to match it. i.e. the metadata that was received was corrupt. libtorrent will
automatically retry to fetch it in this case. This is only relevant when running a
torrent-less download, with the metadata extension provided by libtorrent.
It is generated at severity level ``info``.

::

	struct metadata_received_alert: alert
	{
		metadata_received_alert(
			const torrent_handle& h
			, const std::string& msg);
			
		virtual std::auto_ptr<alert> clone() const;
		torrent_handle handle;
	};


metadata_received_alert
-----------------------

This alert is generated when the metadata has been completely received and the torrent
can start downloading. It is not generated on torrents that are started with metadata, but
only those that needs to download it from peers (when utilizing the libtorrent extension).
It is generated at severity level ``info``.

::

	struct metadata_received_alert: alert
	{
		metadata_received_alert(
			const torrent_handle& h
			, const std::string& msg);
			
		virtual std::auto_ptr<alert> clone() const;
		torrent_handle handle;
	};


fastresume_rejected_alert
-------------------------

This alert is generated when a fastresume file has been passed to ``add_torrent`` but the
files on disk did not match the fastresume file. The string explaints the reason why the
resume file was rejected. It is generated at severity level ``warning``.

::

	struct fastresume_rejected_alert: alert
	{
		fastresume_rejected_alert(torrent_handle const& h
			, std::string const& msg);

		virtual std::auto_ptr<alert> clone() const;
		torrent_handle handle;
	};



.. chat_message_alert
	------------------

	This alert is generated when you receive a chat message from another peer. Chat messages
	are supported as an extension ("chat"). It is generated as severity level ``critical``,
	even though it doesn't necessarily require any user intervention, it's high priority
	since you would almost never want to ignore such a message. The alert class contain
	a torrent_handle_ to the torrent in which the sender-peer is a member and the ip
	of the sending peer.

	::

		struct chat_message_alert: alert
		{
			chat_message_alert(const torrent_handle& h
				, const address& sender
				, const std::string& msg);
	
			virtual std::auto_ptr<alert> clone() const;
	
			torrent_handle handle;
			address ip;
		};


dispatcher
----------

*TODO: describe the dispatcher mechanism*



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


examples
========

Except for the example programs in this manual, there's also a bigger example
of a (little bit) more complete client, ``client_test``. There are separate
instructions for how to use it here__ if you'd like to try it.

__ client_test.html

dump_torrent
------------

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
			entry e = bdecode(std::istream_iterator<char>(in)
				, std::istream_iterator<char>());
			torrent_info t(e);

			// print info about torrent
			std::cout << "\n\n----- torrent file info -----\n\n";
			std::cout << "trackers:\n";
			for (std::vector<announce_entry>::const_iterator i
				= t.trackers().begin(), end(t.trackers().end); i != end; ++i)
			{
				std::cout << i->tier << ": " << i->url << "\n";
			}

			std::cout << "number of pieces: " << t.num_pieces() << "\n";
			std::cout << "piece length: " << t.piece_length() << "\n";
			std::cout << "files:\n";
			for (torrent_info::file_iterator i = t.begin_files();
				i != t.end_files(); ++i)
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
-------------

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
			session s;
			s.listen_on(std::make_pair(6881, 6889));
	
			std::ifstream in(argv[1], std::ios_base::binary);
			in.unsetf(std::ios_base::skipws);
			entry e = bdecode(std::istream_iterator<char>(in)
				, std::istream_iterator<char>());
			s.add_torrent(torrent_info(e), "");
				
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

make_torrent
------------

Shows how to create a torrent from a directory tree::

	#include <iostream>
	#include <fstream>
	#include <iterator>
	#include <iomanip>

	#include "libtorrent/entry.hpp"
	#include "libtorrent/bencode.hpp"
	#include "libtorrent/torrent_info.hpp"
	#include "libtorrent/file.hpp"
	#include "libtorrent/storage.hpp"
	#include "libtorrent/hasher.hpp"

	#include <boost/filesystem/operations.hpp>
	#include <boost/filesystem/path.hpp>
	#include <boost/filesystem/fstream.hpp>

	using namespace boost::filesystem;
	using namespace libtorrent;

	void add_files(torrent_info& t, path const& p, path const& l)
	{
		path f(p / l);
		if (is_directory(f))
		{
			for (directory_iterator i(f), end; i != end; ++i)
				add_files(t, p, l / i->leaf());
		}
		else
		{
			std::cerr << "adding \"" << l.string() << "\"\n";
			file fi(f, file::in);
			fi.seek(0, file::end);
			libtorrent::size_type size = fi.tell();
			t.add_file(l, size);
		}
	}

	int main(int argc, char* argv[])
	{
		using namespace libtorrent;
		using namespace boost::filesystem;

		if (argc != 4)
		{
			std::cerr << "usage: make_torrent <output torrent-file> "
				"<announce url> <file or directory to create torrent from>\n";
			return 1;
		}

		boost::filesystem::path::default_name_check(native);

		try
		{
			torrent_info t;
			path full_path = initial_path() / path(argv[3]);
			ofstream out(initial_path() / path(argv[1]), std::ios_base::binary);

			int piece_size = 256 * 1024;
			char const* creator_str = "libtorrent";

			add_files(t, full_path.branch_path(), full_path.leaf());
			t.set_piece_size(piece_size);

			storage st(t, full_path.branch_path());
			t.add_tracker(argv[2]);

			// calculate the hash for all pieces
			int num = t.num_pieces();
			std::vector<char> buf(piece_size);
			for (int i = 0; i < num; ++i)
			{
				st.read(&buf[0], i, 0, t.piece_size(i));
				hasher h(&buf[0], t.piece_size(i));
				t.set_hash(i, h.final());
				std::cerr << (i+1) << "/" << num << "\r";
			}

			t.set_creator(creator_str);

			// create the torrent and print it to out
			entry e = t.create_torrent();
			libtorrent::bencode(std::ostream_iterator<char>(out), e);
		}
		catch (std::exception& e)
		{
			std::cerr << e.what() << "\n";
		}

		return 0;
	}


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
| ``slots``            | list of integers. The list mappes slots to piece indices. It |
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
|                      | | ``ip``   | string, the ip address of the peer.           | |
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
|                      | is the timestamp when the last time someone wrote to it.     |
|                      | This information is used to compare with the files on disk.  |
|                      | All the files must match exactly this information in order   |
|                      | to consider the resume data as current. Otherwise a full     |
|                      | re-check is issued.                                          |
+----------------------+--------------------------------------------------------------+

threads
=======

libtorrent starts 3 threads.

 * The first thread is the main thread that will sit
   idle in a ``select()`` call most of the time. This thread runs the main loop
   that will send and receive data on all connections.
   
 * The second thread is a hash-check thread. Whenever a torrent is added it will
   first be passed to this thread for checking the files that may already have been
   downloaded. If there is any resume data this thread will make sure it is valid
   and matches the files. Once the torrent has been checked, it is passed on to the
   main thread that will start it. The hash-check thread has a queue of torrents,
   it will only check one torrent at a time.

 * The third thread is spawned the first time a tracker is contacted. It is used
   for doing calls to ``gethostbyname()``. Since this call is blocking (and may block
   for several seconds if the dns server is down or slow) it is necessary to run this
   in its own thread to avoid stalling the main thread.


storage allocation
==================

There are two modes in which storage (files on disk) are allocated in libtorrent.

 * The traditional *full allocation* mode, where the entire files are filled up with
   zeroes before anything is downloaded.

 * And the *compact allocation* mode, where only files are allocated for actual
   pieces that have been downloaded. This is the default allocation mode in libtorrent.

The allocation mode is selected when a torrent is started. It is passed as a boolean
argument to ``session::add_torrent()`` (see `add_torrent()`_). These two modes have
different drawbacks and benefits.

full allocation
---------------

When a torrent is started in full allocation mode, the checker thread (see threads_)
will make sure that the entire storage is allocated, and fill any gaps with zeroes.
It will of course still check for existing pieces and fast resume data. The main
drawbacks of this mode are:

 * It will take longer to start the torrent, since it will need to fill the files
   with zeroes. This delay is linearly dependent on the size of the download.

 * The download will occupy unnecessary disk space between download sessions.

 * Disk caches usually perform extremely poorly with random access to large files
   and may slow down a download considerably.

The benefit of thise mode are:

 * Downloaded pieces are written directly to their final place in the files and the
   total number of disk operations will be fewer and may also play nicer to
   filesystems' file allocation, and reduce fragmentation.

 * No risk of a download failing because of a full disk during download.


compact allocation
------------------

The compact allocation will only allocate as much storage as it needs to keep the
pieces downloaded so far. This means that pieces will be moved around to be placed
at their final position in the files while downloading (to make sure the completed
download has all its pieces in the correct place). So, the main drawbacks are:

 * More disk operations while downloading since pieces are moved around.

 * Potentially more fragmentation in the filesystem.

The benefits though, are:

 * No startup delay, since the files doesn't need allocating.

 * The download will not use unnecessary disk space.

 * Disk caches perform much better than in full allocation and raises the download
   speed limit imposed by the disk.

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

chat messages
-------------

Extension name: "chat"

The payload in the packet is a bencoded dictionary with any
combination of the following entries:

   +----------+--------------------------------------------------------+
   | "msg"    | This is a string that contains a message that          |
   |          | should be displayed to the user.                       |
   +----------+--------------------------------------------------------+
   | "ctrl"   | This is a control string that can tell a client that   |
   |          | it is ignored (to make the user aware of that) and     |
   |          | it can also tell a client that it is no longer ignored.|
   |          | These notifications are encoded as the strings:        |
   |          | "ignored" and "not ignored".                           |
   |          | Any unrecognized strings should be ignored.            |
   +----------+--------------------------------------------------------+

metadata from peers
-------------------

Extension name: "metadata"

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


filename checks
===============

Boost.Filesystem will by default check all its paths to make sure they conform
to filename requirements on many platforms. If you don't want this check, you can
set it to either only check for native filesystem requirements or turn it off
alltogether. You can use::

	boost::filesystem::path::default_name_check(boost::filesystem::native);

for example. For more information, see the `Boost.Filesystem docs`__.

__ http://www.boost.org/libs/filesystem/doc/index.htm


acknowledgements
================

Written by Arvid Norberg. Copyright |copy| 2003-2005

Contributions by Magnus Jonsson, Daniel Wallin and Cory Nelson

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


