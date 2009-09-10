#!/bin/sh

rm -f config.log config.report configure
rm -f m4/libtool.m4 m4/lt~obsolete.m4 m4/ltsugar.m4 m4/ltversion.m4 m4/ltoptions.m4
rm -fr autom4te.cache build-aux
find -name Makefile -o -name Makefile.in -exec rm '{}' \;
chmod a-x docs/*.rst docs/*.htm* src/*.cpp include/libtorrent/*.hpp

./autotool.sh
./configure --enable-python-binding --with-zlib=shipped --enable-examples=yes --enable-tests=yes --with-boost-system=mt --with-boost-python=mt --with-boost-thread=mt --with-boost-filesystem=mt
make -j 8 dist check
