#!/bin/bash

wget http://releases.linaro.org/components/toolchain/binaries/latest-5/aarch64-linux-gnu/gcc-linaro-5.3-2016.02-x86_64_aarch64-linux-gnu.tar.xz
tar xf gcc-linaro-5.3-2016.02-x86_64_aarch64-linux-gnu.tar.xz
export PATH=${PWD}/gcc-linaro-5.3-2016.02-x86_64_aarch64-linux-gnu/bin:${PATH}
aarch64-linux-gnu-gcc --version;

wget -O boost.zip http://pilotfiber.dl.sourceforge.net/project/boost/boost/1.55.0/boost_1_55_0.zip
unzip -qq boost.zip
export BOOST_ROOT=$PWD/boost_1_55_0

echo "using gcc : arm64 : aarch64-linux-gnu-gcc : <cflags>-std=c11 <cxxflags>-std=c++11 <cxxflags>-fsigned-char <linkflags>-lstdc++ <linkflags>-lm ;" >> ~/user-config.jam
cd tools
bjam warnings-as-errors=on variant=test_arm toolset=gcc-arm64 target-os=linux link=static location=./bin/gcc-arm64
cd ..

sudo apt-get install -y qemu-user-static debootstrap
sudo debootstrap --variant=minbase --arch arm64 --foreign --include=build-essential testing rootfs
sudo cp /usr/bin/qemu-aarch64-static rootfs/usr/bin/
sudo chroot rootfs /debootstrap/debootstrap --second-stage

sudo cp -R ./tools/bin/gcc-arm64/test_arm rootfs/
sudo chroot rootfs mount -t proc none /proc

sudo chroot rootfs ./test_arm
