#!/bin/sh

set -xe

if [ $(uname -m) != x86_64 -a $(uname -m) != aarch64 ]
then
	echo "ccache isn't known to exist on $(uname -m). skipping ccache setup"
	exit 0
fi

yum install -y epel-release  # overlay containing ccache
yum install -y ccache

# The symlinks in /usr/lib64/ccache are auto-managed by rpm postinstall
# scripts. They are only created if appropriate packages are installed. However
# this management only knows about the standard gcc* packages, not the
# devtoolset packages, so they don't get created correctly on manylinux. We try
# to create them ourselves.
mkdir -p /usr/local/ccache/bin
for path in /opt/rh/devtoolset-*/root/usr/bin/*cc /opt/rh/devtoolset-*/root/usr/bin/*cc-[0-9]* /opt/rh/devtoolset-*/root/usr/bin/*++ /opt/rh/devtoolset-*/root/usr/bin/*++-[0-9]* /opt/rh/devtoolset-*/root/usr/bin/*cpp /opt/rh/devtoolset-*/root/usr/bin/*cpp-[0-9]*
do
	ln -s /usr/bin/ccache "/usr/local/ccache/bin/$(basename "$path")"
done
