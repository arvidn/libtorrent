test_programs = \
  test_privacy               \
  test_file_storage          \
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
  test_dht                   \
  test_lsd                   \
  test_metadata_extension    \
  test_pe_crypto             \
  test_pex                   \
  test_piece_picker          \
  test_primitives            \
  test_read_piece            \
  test_rss                   \
  test_ssl                   \
  test_storage               \
  test_swarm                 \
  test_threads               \
  test_torrent               \
  test_torrent_parse         \
  test_tracker               \
  test_trackers_extension    \
  test_transfer              \
  test_upnp                  \
  test_utp                   \
  test_session               \
  test_web_seed              \
  test_remap_files

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
  test_torrents/empty_path_multi.torrent \
  test_torrents/duplicate_web_seeds.torrent \
  eztv.xml \
  kat.xml \
  cb.xml \
  mn.xml \
  pb.xml \
  upnp.xml

EXTRA_PROGRAMS = $(test_programs)

noinst_HEADERS = test.hpp setup_transfer.hpp

libtest_la_SOURCES = main.cpp setup_transfer.cpp dht_server.cpp peer_server.cpp

test_privacy_SOURCES = test_privacy.cpp
test_file_storage_SOURCES = test_file_storage.cpp
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
test_lsd_SOURCES = test_lsd.cpp
test_metadata_extension_SOURCES = test_metadata_extension.cpp
test_pe_crypto_SOURCES = test_pe_crypto.cpp
test_pex_SOURCES = test_pex.cpp
test_piece_picker_SOURCES = test_piece_picker.cpp
test_primitives_SOURCES = test_primitives.cpp
test_read_piece_SOURCES = test_read_piece.cpp
test_storage_SOURCES = test_storage.cpp
test_swarm_SOURCES = test_swarm.cpp
test_rss_SOURCES = test_rss.cpp
test_ssl_SOURCES = test_ssl.cpp
test_threads_SOURCES = test_threads.cpp
test_torrent_SOURCES = test_torrent.cpp
test_torrent_parse_SOURCES = test_torrent_parse.cpp
test_tracker_SOURCES = test_tracker.cpp
test_trackers_extension_SOURCES = test_trackers_extension.cpp
test_transfer_SOURCES = test_transfer.cpp
test_upnp_SOURCES = test_upnp.cpp
test_utp_SOURCES = test_utp.cpp
test_session_SOURCES = test_session.cpp
test_web_seed_SOURCES = test_web_seed.cpp
test_remap_files_SOURCES = test_remap_files.cpp

LDADD = $(top_builddir)/src/libtorrent-rasterbar.la libtest.la

#AM_CXXFLAGS=-ftemplate-depth-50 -I$(top_srcdir)/include -I$(top_srcdir)/include/libtorrent @DEBUGFLAGS@ @PTHREAD_CFLAGS@
AM_CPPFLAGS=-ftemplate-depth-50 -I$(top_srcdir)/include @DEBUGFLAGS@

#AM_LDFLAGS= @BOOST_SYSTEM_LIB@ @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @PTHREAD_LIBS@ @SSL_LDFLAGS@ @SSL_LIBS@
