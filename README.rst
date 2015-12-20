libtorrent
----------

.. image:: https://travis-ci.org/arvidn/libtorrent.svg?branch=master
    :target: https://travis-ci.org/arvidn/libtorrent

.. image:: https://ci.appveyor.com/api/projects/status/w7teauvub5813mew/branch/master?svg=true
    :target: https://ci.appveyor.com/project/arvidn/libtorrent/branch/master

.. image:: https://codecov.io/github/arvidn/libtorrent/coverage.svg?branch=master
    :target: https://codecov.io/github/arvidn/libtorrent?branch=master&view=all#sort=missing&dir=desc

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

pull request checklist
......................

When creating a pull request, please consider the following checklist:

* make sure both travis-CI and appveyor builds are green. Note that on gcc and
  clang warnings are treated as errors. Some tests may be flapping, if so,
  please issue a rebuild of the specific build configuration. (I'm working on
  making all tests deterministic)
* If adding a user-facing feature, please add brief entry to ``ChangeLog``
* Add a unit test to confirm the new behavior or feature. Don't forget negative
  tests (i.e. failure cases) and please pay as much care to tests as you would
  production code.
* rebase on top of master periodically
* if your patch is against the current stable release branch, please also
  forward-port the patch to master (at the time of this writing, automatic
  merge in git does not work, possibly because the branch was created in svn)
* if your patch adds a new .cpp file, please make sure it's added to the
  appropriate ``Jamfile``, ``Makefile.am`` and ``CMakeList.txt``. If it's adding
  a header file, make sure it's added to ``include/libtorrent/Makefile.am``.
