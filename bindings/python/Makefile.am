
EXTRA_DIST = \
  Jamfile                   \
  setup.py                  \
  client.py                 \
  rss_reader.py             \
  simple_client.py          \
  make_torrent.py           \
  src/alert.cpp             \
  src/bytes.hpp             \
  src/big_number.cpp        \
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
  src/boost_python.hpp      \
  src/session.cpp           \
  src/session_settings.cpp  \
  src/string.cpp            \
  src/torrent_handle.cpp    \
  src/torrent_info.cpp      \
  src/torrent_status.cpp    \
  src/utility.cpp           \
  src/version.cpp

if ENABLE_PYTHON_BINDING

all-local:
	$(PYTHON) $(srcdir)/setup.py build

install-exec-local:
	$(PYTHON) $(srcdir)/setup.py install @PYTHON_INSTALL_PARAMS@

uninstall-local:
	rm -rf $(DESTDIR)$(libdir)/python*/*-packages/*libtorrent*

clean-local:
	$(PYTHON) $(srcdir)/setup.py clean --all

endif
