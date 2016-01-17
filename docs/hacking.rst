==================
libtorrent hacking
==================

:Author: Arvid Norberg, arvid@libtorrent.org
:Version: 1.0.8

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
necessarily. Each piece is plit up in *blocks*, which is a 16 kiB. A block
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
important distiction is between a connected peer (*peer_connection*) and a peer
we just know about, and may have been connected to, and may connect to in the
future (*policy::peer*). The list of (not connected) peers may grow very large
if not limited (through tracker responses, DHT and peer exchange). This list
is typically limited to a few thousand peers.

The *policy* in libtorrent is somewhat poorly named. It was initially intended
to be a customization point where a client could define peer selection behavior
and unchoke logic. It didn't end up being though, and a more accurate name would
be peer_list. It really just maintains a potentially large list of known peers
for a swarm (not necessarily connected).

structure
=========

This is the high level structure of libtorrent. Bold types are part of the public
interface:


.. image:: hacking.png

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

policy
------

piece_picker
------------

torrent_info
------------

threads
=======

libtorrent starts 2 or 3 threads.

 * The first thread is the main thread that will sit
   idle in a ``kqueue()`` or ``epoll`` call most of the time.
   This thread runs the main loop that will send and receive
   data on all connections.

 * The second thread is the disk I/O thread. All disk read and write operations
   are passed to this thread and messages are passed back to the main thread when
   the operation completes. The disk thread also verifies the piece hashes.

 * The third and forth threads are spawned by asio on systems that don't support
   non-blocking host name resolution to simulate non-blocking getaddrinfo().



