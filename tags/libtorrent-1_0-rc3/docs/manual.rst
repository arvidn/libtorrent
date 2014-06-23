============================
libtorrent API Documentation
============================

:Author: Arvid Norberg, arvid@rasterbar.com
:Version: 1.0.0

.. contents:: Table of contents
  :depth: 1
  :backlinks: none

overview
========

The interface of libtorrent consists of a few classes. The main class is
the ``session``, it contains the main loop that serves all torrents.

The basic usage is as follows:

* construct a session
* load session state from settings file (see load_state())
* start extensions (see add_extension()).
* start DHT, LSD, UPnP, NAT-PMP etc (see start_dht(), start_lsd(), start_upnp()
  and start_natpmp()).
* parse .torrent-files and add them to the session (see torrent_info,
  async_add_torrent() and add_torrent())
* main loop (see session)

	* poll for alerts (see wait_for_alert(), pop_alerts())
	* handle updates to torrents, (see state_update_alert).
	* handle other alerts, (see alert).
	* query the session for information (see session::status()).
	* add and remove torrents from the session (remove_torrent())

* save resume data for all torrent_handles (optional, see
  save_resume_data())
* save session state (see save_state())
* destruct session object

Each class and function is described in this manual.

For a description on how to create torrent files, see create_torrent.

.. _make_torrent: make_torrent.html

things to keep in mind
======================

A common problem developers are facing is torrents stopping without explanation.
Here is a description on which conditions libtorrent will stop your torrents,
how to find out about it and what to do about it.

Make sure to keep track of the paused state, the error state and the upload
mode of your torrents. By default, torrents are auto-managed, which means
libtorrent will pause them, unpause them, scrape them and take them out
of upload-mode automatically.

Whenever a torrent encounters a fatal error, it will be stopped, and the
``torrent_status::error`` will describe the error that caused it. If a torrent
is auto managed, it is scraped periodically and paused or resumed based on
the number of downloaders per seed. This will effectively seed torrents that
are in the greatest need of seeds.

If a torrent hits a disk write error, it will be put into upload mode. This
means it will not download anything, but only upload. The assumption is that
the write error is caused by a full disk or write permission errors. If the
torrent is auto-managed, it will periodically be taken out of the upload
mode, trying to write things to the disk again. This means torrent will recover
from certain disk errors if the problem is resolved. If the torrent is not
auto managed, you have to call set_upload_mode() to turn
downloading back on again.

network primitives
==================

There are a few typedefs in the ``libtorrent`` namespace which pulls
in network types from the ``asio`` namespace. These are::

	typedef asio::ip::address address;
	typedef asio::ip::address_v4 address_v4;
	typedef asio::ip::address_v6 address_v6;
	using asio::ip::tcp;
	using asio::ip::udp;

These are declared in the ``<libtorrent/socket.hpp>`` header.

The ``using`` statements will give easy access to::

	tcp::endpoint
	udp::endpoint

Which are the endpoint types used in libtorrent. An endpoint is an address
with an associated port.

For documentation on these types, please refer to the `asio documentation`_.

.. _`asio documentation`: http://asio.sourceforge.net/asio-0.3.8/doc/asio/reference.html

exceptions
==========

Many functions in libtorrent have two versions, one that throws exceptions on
errors and one that takes an ``error_code`` reference which is filled with the
error code on errors.

There is one exception class that is used for errors in libtorrent, it is based
on boost.system's ``error_code`` class to carry the error code.

For more information, see libtorrent_exception and error_code_enum.

translating error codes
-----------------------

The error_code::message() function will typically return a localized error string,
for system errors. That is, errors that belong to the generic or system category.

Errors that belong to the libtorrent error category are not localized however, they
are only available in english. In order to translate libtorrent errors, compare the
error category of the ``error_code`` object against ``libtorrent::get_libtorrent_category()``,
and if matches, you know the error code refers to the list above. You can provide
your own mapping from error code to string, which is localized. In this case, you
cannot rely on ``error_code::message()`` to generate your strings.

The numeric values of the errors are part of the API and will stay the same, although
new error codes may be appended at the end.

Here's a simple example of how to translate error codes::

	std::string error_code_to_string(boost::system::error_code const& ec)
	{
		if (ec.category() != libtorrent::get_libtorrent_category())
		{
			return ec.message();
		}
		// the error is a libtorrent error

		int code = ec.value();
		static const char const* swedish[] =
		{
			"inget fel",
			"en fil i torrenten kolliderar med en fil fran en annan torrent",
			"hash check misslyckades",
			"torrentfilen ar inte en dictionary",
			"'info'-nyckeln saknas eller ar korrupt i torrentfilen",
			"'info'-faltet ar inte en dictionary",
			"'piece length' faltet saknas eller ar korrupt i torrentfilen",
			"torrentfilen saknar namnfaltet",
			"ogiltigt namn i torrentfilen (kan vara en attack)",
			// ... more strings here
		};

		// use the default error string in case we don't have it
		// in our translated list
		if (code < 0 || code >= sizeof(swedish)/sizeof(swedish[0]))
			return ec.message();

		return swedish[code];
	}

magnet links
============

Magnet links are URIs that includes an info-hash, a display name and optionally
a tracker url. The idea behind magnet links is that an end user can click on a
link in a browser and have it handled by a bittorrent application, to start a
download, without any .torrent file.

The format of the magnet URI is:

**magnet:?xt=urn:btih:** *Base16 encoded info-hash* [ **&dn=** *name of download* ] [ **&tr=** *tracker URL* ]*

queuing
=======

libtorrent supports *queuing*. Which means it makes sure that a limited number of
torrents are being downloaded at any given time, and once a torrent is completely
downloaded, the next in line is started.

Torrents that are *auto managed* are subject to the queuing and the active
torrents limits. To make a torrent auto managed, set ``auto_managed`` to true
when adding the torrent (see async_add_torrent() and add_torrent()).

The limits of the number of downloading and seeding torrents are controlled via
``active_downloads``, ``active_seeds`` and ``active_limit`` in
session_settings. These limits takes non auto managed torrents into account as
well. If there are more non-auto managed torrents being downloaded than the
``active_downloads`` setting, any auto managed torrents will be queued until
torrents are removed so that the number drops below the limit.

The default values are 8 active downloads and 5 active seeds.

At a regular interval, torrents are checked if there needs to be any
re-ordering of which torrents are active and which are queued. This interval
can be controlled via ``auto_manage_interval`` in session_settings. It defaults
to every 30 seconds.

For queuing to work, resume data needs to be saved and restored for all
torrents. See save_resume_data().

downloading
-----------

Torrents that are currently being downloaded or incomplete (with bytes still to
download) are queued. The torrents in the front of the queue are started to be
actively downloaded and the rest are ordered with regards to their queue
position. Any newly added torrent is placed at the end of the queue. Once a
torrent is removed or turns into a seed, its queue position is -1 and all
torrents that used to be after it in the queue, decreases their position in
order to fill the gap.

The queue positions are always in a sequence without any gaps.

Lower queue position means closer to the front of the queue, and will be
started sooner than torrents with higher queue positions.

To query a torrent for its position in the queue, or change its position, see:
queue_position(), queue_position_up(), queue_position_down(),
queue_position_top() and queue_position_bottom().

seeding
-------

Auto managed seeding torrents are rotated, so that all of them are allocated a
fair amount of seeding. Torrents with fewer completed *seed cycles* are
prioritized for seeding. A seed cycle is completed when a torrent meets either
the share ratio limit (uploaded bytes / downloaded bytes), the share time ratio
(time seeding / time downloaing) or seed time limit (time seeded).

The relevant settings to control these limits are ``share_ratio_limit``,
``seed_time_ratio_limit`` and ``seed_time_limit`` in session_settings.


fast resume
===========

The fast resume mechanism is a way to remember which pieces are downloaded
and where they are put between sessions. You can generate fast resume data by
calling save_resume_data() on torrent_handle. You can
then save this data to disk and use it when resuming the torrent. libtorrent
will not check the piece hashes then, and rely on the information given in the
fast-resume data. The fast-resume data also contains information about which
blocks, in the unfinished pieces, were downloaded, so it will not have to
start from scratch on the partially downloaded pieces.

To use the fast-resume data you simply give it to async_add_torrent() and
add_torrent(), and it will skip the time consuming checks. It may have to do
the checking anyway, if the fast-resume data is corrupt or doesn't fit the
storage for that torrent, then it will not trust the fast-resume data and just
do the checking.

file format
-----------

The file format is a bencoded dictionary containing the following fields:

+--------------------------+--------------------------------------------------------------+
| ``file-format``          | string: "libtorrent resume file"                             |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``file-version``         | integer: 1                                                   |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``info-hash``            | string, the info hash of the torrent this data is saved for. |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``blocks per piece``     | integer, the number of blocks per piece. Must be: piece_size |
|                          | / (16 * 1024). Clamped to be within the range [1, 256]. It   |
|                          | is the number of blocks per (normal sized) piece. Usually    |
|                          | each block is 16 * 1024 bytes in size. But if piece size is  |
|                          | greater than 4 megabytes, the block size will increase.      |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``pieces``               | A string with piece flags, one character per piece.          |
|                          | Bit 1 means we have that piece.                              |
|                          | Bit 2 means we have verified that this piece is correct.     |
|                          | This only applies when the torrent is in seed_mode.          |
+--------------------------+--------------------------------------------------------------+
| ``slots``                | list of integers. The list maps slots to piece indices. It   |
|                          | tells which piece is on which slot. If piece index is -2 it  |
|                          | means it is free, that there's no piece there. If it is -1,  |
|                          | means the slot isn't allocated on disk yet. The pieces have  |
|                          | to meet the following requirement:                           |
|                          |                                                              |
|                          | If there's a slot at the position of the piece index,        |
|                          | the piece must be located in that slot.                      |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``total_uploaded``       | integer. The number of bytes that have been uploaded in      |
|                          | total for this torrent.                                      |
+--------------------------+--------------------------------------------------------------+
| ``total_downloaded``     | integer. The number of bytes that have been downloaded in    |
|                          | total for this torrent.                                      |
+--------------------------+--------------------------------------------------------------+
| ``active_time``          | integer. The number of seconds this torrent has been active. |
|                          | i.e. not paused.                                             |
+--------------------------+--------------------------------------------------------------+
| ``seeding_time``         | integer. The number of seconds this torrent has been active  |
|                          | and seeding.                                                 |
+--------------------------+--------------------------------------------------------------+
| ``num_seeds``            | integer. An estimate of the number of seeds on this torrent  |
|                          | when the resume data was saved. This is scrape data or based |
|                          | on the peer list if scrape data is unavailable.              |
+--------------------------+--------------------------------------------------------------+
| ``num_downloaders``      | integer. An estimate of the number of downloaders on this    |
|                          | torrent when the resume data was last saved. This is used as |
|                          | an initial estimate until we acquire up-to-date scrape info. |
+--------------------------+--------------------------------------------------------------+
| ``upload_rate_limit``    | integer. In case this torrent has a per-torrent upload rate  |
|                          | limit, this is that limit. In bytes per second.              |
+--------------------------+--------------------------------------------------------------+
| ``download_rate_limit``  | integer. The download rate limit for this torrent in case    |
|                          | one is set, in bytes per second.                             |
+--------------------------+--------------------------------------------------------------+
| ``max_connections``      | integer. The max number of peer connections this torrent     |
|                          | may have, if a limit is set.                                 |
+--------------------------+--------------------------------------------------------------+
| ``max_uploads``          | integer. The max number of unchoked peers this torrent may   |
|                          | have, if a limit is set.                                     |
+--------------------------+--------------------------------------------------------------+
| ``seed_mode``            | integer. 1 if the torrent is in seed mode, 0 otherwise.      |
+--------------------------+--------------------------------------------------------------+
| ``file_priority``        | list of integers. One entry per file in the torrent. Each    |
|                          | entry is the priority of the file with the same index.       |
+--------------------------+--------------------------------------------------------------+
| ``piece_priority``       | string of bytes. Each byte is interpreted as an integer and  |
|                          | is the priority of that piece.                               |
+--------------------------+--------------------------------------------------------------+
| ``auto_managed``         | integer. 1 if the torrent is auto managed, otherwise 0.      |
+--------------------------+--------------------------------------------------------------+
| ``sequential_download``  | integer. 1 if the torrent is in sequential download mode,    |
|                          | 0 otherwise.                                                 |
+--------------------------+--------------------------------------------------------------+
| ``paused``               | integer. 1 if the torrent is paused, 0 otherwise.            |
+--------------------------+--------------------------------------------------------------+
| ``trackers``             | list of lists of strings. The top level list lists all       |
|                          | tracker tiers. Each second level list is one tier of         |
|                          | trackers.                                                    |
+--------------------------+--------------------------------------------------------------+
| ``mapped_files``         | list of strings. If any file in the torrent has been         |
|                          | renamed, this entry contains a list of all the filenames.    |
|                          | In the same order as in the torrent file.                    |
+--------------------------+--------------------------------------------------------------+
| ``url-list``             | list of strings. List of url-seed URLs used by this torrent. |
|                          | The urls are expected to be properly encoded and not contain |
|                          | any illegal url characters.                                  |
+--------------------------+--------------------------------------------------------------+
| ``httpseeds``            | list of strings. List of httpseed URLs used by this torrent. |
|                          | The urls are expected to be properly encoded and not contain |
|                          | any illegal url characters.                                  |
+--------------------------+--------------------------------------------------------------+
| ``merkle tree``          | string. In case this torrent is a merkle torrent, this is a  |
|                          | string containing the entire merkle tree, all nodes,         |
|                          | including the root and all leaves. The tree is not           |
|                          | necessarily complete, but complete enough to be able to send |
|                          | any piece that we have, indicated by the have bitmask.       |
+--------------------------+--------------------------------------------------------------+
| ``save_path``            | string. The save path where this torrent was saved. This is  |
|                          | especially useful when moving torrents with move_storage()   |
|                          | since this will be updated.                                  |
+--------------------------+--------------------------------------------------------------+
| ``peers``                | list of dictionaries. Each dictionary has the following      |
|                          | layout:                                                      |
|                          |                                                              |
|                          | +----------+-----------------------------------------------+ |
|                          | | ``ip``   | string, the ip address of the peer. This is   | |
|                          | |          | not a binary representation of the ip         | |
|                          | |          | address, but the string representation. It    | |
|                          | |          | may be an IPv6 string or an IPv4 string.      | |
|                          | +----------+-----------------------------------------------+ |
|                          | | ``port`` | integer, the listen port of the peer          | |
|                          | +----------+-----------------------------------------------+ |
|                          |                                                              |
|                          | These are the local peers we were connected to when this     |
|                          | fast-resume data was saved.                                  |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``unfinished``           | list of dictionaries. Each dictionary represents an          |
|                          | piece, and has the following layout:                         |
|                          |                                                              |
|                          | +-------------+--------------------------------------------+ |
|                          | | ``piece``   | integer, the index of the piece this entry | |
|                          | |             | refers to.                                 | |
|                          | +-------------+--------------------------------------------+ |
|                          | | ``bitmask`` | string, a binary bitmask representing the  | |
|                          | |             | blocks that have been downloaded in this   | |
|                          | |             | piece.                                     | |
|                          | +-------------+--------------------------------------------+ |
|                          | | ``adler32`` | The adler32 checksum of the data in the    | |
|                          | |             | blocks specified by ``bitmask``.           | |
|                          | |             |                                            | |
|                          | +-------------+--------------------------------------------+ |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``file sizes``           | list where each entry corresponds to a file in the file list |
|                          | in the metadata. Each entry has a list of two values, the    |
|                          | first value is the size of the file in bytes, the second     |
|                          | is the time stamp when the last time someone wrote to it.    |
|                          | This information is used to compare with the files on disk.  |
|                          | All the files must match exactly this information in order   |
|                          | to consider the resume data as current. Otherwise a full     |
|                          | re-check is issued.                                          |
+--------------------------+--------------------------------------------------------------+
| ``allocation``           | The allocation mode for the storage. Can be either ``full``  |
|                          | or ``compact``. If this is full, the file sizes and          |
|                          | timestamps are disregarded. Pieces are assumed not to have   |
|                          | moved around even if the files have been modified after the  |
|                          | last resume data checkpoint.                                 |
+--------------------------+--------------------------------------------------------------+

storage allocation
==================

There are two modes in which storage (files on disk) are allocated in libtorrent.

1. The traditional *full allocation* mode, where the entire files are filled up
   with zeros before anything is downloaded. Files are allocated on demand, the
   first time anything is written to them. The main benefit of this mode is that
   it avoids creating heavily fragmented files.

2. The *sparse allocation*, sparse files are used, and pieces are downloaded
   directly to where they belong. This is the recommended (and default) mode.

In previous versions of libtorrent, a 3rd mode was supported, *compact
allocation*. Support for this is deprecated and will be removed in future
versions of libtorrent. It's still described in here for completeness.

The allocation mode is selected when a torrent is started. It is passed as an
argument to session::add_torrent() or session::async_add_torrent().

The decision to use full allocation or compact allocation typically depends on
whether any files have priority 0 and if the filesystem supports sparse files.

sparse allocation
-----------------

On filesystems that supports sparse files, this allocation mode will only use
as much space as has been downloaded.

The main drawback of this mode is that it may create heavily fragmented files.

 * It does not require an allocation pass on startup.

full allocation
---------------

When a torrent is started in full allocation mode, the disk-io thread
will make sure that the entire storage is allocated, and fill any gaps with zeros.
It will of course still check for existing pieces and fast resume data. The main
drawbacks of this mode are:

 * It may take longer to start the torrent, since it will need to fill the files
   with zeroes. This delay is linear to the size of the download.

 * The download may occupy unnecessary disk space between download sessions.

 * Disk caches usually perform poorly with random access to large files
   and may slow down the download some.

The benefits of this mode are:

 * Downloaded pieces are written directly to their final place in the files and
   the total number of disk operations will be fewer and may also play nicer to
   filesystems' file allocation, and reduce fragmentation.

 * No risk of a download failing because of a full disk during download, once
   all files have been created.

compact allocation
------------------

.. note::
	Note that support for compact allocation is deprecated in libttorrent, and will
	be removed in future versions.

The compact allocation will only allocate as much storage as it needs to keep
the pieces downloaded so far. This means that pieces will be moved around to be
placed at their final position in the files while downloading (to make sure the
completed download has all its pieces in the correct place). So, the main
drawbacks are:

 * More disk operations while downloading since pieces are moved around.

 * Potentially more fragmentation in the filesystem.

 * Cannot be used while having files with priority 0.

The benefits though, are:

 * No startup delay, since the files don't need allocating.

 * The download will not use unnecessary disk space.

 * Disk caches perform much better than in full allocation and raises the
   download speed limit imposed by the disk.

 * Works well on filesystems that don't support sparse files.

The algorithm that is used when allocating pieces and slots isn't very
complicated. For the interested, a description follows.

storing a piece:

1. let **A** be a newly downloaded piece, with index **n**.
2. let **s** be the number of slots allocated in the file we're
   downloading to. (the number of pieces it has room for).
3. if **n** >= **s** then allocate a new slot and put the piece there.
4. if **n** < **s** then allocate a new slot, move the data at
   slot **n** to the new slot and put **A** in slot **n**.

allocating a new slot:

1. if there's an unassigned slot (a slot that doesn't
   contain any piece), return that slot index.
2. append the new slot at the end of the file (or find an unused slot).
3. let **i** be the index of newly allocated slot
4. if we have downloaded piece index **i** already (to slot **j**) then

   1. move the data at slot **j** to slot **i**.
   2. return slot index **j** as the newly allocated free slot.

5. return **i** as the newly allocated slot.

extensions
==========

These extensions all operates within the `extension protocol`_. The name of the
extension is the name used in the extension-list packets, and the payload is
the data in the extended message (not counting the length-prefix, message-id
nor extension-id).

.. _`extension protocol`: extension_protocol.html

Note that since this protocol relies on one of the reserved bits in the
handshake, it may be incompatible with future versions of the mainline
bittorrent client.

These are the extensions that are currently implemented.

metadata from peers
-------------------

Extension name: "LT_metadata"

This extension is deprecated in favor of the more widely supported
``ut_metadata`` extension, see `BEP 9`_. The point with this extension is that
you don't have to distribute the metadata (.torrent-file) separately. The
metadata can be distributed through the bittorrent swarm. The only thing you
need to download such a torrent is the tracker url and the info-hash of the
torrent.

It works by assuming that the initial seeder has the metadata and that the
metadata will propagate through the network as more peers join.

There are three kinds of messages in the metadata extension. These packets are
put as payload to the extension message. The three packets are:

	* request metadata
	* metadata
	* don't have metadata

request metadata:

+-----------+---------------+----------------------------------------+
| size      | name          | description                            |
+===========+===============+========================================+
| uint8_t   | msg_type      | Determines the kind of message this is |
|           |               | 0 means 'request metadata'             |
+-----------+---------------+----------------------------------------+
| uint8_t   | start         | The start of the metadata block that   |
|           |               | is requested. It is given in 256:ths   |
|           |               | of the total size of the metadata,     |
|           |               | since the requesting client don't know |
|           |               | the size of the metadata.              |
+-----------+---------------+----------------------------------------+
| uint8_t   | size          | The size of the metadata block that is |
|           |               | requested. This is also given in       |
|           |               | 256:ths of the total size of the       |
|           |               | metadata. The size is given as size-1. |
|           |               | That means that if this field is set   |
|           |               | 0, the request wants one 256:th of the |
|           |               | metadata.                              |
+-----------+---------------+----------------------------------------+

metadata:

+-----------+---------------+----------------------------------------+
| size      | name          | description                            |
+===========+===============+========================================+
| uint8_t   | msg_type      | 1 means 'metadata'                     |
+-----------+---------------+----------------------------------------+
| int32_t   | total_size    | The total size of the metadata, given  |
|           |               | in number of bytes.                    |
+-----------+---------------+----------------------------------------+
| int32_t   | offset        | The offset of where the metadata block |
|           |               | in this message belongs in the final   |
|           |               | metadata. This is given in bytes.      |
+-----------+---------------+----------------------------------------+
| uint8_t[] | metadata      | The actual metadata block. The size of |
|           |               | this part is given implicit by the     |
|           |               | length prefix in the bittorrent        |
|           |               | protocol packet.                       |
+-----------+---------------+----------------------------------------+

Don't have metadata:

+-----------+---------------+----------------------------------------+
| size      | name          | description                            |
+===========+===============+========================================+
| uint8_t   | msg_type      | 2 means 'I don't have metadata'.       |
|           |               | This message is sent as a reply to a   |
|           |               | metadata request if the the client     |
|           |               | doesn't have any metadata.             |
+-----------+---------------+----------------------------------------+

.. _`BEP 9`: http://bittorrent.org/beps/bep_0009.html

dont_have
---------

Extension name: "lt_dont_have"

The ``dont_have`` extension message is used to tell peers that the client no
longer has a specific piece. The extension message should be advertised in the
``m`` dictionary as ``lt_dont_have``. The message format mimics the regular
``HAVE`` bittorrent message.

Just like all extension messages, the first 2 bytes in the mssage itself are 20
(the bittorrent extension message) and the message ID assigned to this
extension in the ``m`` dictionary in the handshake.

+-----------+---------------+----------------------------------------+
| size      | name          | description                            |
+===========+===============+========================================+
| uint32_t  | piece         | index of the piece the peer no longer  |
|           |               | has.                                   |
+-----------+---------------+----------------------------------------+

The length of this message (including the extension message prefix) is 6 bytes,
i.e. one byte longer than the normal ``HAVE`` message, because of the extension
message wrapping.

HTTP seeding
------------

There are two kinds of HTTP seeding. One with that assumes a smart (and polite)
client and one that assumes a smart server. These are specified in `BEP 19`_
and `BEP 17`_ respectively.

libtorrent supports both. In the libtorrent source code and API, BEP 19 urls
are typically referred to as *url seeds* and BEP 17 urls are typically referred
to as *HTTP seeds*.

The libtorrent implementation of `BEP 19`_ assumes that, if the URL ends with a
slash ('/'), the filename should be appended to it in order to request pieces
from that file. The way this works is that if the torrent is a single-file
torrent, only that filename is appended. If the torrent is a multi-file
torrent, the torrent's name '/' the file name is appended. This is the same
directory structure that libtorrent will download torrents into.

.. _`BEP 17`: http://bittorrent.org/beps/bep_0017.html
.. _`BEP 19`: http://bittorrent.org/beps/bep_0019.html

piece picker
============

The piece picker in libtorrent has the following features:

* rarest first
* sequential download
* random pick
* reverse order picking
* parole mode
* prioritize partial pieces
* prefer whole pieces
* piece affinity by speed category
* piece priorities

internal representation
-----------------------

It is optimized by, at all times, keeping a list of pieces ordered by rarity,
randomly shuffled within each rarity class. This list is organized as a single
vector of contigous memory in RAM, for optimal memory locality and to eliminate
heap allocations and frees when updating rarity of pieces.

Expensive events, like a peer joining or leaving, are evaluated lazily, since
it's cheaper to rebuild the whole list rather than updating every single piece
in it. This means as long as no blocks are picked, peers joining and leaving is
no more costly than a single peer joining or leaving. Of course the special
cases of peers that have all or no pieces are optimized to not require
rebuilding the list.

picker strategy
---------------

The normal mode of the picker is of course *rarest first*, meaning pieces that
few peers have are preferred to be downloaded over pieces that more peers have.
This is a fundamental algorithm that is the basis of the performance of
bittorrent. However, the user may set the piece picker into sequential download
mode. This mode simply picks pieces sequentially, always preferring lower piece
indices.

When a torrent starts out, picking the rarest pieces means increased risk that
pieces won't be completed early (since there are only a few peers they can be
downloaded from), leading to a delay of having any piece to offer to other
peers. This lack of pieces to trade, delays the client from getting started
into the normal tit-for-tat mode of bittorrent, and will result in a long
ramp-up time. The heuristic to mitigate this problem is to, for the first few
pieces, pick random pieces rather than rare pieces. The threshold for when to
leave this initial picker mode is determined by
session_settings::initial_picker_threshold.

reverse order
-------------

An orthogonal setting is *reverse order*, which is used for *snubbed* peers.
Snubbed peers are peers that appear very slow, and might have timed out a piece
request. The idea behind this is to make all snubbed peers more likely to be
able to do download blocks from the same piece, concentrating slow peers on as
few pieces as possible. The reverse order means that the most common pieces are
picked, instead of the rarest pieces (or in the case of sequential download,
the last pieces, intead of the first).

parole mode
-----------

Peers that have participated in a piece that failed the hash check, may be put
in *parole mode*. This means we prefer downloading a full piece  from this
peer, in order to distinguish which peer is sending corrupt data. Whether to do
this is or not is controlled by session_settings::use_parole_mode.

In parole mode, the piece picker prefers picking one whole piece at a time for
a given peer, avoiding picking any blocks from a piece any other peer has
contributed to (since that would defeat the purpose of parole mode).

prioritize partial pieces
-------------------------

This setting determines if partially downloaded or requested pieces should
always be preferred over other pieces. The benefit of doing this is that the
number of partial pieces is minimized (and hence the turn-around time for
downloading a block until it can be uploaded to others is minimized). It also
puts less stress on the disk cache, since fewer partial pieces need to be kept
in the cache. Whether or not to enable this is controlled by
session_settings::prioritize_partial_pieces.

The main benefit of not prioritizing partial pieces is that the rarest first
algorithm gets to have more influence on which pieces are picked. The picker is
more likely to truly pick the rarest piece, and hence improving the performance
of the swarm.

This setting is turned on automatically whenever the number of partial pieces
in the piece picker exceeds the number of peers we're connected to times 1.5.
This is in order to keep the waste of partial pieces to a minimum, but still
prefer rarest pieces.

prefer whole pieces
-------------------

The *prefer whole pieces* setting makes the piece picker prefer picking entire
pieces at a time. This is used by web connections (both http seeding
standards), in order to be able to coalesce the small bittorrent requests to
larger HTTP requests. This significantly improves performance when downloading
over HTTP.

It is also used by peers that are downloading faster than a certain threshold.
The main advantage is that these peers will better utilize the other peer's
disk cache, by requesting all blocks in a single piece, from the same peer.

This threshold is controlled by session_settings::whole_pieces_threshold.

*TODO: piece affinity by speed category*
*TODO: piece priorities*

SSL torrents
============

Torrents may have an SSL root (CA) certificate embedded in them. Such torrents
are called *SSL torrents*. An SSL torrent talks to all bittorrent peers over
SSL. The protocols are layered like this::

	+-----------------------+
	| BitTorrent protocol   |
	+-----------------------+
	| SSL                   |
	+-----------+-----------+
	| TCP       | uTP       |
	|           +-----------+
	|           | UDP       |
	+-----------+-----------+

During the SSL handshake, both peers need to authenticate by providing a
certificate that is signed by the CA certificate found in the .torrent file.
These peer certificates are expected to be privided to peers through some other
means than bittorrent. Typically by a peer generating a certificate request
which is sent to the publisher of the torrent, and the publisher returning a
signed certificate.

In libtorrent, set_ssl_certificate() in torrent_handle is used to tell
libtorrent where to find the peer certificate and the private key for it. When
an SSL torrent is loaded, the torrent_need_cert_alert is posted to remind the
user to provide a certificate.

A peer connecting to an SSL torrent MUST provide the *SNI* TLS extension
(server name indication). The server name is the hex encoded info-hash of the
torrent to connect to. This is required for the client accepting the connection
to know which certificate to present.

SSL connections are accepted on a separate socket from normal bittorrent
connections. To pick which port the SSL socket should bind to, set
session_settings::ssl_listen to a different port. It defaults to port 4433.
This setting is only taken into account when the normal listen socket is opened
(i.e. just changing this setting won't necessarily close and re-open the SSL
socket). To not listen on an SSL socket at all, set ``ssl_listen`` to 0.

This feature is only available if libtorrent is build with openssl support
(``TORRENT_USE_OPENSSL``) and requires at least openSSL version 1.0, since it
needs SNI support.

Peer certificates must have at least one *SubjectAltName* field of type
dNSName. At least one of the fields must *exactly* match the name of the
torrent. This is a byte-by-byte comparison, the UTF-8 encoding must be
identical (i.e. there's no unicode normalization going on). This is the
recommended way of verifying certificates for HTTPS servers according to `RFC
2818`_. Note the difference that for torrents only *dNSName* fields are taken
into account (not IP address fields). The most specific (i.e. last) *Common
Name* field is also taken into account if no *SubjectAltName* did not match.

If any of these fields contain a single asterisk ("*"), the certificate is
considered covering any torrent, allowing it to be reused for any torrent.

The purpose of matching the torrent name with the fields in the peer
certificate is to allow a publisher to have a single root certificate for all
torrents it distributes, and issue separate peer certificates for each torrent.
A peer receiving a certificate will not necessarily be able to access all
torrents published by this root certificate (only if it has a "star cert").

.. _`RFC 2818`: http://www.ietf.org/rfc/rfc2818.txt

testing
-------

To test incoming SSL connections to an SSL torrent, one can use the following
*openssl* command::

	openssl s_client -cert <peer-certificate>.pem -key <peer-private-key>.pem -CAfile \
	   <torrent-cert>.pem -debug -connect 127.0.0.1:4433 -tls1 -servername <info-hash>

To create a root certificate, the Distinguished Name (*DN*) is not taken into
account by bittorrent peers. You still need to specify something, but from
libtorrent's point of view, it doesn't matter what it is. libtorrent only makes
sure the peer certificates are signed by the correct root certificate.

One way to create the certificates is to use the ``CA.sh`` script that comes
with openssl, like thisi (don't forget to enter a common Name for the
certificate)::

	CA.sh -newca
	CA.sh -newreq
	CA.sh -sign

The torrent certificate is located in ``./demoCA/private/demoCA/cacert.pem``,
this is the pem file to include in the .torrent file.

The peer's certificate is located in ``./newcert.pem`` and the certificate's
private key in ``./newkey.pem``.

