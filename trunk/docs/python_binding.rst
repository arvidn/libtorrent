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

building using setup.py
-----------------------

To set up the Python bindings for libtorrent, you must first have libtorrent
built and installed on the system.  See 'building libtorrent'_.

.. _`building libtorrent`: building.html

To build the Python bindings do:

1. Run::

	python setup.py build

2. As root, run::

	python setup.py install

This requires that you have built and install libtorrent first. The setup.py relies
on pkg-config to be installed as well. It invokes pkg-config in order to link
against libtorrent.


building using boost build
--------------------------

To set up your build environment, you need to add some settings to your
``$BOOST_BUILD_PATH/user-config.jam``.

Make sure your user config contains the following line::

	using python : 2.3 ;

Set the version to the version of python you have installed or want to use. If
you've installed python in a non-standard location, you have to add the prefix
path used when you installed python as a second option. Like this::

	using python : 2.3 : /usr ;

The bindings require *at least* python version 2.2.

For more information on how to install and set up boost-build, see the
`building libtorrent`__ section.

.. __: building.html#step-2-setup-bbv2

Once you have boost-build set up, you cd to the ``bindings/python``
directory and invoke ``bjam`` with the apropriate settings. For the available
build variants, see `libtorrent build options`_.

.. _`libtorrent build options`: building.html#step-3-building-libtorrent

For example::

	$ bjam dht-support=on boost=source release link=static

On Mac OS X, this will produce the following python module::

	bin/darwin-4.0/release/dht-support-on/link-static/logging-none/threading-multi/libtorrent.so

using libtorrent in python
==========================

The python interface is nearly identical to the C++ interface. Please refer to
the `main library reference`_. The main differences are:

asio::tcp::endpoint
	The endpoint type is represented as a tuple of a string (as the address) and an int for
	the port number. E.g. ``('127.0.0.1', 6881)`` represents the localhost port 6881.

libtorrent::time_duration
	The time duration is represented as a number of seconds in a regular integer.

The following functions takes a reference to a container that is filled with
entries by the function. The python equivalent of these functions instead returns
a list of entries.

* torrent_handle::get_peer_info
* torrent_handle::file_progress
* torrent_handle::get_download_queue
* torrent_handle::piece_availability


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

		state_str = ['queued', 'checking', 'downloading metadata', \
			'downloading', 'finished', 'seeding', 'allocating']
		print '%.2f%% complete (down: %.1f kb/s up: %.1f kB/s peers: %d) %s' % \
			(s.progress * 100, s.download_rate / 1000, s.upload_rate / 1000, \
			s.num_peers, state_str[s.state])

		time.sleep(1)

