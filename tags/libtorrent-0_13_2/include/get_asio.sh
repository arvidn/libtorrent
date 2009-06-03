#! /bin/sh

# $Id$

set -x
set -e

rm -Rf asio
rm -Rf libtorrent/asio*
#cvs -d :pserver:anonymous@asio.cvs.sourceforge.net:/cvsroot/asio login # uncomment this if you're the first time, use empty password
cvs -d :pserver:anonymous@asio.cvs.sourceforge.net:/cvsroot/asio export -r asio-1-0-0 -d asio asio/include
ln -s ../asio/asio libtorrent
ln -s ../asio/asio.hpp libtorrent

