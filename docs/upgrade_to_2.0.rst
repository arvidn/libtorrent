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
C++ version using the ``cxxstd=14`` build feature (14 is the default).

BitTorrent v2 support
=====================

Supporting bittorrent v2 come with some changes to the API. Specifically to
support *hybrid* torrents. i.e. torrents that are compatible with v1-only
bittorrent clients as well as supporting v2 features among the peers that
support them.

info-hashes
-----------

With bittorrent v2 support, each torrent may now have two separate info hashes,
one sha-1 hash and one sha-256 hash. These are bundled in a new type called
info_hash_t. Many places that previously took an info-hash as sha1_hash now
takes an info_hash_t. For backwards compatibility, info_hash_t is implicitly
convertible to and from sha1_hash and is interpreted as the v1 info-hash.
The implicit conversion is deprecated though.

Perhaps most noteworthy is that ``add_torrent_params::info_hash`` is now of type
info_hash_t.

The alerts torrent_removed_alert, torrent_deleted_alert,
torrent_delete_failed_alert all have ``info_hash`` members. They are no longer
of the type sha1_hash, but info_hash_t.

announce_entry/tracker changes
------------------------------

On major change in the API is reporting of trackers. Since hybrid torrents
announce once per info-hash (once for v1 and once for v2), the tracker results
are also reported per *bittorrent version*.

Each tracker (announce_entry) has a list of ``endpoints``. Each corresponding to
a local listen socket. Each listen socket is announced independently. The
announce_endpoint in turn has an array ``info_hashes``, containing objects of
type announce_infohash, for each bittorrent version. The array is indexed by
the enum protocol_version. There are two members, ``V1`` and ``V2``.

Example:

.. code:: c++

	std::vector<lt::announce_entry> tr = h.trackers();
	for (lt::announce_entry const& ae : h.trackers()) {
	    for (lt::announce_endpoint const& aep : ae.endpoints) {
	        int version = 1;
	        for (lt::announce_infohash const& ai : aep.info_hashes) {
	            std::cout << "[V" << version << "] " << ae.tier << " " << ae.url
	                << " " << (ih.updating ? "updating" : "")
	                << " " << (ih.start_sent ? "start-sent" : "")
	                << " fails: " << ih.fails
	                << " msg: " << ih.message
	                << "\n";
	            ++version;
	        }
	    }
	}

Merkle tree support removed
---------------------------

The old merkle tree torrent support has been removed, as BitTorrent v2 has
better support for merkle trees, where each file has its own merkle tree.

This means add_torrent_params no longer has the ``merkle_tree`` member. Instead
it has the new ``verified_leaf_hashes`` and ``merkle_trees`` members.

It also means the ``merkle`` flag for create_torrent has been removed.
torrent_info no longer has ``set_merkle_tree()`` and ``merkle_tree()`` member
functions.

create_torrent changes
----------------------

The create_torrent class creates *hybrid* torrents by default. i.e. torrents
compatible with both v1 and v2 bittorrent clients.

To create v1-only torrents use the ``v1_only`` flag. To create v2-only torrents,
use the ``v2_only`` flag.

Perhaps the most important addition for v2 torrents is the new member function
set_hash2(), which is similar to set_hash(), but for the v2-part of a torrent.
One important difference is that v2 hashes are SHA-256 hashes, and they are set
*per file*. In v2 torrents, each file forms a merkle tree and each v2 piece hash
is the SHA-256 merkle root hash of the 16 kiB blocks in that piece.

All v2 torrents have pieces aligned to files, so the ``optimize_alignment`` flag
is no longer relevant (as it's effectively always on). Similarly, the
``mutable_torrent_support`` flag is also always on.

``pad_file_limit`` and ``alignment`` parameters to the create_torrent constructor
have also been removed. The rules for padding and alignment is well defined for
v2 torrents.

set_file_hash() and file_hash() functions are obsolete, as v2 torrents have
a file_root() for each file.


on_unknown_torrent() plugin API
-------------------------------

Since hybrid torrents have two info-hashes, the on_unknown_torrent() function
on the plugin class now takes an info_hash_t instead of a sha1_hash.


Adding torrents by URL no longer supported
==========================================

The URL covers 3 separate features, all deprecated in the previous version and
removed in 2.0.

downloading over HTTP
---------------------

One used to be able to add a torrent by specifying an HTTP URL in the
``add_torrent_params::url`` member. Libtorrent would download the file and attempt
to load the file as a .torrent file. The torrent_handle in this mode would
not represent a torrent, but a *potential* torrent. Its info-hash was the hash of
the URL until the torrent file could be loaded, at which point the info hash *changed*.
The corresponding torrent_update_alert has also been removed. In libtorrent 2.0
info-hashes cannot change. (Although they can be amended with bittorrent v1 or v2
info-hashes).

Instead of using this feature, clients should download the .torrent files
themselves, possibly spawn their own threads, before adding them to the session.

magnet links
------------

The ``add_torrent_params::url`` could also be used to add torrents by magnet link.
This was also deprecated in the previous version and has been removed in
libtorrent 2.0. Instead, use parse_magnet_uri() to construct an add_torrent_params
object to add to the session. This also allows the client to alter settings,
such as ``save_path``, before adding the magnet link.

async loading of .torrent files
-------------------------------

The ``add_torrent_params::url`` field also supported ``file://`` URLs. This would
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
open file descriptors) all storage objects would share a file_pool object
passed in to them.

In libtorrent 2.0, the default disk I/O uses memory mapped files, which means
a lot more of what used to belong in the disk caching subsystem is now handled
by the kernel. This greatly simplifies the disk code and also has the potential
of making a lot more efficient use of modern disks as well as physical memory.

In this new system, the customization point is the whole disk I/O subsystem.
Instead of configuring a custom storage (implementing storage_interface) when
adding a torrent, you can now configure a disk subsystem (implementing
disk_interface) when creating a session.

Systems that don't support memory mapped files can still be used with a simple
``fopen()``/``fclose()`` family of functions. This disk subsystem is also not threaded
and generally more primitive than the memory mapped file one.

Clients that need to customize storage should implement the disk_interface and
configure it at session creation time instead of storage_interface configured
in add_torrent_params. add_torrent_params no longer has a storage_constructor
member.

As a consequence of this, ``get_storage_impl()`` has been removed from torrent_handle.

cache_size
----------

The ``cache_size`` setting is no longer used. The caching of disk I/O is handled
by the perating system.

get_cache_info() get_cache_status()
-----------------------------------

Since libtorrent no longer manages the disk cache (except for a store-buffer),
``get_cache_info()`` and ``get_cache_status()`` on the session object has also
been removed. They cannot return anything useful.

last remnants of RSS support removed
====================================

The ``rss_notification`` alert category flag has been removed, which has been unused
and deprecated since libtorrent 1.2.

The ``uuid`` member of add_torrent_params has been removed. Torrents can no longer
be added under a specific UUID. This feature was specifically meant for RSS feeds,
which was removed in the previous version of libtorrent.

