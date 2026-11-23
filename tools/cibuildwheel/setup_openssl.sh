#!/bin/sh

set -xe

TOOLS=$(dirname "$(readlink -f "$0")")

. "$TOOLS/manylinux/openssl-version.sh"

if [ -f /etc/redhat-release ]
then
	# The system OpenSSL on manylinux_2_28 (1.1.1) is new enough for
	# build-openssl.sh to skip building, but it's EOL and the image has no
	# headers or static libraries for it. Always build our own.
	yum install -y perl-core
	BUILD_DIR=$(mktemp -d)
	cd "$BUILD_DIR"
	curl -fsSL -o "${OPENSSL_ROOT}.tar.gz" "${OPENSSL_DOWNLOAD_URL}/${OPENSSL_ROOT}.tar.gz"
	echo "${OPENSSL_HASH}  ${OPENSSL_ROOT}.tar.gz" | sha256sum -c -
	tar -xzf "${OPENSSL_ROOT}.tar.gz"
	cd "${OPENSSL_ROOT}"
	./Configure --prefix=/usr/local/ssl --openssldir=/usr/local/ssl --libdir=lib > /dev/null
	make > /dev/null
	make install_sw > /dev/null
	cd /
	rm -rf "$BUILD_DIR"
else
	manylinux-entrypoint "$TOOLS/manylinux/build-openssl.sh"
fi

# If the build script finds a new enough openssl on the system, it will skip building.

if [ -d /usr/local/ssl ]
then
	ln -s /usr/local/ssl/include/openssl /usr/local/include
	ln -s /usr/local/ssl/lib/libcrypto.a /usr/local/lib
	ln -s /usr/local/ssl/lib/libssl.a /usr/local/lib
fi
