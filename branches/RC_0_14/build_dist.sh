#!/bin/sh
rm -f config.cache
rm -fr autom4te.cache
rm -rf config.log
rm -rf Makefile
chmod a-x docs/*.rst docs/*.htm*

autoheader
aclocal -I m4
glibtoolize -f
automake -ac
autoconf
./configure --enable-python-binding --with-zlib=shipped --enable-examples=yes --enable-tests=yes --with-boost-system=mt --with-boost-python=mt --with-boost-thread=mt --with-boost-filesystem=mt --with-boost-regex=mt --with-boost-program-options=mt
make -j 8 dist check

