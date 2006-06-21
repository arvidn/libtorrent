SUBDIRS = include @ZLIBDIR@ src examples test
EXTRA_DIST = docs/manual.html docs/manual.rst docs/extension_protocol.rst \
docs/extension_protocol.html docs/udp_tracker_protocol.rst \
docs/projects.rst docs/projects.html \
docs/udp_tracker_protocol.html docs/client_test.rst docs/client_test.html \
docs/unicode_support.png docs/client_test.png docs/style.css Jamfile project-root.jam \
m4/ac_cxx_namespaces.m4 m4/acx_pthread.m4 m4/ax_boost_date-time.m4 \
m4/ax_boost_filesystem.m4 m4/ax_boost_thread.m4 src/file_win.cpp libtorrent.pc

pkginclude_HEADER = \
debian/changelog \
debian/compat \
debian/control \
debian/copyright \
debian/files \
debian/libtorrent0-dev.dirs \
debian/libtorrent0-dev.docs \
debian/libtorrent0-dev.install \
debian/libtorrent0.dirs \
debian/libtorrent0.docs \
debian/libtorrent0.install \
debian/rules


pkgconfigdir   = $(libdir)/pkgconfig
pkgconfig_DATA = libtorrent.pc

check: test
	test/test_hasher \
	&& test/test_bencoding \
	&& test/test_ip_filter \
	&& test/test_piece_picker \
	&& test/test_storage \
	&& test/test_buffer \
	&& echo && echo && echo "  **** all tests passed ****" && echo && echo

deb:
	dpkg-buildpackage -rfakeroot -us -uc
