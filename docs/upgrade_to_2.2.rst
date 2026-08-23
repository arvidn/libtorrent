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

``file_storage`` is not a class clients are meant to construct. It is part of
the ``torrent_info`` object, and is normally only accessed as a reference
through it. A ``file_storage`` object must never outlive the ``torrent_info``
it came from.

``file_storage``'s ``add_file_borrow()`` overloads are no longer
exported from the shared library, and have been removed from the
python bindings. They were never meant to be part of the public
interface, only used internally by ``torrent_info`` while parsing a
.torrent file.

To build a file list when creating a new torrent, use ``create_torrent``
and ``create_file_entry`` instead, see `Creating torrents`_ in the 2.1
upgrade guide.

.. _`Creating torrents`: upgrade_to_2.1-ref.html#creating-torrents

configurable filename sanitization
==================================

The rules for sanitizing filenames found in a torrent's info-dict are now
controlled by ``path_sanitize_flags_t``, exposed via
``add_torrent_params::sanitize_flags`` and
``load_torrent_limits::sanitize_flags``. It defaults to
``path_sanitize_flags::default_flags``, which now also filters DOS/Windows
reserved device names (e.g. ``con``, ``com1``) on Windows, via the new
``path_sanitize_flags::filter_dos_reserved_names`` bit. This also includes
``path_sanitize_flags::filter_unicode_formatting_chars``, which makes the
filtering of unicode formatting characters (introduced in libtorrent 2.1)
optional.

The ruleset that was actually in effect in earlier releases is preserved
under versioned names, ``path_sanitize_flags::libtorrent_2_0`` and
``path_sanitize_flags::libtorrent_2_1``, matching the naming of the new
``path_sanitize_flags::libtorrent_2_2`` (equivalent to ``default_flags``).
Resume data written before this feature existed is interpreted using
``libtorrent_2_0`` or ``libtorrent_2_1``, based on the ``libtorrent-version``
recorded in it, rather than ``default_flags``, so an upgrade never silently
re-sanitizes an existing torrent's files under different rules than the
ones used to create them on disk.

Changing this ruleset for a torrent that already has files on disk can make
libtorrent look for them under different names than the ones actually
there. The ordinary resume data round trip pins and restores it
automatically; only clients overriding ``sanitize_flags`` themselves, or
reconstructing ``add_torrent_params`` from a .torrent file rather than
resume data, need to take care.

The deprecated ``torrent_info`` constructors that take no
``load_torrent_limits`` default to ``libtorrent_2_1`` rather than
``default_flags``, since callers still using them cannot supply a ruleset
and are most likely dealing with pre-existing torrents.

Clients that cache a torrent's info-dict separately from its resume data
(e.g. a stored .torrent file re-parsed on startup, independently of the
fast-resume file) must propagate the ``sanitize_flags`` recovered by
``read_resume_data()`` into the ``load_torrent_limits`` used for that
separate re-parse themselves; libtorrent has no way to reconcile the two
once they come from different calls.
