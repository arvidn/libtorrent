=================
libtorrent manual
=================

:Author: Arvid Norberg, arvid@libtorrent.org
:Version: 1.0.8

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

downloading and building
========================

To acquire the latest version of libtorrent, you'll have to grab it from SVN.
You'll find instructions on how to do this here__ (see subversion access).

__ http://sourceforge.net/svn/?group_id=79942

The build systems supported "out of the box" in libtorrent are boost-build v2
(BBv2) and autotools (for unix-like systems). If you still can't build after
following these instructions, you can usually get help in the ``#libtorrent``
IRC channel on ``irc.freenode.net``.

.. warning::

	A common mistake when building and linking against libtorrent is
	to build with one set of configuration options (#defines) and
	link against it using a different set of configuration options. Since
	libtorrent has some code in header files, that code will not be
	compatible with the built library if they see different configurations.

	Always make sure that the same TORRENT_* macros are defined when you
	link against libtorrent as when you build it.

	Boost-build supports propagating configuration options to dependencies.
	When building using the makefiles, this is handled by setting the
	configuration options in the pkg-config file. Always use pkg-config
	when linking against libtorrent.

building from svn
-----------------

To build libtorrent from svn you need to check out the libtorrent sources from
sourceforge. If you downloaded a release tarball, you can skip this section.

To check out libtorrent follow these instructions__.

__ http://sourceforge.net/svn/?group_id=79942

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

If you want to build against an installed copy of boost, you can skip directly
to step 3 (assuming you also have boost build installed).


Step 1: Download boost
~~~~~~~~~~~~~~~~~~~~~~

You'll find boost here__.

__ http://sourceforge.net/project/showfiles.php?group_id=7586&package_id=8041&release_id=619445

Extract the archive to some directory where you want it. For the sake of this
guide, let's assume you extract the package to ``c:\boost_1_34_0`` (I'm using
a windows path in this example since if you're on linux/unix you're more likely
to use the autotools). You'll need at least version 1.34 of the boost library
in order to build libtorrent.


Step 2: Setup BBv2
~~~~~~~~~~~~~~~~~~

First you need to build ``bjam``. You do this by opening a terminal (In
windows, run ``cmd``). Change directory to
``c:\boost_1_34_0\tools\jam\src``. Then run the script called
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
it to ``c:\boost_1_34_0\tools\build\v2``.

To set an environment variable in windows, type for example::

  set BOOST_BUILD_PATH=c:\boost_1_34_0\tools\build\v2

In a terminal window.

The last thing to do to complete the setup of BBv2 is to modify your
``user-config.jam`` file. It is located in ``c:\boost_1_34_0\tools\build\v2``.
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

Also see the `official installation instructions`_.

.. _`official installation instructions`: http://www.boost.org/doc/html/bbv2/installation.html


Step 3: Building libtorrent
~~~~~~~~~~~~~~~~~~~~~~~~~~~

When building libtorrent, the ``Jamfile`` expects the environment variable
``BOOST_ROOT`` to be set to the boost installation directory. It uses this to
find the boost libraries it depends on, so they can be built and their headers
files found. So, set this to ``c:\boost_1_34_0``. You only need this if you're
building against a source distribution of boost.

Then the only thing left is simply to invoke ``bjam``. If you want to specify
a specific toolset to use (compiler) you can just add that to the commandline.
For example::

  bjam msvc-7.1 boost=source
  bjam gcc-3.3 boost=source
  bjam darwin-4.0 boost=source

If you're building against a system installed boost, specify ``boost=system``.

To build different versions you can also just add the name of the build
variant. Some default build variants in BBv2 are ``release``, ``debug``,
``profile``.

You can build libtorrent as a dll too, by typing ``link=shared``, or
``link=static`` to build a static library.

If you want to explicitly say how to link against the runtime library, you
can set the ``runtime-link`` feature on the commandline, either to ``shared``
or ``static``. Most operating systems will only allow linking shared against
the runtime, but on windows you can do both. Example::

  bjam msvc-7.1 link=static runtime-link=static boost=source

.. note::

	When building on windows, the path boost-build puts targets in may be too
	long. If you get an error message like: "The input line is long", try to
	pass --abbreviate-paths on the bjam command line.

.. warning::

  If you link statically to the runtime library, you cannot build libtorrent
  as a shared library (DLL), since you will get separate heaps in the library
  and in the client application. It will result in crashes and possibly link
  errors.

.. note::

  With boost-build V2 (Milestone 11), the darwin toolset uses the ``-s`` linker
  option to strip debug symbols. This option is buggy in Apple's GCC, and
  will make the executable crash on startup. On Mac OS X, instead build
  your release executables with the ``debug-symbols=on`` option, and
  later strip your executable with ``strip``.

.. note::

  Some linux systems requires linking against ``librt`` in order to access
  the POSIX clock functions. If you get an error complaining about a missing
  symbol ``clock_gettime``, you have to give ``need-librt=yes`` on the
  bjam command line. This will make libtorrent link against ``librt``.

.. note::

  When building on Solaris, you might have to specify ``stdlib=sun-stlport``
  on the bjam command line.

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
``/cygdrive/c/boost_1_34_0``). In the windows environment, they should have the typical
windows format (``c:/boost_1_34_0``).

.. note::
	In Jamfiles, spaces are separators. It's typically easiest to avoid spaces
	in path names. If you want spaces in your paths, make sure to quote them
	with double quotes (").

The ``Jamfile`` will define ``NDEBUG`` when it's building a release build.
For more build configuration flags see `Build configurations`_.

Build features:

+--------------------------+----------------------------------------------------+
| boost build feature      | values                                             |
+==========================+====================================================+
| ``boost``                | * ``system`` - default. Tells the Jamfile that     |
|                          |   boost is installed and should be linked against  |
|                          |   the system libraries.                            |
|                          | * ``source`` - Specifies that boost is to be built |
|                          |   from source. The environment variable            |
|                          |   ``BOOST_ROOT`` must be defined to point to the   |
|                          |   boost directory.                                 |
+--------------------------+----------------------------------------------------+
| ``boost-link``           | * ``static`` - links statically against the boost  |
|                          |   libraries.                                       |
|                          | * ``shared`` - links dynamically against the boost |
|                          |   libraries.                                       |
+--------------------------+----------------------------------------------------+
| ``logging``              | * ``none`` - no logging.                           |
|                          | * ``default`` - basic session logging.             |
|                          | * ``verbose`` - verbose peer wire logging.         |
|                          | * ``errors`` - like verbose, but limited to errors.|
+--------------------------+----------------------------------------------------+
| ``dht``                  | * ``on`` - build with support for tracker less     |
|                          |   torrents and DHT support.                        |
|                          | * ``logging`` - build with DHT support and verbose |
|                          |   logging of the DHT protocol traffic.             |
|                          | * ``off`` - build without DHT support.             |
+--------------------------+----------------------------------------------------+
| ``need-librt``           | * ``no`` - this platform does not need to link     |
|                          |   against librt to have POSIX time functions.      |
|                          | * ``yes`` - specify this if your linux system      |
|                          |   requires you to link against librt.a. This is    |
|                          |   typically the case on x86 64 bit systems.        |
+--------------------------+----------------------------------------------------+
| ``asserts``              | * ``auto`` - asserts are on if in debug mode       |
|                          | * ``on`` - asserts are on, even in release mode    |
|                          | * ``off`` - asserts are disabled                   |
|                          | * ``production`` - assertion failures are logged   |
|                          |   to ``asserts.log`` in the current working        |
|                          |   directory, but won't abort the process.          |
|                          |   The file they are logged to can be customized    |
|                          |   by setting the global pointer ``extern char      |
|                          |   const* libtorrent_assert_log`` to a different    |
|                          |   filename.                                        |
|                          | * ``system`` use the libc assert macro             |
+--------------------------+----------------------------------------------------+
| ``geoip``                | * ``off`` - geo ip lookups disabled                |
|                          | * ``static`` - MaxMind_ geo ip lookup code linked  |
|                          |   in statically. Note that this code is under      |
|                          |   LGPL license.                                    |
|                          | * ``shared`` - The MaxMind_ geo ip lookup library  |
|                          |   is expected to be installed on the system and    |
|                          |   it will be used.                                 |
+--------------------------+----------------------------------------------------+
| ``upnp-logging``         | * ``off`` - default. Does not log UPnP traffic.    |
|                          | * ``on`` - creates "upnp.log" with the messages    |
|                          |   sent to and received from UPnP devices.          |
+--------------------------+----------------------------------------------------+
| ``encryption``           | * ``openssl`` - links against openssl and          |
|                          |   libcrypto to enable https and encrypted          |
|                          |   bittorrent connections.                          |
|                          | * ``gcrypt`` - links against libgcrypt to enable   |
|                          |   encrypted bittorrent connections.                |
|                          | * ``tommath`` - uses a shipped version of          |
|                          |   libtommath and a custom rc4 implementation       |
|                          |   (based on libtomcrypt). This is the default      |
|                          |   option.                                          |
|                          | * ``off`` - turns off support for encrypted        |
|                          |   connections. The shipped public domain SHA-1     |
|                          |   implementation is used.                          |
+--------------------------+----------------------------------------------------+
| ``allocator``            | * ``pool`` - default, uses pool allocators for     |
|                          |   send buffers.                                    |
|                          | * ``system`` - uses ``malloc()`` and ``free()``    |
|                          |   instead. Might be useful to debug buffer issues  |
|                          |   with tools like electric fence or libgmalloc.    |
|                          | * ``debug`` - instruments buffer usage to catch    |
|                          |   bugs in libtorrent.                              |
+--------------------------+----------------------------------------------------+
| ``link``                 | * ``static`` - builds libtorrent as a static       |
|                          |   library (.a / .lib)                              |
|                          | * ``shared`` - builds libtorrent as a shared       |
|                          |   library (.so / .dll).                            |
+--------------------------+----------------------------------------------------+
| ``runtime-link``         | * ``static`` - links statically against the        |
|                          |   run-time library (if available on your           |
|                          |   platform).                                       |
|                          | * ``shared`` - link dynamically against the        |
|                          |   run-time library (default).                      |
+--------------------------+----------------------------------------------------+
| ``variant``              | * ``debug`` - builds libtorrent with debug         |
|                          |   information and invariant checks.                |
|                          | * ``release`` - builds libtorrent in release mode  |
|                          |   without invariant checks and with optimization.  |
|                          | * ``profile`` - builds libtorrent with profile     |
|                          |   information.                                     |
+--------------------------+----------------------------------------------------+
| ``character-set``        | This setting will only have an affect on windows.  |
|                          | Other platforms are expected to support UTF-8.     |
|                          |                                                    |
|                          | * ``unicode`` - The unicode version of the win32   |
|                          |   API is used. This is default.                    |
|                          | * ``ansi`` - The ansi version of the win32 API is  |
|                          |   used.                                            |
+--------------------------+----------------------------------------------------+
| ``invariant-checks``     | This setting only affects debug builds (where      |
|                          | ``NDEBUG`` is not defined). It defaults to ``on``. |
|                          |                                                    |
|                          | * ``on`` - internal invariant checks are enabled.  |
|                          | * ``off`` - internal invariant checks are          |
|                          |   disabled. The resulting executable will run      |
|                          |   faster than a regular debug build.               |
|                          | * ``full`` - turns on extra expensive invariant    |
|                          |   checks.                                          |
+--------------------------+----------------------------------------------------+
| ``debug-symbols``        | * ``on`` - default for debug builds. This setting  |
|                          |   is useful for building release builds with       |
|                          |   symbols.                                         |
|                          | * ``off`` - default for release builds.            |
+--------------------------+----------------------------------------------------+
| ``deprecated-functions`` | * ``on`` - default. Includes deprecated functions  |
|                          |   of the API (might produce warnings during build  |
|                          |   when deprecated functions are used).             |
|                          | * ``off`` - excludes deprecated functions from the |
|                          |   API. Generates build errors when deprecated      |
|                          |   functions are used.                              |
+--------------------------+----------------------------------------------------+
| ``full-stats``           | * ``on`` - default, collects stats for IP overhead |
|                          |   and DHT and trackers. This uses a little bit     |
|                          |   extra memory for each peer and torrent.          |
|                          | * ``off`` - only collects the standard stats for   |
|                          |   upload and download rate.                        |
+--------------------------+----------------------------------------------------+
| ``iconv``                | * ``auto`` - use iconv for string conversions for  |
|                          |   linux and mingw and other posix platforms.       |
|                          | * ``on`` - force use of iconv                      |
|                          | * ``off`` - force not using iconv (disables locale |
|                          |   awareness except on windows).                    |
+--------------------------+----------------------------------------------------+
| ``i2p``                  | * ``on`` - build with I2P support                  |
|                          | * ``off`` - build without I2P support              |
+--------------------------+----------------------------------------------------+
| ``boost-date-time``      | * ``off`` - don't build asio types that depend     |
|                          |   on boost.date_time. libtorrent doesn't use them  |
|                          |   but if the client does, you need these to be     |
|                          |   built.                                           |
|                          | * ``on`` - build asio types that depend on         |
|                          |   boost.date_time.                                 |
+--------------------------+----------------------------------------------------+
| ``statistics``           | * ``off`` - default. No logging of additional      |
|                          |   stats.                                           |
|                          | * ``on`` - log session statistics in current       |
|                          |   working directory session_stats<pid>. The log    |
|                          |   is rotated every hour. It can be parsed by the   |
|                          |   parse_session_stats.py script (requires gnuplot) |
+--------------------------+----------------------------------------------------+

.. _MaxMind: http://www.maxmind.com/app/api

The ``variant`` feature is *implicit*, which means you don't need to specify
the name of the feature, just the value.

The logs created when building vlog or log mode are put in a directory called
``libtorrent_logs`` in the current working directory.

When building the example client on windows, you need to build with
``link=static`` otherwise you may get unresolved external symbols for some
boost.program-options symbols.

For more information, see the `Boost build v2 documentation`__, or more
specifically `the section on builtin features`__.

__ http://www.boost.org/tools/build/v2/index.html
__ http://www.boost.org/doc/html/bbv2/reference.html#bbv2.advanced.builtins.features


building with autotools
-----------------------

First of all, you need to install ``automake`` and ``autoconf``. Many
unix/linux systems comes with these preinstalled.

The prerequisites for building libtorrent are boost.thread, boost.date_time
and boost.filesystem. Those are the *compiled* boost libraries needed. The
headers-only libraries needed include (but is not necessarily limited to)
boost.bind, boost.ref, boost.multi_index, boost.optional, boost.lexical_cast,
boost.integer, boost.iterator, boost.tuple, boost.array, boost.function,
boost.smart_ptr, boost.preprocessor, boost.static_assert.

If you want to build the ``client_test`` example, you'll also need boost.regex
and boost.program_options.

Step 1: Generating the build system
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

No build system is present if libtorrent is checked out from CVS - it
needs to be generated first. If you're building from a released tarball,
you may skip directly to `Step 2: Running configure`_.

Execute the following command to generate the build system::

	./autotool.sh

Step 2: Running configure
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

It was observed on FreeBSD (release 6.0) that one needs to add '-lpthread' to
LDFLAGS, as Boost::Thread detection will fail without it, even if
Boost::Thread is installed.

If you need to set these variables, it may be a good idea to add those lines
to your ``~/.profile`` or ``~/.tcshrc`` depending on your shell.

If the boost libraries are named with a suffix on your platform, you may use
the ``--with-boost-thread=`` option to specify the suffix used for the thread
library in this case. For more information about these options, run::

	./configure --help

On gentoo the boost libraries that are built with multi-threading support have
the suffix ``mt``.

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

Step 3: Building libtorrent
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Once the configure script is run successfully, you just type ``make`` and
libtorrent, the examples and the tests will be built.

When libtorrent is built it may be a good idea to run the tests, you do this
by running ``make check``.

If you want to build a release version (without debug info, asserts and
invariant checks), you have to rerun the configure script and rebuild, like this::

  ./configure --disable-debug
  make clean
  make

building with other build systems
---------------------------------
  
If you're building in MS Visual Studio, you may have to set the compiler
options "force conformance in for loop scope", "treat wchar_t as built-in
type" and "Enable Run-Time Type Info" to Yes.

build configurations
--------------------

By default libtorrent is built In debug mode, and will have pretty expensive
invariant checks and asserts built into it. If you want to disable such checks
(you want to do that in a release build) you can see the table below for which
defines you can use to control the build.

+----------------------------------------+-------------------------------------------------+
| macro                                  | description                                     |
+========================================+=================================================+
| ``NDEBUG``                             | If you define this macro, all asserts,          |
|                                        | invariant checks and general debug code will be |
|                                        | removed. Since there is quite a lot of code in  |
|                                        | in header files in libtorrent, it may be        |
|                                        | important to define the symbol consistently     |
|                                        | across compilation units, including the clients |
|                                        | files. Potential problems is different          |
|                                        | compilation units having different views of     |
|                                        | structs and class layouts and sizes.            |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_LOGGING``                    | This macro will enable logging of the session   |
|                                        | events, such as tracker announces and incoming  |
|                                        | connections (as well as blocked connections).   |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_GEO_IP``             | This is defined by default by the Jamfile. It   |
|                                        | disables the GeoIP features, and avoids linking |
|                                        | against LGPL:ed code.                           |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_VERBOSE_LOGGING``            | If you define this macro, every peer connection |
|                                        | will log its traffic to a log file as well as   |
|                                        | the session log.                                |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_STORAGE_DEBUG``              | This will enable extra expensive invariant      |
|                                        | checks in the storage, including logging of     |
|                                        | piece sorting.                                  |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_UPNP_LOGGING``               | Generates a "upnp.log" file with the UPnP       |
|                                        | traffic. This is very useful when debugging     |
|                                        | support for various UPnP routers.               |
|                                        | support for various UPnP routers.               |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISK_STATS``                 | This will create a log of all disk activity     |
|                                        | which later can parsed and graphed using        |
|                                        | ``parse_disk_log.py``.                          |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_STATS``                      | This will generate a log with transfer rates,   |
|                                        | downloading torrents, seeding torrents, peers,  |
|                                        | connecting peers and disk buffers in use. The   |
|                                        | log can be parsed and graphed with              |
|                                        | ``parse_session_stats.py``.                     |
+----------------------------------------+-------------------------------------------------+
| ``UNICODE``                            | If building on windows this will make sure the  |
|                                        | UTF-8 strings in pathnames are converted into   |
|                                        | UTF-16 before they are passed to the file       |
|                                        | operations.                                     |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_POOL_ALLOCATOR``     | Disables use of ``boost::pool<>``.              |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_LINKING_SHARED``             | If this is defined when including the           |
|                                        | libtorrent headers, the classes and functions   |
|                                        | will be tagged with ``__declspec(dllimport)``   |
|                                        | on msvc and default visibility on GCC 4 and     |
|                                        | later. Set this in your project if you're       |
|                                        | linking against libtorrent as a shared library. |
|                                        | (This is set by the Jamfile when                |
|                                        | ``link=shared`` is set).                        |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_BUILDING_SHARED``            | If this is defined, the functions and classes   |
|                                        | in libtorrent are marked with                   |
|                                        | ``__declspec(dllexport)`` on msvc, or with      |
|                                        | default visibility on GCC 4 and later. This     |
|                                        | should be defined when building libtorrent as   |
|                                        | a shared library. (This is set by the Jamfile   |
|                                        | when ``link=shared`` is set).                   |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_DHT``                | If this is defined, the support for trackerless |
|                                        | torrents will be disabled.                      |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DHT_VERBOSE_LOGGING``        | This will enable verbose logging of the DHT     |
|                                        | protocol traffic.                               |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_ENCRYPTION``         | This will disable any encryption support and    |
|                                        | the dependencies of a crypto library.           |
|                                        | Encryption support is the peer connection       |
|                                        | encrypted supported by clients such as          |
|                                        | uTorrent, Azureus and KTorrent.                 |
|                                        | If this is not defined, either                  |
|                                        | ``TORRENT_USE_OPENSSL`` or                      |
|                                        | ``TORRENT_USE_GCRYPT`` must be defined.         |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_EXTENSIONS``         | When defined, libtorrent plugin support is      |
|                                        | disabled along with support for the extension   |
|                                        | handskake (BEP 10).                             |
+----------------------------------------+-------------------------------------------------+
| ``_UNICODE``                           | On windows, this will cause the file IO         |
|                                        | use wide character API, to properly support     |
|                                        | non-ansi characters.                            |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_RESOLVE_COUNTRIES``  | Defining this will disable the ability to       |
|                                        | resolve countries of origin for peer IPs.       |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_INVARIANT_CHECKS``   | This will disable internal invariant checks in  |
|                                        | libtorrent. The invariant checks can sometime   |
|                                        | be quite expensive, they typically don't scale  |
|                                        | very well. This option can be used to still     |
|                                        | build in debug mode, with asserts enabled, but  |
|                                        | make the resulting executable faster.           |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_EXPENSIVE_INVARIANT_CHECKS`` | This will enable extra expensive invariant      |
|                                        | checks. Useful for finding particular bugs      |
|                                        | or for running before releases.                 |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_NO_DEPRECATE``               | This will exclude all deprecated functions from |
|                                        | the header files and cpp files.                 |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_PRODUCTION_ASSERTS``         | Define to either 0 or 1. Enables assert logging |
|                                        | in release builds.                              |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_USE_ASSERTS``                | Define as 0 to disable asserts unconditionally. |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_USE_SYSTEM_ASSERTS``         | Uses the libc assert macro rather then the      |
|                                        | custom one.                                     |
+----------------------------------------+-------------------------------------------------+


If you experience that libtorrent uses unreasonable amounts of cpu, it will
definitely help to define ``NDEBUG``, since it will remove the invariant checks
within the library.

building openssl for windows
----------------------------

To build openssl for windows with Visual Studio 7.1 (2003) execute the following commands
in a command shell::

	perl Configure VC-WIN32 --prefix="c:/openssl
	call ms\do_nasm
	call "C:\Program Files\Microsoft Visual Studio .NET 2003\vc7\bin\vcvars32.bat"
	nmake -f ms\nt.mak
	copy inc32\openssl "C:\Program Files\Microsoft Visual Studio .NET 2003\vc7\include\"
	copy out32\libeay32.lib "C:\Program Files\Microsoft Visual Studio .NET 2003\vc7\lib"
	copy out32\ssleay32.lib "C:\Program Files\Microsoft Visual Studio .NET 2003\vc7\lib"

This will also install the headers and library files in the visual studio directories to
be picked up by libtorrent.

