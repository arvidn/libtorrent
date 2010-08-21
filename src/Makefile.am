lib_LTLIBRARIES = libtorrent-rasterbar.la

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

if WITH_SHIPPED_GEOIP
GEOIP_SOURCES = GeoIP.c
endif

libtorrent_rasterbar_la_SOURCES = entry.cpp escape_string.cpp \
lazy_bdecode.cpp assert.cpp enum_net.cpp broadcast_socket.cpp \
peer_connection.cpp bt_peer_connection.cpp web_peer_connection.cpp \
natpmp.cpp piece_picker.cpp policy.cpp session.cpp session_impl.cpp sha1.cpp \
stat.cpp storage.cpp torrent.cpp torrent_handle.cpp pe_crypto.cpp \
torrent_info.cpp tracker_manager.cpp http_connection.cpp \
http_tracker_connection.cpp udp_tracker_connection.cpp \
alert.cpp identify_client.cpp ip_filter.cpp file.cpp metadata_transfer.cpp \
logger.cpp file_pool.cpp ut_pex.cpp lsd.cpp upnp.cpp instantiate_connection.cpp \
socks5_stream.cpp socks4_stream.cpp http_stream.cpp connection_queue.cpp \
disk_io_thread.cpp ut_metadata.cpp magnet_uri.cpp udp_socket.cpp smart_ban.cpp \
http_parser.cpp gzip.cpp disk_buffer_holder.cpp create_torrent.cpp \
parse_url.cpp file_storage.cpp error_code.cpp $(kademlia_sources) $(GEOIP_SOURCES)
# mapped_storage.cpp 

noinst_HEADERS = \
$(top_srcdir)/include/libtorrent/alert.hpp \
$(top_srcdir)/include/libtorrent/alert_types.hpp \
$(top_srcdir)/include/libtorrent/assert.hpp \
$(top_srcdir)/include/libtorrent/aux_/session_impl.hpp \
$(top_srcdir)/include/libtorrent/bandwidth_manager.hpp \
$(top_srcdir)/include/libtorrent/bandwidth_limit.hpp \
$(top_srcdir)/include/libtorrent/bandwidth_queue_entry.hpp \
$(top_srcdir)/include/libtorrent/bencode.hpp \
$(top_srcdir)/include/libtorrent/bitfield.hpp \
$(top_srcdir)/include/libtorrent/broadcast_socket.hpp \
$(top_srcdir)/include/libtorrent/buffer.hpp \
$(top_srcdir)/include/libtorrent/connection_queue.hpp \
$(top_srcdir)/include/libtorrent/create_torrent.hpp \
$(top_srcdir)/include/libtorrent/debug.hpp \
$(top_srcdir)/include/libtorrent/disk_io_thread.hpp \
$(top_srcdir)/include/libtorrent/entry.hpp \
$(top_srcdir)/include/libtorrent/enum_net.hpp \
$(top_srcdir)/include/libtorrent/escape_string.hpp \
$(top_srcdir)/include/libtorrent/error_code.hpp \
$(top_srcdir)/include/libtorrent/extensions.hpp \
$(top_srcdir)/include/libtorrent/extensions/metadata_transfer.hpp \
$(top_srcdir)/include/libtorrent/extensions/logger.hpp \
$(top_srcdir)/include/libtorrent/extensions/ut_pex.hpp \
$(top_srcdir)/include/libtorrent/file.hpp \
$(top_srcdir)/include/libtorrent/file_pool.hpp \
$(top_srcdir)/include/libtorrent/file_storage.hpp \
$(top_srcdir)/include/libtorrent/fingerprint.hpp \
$(top_srcdir)/include/libtorrent/gzip.hpp \
$(top_srcdir)/include/libtorrent/hasher.hpp \
$(top_srcdir)/include/libtorrent/http_connection.hpp \
$(top_srcdir)/include/libtorrent/http_stream.hpp \
$(top_srcdir)/include/libtorrent/http_parser.hpp \
$(top_srcdir)/include/libtorrent/session_settings.hpp \
$(top_srcdir)/include/libtorrent/http_tracker_connection.hpp \
$(top_srcdir)/include/libtorrent/identify_client.hpp \
$(top_srcdir)/include/libtorrent/instantiate_connection.hpp \
$(top_srcdir)/include/libtorrent/intrusive_ptr_base.hpp \
$(top_srcdir)/include/libtorrent/invariant_check.hpp \
$(top_srcdir)/include/libtorrent/io.hpp \
$(top_srcdir)/include/libtorrent/ip_filter.hpp \
$(top_srcdir)/include/libtorrent/chained_buffer.hpp \
$(top_srcdir)/include/libtorrent/lsd.hpp \
$(top_srcdir)/include/libtorrent/peer.hpp \
$(top_srcdir)/include/libtorrent/peer_connection.hpp \
$(top_srcdir)/include/libtorrent/bt_peer_connection.hpp \
$(top_srcdir)/include/libtorrent/web_peer_connection.hpp \
$(top_srcdir)/include/libtorrent/parse_url.hpp \
$(top_srcdir)/include/libtorrent/pe_crypto.hpp \
$(top_srcdir)/include/libtorrent/natpmp.hpp \
$(top_srcdir)/include/libtorrent/pch.hpp \
$(top_srcdir)/include/libtorrent/peer_id.hpp \
$(top_srcdir)/include/libtorrent/peer_info.hpp \
$(top_srcdir)/include/libtorrent/peer_request.hpp \
$(top_srcdir)/include/libtorrent/piece_block_progress.hpp \
$(top_srcdir)/include/libtorrent/piece_picker.hpp \
$(top_srcdir)/include/libtorrent/policy.hpp \
$(top_srcdir)/include/libtorrent/session.hpp \
$(top_srcdir)/include/libtorrent/size_type.hpp \
$(top_srcdir)/include/libtorrent/socket.hpp \
$(top_srcdir)/include/libtorrent/socket_type.hpp \
$(top_srcdir)/include/libtorrent/socks4_stream.hpp \
$(top_srcdir)/include/libtorrent/socks5_stream.hpp \
$(top_srcdir)/include/libtorrent/stat.hpp \
$(top_srcdir)/include/libtorrent/storage.hpp \
$(top_srcdir)/include/libtorrent/time.hpp \
$(top_srcdir)/include/libtorrent/torrent.hpp \
$(top_srcdir)/include/libtorrent/torrent_handle.hpp \
$(top_srcdir)/include/libtorrent/torrent_info.hpp \
$(top_srcdir)/include/libtorrent/tracker_manager.hpp \
$(top_srcdir)/include/libtorrent/udp_tracker_connection.hpp \
$(top_srcdir)/include/libtorrent/utf8.hpp \
$(top_srcdir)/include/libtorrent/xml_parse.hpp \
$(top_srcdir)/include/libtorrent/variant_stream.hpp \
$(top_srcdir)/include/libtorrent/version.hpp


libtorrent_rasterbar_la_LDFLAGS = $(LDFLAGS) -version-info $(INTERFACE_VERSION_INFO)
libtorrent_rasterbar_la_LIBADD = @ZLIB@ @BOOST_SYSTEM_LIB@ @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @PTHREAD_LIBS@ @SSL_LIBS@

AM_CXXFLAGS= -ftemplate-depth-100 -I$(top_srcdir)/include -I$(top_srcdir)/include/libtorrent @ZLIBINCL@ @DEBUGFLAGS@ @PTHREAD_CFLAGS@ -DBOOST_MULTI_INDEX_DISABLE_SERIALIZATION @SSL_CXXFLAGS@
AM_CFLAGS= -I$(top_srcdir)/include -I$(top_srcdir)/include/libtorrent @ZLIBINCL@ @DEBUGFLAGS@ @PTHREAD_CFLAGS@
AM_LDFLAGS= $(LDFLAGS) @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @PTHREAD_LIBS@ @SSL_LDFLAGS@ @SSL_LIBS@

