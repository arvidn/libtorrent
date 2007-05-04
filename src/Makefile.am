lib_LTLIBRARIES = libtorrent.la

if USE_DHT
kademlia_sources = kademlia/closest_nodes.cpp \
kademlia/dht_tracker.cpp \
kademlia/find_data.cpp \
kademlia/node.cpp \
kademlia/node_id.cpp \
kademlia/refresh.cpp \
kademlia/routing_table.cpp \
kademlia/rpc_manager.cpp \
kademlia/traversal_algorithm.cpp
endif

libtorrent_la_SOURCES = allocate_resources.cpp \
bandwidth_manager.cpp entry.cpp escape_string.cpp \
peer_connection.cpp bt_peer_connection.cpp web_peer_connection.cpp \
piece_picker.cpp policy.cpp session.cpp session_impl.cpp sha1.cpp stat.cpp \
storage.cpp torrent.cpp torrent_handle.cpp \
torrent_info.cpp tracker_manager.cpp \
http_tracker_connection.cpp udp_tracker_connection.cpp \
alert.cpp identify_client.cpp ip_filter.cpp file.cpp metadata_transfer.cpp \
logger.cpp file_pool.cpp ut_pex.cpp $(kademlia_sources)

noinst_HEADERS = \
$(top_srcdir)/include/libtorrent/alert.hpp \
$(top_srcdir)/include/libtorrent/alert_types.hpp \
$(top_srcdir)/include/libtorrent/allocate_resources.hpp \
$(top_srcdir)/include/libtorrent/aux_/allocate_resources_impl.hpp \
$(top_srcdir)/include/libtorrent/aux_/session_impl.hpp \
$(top_srcdir)/include/libtorrent/bandwidth_manager.hpp \
$(top_srcdir)/include/libtorrent/bencode.hpp \
$(top_srcdir)/include/libtorrent/buffer.hpp \
$(top_srcdir)/include/libtorrent/debug.hpp \
$(top_srcdir)/include/libtorrent/entry.hpp \
$(top_srcdir)/include/libtorrent/escape_string.hpp \
$(top_srcdir)/include/libtorrent/extensions.hpp \
$(top_srcdir)/include/libtorrent/extensions/metadata_transfer.hpp \
$(top_srcdir)/include/libtorrent/extensions/logger.hpp \
$(top_srcdir)/include/libtorrent/extensions/ut_pex.hpp \
$(top_srcdir)/include/libtorrent/file.hpp \
$(top_srcdir)/include/libtorrent/file_pool.hpp \
$(top_srcdir)/include/libtorrent/fingerprint.hpp \
$(top_srcdir)/include/libtorrent/hasher.hpp \
$(top_srcdir)/include/libtorrent/session_settings.hpp \
$(top_srcdir)/include/libtorrent/http_tracker_connection.hpp \
$(top_srcdir)/include/libtorrent/identify_client.hpp \
$(top_srcdir)/include/libtorrent/invariant_check.hpp \
$(top_srcdir)/include/libtorrent/io.hpp \
$(top_srcdir)/include/libtorrent/ip_filter.hpp \
$(top_srcdir)/include/libtorrent/peer.hpp \
$(top_srcdir)/include/libtorrent/peer_connection.hpp \
$(top_srcdir)/include/libtorrent/bt_peer_connection.hpp \
$(top_srcdir)/include/libtorrent/web_peer_connection.hpp \
$(top_srcdir)/include/libtorrent/peer_id.hpp \
$(top_srcdir)/include/libtorrent/peer_info.hpp \
$(top_srcdir)/include/libtorrent/peer_request.hpp \
$(top_srcdir)/include/libtorrent/piece_block_progress.hpp \
$(top_srcdir)/include/libtorrent/piece_picker.hpp \
$(top_srcdir)/include/libtorrent/policy.hpp \
$(top_srcdir)/include/libtorrent/resource_request.hpp \
$(top_srcdir)/include/libtorrent/session.hpp \
$(top_srcdir)/include/libtorrent/size_type.hpp \
$(top_srcdir)/include/libtorrent/socket.hpp \
$(top_srcdir)/include/libtorrent/stat.hpp \
$(top_srcdir)/include/libtorrent/storage.hpp \
$(top_srcdir)/include/libtorrent/torrent.hpp \
$(top_srcdir)/include/libtorrent/torrent_handle.hpp \
$(top_srcdir)/include/libtorrent/torrent_info.hpp \
$(top_srcdir)/include/libtorrent/tracker_manager.hpp \
$(top_srcdir)/include/libtorrent/udp_tracker_connection.hpp \
$(top_srcdir)/include/libtorrent/utf8.hpp \
$(top_srcdir)/include/libtorrent/version.hpp


libtorrent_la_LDFLAGS = $(LDFLAGS) -version-info 1:0:1
libtorrent_la_LIBADD = @ZLIB@ -l@BOOST_DATE_TIME_LIB@ -l@BOOST_FILESYSTEM_LIB@ -l@BOOST_THREAD_LIB@ @PTHREAD_LIBS@

AM_CXXFLAGS= -ftemplate-depth-50 -I$(top_srcdir)/include -I$(top_srcdir)/include/libtorrent @ZLIBINCL@ @DEBUGFLAGS@ @PTHREAD_CFLAGS@
AM_LDFLAGS= $(LDFLAGS) -l@BOOST_DATE_TIME_LIB@ -l@BOOST_FILESYSTEM_LIB@ -l@BOOST_THREAD_LIB@ @PTHREAD_LIBS@

