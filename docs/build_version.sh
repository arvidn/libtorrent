#!/bin/sh

set -x

# echo pull version from version header
VERSION=$(grep "#define LIBTORRENT_VERSION " ../include/libtorrent/version.hpp | awk '{ print $3 }')
# strip quotes
VERSION=${VERSION%\"}
VERSION=${VERSION#\"}
mkdir -p version/${VERSION}
TARGET=version/${VERSION}

make stage WEB_PATH=${TARGET}

