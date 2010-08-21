#! /bin/sh

# $Id$

set -x
set -e

rm -Rf libtorrent/asio libtorrent/asio.hpp asio
#cvs -d :pserver:anonymous@asio.cvs.sourceforge.net:/cvsroot/asio login # uncomment this if you're the first time, use empty password
cvs -d :pserver:anonymous@asio.cvs.sourceforge.net:/cvsroot/asio export -r asio-1-2-0 asio/include/asio asio/include/asio.hpp 
mv asio/include/asio libtorrent/
mv asio/include/asio.hpp libtorrent/

