#!/bin/sh

set -e
set -x

python tools/clean.py

cd docs
make
cd ..

rm -f m4/libtool.m4 m4/lt~obsolete.m4 m4/ltsugar.m4 m4/ltversion.m4 m4/ltoptions.m4
chmod a-x docs/*.rst docs/*.htm* src/*.cpp include/libtorrent/*.hpp

./autotool.sh
./configure --enable-python-binding --enable-examples=yes --enable-encryption --enable-tests=yes
make V=1 -j16 check

./configure --enable-python-binding --enable-examples=yes --enable-encryption
make V=1 -j16 dist

