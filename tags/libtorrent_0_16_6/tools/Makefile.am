tool_programs =  \
  parse_hash_fails

if ENABLE_EXAMPLES
bin_PROGRAMS = $(tool_programs)
endif

EXTRA_PROGRAMS = $(tool_programs)
EXTRA_DIST = Jamfile

parse_hash_fails_SOURCES = parse_hash_fails.cpp
#client_test_LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

AM_CPPFLAGS = -ftemplate-depth-50 -I$(top_srcdir)/include @DEBUGFLAGS@

AM_LDFLAGS = @BOOST_SYSTEM_LIB@
#AM_LDFLAGS = $(LDFLAGS) @BOOST_SYSTEM_LIB@ @OPENSSL_LDFLAGS@ @OPENSSL_LIBS@
#AM_LDFLAGS = @OPENSSL_LDFLAGS@
