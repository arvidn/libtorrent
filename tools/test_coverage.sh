#!/bin/bash

# $1 = test_name
# $2 = filename pattern for tested source files
function run_test {

	set -e
	if [[ ! -f test-coverage/coverage_$1_full ]]; then
		cd test
		B2_ARGS='sanitize=off asserts=off invariant-checks=off link=static deprecated-functions=off debug-iterators=off test-coverage=on picker-debugging=off'
		bjam $B2_ARGS $1 testing.execute=off
		EXE_PATH=$(ls -d bin/$1.test/*/debug/debug-iterators-off/deprecated-functions-off/export-extra-on/link-static/test-coverage-on/threading-multi)

		# force running the test
		rm -f $EXE_PATH/$1.output
		rm -f $EXE_PATH/$1.run
		rm -f $EXE_PATH/$1.test
		cd ..
		# expand the pattern to find the path to the object files
		OBJECT_PATH=$(ls -d bin/*/debug/debug-iterators-off/deprecated-functions-off/export-extra-on/link-static/test-coverage-on/threading-multi)
		# clear counters from last run
		rm -f $OBJECT_PATH/src/*.gcda
		rm -f $OBJECT_PATH/e25519/src/*.gcda
		rm -f test/$EXE_PATH/*.gcda
		cd test
		# now run the test
		bjam $B2_ARGS $1 -l250
		cd ..

		lcov --base-directory test -d test/$EXE_PATH -d $OBJECT_PATH/src -d $OBJECT_PATH/ed25519/src -c -o test-coverage/coverage_$1_full --exclude "/usr/*" --exclude "/Applications/Xcode.app/*" --exclude "*/boost/*"
	fi
	lcov --extract test-coverage/coverage_$1_full "$2" -o test-coverage/coverage_$1

	if [ ! -f test-coverage/coverage_all ]; then
		cp test-coverage/coverage_$1 test-coverage/coverage_all
	else
		lcov --add-tracefile test-coverage/coverage_$1 --add-tracefile test-coverage/coverage_all -o test-coverage/coverage_all
	fi

	if [[ $# > 2 ]]; then
		lcov --extract test-coverage/coverage_$1_full "$3" -o test-coverage/coverage_$1
		lcov --add-tracefile test-coverage/coverage_$1 --add-tracefile test-coverage/coverage_all -o test-coverage/coverage_all
	fi
	if [[ $# > 3 ]]; then
		lcov --extract test-coverage/coverage_$1_full "$4" -o test-coverage/coverage_$1
		lcov --add-tracefile test-coverage/coverage_$1 --add-tracefile test-coverage/coverage_all -o test-coverage/coverage_all
	fi
	set +e
}

mkdir -p test-coverage
rm -f test-coverage/coverage_all

run_test test_create_torrent "*/create_torrent.*"
run_test test_bandwidth_limiter "*/bandwidth_*.*"
run_test test_alloca "*/alloca.hpp"
run_test test_generate_peer_id "*/generate_peer_id.*"
run_test test_file_progress "*/file_progress.*"
run_test test_stack_allocator "*/stack_allocator.*"
run_test test_linked_list "*/linked_list.*"
run_test test_enum_net "*/enum_net.*"
run_test test_stat_cache "*/stat_cache.*"
run_test test_dos_blocker "*/dos_blocker.*"
run_test test_fence "*/disk_job_fence.*"
run_test test_settings_pack "*/settings_pack.*"
run_test test_timestamp_history "*/timestamp_history.*"
run_test test_merkle "*/merkle.*"
run_test test_resolve_links "*/resolve_links.*"
run_test test_heterogeneous_queue "*/heterogeneous_queue.*"
run_test test_socket_io "*/socket_io.*"
run_test test_peer_priority "*/torrent_peer.*"
run_test test_tailqueue "*/tailqueue.*"
run_test test_bencoding "*/entry.*" "*/bencode.*" "*/bdecode.*"
run_test test_bdecode "*/bdecode.*"
run_test test_io "*/io.hpp"
run_test test_block_cache "*/block_cache.*"
run_test test_peer_classes "*/peer_class*.*"
run_test test_bloom_filter "*/bloom_filter.*"
run_test test_sha1_hash "*/sha1_hash.*"
run_test test_identify_client "*/identify_client.*"
run_test test_packet_buffer "*/packet_buffer.*"
run_test test_ip_voter "*/ip_voter.*"
run_test test_bitfield "*/bitfield.*"
run_test test_alert_manager "*/alert_manager.*"
run_test test_alert_types "*/alert_types.*"
run_test test_dht "*/kademlia/*"
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
run_test test_string "*/escape_string.*" "*/string_util.*"
run_test test_utf8 "*/ConvertUTF.*"
run_test test_hasher "*/hasher.*"
run_test test_hasher512 "*/hasher512.*"
run_test test_span "*/span.hpp"
run_test test_crc32 "*/crc32c.*"
run_test test_ffs "*/ffs.cpp"
run_test test_ed25519 "*/ed25519/src/*"
run_test test_receive_buffer "*/receive_buffer.*"
run_test test_magnet "*/magnet_uri.*"
run_test test_session "*/session_impl.*" "*/session.*"
run_test test_remove_torrent "*/session_impl.*"
run_test test_read_piece "*/torrent.*"
run_test test_session_params "*/session.*"
run_test test_buffer "*/buffer.*"
run_test test_file "*/file.*"
run_test test_read_resume "*/read_resume_data.*" "*/write_resume_data.*"
run_test test_resume "*/torrent.*"
run_test test_checking "*/torrent.*"
run_test test_pe_crypto "*/pe_crypto.*"
run_test test_remap_files "*/file_storage.*" "*/torrent.*"
run_test test_time_critical "*/torrent.*" "*/peer_connection.*" "*/bt_peer_connection.*"
run_test test_pex "*/ut_pex.*"
run_test test_checking "*/torrent.*" "*/disk_io_thread.*"
run_test test_url_seed "*/web_peer_connection.*"
run_test test_web_seed "*/web_peer_connection.*"
run_test test_web_seed_redirect "*/web_peer_connection.*"
run_test test_web_seed_socks4 "*/web_peer_connection.*"
run_test test_web_seed_socks5 "*/web_peer_connection.*"
run_test test_web_seed_socks5_pw "*/web_peer_connection.*"
run_test test_web_seed_http "*/web_peer_connection.*"
run_test test_web_seed_http_pw "*/web_peer_connection.*"
run_test test_web_seed_chunked "*/web_peer_connection.*"
run_test test_web_seed_ban "*/web_peer_connection.*"
run_test test_torrent "*/torrent.*"
run_test test_auto_unchoke "*/session_impl.*"

genhtml -o test-coverage/ -t libtorrent-unit-tests --num-spaces=4 test-coverage/coverage_all

