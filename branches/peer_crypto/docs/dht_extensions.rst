:Author: Arvid Norberg, arvid@libtorrent.org

Mainline DHT extensions
=======================

libtorrent implements a few extensions to the Mainline DHT protocol.

get_peers response
------------------

libtorrent always responds with ``nodes`` to a get_peers request. If it has
peers for the specified info-hash, it will return ``values`` as well. This is
because just because some peer announced to us, doesn't mean that we are
among the 8 closest nodes of the info hash. libtorrent also keeps traversing
nodes using get_peers until it has found the 8 closest ones, and then announces
to those nodes.

forward compatibility
---------------------

In order to support future DHT messages, any message which is not recognized
but has either an ``info_hash`` or ``target`` argument is interpreted as
find node for that target. i.e. it returns nodes. This allows future messages
to be properly forwarded by clients that don't understand them instead of
being blocked.

client identification
---------------------

In each DHT packet, an extra key is inserted named "v". This is a string
describing the client and version used. This can help alot when debugging
and finding errors in client implementations. The string is encoded as four
characters, two characters describing the client and two characters interpreted
as a binary number describing the client version.

Currently known clients:

+---------------+--------+
| uTorrent      | ``UT`` |
+---------------+--------+
| libtorrent    | ``LT`` |
+---------------+--------+
| MooPolice     | ``MP`` |
+---------------+--------+
| GetRight      | ``GR`` |
+---------------+--------+

IPv6 support
------------

**This extension is superseeded by** `BEP 32`_.

.. _`BEP 32`: http://bittorrent.org/beps/bep_0032.html

The DHT messages that don't support IPv6 are the ``nodes`` replies.
They encode all the contacts as 6 bytes packed together in sequence in a
string. The problem is that IPv6 endpoints cannot be encoded as 6 bytes, but
needs 18 bytes. The extension libtorrent applies is to add another key, called
``nodes2``.

``nodes2`` may be present in replies that contains a ``nodes`` key. It is encoded
as a list of strings. Each string represents one contact and is encoded as 20
bytes node-id and then a variable length encoded IP address (6 bytes in IPv4 case
and 18 bytes in IPv6 case).

As an optimization, libtorrent does not include the extra key in case there are
only IPv4 nodes present.

