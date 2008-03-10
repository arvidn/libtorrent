=========================
libtorrent python binding
=========================

:Author: Arvid Norberg, arvid@rasterbar.com

.. contents:: Table of contents
	:depth: 2
	:backlinks: none

building
========

Building the libtorrent python bindings will produce a shared library (DLL)
which is a python module that can be imported in a python program.

The only supported build system for the bindings are currently boost build. To
set up your build environment, you need to add some settings to your
``$BOOST_BUILD_PATH/user-config.jam``.

Make sure your user config contains the following line::

	using python : 2.3 ;

Set the version to the version of python you have installed or want to use. If
you've installed python in a non-standard location, you have to add the prefix
path used when you installed python as a second option. Like this::

	using python : 2.3 : /usr ;

The bindings require *at least* python version 2.2.

For more information on how to install and set up boost-build, see the
`building libtorrent`_ section.

.. _`building libtorrent`: building.html#step-2-setup-bbv2

Once you have boost-build set up, you cd to the ``bindings/python``
directory and invoke ``bjam`` with the apropriate settings. For the available
build variants, see `libtorrent build options`_.

.. _`libtorrent build options`: building.html#step-3-building-libtorrent

For example::

	$ bjam dht-support=on boost=source release link=static

On Mac OS X, this will produce the following python module::

	bin/darwin-4.0/release/dht-support-on/link-static/logging-none/threading-multi/libtorrent.so

using
=====

The python interface is nearly identical to the C++ interface. Please refer to
the `main library reference`_.

.. _`main library reference`: manual.html

For an example python program, see ``client.py`` in the ``bindings/python``
directory.

A very simple example usage of the module would be something like this::

	import libtorrent as lt
	import time

	ses = lt.session()
	ses.listen_on(6881, 6891)

	e = lt.bdecode(open("test.torrent", 'rb').read())
	info = lt.torrent_info(e)

	h = ses.add_torrent(info, "./", storage_mode=storage_mode_sparse)

	while (not h.is_seed()):
		s = h.status()

		state_str = ['queued', 'checking', 'connecting', 'downloading metadata', \
			'downloading', 'finished', 'seeding', 'allocating']
		print '%.2f%% complete (down: %.1f kb/s up: %.1f kB/s peers: %d) %s' % \
			(s.progress * 100, s.download_rate / 1000, s.upload_rate / 1000, \
			s.num_peers, state_str[s.state])

		time.sleep(1)

