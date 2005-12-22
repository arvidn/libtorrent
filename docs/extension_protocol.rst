:Author: Arvid Norberg, arvid@rasterbar.com

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

extension protocol
==================

The extension protocol is based on a protocol designed by Nolar, one of the
developers of Azureus. Azureus never ended up using it though.

To advertise to other clients that you support the extension protocol, the
peer-id should end with the three ascii letters ``ext``.

Once support for the protocol is established, the client is supposed to
support 2 new messages:

+-------------------+----+
|name               | id |
+===================+====+
|``extension_list`` | 20 |
+-------------------+----+
|``extended``       | 21 |
+-------------------+----+

Both these messages are sent as any other bittorrent message, with a 4 byte
length prefix and a single byte identifying the message.

extension_list
--------------

The payload in the ``extension_list`` message is a bencoded dictionary mapping
names of extensions to identification numbers of each extension. The only
requirement on the identification numbers is that no extensions share the
same, they have to be unique within the message.

An example of how an ``extension_list`` message looks like:

+----------------------+---+
| ``metadata``         | 0 |
+----------------------+---+
| ``listen_port``      | 1 |
+----------------------+---+

and in the encoded form:

``d8:metadatai0e11:listen_porti1ee``

This message is sent immediately after the handshake to any peer that supports
the extension protocol.

The IDs must be stored for every peer, becuase every peer may have different
IDs for the same extension.

extended
--------

The ``extended`` message is the transport message for the extensions. The
first 4 bytes of the message's payload is an ID that corresponds to one of
the extensions the peer supports. As usual, it is encoded in big-endian.
When you send an ``extended`` message, you should only use IDs you previously
got from this peer's ``extension_list``.

The payload after this ID depends on the extension.


