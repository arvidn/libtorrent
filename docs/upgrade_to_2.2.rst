===========================
Upgrading to libtorrent 2.2
===========================

:Author: Arvid Norberg, arvid@libtorrent.org

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

This document summarizes the changes affecting library clients in libtorrent
2.2.

C++17 no longer supported
=========================

libtorrent 2.2 requires at least C++-20. To build with boost build, specify the
C++ version using the ``cxxstd=20`` build feature (20 is the default).

predictive pieces feature removed
=================================

The predictive pieces feature has been removed. ``settings_pack::predictive_piece_announce``
is deprecated and no longer has any effect. The ``predictive-pieces`` build
feature and the ``TORRENT_DISABLE_PREDICTIVE_PIECES`` macro have also been
removed.

smart_ban is now built-in
=========================

The ``smart_ban`` plugin is no longer an extension. Its functionality is now
built into the ``torrent`` object directly, controlled by the
``settings_pack::enable_smart_ban`` setting (enabled by default).
``create_smart_ban_plugin()`` is deprecated, constructs nothing and always
returns a null pointer.

file_storage isn't constructible
================================

``file_storage`` is not a class clients are meant to build up from
scratch. It is part of the ``torrent_info`` object, and is normally
only accessed as a reference through it (e.g. via
``torrent_info::layout()``). A ``file_storage`` object must never
outlive the ``torrent_info`` it came from.

To build a file list when creating a new torrent, use ``create_torrent``
and ``create_file_entry`` instead, see `Creating torrents`_ in the 2.1
upgrade guide.

``file_storage``'s default constructor and the ``add_file_borrow()``
overloads are no longer exported from the shared library, and have
been removed from the python bindings. They were never meant to be
part of the public interface, only used internally by ``torrent_info``
while parsing a .torrent file. Statically linked C++ code can
technically still call them, but this should not be relied on.

``file_storage``'s copy and move constructors/assignment and
``add_file()`` remain exported and are available from python, so an
existing ``file_storage`` (obtained by reference from
``torrent_info::layout()`` or ``torrent_info::files()``) can still be
copied and appended to. The deprecated ``create_torrent(file_storage)``
python constructor remains available for this reason. The python
bindings for ``torrent_info::remap_files()`` (still available and
deprecated in C++) and the free function ``add_files()`` have been
removed, since neither had a way to obtain a suitable ``file_storage``
argument from python.

.. _`Creating torrents`: upgrade_to_2.1-ref.html#creating-torrents
