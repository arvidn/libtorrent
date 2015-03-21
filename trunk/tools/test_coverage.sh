#!/bin/bash

OBJECT_PATH=bin/gcc-4.8/debug/asserts-off/boost-source/deprecated-functions-off/export-extra-on/invariant-checks-off/link-static/logging-on/test-coverage-on/threading-multi/src

function run_test {
	set +e
	rm $OBJECT_PATH/*.gcda
	set -e

	cd test
	set +e
	rm -rf bin

	bjam asserts=off invariant-checks=off link=static boost=source deprecated-functions=off debug-iterators=off test-coverage=on picker-debugging=off -j4 $1
	cd ..
	set -e

	lcov -d $OBJECT_PATH/ -c -o coverage_$1_full
	lcov --extract coverage_$1_full "$2" -o coverage_$1
	rm -rf test-coverage/$1
	genhtml -o test-coverage/$1 -t $1 --num-spaces 4 coverage_$1
	rm coverage_$1 coverage_$1_full
	
}

# force rebuilding and rerunning the unit tests
cd test
set +e
rm -rf bin
set -e
cd ..

set +e
mkdir test-coverage
set -e

run_test test_dht "*/kademlia/*"
run_test test_bdecode "*/bdecode.*"
run_test test_piece_picker "*/piece_picker.*"
run_test test_torrent_info "*/torrent_info.*"
run_test test_part_file "*/part_file.*"
run_test test_http_parser "*/http_parser.*"
run_test test_ip_filter "*/ip_filter.*"
run_test test_utp "*/utp_stream.*"
run_test test_peer_list "*/peer_list.*"
run_test test_gzip "*/gzip.cpp"
run_test test_file_storage "*/file_storage.*"
run_test test_storage "*/storage.*"
run_test test_xml "*/xml_parse.*"
run_test test_sliding_average "*/sliding_average.*"
run_test test_string "*/escape_string.*"
run_test test_utf8 "*/ConvertUTF.*"

