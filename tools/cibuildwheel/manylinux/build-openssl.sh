#!/bin/bash
# Top-level build script called from Dockerfile

# Stop at any error, show all commands
set -exuo pipefail

# Get script directory
MY_DIR=$(dirname "${BASH_SOURCE[0]}")

# Get build utilities
# shellcheck source-path=SCRIPTDIR
source "${MY_DIR}/build_utils.sh"

# Install a more recent openssl
check_var "${OPENSSL_ROOT}"
check_var "${OPENSSL_HASH}"
check_var "${OPENSSL_DOWNLOAD_URL}"

OPENSSL_VERSION=${OPENSSL_ROOT#*-}
OPENSSL_MIN_VERSION=1.1.1

# || test $? -eq 141 is there to ignore SIGPIPE with set -o pipefail
# c.f. https://stackoverflow.com/questions/22464786/ignoring-bash-pipefail-for-error-code-141#comment60412687_33026977
INSTALLED=$( (openssl version | head -1 || test $? -eq 141) | awk '{ print $2 }')
SMALLEST=$(echo -e "${INSTALLED}\n${OPENSSL_MIN_VERSION}" | sort -t. -k 1,1n -k 2,2n -k 3,3n -k 4,4n | head -1 || test $? -eq 141)

# Ignore letters in version numbers
if [ "${SMALLEST}" = "${OPENSSL_MIN_VERSION}" ]; then
	echo "skipping installation of openssl ${OPENSSL_VERSION}, system provides openssl ${INSTALLED} which is newer than openssl ${OPENSSL_MIN_VERSION}"
	exit 0
fi

if [ "${OS_ID_LIKE}" = "rhel" ];then
	manylinux_pkg_remove openssl-devel
elif [ "${OS_ID_LIKE}" = "debian" ];then
	manylinux_pkg_remove libssl-dev
elif [ "${OS_ID_LIKE}" = "alpine" ]; then
	manylinux_pkg_remove openssl-dev
fi

PREFIX=/opt/_internal/openssl-${OPENSSL_VERSION%.*}
manylinux_pkg_install perl-core
fetch_source "${OPENSSL_ROOT}.tar.gz" "${OPENSSL_DOWNLOAD_URL}"
check_sha256sum "${OPENSSL_ROOT}.tar.gz" "${OPENSSL_HASH}"
tar -xzf "${OPENSSL_ROOT}.tar.gz"
pushd "${OPENSSL_ROOT}"
./Configure "--prefix=${PREFIX}" "--openssldir=${PREFIX}" --libdir=lib CPPFLAGS="${MANYLINUX_CPPFLAGS}" CFLAGS="${MANYLINUX_CFLAGS}" CXXFLAGS="${MANYLINUX_CXXFLAGS}" LDFLAGS="${MANYLINUX_LDFLAGS} -Wl,-rpath,\$(LIBRPATH)" > /dev/null
make > /dev/null
make install_sw > /dev/null
popd
rm -rf "${OPENSSL_ROOT}" "${OPENSSL_ROOT}.tar.gz"

strip_ "${PREFIX}"

"${PREFIX}/bin/openssl" version
ln -s "${PREFIX}" /usr/local/ssl
