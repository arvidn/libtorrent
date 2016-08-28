AUTOMAKE_OPTIONS = subdir-objects

test_programs = \
  test_bitfield              \
  test_file                  \
  test_file_storage          \
  test_privacy               \
  test_priority              \
  test_auto_unchoke          \
  test_bandwidth_limiter     \
  test_bdecode_performance   \
  test_bencoding             \
  test_buffer                \
  test_checking              \
  test_fast_extension        \
  test_hasher                \
  test_http_connection       \
  test_ip_filter             \
  test_ip_voter              \
  test_dht                   \
  test_lsd                   \
  test_metadata_extension    \
  test_pe_crypto             \
  test_peer_priority         \
  test_pex                   \
  test_piece_picker          \
  test_xml                   \
  test_string                \
  test_primitives            \
  test_http_parser           \
  test_magnet                \
  test_packet_buffer         \
  test_read_piece            \
  test_resume                \
  test_rss                   \
  test_ssl                   \
  test_storage               \
  test_swarm                 \
  test_time                  \
  test_threads               \
  test_torrent               \
  test_torrent_info          \
  test_torrent_parse         \
  test_tracker               \
  test_trackers_extension    \
  test_transfer              \
  test_create_torrent        \
  test_upnp                  \
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
  test_gzip                  \
  test_utf8                  \
  test_socket_io             \
  test_sliding_average

if ENABLE_TESTS
check_PROGRAMS = $(test_programs)
noinst_LTLIBRARIES = libtest.la
endif

TESTS = $(check_PROGRAMS)

EXTRA_DIST = Jamfile \
  test_torrents/base.torrent \
  test_torrents/empty_path.torrent \
  test_torrents/parent_path.torrent \
  test_torrents/hidden_parent_path.torrent \
  test_torrents/single_multi_file.torrent \
  test_torrents/slash_path.torrent \
  test_torrents/slash_path2.torrent \
  test_torrents/slash_path3.torrent \
  test_torrents/backslash_path.torrent \
  test_torrents/url_list.torrent \
  test_torrents/url_list2.torrent \
  test_torrents/url_list3.torrent \
  test_torrents/httpseed.torrent \
  test_torrents/empty_httpseed.torrent \
  test_torrents/long_name.torrent \
  test_torrents/whitespace_url.torrent \
  test_torrents/duplicate_files.torrent \
  test_torrents/invalid_piece_len.torrent \
  test_torrents/missing_piece_len.torrent \
  test_torrents/negative_piece_len.torrent \
  test_torrents/no_name.torrent \
  test_torrents/invalid_name.torrent \
  test_torrents/invalid_name2.torrent \
  test_torrents/invalid_name3.torrent \
  test_torrents/invalid_info.torrent \
  test_torrents/string.torrent \
  test_torrents/negative_size.torrent \
  test_torrents/negative_file_size.torrent \
  test_torrents/pad_file.torrent \
  test_torrents/invalid_path_list.torrent \
  test_torrents/missing_path_list.torrent \
  test_torrents/invalid_pieces.torrent \
  test_torrents/unaligned_pieces.torrent \
  test_torrents/creation_date.torrent \
  test_torrents/no_creation_date.torrent \
  test_torrents/symlink1.torrent \
  test_torrents/url_seed.torrent \
  test_torrents/url_seed_multi.torrent \
  test_torrents/url_seed_multi_space.torrent \
  test_torrents/url_seed_multi_space_nolist.torrent \
  test_torrents/invalid_root_hash.torrent \
  test_torrents/invalid_root_hash2.torrent \
  test_torrents/root_hash.torrent \
  test_torrents/invalid_file_size.torrent \
  test_torrents/empty_path_multi.torrent \
  test_torrents/duplicate_web_seeds.torrent \
  eztv.xml \
  kat.xml \
  cb.xml \
  mn.xml \
  pb.xml \
  upnp.xml \
  zeroes.gz \
  utf8_test.txt \
  test_natpmp.cpp

EXTRA_PROGRAMS = $(test_programs)

noinst_HEADERS = test.hpp setup_transfer.hpp dht_server.hpp \
	peer_server.hpp udp_tracker.hpp web_seed_suite.hpp

libtest_la_SOURCES = main.cpp \
  setup_transfer.cpp \
  dht_server.cpp \
  udp_tracker.cpp \
  peer_server.cpp \
  web_seed_suite.cpp

test_bitfield_SOURCES = test_bitfield.cpp
test_file_SOURCES = test_file.cpp
test_file_storage_SOURCES = test_file_storage.cpp
test_privacy_SOURCES = test_privacy.cpp
test_priority_SOURCES = test_priority.cpp
test_auto_unchoke_SOURCES = test_auto_unchoke.cpp
test_bandwidth_limiter_SOURCES = test_bandwidth_limiter.cpp
test_bdecode_performance_SOURCES = test_bdecode_performance.cpp
test_dht_SOURCES = test_dht.cpp
test_bencoding_SOURCES = test_bencoding.cpp
test_buffer_SOURCES = test_buffer.cpp
test_checking_SOURCES = test_checking.cpp
test_fast_extension_SOURCES = test_fast_extension.cpp
test_hasher_SOURCES = test_hasher.cpp
test_http_connection_SOURCES = test_http_connection.cpp
test_ip_filter_SOURCES = test_ip_filter.cpp
test_ip_voter_SOURCES = test_ip_voter.cpp
test_lsd_SOURCES = test_lsd.cpp
test_metadata_extension_SOURCES = test_metadata_extension.cpp
test_peer_priority_SOURCES = test_peer_priority.cpp
test_pe_crypto_SOURCES = test_pe_crypto.cpp
test_pex_SOURCES = test_pex.cpp
test_piece_picker_SOURCES = test_piece_picker.cpp
test_xml_SOURCES = test_xml.cpp
test_string_SOURCES = test_string.cpp
test_primitives_SOURCES = test_primitives.cpp
test_http_parser_SOURCES = test_http_parser.cpp
test_magnet_SOURCES = test_magnet.cpp
test_packet_buffer_SOURCES = test_packet_buffer.cpp
test_read_piece_SOURCES = test_read_piece.cpp
test_storage_SOURCES = test_storage.cpp
test_swarm_SOURCES = test_swarm.cpp
test_resume_SOURCES = test_resume.cpp
test_rss_SOURCES = test_rss.cpp
test_ssl_SOURCES = test_ssl.cpp
test_time_SOURCES = test_time.cpp
test_threads_SOURCES = test_threads.cpp
test_torrent_SOURCES = test_torrent.cpp
test_torrent_info_SOURCES = test_torrent_info.cpp
test_torrent_parse_SOURCES = test_torrent_parse.cpp
test_tracker_SOURCES = test_tracker.cpp
test_trackers_extension_SOURCES = test_trackers_extension.cpp
test_transfer_SOURCES = test_transfer.cpp
test_create_torrent_SOURCES = test_create_torrent.cpp
test_upnp_SOURCES = test_upnp.cpp
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
test_gzip_SOURCES = test_gzip.cpp
test_utf8_SOURCES = test_utf8.cpp
test_socket_io_SOURCES = test_socket_io.cpp
test_sliding_average_SOURCES = test_sliding_average.cpp

LDADD = libtest.la $(top_builddir)/src/libtorrent-rasterbar.la

#AM_CXXFLAGS=-ftemplate-depth-50 -I$(top_srcdir)/include -I$(top_srcdir)/include/libtorrent @DEBUGFLAGS@ @PTHREAD_CFLAGS@
AM_CPPFLAGS=-ftemplate-depth-50 -I$(top_srcdir)/include @DEBUGFLAGS@

AM_LDFLAGS=@BOOST_SYSTEM_LIB@ @PTHREAD_LIBS@ @OPENSSL_LDFLAGS@ @OPENSSL_LIBS@

