#! /bin/sh

# $Id$

cd ..
# build package using BTG key (http://btg.berlios.de), by default
fakeroot dpkg-buildpackage -k9277A46E -tc
