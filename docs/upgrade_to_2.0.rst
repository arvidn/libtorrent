===========================
Upgrading to libtorrent 2.0
===========================

:Author: Arvid Norberg, arvid@libtorrent.org

.. contents:: Table of contents
  :depth: 1
  :backlinks: none

In libtorrent 2.0, some parts of the API has changed and some deprecated parts
have been removed.
This document summarizes the changes affecting library clients.

C++11 no longer supported
=========================

libtorrent 2.0 requires at least C++-14. To build with boost build, specify the
C++ version using the `cxxstd=14` build feature (14 is the default).

Adding torrents by URL no longer supported
==========================================

The URL covers 3 separate features, all deprecated in the previous version and
removed in 2.0.

downloading over HTTP
---------------------

One used to be able to add a torrent by specifying an HTTP URL in the
`add_torrent_params::url` member. Libtorrent would download the file and attempt
to load the file as a .torrent file. The torrent_handle in this mode would
not represent a torrent, but a *potential* torrent. Its info-hash was the hash of
the URL until the torrent file could be loaded, at which point the info hash *changed*.
The corresponding `torrent_update_alert` has also been removed. In libtorrent 2.0
info-hashes cannot change. (Although they can be amended with bittorrent v1 or v2
info-hashes).

Instead of using this feature, clients should download the .torrent files
themselves, possibly spawn their own threads, before adding them to the session.

magnet links
------------

The `add_torrent_params::url` could also be used to add torrents by magnet link.
This was also deprecated in the previous version and has been removed in
libtorrent 2.0. Instead, use parse_magnet_uri() to construct an add_torrent_params
object to add to the session. This also allows the client to alter settings,
such as `save_path`, before adding the magnet link.

async loading of .torrent files
-------------------------------

The `add_torrent_params::url` field also supported `file://` URLs. This would
use a libtorrent thread to load the file from disk, asynchronously (in the case
of async_add_torrent()). This feature has been removed. Clients should instead
load their torrents from disk themselves, before adding them to the session.
Possibly spawning their own threads.

Disk I/O overhaul
=================

In libtorrent 2.0, the disk I/O subsystem underwent a significant update. In
previous versions of libtorrent, each torrent has had its own, isolated,
disk storage object. This was a customization point. In order to share things
like a pool of open file handles across torrents (to have a global limit on
open file descriptors) all storage objects would share a `file_pool` object
passed in to them.

In libtorrent 2.0, the default disk I/O uses memory mapped files, which means
a lot more of what used to belong in the disk caching subsystem is now handled
by the kernel. This greatly simplifies the disk code and also has the potential
of making a lot more efficient use of modern disks as well as physical memory.

In this new system, the customization point is the whole disk I/O subsystem.
Instead of configuring a custom storage (implementing `storage_interface`) when
adding a torrent, you can now configure a disk subsystem (implementing
`disk_interface`) when creating a session.

Systems that don't support memory mapped files can still be used with a simple
`fopen()/fclose()` family of functions. This disk subsystem is also not threaded
and generally more primitive than the memory mapped file one.

Clients that need to customize storage should implement the `disk_interface` and
configure it at session creation time instead of `storage_interface` configured
in add_torrent_params. add_torrent_params no longer has a `storage_constructor`
member.

As a consequence of this, `get_storage_impl()` has been removed from torrent_handle.

get_cache_info() get_cache_status()
-----------------------------------

Since libtorrent no longer manages the disk cache (except for a store-buffer),
`get_cache_info()` and `get_cache_status()` on the session object has also
been removed. They cannot return anything useful.

last remnants of RSS support removed
====================================

The `rss_notification` alert category flag has been removed, which has been unused
and deprecated since libtorrent 1.2.

The `uuid` member of add_torrent_params has been removed. Torrents can no longer
be added under a specific UUID. This feature was specifically meant for RSS feeds,
which was removed in the previous version of libtorrent.

