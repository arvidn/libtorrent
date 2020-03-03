libtorrent
----------

.. image:: https://travis-ci.org/arvidn/libtorrent.svg?branch=master
    :target: https://travis-ci.org/arvidn/libtorrent

.. image:: https://ci.appveyor.com/api/projects/status/w7teauvub5813mew/branch/master?svg=true
    :target: https://ci.appveyor.com/project/arvidn/libtorrent/branch/master

.. image:: https://doozer.io/badge/arvidn/libtorrent/buildstatus/master
	:target: https://doozer.io/user/arvidn/libtorrent

.. image:: https://img.shields.io/lgtm/alerts/g/arvidn/libtorrent.svg?logo=lgtm&logoWidth=18
	:target: https://lgtm.com/projects/g/arvidn/libtorrent/alerts/

.. image:: https://codecov.io/github/arvidn/libtorrent/coverage.svg?branch=master
    :target: https://codecov.io/github/arvidn/libtorrent?branch=master&view=all#sort=missing&dir=desc

.. image:: https://img.shields.io/lgtm/grade/cpp/g/arvidn/libtorrent.svg?logo=lgtm&logoWidth=18
	:target: https://lgtm.com/projects/g/arvidn/libtorrent/context:cpp

.. image:: https://sonarcloud.io/api/project_badges/measure?project=libtorrent&metric=alert_status
	:target: https://sonarcloud.io/dashboard?id=libtorrent

.. image:: https://sonarcloud.io/api/project_badges/measure?project=libtorrent&metric=security_rating
	:target: https://sonarcloud.io/dashboard?id=libtorrent

.. image:: https://sonarcloud.io/api/project_badges/measure?project=libtorrent&metric=sqale_rating
	:target: https://sonarcloud.io/dashboard?id=libtorrent

.. image:: https://www.openhub.net/p/rasterbar-libtorrent/widgets/project_thin_badge.gif
    :target: https://www.openhub.net/p/rasterbar-libtorrent?ref=sample

.. image:: https://bestpractices.coreinfrastructure.org/projects/3020/badge
    :target: https://bestpractices.coreinfrastructure.org/en/projects/3020

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

libtorrent `ABI report`_.

.. _`ABI report`: https://abi-laboratory.pro/index.php?view=timeline&l=libtorrent

.. __: docs/building.rst
.. __: docs/python_binding.rst

