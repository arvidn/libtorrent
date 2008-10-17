#!/bin/sh
rm -f config.cache
rm -fr autom4te.cache
chmod a-x docs/*.rst docs/*.htm*

/opt/local/bin/autoheader
/opt/local/bin/aclocal -I m4
/sw/bin/libtoolize -f
/opt/local/bin/automake -ac
/opt/local/bin/autoconf
./configure --with-zlib=shipped --enable-examples=yes --enable-tests=yes --with-boost-thread=mt-1_35 --with-boost-system=mt-1_35 --with-boost-filesystem=mt-1_35 --with-boost-regex=mt-1_35 --with-boost-program-options=mt-1_35
make dist check

