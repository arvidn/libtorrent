#!/bin/bash

set -e
set -x

D=$(dirname $0)

curl -o "$D/build_utils.sh" https://raw.githubusercontent.com/pypa/manylinux/main/docker/build_scripts/build_utils.sh
chmod +x "$D/build_utils.sh"
curl -o "$D/build-openssl.sh" https://raw.githubusercontent.com/pypa/manylinux/main/docker/build_scripts/build-openssl.sh
chmod +x "$D/build-openssl.sh"
curl https://raw.githubusercontent.com/pypa/manylinux/main/docker/Dockerfile | {
	echo "#!/bin/bash"
	echo
	grep OPENSSL_ | sed 's/.*\(OPENSSL_[^=]*=[^ ]*\).*/export \1/g'
} > "$D/openssl-version.sh"
