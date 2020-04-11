AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES = libtorrent-rasterbar.la

if ENABLE_DHT
KADEMLIA_SOURCES = \
  kademlia/dht_state.cpp        \
  kademlia/dht_storage.cpp      \
  kademlia/dht_tracker.cpp      \
  kademlia/find_data.cpp        \
  kademlia/put_data.cpp         \
  kademlia/msg.cpp              \
  kademlia/node.cpp             \
  kademlia/node_entry.cpp       \
  kademlia/node_id.cpp          \
  kademlia/refresh.cpp          \
  kademlia/routing_table.cpp    \
  kademlia/rpc_manager.cpp      \
  kademlia/traversal_algorithm.cpp \
  kademlia/dos_blocker.cpp      \
  kademlia/get_peers.cpp        \
  kademlia/get_item.cpp         \
  kademlia/item.cpp             \
  kademlia/ed25519.cpp          \
  kademlia/sample_infohashes.cpp \
  kademlia/dht_settings.cpp     \
  ../ed25519/src/add_scalar.cpp \
  ../ed25519/src/fe.cpp         \
  ../ed25519/src/ge.cpp         \
  ../ed25519/src/key_exchange.cpp \
  ../ed25519/src/keypair.cpp    \
  ../ed25519/src/sc.cpp         \
  ../ed25519/src/sign.cpp       \
  ../ed25519/src/verify.cpp     \
  hasher512.cpp
endif

libtorrent_rasterbar_la_SOURCES = \
  web_connection_base.cpp         \
  alert.cpp                       \
  alert_manager.cpp               \
  announce_entry.cpp              \
  assert.cpp                      \
  bandwidth_limit.cpp             \
  bandwidth_manager.cpp           \
  bandwidth_queue_entry.cpp       \
  bdecode.cpp                     \
  bitfield.cpp                    \
  bloom_filter.cpp                \
  broadcast_socket.cpp            \
  block_cache.cpp                 \
  bt_peer_connection.cpp          \
  chained_buffer.cpp              \
  choker.cpp                      \
  close_reason.cpp                \
  ConvertUTF.cpp                  \
  cpuid.cpp                       \
  crc32c.cpp                      \
  create_torrent.cpp              \
  disk_buffer_holder.cpp          \
  disk_buffer_pool.cpp            \
  disk_io_job.cpp                 \
  disk_io_thread.cpp              \
  disk_io_thread_pool.cpp         \
  disk_job_fence.cpp              \
  disk_job_pool.cpp               \
  entry.cpp                       \
  enum_net.cpp                    \
  error_code.cpp                  \
  escape_string.cpp               \
  file.cpp                        \
  path.cpp                        \
  file_pool.cpp                   \
  file_storage.cpp                \
  fingerprint.cpp                 \
  generate_peer_id.cpp            \
  gzip.cpp                        \
  hasher.cpp                      \
  hex.cpp                         \
  http_connection.cpp             \
  http_parser.cpp                 \
  http_seed_connection.cpp        \
  http_stream.cpp                 \
  http_tracker_connection.cpp     \
  i2p_stream.cpp                  \
  identify_client.cpp             \
  instantiate_connection.cpp      \
  ip_filter.cpp                   \
  ip_notifier.cpp                 \
  ip_voter.cpp                    \
  lazy_bdecode.cpp                \
  listen_socket_handle.cpp        \
  lsd.cpp                         \
  magnet_uri.cpp                  \
  merkle.cpp                      \
  natpmp.cpp                      \
  openssl.cpp                     \
  parse_url.cpp                   \
  part_file.cpp                   \
  pe_crypto.cpp                   \
  performance_counters.cpp        \
  peer_connection.cpp             \
  peer_connection_handle.cpp      \
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
  read_resume_data.cpp            \
  write_resume_data.cpp           \
  request_blocks.cpp              \
  resolve_links.cpp               \
  resolver.cpp                    \
  session.cpp                     \
  session_call.cpp                \
  session_handle.cpp              \
  session_impl.cpp                \
  session_settings.cpp            \
  proxy_settings.cpp              \
  settings_pack.cpp               \
  sha1_hash.cpp                   \
  smart_ban.cpp                   \
  socket_io.cpp                   \
  socket_type.cpp                 \
  socks5_stream.cpp               \
  stat.cpp                        \
  stat_cache.cpp                  \
  storage.cpp                     \
  storage_piece_set.cpp           \
  storage_utils.cpp               \
  session_stats.cpp               \
  string_util.cpp                 \
  torrent.cpp                     \
  torrent_handle.cpp              \
  torrent_info.cpp                \
  torrent_peer.cpp                \
  torrent_peer_allocator.cpp      \
  torrent_status.cpp              \
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
  version.cpp                     \
  file_progress.cpp               \
  ffs.cpp                         \
  add_torrent_params.cpp          \
  peer_info.cpp                   \
  stack_allocator.cpp             \
  sha1.cpp                        \
  sha512.cpp                      \
  \
  $(KADEMLIA_SOURCES)

AM_CPPFLAGS = -DTORRENT_BUILDING_LIBRARY @DEBUGFLAGS@
AM_LDFLAGS = @OPENSSL_LDFLAGS@
DEFAULT_INCLUDES = -I$(top_srcdir)/include @OPENSSL_INCLUDES@

libtorrent_rasterbar_la_LDFLAGS = -version-info $(INTERFACE_VERSION_INFO)
libtorrent_rasterbar_la_LIBADD = @OPENSSL_LIBS@
libtorrent_rasterbar_la_CPPFLAGS = $(AM_CPPFLAGS)

if HAVE_ANDROID
libtorrent_rasterbar_la_LIBADD += -ldl
endif

if HAVE_WINDOWS
libtorrent_rasterbar_la_LIBADD += -liphlpapi -lws2_32 -lwsock32
libtorrent_rasterbar_la_CPPFLAGS += -DWIN32_LEAN_AND_MEAN -D__USE_W32_SOCKETS -DWIN32 -D_WIN32
endif
