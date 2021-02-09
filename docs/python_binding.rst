=========================
libtorrent python binding
=========================

.. include:: header.rst

.. contents:: Table of contents
	:depth: 2
	:backlinks: none

building
========

libtorrent can be built as a python module.

The best way to build the python bindings is using ``setup.py``. This invokes
``b2`` under the hood, so you must have all of libtorrent's build dependencies
installed.

If you just want to build the shared library python extension without python
packaging semantics, you can also invoke ``b2`` directly.

prerequisites
=============

Whether building with ``setup.py`` or directly invoking ``b2``, you must
install the build prerequisites on your system:

1. All `the build prerequisites for the main libtorrent library`__, including
   boost libraries and ``b2``, and your building toolchain (``gcc``, visual
   studio, etc).
2. Boost.Python, if not otherwise included in your boost installation
3. Python 3.6+. Older versions may work, but are not tested.

.. __: building.html

environment variables
---------------------

``b2`` is very sensitive to environment variables. At least the following are
required:

1. ``BOOST_ROOT``
2. ``BOOST_BUILD_PATH``

``b2`` is also known to reference dozens of other environment variables when
detecting toolsets. Keep this in mind if you are building in an isolation
environment like ``tox``.

building with setup.py
======================

By default, ``setup.py`` will invoke ``b2`` to build libtorrent::

	python setup.py build

``setup.py`` is a normal ``distutils``-based setup script.

To install into your python environment::

	python setup.py install

To build a binary wheel package::

	python -m pip install wheel
	python setup.py bdist_wheel

build for a different python version
------------------------------------

``setup.py`` will target the running interpreter. To build for different python
versions, you must change how you invoke ``setup.py``::

	# build for python3.6
	python3.6 setup.py build
	# build for python3.7
	python3.7 setup.py build


customizing the build
---------------------

You can customize the build by passing options to the ``build_ext`` step of
``setup.py`` by passing arguments directly to ``b2`` via ``--b2-args=``::

	python setup.py build_ext --b2-args="toolset=msvc-14.2 linkflags=-L../../src/.libs"

For a full list of ``b2`` build options, see `libtorrent build features`_.

.. _`libtorrent build features`: building.html#build-features

Here, it's important to note that ``build_ext`` has no "memory" of the build
config and arguments you passed to it before. This is *different* from the way
``distutils`` normally works. Consider::

	python setup.py build_ext --b2-args="optimization=space"
	# the following will build with DEFAULT optimization
	python setup.py install

In order to customize the build *and* run other steps like installation, you
should run the steps inline with ``build_ext``::

	python setup.py build_ext --b2-args="optimization=space" install


building with b2
================

You will need to update your ``user-config.jam`` so ``b2`` can find your python
installation.

``b2`` has some auto-detection capabilities. You may be able to do just this::

	using python : 3.6 ;

However you may need to specify full paths. On windows, it make look like
this::

	using python : 3.6 : C:/Users/<UserName>/AppData/Local/Programs/Python/Python36 : C:/Users/<UserName>/AppData/Local/Programs/Python/Python36/include : C:/Users/<UserName>/AppData/Local/Programs/Python/Python36/libs ;

Or on Linux, like this::

	using python : 3.6 : /usr/bin/python3.6 : /usr/include/python3.6 : /usr/lib/python3.6 ;

Note that ``b2``'s python path detection is known to only work for global
python installations. It is known to be broken for virtualenvs or ``pyenv``. If
you are using ``pyenv`` to manage your python versions, you must specify full
include and library paths yourself.

invoking b2
-----------

Build the bindings like so::

	cd bindings/python
	b2 release python=3.6 address-model=64

Note that ``address-model`` should match the python installation you are
building for.

For other build features, see `libtorrent build options`_.

.. _`libtorrent build options`: building.html#build-features


static linking
--------------

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

	b2 release python=3.6 libtorrent-link=static boost-link=static

helper targets
--------------

There are some targets for placing the build artifact in a helpful location::

	$ b2 release python=3.6 stage_module stage_dependencies

This will produce a ``libtorrent`` python module in the current directory (file
name extension depends on operating system). The libraries the python module depends
on will be copied into ``./dependencies``.

To install the python module, build it with the following command::

	b2 release python=3.6 install_module

By default the module will be installed to the python user site. This can be
changed with the ``python-install-scope`` feature. The valid values are ``user``
(default) and ``system``. e.g.::

	b2 release python=3.6 install_module python-install-scope=system

To specify a custom installation path for the python module, specify the desired
path with the ``python-install-path`` feature. e.g.::

	b2 release python=3.6 install_module python-install-path=/home/foobar/python-site/

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

