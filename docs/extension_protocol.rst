:Author: Arvid Norberg, arvid@rasterbar.com
         Ludvig Strigeus, ludde@utorrent.com

extension protocol for bittorrent
=================================

The intention of this protocol is to provide a simple and thin transport
for extensions to the bittorrent protocol. Supporting this protocol makes
it easy to add new extensions without interfering with the standard
bittorrent protocol or clients that don't support this extension or the
one you want to add.

To advertise to other clients that you support, one bit from the reserved
bytes is used.

The bit selected for the extension protocol is bit 20 from the right (counting
starts at 0). So (reserved_byte[5] & 0x10) is the expression to use for checking
if the client supports extended messaging.

Once support for the protocol is established, the client is supposed to
support 1 new message:

+------------------------+----+
|name                    | id |
+========================+====+
|``extended``            | 20 |
+------------------------+----+

This message is sent as any other bittorrent message, with a 4 byte length
prefix and a single byte identifying the message (the single byte being 20
in this case). At the start of the payload of the message, is a single byte
message identifier. This identifier can refer to different extension messages
and only one ID is specified, 0. If the ID is 0, the message is a handshake
message which is described below. The layout of a general ``extended`` message
follows (including the message headers used by the bittorrent protocol):

+----------+---------------------------------------------------------+
| size     | description                                             |
+==========+=========================================================+
| uint32_t | length prefix. Specifies the number of bytes for the    |
|          | entire message. (Big endian)                            |
+----------+---------------------------------------------------------+
| uint8_t  | bittorrent message ID, = 20                             |
+----------+---------------------------------------------------------+
| uint8_t  | extended message ID. 0 = handshake, >0 = extended       |
|          | message as specified by the handshake.                  |
+----------+---------------------------------------------------------+


handshake message
-----------------

The payload of the handshake message is a bencoded dictionary. All items
in the dictionary are optional. Any unknown names should be ignored
by the client. All parts of the dictionary are case sensitive.
This is the defined item in the dictionary:

+-------+-----------------------------------------------------------+
| name  | description                                               |
+=======+===========================================================+
| m     | Dictionary of supported extension messages which maps     |
|       | names of extensions to an extended message ID for each    |
|       | extension message. The only requirement on these IDs      |
|       | is that no extension message share the same one. Setting  |
|       | an extension number to zero means that the extension is   |
|       | not supported/disabled. The client should ignore any      |
|       | extension names it doesn't recognize.                     |
|       |                                                           |
|       | The extension message IDs are the IDs used to send the    |
|       | extension messages to the peer sending this handshake.    |
|       | i.e. The IDs are local to this particular peer.           |
+-------+-----------------------------------------------------------+


Here are some other items that an implementation may choose to support:

+--------+-----------------------------------------------------------+
| name   | description                                               |
+========+===========================================================+
| p      | Local TCP listen port. Allows each side to learn about    |
|        | the TCP port number of the other side. Note that there is |
|        | no need for the receiving side of the connection to send  |
|        | this extension message, since its port number is already  |
|        | known.                                                    |
+--------+-----------------------------------------------------------+
| v      | Client name and version (as a utf-8 string).              |
|        | This is a much more reliable way of identifying the       |
|        | client than relying on the peer id encoding.              |
+--------+-----------------------------------------------------------+
| yourip | A string containing the compact representation of the ip  |
|        | address this peer sees you as. i.e. this is the           |
|        | receiver's external ip address (no port is included).     |
|        | This may be either an IPv4 (4 bytes) or an IPv6           |
|        | (16 bytes) address.                                       |
+--------+-----------------------------------------------------------+
| ipv6   | If this peer has an IPv6 interface, this is the compact   |
|        | representation of that address (16 bytes). The client may |
|        | prefer to connect back via the IPv6 address.              |
+--------+-----------------------------------------------------------+
| ipv4   | If this peer has an IPv4 interface, this is the compact   |
|        | representation of that address (4 bytes). The client may  |
|        | prefer to connect back via this interface.                |
+--------+-----------------------------------------------------------+
| reqq   | An integer, the number of outstanding request messages    |
|        | this client supports without dropping any. The default in |
|        | in libtorrent is 250.                                     |
+--------+-----------------------------------------------------------+

The handshake dictionary could also include extended handshake
information, such as support for encrypted headers or anything
imaginable.

An example of what the payload of a handshake message could look like:

+------------------------------------------------------+
| Dictionary                                           |
+===================+==================================+
| ``m``             |  +--------------------------+    |
|                   |  | Dictionary               |    |
|                   |  +======================+===+    |
|                   |  | ``LT_metadata``      | 1 |    |
|                   |  +----------------------+---+    |
|                   |  | ``ut_pex``           | 2 |    |
|                   |  +----------------------+---+    |
|                   |                                  |
+-------------------+----------------------------------+
| ``p``             | 6881                             |
+-------------------+----------------------------------+
| ``v``             | "uTorrent 1.2"                   |
+-------------------+----------------------------------+

and in the encoded form:

``d1:md11:LT_metadatai1e6:ut_pexi2ee1:pi6881e1:v12:uTorrent 1.2e``

To make sure the extension names do not collide by mistake, they should be
prefixed with the two (or one) character code that is used to identify the
client that introduced the extension. This applies for both the names of
extension messages, and for any additional information put inside the
top-level dictionary. All one and two byte identifiers are invalid to use
unless defined by this specification.

This message should be sent immediately after the standard bittorrent handshake
to any peer that supports this extension protocol. It is valid to send the
handshake message more than once during the lifetime of a connection,
the sending client should not be disconnected. An implementation may choose
to ignore the subsequent handshake messages (or parts of them).

Subsequent handshake messages can be used to enable/disable extensions
without restarting the connection. If a peer supports changing extensions
at run time, it should note that the ``m`` dictionary is additive.
It's enough that it contains the actual *CHANGES* to the extension list.
To disable the support for ``LT_metadata`` at run-time, without affecting
any other extensions, this message should be sent:
``d11:LT_metadatai0ee``.
As specified above, the value 0 is used to turn off an extension.

The extension IDs must be stored for every peer, becuase every peer may have
different IDs for the same extension.

This specification, deliberately, does not specify any extensions such as
peer-exchange or metadata exchange. This protocol is merely a transport
for the actual extensions to the bittorrent protocol and the extensions
named in the example above (such as ``p``) are just examples of possible
extensions.

rationale
---------

The reason why the extension messages' IDs would be defined in the handshake
is to avoid having a global registry of message IDs. Instead the names of the
extension messages requires unique names, which is much easier to do without
a global registry. The convention is to use a two letter prefix on the
extension message names, the prefix would identify the client first
implementing the extension message. e.g. ``LT_metadata`` is implemented by
libtorrent, and hence it has the ``LT`` prefix.

If the client supporting the extensions can decide which numbers the messages
it receives will have, it means they are constants within that client. i.e.
they can be used in ``switch`` statements. It's easy for the other end to
store an array with the ID's we expect for each message and use that for
lookups each time it sends an extension message.

The reason for having a dictionary instead of having an array (using
implicitly assigned index numbers to the extensions) is that if a client
want to disable some extensions, the ID numbers would change, and it wouldn't
be able to use constants (and hence, not use them in a ``switch``). If the
messages IDs would map directly to bittorrent message IDs, It would also make
it possible to map extensions in the handshake to existing extensions with
fixed message IDs.

The reasoning behind having a single byte as extended message identifier is
to follow the the bittorrent spec. with its single byte message identifiers.
It is also considered to be enough. It won't limit the total number of
extensions, only the number of extensions used simultaneously.

The reason for using single byte identifiers for the standardized handshake
identifiers is 1) The mainline DHT uses single byte identifiers. 2) Saves
bandwidth. The only advantage of longer messages is that it makes the
protocol more readable for a human, but the BT protocol wasn't designed to
be a human readable protocol, so why bother.

