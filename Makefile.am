SUBDIRS = include @ZLIBDIR@ src examples test
EXTRA_DIST = docs Jamfile project-root.jam \
m4/ac_cxx_namespaces.m4 m4/acx_pthread.m4 m4/ax_boost_date-time.m4 \
m4/ax_boost_filesystem.m4 m4/ax_boost_thread.m4 src/file_win.cpp

check: test
	test/test_hasher && test/test_bencoding && test/test_ip_filter && echo "tests done, all OK"
