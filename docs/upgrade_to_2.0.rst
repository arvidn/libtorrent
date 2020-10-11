===========================
Upgrading to libtorrent 2.0
===========================

:Author: Arvid Norberg, arvid@libtorrent.org

.. contents:: Table of contents
  :depth: 2
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
one SHA-1 hash and one SHA-256 hash. These are bundled in a new type called
info_hash_t. Many places that previously took an info-hash as sha1_hash now
takes an info_hash_t. For backwards compatibility, info_hash_t is implicitly
convertible to and from sha1_hash and is interpreted as the v1 info-hash.
The implicit conversion is deprecated though.

Perhaps most noteworthy is that ``add_torrent_params::info_hash`` is now
deprecated in favor of ``add_torrent_params::info_hashes`` which is an
info_hash_t.

The alerts torrent_removed_alert, torrent_deleted_alert,
torrent_delete_failed_alert all have ``info_hash`` members. Those members are
now deprecated in favor of an ``info_hashes`` member, which is of type
info_hash_t.

An info_hash_t object for a hybrid torrent will have both the v1 and v2 hashes
set, it will compare false to a sha1_hash of *just* the v1 hash.

Calls to torrent_handle::info_hash() may need to be replaced by
torrent_handle::info_hashes(), in order to get both v1 and v2 hashes.

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

socket_type_t
-------------

There is a new ``enum class`` called ``socket_type_t`` used to identify different
kinds of sockets. In previous versions of libtorrent this was exposed as plain
``int`` with subtly different sets of meanings.

Previously there was an enum value ``udp``, which has been deprecated in favor of ``utp``.

The socket type is exposed in the following alerts, which now use the ``socket_type_t``
enum instead of ``int``:

* ``peer_connect_alert``
* ``peer_disconnected_alert``
* ``incoming_connection_alert``
* ``listen_failed_alert``
* ``listen_succeeded_alert``


DHT settings
============

DHT configuration options have previously been set separately from the main client settings.
In libtorrent 2.0 they have been unified into the main settings_pack.

Hence, `lt::dht::dht_settings` is now deprecated, in favor of the new `dht_*`
settings in settings_pack.

Deprecating `dht_settings` also causes an API change to the dht custom storage
constructor (see session_params). Instead of taking a `dht_settings` object, it
is now passed the full `settings_pack`. This is considered a niche interface,
so there is no backward compatibility option provided.

stats_alert
===========

The stats_alert is deprecated. Instead, call session::post_torrent_updates().
This will post a state_update_alert containing torrent_status of all torrents
that have any updates since last time this function was called.

The new mechanism scales a lot better.

saving and restoring session state
==================================

The functions ``save_state()`` and ``load_state()`` on the session object have
been deprecated in favor loading the session state up-front using
read_session_params() and construct the session from it.

The session state can be acquired, in the form of a session_params object, by
calling session::session_state().

The session_params object is passed to the session constructor, and will restore
the state from a previous session.

Use read_session_params() and write_session_params() to serialize and de-serialize
the session_params object.

As a result of this, plugins that wish to save and restore state or settings
must now use the new overload of load_state(), that takes a
``std::map<std::string, std::string>``. Similarly, for saving state, it now has
to be saved to a ``std::map<std::string, std::string>`` via the new overload of
save_state().

A lot of session constructors have been deprecated in favor of the ones that take
a session_params object. The session_params object can be implicitly constructed
from a settings_pack, to cover one of the now-deprecated constructors. However,
to access this conversion `libtorrent/session_params.hpp` must be included.

userdata is no longer a void\*
==============================

The ``userdata`` field in add_torrent_params is no longer a raw void pointer.
Instead it is a type-safe client_data_t object. client_data_t is similar to
``std::any``, it can hold a pointer of any type by assignment and can be cast
back to that pointer via ``static_cast`` (explicit conversion). However, if the
pointer type it is cast to is not identical to what was assigned, a ``nullptr``
is returned. Note that the type has to be identical in CV-qualifiers as well.

This userdata field affects the plugin APIs that has this field passed into it.

Additionally, there's now a way to ask a torrent_handle for the userdata, so it is
associated with the torrent itself.

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
The corresponding ``torrent_update_alert`` has also been removed. In libtorrent 2.0
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
Instead of configuring a custom storage (implementing ``storage_interface``) when
adding a torrent, you can now configure a disk subsystem (implementing
disk_interface) when creating a session.

Systems that don't support memory mapped files can still be used with a simple
``fopen()``/``fclose()`` family of functions. This disk subsystem is also not threaded
and generally more primitive than the memory mapped file one.

Clients that need to customize storage should implement the disk_interface and
configure it at session creation time instead of ``storage_interface`` configured
in add_torrent_params. add_torrent_params no longer has a storage_constructor
member.

As a consequence of this, ``get_storage_impl()`` has been removed from torrent_handle.

``aio_threads`` and ``hashing_threads``
---------------------------------------

In previous versions of libtorrent, the number of disk threads to use were
configured by settings_pack::aio_threads. Every fourth thread was dedicated to
run hash jobs, i.e. computing SHA-1 piece hashes to compare them against the
expected hash.

This setting has now been split up to allow controlling the number of dedicated
hash threads independently from the number of generic disk I/O threads.
settings_pack::hashing_threads is now used to control the number of threads
dedicated to computing hashes.

cache_size
----------

The ``cache_size`` setting is no longer used. The caching of disk I/O is handled
by the operating system.

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

