#!/bin/sh

set -e
set -x

python3 tools/clean.py

cd docs
make
cd ..

rm -f m4/libtool.m4 m4/lt~obsolete.m4 m4/ltsugar.m4 m4/ltversion.m4 m4/ltoptions.m4
chmod a-x docs/*.rst docs/*.htm* src/*.cpp include/libtorrent/*.hpp

./autotool.sh
./configure --enable-python-binding --enable-examples=yes --enable-encryption --enable-tests=yes
make dist

VERSION=1.2.18

tar xvzf libtorrent-rasterbar-${VERSION}.tar.gz
cd libtorrent-rasterbar-${VERSION}/test

b2 link=static $1

