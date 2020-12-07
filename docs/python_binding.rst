==============================
Python bindings for libtorrent
==============================

.. include:: header.rst

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

Installation
============

Requirements
------------

The bindings require *at least* Python 2.2. However, only Python 3.5 and newer
versions are officially supported and tested, so it is not recommended to use
older ones.

The bindings require CPython. Alternative implementations such as PyPy do not
work without modifications to bindings and are not officially supported.

Installation from PyPI
----------------------

The bindings provide pre-built Python wheels for Windows, macOS and manylinux2014
for Python 3.5-3.8. They contain all libraries that are needed, including Boost,
so it is not necessary to install them manually. If your platform supports wheels,
it is heavily recommended to use them because they make installation a lot easier
and faster.

You can install wheels from PyPI using pip::

    pip install libtorrent

This will download and install pre-built libtorrent bindings that you can
normally import and use, just like any other Python package.

*Note:* Make sure the path to pip is correct. You may need to use ``pip3``,
``python -m pip`` or similar commands instead, depending on your system
configuration. The rest of this document will assume you know the path/name of pip
and Python for your system.

Installation from development wheels
------------------------------------


You can also install pre-built wheels for the latest development changes from
GitHub Actions. Note that these wheels can be unstable and are not officially released.

You can find the latest Python builds on `GitHub Actions website
<https://github.com/arvidn/libtorrent/actions?query=workflow%3A%22Python+bindings%22>`_.
You can choose desired build and download ``wheels`` artifact, which is a ZIP containing all
wheels for supported platforms. You can then extract the desired wheel and install it using ``pip``.


Installation from source
------------------------

If you use unsupported platform or Python version, installation from PyPI will
fail. In this case, you will have to manually download libtorrent and built it.
You can either build just a shared library (DLL) and manually move it to Python
include path, a wheel which you can re-distribute to other systems with the same
platform and Python version and install it there, or directly install the
package.

See below for more details about the manual building. The document will assume you
have already downloaded libtorrent, either via repository cloning or from
the provided archive. Make sure repository/archive contain all code and
dependencies that you need, including Git submodules.

Building
========

Preparation
-----------

Step 1: Download toolset
~~~~~~~~~~~~~~~~~~~~~~~~

You will need to have C++ toolset installed and configured on your system.
Toolset needs to support at least C++17 and its binaries need to be accessible
in the path.

For Windows, you will need to download and install `Microsoft Visual C++ Build
Tools <https://visualstudio.microsoft.com/visual-cpp-build-tools/>`_. For Linux
and macOS, you will need to install G++ or Clang respectively.

Step 2: Download Boost
~~~~~~~~~~~~~~~~~~~~~~

*Note:* In case if you already have Boost installed and built on your system,
either manually or via a package manager, you can skip these steps. However,
you need to make sure Boost was built for correct Python version, Boost Build
(``b2.exe`` or ``b2``) executable is in the path, and that the Boost headers and
libraries are in your toolset's include and library path.

You will also need to download `Boost <https://www.boost.org/users/download/>`_.
For detailed information on how to install and set up Boost, see the
`building libtorrent <building.html#step-2-setup-bbv2>`_ section of the documentation.

In short, you will have to create these environment variables:

* BOOST_BUILD_PATH: "/path/to/downloaded/boost/tools/build/"
* BOOST_ROOT: "/path/to/downloaded/boost/"

Then you need to navigate to ``BOOST_ROOT``, execute ``bootstrap.bat`` (Windows)
or ``bootstrap.sh`` (Linux and macOS), and add that directory to path.

Step 3: Configure Boost for Python
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When building bindings using ``setup.py`` script or ``pip``, Boost will by
default use Python executable that was used to run ``setup.py`` or ``pip``, and
guess Python include and library path.

Building and creating wheel
---------------------------

You can create wheel by running ``setup.py``, either from repository/archive
root or from ``bindings/python`` subdirectory::

    python setup.py bdist_wheel

Note that this will leave some auto-generated files in your repository/archive.
If you want to keep it clean, you can build wheel by running ``pip`` from the
repository root::

    pip wheel .

This will copy the whole repository/archive to a temporary directory, build the wheel
there and copy the wheel back. However, copying all files might take some time,
so the build will last longer. Any Boost cache that you make will also be
discarded on next run.

The produced wheel will contain a shared library that can be, once the wheel is
installed, imported in Python programs. A wheel can also be re-distributed to
other systems with the same platform and Python version.

libtorrent will be by default linked statically, which means you won't have to
install it separately. In case you do want to link libtorrent as a shared
library, you can provide ``--libtorrent-link=shared`` argument to ``build_ext`` when
building wheel. In this case, libtorrent will have to be in library path on
every system where you want to import package.

Unless you build the wheel on Windows, Boost libraries will by default be linked
as shared libraries. This means they will need to be installed on every system
where you want to use package. You can provide ``--boost-link=static`` argument
to ``build_ext`` to force linking Boost statically on all systems. Building and
linking Boost as static library is only possibly by building it from source and
requires at least Boost 1.74.0. To do this, specify the ``BOOST_ROOT`` environment
variable to point to the root directory of the Boost source distribution before
running the build.

Alternatively, Python tools like auditwheel and delocate can be used to detect
shared libraries and statically include them into the wheel.

The build script will auto-detect toolset for the current system. You can also
specify `--toolset` argument to force a different toolset.

Building and installing package
-------------------------------

You can create wheel by running ``setup.py``, either from repository/archive
root or from ``bindings/python`` subdirectory::

    python setup.py install

Note that this will leave some auto-generated files in your repository/archive.
If you want to keep it clean, you can build wheel by running ``pip`` from
repository root::

    pip install .

This will copy whole repository/archive to a temporary directory, build the wheel
there and install it. However, copying all files might take some time, so the
build will last longer. Any Boost cache that you make will also be discarded on
next run.

The package will be installed along with other installed Python packages, either
globally or into a virtual environment. It can be imported and used just like any
other Python package.

The same linking and toolset configuration principles apply as when just creating the wheel. See the above
section for more details.

Building directly using Boost Build
-----------------------------------

*Note:* It is very unlikely that you will need to use this option unless you
want to do advanced configurations to bindings or libtorrent itself. You should
prefer other the two options if possible.

You can also directly invoke ``b2`` from ``bindings/python`` directory and
provide your own configuration options. Note that this won't build or install
any wheel, but just create a shared library that can be imported from Python
programs.

You can see default ``b2`` arguments and hove they are chosen in ``setup.py``.
For details about all supported options, it is recommended to check `available
build options <building.html#step-2-building-libtorrent>`_ in the
documentation.

For example::

    b2 -j30 stage_module stage_dependencies

This will produce a ``libtorrent`` Python module in the current directory
(suffix and extension depend on the operating system). The libraries the Python
module depends on will be linked as shared libraries and be copied into
``./dependencies``.

By default, all libraries will be linked as shared libraries. You can control
whether the libtorrent main library or Boost is linked statically with the
``libtorrent-link=static`` and ``boost-link=static`` respectively.

For example, to build a self-contained python module::

    b2 -j30 libtorrent-link=static boost-link=static stage_module

Building and linking boost as static library on platforms other than Windows
is only possibly by building it from source and requires at least Boost 1.74.0.
Specify the ``BOOST_ROOT`` environment variable to point to the root directory
of the Boost source distribution and then run ``b2`` to do this.

Installing directly using Boost Build
-------------------------------------

*Note:* It is very unlikely that you will need to use this option unless you
want to do advanced configurations to bindings or libtorrent itself. You should
prefer other the two options if possible.

To install the Python module using Boost Build, build it with the following command::

    b2 install_module

By default the module will be installed to the Python user site. This can be
changed with the ``python-install-scope`` feature. The valid values are ``user``
(default) and ``system``. e.g.::

    b2 install_module python-install-scope=system

The Python interpreter and the python site used, depends on your Python
configuration in ``user-config.jam`` and which version of Python the module is
being built for.

To specify a custom installation path for the Python module, specify the desired
path with the ``python-install-path`` feature. e.g.::

    b2 install_module python-install-path=/home/foobar/python-site/

Usage
=====

Introduction
------------

The Python interface is nearly identical to the C++ interface. Please refer to
the `library reference <reference.html>`_. The main differences are:

asio::tcp::endpoint
    The endpoint type is represented as a tuple of a string for the address and an int for
    the port number. E.g. ``("127.0.0.1", 6881)`` represents the localhost port 6881.

lt::time_duration
    The time duration is represented as a number of seconds in a regular integer.

The following functions take a reference to a container that is filled with
entries by the function. The Python equivalents of these functions instead return
a list of entries.

* torrent_handle::get_peer_info
* torrent_handle::file_progress
* torrent_handle::get_download_queue
* torrent_handle::piece_availability

create_torrent::add_node()
    Takes two arguments, one string and one integer, instead of a pair. The string
    is the address and the integer is the port.

session::apply_settings()
    Accepts a dictionary with keys matching the names of settings in settings_pack.
    When calling ``apply_settings``, the dictionary does not need to have every
    settings set, keys that are not present are not updated.

To get a python dictionary of the settings, call ``session::get_settings()``.

Retrieving session statistics in Python is more convenient than that in C++. The
statistics are stored as an array in ``session_stats_alert``, which will be
posted after calling ``post_session_stats()`` in the ``session`` object. In
order to interpret the statistics array, in C++ it is required to call
``session_stats_metrics()`` to get the indices of these metrics, while in Python
it can be done using ``session_stats_alert.values["NAME_OF_METRIC"]``, where
``NAME_OF_METRIC`` is the name of a metric.

set_alert_notify
----------------

The ``set_alert_notify()`` function is not compatible with Python, since it
requires locking the GIL from within the libtorrent thread to call the callback,
it can cause a deadlock with the main thread.

Instead, use the Python-specific ``set_alert_fd()`` which takes a file descriptor
that will have 1 byte written to it to notify the client that there are new
alerts to be popped.

The file descriptor should be set to non-blocking mode. If writing to the
file/sending to the socket blocks, libtorrent's internal thread will stall.

This can be used with ``socket.socketpair()``, for example. The file descriptor
is what ``fileno()`` returns on a socket.

Example
=======

For an example python program, see ``client.py`` in the ``bindings/python/examples``
directory.

A very simple example usage of the module would be something like this:

.. include:: ../bindings/python/examples/simple_client.py
    :code: python
    :tab-width: 2
    :start-after: from __future__ import print_function
