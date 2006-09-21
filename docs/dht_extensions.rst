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

The only DHT messages that don't support IPv6 is the ``nodes`` reply. It
encodes all the contacts as 6 bytes sequences packet together in sequence in 
 string. The problem is that IPv6 endpoints cannot be encoded as 6 bytes, but
18 bytes. The extension libtorrent applies is to add another key, called
``nodes2`` which is encoded as a list of strings. Each string represents one
contact and is encoded as 20 bytes node-id and then a variable length encoded
IP address (6 bytes in IPv4 case and 18 bytes in IPv6 case).

