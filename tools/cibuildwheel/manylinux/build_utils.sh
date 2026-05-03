#!/bin/bash
# Helper utilities for build


# use all flags used by ubuntu 20.04 for hardening builds, dpkg-buildflags --export
# other flags mentioned in https://wiki.ubuntu.com/ToolChain/CompilerFlags can't be
# used because the distros used here are too old
MANYLINUX_CPPFLAGS="-Wdate-time -D_FORTIFY_SOURCE=2"
MANYLINUX_CFLAGS="-g -O2 -Wall -fdebug-prefix-map=/=. -fstack-protector-strong -Wformat -Werror=format-security"
MANYLINUX_CXXFLAGS="-g -O2 -Wall -fdebug-prefix-map=/=. -fstack-protector-strong -Wformat -Werror=format-security"
MANYLINUX_LDFLAGS="-Wl,-Bsymbolic-functions -Wl,-z,relro -Wl,-z,now"

if [ "${AUDITWHEEL_POLICY:0:10}" == "musllinux_" ]; then
	export BASE_POLICY=musllinux
	PACKAGE_MANAGER=apk
else
	export BASE_POLICY=manylinux
	if command -v dnf >/dev/null 2>&1; then
		PACKAGE_MANAGER=dnf
	elif command -v yum >/dev/null 2>&1; then
		PACKAGE_MANAGER=yum
	elif command -v apt-get >/dev/null 2>&1; then
		PACKAGE_MANAGER=apt
	else
		echo "unsupported image"
		exit 1
	fi
fi

# shellcheck source=/dev/null
OS_ID_LIKE=$(. /etc/os-release; echo "${ID} ${ID_LIKE:-}")
case "${OS_ID_LIKE}" in
	*rhel*) OS_ID_LIKE=rhel;;
	*debian) OS_ID_LIKE=debian;;
	*alpine*) OS_ID_LIKE=alpine;;
	*) echo "unsupported image"; exit 1;;
esac

function check_var {
	if [ -z "$1" ]; then
		echo "required variable not defined"
		exit 1
	fi
}


function fetch_source {
	# This is called both inside and outside the build context (e.g. in Travis) to prefetch
	# source tarballs, where curl exists (and works)
	local file=$1
	check_var "${file}"
	local url=$2
	check_var "${url}"
	if [ -f "${file}" ]; then
		echo "${file} exists, skipping fetch"
	else
		curl -fsSL -o "${file}" "${url}/${file}"
	fi
}


function check_sha256sum {
	local fname=$1
	check_var "${fname}"
	local sha256=$2
	check_var "${sha256}"

	echo "${sha256}  ${fname}" > "${fname}.sha256"
	sha256sum -c "${fname}.sha256"
	rm -f "${fname}.sha256"
}

# shellcheck disable=SC2120 # optional arguments
function do_standard_install {
	./configure "$@" CPPFLAGS="${MANYLINUX_CPPFLAGS}" CFLAGS="${MANYLINUX_CFLAGS}" "CXXFLAGS=${MANYLINUX_CXXFLAGS}" LDFLAGS="${MANYLINUX_LDFLAGS}" > /dev/null
	make > /dev/null
	make install > /dev/null
}

function strip_ {
	# Strip what we can -- and ignore errors, because this just attempts to strip
	# *everything*, including non-ELF files:
	find "$1" -type f -print0 | xargs -0 -n1 strip --strip-unneeded 2>/dev/null || true
}

function clean_pyc {
	find "$1" -type f -a \( -name '*.pyc' -o -name '*.pyo' \) -delete
}

function manylinux_pkg_install {
	if [ "${PACKAGE_MANAGER}" = "yum" ]; then
		yum -y install "$@"
	elif [ "${PACKAGE_MANAGER}" = "dnf" ]; then
		dnf -y install --allowerasing "$@"
	elif  [ "${PACKAGE_MANAGER}" = "apt" ]; then
		DEBIAN_FRONTEND=noninteractive apt-get update -qq
		DEBIAN_FRONTEND=noninteractive apt-get install -qq -y --no-install-recommends "$@"
	elif [ "${PACKAGE_MANAGER}" = "apk" ]; then
		apk add --no-cache "$@"
	else
		return 1
	fi
}

function manylinux_pkg_remove {
	if [ "${PACKAGE_MANAGER}" = "yum" ]; then
		yum erase -y "$@"
	elif [ "${PACKAGE_MANAGER}" = "dnf" ];then
		dnf erase -y "$@"
	elif [ "${PACKAGE_MANAGER}" = "apt" ];then
		DEBIAN_FRONTEND=noninteractive apt-get remove -y "$@"
	elif [ "${PACKAGE_MANAGER}" = "apk" ]; then
		apk del "$@"
	else
		return 1
	fi
}

function manylinux_pkg_clean {
	if [ "${PACKAGE_MANAGER}" = "yum" ]; then
		yum clean all
		rm -rf /var/cache/yum
	elif [ "${PACKAGE_MANAGER}" = "dnf" ]; then
		dnf clean all
		rm -rf /var/cache/dnf
	elif  [ "${PACKAGE_MANAGER}" = "apt" ]; then
		DEBIAN_FRONTEND=noninteractive apt-get clean -qq
		rm -rf /var/lib/apt/lists/*
	elif [ "${PACKAGE_MANAGER}" = "apk" ]; then
		:
	else
		return 1
	fi
}
