==================================
Building libtorrent on Ubuntu 6.06
==================================

:Date: Oct 12, 2006
:Author: Xi Stan
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

   mkdir /home/you/work
   cd /home/you/work

Check out ``boost``, ``libtorrent``, ``asio`` source code from cvs
by executing the following commands::

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
the library themselves::

   cd /home/you/boost
   set BOOST_BUILD_PATH=/home/you/boost/tools/build/v2
   set BOOST_ROOT=/home/you/boost
   cd /home/you/boost/tools/build/boost-build/jam_src
   ./build.sh
   cp /bin.linuxx86/bjam /usr/bin
   cd /home/you/boost
   bjam -sTOOLS=gcc install

If you're successful you will see the followinf files in ``/usr/local/lib``::

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

Execute the following command::

   cp -R /home/you/asio/include/asio* /home/you/libtorrent/include/libtorrent

Step 4: Building libtorrent
===========================

To use the auto tools to build libtorrent, execute the following commands::

   cd /home/you/libtorrent
   export CXXFLAGS=-I/usr/local/include/boost-1_35
   export LDFLAGS=-L/usr/local/lib

   ./configure --with-boost-date-time=boost_date_time-gcc \
   --with-boost-filesystem=boost_filesystem-gcc \
   --with-boost-thread=boost_thread-gcc-mt

   make
   sudo make install

If successful, you will see the following file::

   /usr/local/lib/libtorrent.a
   /usr/local/lib/libtorrent.so.0
   /usr/local/lib/libtorrent.la
   /usr/local/lib/libtorrent.so.0.1.0
   /usr/local/lib/libtorrent.so

