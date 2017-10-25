libtorrent
----------

.. image:: https://travis-ci.org/arvidn/libtorrent.svg?branch=master
    :target: https://travis-ci.org/arvidn/libtorrent

.. image:: https://ci.appveyor.com/api/projects/status/w7teauvub5813mew/branch/master?svg=true
    :target: https://ci.appveyor.com/project/arvidn/libtorrent/branch/master

.. image:: https://doozer.io/badge/arvidn/libtorrent/buildstatus/master
	:target: https://doozer.io/user/arvidn/libtorrent

.. image:: https://codecov.io/github/arvidn/libtorrent/coverage.svg?branch=master
    :target: https://codecov.io/github/arvidn/libtorrent?branch=master&view=all#sort=missing&dir=desc

.. image:: https://sonarcloud.io/api/badges/gate?key=libtorrent
	:target: https://sonarcloud.io/dashboard?id=libtorrent

.. image:: https://www.openhub.net/p/rasterbar-libtorrent/widgets/project_thin_badge.gif
    :target: https://www.openhub.net/p/rasterbar-libtorrent?ref=sample

libtorrent is an open source C++ library implementing the BitTorrent protocol,
along with most popular extensions, making it suitable for real world
deployment. It is configurable to be able to fit both servers and embedded
devices.

The main goals of libtorrent are to be efficient and easy to use.

See `libtorrent.org`__ for more detailed build and usage instructions.

.. __: http://libtorrent.org

To build with boost-build, make sure boost and boost-build is installed and run:

   b2

In the libtorrent root. To build the examples, run ``b2`` in the ``examples``
directory.

See `building.html`__ for more details on how to build and which configuration
options are available. For python bindings, see `the python docs`__.

.. __: docs/building.rst
.. __: docs/python_binding.rst

