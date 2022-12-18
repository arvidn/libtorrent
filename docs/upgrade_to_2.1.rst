===========================
Upgrading to libtorrent 2.1
===========================

:Author: Arvid Norberg, arvid@libtorrent.org

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

In libtorrent 2.1, some parts of the API has changed and some deprecated parts
have been removed.
This document summarizes the changes affecting library clients.

C++14 no longer supported
=========================

libtorrent 2.1 requires at least C++-17. To build with boost build, specify the
C++ version using the ``cxxstd=17`` build feature (17 is the default).

WebTorrent support
==================

The major new feature in libtorrent 2.1 is WebTorrent support. WebTorrent
requires a stun server, it can be configured via the ``webtorrent_stun_server``
configuration option. It defaults to "stun.l.google.com:19302".

To enable webtorrent support, build with ``webtorrent=on``.

loading and saving torrents
===========================

The loading and saving of .torrent files and resume data has been simplified and
responsibilities of classes have become clearer.

	torrent_info
		Represents the immutable info section of a torrent file. It includes:
			* the file list
			* piece hashes (v1 torrents)
			* merkle root hashes (v2 torrents)
	add_torrent_params
		Represents the full torrent file, including:
			* the torrent_info, which is the immutable part
			* trackers
			* web seed URLs
			* piece layers (v2 torrents)

To load a torrent file from disk, use one of load_torrent_file(),
load_torrent_buffer() or load_torrent_parsed(). Those functions return an
add_torrent_params object containing all the information about the torrent.

To save a torrent file from an active torrent session, first ask for the
add_torrent_params object by calling save_resume_data() and pass in the
save_info_dict flag. The resulting save_resume_data_alert will contain an
add_torrent_params object which can be saved to disk using write_torrent_file().

Creating torrents
=================

There is a new API to create torrents. Previously the file structure and layout
would first be defined in a file_storage object then passed to a create_torrent
object's constructor. The new API instead builds the file list as a
std::vector<lt::create_file_entry>.

The new API can be summarized by this example:

.. code:: c++

	// recursively create a list of all files in the directory
	auto files = list_files("./my_torrent");

	create_torrent t(std::move(files));
	t.add_tracker("http://my.tracker.com/announce");
	t.set_creator("libtorrent example");

	// reads the files and calculates the hashes
	set_piece_hashes(t, ".");

	ofstream out("my_torrent.torrent", std::ios_base::binary);
	bencode(std::ostream_iterator<char>(out), t.generate());

The main motivations are:

1. To simplify the torrent creation logic by not conflating loading and parsing
	existing torrents with the act of creating them. The file_storage object is
	primarily used for the former and has an API that's largely internal. e.g.
	it never makes sense to specify the file root when adding files to a torrent
	about to be created. The file root is computed by the hashes added later.
2. To allow further optimizations of file_storage. By being used solely for
	representing an existing torrents file layout, it can be made more space
	efficient.

For more details, see create_torrent and create_file_entry.

resume data format
==================

The resume data format has been optimized to use less space, especially for v2
torrents. The ``file-version`` has been bumped from ``1`` to ``2`` with some
fields being serialized in a more space efficient manner.

The version-1 ``pieces`` field uses 8 bits per piece. 1 bit to indicate whether the piece
had been downloaded and 1 bit to indicate whether the piece had been verified,
in seed-mode. 6 of those bits were unused. In version-2, This field is now split
into ``pieces`` and a ``verified``, both of which are actual bitfields, one bit per piece.

Likewise, for merkle trees (for v2 torrents) the version-1 ``verified`` and ``mask``
fields are encoded as text. In version-2, these have been changed to actual bitfields.

A user-facing consequence is that the add_torrent_params class has changed
slightly. The ``merkle_tree_mask`` and ``verified_leaf_hashes`` fields are now
a sequences of bitfield objects.

load_resume_data() can still load the previous resume data format.

standard string_view
====================

libtorrent now uses ``std::string_view`` instead of ``boost::string_view`` or ``boost::string_ref```.
This affects libtorrent's API to some extent.

bump API version
================

By default, functions deprecated in libtorrent 1.1 and earlier are not included
in the build now. To enable them, build with ``deprecated-functions=1``.

entry as a variant
==================

The ``entry`` class (representing a node in a bencoded tree) has always had
variant-like properties. It can hold a string, integer, list of dictionary. This
type now derives from ``std::variant`` to provide a standard variant interface.

session construction
====================

Previously, ``session_flags_t`` could be passed directly to the ``session``
constructor. These constructors are now deprecated in favor of the unified
constructor taking a ``session_params`` object. Flags can now be passed through
the ``session_params`` object.

set_piece_hashes()
==================

The signature of set_piece_hashes() has changed slightly, from taking a
settings_interface to a settings_pack. The impact of this change is expected to
be minimal, since settings_pack implements the settings_interface and is the
only public type doing so.
