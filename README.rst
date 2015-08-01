libtorrent
----------

.. image:: https://travis-ci.org/arvidn/libtorrent.svg?branch=RC_1_0
    :target: https://travis-ci.org/arvidn/libtorrent

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
