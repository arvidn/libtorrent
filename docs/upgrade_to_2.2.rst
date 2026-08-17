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
