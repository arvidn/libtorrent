#!/bin/sh

set -xe

TOOLS=$(dirname "$(readlink -f "$0")")

. "$TOOLS/manylinux/openssl-version.sh"
manylinux-entrypoint "$TOOLS/manylinux/build-openssl.sh"

# If the build script finds a new enough openssl on the system, it will skip building.

if [ -d /usr/local/ssl ]
then
	ln -s /usr/local/ssl/include/openssl /usr/local/include
	ln -s /usr/local/ssl/lib/libcrypto.a /usr/local/lib
	ln -s /usr/local/ssl/lib/libssl.a /usr/local/lib
fi
