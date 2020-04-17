=================
libtorrent manual
=================

.. include:: header.rst

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

downloading and building
========================

To download the latest version of libtorrent, clone the `github repository`__.

__ https://github.com/arvidn/libtorrent

The build systems supported "out of the box" in libtorrent are boost-build v2
(BBv2) and cmake. If you still can't build after following these instructions,
you can usually get help in the ``#libtorrent`` IRC channel on
``irc.freenode.net``.

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

building from git
-----------------

To build libtorrent from git you need to clone the libtorrent repository from
github. If you downloaded a release `tarball`__, you can skip this section.

__ https://github.com/arvidn/libtorrent/releases/latest

::

	git clone https://github.com/arvidn/libtorrent.git


building with BBv2
------------------

The primary reason to use boost-build is that it will automatically build the
dependent boost libraries with the correct compiler settings, in order to
ensure that the build targets are link compatible (see `boost guidelines`__
for some details on this issue).

__ https://boost.org/more/separate_compilation.html

Since BBv2 will build the boost libraries for you, you need the full boost
source package. Having boost installed via some package system is usually not
enough (and even if it is enough, the necessary environment variables are
usually not set by the package installer).

If you want to build against an installed copy of boost, you can skip directly
to step 3 (assuming you also have boost build installed).


Step 1: Download boost
~~~~~~~~~~~~~~~~~~~~~~

If you want to build against boost installed on your system, you can skip this
strep. Just make sure to have `BOOST_ROOT` unset for the `b2` invocation.

You'll find boost here__.

__ https://www.boost.org/users/download/#live

Extract the archive to some directory where you want it. For the sake of this
guide, let's assume you extract the package to ``c:\boost_1_69_0``. You'll
need at least version 1.58 of the boost library in order to build libtorrent.


Step 2: Setup BBv2
~~~~~~~~~~~~~~~~~~

If you have installed ``boost-build`` via a package manager, you can skip this
step. If not, you need to build boost build from the boost source package.

First you need to build ``b2``. You do this by opening a terminal (In windows,
run ``cmd``). Change directory to ``c:\boost_1_68_0\tools\build``. Then run the
script called ``bootstrap.bat`` or ``bootstrap.sh`` on a Unix system. This will
build ``b2`` and place it in a directory ``src/engine/bin.<architecture>``.
Copy the ``b2.exe`` (or ``b2`` on a Unix system) to a place that's in you
shell's ``PATH``. On Linux systems a place commonly used may be
``/usr/local/bin`` or on Windows ``c:\windows`` (you can also add directories to
the search paths by modifying the environment variable called ``PATH``).

Now you have ``b2`` installed. ``b2`` can be considered an interpreter
that the boost-build system is implemented on. So boost-build uses ``b2``.
So, to complete the installation you need to make two more things. You need to
set the environment variable ``BOOST_BUILD_PATH``. This is the path that tells
``b2`` where it can find boost-build, your configuration file and all the
toolsets (descriptions used by boost-build to know how to use different
compilers on different platforms). Assuming the boost install path above, set
it to ``c:\boost_1_68_0\tools\build``.

To set an environment variable in windows, type for example::

  set BOOST_BUILD_PATH=c:\boost_1_68_0\tools\build\v2

In a terminal window.

The last thing to do is to configure which compiler(s) to use. Create a file
``user-config.jam`` in your home directory. Depending on your platform and which
compiler you're using, you should add a line for each compiler and compiler
version you have installed on your system that you want to be able to use with
BBv2. For example, if you're using Microsoft Visual Studio 12 (2013), just add a
line::

  using msvc : 14.0 ;

If you use GCC, add the line::

  using gcc ;

If you have more than one version of GCC installed, you can add the
command line used to invoke g++ after the version number, like this::

  using gcc : 6.0 : g++-6 ;
  using gcc : 7.0 : g++-7 ;

Another toolset worth mentioning is the ``darwin`` toolset (for macOS).
From Tiger (10.4) macOS comes with both GCC 3.3 and GCC 4.0. Then you can
use the following toolsets::

  using darwin : 3.3 : g++-3.3 ;
  using darwin : 4.0 : g++-4.0 ;

Note that the spaces around the semi-colons and colons are important!

Also see the `official installation instructions`_.

.. _`official installation instructions`: https://www.boost.org/doc/html/bbv2/installation.html


Step 3: Building libtorrent
~~~~~~~~~~~~~~~~~~~~~~~~~~~

When building libtorrent, boost is either picked up from system installed
locations or from a boost source package, if the ``BOOST_ROOT`` environment
variable is set pointing to one. If you're building boost from source, set
``BOOST_ROOT`` to your boost directory, e.g. ``c:\boost_1_68_0``.

Then the only thing left is simply to invoke ``b2``. If you want to specify
a specific toolset to use (compiler) you can just add that to the command line.
For example::

  b2 msvc-14.0
  b2 gcc-7.0
  b2 darwin-4.0

.. note::

	If the environment variable ``BOOST_ROOT`` is not set, the Jamfile will
	attempt to link against "installed" boost libraries. i.e. assume the headers
	and libraries are available in default search paths.
	In this case it's critical that you build your project with the same version
	of C++ and the same build flags as the system libraries were built with.

To build different versions you can also just add the name of the build
variant. Some default build variants in BBv2 are ``release``, ``debug``,
``profile``.

You can build libtorrent as a DLL too, by typing ``link=shared``, or
``link=static`` to build a static library.

If you want to explicitly say how to link against the runtime library, you
can set the ``runtime-link`` feature on the command line, either to ``shared``
or ``static``. Most operating systems will only allow linking shared against
the runtime, but on windows you can do both. Example::

  b2 msvc-14.0 link=static runtime-link=static

.. note::

	When building on windows, the path boost-build puts targets in may be too
	long. If you get an error message like: "The input line is long", try to
	pass --hash on the ``b2`` command line.

.. warning::

  If you link statically to the runtime library, you cannot build libtorrent
  as a shared library (DLL), since you will get separate heaps in the library
  and in the client application. It will result in crashes and possibly link
  errors.

.. note::

  Some Linux systems requires linking against ``librt`` in order to access
  the POSIX clock functions. If you get an error complaining about a missing
  symbol ``clock_gettime``, you have to give ``need-librt=yes`` on the
  b2 command line. This will make libtorrent link against ``librt``.

.. note::

  When building on Solaris, you may have to specify ``stdlib=sun-stlport``
  on the b2 command line.

The build targets are put in a directory called bin, and under it they are
sorted in directories depending on the toolset and build variant used.

To build the examples, just change directory to the examples directory and
invoke ``b2`` from there. To build and run the tests, go to the test
directory and run ``b2``.

Note that if you're building on windows using the ``msvc`` toolset, you cannot run it
from a cygwin terminal, you'll have to run it from a ``cmd`` terminal. The same goes for
cygwin, if you're building with gcc in cygwin you'll have to run it from a cygwin terminal.
Also, make sure the paths are correct in the different environments. In cygwin, the paths
(``BOOST_BUILD_PATH`` and ``BOOST_ROOT``) should be in the typical Unix-format (e.g.
``/cygdrive/c/boost_1_68_0``). In the windows environment, they should have the typical
windows format (``c:/boost_1_68_0``).

.. note::
	In Jamfiles, spaces are separators. It's typically easiest to avoid spaces
	in path names. If you want spaces in your paths, make sure to quote them
	with double quotes (").

The ``Jamfile`` will define ``NDEBUG`` when it's building a release build.
For more build configuration flags see `Build configurations`_.

When enabling linking against openssl (by setting the ``crypto`` feature to
``openssl``) the Jamfile will look in some default directory for the openssl
headers and libraries. On macOS, it will look for the homebrew openssl package.
On Windows, it will look in ``C:\OpenSSL-Win32``, or ``C:\OpenSSL-Win64`` if
compiling in 64-bit.

To customize the library path and include path for openssl, set the features
``openssl-lib`` and ``openssl-include`` respectively.

Build features:

+--------------------------+----------------------------------------------------+
| boost build feature      | values                                             |
+==========================+====================================================+
| ``boost-link``           | * ``static`` - links statically against the boost  |
|                          |   libraries.                                       |
|                          | * ``shared`` - links dynamically against the boost |
|                          |   libraries.                                       |
+--------------------------+----------------------------------------------------+
| ``openssl-lib``          | can be used to specify the directory where libssl  |
|                          | and libcrypto are installed (or the windows        |
|                          | counterparts).                                     |
+--------------------------+----------------------------------------------------+
| ``openssl-include``      | can be used to specify the include directory where |
|                          | the openssl headers are installed.                 |
+--------------------------+----------------------------------------------------+
| ``logging``              | * ``off`` - logging alerts disabled. The           |
|                          |   reason to disable logging is to keep the binary  |
|                          |   size low where that matters.                     |
|                          | * ``on`` - default. logging alerts available,      |
|                          |   still need to be enabled by the alert mask.      |
+--------------------------+----------------------------------------------------+
| ``dht``                  | * ``on`` - build with DHT support                  |
|                          | * ``off`` - build without DHT support.             |
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
| ``encryption``           | * ``on`` - encrypted bittorrent connections        |
|                          |   enabled. (Message Stream encryption).(default)   |
|                          | * ``off`` - turns off support for encrypted        |
|                          |   connections. The shipped public domain SHA-1     |
|                          |   implementation is used.                          |
+--------------------------+----------------------------------------------------+
| ``mutable-torrents``     | * ``on`` - mutable torrents are supported          |
|                          |   (`BEP 38`_) (default).                           |
|                          | * ``off`` - mutable torrents are not supported.    |
+--------------------------+----------------------------------------------------+
| ``crypto``               | * ``built-in`` - (default) uses built-in SHA-1     |
|                          |   implementation. In macOS/iOS it uses             |
|                          |   CommonCrypto SHA-1 implementation.               |
|                          | * ``openssl`` - links against openssl and          |
|                          |   libcrypto to use for SHA-1 hashing.              |
|                          |   This also enables HTTPS-tracker support and      |
|                          |   support for bittorrent over SSL.                 |
|                          | * ``libcrypto`` - links against libcrypto          |
|                          |   to use the SHA-1 implementation. (no SSL support)|
|                          | * ``gcrypt`` - links against libgcrypt             |
|                          |   to use the SHA-1 implementation. (no SSL support)|
+--------------------------+----------------------------------------------------+
| ``openssl-version``      | This can be used on windows to link against the    |
|                          | special OpenSSL library names used on windows      |
|                          | prior to OpenSSL 1.1.                              |
|                          |                                                    |
|                          | * ``1.1`` - link against the normal openssl        |
|                          |   library name. (default)                          |
|                          | * ``pre1.1`` - link against the old windows names  |
|                          |   (i.e. ``ssleay32`` and ``libeay32``.             |
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
| ``iconv``                | * ``auto`` - use iconv for string conversions for  |
|                          |   Linux and MinGW and other posix platforms.       |
|                          | * ``on`` - force use of iconv                      |
|                          | * ``off`` - force not using iconv (disables locale |
|                          |   awareness except on windows).                    |
+--------------------------+----------------------------------------------------+
| ``i2p``                  | * ``on`` - default. build with I2P support         |
|                          | * ``off`` - build without I2P support              |
+--------------------------+----------------------------------------------------+
| ``profile-calls``        | * ``off`` - default. No additional call profiling. |
|                          | * ``on`` - Enable logging of stack traces of       |
|                          |   calls into libtorrent that are blocking. On      |
|                          |   session shutdown, a file ``blocking_calls.txt``  |
|                          |   is written with stack traces of blocking calls   |
|                          |   ordered by the number of them.                   |
+--------------------------+----------------------------------------------------+
| ``utp-log``              | * ``off`` - default. Do not print verbose uTP      |
|                          |   log.                                             |
|                          | * ``on`` - Print verbose uTP log, used to debug    |
|                          |   the uTP implementation.                          |
+--------------------------+----------------------------------------------------+
| ``picker-debugging``     | * ``off`` - default. no extra invariant checks in  |
|                          |   piece picker.                                    |
|                          | * ``on`` - include additional invariant checks in  |
|                          |   piece picker. Used for testing the piece picker. |
+--------------------------+----------------------------------------------------+
| ``extensions``           | * ``on`` - enable extensions to the bittorrent     |
|                          |   protocol.(default)                               |
|                          | * ``off`` - disable bittorrent extensions.         |
+--------------------------+----------------------------------------------------+
| ``streaming``            | * ``on`` - enable streaming functionality. i.e.    |
|                          |   ``set_piece_deadline()``. (default)              |
|                          | * ``off`` - disable streaming functionality.       |
+--------------------------+----------------------------------------------------+
| ``super-seeding``        | * ``on`` - enable super seeding feature. (default) |
|                          | * ``off`` - disable super seeding feature          |
+--------------------------+----------------------------------------------------+
| ``share-mode``           | * ``on`` - enable share-mode feature. (default)    |
|                          | * ``off`` - disable share-mode feature             |
+--------------------------+----------------------------------------------------+
| ``predictive-pieces``    | * ``on`` - enable predictive piece announce        |
|                          |   feature. i.e.                                    |
|                          |   settings_pack::predictive_piece_announce         |
|                          |   (default)                                        |
|                          | * ``off`` - disable feature.                       |
+--------------------------+----------------------------------------------------+
| ``fpic``                 | * ``off`` - default. Build without specifying      |
|                          |   ``-fPIC``.                                       |
|                          | * ``on`` - Force build with ``-fPIC`` (useful for  |
|                          |   building a static library to be linked into a    |
|                          |   shared library).                                 |
+--------------------------+----------------------------------------------------+

The ``variant`` feature is *implicit*, which means you don't need to specify
the name of the feature, just the value.

When building the example client on windows, you need to build with
``link=static`` otherwise you may get unresolved external symbols for some
boost.program-options symbols.

For more information, see the `Boost build v2 documentation`__, or more
specifically `the section on built-in features`__.

__ https://boostorg.github.io/build/manual/develop/index.html
__ https://boostorg.github.io/build/manual/develop/index.html#bbv2.overview.builtins.features


Step 4: Installing libtorrent
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To install libtorrent run ``b2`` with the ``install`` target::

	b2 install --prefix=/usr/local

Change the value of the ``--prefix`` argument to install it in a different location.

building with cmake
-------------------

First of all, you need to install ``cmake``. Additionally you need a build
system to actually schedule builds, for example ``ninja``.

Step 1: Generating the build system
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Create a build directory for out-of-source build inside the libtorrent root directory::

	mkdir build

and ``cd`` there::

	cd build

Run ``cmake`` in the build directory, like this::

	cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=11 -G Ninja ..

The ``CMAKE_CXX_STANDARD`` has to be at least 11, but you may want to raise it
to ``14`` or ``17`` if your project use a newer version of the C++ standard.

.. warning::

	The detection of boost sometimes fail in subtle ways. If you have the
	``BOOST_ROOT`` environment variable set, it may find the pre-built system
	libraries, but use the header files from your source package. To avoid this,
	invoke ``cmake`` with ``BOOST_ROOT`` set to an empty string:
	``BOOST_ROOT="" cmake ...``.

Other build options are:

+-----------------------+---------------------------------------------------+
| ``BUILD_SHARED_LIBS`` | Defaults ``ON``. Builds libtorrent as a shared    |
|                       | library.                                          |
+-----------------------+---------------------------------------------------+
| ``static_runtime``    | Defaults ``OFF``. Link libtorrent statically      |
|                       | against the runtime libraries.                    |
+-----------------------+---------------------------------------------------+
| ``build_tests``       | Defaults ``OFF``. Also build the libtorrent       |
|                       | tests.                                            |
+-----------------------+---------------------------------------------------+
| ``build_examples``    | Defaults ``OFF``. Also build the examples in the  |
|                       | examples directory.                               |
+-----------------------+---------------------------------------------------+
| ``build_tools``       | Defaults ``OFF``. Also build the tools in the     |
|                       | tools directory.                                  |
+-----------------------+---------------------------------------------------+
| ``python-bindings``   | Defaults ``OFF``. Also build the python bindings  |
|                       | in bindings/python directory.                     |
+-----------------------+---------------------------------------------------+
| ``encryption``        | Defaults ``ON``. Support trackers and bittorrent  |
|                       | over TLS, and obfuscated bittorrent connections.  |
+-----------------------+---------------------------------------------------+

Options are set on the ``cmake`` command line with the ``-D`` option or later on using ``ccmake`` or ``cmake-gui`` applications. ``cmake`` run outputs a summary of all available options and their current values.

.. note::

	If you are linking statically against OpenSSL on Windows and not using ``-Dstatic_runtime=ON``,
	you should additionally use the option ``-DOPENSSL_USE_STATIC_LIBS=ON``.
	If you use ``-Dstatic_runtime=ON``, ``-DOPENSSL_USE_STATIC_LIBS=ON`` is implied.

Step 2: Building libtorrent
~~~~~~~~~~~~~~~~~~~~~~~~~~~

In the terminal, run::

	ninja -j8

in the build directory the number after ``-j`` specifies the number of parallel jobs to build in; you may omit this option to let ``ninja`` use all your cores).

If you enabled test in the configuration step, to run them, run::

	ctest -j8

building with VCPKG
-------------------

You can download and install libtorrent using the [vcpkg](https://github.com/Microsoft/vcpkg/) dependency manager::

	git clone https://github.com/Microsoft/vcpkg.git
	cd vcpkg
	./bootstrap-vcpkg.sh
	./vcpkg integrate install
	./vcpkg install libtorrent

The libtorrent port in vcpkg is kept up to date by Microsoft team members and community contributors.
If the version is out of date, please [create an issue or pull request](https://github.com/Microsoft/vcpkg) on the vcpkg repository.

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
| ``TORRENT_DISABLE_LOGGING``            | This macro will disable support for logging     |
|                                        | alerts, like log_alert, torrent_log_alert and   |
|                                        | peer_log_alert. With this build flag, you       |
|                                        | cannot enable those alerts.                     |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_SUPERSEEDING``       | This macro will disable support for super       |
|                                        | seeding. The settings will exist, but will not  |
|                                        | have an effect, when this macro is defined.     |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_SHARE_MODE``         | This macro will disable support for share-mode. |
|                                        | i.e. the mode to maximize upload/download       |
|                                        | ratio for a torrent.                            |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_MUTABLE_TORRENTS``   | Disables mutable torrent support (`BEP 38`_)    |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_STREAMING``          | Disables set_piece_deadline() and associated    |
|                                        | functionality.                                  |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_PREDICTIVE_PIECES``  | Disables                                        |
|                                        | settings_pack::predictive_piece_announce        |
|                                        | feature.                                        |
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
| ``TORRENT_DISABLE_ENCRYPTION``         | This will disable any encryption support and    |
|                                        | the dependencies of a crypto library.           |
|                                        | Encryption support is the peer connection       |
|                                        | encrypted supported by clients such as          |
|                                        | uTorrent, Azureus and KTorrent.                 |
|                                        | If this is not defined, either                  |
|                                        | ``TORRENT_USE_LIBCRYPTO`` or                    |
|                                        | ``TORRENT_USE_LIBGCRYPT`` must be defined.      |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_DISABLE_EXTENSIONS``         | When defined, libtorrent plugin support is      |
|                                        | disabled along with support for the extension   |
|                                        | handshake (BEP 10).                             |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_USE_INVARIANT_CHECKS``       | If defined to non-zero, this will enable        |
|                                        | internal invariant checks in libtorrent.        |
|                                        | The invariant checks can sometimes              |
|                                        | be quite expensive, they typically don't scale  |
|                                        | very well.                                      |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_EXPENSIVE_INVARIANT_CHECKS`` | This will enable extra expensive invariant      |
|                                        | checks. Useful for finding particular bugs      |
|                                        | or for running before releases.                 |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_NO_DEPRECATE``               | This will exclude all deprecated functions from |
|                                        | the header files and source files.              |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_PRODUCTION_ASSERTS``         | Define to either 0 or 1. Enables assert logging |
|                                        | in release builds.                              |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_USE_ASSERTS``                | Define as 0 to disable asserts unconditionally. |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_USE_SYSTEM_ASSERTS``         | Uses the libc assert macro rather then the      |
|                                        | custom one.                                     |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_USE_OPENSSL``                | Link against ``libssl`` for SSL support. Must   |
|                                        | be combined with ``TORRENT_USE_LIBCRYPTO``      |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_USE_LIBCRYPTO``              | Link against ``libcrypto`` for SHA-1 support    |
|                                        | and other hashing algorithms.                   |
+----------------------------------------+-------------------------------------------------+
| ``TORRENT_USE_LIBGCRYPT``              | Link against ``libgcrypt`` for SHA-1 support    |
|                                        | and other hashing algorithms.                   |
+----------------------------------------+-------------------------------------------------+

.. _`BEP 38`: https://www.bittorrent.org/beps/bep_0038.html

If you experience that libtorrent uses unreasonable amounts of CPU, it will
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
