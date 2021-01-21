#!/bin/sh

set -ex

cd test
nice b2 -l300 link=static deprecated-functions=on,off crypto=openssl,built-in
nice b2 -l300 link=static crypto=gnutls webtorrent=on

cd ../simulation
nice b2 -l300 link=static

cd ../examples
nice b2 link=static deprecated-functions=on,off crypto=openssl

cd ../tools
nice b2 link=static deprecated-functions=on,off crypto=openssl

cd ../docs
make spell-check

cd ../bindings/python
nice b2 stage_module stage_dependencies
LD_LIBRARY_PATH=./dependencies python3 test.py

