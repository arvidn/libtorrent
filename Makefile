VERSION=1.3.0

NCORES=1
ifneq ($(OS),Windows_NT)
	UNAME=$(shell uname -s)
	ifeq ($(UNAME), Darwin)
		NCORES=$(shell sysctl -n hw.ncpu)
	endif
	ifeq ($(UNAME), Linux)
		NCORES=$(shell nproc)
	endif
endif

BUILD_CONFIG=release cxxstd=14 link=shared crypto=openssl warnings=off -j${NCORES}

ifeq (${PREFIX},)
PREFIX=/usr/local/
endif

ALL: FORCE
	BOOST_ROOT="" bjam ${BUILD_CONFIG}

python-binding: FORCE
	(cd bindings/python; BOOST_ROOT="" bjam ${BUILD_CONFIG} stage_module stage_dependencies)

examples: FORCE
	(cd examples; BOOST_ROOT="" bjam ${BUILD_CONFIG} stage_client_test stage_connection_tester)

tools: FORCE
	(cd tools; BOOST_ROOT="" bjam ${BUILD_CONFIG})

install: FORCE
	BOOST_ROOT="" bjam ${BUILD_CONFIG} install --prefix=${PREFIX}

sim: FORCE
	(cd simulation; BOOST_ROOT="" bjam $(filter-out crypto=openssl,${BUILD_CONFIG}) crypto=built-in)

check: FORCE
	(cd test; BOOST_ROOT="" bjam crypto=openssl warnings=off -j${NCORES})

clean: FORCE
	rm -rf \
    bin \
    examples/bin \
    tools/bin \
    bindings/python/bin \
    test/bin \
    simulation/bin \
    simulator/libsimulator/bin

DOCS_IMAGES = \
  docs/client_test.png            \
  docs/cwnd.png                   \
  docs/cwnd_thumb.png             \
  docs/delays.png                 \
  docs/delays_thumb.png           \
  docs/hacking.html               \
  docs/merkle_tree.png            \
  docs/our_delay_base.png         \
  docs/our_delay_base_thumb.png   \
  docs/read_disk_buffers.png      \
  docs/read_disk_buffers.diagram  \
  docs/storage.png                \
  docs/todo.html                  \
  docs/write_disk_buffers.png     \
  docs/write_disk_buffers.diagram \
  docs/ip_id_v4.png               \
  docs/ip_id_v6.png               \
  docs/hash_distribution.png      \
  docs/complete_bit_prefixes.png  \
  docs/troubleshooting.dot        \
  docs/troubleshooting.png        \
  docs/troubleshooting_thumb.png  \
  docs/hacking.diagram            \
  docs/hacking.png                \
  docs/disk_cache.diagram         \
  docs/disk_cache.png             \
  docs/utp_stack.diagram          \
  docs/utp_stack.png              \
  docs/bitcoin.png                \
  docs/style.css                  \
  docs/rst.css                    \
  docs/img/bg.png                 \
  docs/img/blue_bottom.png        \
  docs/img/blue_top.png           \
  docs/img/dotline.gif            \
  docs/img/minus.gif              \
  docs/img/orange.png             \
  docs/join_rst.py

DOCS_PAGES = \
  docs/building.html              \
  docs/client_test.html           \
  docs/contributing.html          \
  docs/dht_extensions.html        \
  docs/dht_rss.html               \
  docs/dht_sec.html               \
  docs/dht_store.html             \
  docs/examples.html              \
  docs/extension_protocol.html    \
  docs/features.html              \
  docs/index.html                 \
  docs/manual-ref.html            \
  docs/projects.html              \
  docs/python_binding.html        \
  docs/tuning.html                \
  docs/settings.rst               \
  docs/stats_counters.rst         \
  docs/troubleshooting.html       \
  docs/udp_tracker_protocol.html  \
  docs/utp.html                   \
  docs/streaming.html             \
  docs/building.rst               \
  docs/client_test.rst            \
  docs/contributing.rst           \
  docs/dht_extensions.rst         \
  docs/dht_rss.rst                \
  docs/dht_sec.rst                \
  docs/dht_store.rst              \
  docs/examples.rst               \
  docs/extension_protocol.rst     \
  docs/features.rst               \
  docs/index.rst                  \
  docs/manual-ref.rst             \
  docs/projects.rst               \
  docs/python_binding.rst         \
  docs/tuning.rst                 \
  docs/troubleshooting.rst        \
  docs/udp_tracker_protocol.rst   \
  docs/utp.rst                    \
  docs/streaming.rst              \
  docs/tutorial.rst               \
  docs/header.rst                 \
  docs/tutorial.html              \
  docs/upgrade_to_1.2-ref.html    \
  docs/reference-Alerts.html      \
  docs/reference-Bdecoding.html   \
  docs/reference-Bencoding.html   \
  docs/reference-Core.html        \
  docs/reference-Create_Torrents.html \
  docs/reference-ed25519.html     \
  docs/reference-Error_Codes.html \
  docs/reference-Filter.html      \
  docs/reference-Plugins.html     \
  docs/reference-Settings.html    \
  docs/reference-Storage.html     \
  docs/reference-Custom_Storage.html \
  docs/reference-Utility.html     \
  docs/reference.html             \
  docs/single-page-ref.html

ED25519_SOURCE = \
  fe.h \
  fixedint.h \
  ge.h \
  precomp_data.h \
  sc.h \
  add_scalar.cpp \
  fe.cpp \
  ge.cpp \
  key_exchange.cpp \
  keypair.cpp \
  sc.cpp \
  sign.cpp \
  verify.cpp \
  sha512.cpp \
  hasher512.cpp \

EXTRA_DIST = \
  Jamfile \
  Jamroot.jam \
  Makefile \
  CMakeLists.txt \
  cmake/Modules/FindLibGcrypt.cmake \
  cmake/Modules/GeneratePkgConfig.cmake \
  cmake/Modules/ucm_flags.cmake \
  cmake/Modules/GeneratePkgConfig/generate-pkg-config.cmake.in \
  cmake/Modules/GeneratePkgConfig/pkg-config.cmake.in \
  cmake/Modules/GeneratePkgConfig/target-compile-settings.cmake.in \
  LibtorrentRasterbarConfig.cmake.in \
  setup.py \
  LICENSE \
  COPYING \
  AUTHORS \
  NEWS \
  CONTRIBUTING.rst \
  README.rst \
  ChangeLog \
  $(DOCS_PAGES) \
  $(DOCS_IMAGES)

PYTHON_FILES= \
  CMakeLists.txt            \
  Jamfile                   \
  client.py                 \
  make_torrent.py           \
  setup.py                  \
  setup.py.cmake.in         \
  simple_client.py          \
  src/alert.cpp             \
  src/boost_python.hpp      \
  src/bytes.hpp             \
  src/converters.cpp        \
  src/create_torrent.cpp    \
  src/datetime.cpp          \
  src/entry.cpp             \
  src/error_code.cpp        \
  src/fingerprint.cpp       \
  src/gil.hpp               \
  src/ip_filter.cpp         \
  src/magnet_uri.cpp        \
  src/module.cpp            \
  src/optional.hpp          \
  src/peer_info.cpp         \
  src/session.cpp           \
  src/session_settings.cpp  \
  src/sha1_hash.cpp         \
  src/string.cpp            \
  src/torrent_handle.cpp    \
  src/torrent_info.cpp      \
  src/torrent_status.cpp    \
  src/utility.cpp           \
  src/version.cpp

EXAMPLE_FILES= \
  CMakeLists.txt \
  Jamfile \
  bt-get.cpp \
  bt-get2.cpp \
  client_test.cpp \
  cmake/FindLibtorrentRasterbar.cmake \
  connection_tester.cpp \
  dump_torrent.cpp \
  make_torrent.cpp \
  print.cpp \
  print.hpp \
  session_view.cpp \
  session_view.hpp \
  simple_client.cpp \
  custom_storage.cpp \
  stats_counters.cpp \
  torrent_view.cpp \
  torrent_view.hpp \
  upnp_test.cpp

TOOLS_FILES= \
  CMakeLists.txt         \
  Jamfile                \
  dht_put.cpp            \
  parse_dht_log.py       \
  parse_dht_rtt.py       \
  parse_dht_stats.py     \
  parse_peer_log.py      \
  parse_sample.py        \
  parse_session_stats.py \
  parse_utp_log.py       \
  session_log_alerts.cpp

KADEMLIA_SOURCES = \
  dht_settings.cpp     \
  dht_state.cpp        \
  dht_storage.cpp      \
  dht_tracker.cpp      \
  dos_blocker.cpp      \
  ed25519.cpp          \
  find_data.cpp        \
  get_item.cpp         \
  get_peers.cpp        \
  item.cpp             \
  msg.cpp              \
  node.cpp             \
  node_entry.cpp       \
  node_id.cpp          \
  put_data.cpp         \
  refresh.cpp          \
  routing_table.cpp    \
  rpc_manager.cpp      \
  sample_infohashes.cpp \
  traversal_algorithm.cpp

SOURCES = \
  ConvertUTF.cpp                  \
  add_torrent_params.cpp          \
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
  bt_peer_connection.cpp          \
  chained_buffer.cpp              \
  choker.cpp                      \
  close_reason.cpp                \
  cpuid.cpp                       \
  crc32c.cpp                      \
  create_torrent.cpp              \
  disabled_disk_io.cpp            \
  disk_buffer_holder.cpp          \
  disk_buffer_pool.cpp            \
  disk_interface.cpp              \
  disk_io_job.cpp                 \
  disk_io_thread.cpp              \
  disk_io_thread_pool.cpp         \
  disk_job_fence.cpp              \
  disk_job_pool.cpp               \
  entry.cpp                       \
  enum_net.cpp                    \
  error_code.cpp                  \
  escape_string.cpp               \
  ffs.cpp                         \
  file.cpp                        \
  file_progress.cpp               \
  file_storage.cpp                \
  file_view_pool.cpp              \
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
  mmap.cpp                        \
  natpmp.cpp                      \
  packet_buffer.cpp               \
  parse_url.cpp                   \
  part_file.cpp                   \
  path.cpp                        \
  pe_crypto.cpp                   \
  peer_class.cpp                  \
  peer_class_set.cpp              \
  peer_connection.cpp             \
  peer_connection_handle.cpp      \
  peer_info.cpp                   \
  peer_list.cpp                   \
  performance_counters.cpp        \
  piece_picker.cpp                \
  platform_util.cpp               \
  posix_disk_io.cpp               \
  posix_storage.cpp               \
  proxy_base.cpp                  \
  proxy_settings.cpp              \
  puff.cpp                        \
  random.cpp                      \
  read_resume_data.cpp            \
  receive_buffer.cpp              \
  request_blocks.cpp              \
  resolve_links.cpp               \
  resolver.cpp                    \
  session.cpp                     \
  session_call.cpp                \
  session_handle.cpp              \
  session_impl.cpp                \
  session_settings.cpp            \
  session_stats.cpp               \
  session_udp_sockets.cpp         \
  settings_pack.cpp               \
  sha1.cpp                        \
  sha1_hash.cpp                   \
  smart_ban.cpp                   \
  socket_io.cpp                   \
  socket_type.cpp                 \
  socks5_stream.cpp               \
  stack_allocator.cpp             \
  stat.cpp                        \
  stat_cache.cpp                  \
  storage.cpp                     \
  storage_utils.cpp               \
  string_util.cpp                 \
  time.cpp                        \
  timestamp_history.cpp           \
  torrent.cpp                     \
  torrent_handle.cpp              \
  torrent_info.cpp                \
  torrent_peer.cpp                \
  torrent_peer_allocator.cpp      \
  torrent_status.cpp              \
  tracker_manager.cpp             \
  udp_socket.cpp                  \
  udp_tracker_connection.cpp      \
  upnp.cpp                        \
  ut_metadata.cpp                 \
  ut_pex.cpp                      \
  utf8.cpp                        \
  utp_socket_manager.cpp          \
  utp_stream.cpp                  \
  version.cpp                     \
  web_connection_base.cpp         \
  web_peer_connection.cpp         \
  write_resume_data.cpp           \
  xml_parse.cpp

HEADERS = \
  ConvertUTF.h                 \
  add_torrent_params.hpp       \
  address.hpp                  \
  alert.hpp                    \
  alert_manager.hpp            \
  alert_types.hpp              \
  announce_entry.hpp           \
  assert.hpp                   \
  bandwidth_limit.hpp          \
  bandwidth_manager.hpp        \
  bandwidth_queue_entry.hpp    \
  bandwidth_socket.hpp         \
  bdecode.hpp                  \
  bencode.hpp                  \
  bitfield.hpp                 \
  bloom_filter.hpp             \
  broadcast_socket.hpp         \
  bt_peer_connection.hpp       \
  buffer.hpp                   \
  chained_buffer.hpp           \
  choker.hpp                   \
  close_reason.hpp             \
  config.hpp                   \
  copy_ptr.hpp                 \
  crc32c.hpp                   \
  create_torrent.hpp           \
  deadline_timer.hpp           \
  debug.hpp                    \
  disabled_disk_io.hpp         \
  disk_buffer_holder.hpp       \
  disk_buffer_pool.hpp         \
  disk_interface.hpp           \
  disk_io_job.hpp              \
  disk_io_thread.hpp           \
  disk_io_thread_pool.hpp      \
  disk_job_pool.hpp            \
  disk_observer.hpp            \
  download_priority.hpp        \
  entry.hpp                    \
  enum_net.hpp                 \
  error.hpp                    \
  error_code.hpp               \
  extensions.hpp               \
  file.hpp                     \
  file_storage.hpp             \
  fingerprint.hpp              \
  flags.hpp                    \
  fwd.hpp                      \
  gzip.hpp                     \
  hasher.hpp                   \
  heterogeneous_queue.hpp      \
  hex.hpp                      \
  http_connection.hpp          \
  http_parser.hpp              \
  http_seed_connection.hpp     \
  http_stream.hpp              \
  http_tracker_connection.hpp  \
  i2p_stream.hpp               \
  identify_client.hpp          \
  index_range.hpp              \
  invariant_check.hpp          \
  io.hpp                       \
  io_context.hpp               \
  io_service.hpp               \
  ip_filter.hpp                \
  ip_voter.hpp                 \
  lazy_entry.hpp               \
  link.hpp                     \
  lsd.hpp                      \
  magnet_uri.hpp               \
  natpmp.hpp                   \
  netlink.hpp                  \
  operations.hpp               \
  optional.hpp                 \
  packet_buffer.hpp            \
  packet_pool.hpp              \
  parse_url.hpp                \
  part_file.hpp                \
  pe_crypto.hpp                \
  peer.hpp                     \
  peer_class.hpp               \
  peer_class_set.hpp           \
  peer_class_type_filter.hpp   \
  peer_connection.hpp          \
  peer_connection_handle.hpp   \
  peer_connection_interface.hpp \
  peer_id.hpp                  \
  peer_info.hpp                \
  peer_list.hpp                \
  peer_request.hpp             \
  performance_counters.hpp     \
  pex_flags.hpp                \
  piece_block.hpp              \
  piece_block_progress.hpp     \
  piece_picker.hpp             \
  platform_util.hpp            \
  portmap.hpp                  \
  posix_disk_io.hpp            \
  proxy_base.hpp               \
  puff.hpp                     \
  random.hpp                   \
  read_resume_data.hpp         \
  receive_buffer.hpp           \
  request_blocks.hpp           \
  resolve_links.hpp            \
  resolver.hpp                 \
  resolver_interface.hpp       \
  session.hpp                  \
  session_handle.hpp           \
  session_settings.hpp         \
  session_stats.hpp            \
  session_status.hpp           \
  session_types.hpp            \
  settings_pack.hpp            \
  sha1.hpp                     \
  sha1_hash.hpp                \
  sliding_average.hpp          \
  socket.hpp                   \
  socket_io.hpp                \
  socks5_stream.hpp            \
  span.hpp                     \
  ssl_stream.hpp               \
  stack_allocator.hpp          \
  stat.hpp                     \
  stat_cache.hpp               \
  storage.hpp                  \
  storage_defs.hpp             \
  string_util.hpp              \
  string_view.hpp              \
  tailqueue.hpp                \
  time.hpp                     \
  timestamp_history.hpp        \
  torrent.hpp                  \
  torrent_flags.hpp            \
  torrent_handle.hpp           \
  torrent_info.hpp             \
  torrent_peer.hpp             \
  torrent_peer_allocator.hpp   \
  torrent_status.hpp           \
  tracker_manager.hpp          \
  udp_socket.hpp               \
  udp_tracker_connection.hpp   \
  union_endpoint.hpp           \
  units.hpp                    \
  upnp.hpp                     \
  utf8.hpp                     \
  utp_socket_manager.hpp       \
  utp_stream.hpp               \
  vector_utils.hpp             \
  version.hpp                  \
  web_connection_base.hpp      \
  web_peer_connection.hpp      \
  write_resume_data.hpp        \
  xml_parse.hpp                \
  \
  aux_/aligned_storage.hpp          \
  aux_/aligned_union.hpp            \
  aux_/alloca.hpp                   \
  aux_/allocating_handler.hpp       \
  aux_/array.hpp                    \
  aux_/bind_to_device.hpp           \
  aux_/byteswap.hpp                 \
  aux_/container_wrapper.hpp        \
  aux_/cppint_import_export.hpp     \
  aux_/cpuid.hpp                    \
  aux_/deferred_handler.hpp         \
  aux_/deque.hpp                    \
  aux_/dev_random.hpp               \
  aux_/disable_warnings_pop.hpp     \
  aux_/disable_warnings_push.hpp    \
  aux_/disk_job_fence.hpp           \
  aux_/ed25519.hpp                  \
  aux_/escape_string.hpp            \
  aux_/export.hpp                   \
  aux_/ffs.hpp                      \
  aux_/file_progress.hpp            \
  aux_/file_view_pool.hpp           \
  aux_/generate_peer_id.hpp         \
  aux_/has_block.hpp                \
  aux_/hasher512.hpp                \
  aux_/instantiate_connection.hpp   \
  aux_/io.hpp                       \
  aux_/ip_notifier.hpp              \
  aux_/listen_socket_handle.hpp     \
  aux_/lsd.hpp                      \
  aux_/merkle.hpp                   \
  aux_/mmap.hpp                     \
  aux_/noexcept_movable.hpp         \
  aux_/numeric_cast.hpp             \
  aux_/open_mode.hpp                \
  aux_/openssl.hpp                  \
  aux_/path.hpp                     \
  aux_/portmap.hpp                  \
  aux_/posix_storage.hpp            \
  aux_/proxy_settings.hpp           \
  aux_/range.hpp                    \
  aux_/route.h                      \
  aux_/scope_end.hpp                \
  aux_/session_call.hpp             \
  aux_/session_impl.hpp             \
  aux_/session_interface.hpp        \
  aux_/session_settings.hpp         \
  aux_/session_udp_sockets.hpp      \
  aux_/set_socket_buffer.hpp        \
  aux_/sha512.hpp                   \
  aux_/socket_type.hpp              \
  aux_/storage_utils.hpp            \
  aux_/store_buffer.hpp             \
  aux_/string_ptr.hpp               \
  aux_/strview_less.hpp             \
  aux_/suggest_piece.hpp            \
  aux_/throw.hpp                    \
  aux_/time.hpp                     \
  aux_/torrent_impl.hpp             \
  aux_/unique_ptr.hpp               \
  aux_/vector.hpp                   \
  aux_/win_crypto_provider.hpp      \
  aux_/win_util.hpp                 \
  \
  extensions/smart_ban.hpp          \
  extensions/ut_metadata.hpp        \
  extensions/ut_pex.hpp             \
  \
  kademlia/announce_flags.hpp       \
  kademlia/dht_observer.hpp         \
  kademlia/dht_settings.hpp         \
  kademlia/dht_state.hpp            \
  kademlia/dht_storage.hpp          \
  kademlia/dht_tracker.hpp          \
  kademlia/direct_request.hpp       \
  kademlia/dos_blocker.hpp          \
  kademlia/ed25519.hpp              \
  kademlia/find_data.hpp            \
  kademlia/get_item.hpp             \
  kademlia/get_peers.hpp            \
  kademlia/io.hpp                   \
  kademlia/item.hpp                 \
  kademlia/msg.hpp                  \
  kademlia/node.hpp                 \
  kademlia/node_entry.hpp           \
  kademlia/node_id.hpp              \
  kademlia/observer.hpp             \
  kademlia/put_data.hpp             \
  kademlia/refresh.hpp              \
  kademlia/routing_table.hpp        \
  kademlia/rpc_manager.hpp          \
  kademlia/sample_infohashes.hpp    \
  kademlia/traversal_algorithm.hpp  \
  kademlia/types.hpp

TRY_SIGNAL = \
  signal_error_code.cpp \
  signal_error_code.hpp \
  try_signal.cpp \
  try_signal.hpp \
  try_signal_mingw.hpp \
  try_signal_msvc.hpp \
  try_signal_posix.hpp \
  LICENSE \
  README.rst \
  Jamfile \
  CMakeLists.txt

SIM_SOURCES = \
  Jamfile \
  create_torrent.cpp \
  create_torrent.hpp \
  fake_peer.hpp \
  make_proxy_settings.hpp \
  setup_dht.cpp \
  setup_dht.hpp \
  setup_swarm.cpp \
  setup_swarm.hpp \
  test_auto_manage.cpp \
  test_checking.cpp \
  test_dht.cpp \
  test_dht_bootstrap.cpp \
  test_dht_rate_limit.cpp \
  test_dht_storage.cpp \
  test_error_handling.cpp \
  test_fast_extensions.cpp \
  test_http_connection.cpp \
  test_ip_filter.cpp \
  test_metadata_extension.cpp \
  test_optimistic_unchoke.cpp \
  test_pause.cpp \
  test_pe_crypto.cpp \
  test_save_resume.cpp \
  test_session.cpp \
  test_socks5.cpp \
  test_super_seeding.cpp \
  test_swarm.cpp \
  test_thread_pool.cpp \
  test_torrent_status.cpp \
  test_tracker.cpp \
  test_transfer.cpp \
  test_utp.cpp \
  test_web_seed.cpp \
  utils.cpp \
  utils.hpp

LIBSIM_SOURCES = \
  acceptor.cpp \
  default_config.cpp \
  high_resolution_clock.cpp \
  high_resolution_timer.cpp \
  http_proxy.cpp \
  http_server.cpp \
  io_service.cpp \
  pcap.cpp \
  queue.cpp \
  resolver.cpp \
  simulation.cpp \
  simulator.cpp \
  sink_forwarder.cpp \
  socks_server.cpp \
  tcp_socket.cpp \
  udp_socket.cpp

LIBSIM_HEADERS = \
  chrono.hpp \
  config.hpp \
  function.hpp \
  handler_allocator.hpp \
  http_proxy.hpp \
  http_server.hpp \
  noexcept_movable.hpp \
  packet.hpp \
  pcap.hpp \
  pop_warnings.hpp \
  push_warnings.hpp \
  queue.hpp \
  simulator.hpp \
  sink.hpp \
  sink_forwarder.hpp \
  socks_server.hpp \
  utils.hpp

LIBSIM_EXTRA = \
  CMakeLists.txt \
  Jamfile \
  Jamroot.jam \
  LICENSE \
  README.rst

LIBSIM_TESTS = \
  acceptor.cpp \
  main.cpp \
  multi_accept.cpp \
  multi_homed.cpp \
  null_buffers.cpp \
  parse_request.cpp \
  resolver.cpp \
  timer.cpp \
  udp_socket.cpp \
  catch.hpp

TEST_SOURCES = \
  enum_if.cpp \
  test_alert_manager.cpp \
  test_alert_types.cpp \
  test_alloca.cpp \
  test_auto_unchoke.cpp \
  test_bandwidth_limiter.cpp \
  test_bdecode.cpp \
  test_bencoding.cpp \
  test_bitfield.cpp \
  test_bloom_filter.cpp \
  test_buffer.cpp \
  test_checking.cpp \
  test_crc32.cpp \
  test_create_torrent.cpp \
  test_dht.cpp \
  test_dht_storage.cpp \
  test_direct_dht.cpp \
  test_dos_blocker.cpp \
  test_ed25519.cpp \
  test_enum_net.cpp \
  test_fast_extension.cpp \
  test_fence.cpp \
  test_ffs.cpp \
  test_file.cpp \
  test_file_progress.cpp \
  test_file_storage.cpp \
  test_flags.cpp \
  test_generate_peer_id.cpp \
  test_gzip.cpp \
  test_hasher.cpp \
  test_hasher512.cpp \
  test_heterogeneous_queue.cpp \
  test_http_connection.cpp \
  test_http_parser.cpp \
  test_identify_client.cpp \
  test_io.cpp \
  test_ip_filter.cpp \
  test_ip_voter.cpp \
  test_listen_socket.cpp \
  test_lsd.cpp \
  test_magnet.cpp \
  test_merkle.cpp \
  test_mmap.cpp \
  test_packet_buffer.cpp \
  test_part_file.cpp \
  test_pe_crypto.cpp \
  test_peer_classes.cpp \
  test_peer_list.cpp \
  test_peer_priority.cpp \
  test_pex.cpp \
  test_piece_picker.cpp \
  test_primitives.cpp \
  test_priority.cpp \
  test_privacy.cpp \
  test_read_piece.cpp \
  test_read_resume.cpp \
  test_receive_buffer.cpp \
  test_recheck.cpp \
  test_remap_files.cpp \
  test_remove_torrent.cpp \
  test_resolve_links.cpp \
  test_resume.cpp \
  test_session.cpp \
  test_session_params.cpp \
  test_settings_pack.cpp \
  test_sha1_hash.cpp \
  test_sliding_average.cpp \
  test_socket_io.cpp \
  test_span.cpp \
  test_ssl.cpp \
  test_stack_allocator.cpp \
  test_stat_cache.cpp \
  test_storage.cpp \
  test_string.cpp \
  test_tailqueue.cpp \
  test_threads.cpp \
  test_time.cpp \
  test_time_critical.cpp \
  test_timestamp_history.cpp \
  test_torrent.cpp \
  test_torrent_info.cpp \
  test_tracker.cpp \
  test_transfer.cpp \
  test_upnp.cpp \
  test_url_seed.cpp \
  test_utf8.cpp \
  test_utp.cpp \
  test_web_seed.cpp \
  test_web_seed_ban.cpp \
  test_web_seed_chunked.cpp \
  test_web_seed_http.cpp \
  test_web_seed_http_pw.cpp \
  test_web_seed_redirect.cpp \
  test_web_seed_socks4.cpp \
  test_web_seed_socks5.cpp \
  test_web_seed_socks5_no_peers.cpp \
  test_web_seed_socks5_pw.cpp \
  test_xml.cpp \
  \
  main.cpp \
  test.cpp \
  setup_transfer.cpp \
  dht_server.cpp \
  udp_tracker.cpp \
  peer_server.cpp \
  bittorrent_peer.cpp \
  make_torrent.cpp \
  web_seed_suite.cpp \
  swarm_suite.cpp \
  test_utils.cpp \
  settings.cpp \
  print_alerts.cpp \
  test.hpp \
  setup_transfer.hpp \
  dht_server.hpp \
  peer_server.hpp \
  udp_tracker.hpp \
  web_seed_suite.hpp \
  swarm_suite.hpp \
  test_utils.hpp \
  settings.hpp \
  make_torrent.hpp \
  bittorrent_peer.hpp \
  print_alerts.hpp

TEST_TORRENTS = \
  absolute_filename.torrent \
  backslash_path.torrent \
  bad_name.torrent \
  base.torrent \
  creation_date.torrent \
  duplicate_files.torrent \
  duplicate_web_seeds.torrent \
  empty_httpseed.torrent \
  empty_path.torrent \
  empty_path_multi.torrent \
  hidden_parent_path.torrent \
  httpseed.torrent \
  invalid_file_size.torrent \
  invalid_filename.torrent \
  invalid_filename2.torrent \
  invalid_info.torrent \
  invalid_merkle.torrent \
  invalid_name.torrent \
  invalid_name2.torrent \
  invalid_name3.torrent \
  invalid_path_list.torrent \
  invalid_piece_len.torrent \
  invalid_pieces.torrent \
  invalid_root_hash.torrent \
  invalid_root_hash2.torrent \
  invalid_symlink.torrent \
  large.torrent \
  long_name.torrent \
  many_pieces.torrent \
  missing_path_list.torrent \
  missing_piece_len.torrent \
  negative_file_size.torrent \
  negative_piece_len.torrent \
  negative_size.torrent \
  no_creation_date.torrent \
  no_files.torrent \
  no_name.torrent \
  pad_file.torrent \
  pad_file_no_path.torrent \
  parent_path.torrent \
  root_hash.torrent \
  sample.torrent \
  single_multi_file.torrent \
  slash_path.torrent \
  slash_path2.torrent \
  slash_path3.torrent \
  string.torrent \
  symlink1.torrent \
  symlink_zero_size.torrent \
  unaligned_pieces.torrent \
  unordered.torrent \
  url_list.torrent \
  url_list2.torrent \
  url_list3.torrent \
  url_seed.torrent \
  url_seed_multi.torrent \
  url_seed_multi_single_file.torrent \
  url_seed_multi_space.torrent \
  url_seed_multi_space_nolist.torrent \
  whitespace_url.torrent \
  v2.torrent \
  v2_invalid_file.torrent \
  v2_multipiece_file.torrent \
  v2_only.torrent \
  v2_mismatching_metadata.torrent \
  v2_invalid_filename.torrent \
  v2_no_piece_layers.torrent \
  v2_large_file.torrent \
  v2_piece_size.torrent \
  v2_no_power2_piece.torrent \
  v2_deep_recursion.torrent \
  v2_no_piece_layers.torrent \
  v2_large_offset.torrent

MUTABLE_TEST_TORRENTS = \
  test1.torrent \
  test1_pad_files.torrent \
  test1_single.torrent \
  test1_single_padded.torrent \
  test2.torrent \
  test2_pad_files.torrent \
  test3.torrent \
  test3_pad_files.torrent

TEST_EXTRA = Jamfile \
  Jamfile                    \
  CMakeLists.txt             \
  $(addprefix test_torrents/,${TEST_TORRENTS}) \
  $(addprefix mutable_test_torrents/,${MUTABLE_TEST_TORRENTS}) \
  root1.xml                          \
  root2.xml                          \
  root3.xml                          \
  ssl/regenerate_test_certificate.sh \
  ssl/cert_request.pem               \
  ssl/dhparams.pem                   \
  ssl/invalid_peer_certificate.pem   \
  ssl/invalid_peer_private_key.pem   \
  ssl/peer_certificate.pem           \
  ssl/peer_private_key.pem           \
  ssl/root_ca_cert.pem               \
  ssl/root_ca_private.pem            \
  ssl/server.pem                     \
  zeroes.gz \
  corrupt.gz \
  invalid1.gz \
  utf8_test.txt \
  web_server.py \
  socks.py \
  http.py

dist: FORCE
	(cd docs; make)
	rm -rf libtorrent-rasterbar-${VERSION} libtorrent-rasterbar-${VERSION}.tar.gz
	mkdir libtorrent-rasterbar-${VERSION}
	rsync -R ${EXTRA_DIST} \
    $(addprefix src/,${SOURCES}) \
    $(addprefix src/kademlia/,${KADEMLIA_SOURCES}) \
    $(addprefix include/libtorrent/,${HEADERS}) \
    $(addprefix examples/,${EXAMPLE_FILES}) \
    $(addprefix tools/,${TOOLS_FILES}) \
    $(addprefix bindings/python/,${PYTHON_FILES}) \
    $(addprefix test/,${TEST_SOURCES}) \
    $(addprefix test/,${TEST_EXTRA}) \
    $(addprefix simulation/,${SIM_SOURCES}) \
    $(addprefix deps/try_signal/,${TRY_SIGNAL}) \
    $(addprefix simulation/libsimulator/,${LIBSIM_EXTRA}) \
    $(addprefix simulation/libsimulator/test,${LIBSIM_TEST}) \
    $(addprefix simulation/libsimulator/include/simulator/,${LIBSIM_HEADERS}) \
    $(addprefix simulation/libsimulator/src/,${LIBSIM_SOURCES}) \
    $(addprefix src/ed25519/,$(ED25519_SOURCE)) \
    libtorrent-rasterbar-${VERSION}
	tar -czf libtorrent-rasterbar-${VERSION}.tar.gz libtorrent-rasterbar-${VERSION}

FORCE:

