AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES = libtorrent-rasterbar.la

if ENABLE_DHT
KADEMLIA_SOURCES = \
  kademlia/dht_tracker.cpp      \
  kademlia/find_data.cpp        \
  kademlia/node.cpp             \
  kademlia/node_entry.cpp       \
  kademlia/node_id.cpp          \
  kademlia/refresh.cpp          \
  kademlia/routing_table.cpp    \
  kademlia/rpc_manager.cpp      \
  kademlia/logging.cpp          \
  kademlia/traversal_algorithm.cpp \
  kademlia/dos_blocker.cpp      \
  kademlia/get_peers.cpp        \
  kademlia/get_item.cpp         \
  kademlia/item.cpp             \
  ../ed25519/src/add_scalar.cpp \
  ../ed25519/src/fe.cpp         \
  ../ed25519/src/ge.cpp         \
  ../ed25519/src/key_exchange.cpp \
  ../ed25519/src/keypair.cpp    \
  ../ed25519/src/sc.cpp         \
  ../ed25519/src/seed.cpp       \
  ../ed25519/src/sha512.cpp     \
  ../ed25519/src/sign.cpp       \
  ../ed25519/src/verify.cpp
endif

if WITH_OPENSSL
ASIO_OPENSSL_SOURCES = asio_ssl.cpp
endif

libtorrent_rasterbar_la_SOURCES = \
  web_connection_base.cpp         \
  alert.cpp                       \
  alert_manager.cpp               \
  allocator.cpp                   \
  asio.cpp                        \
  assert.cpp                      \
  bandwidth_limit.cpp             \
  bandwidth_manager.cpp           \
  bandwidth_queue_entry.cpp       \
  bdecode.cpp                     \
  bloom_filter.cpp                \
  broadcast_socket.cpp            \
  block_cache.cpp                 \
  bt_peer_connection.cpp          \
  chained_buffer.cpp              \
  choker.cpp                      \
  close_reason.cpp                \
  ConvertUTF.cpp                  \
  crc32c.cpp                      \
  create_torrent.cpp              \
  disk_buffer_holder.cpp          \
  disk_buffer_pool.cpp            \
  disk_io_job.cpp                 \
  disk_io_thread.cpp              \
  disk_job_pool.cpp               \
  entry.cpp                       \
  enum_net.cpp                    \
  error_code.cpp                  \
  escape_string.cpp               \
  file.cpp                        \
  file_pool.cpp                   \
  file_storage.cpp                \
  gzip.cpp                        \
  hasher.cpp                      \
  http_connection.cpp             \
  http_parser.cpp                 \
  http_seed_connection.cpp        \
  http_stream.cpp                 \
  http_tracker_connection.cpp     \
  i2p_stream.cpp                  \
  identify_client.cpp             \
  instantiate_connection.cpp      \
  ip_filter.cpp                   \
  ip_voter.cpp                    \
  lazy_bdecode.cpp                \
  lsd.cpp                         \
  lt_trackers.cpp                 \
  magnet_uri.cpp                  \
  metadata_transfer.cpp           \
  mpi.c                           \
  natpmp.cpp                      \
  parse_url.cpp                   \
  part_file.cpp                   \
  pe_crypto.cpp                   \
  performance_counters.cpp        \
  peer_connection.cpp             \
  peer_class.cpp                  \
  peer_class_set.cpp              \
  piece_picker.cpp                \
  platform_util.cpp               \
  packet_buffer.cpp               \
  proxy_base.cpp                  \
  peer_list.cpp                   \
  puff.cpp                        \
  random.cpp                      \
  receive_buffer.cpp              \
  request_blocks.cpp              \
  resolve_links.cpp               \
  resolver.cpp                    \
  rss.cpp                         \
  session.cpp                     \
  session_call.cpp                \
  session_impl.cpp                \
  session_settings.cpp            \
  settings_pack.cpp               \
  sha1.cpp                        \
  smart_ban.cpp                   \
  socket_io.cpp                   \
  socket_type.cpp                 \
  socks5_stream.cpp               \
  stat.cpp                        \
  stat_cache.cpp                  \
  storage.cpp                     \
  session_stats.cpp               \
  string_util.cpp                 \
  tailqueue.cpp                   \
  thread.cpp                      \
  torrent.cpp                     \
  torrent_handle.cpp              \
  torrent_info.cpp                \
  torrent_peer.cpp                \
  torrent_peer_allocator.cpp      \
  time.cpp                        \
  timestamp_history.cpp           \
  tracker_manager.cpp             \
  udp_socket.cpp                  \
  udp_tracker_connection.cpp      \
  upnp.cpp                        \
  ut_metadata.cpp                 \
  ut_pex.cpp                      \
  utf8.cpp                        \
  utp_socket_manager.cpp          \
  utp_stream.cpp                  \
  web_peer_connection.cpp         \
  xml_parse.cpp                   \
  \
  $(KADEMLIA_SOURCES)             \
  $(ASIO_OPENSSL_SOURCES)

#libtorrent_rasterbar_la_LDFLAGS = $(LDFLAGS) -version-info $(INTERFACE_VERSION_INFO)
libtorrent_rasterbar_la_LDFLAGS = -version-info $(INTERFACE_VERSION_INFO)

#libtorrent_rasterbar_la_LIBADD =  @BOOST_SYSTEM_LIB@ @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @OPENSSL_LIBS@
libtorrent_rasterbar_la_LIBADD = @BOOST_SYSTEM_LIB@ @OPENSSL_LIBS@

#AM_CXXFLAGS= -ftemplate-depth-100 -I$(top_srcdir)/include @DEBUGFLAGS@ @OPENSSL_INCLUDES@
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ed25519/src @DEBUGFLAGS@ @OPENSSL_INCLUDES@
AM_CFLAGS = -I$(top_srcdir)/ed25519/src -std=c99

#AM_CFLAGS= -I$(top_srcdir)/include @DEBUGFLAGS@
#AM_LDFLAGS = $(LDFLAGS) @BOOST_SYSTEM_LIB@ @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @OPENSSL_LDFLAGS@ @OPENSSL_LIBS@
AM_LDFLAGS = @OPENSSL_LDFLAGS@

