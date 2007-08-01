bin_PROGRAMS = @CLIENT_TEST_BIN@ dump_torrent make_torrent simple_client
EXTRA_PROGRAMS = client_test
EXTRA_DIST = Jamfile

client_test_SOURCES = client_test.cpp
client_test_LDADD = $(top_builddir)/src/libtorrent.la -l@BOOST_REGEX_LIB@ -l@BOOST_PROGRAM_OPTIONS_LIB@

dump_torrent_SOURCES = dump_torrent.cpp
dump_torrent_LDADD = $(top_builddir)/src/libtorrent.la

make_torrent_SOURCES = make_torrent.cpp
make_torrent_LDADD = $(top_builddir)/src/libtorrent.la

simple_client_SOURCES = simple_client.cpp
simple_client_LDADD = $(top_builddir)/src/libtorrent.la

AM_CXXFLAGS=-ftemplate-depth-50 -I$(top_srcdir)/include -I$(top_srcdir)/include/libtorrent @DEBUGFLAGS@ @PTHREAD_CFLAGS@ -DBOOST_MULTI_INDEX_DISABLE_SERIALIZATION
AM_LDFLAGS= ${LDLAGS} -L./ -l@BOOST_DATE_TIME_LIB@ -l@BOOST_FILESYSTEM_LIB@ -l@BOOST_THREAD_LIB@ @PTHREAD_LIBS@ -DBOOST_MULTI_INDEX_DISABLE_SERIALIZATION
