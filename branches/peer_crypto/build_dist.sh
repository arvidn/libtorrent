#!/bin/sh

make distclean

cd docs
make
cd ..

#clear out any extended attributes that Finder may add
sudo xattr -r -d com.apple.FinderInfo *

rm -f config.log config.report configure
rm -f m4/libtool.m4 m4/lt~obsolete.m4 m4/ltsugar.m4 m4/ltversion.m4 m4/ltoptions.m4
rm -fr autom4te.cache build-aux
rm -f Makefile Makefile.in
rm -f src/Makefile src/Makefile.in
rm -f include/libtorrent/Makefile include/libtorrent/Makefile.in
rm -f examples/Makefile examples/Makefile.in examples/.dep examples/.libs
rm -rf test/Makefile test/Makefile.in test/.dep test/.lib
rm -rf bindings/Makefile bindings/Makefile.in bindings/.dep bindings/.libs
rm -f bindings/python/Makefile bindings/python/Makefile.in
chmod a-x docs/*.rst docs/*.htm* src/*.cpp include/libtorrent/*.hpp

./autotool.sh
./configure --enable-python-binding --enable-examples=yes --enable-encryption --enable-tests=yes --with-boost-system=mt --with-boost-chrono=mt --with-boost-random=mt --with-boost-python=mt
make V=1 -j8 check distcheck

./configure --enable-python-binding --enable-examples=yes --enable-encryption --with-boost-system=mt --with-boost-chrono=mt --with-boost-random=mt --with-boost-python=mt
make V=1 -j8 distcheck

