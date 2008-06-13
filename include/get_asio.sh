#! /bin/sh

# $Id$

set -x
set -e

rm -Rf asio
rm -Rf libtorrent/asio*
#cvs -d :pserver:anonymous@asio.cvs.sourceforge.net:/cvsroot/asio login # uncomment this if you're the first time, use empty password
cvs -d :pserver:anonymous@asio.cvs.sourceforge.net:/cvsroot/asio export -D 'Fri, 13 Jun 2008 14:18:47 +0400' -d asio asio/include
ln -s ../asio/asio libtorrent
ln -s ../asio/asio.hpp libtorrent
