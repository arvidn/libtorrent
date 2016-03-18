ACLOCAL_AMFLAGS = -I m4

AM_DISTCHECK_CONFIGURE_FLAGS = --enable-tests=yes

SUBDIRS = include/libtorrent src examples test bindings tools

DOCS_IMAGES = \
  docs/client_test.png            \
  docs/cwnd.png                   \
  docs/cwnd_thumb.png             \
  docs/delays.png                 \
  docs/delays_thumb.png           \
  docs/disk_io.png                \
  docs/hacking.html               \
  docs/im_thumb.jpg               \
  docs/merkle_tree.png            \
  docs/our_delay_base.png         \
  docs/our_delay_base_thumb.png   \
  docs/read_disk_buffers.png      \
  docs/read_disk_buffers.diagram  \
  docs/session_stats_peers.png    \
  docs/storage.png                \
  docs/todo.html                  \
  docs/write_disk_buffers.png     \
  docs/write_disk_buffers.diagram \
  docs/ziptorrent_thumb.gif       \
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
  docs/style.css                  \
  docs/rst.css                    \
  docs/img/bg.png                 \
  docs/img/blue_bottom.png        \
  docs/img/blue_top.png           \
  docs/img/dotline.gif            \
  docs/img/minus.gif              \
  docs/img/orange.png

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
  docs/tutorial.html              \
  docs/reference-Alerts.html      \
  docs/reference-Bdecoding.html   \
  docs/reference-Bencoding.html   \
  docs/reference-Core.html        \
  docs/reference-Create_Torrents.html \
  docs/reference-Custom_Storage.html \
  docs/reference-ed25519.html     \
  docs/reference-Error_Codes.html \
  docs/reference-Filter.html      \
  docs/reference-Plugins.html     \
  docs/reference-Settings.html    \
  docs/reference-Storage.html     \
  docs/reference-Utility.html     \
  docs/reference.html

ED25519_SOURCE = \
  ed25519/readme.md \
  ed25519/test.c \
  ed25519/src/fe.h \
  ed25519/src/fixedint.h \
  ed25519/src/ge.h \
  ed25519/src/precomp_data.h \
  ed25519/src/sc.h \
  ed25519/src/sha512.h

EXTRA_DIST = \
  Jamfile \
  Jamroot.jam \
  CMakeLists.txt \
  setup.py \
  LICENSE \
  README.rst \
  $(DOCS_PAGES) \
  $(DOCS_IMAGES) \
  $(ED25519_SOURCE)

pkgconfigdir   = $(libdir)/pkgconfig
pkgconfig_DATA = libtorrent-rasterbar.pc

