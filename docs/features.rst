=================
libtorrent manual
=================

:Author: Arvid Norberg, arvid@rasterbar.com

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

introduction
============

libtorrent is a C++ library that aims to be a good alternative to all the
other bittorrent implementations around. It is a
library and not a full featured client, although it comes with a working
example client.

The main goals of libtorrent are:

* to be cpu efficient
* to be memory efficient
* to be very easy to use

features
========

libtorrent is still being developed, however it is stable. It is an ongoing
project (including this documentation). The current state includes the
following features:

* trackerless torrents (using the Mainline kademlia DHT protocol) with
  some `DHT extensions`_.
* support for IPv6
* NAT-PMP and UPnP support (automatic port mapping on routers that supports it)
* uses a separate disk I/O thread to not have the disk ever block on network or
  client interaction. (see threads_).
* supports the bittorrent `extension protocol`_. See extensions_.
* supports the uTorrent metadata transfer protocol (i.e. magnet links).
* supports the uTorrent peer exchange protocol (PEX).
* supports local peer discovery (multicasts for peers on the same local network)
* adjusts the length of the request queue depending on download rate.
* has an adjustable read and write disk cache for improved disk throughput.
* multitracker extension support (supports both the `specification by John Hoffman`__
  and the uTorrent interpretation).
* tracker scrapes
* supports both sparse files and compact file allocation (where pieces
  are kept consolidated on disk)
* supports files > 2 gigabytes.
* serves multiple torrents on a single port and in a single thread
* fast resume support, a way to get rid of the costly piece check at the
  start of a resumed torrent. Saves the storage state, piece_picker state
  as well as all local peers in a separate fast-resume file.
* `HTTP seeding`_, as `specified by Michael Burford of GetRight`__.
* piece picking on block-level (as opposed to piece-level).
  This means it can download parts of the same piece from different peers.
  It will also prefer to download whole pieces from single peers if the
  download speed is high enough from that particular peer.
* supports the `udp-tracker protocol`_ by Olaf van der Spek.
* queues torrents for file check, instead of checking all of them in parallel.
* supports http proxies and basic proxy authentication
* gzipped tracker-responses
* can limit the upload and download bandwidth usage and the maximum number of
  unchoked peers
* implements fair trade. User settable trade-ratio, must at least be 1:1,
  but one can choose to trade 1 for 2 or any other ratio that isn't unfair
  to the other party.
* supports the ``no_peer_id=1`` extension that will ease the load off trackers.
* possibility to limit the number of connections.
* delays have messages if there's no other outgoing traffic to the peer, and
  doesn't send have messages to peers that already has the piece. This saves
  bandwidth.
* does not have any requirements on the piece order in a torrent that it
  resumes. This means it can resume a torrent downloaded by any client.
* supports the ``compact=1`` tracker parameter.
* selective downloading. The ability to select which parts of a torrent you
  want to download.
* ip filter to disallow ip addresses and ip ranges from connecting and
  being connected

.. _`DHT extensions`: dht_extensions.html
__ http://bittorrent.org/beps/bep_0012.html
__ http://www.getright.com/seedtorrent.html
.. _`extension protocol`: extension_protocol.html
.. _`udp-tracker protocol`: udp_tracker_protocol.html

portability
===========

libtorrent is portable at least among Windows, MacOS X and other UNIX-systems.
It uses Boost.Thread, Boost.Filesystem, Boost.Date_time and various other
boost libraries as well as zlib_ (shipped) and asio_ (shipped). At least version
1.33.1 of boost is required.

.. _zlib: http://www.zlib.org
.. _asio: http://asio.sf.net

Since libtorrent uses asio, it will take full advantage of high performance
network APIs on the most popular platforms. I/O completion ports on windows,
epoll on linux and kqueue on MacOS X and BSD.

libtorrent has been successfully compiled and tested on:

* Windows 2000 vc7.1, vc8
* Linux x86 GCC 3.3, GCC 3.4.2
* MacOS X (darwin), (Apple's) GCC 3.3, (Apple's) GCC 4.0
* SunOS 5.8 GCC 3.1
* Cygwin GCC 3.3.3

Fails on:

* GCC 2.95.4
* msvc6

license
=======

libtorrent is released under the BSD-license_.

.. _BSD-license: http://www.opensource.org/licenses/bsd-license.php

This means that you can use the library in your project without having to
release its source code. The only requirement is that you give credit
to the author of the library by including the libtorrent license in your
software or documentation.

`Here's`__ a list of some projects that uses libtorrent.

__ projects.html

.. _`http seeding`: manual.html#http-seeding
.. _threads: manual.html#threads
.. _extensions: manual.html#extensions

