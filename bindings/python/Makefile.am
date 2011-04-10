
EXTRA_DIST = \
  Jamfile                   \
  setup.py                  \
  client.py                 \
  simple_client.py          \
  make_torrent.py           \
  src/alert.cpp             \
  src/big_number.cpp        \
  src/converters.cpp        \
  src/create_torrent.cpp    \
  src/datetime.cpp          \
  src/entry.cpp             \
  src/extensions.cpp        \
  src/filesystem.cpp        \
  src/fingerprint.cpp       \
  src/gil.hpp               \
  src/ip_filter.cpp         \
  src/magnet_uri.cpp        \
  src/module.cpp            \
  src/optional.hpp          \
  src/peer_info.cpp         \
  src/peer_plugin.cpp       \
  src/session.cpp           \
  src/session_settings.cpp  \
  src/torrent.cpp           \
  src/torrent_handle.cpp    \
  src/torrent_info.cpp      \
  src/torrent_status.cpp    \
  src/utility.cpp           \
  src/version.cpp

if ENABLE_PYTHON_BINDING

all-local:
	$(PYTHON) setup.py build

install-exec-local:
	$(PYTHON) setup.py install @PYTHON_INSTALL_PARAMS@

uninstall-local:
	rm -rf $(DESTDIR)$(libdir)/python*/*-packages/*libtorrent*

clean-local:
	$(PYTHON) setup.py clean --all

endif
