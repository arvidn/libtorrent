set -ex

cd test
b2 -j$(nproc) -l300 link=static deprecated-functions=on,off crypto=openssl,built-in
b2 -j$(nproc) -l300 link=static crypto=gnutls webtorrent=on

cd ../simulation
b2 -j$(nproc) -l300 link=static

cd ../examples
b2 -j$(nproc) link=static deprecated-functions=on,off crypto=openssl

cd ../tools
b2 -j$(nproc) link=static deprecated-functions=on,off crypto=openssl

cd ../docs
make spell-check

cd ../bindings/python
b2 -j$(nproc) stage_module stage_dependencies
LD_LIBRARY_PATH=./dependencies python3 test.py

