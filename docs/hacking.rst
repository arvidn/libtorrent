==================
libtorrent hacking
==================

.. include:: header.rst

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

This describe some of the internals of libtorrent. If you're looking for
something to contribute, please take a look at the `todo list`_.

.. _`todo list`: todo.html

terminology
===========

This section describes some of the terminology used throughout the
libtorrent source. Having a good understanding of some of these keywords
helps understanding what's going on.

A *piece* is a part of the data of a torrent that has a SHA-1 hash in
the .torrent file. Pieces are almost always a power of two in size, but not
necessarily. Each piece is split up in *blocks*, which is a 16 kiB. A block
never spans two pieces. If a piece is smaller than 16 kiB or not divisible
by 16 kiB, there are blocks smaller than that.

16 kiB is a de-facto standard of the largest transfer unit in the bittorrent
protocol. Clients typically reject any request for larger pieces than this.

The *piece picker* is the part of a bittorrent client that is responsible for
the logic to determine which requests to send to peers. It doesn't actually
pick full pieces, but blocks (from pieces).

The file layout of a torrent is represented by *file storage* objects. This
class contains a list of all files in the torrent (in a well defined order),
the size of the pieces and implicitly the total size of the whole torrent and
number of pieces. The file storage determines the mapping from *pieces*
to *files*. This representation may be quite complex in order to keep it extremely
compact. This is useful to load very large torrents without exploding in memory
usage.

A *torrent* object represents all the state of swarm download. This includes
a piece picker, a list of peer connections, file storage (torrent file). One
important distinction is between a connected peer (*peer_connection*) and a peer
we just know about, and may have been connected to, and may connect to in the
future (*torrent_peer*). The list of (not connected) peers may grow very large
if not limited (through tracker responses, DHT and peer exchange). This list
is typically limited to a few thousand peers.

The *peer_list* maintains a potentially large list of known peers for a swarm
(not necessarily connected).

structure
=========

This is the high level structure of libtorrent. Bold types are part of the public
interface:


.. image:: img/hacking.png
	:class: bw

session_impl
------------

This is the session state object, containing all session global information, such as:

	* the list of all torrents ``m_torrent``.
	* the list of all peer connections ``m_connections``.
	* the global rate limits ``m_settings``.
	* the DHT state ``m_dht``.
	* the port mapping state, ``m_upnp`` and ``m_natpmp``.

session
-------

This is the public interface to the session. It implements pimpl (pointer to implementation)
in order to hide the internal representation of the ``session_impl`` object from the user and
make binary compatibility simpler to maintain.

torrent_handle
--------------

This is the public interface to a ``torrent``. It holds a weak reference to the internal
``torrent`` object and manipulates it by sending messages to the network thread.

torrent
-------

peer_connection
---------------

peer_list
---------

piece_picker
------------

torrent_info
------------

threads
=======

libtorrent starts at least 3 threads, but likely more, depending on the
settings_pack::aio_threads setting. The kinds of threads are:

 * The main network thread that manages all sockets;
   sending and receiving messages and maintaining all session, torrent and peer
   state. In an idle session, this thread will mostly be blocked in a system call,
   waiting for socket activity, such as ``epoll()``.

 * A disk I/O thread. There may be multiple disk threads. All disk read and
   write operations are passed to this thread and messages are passed back to
   the main thread when the operation completes. This kind of thread also performs
   the SHA-1/SHA-256 calculations to verify pieces. Some disk threads may have an
   affinity for those jobs, to avoid starvation of the disk.

 * At least one thread is spawned by boost.asio on systems that don't support
   asynchronous host name resolution, in order to simulate non-blocking ``getaddrinfo()``.

