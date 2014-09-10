example_programs =  \
  client_test       \
  dump_torrent      \
  make_torrent      \
  simple_client     \
  rss_reader        \
  upnp_test         \
  connection_tester

if ENABLE_EXAMPLES
bin_PROGRAMS = $(example_programs)
endif

EXTRA_PROGRAMS = $(example_programs)
EXTRA_DIST = Jamfile

client_test_SOURCES = client_test.cpp
#client_test_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

dump_torrent_SOURCES = dump_torrent.cpp
#dump_torrent_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

make_torrent_SOURCES = make_torrent.cpp
#make_torrent_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

simple_client_SOURCES = simple_client.cpp
#simple_client_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

connection_tester_SOURCES = connection_tester.cpp
#connection_tester_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

rss_reader_SOURCES = rss_reader.cpp
#rss_reader_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

upnp_test_SOURCES = upnp_test.cpp
#upnp_test_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

AM_CPPFLAGS = -ftemplate-depth-50 -I$(top_srcdir)/include @DEBUGFLAGS@

AM_LDFLAGS = @BOOST_SYSTEM_LIB@
#AM_LDFLAGS = $(LDFLAGS) @BOOST_SYSTEM_LIB@ @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @OPENSSL_LDFLAGS@ @OPENSSL_LIBS@
#AM_LDFLAGS = @OPENSSL_LDFLAGS@
