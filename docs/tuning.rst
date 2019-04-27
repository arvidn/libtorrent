=================
libtorrent manual
=================

:Author: Arvid Norberg, arvid@libtorrent.org
:Version: 1.1.13

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

tuning libtorrent
=================

libtorrent expose most parameters used in the bittorrent engine for
customization through the ``settings_pack``. This makes it possible to
test and tweak the parameters for certain algorithms to make a client
that fits a wide range of needs. From low memory embedded devices to
servers seeding thousands of torrents. The default settings in libtorrent
are tuned for an end-user bittorrent client running on a normal desktop
computer.

This document describes techniques to benchmark libtorrent performance
and how parameters are likely to affect it.

profiling
=========

libtorrent is instrumented with a number of counters and gauges you can have
access to via the ``session_stats_alert``. First, enable these alerts in the
alert mask::

	settings_pack p;
	p.set_int(settings_mask::alert_mask, alert::stats_notification);
	ses.apply_settings(p);

Then print alerts to a file::

	std::vector<alert*> alerts;
	ses.pop_alerts(&alerts);

	for (auto* a : alerts) {
		std::cout << a->message() << "\n";
	}

If you want to separate generic alerts from session stats, you can filter on the
alert category in the alert, ``alert::category()``.

The alerts with data will have the type ``session_stats_alert`` and there is one
``session_log_alert`` that will be posted on startup containing the column names
for all metrics. Logging this line will greatly simplify interpreting the output.

The python scrip in ``tools/parse_session_stats.py`` can parse the resulting
file and produce graphs of relevant stats. It requires gnuplot__.

__ http://www.gnuplot.info

reducing memory footprint
=========================

These are things you can do to reduce the memory footprint of libtorrent. You get
some of this by basing your default ``settings_pack`` on the ``min_memory_usage()``
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
the absolute most amount of memory, you can disable the cache by setting
``settings_pack::cache_size`` to 0. You might want to consider using the cache
but just disable caching read operations. You do this by settings
``settings_pack::use_read_cache`` to false. This is the main factor in how much
memory will be used by the client. Keep in mind that you will degrade performance
by disabling the cache. You should benchmark the disk access in order to make an
informed trade-off.

remove torrents
---------------

Torrents that have been added to libtorrent will inevitably use up memory, even
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
for high latency connections. It is however an opportunity to save memory per
connection, and might be worth considering if you have a very large number of
peer connections. This memory will not be visible in your process, this sets
the amount of kernel memory is used for your sockets.

Change this by setting ``settings_pack::recv_socket_buffer_size`` and
``settings_pack::send_socket_buffer_size``.

peer list size
--------------

The default maximum for the peer list is 4000 peers. For IPv4 peers, each peer
entry uses 32 bytes, which ends up using 128 kB per torrent. If seeding 4 popular
torrents, the peer lists alone uses about half a megabyte.

The default limit is the same for paused torrents as well, so if you have a
large number of paused torrents (that are popular) it will be even more
significant.

If you're short of memory, you should consider lowering the limit. 500 is probably
enough. You can do this by setting ``settings_pack::max_peerlist_size`` to
the max number of peers you want in a torrent's peer list. This limit applies per
torrent. For 5 torrents, the total number of peers in peerlists will be 5 times
the setting.

You should also lower the same limit but for paused torrents. It might even make sense
to set that even lower, since you only need a few peers to start up while waiting
for the tracker and DHT to give you fresh ones. The max peer list size for paused
torrents is set by ``settings_pack::max_paused_peerlist_size``.

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
``settings_pack::send_buffer_watermark``, the peer will ask the disk I/O thread
for more data to send. The trade-off here is between wasting memory by having too
much data in the send buffer, and hurting send rate by starving out the socket,
waiting for the disk read operation to complete.

If your main objective is memory usage and you're not concerned about being able
to achieve high send rates, you can set the watermark to 9 bytes. This will guarantee
that no more than a single (16 kiB) block will be on the send buffer at a time, for
all peers. This is the least amount of memory possible for the send buffer.

You should benchmark your max send rate when adjusting this setting. If you have
a very fast disk, you are less likely see a performance hit.

reduce executable size
----------------------

Compilers generally add a significant number of bytes to executables that make use
of C++ exceptions. By disabling exceptions (-fno-exceptions on GCC), you can
reduce the executable size with up to 45%. In order to build without exception
support, you need to patch parts of boost.

Also make sure to optimize for size when compiling.

Another way of reducing the executable size is to disable code that isn't used.
There are a number of ``TORRENT_*`` macros that control which features are included
in libtorrent. If these macros are used to strip down libtorrent, make sure the same
macros are defined when building libtorrent as when linking against it. If these
are different the structures will look different from the libtorrent side and from
the client side and memory corruption will follow.

One, probably, safe macro to define is ``TORRENT_NO_DEPRECATE`` which removes all
deprecated functions and struct members. As long as no deprecated functions are
relied upon, this should be a simple way to eliminate a little bit of code.

For all available options, see the `building libtorrent`_ secion.

.. _`building libtorrent`: building.html

high performance seeding
========================

In the case of a high volume seed, there are two main concerns. Performance and scalability.
This translates into high send rates, and low memory and CPU usage per peer connection.

file pool
---------

libtorrent keeps an LRU file cache. Each file that is opened, is stuck in the cache. The main
purpose of this is because of anti-virus software that hooks on file-open and file close to
scan the file. Anti-virus software that does that will significantly increase the cost of
opening and closing files. However, for a high performance seed, the file open/close might
be so frequent that it becomes a significant cost. It might therefore be a good idea to allow
a large file descriptor cache. Adjust this though ``settings_pack::file_pool_size``.

Don't forget to set a high rlimit for file descriptors in your process as well. This limit
must be high enough to keep all connections and files open.

disk cache
----------

You typically want to set the cache size to as high as possible. The
``settings_pack::cache_size`` is specified in 16 kiB blocks. Since you're seeding,
the cache would be useless unless you also set ``settings_pack::use_read_cache``
to true.

In order to increase the possibility of read cache hits, set the
``settings_pack::cache_expiry`` to a large number. This won't degrade anything as
long as the client is only seeding, and not downloading any torrents.

There's a *guided cache* mode. This means the size of the read cache line that's
stored in the cache is determined based on the upload rate to the peer that
triggered the read operation. The idea being that slow peers don't use up a
disproportional amount of space in the cache. This is enabled through
``settings_pack::guided_read_cache``.

In cases where the assumption is that the cache is only used as a read-ahead, and that no
other peer will ever request the same block while it's still in the cache, the read
cache can be set to be *volatile*. This means that every block that is requested out of
the read cache is removed immediately. This saves a significant amount of cache space
which can be used as read-ahead for other peers. To enable volatile read cache, set
``settings_pack::volatile_read_cache`` to true.

uTP-TCP mixed mode
------------------

libtorrent supports uTP_, which has a delay based congestion controller. In order to
avoid having a single TCP bittorrent connection completely starve out any uTP connection,
there is a mixed mode algorithm. This attempts to detect congestion on the uTP peers and
throttle TCP to avoid it taking over all bandwidth. This balances the bandwidth resources
between the two protocols. When running on a network where the bandwidth is in such an
abundance that it's virtually infinite, this algorithm is no longer necessary, and might
even be harmful to throughput. It is adviced to experiment with the
``session_setting::mixed_mode_algorithm``, setting it to ``settings_pack::prefer_tcp``.
This setting entirely disables the balancing and unthrottles all connections. On a typical
home connection, this would mean that none of the benefits of uTP would be preserved
(the modem's send buffer would be full at all times) and uTP connections would for the most
part be squashed by the TCP traffic.

.. _`uTP`: utp.html

send buffer low watermark
-------------------------

libtorrent uses a low watermark for send buffers to determine when a new piece should
be requested from the disk I/O subsystem, to be appended to the send buffer. The low
watermark is determined based on the send rate of the socket. It needs to be large
enough to not draining the socket's send buffer before the disk operation completes.

The watermark is bound to a max value, to avoid buffer sizes growing out of control.
The default max send buffer size might not be enough to sustain very high upload rates,
and you might have to increase it. It's specified in bytes in
``settings_pack::send_buffer_watermark``.

peers
-----

First of all, in order to allow many connections, set the global connection limit
high, ``settings_pack::connections_limit``. Also set the upload rate limit to
infinite, ``settings_pack::upload_rate_limit``, 0 means infinite.

When dealing with a large number of peers, it might be a good idea to have slightly
stricter timeouts, to get rid of lingering connections as soon as possible.

There are a couple of relevant settings: ``settings_pack::request_timeout``,
``settings_pack::peer_timeout`` and ``settings_pack::inactivity_timeout``.

For seeds that are critical for a delivery system, you most likely want to allow
multiple connections from the same IP. That way two people from behind the same NAT
can use the service simultaneously. This is controlled by
``settings_pack::allow_multiple_connections_per_ip``.

In order to always unchoke peers, turn off automatic unchoke by setting
``settings_pack::choking_algorithm`` to ``fixed_slot_choker`` and set the number
of upload slots to a large number via ``settings_pack::unchoke_slots_limit``,
or use -1 (which means infinite).

torrent limits
--------------

To seed thousands of torrents, you need to increase the ``settings_pack::active_limit``
and ``settings_pack::active_seeds``.

SHA-1 hashing
-------------

When downloading at very high rates, it is possible to have the CPU be the
bottleneck for passing every downloaded byte through SHA-1. In order to enable
calculating SHA-1 hashes in parallel, on multi-core systems, set
``settings_pack::aio_threads`` to the number of threads libtorrent should
perform I/O and do SHA-1 hashing in. Only if that thread is close to saturating
one core does it make sense to increase the number of threads.

scalability
===========

In order to make more efficient use of the libtorrent interface when running a large
number of torrents simultaneously, one can use the ``session::get_torrent_status()`` call
together with ``session::post_torrent_updates()``. Keep in mind that every call into
libtorrent that return some value have to block your thread while posting a message to
the main network thread and then wait for a response. Calls that don't return any data
will simply post the message and then immediately return, performing the work
asynchonuously. The time this takes might become significant once you reach a
few hundred torrents, depending on how many calls you make to each torrent and how often.
``session::get_torrent_status()`` lets you query the status of all torrents in a single call.
This will actually loop through all torrents and run a provided predicate function to
determine whether or not to include it in the returned vector.

To use ``session::post_torrent_updates()`` torrents need to have the ``flag_update_subscribe``
flag set. When post_torrent_updates() is called, a ``state_update_alert`` alert
is posted, with all the torrents that have updated since the last time this
function was called. The client have to keep its own state of all torrents, and
update it based on this alert.

benchmarking
============

There is a bunch of built-in instrumentation of libtorrent that can be used to get an insight
into what it's doing and how well it performs. This instrumentation is enabled by defining
preprocessor symbols when building.

There are also a number of scripts that parses the log files and generates graphs (requires
gnuplot and python).

disk metrics
------------

To enable disk I/O instrumentation, define ``TORRENT_DISK_STATS`` when building. When built
with this configuration libtorrent will create three log files, measuring various aspects of
the disk I/O. The following table is an overview of these files and what they measure.

+--------------------------+--------------------------------------------------------------+
| filename                 | description                                                  |
+==========================+==============================================================+
| ``file_access.log``      | This is a low level log of read and write operations, with   |
|                          | timestamps and file offsets. The file offsets are byte       |
|                          | offsets in the torrent (not in any particular file, in the   |
|                          | case of a multi-file torrent). This can be used as an        |
|                          | estimate of the physical drive location. The purpose of      |
|                          | this log is to identify the amount of seeking the drive has  |
|                          | to do.                                                       |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+

file_access.log
'''''''''''''''

The disk access log is a binary file that can be parsed and converted to human
readable by the script ``tools/parse_access_log.py``. This tool produces a
graphical representation of the disk access and requires ``gnuplot``.

understanding the disk threads
==============================

*This section is somewhat outdated, there are potentially more than one disk
thread*

All disk operations are funneled through a separate thread, referred to as the
disk thread. The main interface to the disk thread is a queue where disk jobs
are posted, and the results of these jobs are then posted back on the main
thread's io_service.

A disk job is essentially one of:

1. write this block to disk, i.e. a write job. For the most part this is just a
	matter of sticking the block in the disk cache, but if we've run out of
	cache space or completed a whole piece, we'll also flush blocks to disk.
	This is typically very fast, since the OS just sticks these buffers in its
	write cache which will be flushed at a later time, presumably when the drive
	head will pass the place on the platter where the blocks go.

2. read this block from disk. The first thing that happens is we look in the
	cache to see if the block is already in RAM. If it is, we'll return
	immediately with this block. If it's a cache miss, we'll have to hit the
	disk. Here we decide to defer this job. We find the physical offset on the
	drive for this block and insert the job in an ordered queue, sorted by the
	physical location. At a later time, once we don't have any more non-read
	jobs left in the queue, we pick one read job out of the ordered queue and
	service it. The order we pick jobs out of the queue is according to an
	elevator cursor moving up and down along the ordered queue of read jobs. If
	we have enough space in the cache we'll read read_cache_line_size number of
	blocks and stick those in the cache. This defaults to 32 blocks. If the
	system supports asynchronous I/O (Windows, Linux, Mac OS X, BSD, Solars for
	instance), jobs will be issued immediately to the OS. This especially
	increases read throughput, since the OS has a much greater flexibility to
	reorder the read jobs.

Other disk job consist of operations that needs to be synchronized with the
disk I/O, like renaming files, closing files, flushing the cache, updating the
settings etc. These are relatively rare though.

contributions
=============

If you have added instrumentation for some part of libtorrent that is not
covered here, or if you have improved any of the parser scrips, please consider
contributing it back to the project.

If you have run tests and found that some algorithm or default value in
libtorrent are suboptimal, please contribute that knowledge back as well, to
allow us to improve the library.

If you have additional suggestions on how to tune libtorrent for any specific
use case, please let us know and we'll update this document.

