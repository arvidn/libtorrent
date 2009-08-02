#!/bin/sh
rm -f config.cache
rm -fr autom4te.cache
chmod a-x docs/*.rst docs/*.htm*

/opt/local/bin/autoheader
/opt/local/bin/aclocal -I m4
/opt/local/bin/glibtool -f
/opt/local/bin/automake -ac
/opt/local/bin/autoconf
./configure --enable-python-binding --with-zlib=shipped --enable-examples=yes --enable-tests=yes
# --with-boost-python=mt-1_35 --with-boost-thread=mt-1_35 --with-boost-system=mt-1_35 --with-boost-filesystem=mt-1_35 --with-boost-regex=mt-1_35 --with-boost-program-options=mt-1_35
make dist check

