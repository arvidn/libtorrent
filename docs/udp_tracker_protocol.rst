=========================================
Bittorrent udp-tracker protocol extension
=========================================

A tracker with the protocol "udp://" in its URI
is supposed to be contacted using this protocol.

This protocol is supported by
xbt-tracker_.


.. _xbt-tracker: http://xbtt.sourceforge.net

For additional information and descritptions of
the terminology used in this document, see
the `protocol specification`__

__ http://wiki.theory.org/index.php/BitTorrentSpecification

All values are sent in network byte order (big endian). The sizes
are specified with ANSI-C standard types.

If no response to a request is received within 15 seconds, resend
the request. If no reply has been received after 60 seconds, stop
retrying.


connecting
----------

Client sends packet:

+-------------+---------------------+----------------------------------------+
| size        | name                | description                            |
+=============+=====================+========================================+
| int64_t     | connection_id       | Not used, ignored by tracker.          |
+-------------+---------------------+----------------------------------------+
| int32_t     | action              | 0 for a connection request             |
+-------------+---------------------+----------------------------------------+
| int32_t     | transaction_id      | Randomized by client.                  |
+-------------+---------------------+----------------------------------------+

optional part

+-------------+---------------------+----------------------------------------+
| size        | name                | description                            |
+=============+=====================+========================================+
| int8_t      | flags               | 1 = authentication                     |
+-------------+---------------------+----------------------------------------+
| int8_t[8]   | username            |                                        |
+-------------+---------------------+----------------------------------------+
| int8_t[8]   | password_hash       | sha1-hash of the tracker password      |
+-------------+---------------------+----------------------------------------+

what is the point of having a fixed size username field instead of
a null-terminated string?

Why send the hash of the password instead of the password itself?


Server replies with packet:

+-------------+---------------------+----------------------------------------+
| size        | name                | description                            |
+=============+=====================+========================================+
| int32_t     | action              | Describes the type of packet, in this  |
|             |                     | case it should be 0, for connect.      |
|             |                     | If 3 (for error) see errors_.          |
+-------------+---------------------+----------------------------------------+
| int32_t     | transaction_id      | Must match the transaction_id sent     |
|             |                     | from the client.                       |
+-------------+---------------------+----------------------------------------+
| int64_t     | connection_id       | A connection id, this is used when     |
|             |                     | further information is exchanged with  |
|             |                     | the tracker, to identify you.          |
|             |                     | This connection id can be reused for   |
|             |                     | multiple requests, but if it's cached  |
|             |                     | for too long, it will not be valid     |
|             |                     | anymore.                               |
+-------------+---------------------+----------------------------------------+


announcing
----------

Client sends packet:

+-------------+---------------------+----------------------------------------+
| size        | name                | description                            |
+=============+=====================+========================================+
| int64_t     | connection_id       | The connection id acquired from        |
|             |                     | establishing the connection.           |
+-------------+---------------------+----------------------------------------+
| int32_t     | action              | Action. in this case, 1 for announce.  |
+-------------+---------------------+----------------------------------------+
| int32_t     | transaction_id      | Randomized by client.                  |
+-------------+---------------------+----------------------------------------+
| int8_t[20]  | info_hash           | The info-hash of the torrent you want  |
|             |                     | announce yourself in.                  |
+-------------+---------------------+----------------------------------------+
| int8_t[20]  | peer_id             | Your peer id.                          |
+-------------+---------------------+----------------------------------------+
| int64_t     | downloaded          | The number of byte you've downloaded   |
|             |                     | in this session.                       |
+-------------+---------------------+----------------------------------------+
| int64_t     | left                | The number of bytes you have left to   |
|             |                     | download until you're finished.        |
+-------------+---------------------+----------------------------------------+
| int64_t     | uploaded            | The number of bytes you have uploaded  |
|             |                     | in this session.                       |
+-------------+---------------------+----------------------------------------+
| int32_t     | event               | The event, one of                      |
|             |                     |                                        |
|             |                     |    * none = 0                          |
|             |                     |    * completed = 1                     |
|             |                     |    * started = 2                       |
|             |                     |    * stopped = 3                       |
+-------------+---------------------+----------------------------------------+
| uint32_t    | ip                  | Your ip address. Set to 0 if you want  |
|             |                     | the tracker to use the ``sender`` of   |
|             |                     | this udp packet.                       |
+-------------+---------------------+----------------------------------------+
| int32_t     | num_want            | The maximum number of peers you want   |
|             |                     | in the reply. Use -1 for default.      |
+-------------+---------------------+----------------------------------------+
| uint16_t    | port                | The port you're listening on.          |
+-------------+---------------------+----------------------------------------+

Server replies with packet:

+-------------+---------------------+----------------------------------------+
| size        | name                | description                            |
+=============+=====================+========================================+
| int32_t     | action              | The action this is a reply to. Should  |
|             |                     | in this case be 1 for announce.        |
|             |                     | If 3 (for error) see errors_.          |
+-------------+---------------------+----------------------------------------+
| int32_t     | transaction_id      | Must match the transaction_id sent     |
|             |                     | in the announce request.               |
+-------------+---------------------+----------------------------------------+
| int32_t     | interval            | the number of seconds you should wait  |
|             |                     | until reannouncing yourself.           |
+-------------+---------------------+----------------------------------------+

The rest of the server reply is a variable number of the following structure:

+-------------+---------------------+----------------------------------------+
| size        | name                | description                            |
+=============+=====================+========================================+
| int32_t     | ip                  | The ip of a peer in the swarm.         |
+-------------+---------------------+----------------------------------------+
| uint16_t    | port                | The peer's listen port.                |
+-------------+---------------------+----------------------------------------+


scraping
--------

Client sends packet:

+-------------+---------------------+----------------------------------------+
| size        | name                | description                            |
+=============+=====================+========================================+
| int64_t     | connection_id       | The connection id retreived from the   |
|             |                     | establishing of the connection.        |
+-------------+---------------------+----------------------------------------+
| int32_t     | action              | The action, in this case, 2 for        |
|             |                     | scrape.                                |
+-------------+---------------------+----------------------------------------+
| int32_t     | transaction_id      | Randomized by client.                  |
+-------------+---------------------+----------------------------------------+
| int8_t[20]  | info_hash           | The info hash that is to be scraped.   |
+-------------+---------------------+----------------------------------------+

Server replies with packet:

+-------------+---------------------+----------------------------------------+
| size        | name                | description                            |
+=============+=====================+========================================+
| int32_t     | action              | The action, should in this case be     |
|             |                     | 2 for scrape.                          |
|             |                     | If 3 (for error) see errors_.          |
+-------------+---------------------+----------------------------------------+
| int32_t     | transaction_id      | Must match the sent transaction id.    |
+-------------+---------------------+----------------------------------------+

The rest of the packet contains a variable number of the following structures:

+-------------+---------------------+----------------------------------------+
| size        | name                | description                            |
+=============+=====================+========================================+
| int8_t[20]  | info_hash           | The info hash of this info.            |
+-------------+---------------------+----------------------------------------+
| int32_t     | complete            | The total number of completed          |
|             |                     | downloads.                             |
+-------------+---------------------+----------------------------------------+
| int32_t     | downloaded          | The current number of connected seeds. |
+-------------+---------------------+----------------------------------------+
| int32_t     | incomplete          | The current number of connected        |
|             |                     | leechers.                              |
+-------------+---------------------+----------------------------------------+

errors
------

In case of a tracker error, the server replies with this packet:

+-------------+---------------------+----------------------------------------+
| size        | name                | description                            |
+=============+=====================+========================================+
| int32_t     | action              | The action, in this case 3, for error. |
+-------------+---------------------+----------------------------------------+
| int32_t     | transaction_id      | Must match the transaction_id sent     |
|             |                     | from the client.                       |
+-------------+---------------------+----------------------------------------+
| int8_t[]    | error_string        | The rest of the packet is a string     |
|             |                     | describing the error.                  |
+-------------+---------------------+----------------------------------------+


actions
-------

The action fields has the following encoding:

	* connect = 0
	* announce = 1
	* scrape = 2
	* error = 3 (only in server replies)


credits
-------

Protocol designed by Olaf van der Spek

