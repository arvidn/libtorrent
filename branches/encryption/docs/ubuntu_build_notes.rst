==================================
Building libtorrent on Ubuntu 6.06
==================================

:Date: Nov 6, 2006
:Authors: Xi Stan, Francois Dermu
:Contact: stan8688@gmail.com


Prerequisites
=============

To build libtorrent, you need the following libraries:

* http://www.rasterbar.com/products/libtorrent/index.html
* http://www.boost.org
* http://asio.sourceforge.net/

Step 1: Acquire the source code from cvs
========================================

Create a directory for the project::

   mkdir ${HOME}/work
   cd ${HOME}/work

Check out ``boost``, ``libtorrent``, ``asio`` source code from cvs
by executing the following commands:

*No password needed (just hit enter when prompted)*

::

   cvs -d:pserver:anonymous@boost.cvs.sourceforge.net:/cvsroot/boost login
   cvs -z3 -d:pserver:anonymous@boost.cvs.sourceforge.net:/cvsroot/boost checkout boost
   cvs -d:pserver:anonymous@boost.cvs.sourceforge.net:/cvsroot/boost logout

   cvs -d:pserver:anonymous@libtorrent.cvs.sourceforge.net:/cvsroot/libtorrent login
   cvs -z3 -d:pserver:anonymous@libtorrent.cvs.sourceforge.net:/cvsroot/libtorrent co -P libtorrent
   cvs -d:pserver:anonymous@libtorrent.cvs.sourceforge.net:/cvsroot/libtorrent logout

   cvs -d:pserver:anonymous@asio.cvs.sourceforge.net:/cvsroot/asio login
   cvs -z3 -d:pserver:anonymous@asio.cvs.sourceforge.net:/cvsroot/asio co -P asio
   cvs -d:pserver:anonymous@asio.cvs.sourceforge.net:/cvsroot/asio login

Step 2: Building boost
======================

To build boost, first build boost-build and then use that to build
the libraries themselves:

.. parsed-literal::

	BASE_DIR=${HOME} *### Feel free to change this one.*
	BOOST_ROOT=${BASE_DIR}/boost
	BOOST_BUILD_PATH=${BOOST_ROOT}/tools/build/v2
	cd ${BOOST_ROOT}/tools/jam/src
	./build.sh
	sudo cp ./bin.linuxx86/bjam /usr/bin
	cd $BOOST_ROOT
	sudo bjam -sTOOLS=gcc install

*It takes about 45 min. (so if you want to grap a coke, now is the time)*


If you're successful you will see the following files in ``/usr/local/lib``::

   libboost_date_time-gcc-d-1_31.so
   libboost_date_time-gcc-mt-d-1_31.so
   libboost_date_time-gcc-1_31.so
   libboost_date_time-gcc-mt-1_31.so
   libboost_date_time-gcc-d-1_31.a
   libboost_date_time-gcc-mt-d-1_31.a
   libboost_date_time-gcc-1_31.a
   libboost_date_time-gcc-mt-1_31.a

Step 3: Copy asio into the libtorrent directory
===============================================

Skip this step if you're using a released tarball.

Execute the following command::

   cp -R ${BASE_DIR}/asio/include/asio* ${BASE_DIR}/libtorrent/include/libtorrent

Step 4: Building libtorrent
===========================

building with autotools
-----------------------

First of all, you need to install automake and autoconf. Many unix/linux systems
comes with these preinstalled. The prerequisites for building libtorrent are
boost.thread, boost.date_time and boost.filesystem. Those are the *compiled* boost
libraries needed. The headers-only libraries needed include (but is not necessarily
limited to) boost.bind, boost.ref, boost.multi_index, boost.optional,
boost.lexical_cast, boost.integer, boost.iterator, boost.tuple, boost.array,
boost.function, boost.smart_ptr, boost.preprocessor, boost.static_assert.

If you want to build the client_test example, you'll also need boost.regex and boost.program_options.

generating the build system
---------------------------

No build system is present if libtorrent is checked out from CVS - it needs to be
generated first. If you're building from a released tarball, you may skip directly
to `running configure`_.

Execute the following commands, in the given order, to generate the build system::

	cd ${BASE_DIR}/libtorrent
	CXXFLAGS="-I/usr/local/include/boost-1_35 -I${BASE_DIR}/libtorrent/include/libtorrent"
	LDFLAGS=-L/usr/local/lib

	aclocal -I m4
	autoheader
	libtoolize --copy --force
	automake --add-missing --copy --gnu
	autoconf

On darwin/OSX you have to run glibtoolize instead of libtoolize.

running configure
-----------------

To use the auto tools to build libtorrent, execute the following commands::

	cd ${BASE_DIR}/libtorrent
	CXXFLAGS="-I/usr/local/include/boost-1_35 -I${BASE_DIR}/libtorrent/include/libtorrent"
	LDFLAGS=-L/usr/local/lib

	./configure --with-boost-date-time=boost_date_time-gcc \
	--with-boost-filesystem=boost_filesystem-gcc \
	--with-boost-thread=boost_thread-gcc-mt

	make
	sudo make install

If successful, you will see the following files::

   /usr/local/lib/libtorrent.a
   /usr/local/lib/libtorrent.so.0
   /usr/local/lib/libtorrent.la
   /usr/local/lib/libtorrent.so.0.1.0
   /usr/local/lib/libtorrent.so

