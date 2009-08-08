if ENABLE_EXAMPLES
# no indent here! - BSD make issue
bin_PROGRAMS = @CLIENT_TEST_BIN@ dump_torrent make_torrent simple_client enum_if
endif
EXTRA_PROGRAMS = client_test dump_torrent make_torrent simple_client enum_if
EXTRA_DIST = Jamfile

client_test_SOURCES = client_test.cpp
client_test_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la @BOOST_REGEX_LIB@ @BOOST_PROGRAM_OPTIONS_LIB@

dump_torrent_SOURCES = dump_torrent.cpp
dump_torrent_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

make_torrent_SOURCES = make_torrent.cpp
make_torrent_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

simple_client_SOURCES = simple_client.cpp
simple_client_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

enum_if_SOURCES = enum_if.cpp
enum_if_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

AM_CXXFLAGS=-ftemplate-depth-50 -I$(top_srcdir)/include -I$(top_srcdir)/include/libtorrent @DEBUGFLAGS@ @PTHREAD_CFLAGS@ -DBOOST_MULTI_INDEX_DISABLE_SERIALIZATION

AM_LDFLAGS= ${LDLAGS} -L./ @BOOST_SYSTEM_LIB@ @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @PTHREAD_LIBS@ -DBOOST_MULTI_INDEX_DISABLE_SERIALIZATION @SSL_LDFLAGS@ @SSL_LIBS@
