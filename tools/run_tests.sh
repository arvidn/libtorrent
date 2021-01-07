set -ex

cd test
b2 -l300 link=static deprecated-functions=on,off crypto=openssl,built-in

cd ../simulation
b2 -l300 link=static

cd ../examples
b2 link=static deprecated-functions=on,off crypto=openssl

cd ../tools
b2 link=static deprecated-functions=on,off crypto=openssl

cd ../docs
make spell-check

cd ../bindings/python
b2 stage_module stage_dependencies
LD_LIBRARY_PATH=./dependencies python3 test.py

