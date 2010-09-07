:Author: Arvid Norberg, arvid@rasterbar.com

Mainline DHT extensions
=======================

libtorrent implements a few extensions to the Mainline DHT protocol.

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

