============================
libtorrent API Documentation
============================

.. include:: header.rst

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

Each class and function is described in this manual, you may want to have a
look at the tutorial_ as well.

.. _tutorial: tutorial.html

For a description on how to create torrent files, see create_torrent.

.. _make_torrent: make_torrent.html

forward declarations
====================

Forward declaring types from the libtorrent namespace is discouraged as it may
break in future releases. Instead include ``libtorrent/fwd.hpp`` for forward
declarations of all public types in libtorrent.

trouble shooting
================

A common problem developers are facing is torrents stopping without explanation.
Here is a description on which conditions libtorrent will stop your torrents,
how to find out about it and what to do about it.

Make sure to keep track of the paused state, the error state and the upload
mode of your torrents. By default, torrents are auto-managed, which means
libtorrent will pause, resume, scrape them and take them out
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

For a more detailed guide on how to trouble shoot performance issues, see
troubleshooting_

.. _troubleshooting: troubleshooting.html

ABI considerations
==================

libtorrent maintains a stable ABI for versions with the same major and minor versions.

e.g. libtorrent-1.2.0 is ABI compatible with libtorrent-1.2.1 but not with libtorrent-1.1

network primitives
==================

There are a few typedefs in the ``libtorrent`` namespace which pulls
in network types from the ``boost::asio`` namespace. These are::

	using address = boost::asio::ip::address;
	using address_v4 = boost::asio::ip::address_v4;
	using address_v6 = boost::asio::ip::address_v6;
	using boost::asio::ip::tcp;
	using boost::asio::ip::udp;

These are declared in the ``<libtorrent/socket.hpp>`` header.

The ``using`` statements will give easy access to::

	tcp::endpoint
	udp::endpoint

Which are the endpoint types used in libtorrent. An endpoint is an address
with an associated port.

For documentation on these types, please refer to the `asio documentation`_.

.. _`asio documentation`: https://www.boost.org/doc/libs/1_66_0/doc/html/boost_asio.html

exceptions
==========

Many functions in libtorrent have two versions, one that throws exceptions on
errors and one that takes an ``error_code`` reference which is filled with the
error code on errors.

On exceptions, libtorrent will throw ``boost::system::system_error`` exceptions
carrying an ``error_code`` describing the underlying error.

translating error codes
-----------------------

The error_code::message() function will typically return a localized error string,
for system errors. That is, errors that belong to the generic or system category.

Errors that belong to the libtorrent error category are not localized however, they
are only available in English. In order to translate libtorrent errors, compare the
error category of the ``error_code`` object against ``lt::libtorrent_category()``,
and if matches, you know the error code refers to the list above. You can provide
your own mapping from error code to string, which is localized. In this case, you
cannot rely on ``error_code::message()`` to generate your strings.

The numeric values of the errors are part of the API and will stay the same, although
new error codes may be appended at the end.

Here's a simple example of how to translate error codes:

.. code:: c++

	std::string error_code_to_string(boost::system::error_code const& ec)
	{
		if (ec.category() != lt::libtorrent_category())
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

In order to download *just* the metadata (.torrent file) from a magnet link, set
file priorities to 0 in add_torrent_params::file_priorities. It's OK to set the
priority for more files than what is in the torrent. It may not be trivial to
know how many files a torrent has before the metadata has been downloaded.
Additional file priorities will be ignored. By setting a large number of files
to priority 0, chances are that they will all be set to 0 once the metadata is
received (and we know how many files there are).

In this case, when the metadata is received from the swarm, the torrent will
still be running, but it will disconnect the majority of peers (since connections
to peers that already have the metadata are redundant). It will keep seeding the
*metadata* only.

queuing
=======

libtorrent supports *queuing*. Queuing is a mechanism to automatically pause and
resume torrents based on certain criteria. The criteria depends on the overall
state the torrent is in (checking, downloading or seeding).

To opt-out of the queuing logic, make sure your torrents are added with the
torrent_flags::auto_managed bit *cleared* from ``add_torrent_params::flags``.
Or call ``torrent_handle::unset_flags(torrent_flags::auto_managed)`` on the
torrent handle.

The overall purpose of the queuing logic is to improve performance under arbitrary
torrent downloading and seeding load. For example, if you want to download 100
torrents on a limited home connection, you improve performance by downloading
them one at a time (or maybe two at a time), over downloading them all in
parallel. The benefits are:

* the average completion time of a torrent is half of what it would be if all
  downloaded in parallel.
* The amount of upload capacity is more likely to reach the *reciprocation rate*
  of your peers, and is likely to improve your *return on investment* (download
  to upload ratio)
* your disk I/O load is likely to be more local which may improve I/O
  performance and decrease fragmentation.

There are fundamentally 3 separate queues:

* checking torrents
* downloading torrents
* seeding torrents

Every torrent that is not seeding has a queue number associated with it, this is
its place in line to be started. See torrent_status::queue_position.

On top of the limits of each queue, there is an over arching limit, set in
settings_pack::active_limit. The auto manager will never start more than this
number of torrents (with one exception described below). Non-auto-managed
torrents are exempt from this logic, and not counted.

At a regular interval, torrents are checked if there needs to be any
re-ordering of which torrents are active and which are queued. This interval
can be controlled via settings_pack::auto_manage_interval.

For queuing to work, resume data needs to be saved and restored for all
torrents. See torrent_handle::save_resume_data().

queue position
--------------

The torrents in the front of the queue are started and the rest are ordered by
their queue position. Any newly added torrent is placed at the end of the queue.
Once a torrent is removed or turns into a seed, its queue position is -1 and all
torrents that used to be after it in the queue, decreases their position in
order to fill the gap.

The queue positions are always contiguous, in a sequence without any gaps.

Lower queue position means closer to the front of the queue, and will be
started sooner than torrents with higher queue positions.

To query a torrent for its position in the queue, or change its position, see:
torrent_handle::queue_position(), torrent_handle::queue_position_up(),
torrent_handle::queue_position_down(), torrent_handle::queue_position_top()
and torrent_handle::queue_position_bottom().

checking queue
--------------

The checking queue affects torrents in the torrent_status::checking or
torrent_status::allocating state that are auto-managed.

The checking queue will make sure that (of the torrents in its queue) no more than
settings_pack::active_checking_limit torrents are started at any given time.
Once a torrent completes checking and moves into a different state, the next in
line will be started for checking.

Any torrent added force-started or force-stopped (i.e. the auto managed flag is
*not* set), will not be subject to this limit and they will all check
independently and in parallel.

Once a torrent completes the checking of its files, or resume data, it will
be put in the queue for downloading and potentially start downloading immediately.
In order to add a torrent and check its files without starting the download, it
can be added in ``stop_when_ready`` mode.
See add_torrent_params::flag_stop_when_ready. This flag will stop the torrent
once it is ready to start downloading.

This is conceptually the same as waiting for the ``torrent_checked_alert`` and
then call::

	h.auto_managed(false);
	h.pause();

With the important distinction that it entirely avoids the brief window where
the torrent is in downloading state.

downloading queue
-----------------

Similarly to the checking queue, the downloading queue will make sure that no
more than settings_pack::active_downloads torrents are in the downloading
state at any given time.

The torrent_status::queue_position is used again here to determine who is next
in line to be started once a downloading torrent completes or is stopped/removed.

seeding queue
-------------

The seeding queue does not use torrent_status::queue_position to determine which
torrent to seed. Instead, it estimates the *demand* for the torrent to be
seeded. A torrent with few other seeds and many downloaders is assumed to have a
higher demand of more seeds than one with many seeds and few downloaders.

It limits the number of started seeds to settings_pack::active_seeds.

On top of this basic bias, *seed priority* can be controller by specifying a
seed ratio (the upload to download ratio), a seed-time ratio (the download
time to seeding time ratio) and a seed-time (the absolute time to be seeding a
torrent). Until all those targets are hit, the torrent will be prioritized for
seeding.

Among torrents that have met their seed target, torrents where we don't know of
any other seed take strict priority.

In order to avoid flapping, torrents that were started less than 30 minutes ago
also have priority to keep seeding.

Finally, for torrents where none of the above apply, they are prioritized based
on the download to seed ratio.

The relevant settings to control these limits are
settings_pack::share_ratio_limit, settings_pack::seed_time_ratio_limit and
settings_pack::seed_time_limit.

queuing options
---------------

In addition to simply starting and stopping torrents, the queuing mechanism can
have more fine grained control of the resources used by torrents.

half-started torrents
.....................

In addition to the downloading and seeding limits, there are limits on *actions*
torrents perform. The downloading and seeding limits control whether peers are
allowed at all, and if peers are not allowed, torrents are stopped and don't do
anything. If peers are allowed, torrents may:

1. announce to trackers
2. announce to the DHT
3. announce to local peer discovery (local service discovery)

Each of those actions are associated with a cost and hence may need a separate
limit. These limits are controlled by settings_pack::active_tracker_limit,
settings_pack::active_dht_limit and settings_pack::active_lsd_limit
respectively.

Specifically, announcing to a tracker is typically cheaper than
announcing to the DHT. settings_pack::active_dht_limit will limit the number of
torrents that are allowed to announce to the DHT. The highest priority ones
will, and the lower priority ones won't. The will still be considered started
though, and any incoming peers will still be accepted.

If you do not wish to impose such limits (basically, if you do not wish to have
half-started torrents) make sure to set these limits to -1 (infinite).

prefer seeds
............

In the case where ``active_downloads`` + ``active_seeds`` > ``active_limit``,
there's an ambiguity whether the downloads should be satisfied first or the
seeds. To disambiguate this case, the settings_pack::auto_manage_prefer_seeds
determines whether seeds are preferred or not.

inactive torrents
.................

Torrents that are not transferring any bytes (downloading or uploading) have a
relatively low cost to be started. It's possible to exempt such torrents from
the download and seed queues by setting settings_pack::dont_count_slow_torrents
to true.

Since it sometimes may take a few minutes for a newly started torrent to find
peers and be unchoked, or find peers that are interested in requesting data,
torrents are not considered inactive immediately. There must be an extended
period of no transfers before it is considered inactive and exempt from the
queuing limits.

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

To use the fast-resume data you pass it to read_resume_data(), which will return
an add_torrent_params object. Fields of this object can then be altered before
passing it to async_add_torrent() or add_torrent().
The session will then skip the time consuming checks. It may have to do
the checking anyway, if the fast-resume data is corrupt or doesn't fit the
storage for that torrent.

file format
-----------

The file format is a bencoded dictionary containing the following fields:

+--------------------------+--------------------------------------------------------------+
| ``file-format``          | string: "libtorrent resume file"                             |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``info-hash``            | string, the info hash of the torrent this data is saved for. |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``pieces``               | A string with piece flags, one character per piece.          |
|                          | Bit 1 means we have that piece.                              |
|                          | Bit 2 means we have verified that this piece is correct.     |
|                          | This only applies when the torrent is in seed_mode.          |
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
| ``last_upload``          | integer. The number of seconds since epoch when we last      |
|                          | uploaded payload to a peer on this torrent.                  |
+--------------------------+--------------------------------------------------------------+
| ``last_download``        | integer. The number of seconds since epoch when we last      |
|                          | downloaded payload from a peer on this torrent.              |
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
|                          | The URLs are expected to be properly encoded and not contain |
|                          | any illegal url characters.                                  |
+--------------------------+--------------------------------------------------------------+
| ``httpseeds``            | list of strings. List of HTTP seed URLs used by this torrent.|
|                          | The URLs are expected to be properly encoded and not contain |
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
| ``peers``                | string. This string contains IPv4 and port pairs of peers we |
|                          | were connected to last session. The endpoints are in compact |
|                          | representation. 4 bytes IPv4 address followed by 2 bytes     |
|                          | port. Hence, the length of this string should be divisible   |
|                          | by 6.                                                        |
+--------------------------+--------------------------------------------------------------+
| ``banned_peers``         | string. This string has the same format as ``peers`` but     |
|                          | instead represent IPv4 peers that we have banned.            |
+--------------------------+--------------------------------------------------------------+
| ``peers6``               | string. This string contains IPv6 and port pairs of peers we |
|                          | were connected to last session. The endpoints are in compact |
|                          | representation. 16 bytes IPv6 address followed by 2 bytes    |
|                          | port. The length of this string should be divisible by 18.   |
+--------------------------+--------------------------------------------------------------+
| ``banned_peers6``        | string. This string has the same format as ``peers6`` but    |
|                          | instead represent IPv6 peers that we have banned.            |
+--------------------------+--------------------------------------------------------------+
| ``info``                 | If this field is present, it should be the info-dictionary   |
|                          | of the torrent this resume data is for. Its SHA-1 hash must  |
|                          | match the one in the ``info-hash`` field. When present,      |
|                          | the torrent is loaded from here, meaning the torrent can be  |
|                          | added purely from resume data (no need to load the .torrent  |
|                          | file separately). This may have performance advantages.      |
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
| ``allocation``           | The allocation mode for the storage. Can be either           |
|                          | ``allocate`` or ``sparse``.                                  |
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
   with zeros. This delay is linear to the size of the download.

 * The download may occupy unnecessary disk space between download sessions.

 * Disk caches usually perform poorly with random access to large files
   and may slow down the download some.

The benefits of this mode are:

 * Downloaded pieces are written directly to their final place in the files and
   the total number of disk operations will be fewer and may also play nicer to
   the filesystem file allocation, and reduce fragmentation.

 * No risk of a download failing because of a full disk during download, once
   all files have been created.

HTTP seeding
============

There are two kinds of HTTP seeding. One with that assumes a smart (and polite)
client and one that assumes a smart server. These are specified in `BEP 19`_
and `BEP 17`_ respectively.

libtorrent supports both. In the libtorrent source code and API, BEP 19 URLs
are typically referred to as *url seeds* and BEP 17 URLs are typically referred
to as *HTTP seeds*.

The libtorrent implementation of `BEP 19`_ assumes that, if the URL ends with a
slash ('/'), the filename should be appended to it in order to request pieces
from that file. The way this works is that if the torrent is a single-file
torrent, only that filename is appended. If the torrent is a multi-file
torrent, the torrent's name '/' the file name is appended. This is the same
directory structure that libtorrent will download torrents into.

There is limited support for HTTP redirects. In case some files are redirected
to *different hosts*, the files must be piece aligned or padded to be piece
aligned.

.. _`BEP 17`: https://www.bittorrent.org/beps/bep_0017.html
.. _`BEP 19`: https://www.bittorrent.org/beps/bep_0019.html

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
vector of contiguous memory in RAM, for optimal memory locality and to eliminate
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
settings_pack::initial_picker_threshold.

reverse order
-------------

An orthogonal setting is *reverse order*, which is used for *snubbed* peers.
Snubbed peers are peers that appear very slow, and might have timed out a piece
request. The idea behind this is to make all snubbed peers more likely to be
able to do download blocks from the same piece, concentrating slow peers on as
few pieces as possible. The reverse order means that the most common pieces are
picked, instead of the rarest pieces (or in the case of sequential download,
the last pieces, instead of the first).

parole mode
-----------

Peers that have participated in a piece that failed the hash check, may be put
in *parole mode*. This means we prefer downloading a full piece  from this
peer, in order to distinguish which peer is sending corrupt data. Whether to do
this is or not is controlled by settings_pack::use_parole_mode.

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
setting_pack::prioritize_partial_pieces.

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

This threshold is controlled by the settings_pack::whole_pieces_threshold
setting.

*TODO: piece priorities*

Multi-homed hosts
=================

The settings_pack::listen_interfaces setting is used to specify which interfaces/IP addresses
to listen on, and accept incoming connections via.

Each item in ``listen_interfaces`` is an IP address or a device name, followed
by a listen port number. Each item (called ``listen_socket_t``) will have the
following objects associated with it:

* a listen socket accepting incoming TCP connections
* a UDP socket:
  1. to accept incoming uTP connections
  2. to run a DHT instance on
  3. to announce to UDP trackers from
  4. a SOCKS5 UDP tunnel (if applicable)
* a listen address and netmask, describing the network the sockets are bound to
* a Local service discovery object, broadcasting to the specified subnet
* a NAT-PMP/PCP port mapper (if applicable), to map ports on the gateway
  for the specified subnet.
* a UPnP port mapper (if applicable), to map ports on any
* ``InternetGatewayDevice`` found on the specified local subnet.

A ``listen_socket_t`` item may be specified to only be a local network (with
the ``l`` suffix). Such listen socket will only be used to talk to peers and
trackers within the same local network. The netmask defining the network is
queried from the operating system by enumerating network interfaces.

An item that's considered to be "local network" will not be used to announce to
trackers outside of that network. For example, ``10.0.0.2:6881l`` is marked as "local
network" and it will only be used as the source address announcing to a tracker
if the tracker is also within the same local network (e.g. ``10.0.0.0/8``).

If an IP address is the *unspecified* address (i.e. ``0.0.0.0`` or ``::``),
libtorrent will enumerate all addresses it can find for the corresponding
address family. If a device name is specified instead of an IP, it will expand
to all IP addresses associated with that device.

Listen IP addresses that are automatically expanded by libtorrent have some
special rules. They are all assumed to be restricted to be "local network"
unless the following conditions are met:

* the IP address is not in a known link-local range
* the IP address is not in a known loopback range
* the item the IP address was expanded from was not marked local (``l``)
* the IP address is in a globally reachable IP address range OR the routing
  table contains a default route with a gateway for the corresponding network.
  This bullet only applies when expanding from an unspecified IP address. When
  explicitly specifying a device, we don't need to find a route to treat it as
  external.

The NAT-PMP/PCP and UPnP port mapper objects are only created for networks that
are expected to be externally available (i.e. not "local network"). If there are
multiple subnets connected to the internet, they will each have a separate
gateway, and separate port mappings.


default routes
--------------

This section describes the logic for determining whether an address has a
default route associated with it or not. This is only used for listen addresses that
are *expanded* from either an unspecified listen address (``0.0.0.0`` or ``::``)
or from a device name (e.g. ``eth0``).

A network is considered having a default route if there is a default route with
a matching egress network device name and address family.

routing
-------

A ``listen_socket_t`` item can route to a destination address if any of these
hold:

* the destination address falls inside its subnet (i.e. interface address masked
  by netmask is the same as the destination address masked by the netmask).
* the ``listen_socket_t`` does not have the "local network" flag set, and the
  address family matches the destination address.

The ability to route to an address is used when determining whether to announce
to a tracker from a ``listen_socket_t`` and whether to open a SOCKS5 UDP tunnel
for a ``listen_socket_t``.

tracker announces
-----------------

Trackers are announced to from all network interfaces listening for incoming
connections. However, interfaces that cannot be used to reach the tracker, such
as loopback, are not used as the source address for announces. A
``listen_socket_t`` item that can route to at least one of the tracker IP
addresses will be used as the source address for an announce. Each such item
will also have an announce_endpoint item associated with it, in the tracker
list.

If a tracker can be reached on a loopback address, then the loopback interface
*will* be used to announce to that tracker. But under normal circumstances,
loopback will not be used for announcing to trackers.

For more details, see `BEP 7`_.

.. _`BEP 7`: https://www.bittorrent.org/beps/bep_0007.html

SOCKS5 UDP tunnels
------------------

When using a SOCKS5 proxy, each interface that can route to one of the SOCKS5
proxy's addresses will be used to open a UDP tunnel, via that proxy. For
example, if a client has both IPv4 and IPv6 connectivity, but the socks5 proxy
only resolves to IPv4, only the IPv4 address will have a UDP tunnel. In that case,
the IPv6 connection will not be used, since it cannot use the proxy.

predictive piece announce
=========================

In order to improve performance, libtorrent supports a feature called
``predictive piece announce``. When enabled, it will make libtorrent announce
that we have pieces to peers, before we truly have them. The most important
case is to announce a piece as soon as it has been downloaded and passed the
hash check, but not yet been written to disk. In this case, there is a risk the
piece will fail to be written to disk, in which case we won't have the piece
anymore, even though we announced it to peers.

The other case is when we're very close to completing the download of a piece
and assume it will pass the hash check, we can announce it to peers to make it
available one round-trip sooner than otherwise. This lets libtorrent start
uploading the piece to interested peers immediately when the piece complete,
instead of waiting one round-trip for the peers to request it.

This makes for the implementation slightly more complicated, since piece will
have more states and more complicated transitions. For instance, a piece could
be:

1. hashed but not fully written to disk
2. fully written to disk but not hashed
3. not fully downloaded
4. downloaded and hash checked

Once a piece is fully downloaded, the hash check could complete before any of
the write operations or it could complete after all write operations are
complete.

peer classes
============

The peer classes feature in libtorrent allows a client to define custom groups
of peers and rate limit them individually. Each such group is called a *peer
class*. There are a few default peer classes that are always created:

* global - all peers belong to this class, except peers on the local network
* local peers - all peers on the local network belongs to this class TCP peers
* tcp class - all peers connected over TCP belong to this class

The TCP peers class is used by the uTP/TCP balancing logic, if it's enabled, to
throttle TCP peers. The global and local classes are used to adjust the global
rate limits.

When the rate limits are adjusted for a specific torrent, a class is created
implicitly for that torrent.

The default peer class IDs are defined as enums in the ``session`` class:

.. code:: c++

	enum {
		global_peer_class_id,
		tcp_peer_class_id,
		local_peer_class_id
	};

The default peer classes are automatically created on session startup, and
configured to apply to each respective type of connection. There's nothing
preventing a client from reconfiguring the peer class ip- and type filters
to disable or customize which peers they apply to. See set_peer_class_filter()
and set_peer_class_type_filter().

A peer class can be considered a more general form of *labels* that some
clients have. Peer classes however are not just applied to torrents, but
ultimately the peers.

Peer classes can be created with the create_peer_class() call (on the session
object), and deleted with the delete_peer_class() call.

Peer classes are configured with the set_peer_class() get_peer_class() calls.

Custom peer classes can be assigned based on the peer's IP address or the type
of transport protocol used. See set_peer_class_filter() and
set_peer_class_type_filter() for more information.

peer class examples
-------------------

Here are a few examples of common peer class operations.

To make the global rate limit apply to local peers as well, update the IP-filter
based peer class assignment:

.. code:: c++

		std::uint32_t const mask = 1 << lt::session::global_peer_class_id;
		ip_filter f;

		// for every IPv4 address, assign the global peer class
		f.add_rule(make_address("0.0.0.0"), make_address("255.255.255.255"), mask);

		// for every IPv6 address, assign the global peer class
		f.add_rule(make_address("::")
			, make_address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")
			, mask);
		ses.set_peer_class_filter(f);

To make uTP sockets exempt from rate limiting:

.. code:: c++

	peer_class_type_filter flt = ses.get_peer_class_type_filter();
	// filter out the global and local peer class for uTP sockets, if these
	// classes are set by the IP filter
	flt.disallow(peer_class_type_filter::utp_socket, session::global_peer_class_id);
	flt.disallow(peer_class_type_filter::utp_socket, session::local_peer_class_id);

	// this filter should not add the global or local peer class to utp sockets
	flt.remove(peer_class_type_filter::utp_socket, session::global_peer_class_id);
	flt.remove(peer_class_type_filter::utp_socket, session::local_peer_class_id);

	ses.set_peer_class_type_filter(flt);

To make all peers on the internal network not subject to throttling:

.. code:: c++

		std::uint32_t const mask = 1 << lt::session::global_peer_class_id;
		ip_filter f;

		// for every IPv4 address, assign the global peer class
		f.add_rule(make_address("0.0.0.0"), make_address("255.255.255.255"), mask);

		// for every address on the local metwork, set the mask to 0
		f.add_rule(make_address("10.0.0.0"), make_address("10.255.255.255"), 0);
		ses.set_peer_class_filter(f);

SSL torrents
============

Torrents may have an SSL root (CA) certificate embedded in them. Such torrents
are called *SSL torrents*. An SSL torrent talks to all bittorrent peers over
SSL. The protocols are layered like this:

.. image:: utp_stack.png

During the SSL handshake, both peers need to authenticate by providing a
certificate that is signed by the CA certificate found in the .torrent file.
These peer certificates are expected to be provided to peers through some other
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
connections. To enable support for SSL torrents, add a listen interface to the
settings_pack::listen_interfaces setting with the ``s`` suffix. For example::

	0.0.0.0:6881,0.0.0.0:6882s

That will listen for normal bittorrent connections on port 6881 and for SSL
torrent connections on port 6882.

This feature is only available if libtorrent is build with openssl support
(``TORRENT_USE_OPENSSL``) and requires at least OpenSSL version 1.0, since it
needs SNI support.

Peer certificates must have at least one *SubjectAltName* field of type
DNSName. At least one of the fields must *exactly* match the name of the
torrent. This is a byte-by-byte comparison, the UTF-8 encoding must be
identical (i.e. there's no unicode normalization going on). This is the
recommended way of verifying certificates for HTTPS servers according to `RFC
2818`_. Note the difference that for torrents only *DNSName* fields are taken
into account (not IP address fields). The most specific (i.e. last) *Common
Name* field is also taken into account if no *SubjectAltName* did not match.

If any of these fields contain a single asterisk ("*"), the certificate is
considered covering any torrent, allowing it to be reused for any torrent.

The purpose of matching the torrent name with the fields in the peer
certificate is to allow a publisher to have a single root certificate for all
torrents it distributes, and issue separate peer certificates for each torrent.
A peer receiving a certificate will not necessarily be able to access all
torrents published by this root certificate (only if it has a "star cert").

.. _`RFC 2818`: https://www.ietf.org/rfc/rfc2818.txt

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
with openssl, like this (don't forget to enter a common Name for the
certificate)::

	CA.sh -newca
	CA.sh -newreq
	CA.sh -sign

The torrent certificate is located in ``./demoCA/private/demoCA/cacert.pem``,
this is the pem file to include in the .torrent file.

The peer's certificate is located in ``./newcert.pem`` and the certificate's
private key in ``./newkey.pem``.

session statistics
==================

libtorrent provides a mechanism to query performance and statistics counters
from its internals.

The statistics consists of two fundamental types. *counters* and *gauges*. A
counter is a monotonically increasing value, incremented every time some event
occurs. For example, every time the network thread wakes up because a socket
became readable will increment a counter. Another example is every time a
socket receives *n* bytes, a counter is incremented by *n*.

*Counters* are the most flexible of metrics. It allows the program to sample
the counter at any interval, and calculate average rates of increments to the
counter. Some events may be rare and need to be sampled over a longer period in
order to get useful rates, where other events may be more frequent and evenly
distributed that sampling it frequently yields useful values. Counters also
provides accurate overall counts. For example, converting samples of a download
rate into a total transfer count is not accurate and takes more samples.
Converting an increasing counter into a rate is easy and flexible.

*Gauges* measure the instantaneous state of some kind. This is used for metrics
that are not counting events or flows, but states that can fluctuate. For
example, the number of torrents that are currently being downloaded.

It's important to know whether a value is a counter or a gauge in order to
interpret it correctly. In order to query libtorrent for which counters and
gauges are available, call session_stats_metrics(). This will return metadata
about the values available for inspection in libtorrent. It will include
whether a value is a counter or a gauge. The key information it includes is the
index used to extract the actual measurements for a specific counter or gauge.

In order to take a sample, call post_session_stats() in the session object.
This will result in a session_stats_alert being posted. In this alert object,
there is an array of values, these values make up the sample. The value index
in the stats metric indicates which index the metric's value is stored in.

The mapping between metric and value is not stable across versions of
libtorrent. Always query the metrics first, to find out the index at which the
value is stored, before interpreting the values array in the
session_stats_alert. The mapping will *not* change during the runtime of your
process though, it's tied to a specific libtorrent version. You only have to
query the mapping once on startup (or every time ``libtorrent.so`` is loaded,
if it's done dynamically).

The available stats metrics are:

.. include:: stats_counters.rst

glossary
========

The libtorrent documentation use words that are bittorrent terms of art. This
section defines some of these words. For an overview of what bittorrent is and
how it works, see these slides_. For an introduction to the bittorrent DHT, see
`this presentation`_.

.. _slides: bittorrent.pdf
.. _`this presentation`: https://vimeo.com/56044595

announce
	The act of telling a tracker or the DHT network about the existence of
	oneself and how other peers can connect, by specifying port one is listening
	on.

block
	A subset of a piece. Almost always 16 kiB of payload, unless the piece size is
	smaller. This is the granularity file payload is requested from peers on the
	network.

DHT
	The distributed hash table is a cross-swarm, world-wide network of bittorrent
	peers. It's loosely connected, implementing the Kademlia protocol. Its purpose
	is to act as a tracker. Peers can announce their presence to nodes on the DHT
	and other peers can discover them to join the swarm.

HTTP tracker
	A tracker that uses the HTTP protocol for announces.

info dictionary
	The subset of a torrent file that describes piece hashes and file names. This
	is the only mandatory part necessary to join the swarm (network of peers) for
	the torrent.

info hash
	The hash of the info dictionary. This uniquely identifies a torrent and is
	used by the protocol to ensure peers talking to each other agree on which swarm
	they are participating in. Sometimes spelled info-hash.

leecher
	A peer that is still interested in downloading more pieces for the torrent.
	It is not a seed.

magnet link
	A URI containing the info hash for a torrent, allowing peers to join its
	swarm. May optionally contain a display name, trackers and web seeds.
	Typically magnet links rely on peers joining the swarm via the DHT.

metadata
	Synonymous to a torrent file

peer
	A computer running bittorrent client software that participates in the network
	for a particular torrent/set of files.

piece
	The smallest number of bytes that can be validated when downloading (no
	longer the case in bittorrent V2). The smallest part of the files that can be
	advertised to other peers. The size of a piece is determined by the info
	dictionary inside the torrent file.

seed
	A computer running bittorrent client software that has the complete files for
	a specific torrent, able to share any piece for that file with other peers in
	the network

swarm
	The network of peers participating in sharing and downloading of a specific torrent.

torrent
	May refer to a torrent file or the swarm (network of peers) created around
	the torrent file.

torrent file
	A file ending in .torrent describing the content of a set of files (but not
	containing the content). Importantly, it contains hashes of all files, split
	up into pieces. It may optionally contain references to trackers and nodes on
	the DHT network to aid peers in joining the network of peers sharing
	these files.

tracker
	A server peers can announce to and receive other peers back belonging to the
	same swarm. Trackers are used to introduce peers to each other, within a swarm.
	When announcing, the info hash of the torrent is included. Trackers can
	introduce peers to any info-hash that's specified, given other peers also use
	the same tracker. Some trackers restrict which info hashes they support based
	on a white list.

UDP tracker
	A tracker that uses a UDP based protocol for announces.

web seed
	A web server that is acting a seed, providing access to all pieces of all
	files over HTTP. This is an extension that client software may or may not
	support.

