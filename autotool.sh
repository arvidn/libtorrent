#!/bin/sh

# $Id$

# The result of using "autoreconf -fi" should be identical to using this
# script.

set -e
set -x

#${AUTOPOINT:-autopoint} -f
${LIBTOOLIZE:-libtoolize} -c -f || glibtoolize -c -f
${ACLOCAL:-aclocal} -I m4
${AUTOCONF:-autoconf}
#${AUTOHEADER:-autoheader}
${AUTOMAKE:-automake} -acf --foreign
