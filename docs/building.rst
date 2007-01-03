=================
libtorrent manual
=================

:Author: Arvid Norberg, arvid@rasterbar.com

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

downloading and building
========================

To acquire the latest version of libtorrent, you'll have to grab it from CVS.
You'll find instructions on how to do this here__ (see Anonymous CVS access).

__ http://sourceforge.net/cvs/?group_id=79942

The build systems supported "out of the box" in libtorrent are boost-build v2
(BBv2) and autotools (for unix-like systems). If you still can't build after
following these instructions, you can usually get help in the ``#libtorrent``
IRC channel on ``irc.freenode.net``.

Contributed build tutorials
---------------------------

* libtorrent cvs, ubuntu_
* `ubuntu 6.10`_
* suse_
* `visual studio 2005`_

These tutorials may work on similar linux distros as well.

.. _ubuntu: ubuntu_build_notes.html
.. _`ubuntu 6.10`: ubuntu_6_build_notes.html
.. _suse: suse_build_notes.html
.. _`visual studio 2005`: vs2005_build_notes.html

building from cvs
-----------------

To build libtorrent from cvs you need to check out the libtorrent sources from
sourceforge and also check out the asio sources from its sourceforge cvs.
If you downloaded a release tarball, you can skip this section.

To prepare the directory structure for building, follow these steps:

* Check out libtorrent (instructions__).
* Check out asio (instructions__).
* Copy the ``asio/include/asio/`` directory into the ``libtorrent/include/libtorrent/``
  directory. Alternatively you can make a symbolic link.
* Copy ``asio/include/asio.hpp`` into ``libtorrent/include/libtorrent``.

__ http://sourceforge.net/cvs/?group_id=79942
__ http://sourceforge.net/cvs/?group_id=122478

Now the libtorrent directory is ready for building. Follow the steps in one
of the following sections depending on which build system you prefer to use.

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

__ http://sourceforge.net/project/showfiles.php?group_id=7586&package_id=8041&release_id=376197

Extract the archive to some directory where you want it. For the sake of this
guide, let's assume you extract the package to ``c:\boost_1_33_1`` (I'm using
a windows path in this example since if you're on linux/unix you're more likely
to use the autotools). You'll need at least version 1.32 of the boost library
in order to build libtorrent.

If you use 1.32, you need to download BBv2 separately, so for now, let's
assume you will use version 1.33.1.


Step 2: Setup BBv2
~~~~~~~~~~~~~~~~~~

First you need to build ``bjam``. You do this by opening a terminal (In
windows, run ``cmd``). Change directory to
``c:\boost_1_33_1\tools\build\jam_src``. Then run the script called
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
it to ``c:\boost_1_33_1\tools\build\v2``.

To set an environment variable in windows, type for example::

  set BOOST_BUILD_PATH=c:\boost_1_33_1\tools\build\v2

In a terminal window.

The last thing to do to complete the setup of BBv2 is to modify your
``user-config.jam`` file. It is located in ``c:\boost_1_33_1\tools\build\v2``.
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
files found. So, set this to ``c:\boost_1_33_1``.

Then the only thing left is simply to invoke ``bjam``. If you want to specify
a specific toolset to use (compiler) you can just add that to the commandline.
For example::

  bjam msvc-7.1 link=static
  bjam gcc-3.3 link=static
  bjam darwin-4.0 link=static

To build different versions you can also just add the name of the build
variant. Some default build variants in BBv2 are ``release``, ``debug``,
``profile``.

You can build libtorrent as a dll too, by typing ``link=shared``, or
``link=static`` to build a static library. ``link=shared`` is the default.

If you want to explicitly say how to link against the runtime library, you
can set the ``runtime-link`` feature on the commandline, either to ``shared``
or ``static``. Most operating systems will only allow linking shared against
the runtime, but on windows you can do both. Example::

  bjam msvc-7.1 link=static runtime-link=static

.. warning::

  If you link statically to the runtime library, you cannot build libtorrent
  as a shared library (DLL), since you will get separate heaps in the library
  and in the client application. It will result in crashes and possibly link
  errors.


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
``/cygdrive/c/boost_1_33_1``). In the windows environment, they should have the typical
windows format (``c:/boost_1_33_1``).

The ``Jamfile`` will define ``NDEBUG`` when it's building a release build.
For more build configuration flags see `Build configurations`_.

Build features:

+------------------------+----------------------------------------------------+
| boost build feature    | values                                             |
+========================+====================================================+
| ``logging``            | * ``none`` - no logging.                           |
|                        | * ``default`` - basic session logging.             |
|                        | * ``verbose`` - verbose peer wire logging.         |
+------------------------+----------------------------------------------------+
| ``dht-support``        | * ``on`` - build with support for tracker less     |
|                        |   torrents and DHT support.                        |
|                        | * ``logging`` - build with DHT support and verbose |
|                        |   logging of the DHT protocol traffic.             |
|                        | * ``off`` - build without DHT support.             |
+------------------------+----------------------------------------------------+
| ``link``               | * ``static`` - builds libtorrent as a static       |
|                        |   library (.a / .lib)                              |
|                        | * ``shared`` - builds libtorrent as a shared       |
|                        |   library (.so / .dll).                            |
+------------------------+----------------------------------------------------+
| ``runtime-link``       | * ``static`` - links statically against the        |
|                        |   run-time library (if available on your           |
|                        |   platform).                                       |
|                        | * ``shared`` - link dynamically against the        |
|                        |   run-time library (default).                      |
+------------------------+----------------------------------------------------+
| ``variant``            | * ``debug`` - builds libtorrent with debug         |
|                        |   information and invariant checks.                |
|                        | * ``release`` - builds libtorrent in release mode  |
|                        |   without invariant checks and with optimization.  |
|                        | * ``profile`` - builds libtorrent with profile     |
|                        |   information.                                     |
+------------------------+----------------------------------------------------+

The ``variant`` feature is *implicit*, which means you don't need to specify
the name of the feature, just the value.

The logs created when building vlog or log mode are put in a directory called
``libtorrent_logs`` in the current working directory.

When building the example client on windows, you need to build with
``link=static`` otherwise you may get unresolved external symbols for some
boost.program-options symbols.

For more information, see the `Boost build v2 documentation`__.

__ http://www.boost.org/tools/build/v2/index.html

To build all possible variants of libtorrent (good for testing when making
sure all build variants will actually compile), you can invoke this command::

	bjam debug release link=shared link=static logging=verbose logging=default \
	logging=none dht-support=on dht-support=logging dht-support=off

building with autotools
-----------------------

First of all, you need to install ``automake`` and ``autoconf``. Many
unix/linux systems comes with these preinstalled.

The prerequisites for building libtorrent is boost.thread, boost.date_time
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

Execute the following commands, in the given order, to generate
the build system::

	aclocal -I m4
	autoheader
	libtoolize --copy --force
	automake --add-missing --copy --gnu
	autoconf

On darwin/OSX you have to run ``glibtoolize`` instead of ``libtoolize``.

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
type" and "Enable Run-Time Type Info" to Yes. For a detailed description
on how to build libtorrent with VS 2005, see `this document`_.

.. _`this document`: vs2005_build_notes.html

build configurations
--------------------

By default libtorrent is built In debug mode, and will have pretty expensive
invariant checks and asserts built into it. If you want to disable such checks
(you want to do that in a release build) you can see the table below for which
defines you can use to control the build.

+---------------------------------+-------------------------------------------------+
| macro                           | description                                     |
+=================================+=================================================+
| ``NDEBUG``                      | If you define this macro, all asserts,          |
|                                 | invariant checks and general debug code will be |
|                                 | removed. This option takes precedence over      |
|                                 | other debug settings.                           |
+---------------------------------+-------------------------------------------------+
| ``TORRENT_LOGGING``             | This macro will enable logging of the session   |
|                                 | events, such as tracker announces and incoming  |
|                                 | connections (as well as blocked connections).   |
+---------------------------------+-------------------------------------------------+
| ``TORRENT_VERBOSE_LOGGING``     | If you define this macro, every peer connection |
|                                 | will log its traffic to a log file as well as   |
|                                 | the session log.                                |
+---------------------------------+-------------------------------------------------+
| ``TORRENT_STORAGE_DEBUG``       | This will enable extra expensive invariant      |
|                                 | checks in the storage, including logging of     |
|                                 | piece sorting.                                  |
+---------------------------------+-------------------------------------------------+
| ``UNICODE``                     | If building on windows this will make sure the  |
|                                 | UTF-8 strings in pathnames are converted into   |
|                                 | UTF-16 before they are passed to the file       |
|                                 | operations.                                     |
+---------------------------------+-------------------------------------------------+
| ``LITTLE_ENDIAN``               | This will use the little endian version of the  |
|                                 | sha-1 code. If defined on a big-endian system   |
|                                 | the sha-1 hashes will be incorrect and fail.    |
|                                 | If it is not defined and ``__BIG_ENDIAN__``     |
|                                 | isn't defined either (it is defined by Apple's  |
|                                 | GCC) both little-endian and big-endian versions |
|                                 | will be built and the correct code will be      |
|                                 | chosen at run-time.                             |
+---------------------------------+-------------------------------------------------+
| ``TORRENT_LINKING_SHARED``      | If this is defined when including the           |
|                                 | libtorrent headers, the classes and functions   |
|                                 | will be tagged with ``__declspec(dllimport)``   |
|                                 | on msvc and default visibility on GCC 4 and     |
|                                 | later. Set this in your project if you're       |
|                                 | linking against libtorrent as a shared library. |
|                                 | (This is set by the Jamfile when                |
|                                 | ``link=shared`` is set).                        |
+---------------------------------+-------------------------------------------------+
| ``TORRENT_BUILDING_SHARED``     | If this is defined, the functions and classes   |
|                                 | in libtorrent are marked with                   |
|                                 | ``__declspec(dllexport)`` on msvc, or with      |
|                                 | default visibility on GCC 4 and later. This     |
|                                 | should be defined when building libtorrent as   |
|                                 | a shared library. (This is set by the Jamfile   |
|                                 | when ``link=shared`` is set).                   |
+---------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_DHT``         | If this is defined, the support for trackerless |
|                                 | torrents will be disabled.                      |
+---------------------------------+-------------------------------------------------+
| ``TORRENT_DHT_VERBOSE_LOGGING`` | This will enable verbose logging of the DHT     |
|                                 | protocol traffic.                               |
+---------------------------------+-------------------------------------------------+


If you experience that libtorrent uses unreasonable amounts of cpu, it will
definitely help to define ``NDEBUG``, since it will remove the invariant checks
within the library.


