lib_LTLIBRARIES = libtorrent-rasterbar.la

if ENABLE_DHT
KADEMLIA_SOURCES = \
  kademlia/closest_nodes.cpp    \
  kademlia/dht_tracker.cpp      \
  kademlia/find_data.cpp        \
  kademlia/node.cpp             \
  kademlia/node_id.cpp          \
  kademlia/refresh.cpp          \
  kademlia/routing_table.cpp    \
  kademlia/rpc_manager.cpp      \
  kademlia/traversal_algorithm.cpp
endif

if WITH_SHIPPED_GEOIP
GEOIP_SOURCES = GeoIP.c
endif

libtorrent_rasterbar_la_SOURCES = \
  alert.cpp                       \
  allocator.cpp                   \
  assert.cpp                      \
  broadcast_socket.cpp            \
  bt_peer_connection.cpp          \
  connection_queue.cpp            \
  ConvertUTF.cpp                  \
  create_torrent.cpp              \
  disk_buffer_holder.cpp          \
  disk_io_thread.cpp              \
  entry.cpp                       \
  enum_net.cpp                    \
  error_code.cpp                  \
  escape_string.cpp               \
  file.cpp                        \
  file_pool.cpp                   \
  file_storage.cpp                \
  gzip.cpp                        \
  http_connection.cpp             \
  http_parser.cpp                 \
  http_seed_connection.cpp        \
  http_stream.cpp                 \
  http_tracker_connection.cpp     \
  identify_client.cpp             \
  instantiate_connection.cpp      \
  ip_filter.cpp                   \
  lazy_bdecode.cpp                \
  logger.cpp                      \
  lsd.cpp                         \
  lt_trackers.cpp                 \
  magnet_uri.cpp                  \
  metadata_transfer.cpp           \
  natpmp.cpp                      \
  parse_url.cpp                   \
  pe_crypto.cpp                   \
  peer_connection.cpp             \
  piece_picker.cpp                \
  policy.cpp                      \
  session.cpp                     \
  session_impl.cpp                \
  sha1.cpp                        \
  smart_ban.cpp                   \
  socks5_stream.cpp               \
  stat.cpp                        \
  storage.cpp                     \
  torrent.cpp                     \
  torrent_handle.cpp              \
  torrent_info.cpp                \
  tracker_manager.cpp             \
  udp_socket.cpp                  \
  udp_tracker_connection.cpp      \
  upnp.cpp                        \
  ut_metadata.cpp                 \
  ut_pex.cpp                      \
  web_peer_connection.cpp         \
  \
  $(KADEMLIA_SOURCES)             \
  $(GEOIP_SOURCES)

#libtorrent_rasterbar_la_LDFLAGS = $(LDFLAGS) -version-info $(INTERFACE_VERSION_INFO)
libtorrent_rasterbar_la_LDFLAGS = -version-info $(INTERFACE_VERSION_INFO)

#libtorrent_rasterbar_la_LIBADD = @ZLIB@ @BOOST_SYSTEM_LIB@ @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @OPENSSL_LIBS@
libtorrent_rasterbar_la_LIBADD = @BOOST_SYSTEM_LIB@ @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @OPENSSL_LIBS@

#libtorrent_rasterbar_la_LIBADD = @ZLIB@

#AM_CXXFLAGS= -ftemplate-depth-100 -I$(top_srcdir)/include @ZLIBINCL@ @DEBUGFLAGS@ @OPENSSL_INCLUDES@
#AM_CPPFLAGS = -ftemplate-depth-100 -I$(top_srcdir)/include @ZLIBINCL@ @DEBUGFLAGS@ @OPENSSL_INCLUDES@
AM_CPPFLAGS = -ftemplate-depth-100 -I$(top_srcdir)/include @DEBUGFLAGS@ @OPENSSL_INCLUDES@

#AM_CFLAGS= -I$(top_srcdir)/include @ZLIBINCL@ @DEBUGFLAGS@
#AM_LDFLAGS = $(LDFLAGS) @BOOST_SYSTEM_LIB@ @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @OPENSSL_LDFLAGS@ @OPENSSL_LIBS@
AM_LDFLAGS = @OPENSSL_LDFLAGS@

if WITH_SHIPPED_ZLIB
libtorrent_rasterbar_la_LIBADD += $(top_builddir)/zlib/libzlib.la 
AM_CPPFLAGS += -I$(top_srcdir)/zlib
endif

