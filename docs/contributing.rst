=================
libtorrent manual
=================

:Author: Arvid Norberg, arvid@libtorrent.org
:Version: 1.1.0

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

contributing to libtorrent
==========================

There are several ways to contribute to libtorrent at various levels. Any help is
much appreciated. If you're interested in something libtorrent related that's not
enumerated on this page, please contact arvid@libtorrent.org or the `mailing list`_.

.. _`mailing list`: http://lists.sourceforge.net/lists/listinfo/libtorrent-discuss

1. Testing
	This is not just limited to finding bugs and ways to reproduce crashes, but also
	sub-optimal behavior is certain scenarios and finding ways to reproduce those. Please
	report any issue to the bug tracker at `github`_.

	New features that need testing are streaming (``set_piece_deadline()``), the different
	choking algorithms (especially the new BitTyrant choker), the disk cache options (such
	as ``explicit_cache``).

.. _`github`: https://github.com/arvidn/libtorrent/issues

2. Documentation
	Finding typos or outdated sections in the documentation. Contributing documentation
	based on your own experience and experimentation with the library or with BitTorrent
	in general. Non-reference documentation is very much welcome as well, higher level
	descriptions on how to configure libtorrent for various situations for instance.
	The reference documentation for libtorrent is generated from the header files.
	For updates, please submit a `pull request`_.

3. Code
	Contributing code for new features or bug-fixes is highly welcome. If you're interested
	in adding a feature but not sure where to start, please contact the `mailing list`_ or
	``#libtorrent`` @ ``irc.freenode.net``. For proposed fixes or udpates, please
	submit a `pull request`_.

	New features might be better support for integrating with other services, new choking
	algorithms, seeding policies, ports to new platforms etc.

For an overview of the internals of libtorrent, see the hacking_ page.

For outstanding things to do, see the `todo list`_.

.. _hacking: hacking.html

.. _`pull request`: https://github.com/arvidn/libtorrent

.. _`todo list`: todo.html

