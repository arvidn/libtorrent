bin_PROGRAMS = test_hasher test_bencoding test_ip_filter test_piece_picker test_storage test_metadata_extension test_buffer
EXTRA_DIST = Jamfile

test_hasher_SOURCES = main.cpp test_hasher.cpp
test_hasher_LDADD = $(top_builddir)/src/libtorrent.la

test_bencoding_SOURCES = main.cpp test_bencoding.cpp
test_bencoding_LDADD = $(top_builddir)/src/libtorrent.la

test_ip_filter_SOURCES = main.cpp test_ip_filter.cpp
test_ip_filter_LDADD = $(top_builddir)/src/libtorrent.la

test_piece_picker_SOURCES = main.cpp test_piece_picker.cpp
test_piece_picker_LDADD = $(top_builddir)/src/libtorrent.la

test_storage_SOURCES = main.cpp test_storage.cpp
test_storage_LDADD = $(top_builddir)/src/libtorrent.la

test_buffer_SOURCES = main.cpp test_buffer.cpp
test_buffer_LDADD = $(top_builddir)/src/libtorrent.la

test_metadata_extension_SOURCES = main.cpp setup_transfer.cpp test_metadata_extension.cpp
test_metadata_extension_LDADD = $(top_builddir)/src/libtorrent.la

noinst_HEADERS = test.hpp setup_transfer.hpp

AM_CXXFLAGS=-ftemplate-depth-50 -I$(top_srcdir)/include -I$(top_srcdir)/include/libtorrent @DEBUGFLAGS@ @PTHREAD_CFLAGS@
AM_LDFLAGS= -L./ -l@BOOST_DATE_TIME_LIB@ -l@BOOST_FILESYSTEM_LIB@ -l@BOOST_THREAD_LIB@ @PTHREAD_LIBS@ 
