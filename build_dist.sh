#!/bin/sh

set -e
set -x

python tools/clean.py

cd docs
make RST2HTML=rst2html-2.7.py
cd ..

rm -f m4/libtool.m4 m4/lt~obsolete.m4 m4/ltsugar.m4 m4/ltversion.m4 m4/ltoptions.m4
chmod a-x docs/*.rst docs/*.htm* src/*.cpp include/libtorrent/*.hpp

./autotool.sh
./configure --enable-python-binding --enable-examples=yes --enable-encryption --enable-tests=yes --with-boost-system=mt --with-boost-chrono=mt --with-boost-random=mt --with-boost-python=mt --with-openssl=/usr/local/opt/openssl
make V=1 -j8 check

./configure --enable-python-binding --enable-examples=yes --enable-encryption --with-boost-system=mt --with-boost-chrono=mt --with-boost-random=mt --with-boost-python=mt --with-openssl=/usr/local/opt/openssl
make V=1 -j8 dist

