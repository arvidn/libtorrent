ACLOCAL_AMFLAGS = -I m4

#DISTCHECK_CONFIGURE_FLAGS = --enable-tests

SUBDIRS = include/libtorrent src examples test bindings tools

DOCS_IMAGES = \
  docs/arctic_thumb.png           \
  docs/bitbuddy_thumb.jpg         \
  docs/bitfox.png                 \
  docs/bitrocket_thumb.png        \
  docs/bitscast_thumb.png         \
  docs/bitslug_thumb.png          \
  docs/btg_thumb.jpg              \
  docs/bubba.png                  \
  docs/client_test.png            \
  docs/cwnd.png                   \
  docs/cwnd_thumb.png             \
  docs/delays.png                 \
  docs/delays_thumb.png           \
  docs/deluge.png                 \
  docs/disk_access.png            \
  docs/disk_buffer_before_optimization.png  \
  docs/disk_buffer.png            \
  docs/disk_buffer_sample.png     \
  docs/disk_io.png                \
  docs/electric_sheep_thumb.jpg   \
  docs/fatrat.png                 \
  docs/fdm.png                    \
  docs/firetorrent.png            \
  docs/flush.jpg                  \
  docs/halite_thumb.png           \
  docs/hacking.html               \
  docs/im_thumb.jpg               \
  docs/leechcraft.png             \
  docs/libtorrent_screen.png      \
  docs/limewire.png               \
  docs/lince.png                  \
  docs/Linkage.png                \
  docs/merkle_tree.graffle        \
  docs/merkle_tree.png            \
  docs/miro.jpg                   \
  docs/moopolice_thumb.gif        \
  docs/our_delay_base.png         \
  docs/our_delay_base_thumb.png   \
  docs/pump.png                   \
  docs/qbittorrent_thumb.jpg      \
  docs/read_disk_buffers.dot      \
  docs/read_disk_buffers.graffle  \
  docs/read_disk_buffers.png      \
  docs/session_stats_peers.png    \
  docs/storage.graffle            \
  docs/storage.png                \
  docs/style.css                  \
  docs/t2e.jpg                    \
  docs/tvblob.jpg                 \
  docs/tvitty.jpg                 \
  docs/unicode_support.png        \
  docs/write_disk_buffers.dot     \
  docs/write_disk_buffers.graffle \
  docs/write_disk_buffers.png     \
  docs/ziptorrent_thumb.gif       \
  docs/zyxel.png

DOCS_PAGES = \
  docs/building.html              \
  docs/building.rst               \
  docs/client_test.html           \
  docs/client_test.rst            \
  docs/contributing.html          \
  docs/contributing.rst           \
  docs/dht_extensions.html        \
  docs/dht_extensions.rst         \
  docs/dht_rss.html               \
  docs/dht_rss.rst                \
  docs/dht_sec.html               \
  docs/dht_sec.rst                \
  docs/examples.html              \
  docs/examples.rst               \
  docs/extension_protocol.html    \
  docs/extension_protocol.rst     \
  docs/features.html              \
  docs/features.rst               \
  docs/index.html                 \
  docs/index.rst                  \
  docs/libtorrent_plugins.html    \
  docs/libtorrent_plugins.rst     \
  docs/make_torrent.html          \
  docs/make_torrent.rst           \
  docs/manual.html                \
  docs/manual.rst                 \
  docs/projects.html              \
  docs/projects.rst               \
  docs/python_binding.html        \
  docs/python_binding.rst         \
  docs/dht_rss.html               \
  docs/dht_rss.rst                \
  docs/running_tests.html         \
  docs/running_tests.rst          \
  docs/tuning.html                \
  docs/tuning.rst                 \
  docs/udp_tracker_protocol.html  \
  docs/udp_tracker_protocol.rst   \
  docs/utp.html                   \
  docs/utp.rst

EXTRA_DIST = \
  Jamfile \
  Jamroot.jam \
  CMakeLists.txt \
  LICENSE \
  libtorrent-rasterbar.pc  \
  libtorrent-rasterbar-cmake.pc  \
  parse_dht_log.py \
  parse_session_stats.py \
  $(DOCS_PAGES) \
  $(DOCS_IMAGES)

pkgconfigdir   = $(libdir)/pkgconfig
pkgconfig_DATA = libtorrent-rasterbar.pc
