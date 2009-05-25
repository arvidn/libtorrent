=================
libtorrent manual
=================

:Author: Arvid Norberg, arvid@rasterbar.com
:Version: 0.15.0

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

tuning libtorrent
=================

libtorrent expose most constants used in the bittorrent engine for
customization through the ``session_settings``. This makes it possible to
test and tweak the parameters for certain algorithms to make a client
that fits a wide range of needs. From low memory embedded devices to
servers seeding thousands of torrents. The default settings in libtorrent
are tuned for an end-user bittorrent client running on a normal desktop
computer.

This document describes techniques to benchmark libtorrent performance
and how parameters are likely to affect it.

reducing memory footprint
=========================

These are things you can do to reduce the memory footprint of libtorrent. You get
some of this by basing your default ``session_settings`` on the ``min_memory_usage()``
setting preset function.

Keep in mind that lowering memory usage will affect performance, always profile
and benchmark your settings to determine if it's worth the trade-off.

The typical buffer usage of libtorrent, for a single download, with the cache
size set to 256 blocks (256 * 16 kiB = 4 MiB) is::

	read cache:      128.6 (2058 kiB)
	write cache:     103.5 (1656 kiB)
	receive buffers: 7.3   (117 kiB)
	send buffers:    4.8   (77 kiB)
	hash temp:       0.001 (19 Bytes)

The receive buffers is proportional to the number of connections we make, and is
limited by the total number of connections in the session (default is 200).

The send buffers is proportional to the number of upload slots that are allowed
in the session. The default is auto configured based on the observed upload rate.

The read and write cache can be controlled (see section below).

The "hash temp" entry size depends on whether or not hashing is optimized for
speed or memory usage. In this test run it was optimized for memory usage.

disable disk cache
------------------

The bulk of the memory libtorrent will use is used for the disk cache. To save
the absolute most amount of memory, you can disable the the cache by setting
``session_settings::cache_size`` to 0. You might want to consider using the cache
but just disable caching read operations. You do this by settings
``session_settings::use_read_cache`` to false. This is the main factor in how much
memory will be used by the client. Keep in mind that you will degrade performance
by disabling the cache. You should benchmark the disk access in order to make an
informed trade-off.

remove torrents
---------------

Torrents that have been added to libtorrent will inevitably use up memory, event
when it's paused. A paused torrent will not use any peer connection objects or
any send or receive buffers though. Any added torrent holds the entire .torrent
file in memory, it also remembers the entire list of peers that it's heard about
(which can be fairly long unless it's capped). It also retains information about
which blocks and pieces we have on disk, which can be significant for torrents
with many pieces.

If you need to minimize the memory footprint, consider removing torrents from
the session rather than pausing them. This will likely only make a difference
when you have a very large number of torrents in a session.

The downside of removing them is that they will no longer be auto-managed. Paused
auto managed torrents are scraped periodically, to determine which torrents are
in the greatest need of seeding, and libtorrent will prioritize to seed those.

socket buffer sizes
-------------------

You can make libtorrent explicitly set the kernel buffer sizes of all its peer
sockets. If you set this to a low number, you may see reduced throughput, especially
for high latency connections. It is however an oportunity to save memory per
connection, and might be worth considering if you have a very large number of
peer connections. This memory will not be visible in your process, this sets
the amount of kernel memory is used for your sockets.

Change this by setting ``session_settings::recv_socket_buffer_size`` and
``session_settings::send_socket_buffer_size``.

peer list size
--------------

The default maximum for the peer list is 4000 peers. For IPv4 peers, each peer
entry uses 32 bytes, which ends up using 128 kB per torrent. If seeding 4 popular
torrents, the peer lists alone uses about half a megabyte.

The default limit is the same for paused torrents as well, so if you have a
large number of paused torrents (that are popular) it will be even more
significant.

If you're short of memory, you should consider lowering the limit. 500 is probably
enough. You can do this by setting ``session_settings::max_peerlist_size`` to
the max number of peers you want in the torrent's peer list.

You should also lower the same limit but for paused torrents. It might even make sense
to set that even lower, since you only need a few peers to start up while waiting
for the tracker and DHT to give you fresh ones. The max peer list size for paused
torrents is set by ``session_settings::max_paused_peerlist_size``.

The drawback of lowering this number is that if you end up in a position where
the tracker is down for an extended period of time, your only hope of finding live
peers is to go through your list of all peers you've ever seen. Having a large
peer list will also help increase performance when starting up, since the torrent
can start connecting to peers in parallel with connecting to the tracker.

send buffer watermark
---------------------

The send buffer watermark controls when libtorrent will ask the disk I/O thread
to read blocks from disk, and append it to a peer's send buffer.

When the send buffer has fewer than or equal number of bytes as
``session_settings::send_buffer_watermark``, the peer will ask the disk I/O thread
for more data to send. The trade-off here is between wasting memory by having too
much data in the send buffer, and hurting send rate by starving out the socket,
waiting for the disk read operation to complete.

If your main objective is memory usage and you're not concerned about being able
to achieve high send rates, you can set the watermark to 9 bytes. This will guarantee
that no more than a single (16 kiB) block will be on the send buffer at a time, for
all peers. This is the least amount of memory possible for the send buffer.

You should benchmark your max send rate when adjusting this setting. If you have
a very fast disk, you are less likely see a performance hit.

optimize hashing for memory usage
---------------------------------

When libtorrent is doing hash checks of a file, or when it re-reads a piece that
was just completed to verify its hash, there are two options. The default one
is optimized for speed, which allocates buffers for the entire piece, reads in
the whole piece in one read call, then hashes it.

The second option is to optimize for memory usage instead, where a single buffer
is allocated, and the piece is read one block at a time, hashing it as each
block is read from the file. For low memory environments, this latter approach
is recommended. Change this by settings ``session_settings::optimize_hashing_for_speed``
to false. This will significantly reduce peak memory usage, especially for
torrents with very large pieces.

reduce executable size
----------------------

Compilers generally add a significant number of bytes to executables that make use
of C++ exceptions. By disabling exceptions (-fno-exceptions on GCC), you can
reduce the executable size with up to 45%.

Also make sure to optimize for size when compiling.

play nice with the disk
=======================

When checking a torrent, libtorrent will try to read as fast as possible from the disk.
The only thing that might hold it back is a CPU that is slow at calculating SHA-1 hashes,
but typically the file checking is limited by disk read speed. Most operating systems
today do not prioritize disk access based on the importance of the operation, this means
that checking a torrent might delay other disk accesses, such as virtual memory swapping
or just loading file by other (interactive) applications.

In order to play nicer with the disk, and leave some spare time for it to service other
processes that might be of higher importance to the end-user, you can introduce a sleep
between the disc accesses. This is a direct tradeoff between how fast you can check a
torrent and how soft you will hit the disk.

You control this by setting the ``session_settings::file_checks_delay_per_block`` to greater
than zero. This number is the number of milliseconds to sleep between each read of 16 kiB.

The sleeps are not necessarily in between each 16 kiB block (it might be read in larger chunks),
but the number will be multiplied by the number of blocks that were read, to maintain the
same semantics.

benchmarking
============

disk cache stats
----------------

disk access stats
-----------------

session stats
-------------

