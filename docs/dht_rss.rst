======================================
BitTorrent extension for DHT RSS feeds
======================================

:Author: Arvid Norberg, arvid@rasterbar.com
:Version: Draft

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

BitTorrent extension for DHT RSS feeds
--------------------------------------

This is a proposal for an extension to the BitTorrent DHT to allow
for decentralized RSS feed like functionality.

The intention is to allow the creation of repositories of torrents
where only a single identity has the authority to add new content. For
this repository to be robust against network failures and resilient
to attacks at the source.

The target ID under which the repository is stored in the DHT, is the
SHA-1 hash of a feed name and the 512 bit public key. This private key
in this pair MUST be used to sign every item stored in the repository.
Every message that contain signed items MUST also include this key, to
allow the receiver to verify the key itself against the target ID as well
as the validity of the signatures of the items. Every recipient of a
message with feed items in it MUST verify both the validity of the public
key against the target ID it is stored under, as well as the validity of
the signatures of each individual item.

Any peer who is subscribing to a DHT feed SHOULD also participate in
regularly re-announcing items that it knows about. Every participant
SHOULD store items in long term storage, across sessions, in order to
keep items alive for as long as possible, with as few sources as possible.

As with normal DHT announces, the write-token mechanism is used to
prevent spoof attacks.

There are two new proposed messages, ``announce_item`` and ``get_item``.
Every valid item that is announced, should be stored. In a request to get items,
as many items as can fit in a normal UDP packet size should be returned. If
there are more items than can fit, a random sub-set should be returned.

*Is there a better heuristic here? Should there be a bias towards newer items?
If so, there needs to be a signed timestamp as well, which might get messy*

target ID
---------

The target, i.e. the ID in the DHT key space feeds are announced to, MUST always
be SHA-1(*feed_name* + *public_key*). Any request where this condition is not met,
MUST be dropped.

Using the feed name as part of the target means a feed publisher only needs one
public-private keypair for any number of feeds, as long as the feeds have different
names.

messages
--------

These are the proposed new message formats.

requesting items
----------------

.. parsed-literal::

	{
		"a":
		{
			"filter": *<variable size bloom-filter>*,
			"id": *<20 byte id of origin node>*,
			"key": *<64 byte public curve25519 key for this feed>*,
			"n": *<feed-name>*
			"target": *<target-id as derived from public key>*
		},
		"q": "get_item",
		"t": *<transaction-id>*,
		"y": "q",
	}

The ``target`` MUST always be SHA-1(*feed_name* + *public_key*). Any request where
this condition is not met, MUST be dropped.

The ``n`` field is the name of this feed. It MUST be UTF-8 encoded string and it
MUST match the name of the feed in the receiving node.

The bloom filter argument (``filter``) in the ``get_item`` requests is optional.
If included in a request, it represents info-hashes that should be excluded from
the response. In this case, the response should be a random subset of the non-excluded
items, or all of the non-excluded items if they all fit within a packet size.

If the bloom filter is specified, its size MUST be an even multiple of 8 bits. The size
is implied by the length of the string. For each info-hash to exclude from the response,

There are no hash functions for the bloom filter. Since the info-hash is already a
hash digest, each pair of bytes, starting with the first bytes (MSB), are used as the
results from the imaginary hash functions for the bloom filter. k is 3 in this bloom
filter. This means the first 6 bytes of the info-hash is used to set 3 bits in the bloom
filter. The pairs of bytes pulled out from the info-hash are interpreted as a big-endian
16 bit value.

Bits are indexed in bytes from left to right, and within bytes from LSB to MSB. i.e., to
set bit 12: ``bitfield[12/8] |= (12 % 8)``.

Example:
	To indicate that you are not interested in knowing about the info-hash that
	starts with 0x4f7d25a... and you choose a bloom filter of size 80 bits. Set bits
	(0x4f % 80), (0x7d % 80) and (0x25 % 80) in the bloom filter bitmask.


request item response
---------------------

.. parsed-literal::

	{
		"r":
		{
			"ih":
			[
				*<n * 20 byte(s) info-hash>*,
				...
			],
			"sig":
			[
				*<64 byte curve25519 signature of info-hash>*,
				...
			],
			"id": *<20 byte id of origin node>*,
			"token": *<write-token>*
			"nodes": *<n * compact IPv4-port pair>*
			"nodes6": *<n * compact IPv6-port pair>*
		},
		"t": *<transaction-id>*,
		"y": "r",
	}

Since the data that's being signed by the public key already is a hash (i.e.
an info-hash), the signature of each hash-entry is simply the hash encrypted
by the feed's private key.

The ``ih`` and ``sig`` lists MUST have equal number of items. Each item in ``sig``
is the signature of the full string in the corresponding item in the ``ih`` list.

Each item in the ``ih`` list may contain any positive number of 20 byte info-hashes.

The rationale behind using lists of strings where the strings contain multiple
info-hashes is to allow the publisher of a feed to sign multiple info-hashes
together, and thus saving space in the UDP packets, allowing nodes to transfer more
info-hashes per packet. Original publishers of a feed MAY re-announce items lumped
together over time to make the feed more efficient.

A client receiving a ``get_item`` response MUST verify each signature in the ``sig``
list against each corresponding item in the ``ih`` list using the feed's public key.
Any item whose signature

``nodes`` and ``nodes6`` are optional and have the same semantics as the standard
``get_peers`` request. The intention is to be able to use this ``get_item`` request
in the same way, searching for the nodes responsible for the feed.

announcing items
----------------

.. parsed-literal::

	{
		"a":
		{
			"ih":
			[
				*<n * 20 byte info-hash(es)>*,
				...
			],
			"sig":
			[
				*<64 byte curve25519 signature of info-hash(es)>*,
				...
			],
			"id": *<20 byte node-id of origin node>*,
			"key": *<64 byte public curve25519 key for this feed>*,
			"n": *<feed name>*
			"target": *<target-id as derived from public key>*,
			"token": *<write-token as obtained by previous req.>*
		},
		"y": "q",
		"q": "announce_item",
		"t": *<transaction-id>*
	}

An announce can include any number of items, as long as they fit in a packet.

Subscribers to a feed SHOULD also announce items that they know of, to the feed.
In order to make the repository of torrents as reliable as possible, subscribers
SHOULD announce random items from their local repository of items. When re-announcing
items, a random subset of all known items should be announced, randomized
independently for each node it's announced to. This makes it a little bit harder
to determine the IP address an item originated from, since it's a matter of
seeing the first announce, and knowing that it wasn't announced anywhere else
first.

Any subscriber and publisher SHOULD re-announce items every 30 minutes. If
a feed does not receive any announced items in 60 minutes, a peer MAY time
it out and remove it.

Subscribers and publishers SHOULD announce random items.

Example
.......

::

	{
		"a":
		{
			"ih":
			[
				"7ea94c240691311dc0916a2a91eb7c3db2c6f3e4",
				"0d92ad53c052ac1f49cf4434afffafa4712dc062e4168d940a48e45a45a0b10808014dc267549624"
			],
			"sig":
			[
				"980774404e404941b81aa9da1da0101cab54e670cff4f0054aa563c3b5abcb0fe3c6df5dac1ea25266035f09040bf2a24ae5f614787f1fe7404bf12fee5e6101",
				"3fee52abea47e4d43e957c02873193fb9aec043756845946ec29cceb1f095f03d876a7884e38c53cd89a8041a2adfb2d9241b5ec5d70268714d168b9353a2c01"
			],
			"id": "b46989156404e8e0acdb751ef553b210ef77822e",
			"key": "6bc1de5443d1a7c536cdf69433ac4a7163d3c63e2f9c92d78f6011cf63dbcd5b638bbc2119cdad0c57e4c61bc69ba5e2c08b918c2db8d1848cf514bd9958d307",
			"n": "my stuff"
			"target": "b4692ef0005639e86d7165bf378474107bf3a762"
			"token": "23ba"
		},
		"y": "q",
		"q": "announce_item",
		"t": "a421"
	}

Strings are printed in hex for printability, but actual encoding is binary. The
response contains 3 feed items, starting with "7ea94c", "0d92ad" and "e4168d".
These 3 items are not published optimally. If they were to be merged into a single
string in the ``ih`` list, more than 64 bytes would be saved (because of having
one less signature).

Note that ``target`` is in fact SHA1('my stuff' + 'key'). The private key
used in this example is 980f4cd7b812ae3430ea05af7c09a7e430275f324f42275ca534d9f7c6d06f5b.


URI Scheme
----------

The proposed URI scheme for DHT feeds is:

.. parsed-literal::

	magnet:?xt=btfd:*<base16-curve25519-public-key>* &dn= *<feed name>*

Note that a difference from regular torrent magnet links is the **btfd**
versus **btih** used in regular magnet links to torrents.

The *feed name* is mandatory since it is used in the request and when
calculating the target ID.

rationale
---------

The reason to use curve25519_ instead of, for instance, RSA is to fit more signatures
(i.e. items) in a single DHT packet. One packet is typically restricted to between
1280 - 1480 bytes. According to http://cr.yp.to/, curve25519 is free from patent claims
and there are open implementations in both C and Java.

.. _curve25519: http://cr.yp.to/ecdh.html

