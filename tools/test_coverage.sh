#!/bin/bash

OBJECT_PATH=bin/gcc-4.8/debug/asserts-off/boost-source/deprecated-functions-off/export-extra-on/invariant-checks-off/link-static/test-coverage-on/threading-multi/src

# $1 = test_name
# $2 = filename pattern for tested source files
# $3 = optional bjam arguments
function run_test {
	set +e
	rm $OBJECT_PATH/*.gcda
	set -e

	cd test

	set +e
	bjam asserts=off invariant-checks=off link=static deprecated-functions=off debug-iterators=off test-coverage=on picker-debugging=off -j4 $1
	set -e
	cd ..

	lcov -d $OBJECT_PATH/ -c -o coverage_$1_full
	lcov --extract coverage_$1_full "$2" -o test-coverage/coverage_$1
	if [ ! -f test-coverage/coverage_all ]; then
		cp test-coverage/coverage_$1 test-coverage/coverage_all
	else
		lcov --add-tracefile test-coverage/coverage_$1 --add-tracefile test-coverage/coverage_all -o test-coverage/coverage_all
	fi
	rm coverage_$1_full
}

# force rebuilding and rerunning the unit tests
cd test
set +e
rm -rf bin
set -e
cd ..

set +e
mkdir test-coverage
rm test-coverage/coverage_all
set -e

run_test test_bloom_filter "*/bloom_filter.*" -a
run_test test_sha1_hash "*/sha1_hash.*"
run_test test_identify_client "*/identify_client.*"
run_test test_packet_buffer "*/packet_buffer.*"
run_test test_ip_voter "*/ip_voter.*"
run_test test_bitfield "*/bitfield.*"
run_test test_alert_manager "*/alert_manager.*"
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

genhtml -o test-coverage/ -t $1 --num-spaces=4 test-coverage/coverage_all

