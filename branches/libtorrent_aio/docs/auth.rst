===================================
BitTorrent authentication extension
===================================

:Author: Arvid Norberg, arvid@rasterbar.com
:Version: 0.16.4

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

BitTorrent authentication extension
-----------------------------------

This extension indends to cover any combination of the following use cases:

1. Verifying that a torrent is published by a trusted source
2. Have a swarm be private and having peers authenticate peers they connect to
3. Allow peers, with prior knowledge about each other's public key, authenticate in order to set up trusted connections to known peers (i.e. "friends")

These building blocks could be used for building a web of trust, private
swarms and trusted sources for content.

torrent file extension
----------------------

A .torrent file may have the following new fields (not inside the info-hash):

"publisher"
	containing the RSA public key of the publisher of the torrent. Private counterpart
	of this key that has the authority to allow new peers onto the swarm.

"signature"
	The RSA signature of the ``info`` dictionary (specifically, the encrypted SHA-1
	hash of the ``info`` dictionary).

These fields serve the purpose of satisfying use case (1), allowing downloaders to
verify that the torrent has a trusted source.

extension handshake
-------------------

In order to satisfy use case (2), any peer supporting this extension MUST verify
that each peer on the swarm it connects to and receive an incoming connection from
is authenticated by the publisher's public key, if the torrent is *private*.

A torrent is private if the ``info`` dictionary contains an integer key ``private``
set to 1.

The extension handshake dictionary ("m") SHOULD contain a new extension key "lt_auth".

For private torrents, the extension handshake dictionary
MUST contain the certificate granting this peer access to the torrent. The
certificate is a dictionary ``cert`` containing the ``info-hash``,
``pubkey`` (the peer's public key), ``expiry`` (posix time of when cert expires).
The ``cert`` dictionary MAY be extended with more fields.

Next to the ``cert`` entry is a string ``sig`` being the signature
of the SHA-1 hash of the bencoded representation of the ``cert`` dictionary.
The signature is required for private torrents, but not required for non-private
torrents.

An example extension handshake for a private torrent could look like this::

	{
		"cert": {
			"expiry": 1333242356,
			"info-hash": f0fb0ed2d73a28aece3a9e4f91c91cf60e0ea0a6,
			"pubkey":
				79eaeecbfc91b129c34880e3113fc9545e7169fed95a7950a335cfcef3dd2989c
				c80c69d9e66206cdb4e41eeae8b19234804cd55b3eb6dbee15a5362e3fa6eb33b
				a61297dfc63426832623efdf54816474189ddf1baad1d08cb614ed130f9744671
				a0c9bdf32747c284955ad9ae65db875f4f74f51e624710fe7c3db5ec8ce55f175
				b49131cace810d6a61a9fcd0fa4e41e466e09c2a18629f4bebb8dbf4746648122
				bfc8153e3e76ea16cae62e0e1608effa6bf2f1d1954d2e3ef2ca1db327f75a1aa
				711ccad6a564821824a2708c20a9184a221554d1228fdaee39ba3ce4134847fab
				91514d4ed720fabe8082e342dfce15ced93545bdc6
		},
		"m": {
			"lt_auth": 8
		}
		"sig":
			1767303857272cc9133253614712584a3c6be5f81cd777e9e6e99fbf23be27ca9
			d96c3ad596521f90dd4fb2291d6c9a6108625cf58ca836c3e7f15595306cd9311
			5c4736276cb4ee08b7f85a5eb26759bfb46b28f36cecfd73e71fac5cad8f22bb6
			32d07ab4e530587d4fc4d21ae1c52aacbefa0a0dbabb9e24d6c552aaa464a8b94
			97cc776e3b1b4051dbc9ef26952ab0e74188cbf1ea54f37309ef781d37b59ded7
			e6ce09a9531cc9916ecbe1fb93369da47ab79457e9709576f737e3d89e3774731
			d97ac8b1283dc46a382e4ddd5a155b17d6ded6683f508f111c5e974098315c509
			77d533ddb52f42fa5019342496a217483b622c3a
	}


This certificate would expire at ``Sat Mar 31 18:05:56 PDT 2012``. The values of ``sig``,
``info-hash`` and ``pubkey`` are binary strings, they are printed as hex in this example.
The RSA key size SHOULD be 2048 bits (256 bytes).

Whenconnecting to a peer, or accepting an incoming connection, for a private torrent,
the client MUST verify the validity of the incoming certificate. This is done by:

1. Verifying that the certificate has not expired.
2. Verifying that the ``info-hash`` matches the torrent the peers are connected over
3. Verifying that the signature ``sig``, is a valid signature made by the private counterpart of the public key of the publisher of the torrent (i.e. the ``publisher`` key in the torrent file)

If the certificate fails any one of those checks, the peer MUST be
disconnected without exchanging any more information. As an exception, before
disconnecting, the peer MAY send an ``lt_auto`` message saying the authentication
failed.

An example extension handshake for a non-private torrent could look like this::

	{
		"cert": {
			"pubkey":
				79eaeecbfc91b129c34880e3113fc9545e7169fed95a7950a335cfcef3dd2989c
				c80c69d9e66206cdb4e41eeae8b19234804cd55b3eb6dbee15a5362e3fa6eb33b
				a61297dfc63426832623efdf54816474189ddf1baad1d08cb614ed130f9744671
				a0c9bdf32747c284955ad9ae65db875f4f74f51e624710fe7c3db5ec8ce55f175
				b49131cace810d6a61a9fcd0fa4e41e466e09c2a18629f4bebb8dbf4746648122
				bfc8153e3e76ea16cae62e0e1608effa6bf2f1d1954d2e3ef2ca1db327f75a1aa
				711ccad6a564821824a2708c20a9184a221554d1228fdaee39ba3ce4134847fab
				91514d4ed720fabe8082e342dfce15ced93545bdc6
		},
		"m": {
			"lt_auth": 8
		}
	}

Note that the only required field in the ``cert`` in this case is the ``pubkey``,
The assumption in this case is that the public key is recognized by the client
matched by a local directory of trusted peers. This is to support use case (3).

extension message
-----------------

The ``lt_auth`` extension message is used to protect against man-in-the-middle attacks,
and peers trying to assume someone else's identity. To do this, the entire connection
is encrypted with RC4 following this message. If the connection is already encrypted
with RC4, this overrides that encryption by resetting the encryption state.

It has the following format:

	============= ============= ======================================================
	size          name          description
	============= ============= ======================================================
	uint8_t       type          0 = encrypt the connection with RC4 using the symmetric
	                            key passed in this message.
	                            1 = signature verification failed
	                            2 = info-hash verification failed
	                            3 = certificate expired
	------------- ------------- ------------------------------------------------------
	uint8_t[256]  rc4_key       If type is not 0, this field is not included.
	                            The symmetric key the sender of this message will use
	                            to encrypt every message following this one. The key
	                            is encrypted with the recipient's public key. The
	                            length of this field may vary, but for 2048 RSA keys
	                            it ends up being 256 bytes.
	============= ============= ======================================================

Note that this message is somewhat of a layer violation, since it reaches down and
starts encrypting the entire stream below itself. This is illustrated in the figure
below.

::

	+---------------------------+
	| BitTorrent messages       | -----+ lt_auth reaches down
	+---------------------------+      | to the layer below
	| (RC4 protocol encryption) | <----+
	+-------------+-------------+
	| TCP         | uTP         |
	+----------+--+-----+-------+

applications
------------

This is a low level building block and not a complete system for authenticated or
private swarms. It does allow for peers to verify that a torrent comes from a
trusted source (assuming their public key is known in advance). It also allows
for publishers to limit access to content.

For privacy reasons, it is possible to apply this extension in a way that each peer
uses separate identities (key pairs) for every torrent it participates in.

Issues not covered by this extension are:

1. How the distribution of signed certificates are distributed to peers from
   the signing authority (publisher), for use case (2).
2. How peers discover each other and and *pair* by recognizing each other's
   public keys for future trusted connections, for use case (3).
3. How peers learn about the public keys of trusted publishers, for use case (1).

