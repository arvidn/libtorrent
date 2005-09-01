bin_PROGRAMS = client_test dump_torrent make_torrent simple_client
EXTRA_DIST = Jamfile

client_test_SOURCES = client_test.cpp
client_test_LDADD = $(top_builddir)/src/libtorrent.la

dump_torrent_SOURCES = dump_torrent.cpp
dump_torrent_LDADD = $(top_builddir)/src/libtorrent.la

make_torrent_SOURCES = make_torrent.cpp
make_torrent_LDADD = $(top_builddir)/src/libtorrent.la

simple_client_SOURCES = simple_client.cpp
simple_client_LDADD = $(top_builddir)/src/libtorrent.la

AM_CXXFLAGS=-ftemplate-depth-50 -I$(top_srcdir)/include @DEBUGFLAGS@ @PTHREAD_CFLAGS@
AM_LDFLAGS= ${LDLAGS} -L./ -l@BOOST_DATE_TIME_LIB@ -l@BOOST_FILESYSTEM_LIB@ -l@BOOST_THREAD_LIB@ @PTHREAD_LIBS@ 
