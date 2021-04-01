.. image:: docs/img/logo-color-text.png

.. image:: https://github.com/arvidn/libtorrent/actions/workflows/windows.yml/badge.svg
    :target: https://github.com/arvidn/libtorrent/actions/workflows/windows.yml

.. image:: https://github.com/arvidn/libtorrent/actions/workflows/macos.yml/badge.svg
    :target: https://github.com/arvidn/libtorrent/actions/workflows/macos.yml

.. image:: https://github.com/arvidn/libtorrent/actions/workflows/linux.yml/badge.svg
    :target: https://github.com/arvidn/libtorrent/actions/workflows/linux.yml

.. image:: https://github.com/arvidn/libtorrent/actions/workflows/python.yml/badge.svg
    :target: https://github.com/arvidn/libtorrent/actions/workflows/python.yml

.. image:: https://ci.appveyor.com/api/projects/status/w7teauvub5813mew/branch/RC_2_0?svg=true
    :target: https://ci.appveyor.com/project/arvidn/libtorrent/branch/RC_2_0

.. image:: https://api.cirrus-ci.com/github/arvidn/libtorrent.svg?branch=RC_2_0
    :target: https://cirrus-ci.com/github/arvidn/libtorrent

.. image:: https://img.shields.io/lgtm/alerts/g/arvidn/libtorrent.svg?logo=lgtm&logoWidth=18
    :target: https://lgtm.com/projects/g/arvidn/libtorrent/alerts/

.. image:: https://oss-fuzz-build-logs.storage.googleapis.com/badges/libtorrent.svg
    :target: https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&q=proj%3Alibtorrent&can=1

.. image:: https://codecov.io/github/arvidn/libtorrent/coverage.svg?branch=RC_2_0
    :target: https://codecov.io/github/arvidn/libtorrent?branch=RC_2_0&view=all#sort=missing&dir=desc

.. image:: https://img.shields.io/lgtm/grade/cpp/g/arvidn/libtorrent.svg?logo=lgtm&logoWidth=18
    :target: https://lgtm.com/projects/g/arvidn/libtorrent/context:cpp

.. image:: https://www.openhub.net/p/rasterbar-libtorrent/widgets/project_thin_badge.gif
    :target: https://www.openhub.net/p/rasterbar-libtorrent

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

libtorrent package versions in linux distributions, on repology_.

.. _repology: https://repology.org/project/libtorrent-rasterbar/versions

.. __: docs/building.rst
.. __: docs/python_binding.rst

