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

Download `Boost libraries`__ Extract it to c:/Libraries/boost_1_73_0 and create these environmental vars:

.. __: http://www.boost.org/users/history/

1. BOOST_BUILD_PATH: "c:/Libraries/boost_1_73_0/tools/build/"
2. BOOST_ROOT: "c:/Libraries/boost_1_73_0/"

Navigate to ``BOOST_ROOT``, execute "bootstrap.bat" and add to the path "c:/Libraries/boost_1_73_0/"
	
Create a file ``user-config.jam`` in tour home directory and add this::

	using msvc : 14.0 : : <cxxflags>/std:c++11 ;
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
``user-config.jam`` (in your home directory).

Declare the version(s) of python you have installed or want to use. If
you've installed python in a non-standard location, you have to add the prefix
path used when you installed python as a second option. Like this::

	using python : 2.6 : /usr/bin/python2.6 : /usr/include/python2.6 : /usr/lib/python2.6 ;

The bindings require *at least* python version 2.2.

For more information on how to install and set up boost-build, see the
`building libtorrent`__ section.

.. __: building.html#step-2-setup-bbv2

Once you have boost-build set up, you cd to the ``bindings/python``
directory and invoke ``b2`` with the appropriate settings. For the available
build variants, see `libtorrent build options`_.

.. _`libtorrent build options`: building.html#step-3-building-libtorrent

For example::

	$ b2 stage_module stage_dependencies

This will produce a ``libtorrent`` python module in the current directory (file
name extension depends on operating system). The libraries the python module depends
on will be copied into ``./dependencies``.

python version
==============

If you have multiple versions of python installed, and configured in
``user-config.jam``, you can specify which version to build the module against
with the ``python`` feature.

e.g.::

	b2 python=3.9

static linking
==============

A python module is a shared library. Specifying ``link=static`` when building
the binding won't work, as it would try to produce a static library.

Instead, control whether the libtorrent main library or boost is linked
statically with ``libtorrent-link=static`` and ``boost-link=static``
respectively.

By default both are built and linked as shared libraries.

Building and linking boost as static library is only possibly by building it
from source. Specify the ``BOOST_ROOT`` environment variable to point to the
root directory of the boost source distribution.

For example, to build a self-contained python module::

	b2 libtorrent-link=static boost-link=static stage_module

installing python module
========================

To install the python module, build it with the following command::

	b2 install_module

By default the module will be installed to the python user site. This can be
changed with the ``python-install-scope`` feature. The valid values are ``user``
(default) and ``system``. e.g.::

	b2 install_module python-install-scope=system

The python interpreter and the python site used, depends on your python
configuration in ``user-config.jam`` and which version of python the module is
being built for.

To specify a custom installation path for the python module, specify the desired
path with the ``python-install-path`` feature. e.g.::

	b2 install_module python-install-path=/home/foobar/python-site/

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

Retrieving session statistics in Python is more convenient than that in C++. The
statistics are stored as an array in ``session_stats_alert``, which will be
posted after calling ``post_session_stats()`` in the ``session`` object. In
order to interpret the statistics array, in C++ it is required to call
``session_stats_metrics()`` to get the indices of these metrics, while in Python
it can be done using ``session_stats_alert.values["NAME_OF_METRIC"]``, where
``NAME_OF_METRIC`` is the name of a metric.

set_alert_notify
================

The ``set_alert_notify()`` function is not compatible with python. Since it
requires locking the GIL from within the libtorrent thread, to call the callback,
it can cause a deadlock with the main thread.

Instead, use the python-specific ``set_alert_fd()`` which takes a file descriptor
that will have 1 byte written to it to notify the client that there are new
alerts to be popped.

The file descriptor should be set to non-blocking mode. If writing to the
file/sending to the socket blocks, libtorrent's internal thread will stall.

This can be used with ``socket.socketpair()``, for example. The file descriptor
is what ``fileno()`` returns on a socket.

Example
=======

For an example python program, see ``client.py`` in the ``bindings/python``
directory.

A very simple example usage of the module would be something like this:

.. include:: ../bindings/python/simple_client.py
	:code: python
	:tab-width: 2
	:start-after: from __future__ import print_function

