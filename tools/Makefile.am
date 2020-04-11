tool_programs =  \
  dht_put \
  session_log_alerts

if ENABLE_EXAMPLES
bin_PROGRAMS = $(tool_programs)
endif

EXTRA_PROGRAMS = $(tool_programs)
EXTRA_DIST = Jamfile     \
  parse_dht_log.py       \
  parse_dht_rtt.py       \
  parse_dht_stats.py     \
  parse_peer_log.py      \
  parse_sample.py        \
  parse_session_stats.py \
  parse_utp_log.py       \
  CMakeLists.txt

session_log_alerts_SOURCES = session_log_alerts.cpp
dht_put_SOURCES = dht_put.cpp

LDADD = $(top_builddir)/src/libtorrent-rasterbar.la

AM_CPPFLAGS = -ftemplate-depth-50 @DEBUGFLAGS@

AM_LDFLAGS = @BOOST_SYSTEM_LIB@
#AM_LDFLAGS = $(LDFLAGS) @BOOST_SYSTEM_LIB@ @OPENSSL_LDFLAGS@ @OPENSSL_LIBS@
#AM_LDFLAGS = @OPENSSL_LDFLAGS@
DEFAULT_INCLUDES = -I$(top_srcdir)/include
