AUTOMAKE_OPTIONS = subdir-objects

benchmark_programs = \
  bdecode_benchmark

test_programs = \
  test_primitives            \
  test_recheck               \
  test_stat_cache            \
  test_file                  \
  test_privacy               \
  test_priority              \
  test_auto_unchoke          \
  test_checking              \
  test_fast_extension        \
  test_http_connection       \
  test_lsd                   \
  test_pe_crypto             \
  test_pex                   \
  test_read_piece            \
  test_receive_buffer        \
  test_resume                \
  test_ssl                   \
  test_storage               \
  test_time_critical         \
  test_torrent               \
  test_tracker               \
  test_transfer              \
  enum_if                    \
  test_utp                   \
  test_session               \
  test_web_seed              \
  test_web_seed_ban          \
  test_web_seed_chunked      \
  test_web_seed_http         \
  test_web_seed_http_pw      \
  test_web_seed_redirect     \
  test_web_seed_socks4       \
  test_web_seed_socks5       \
  test_web_seed_socks5_no_peers \
  test_web_seed_socks5_pw    \
  test_url_seed              \
  test_remap_files           \
  test_enum_net              \
  test_file_progress         \
  test_linked_list           \
  test_direct_dht

if ENABLE_TESTS
check_PROGRAMS = $(test_programs) $(benchmark_programs)
noinst_LTLIBRARIES = libtest.la
endif

TESTS = $(test_programs)

EXTRA_DIST = Jamfile \
  test_torrents/backslash_path.torrent \
  test_torrents/base.torrent \
  test_torrents/creation_date.torrent \
  test_torrents/duplicate_files.torrent \
  test_torrents/duplicate_web_seeds.torrent \
  test_torrents/empty_httpseed.torrent \
  test_torrents/empty_path.torrent \
  test_torrents/empty_path_multi.torrent \
  test_torrents/hidden_parent_path.torrent \
  test_torrents/httpseed.torrent \
  test_torrents/invalid_file_size.torrent \
  test_torrents/invalid_info.torrent \
  test_torrents/invalid_name.torrent \
  test_torrents/invalid_name2.torrent \
  test_torrents/invalid_name3.torrent \
  test_torrents/invalid_path_list.torrent \
  test_torrents/invalid_piece_len.torrent \
  test_torrents/invalid_pieces.torrent \
  test_torrents/invalid_root_hash.torrent \
  test_torrents/invalid_root_hash2.torrent \
  test_torrents/long_name.torrent \
  test_torrents/missing_path_list.torrent \
  test_torrents/missing_piece_len.torrent \
  test_torrents/negative_file_size.torrent \
  test_torrents/negative_piece_len.torrent \
  test_torrents/negative_size.torrent \
  test_torrents/no_creation_date.torrent \
  test_torrents/no_name.torrent \
  test_torrents/pad_file.torrent \
  test_torrents/parent_path.torrent \
  test_torrents/root_hash.torrent \
  test_torrents/sample.torrent \
  test_torrents/single_multi_file.torrent \
  test_torrents/slash_path.torrent \
  test_torrents/slash_path2.torrent \
  test_torrents/slash_path3.torrent \
  test_torrents/string.torrent \
  test_torrents/symlink1.torrent \
  test_torrents/unaligned_pieces.torrent \
  test_torrents/url_list.torrent \
  test_torrents/url_list2.torrent \
  test_torrents/url_list3.torrent \
  test_torrents/url_seed.torrent \
  test_torrents/url_seed_multi.torrent \
  test_torrents/url_seed_multi_space.torrent \
  test_torrents/url_seed_multi_space_nolist.torrent \
  test_torrents/whitespace_url.torrent \
  mutable_test_torrents/test1.torrent \
  mutable_test_torrents/test1_pad_files.torrent \
  mutable_test_torrents/test1_single.torrent\
  mutable_test_torrents/test1_single_padded.torrent \
  mutable_test_torrents/test2.torrent \
  mutable_test_torrents/test2_pad_files.torrent \
  mutable_test_torrents/test3.torrent \
  mutable_test_torrents/test3_pad_files.torrent \
  zeroes.gz \
  utf8_test.txt \
  web_server.py \
  socks.py \
  http.py

EXTRA_PROGRAMS = $(test_programs) $(benchmark_programs)

noinst_HEADERS = test.hpp setup_transfer.hpp dht_server.hpp \
  peer_server.hpp udp_tracker.hpp web_seed_suite.hpp swarm_suite.hpp \
  test_utils.hpp settings.hpp make_torrent.hpp bittorrent_peer.hpp \
  print_alerts.hpp

libtest_la_SOURCES = main.cpp \
  test.cpp \
  setup_transfer.cpp \
  dht_server.cpp \
  udp_tracker.cpp \
  peer_server.cpp \
  bittorrent_peer.cpp \
  make_torrent.cpp \
  web_seed_suite.cpp \
  swarm_suite.cpp \
  test_utils.cpp \
  settings.cpp \
  print_alerts.cpp

test_primitives_SOURCES = \
  test_primitives.cpp \
  test_packet_buffer.cpp \
  test_timestamp_history.cpp \
  test_sha1_hash.cpp \
  test_bloom_filter.cpp \
  test_identify_client.cpp \
  test_merkle.cpp \
  test_alert_manager.cpp \
  test_resolve_links.cpp \
  test_crc32.cpp \
  test_heterogeneous_queue.cpp \
  test_ip_voter.cpp \
  test_sliding_average.cpp \
  test_socket_io.cpp \
  test_random.cpp \
  test_utf8.cpp \
  test_gzip.cpp \
  test_bitfield.cpp \
  test_part_file.cpp \
  test_peer_list.cpp \
  test_torrent_info.cpp \
  test_time.cpp \
  test_file_storage.cpp \
  test_peer_priority.cpp \
  test_threads.cpp \
  test_tailqueue.cpp \
  test_bandwidth_limiter.cpp \
  test_buffer.cpp \
  test_piece_picker.cpp \
  test_bencoding.cpp \
  test_bdecode.cpp \
  test_http_parser.cpp \
  test_string.cpp \
  test_magnet.cpp \
  test_xml.cpp \
  test_ip_filter.cpp \
  test_hasher.cpp \
  test_dht_storage.cpp \
  test_dht.cpp \
  test_block_cache.cpp \
  test_peer_classes.cpp \
  test_settings_pack.cpp \
  test_fence.cpp \
  test_dos_blocker.cpp \
  test_upnp.cpp

bdecode_benchmark_SOURCES = bdecode_benchmark.cpp
test_recheck_SOURCES = test_recheck.cpp
test_stat_cache_SOURCES = test_stat_cache.cpp
test_file_SOURCES = test_file.cpp
test_privacy_SOURCES = test_privacy.cpp
test_priority_SOURCES = test_priority.cpp
test_auto_unchoke_SOURCES = test_auto_unchoke.cpp
test_checking_SOURCES = test_checking.cpp
test_enum_net_SOURCES = test_enum_net.cpp
test_fast_extension_SOURCES = test_fast_extension.cpp
test_http_connection_SOURCES = test_http_connection.cpp
test_lsd_SOURCES = test_lsd.cpp
test_pe_crypto_SOURCES = test_pe_crypto.cpp
test_pex_SOURCES = test_pex.cpp
test_read_piece_SOURCES = test_read_piece.cpp
test_receive_buffer_SOURCES = test_receive_buffer.cpp
test_storage_SOURCES = test_storage.cpp
test_time_critical_SOURCES = test_time_critical.cpp
test_resume_SOURCES = test_resume.cpp
test_ssl_SOURCES = test_ssl.cpp
test_torrent_SOURCES = test_torrent.cpp
test_tracker_SOURCES = test_tracker.cpp
test_transfer_SOURCES = test_transfer.cpp
enum_if_SOURCES = enum_if.cpp
test_utp_SOURCES = test_utp.cpp
test_session_SOURCES = test_session.cpp
test_web_seed_SOURCES = test_web_seed.cpp
test_web_seed_ban_SOURCES = test_web_seed_ban.cpp
test_web_seed_chunked_SOURCES = test_web_seed_chunked.cpp
test_web_seed_http_SOURCES = test_web_seed_http.cpp
test_web_seed_http_pw_SOURCES = test_web_seed_http_pw.cpp
test_web_seed_redirect_SOURCES = test_web_seed_redirect.cpp
test_web_seed_socks4_SOURCES = test_web_seed_socks4.cpp
test_web_seed_socks5_SOURCES = test_web_seed_socks5.cpp
test_web_seed_socks5_no_peers_SOURCES = test_web_seed_socks5_no_peers.cpp
test_web_seed_socks5_pw_SOURCES = test_web_seed_socks5_pw.cpp
test_url_seed_SOURCES = test_url_seed.cpp
test_remap_files_SOURCES = test_remap_files.cpp
test_file_progress_SOURCES = test_file_progress.cpp
test_linked_list_SOURCES = test_linked_list.cpp
test_direct_dht_SOURCES = test_direct_dht.cpp

LDADD = libtest.la $(top_builddir)/src/libtorrent-rasterbar.la

#AM_CXXFLAGS=-ftemplate-depth-50 -I$(top_srcdir)/include -I$(top_srcdir)/include/libtorrent @DEBUGFLAGS@ @PTHREAD_CFLAGS@
AM_CPPFLAGS=-ftemplate-depth-50 -I$(top_srcdir)/include @DEBUGFLAGS@

AM_LDFLAGS=@BOOST_SYSTEM_LIB@ @PTHREAD_LIBS@ @OPENSSL_LDFLAGS@ @OPENSSL_LIBS@ \
	@BOOST_CHRONO_LIB@

