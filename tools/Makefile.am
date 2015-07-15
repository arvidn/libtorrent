tool_programs =  \
  fuzz_torrent

if ENABLE_EXAMPLES
bin_PROGRAMS = $(tool_programs)
endif

EXTRA_PROGRAMS = $(tool_programs)
EXTRA_DIST = Jamfile     \
  parse_bandwidth_log.py \
  parse_buffer_log.py    \
  parse_dht_log.py       \
  parse_dht_rtt.py       \
  parse_dht_stats.py     \
  parse_disk_access.py   \
  parse_disk_buffer_log.py\
  parse_disk_log.py      \
  parse_memory_log.py    \
  parse_peer_log.py      \
  parse_sample.py        \
  parse_session_stats.py \
  parse_test_results.py  \
  parse_utp_log.py       \
  parse_hash_fails.cpp   \
  run_regression_tests.py\
  run_tests.py

fuzz_torrent_SOURCES = fuzz_torrent.cpp

AM_CPPFLAGS = -ftemplate-depth-50 -I$(top_srcdir)/include @DEBUGFLAGS@

AM_LDFLAGS = $(top_builddir)/src/libtorrent-rasterbar.la \
	$(top_builddir)/src/libasio_src.a \
	@BOOST_SYSTEM_LIB@ @BOOST_CHRONO_LIB@ @BOOST_RANDOM_LIB@ \
	@PTHREAD_LIBS@ @OPENSSL_LDFLAGS@ @OPENSSL_LIBS@
