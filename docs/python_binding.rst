=========================
libtorrent python binding
=========================

.. include:: header.rst

.. contents:: Table of contents
	:depth: 2
	:backlinks: none

building
========

Building the libtorrent python bindings will produce a shared library (DLL)
which is a python module that can be imported in a python program.

building using boost build (windows)
------------------------------------

Download and install `Visual C++ 2015 Build Tools`__

.. __: http://landinghub.visualstudio.com/visual-cpp-build-tools

Download `Boost libraries`__ Extract it to c:/Libraries/boost_1_63_0 and create these environmental vars:

.. __: http://www.boost.org/users/history/

1. BOOST_BUILD_PATH: "c:/Libraries/boost_1_63_0/tools/build/"
2. BOOST_ROOT: "c:/Libraries/boost_1_63_0/"

Navigate to BOOST_ROOT, execute "bootstrap.bat" and add to the path "c:/Libraries/boost_1_63_0/tools/build/src/engine/bin.ntx86/"
	
Move the file ``user-config.jam`` from ``%BOOST_BUILD_PATH%/example/`` to ``%BOOST_BUILD_PATH%/user-config.jam`` and add this at the end:

::

	using msvc : 14.0 : : /std:c++11 ;
	using python : 3.5 : C:/Users/<UserName>/AppData/Local/Programs/Python/Python35 : C:/Users/<UserName>/AppData/Local/Programs/Python/Python35/include : C:/Users/<UserName>/AppData/Local/Programs/Python/Python35/libs ;

(change the python path for yours)

Navigate to bindings/python and execute::
	python setup.py build --bjam
	
Note: If you are using 64 bits python you should edit setup.py and add this to the b2 command:
``address-model=64``

This will create the file libtorrent.pyd inside build/lib/ that contains the binding.
	
building using boost build (others)
-----------------------------------
To set up your build environment, you need to add some settings to your
``$BOOST_BUILD_PATH/user-config.jam``.

A similar line to this line should be in the file (could be another python version)::

	#using python : 2.3 ;

Uncomment it and change it with the version of python you have installed or want to use. If
you've installed python in a non-standard location, you have to add the prefix
path used when you installed python as a second option. Like this::

	using python : 2.6 : /usr/bin/python2.6 : /usr/include/python2.6 : /usr/lib/python2.6 ;

The bindings require *at least* python version 2.2.

For more information on how to install and set up boost-build, see the
`building libtorrent`__ section.

.. __: building.html#step-2-setup-bbv2

Once you have boost-build set up, you cd to the ``bindings/python``
directory and invoke ``bjam`` with the appropriate settings. For the available
build variants, see `libtorrent build options`_.

.. _`libtorrent build options`: building.html#step-3-building-libtorrent

For example::

	$ bjam dht-support=on link=static

On Mac OS X, this will produce the following python module::

	bin/darwin-4.0/release/dht-support-on/link-static/logging-none/threading-multi/libtorrent.so

using libtorrent in python
==========================

The python interface is nearly identical to the C++ interface. Please refer to
the `library reference`_. The main differences are:

asio::tcp::endpoint
	The endpoint type is represented as a tuple of a string (as the address) and an int for
	the port number. E.g. ``("127.0.0.1", 6881)`` represents the localhost port 6881.

lt::time_duration
	The time duration is represented as a number of seconds in a regular integer.

The following functions takes a reference to a container that is filled with
entries by the function. The python equivalent of these functions instead returns
a list of entries.

* torrent_handle::get_peer_info
* torrent_handle::file_progress
* torrent_handle::get_download_queue
* torrent_handle::piece_availability

``create_torrent::add_node()`` takes two arguments, one string and one integer,
instead of a pair. The string is the address and the integer is the port.

``session::apply_settings()`` accepts a dictionary with keys matching the names
of settings in settings_pack.
When calling ``apply_settings``, the dictionary does not need to have every settings set,
keys that are not present are not updated.

To get a python dictionary of the settings, call ``session::get_settings``.

.. _`library reference`: reference.html

Retrieving session statistics in Python is more convenient than that in C++.
The statistics are stored as an array in ``session_stats_alert``, which will be posted after calling ``post_session_stats()`` in the ``session`` object.
In order to interpret the statistics array, in C++ it is required to call ``session_stats_metrics()`` to get the indices of these metrics, while in Python it can be done using ``session_stats_alert.values["NAME_OF_METRIC"]``, where ``NAME_OF_METRIC`` is the name of a metric.

For an example python program, see ``client.py`` in the ``bindings/python``
directory.

A very simple example usage of the module would be something like this:

.. include:: ../bindings/python/simple_client.py
	:code: python
	:tab-width: 2
	:start-after: from __future__ import print_function

