#!/bin/sh

# $Id$

set -e
set -x

aclocal -I m4
libtoolize -c -f
automake -a -c -f
autoconf

rm -Rf config.cache autom4te.cache
