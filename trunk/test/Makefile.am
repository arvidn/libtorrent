check_PROGRAMS = test_hasher test_bencoding test_ip_filter test_piece_picker \
test_storage test_metadata_extension test_buffer test_swarm test_pe_crypto test_primitives \
test_bandwidth_limiter test_upnp test_fast_extension test_pex test_web_seed \
test_http_connection test_lsd

TESTS = $(check_PROGRAMS)

EXTRA_DIST = Jamfile

test_lsd_SOURCES = main.cpp setup_transfer.cpp test_lsd.cpp
test_lsd_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_http_connection_SOURCES = main.cpp setup_transfer.cpp test_http_connection.cpp
test_http_connection_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_web_seed_SOURCES = main.cpp setup_transfer.cpp test_web_seed.cpp
test_web_seed_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_pex_SOURCES = main.cpp setup_transfer.cpp test_pex.cpp
test_pex_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_fast_extension_SOURCES = main.cpp setup_transfer.cpp test_fast_extension.cpp
test_fast_extension_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_upnp_SOURCES = test_upnp.cpp
test_upnp_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_hasher_SOURCES = main.cpp test_hasher.cpp
test_hasher_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_bencoding_SOURCES = main.cpp test_bencoding.cpp
test_bencoding_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_ip_filter_SOURCES = main.cpp test_ip_filter.cpp
test_ip_filter_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_piece_picker_SOURCES = main.cpp test_piece_picker.cpp
test_piece_picker_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_storage_SOURCES = main.cpp setup_transfer.cpp test_storage.cpp
test_storage_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_buffer_SOURCES = main.cpp test_buffer.cpp
test_buffer_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_metadata_extension_SOURCES = main.cpp setup_transfer.cpp test_metadata_extension.cpp
test_metadata_extension_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_swarm_SOURCES = main.cpp setup_transfer.cpp test_swarm.cpp
test_swarm_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_pe_crypto_SOURCES = main.cpp setup_transfer.cpp test_pe_crypto.cpp
test_pe_crypto_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_primitives_SOURCES = main.cpp test_primitives.cpp
test_primitives_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_bandwidth_limiter_SOURCES = main.cpp test_bandwidth_limiter.cpp
test_bandwidth_limiter_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

test_torrent_SOURCES = main.cpp test_torrent.cpp
test_torrent_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

noinst_HEADERS = test.hpp setup_transfer.hpp

AM_CXXFLAGS=-ftemplate-depth-50 -I$(top_srcdir)/include -I$(top_srcdir)/include/libtorrent @DEBUGFLAGS@ @PTHREAD_CFLAGS@ -DBOOST_MULTI_INDEX_DISABLE_SERIALIZATION
AM_LDFLAGS= @BOOST_SYSTEM_LIB@ @BOOST_IOSTREAMS_LIB@ @BOOST_DATE_TIME_LIB@ @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @PTHREAD_LIBS@ @SSL_LDFLAGS@ @SSL_LIBS@
