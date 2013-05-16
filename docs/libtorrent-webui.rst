libtorrent-webui
================

The libtorrent webui is a high-performing, scalable, interface to bittorrent clients
over a websocket. The intention is that applications, specifically web applications,
can control a bittorrent client over this interface, or be extended with bittorrent
functionality.

In this document, the "bittorrent client" and the "application" will be used to refer
to the websocket server and the websocket client respectively. i.e. the application
with bittorrent capabilities and the application talking to it and possibly controlling
it.

The libtorrent-webui protocol sits on top of the `websocket protocol`_. It consists
of independent messages, each encoded as a binary websocket frame.

An application talking to a bittorrent client communicate with it over an *RPC* protocol,
(Remote procedure call). This means each message it sends to the bittorrent client is
conceptually calling a function on the bittorrent client and the response from that
function is then returned back in another message. That is, each RPC call message, has
a corresponding RPC response message.

The protocol is designed specifically to have a very compact represenation on the wire
and to not send redundant information, especially not in the common case. This is why
the protocol is binary and why there's not intermediate structure representation (like
bencoding, rencoding or JSON).

RPC format
----------

To make a function call, send a frame with this format:

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 0        | uint8_t            | ``function-id`` (the function to call)    |
|          |                    | with the most significant bit cleared.    |
+----------+--------------------+-------------------------------------------+
| 1        | uint16_t           | ``transaction-id`` (echoed back in        |
|          |                    | response)                                 |
+----------+--------------------+-------------------------------------------+
| 3        | ...                | *arguments* (RPC call specific)           |
+----------+--------------------+-------------------------------------------+

The response frame looks like this:

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 0        | uint8_t            | ``function-id`` (the function returning)  |
|          |                    | with the most significan bit set.         |
+----------+--------------------+-------------------------------------------+
| 1        | uint16_t           | ``transaction-id`` (copied from the call) |
+----------+--------------------+-------------------------------------------+
| 3        | uint8_t            | ``error-code`` (0 = success)              |
+----------+--------------------+-------------------------------------------+
| 4        | ...                | *return value* (RPC call specific)        |
+----------+--------------------+-------------------------------------------+

The most significant bit of the ``function-id`` field indicates whether the message
is a return value or a call in itself.

The ``transaction-id`` sent in a call, is repeated in the response message. This
allows the caller to pair up calls with their responses. Responses may
be returned out of order relative to the order the calls were made.

The values used for the ``error code`` field in the response are detailed in
`Appendix B`_.

settings
--------

Settings are primarily represented by 16-bit codes. Get- and Set calls for
settings only use these 16-bit codes to refer to settings. The mapping of
settings codes to the actual settings is not defined by the protcol. Instead,
an application need to query the bittorrent client for available settings
which returns a mapping of setting names to type and code. This function
is called `list-settings`_.

functions
---------

Reference for RPC format for all functions specified by this protocol.

All messages sent to the bittorrent client start with an 8 bit message identifier.
See `appendix A`_ for all message IDs.

get_torrent_updates
...................

function id 0.

This function requests updates of torrent states. Updates are typically relative
to the last update. The state for all torrents is assumed to be kept by the
application, and update fields that are changing by querying them with this function.

The frame number is a form of time stamp indicating the current time on the bittorrent
client. This frame number can be used later to request updates since a certain time.
The frame number is always the first argument, all subsequent arguments are updates for
one torrent each.

It's possible to always request the entire state for every torrent by passing in
a frame number of 0.

The call looks like this (not including the RPC-call header):

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 3        | uint32_t           | ``frame-number`` (timestamp)              |
+----------+--------------------+-------------------------------------------+
| 7        | uint64_t           | ``field-bitmask`` (only these fields are  |
|          |                    | returned)                                 |
+----------+--------------------+-------------------------------------------+

The torrent updates don't necessarily include all fields of the torrent. There is
a bitmask indicating which fields are included in this update. Any field not
included should be left at its previous value.

The return value for this function is (offset includes RPC-response header):

+----------+--------------------+-------------------------------------------+
| offset   | type               | name                                      |
+==========+====================+===========================================+
| 4        | uint32_t           | ``frame-number`` (timestamp)              |
+----------+--------------------+-------------------------------------------+
| 8        | uint32_t           | ``num-torrents`` (the number of torrent   |
|          |                    | updates to follow)                        |
+----------+--------------------+-------------------------------------------+
| 8        | uint8_t[20]        | ``info-hash`` indicate which torrent      |
|          |                    | the following update refers to.           |
+----------+--------------------+-------------------------------------------+
| 28       | uint64_t           | ``update-bitmask`` bitmask indicating     |
|          |                    | which torrent fields are being updated.   |
+----------+--------------------+-------------------------------------------+
| 36       | ...                | *values for all updated fields*           |
+----------+--------------------+-------------------------------------------+

The last 3 fields (``info-hash``, ``update-bitmask`` and
*values for all updated fields*) are repeated ``num-torrents`` times.

The fields on torrents, in bitmask bit-order (LSB is bit 0), are:

+----------+---------------------+------------------------------------------+
| field-id | type                | name                                     |
+==========+=====================+==========================================+
| 0        | uint64_t            | ``flags`` bitmask with the following     |
|          |                     | bits:                                    |
|          |                     |                                          |
|          |                     |    0x001. stopped                        |
|          |                     |    0x002. auto-managed                   |
|          |                     |    0x004. sequential-downloads           |
|          |                     |    0x008. seeding                        |
|          |                     |    0x010. finished                       |
|          |                     |    0x020. loaded                         |
|          |                     |    0x040. has-metadata                   |
|          |                     |    0x080. has-incoming-connections       |
|          |                     |    0x100. seed-mode                      |
|          |                     |    0x200. upload-mode                    |
|          |                     |    0x400. share-mode                     |
|          |                     |    0x800. super-seeding                  |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+
| 1        | uint16_t, uint8_t[] | ``name``. This is a variable length      |
|          |                     | string with a 16 bit length prefix.      |
|          |                     | it is encoded as UTF-8.                  |
+----------+---------------------+------------------------------------------+
| 2        | uint64_t            | ``total-uploaded`` (number of bytes)     |
+----------+---------------------+------------------------------------------+
| 3        | uint64_t            | ``total-downloaded`` (number of bytes)   |
+----------+---------------------+------------------------------------------+
| 4        | uint64_t            | ``added-time`` (posix time)              |
+----------+---------------------+------------------------------------------+
| 5        | uint64_t            | ``completed-time`` (posix time)          |
+----------+---------------------+------------------------------------------+
| 6        | uint32_t            | ``upload-rate`` (Bytes per second)       |
+----------+---------------------+------------------------------------------+
| 7        | uint32_t            | ``download-rate`` (Bytes per second)     |
+----------+---------------------+------------------------------------------+
| 8        | uint32_t            | ``progress`` (specified in the range     |
|          |                     | 0 - 1000000)                             |
+----------+---------------------+------------------------------------------+
| 9        | uint16_t, uint8_t[] | ``error`` Variable length string with 16 |
|          |                     | bit length prefix. Encoded as UTF-8.     |
+----------+---------------------+------------------------------------------+
| 10       | uint32_t            | ``connected-peers``                      |
+----------+---------------------+------------------------------------------+
| 11       | uint32_t            | ``connected-seeds``                      |
+----------+---------------------+------------------------------------------+
| 12       | uint32_t            | ``downloaded-pieces``                    |
+----------+---------------------+------------------------------------------+
| 13       | uint64_t            | ``total-done`` The total number of bytes |
|          |                     | completed (downloaded and checked)       |
+----------+---------------------+------------------------------------------+
| 14       | uint32_t, uint32_t  | ``distributed-copies``. The first int    |
|          |                     | is the integer portion of the fraction,  |
|          |                     | the second int is the fractional part.   |
+----------+---------------------+------------------------------------------+
| 15       | uint64_t            | ``all-time-upload`` (Bytes)              |
+----------+---------------------+------------------------------------------+
| 16       | uint64_t            | ``all-time-download`` (Bytes)            |
+----------+---------------------+------------------------------------------+
| 17       | uint32_t            | ``unchoked-peers``                       |
+----------+---------------------+------------------------------------------+
| 18       | uint32_t            | ``num-connections``                      |
+----------+---------------------+------------------------------------------+
| 19       | uint32_t            | ``queue-position``                       |
+----------+---------------------+------------------------------------------+
| 20       | uint8_t             | ``state``                                |
|          |                     |                                          |
|          |                     |    0. checking-files                     |
|          |                     |    1. downloading-metadata               |
|          |                     |    2. downloading                        |
|          |                     |    3. seeding                            |
|          |                     |                                          |
+----------+---------------------+------------------------------------------+
| 21       | uint64_t            | ``failed-bytes`` (Bytes)                 |
+----------+---------------------+------------------------------------------+
| 22       | uint64_t            | ``redundant-bytes`` (Bytes)              |
+----------+---------------------+------------------------------------------+
|          |                     |                                          |
+----------+---------------------+------------------------------------------+

For example, an update with the bitmask ``0x1`` means that the only thing that
changed since the last update for this torrent was one or more of the torrent's
flags. Only the flags field will follow for this torrent's update. If there are
more torrent updates, the next field to read will be the info-hash for the next
update.

torrent actions
...............

There is a group of commands that are simple. That just perform an action on one
or more torrents with no additional arguments. The torrents they operate on are
specified by their corresponding info-hash (encoded as a binary 20 byte string).

The functions that follow this simple syntax are (with function-id):

	1. start
	2. stop
	3. set-auto-managed
	4. clear-auto-managed
	5. queue up
	6. queue down
	7. queue top
	8. queue bottom
	9. remove
	10. remove + data
	11. force recheck
	12. set-sequential-download
	13. clear-sequential-download

The arguments for these functions are (offset includes RPC header):

+----------+--------------------+-----------------------------------------+
| offset   | type               | name                                    |
+==========+====================+=========================================+
| 3        | uint16_t           | ``num-info-hashes``                     |
+----------+--------------------+-----------------------------------------+
| 5        | uint8_t[20]        | ``info-hash``                           |
+----------+--------------------+-----------------------------------------+
| 25       | uint8_t[20]        | additional info-hash (optional)         |
+----------+--------------------+-----------------------------------------+
| ...      | ...                | ...                                     |
+----------+--------------------+-----------------------------------------+

That is, each command can apply to any number of torrents. The 20 byte info-hash
field is repeated ``num-info-hashes`` times. The command is applied to each
torrent whose info hash is specified.

The return value for these commands are the number of torrents that were found
and had the command invoked on them.

+----------+--------------------+-----------------------------------------+
| offset   | type               | name                                    |
+==========+====================+=========================================+
| 4        | uint16_t           | ``num-success-torrents``                |
+----------+--------------------+-----------------------------------------+


list-settings
.............

function id 14.

This message returns all available settings as strings, as well as their
corresponding setting id and type.

This function does not take any arguments. The return value is:

+----------+--------------------+-----------------------------------------+
| offset   | type               | name                                    |
+==========+====================+=========================================+
| 4        | uint32_t           | ``num-string-settings``                 |
+----------+--------------------+-----------------------------------------+
| 8        | uint32_t           | ``num-int-settings``                    |
+----------+--------------------+-----------------------------------------+
| 12       | uint32_t           | ``num-bool-settings``                   |
+----------+--------------------+-----------------------------------------+
| 16       | uint8_t, uint8_t[] | ``setting-name``                        |
+----------+--------------------+-----------------------------------------+
| 17+ n    | uint16_t           | ``setting-id``                          |
+----------+--------------------+-----------------------------------------+

The last 2 fields are repeated ``num-stringsettings`` * ``num-int-settings``
* ``num-bool-settings``  times.

This list of name -> id pairs tells you all of the available settings
for the bittorrent client. Note that the length prefix for the settings name
string is 8 bits.

The ``num-string-settings`` entries are of *string* type, the following
``num-int-settings`` are of *int* type and the following ``num-bool-settings``
are of type *boolean*.

set-settings
............

get-settings
............

Appendix A
==========

Function IDs

+-----+---------------------------+-----------------------------------------+
| ID  | Function name             | Arguments                               |
+=====+===========================+=========================================+
|   0 | get-torrent-updates       | last-frame-number (uint32_t)            |
|     |                           | bitmask indicating which fields to      |
|     |                           | return (uint64_t)                       |
+-----+---------------------------+-----------------------------------------+
|   1 | start                     | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   2 | stop                      | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   3 | set-auto-managed          | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   4 | clear-auto-managed        | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   5 | queue-up                  | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   6 | queue-down                | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   7 | queue-top                 | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   8 | queue-bottom              | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|   9 | remove                    | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|  10 | remove_and_data           | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|  11 | force-recheck             | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|  12 | set-sequential-download   | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|  13 | clear-sequential-download | info-hash, ...                          |
+-----+---------------------------+-----------------------------------------+
|  14 | list-settings             |                                         |
+-----+---------------------------+-----------------------------------------+
|  15 | set-settings              | setting-id, type, value, ...            |
+-----+---------------------------+-----------------------------------------+
|  16 | get-settings              | setting-id, ...                         |
+-----+---------------------------+-----------------------------------------+

Appendix B
==========

Error codes used in RPC response messages.

+------+------------------------------------------------+
| code | meaning                                        |
+======+================================================+
|    0 | no error                                       |
+------+------------------------------------------------+
|    1 | no such function                               |
+------+------------------------------------------------+
|    2 | invalid number of arguments for function       |
+------+------------------------------------------------+
|    3 | invalid argument type for function             |
+------+------------------------------------------------+
|    4 | invalid argument (correct type, but outside    |
|      | of valid domain)                               |
+------+------------------------------------------------+

