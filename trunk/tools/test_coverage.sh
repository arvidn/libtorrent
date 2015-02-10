#!/bin/sh

cd test
bjam asserts=off invariant-checks=off link=static boost=source test-coverage=on picker-debugging=off -j4
cd ..
lcov --zerocounters -q 
lcov -d bin/gcc-4.8/debug/asserts-off/boost-source/debug-iterators-on/export-extra-on/invariant-checks-off/link-static/logging-on/picker-debugging-off/test-coverage-on/threading-multi/src/ -c -o coverage
lcov --remove coverage "/usr*" -o coverage
lcov --remove coverage "*/boost/*" -o coverage
genhtml -o test_coverage -t "libtorrent test coverage" --num-spaces 4 coverage

