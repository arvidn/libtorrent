==================
libtorrent hacking
==================

:Author: Arvid Norberg, arvid@libtorrent.org
:Version: 1.1.0

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
future (*torrent_peer*). The list of (not connected) peers may grow very large
if not limited (through tracker responses, DHT and peer exchange). This list
is typically limited to a few thousand peers.

The *peer_list* maintains a potentially large list of known peers for a swarm
(not necessarily connected).

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

peer_list
---------

piece_picker
------------

torrent_info
------------

threads
=======

libtorrent starts 3 to 5 threads.

 * The first thread is the main thread that will sit
   idle in a ``select()`` call most of the time. This thread runs the main loop
   that will send and receive data on all connections. In reality it's typically
   not actually in ``select()``, but in ``kqueue()``, ``epoll_wait()`` or ``poll``,
   depending on operating system.

 * The second thread is the disk I/O thread. All disk read and write operations
   are passed to this thread and messages are passed back to the main thread when
   the operation completes.

 * The third thread is the SHA-1 hash thread. By default there's only one hash thread,
   but on multi-core machines downloading at very high rates, libtorrent can be configured
   to start any number of hashing threads, to take full use of multi core systems.
   (see ``session_settings::hashing_threads``).

 * The fourth and fifth threads are spawned by asio on systems that don't support
   asynchronous host name resolution, in order to simulate non-blocking ``getaddrinfo()``.

disk cache
==========

The disk cache implements *ARC*, Adaptive Replacement Cache. This consists of a number of LRUs:

1. lru L1 (recently used)
2. lru L1 ghost (recently evicted)
3. lru L2 (frequently used)
4. lru L2 ghost (recently evicted)
5. volatile read blocks
6. write cache (blocks waiting to be flushed to disk)

.. parsed-literal::
	
	             <--- recently used  frequently used --->
	+--------------+--------------+  +--------------+--------------+
	|     L1 **ghost** |           L1 |  | L2           | L2 **ghost**     |
	+--------------+--------------+  +--------------+--------------+
	
	               <---------- cache_size ---------->
	
	<---------------------- 2 x cache_size ------------------------>

These LRUs are stored in ``block_cache`` in an array ``m_lru``.

The cache algorithm works like this::

	if (L1->is_hit(piece)) {
		L1->erase(piece);
		L2->push_back(piece);
	} else if (L2->is_hit(piece)) {
		L2->erase(piece);
		L2->push_back(page);
	} else if (L1->size() == cache_size) {
		L1->pop_front();
		L1->push_back(piece);
	} else {
		if (L1->size() + L2->size() == 2*chache_size) {
			L2->pop_front();
		}
		L1->push_back(piece);
	}

It's a bit more complicated since within L1 and L2 in this pseudo code
have to separate the ghost entries and the in-cache entries.

Note that the most recently used and more frequently used pieces are at
the *back* of the lists. Iterating over a list gives you low priority pieces
first.

In libtorrent pieces are cached, not individual blocks, a single peer would
typically trigger many cache hits when downloading a piece. Since ARC is
sensitive to extra cache hits (a piece is moved to L2 the second time it's
hit) libtorrent only move the cache entry on cache hits when it's hit by
another peer than the last peer that hit it.

Another difference compared to the ARC paper is that libtorrent caches pieces,
which aren't necessarily fully allocated. This means the real cache size is
specified in number of blocks, not pieces, so there's not clear number of pieces
to keep in the ghost lists. There's an ``m_num_arc_pieces`` member in ``block_cache``
that defines the *arc cache size*, in pieces, rather than blocks.

