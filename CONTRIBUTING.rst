For general contribution guidelines, see `www.contribution-guide.org`__.

.. __: http://www.contribution-guide.org/

For general code style, see `c++ core guidelines`__.

.. __: http://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines

bug reporting checklist
.......................

Please keep in mind that there are at least 3 different ways of building
libtorrent (boost-build, autotools and cmake). There are also a number of build
time configuration options (``TORRENT_*`` macros). If it may be relevant, please
include how libtorrent was built in your bug report.

boost-build is the authoritative build platform and autotools is the
authoritative package tool. If there's a problem with a build script, please
consider posting a pull request with a fix.

Please be explicit about the behavior you see, ideally boil it down to its
essentials and provide example code, calling into libtorrent, reproducing the
problem.

For bittorrent protocol level issues, please include session, torrent and peer
level logs. The logs are available as alerts in libtorrent 1.1 and later (and
have to be enabled).

For tracker issues, please include a wireshark dump of the tracker announce
and response. It may be useful to also include session and torrent logs.

pull request checklist
......................

When creating a pull request, please consider the following checklist:

* make sure both travis-CI and appveyor builds are green. Note that on gcc and
  clang warnings are treated as errors. Some tests may be flapping, if so,
  please issue a rebuild of the specific build configuration. (I'm working on
  making all tests deterministic)
* please make sure to add appropriate comments. For client-facing changes,
  update the documentation comments in the public header (accepts restructured
  text)
* If adding a client-facing feature, please add brief entry to ``ChangeLog``
* Add a unit test (``tests``) or a regression test (``simulations``) to confirm
  the new behavior or feature. Don't forget negative tests (failure cases) and
  please pay as much care to tests as you would production code.
* if your patch adds a new .cpp file, please make sure it's added to the
  appropriate ``Jamfile``, ``Makefile.am`` and ``CMakeList.txt``. If it's adding
  a header file, make sure it's added to ``include/libtorrent/Makefile.am``.

