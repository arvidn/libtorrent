======================================
BitTorrent extension for DHT RSS feeds
======================================

:Author: Arvid Norberg, arvid@libtorrent.org
:Version: 1.1.0

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

This proposal has been superseded by the dht_put_ feature. This may
still be implemented on top of that.

.. _dht_put: dht_store.html

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

As with normal DHT announces, the write-token mechanism is used to
prevent IP spoof attacks.

terminology
-----------

In this document, a *storage node* refers to the node in the DHT to which
an item is being announce. A *subscribing node* refers to a node which
makes look ups in the DHT to find the storage nodes, to request items
from them.

linked lists
------------

Items are chained together in a geneal singly linked list. A linked
list does not necessarily contain RSS items, and no RSS related items
are mandatory. However, RSS items will be used as examples in this BEP::

	key = SHA1(name + key)
	+---------+
	| head    |           key = SHA1(bencode(item))
	| +---------+         +---------+
	| | next    |-------->| item    |          key = SHA1(bencode(item))
	| | key     |         | +---------+        +---------+
	| | name    |         | | next    |------->| item    |
	| | seq     |         | | key     |        | +---------+
	| | ...     |         | | ...     |        | | next    |--->0
	| +---------+         | +---------+        | | key     |
	| sig     |           | sig     |          | | ...     |
	+---------+           +---------+          | +---------+
	                                           | sig     |
	                                           +---------+

The ``next`` pointer is at least 20 byte ID in the DHT key space pointing to where the next
item in the list is announced. The list is terminated with an ID of all zeroes.

The ID an items is announced to is determined by the SHA1 hash of the bencoded representation
of the item iteself. This contains all fields in the item, except the signature.
The only mandatory fields in an item are ``next``, ``key`` and ``sig``.

The ``key`` field MUST match the public key of the list head node. The ``sig`` field
MUST be the signature of the bencoded representation of ``item`` or ``head`` (whichever
is included in the message).

All subscribers MUST verify that the item is announced under the correct DHT key
and MUST verify the signature is valid and MUST verify the public key is the same
as the list-head. If a node fails any of these checks, it must be ignored and the
chain of items considered terminated.

Each item holds a bencoded dictionary with arbitrary keys, except two mandatory keys:
``next`` and ``key``. The signature ``sig`` is transferred outside of this dictionary
and is the signature of all of it. An implementation should stora any arbitrary keys that
are announced to an item, within reasonable restriction such as nesting, size and numeric
range of integers.

skip lists
----------

The ``next`` key stored in the list head and the items is a string of at least length
20 bytes, it may be any length divisible by 20. Each 20 bytes are the ID of the next
item in the list, the item 2 hops away, 4 hops away, 8 hops away, and so on. For
simplicity, only the first ID (1 hop) in the ``next`` field is illustrated above.

A publisher of an item SHOULD include as many IDs in the ``next`` field as the remaining
size of the list warrants, within reason.

These skip lists allow for parallelized lookups of items and also makes it more efficient
to search for specific items. It also mitigates breaking lists missing some items.

Figure of the skip list in the first list item::

	n      Item0  Item1  Item2  Item3  Item4  Item5  Item6  Item7  Item8  Item9  Item10
	0        O----->
	20       O------------>
	40       O-------------------------->
	60       O------------------------------------------------------>

*n* refers to the byte offset into the ``next`` field.

list-head
---------

The list head item is special in that it can be updated, without changing its
DHT key. This is required to prepend new items to the linked list. To authenticate
that only the original publisher can update the head, the whole linked list head
is signed. In order to avoid a malicious node to overwrite the list head with an old
version, the sequence number ``seq`` must be monotonically increasing for each update,
and a node hosting the list node MUST not downgrade a list head from a higher sequence
number to a lower one, only upgrade.

The list head's DHT key (which it is announced to) MUST be the SHA1 hash of the name
(``n``) and ``key`` fields concatenated.

Any node MUST reject any list head which is announced under any other ID.

messages
--------

These are the messages to deal with linked lists.

The ``id`` field in these messages has the same semantics as the standard DHT messages,
i.e. the node ID of the node sending the message, to maintain the structure of the DHT
network.

The ``token`` field also has the same semantics as the standard DHT message ``get_peers``
and ``announce_peer``, when requesting an item and to write an item respectively.

``nodes`` and ``nodes6`` has the same semantics as in its ``get_peers`` response.

requesting items
................

This message can be used to request both a list head and a list item. When requesting
a list head, the ``n`` (name) field MUST be specified. When requesting a list item the
``n`` field is not required.

.. parsed-literal::

	{
	   "a":
	   {
	      "id": *<20 byte ID of sending node>*,
	      "key": *<64 byte public curve25519 key for this list>*,
	      "n": *<list name>*
	      "target": *<target-id for 'head' or 'item'>*
	   },
	   "q": "get_item",
	   "t": *<transaction-id>*,
	   "y": "q",
	}

When requesting a list-head the ``target`` MUST always be SHA-1(*feed_name* + *public_key*).
``target`` is the target node ID the item was written to.

The ``n`` field is the name of the list. If specified, It MUST be UTF-8 encoded string
and it MUST match the name of the feed in the receiving node.

request item response
.....................

This is the format of a response of a list head:

.. parsed-literal::

	{
	   "r":
	   {
	      "head":
	      {
	         "key": *<64 byte public curve25519 key for this list>*,
	         "next": *<20 bytes item ID>*,
	         "n": *<name of the linked list>*,
	         "seq": *<monotonically increasing sequence number>*
	      },
	      "sig": *<curve25519 signature of 'head' entry (in bencoded form)>*,
	      "id": *<20 byte id of sending node>*,
	      "token": *<write-token>*,
	      "nodes": *<n * compact IPv4-port pair>*,
	      "nodes6": *<n * compact IPv6-port pair>*
	   },
	   "t": *<transaction-id>*,
	   "y": "r",
	}

This is the format of a response of a list item:

.. parsed-literal::

	{
	   "r":
	   {
	      "item":
	      {
	         "key": *<64 byte public curve25519 key for this list>*,
	         "next": *<20 bytes item ID>*,
	         ...
	      },
	      "sig": *<curve25519 signature of 'item' entry (in bencoded form)>*,
	      "id": *<20 byte id of sending node>*,
	      "token": *<write-token>*,
	      "nodes": *<n * compact IPv4-port pair>*,
	      "nodes6": *<n * compact IPv6-port pair>*
	   },
	   "t": *<transaction-id>*,
	   "y": "r",
	}

A client receiving a ``get_item`` response MUST verify the signature in the ``sig``
field against the bencoded representation of the ``item`` field, using the ``key`` as
the public key. The ``key`` MUST match the public key of the feed.

The ``item`` dictionary MAY contain arbitrary keys, and all keys MUST be stored for
items.

announcing items
................

The message format for announcing a list head:

.. parsed-literal::

	{
	   "a":
	   {
	      "head":
	      {
	         "key": *<64 byte public curve25519 key for this list>*,
	         "next": *<20 bytes item ID>*,
	         "n": *<name of the linked list>*,
	         "seq": *<monotonically increasing sequence number>*
	      },
	      "sig": *<curve25519 signature of 'head' entry (in bencoded form)>*,
	      "id": *<20 byte node-id of origin node>*,
	      "target": *<target-id as derived from public key and name>*,
	      "token": *<write-token as obtained by previous request>*
	   },
	   "y": "q",
	   "q": "announce_item",
	   "t": *<transaction-id>*
	}

The message format for announcing a list item:

.. parsed-literal::

	{
	   "a":
	   {
	      "item":
	      {
	         "key": *<64 byte public curve25519 key for this list>*,
	         "next": *<20 bytes item ID>*,
	         ...
	      },
	      "sig": *<curve25519 signature of 'item' entry (in bencoded form)>*,
	      "id": *<20 byte node-id of origin node>*,
	      "target": *<target-id as derived from item dict>*,
	      "token": *<write-token as obtained by previous request>*
	   },
	   "y": "q",
	   "q": "announce_item",
	   "t": *<transaction-id>*
	}

A storage node MAY reject items and heads whose bencoded representation is
greater than 1024 bytes.

re-announcing
-------------

In order to keep feeds alive, subscriber nodes SHOULD help out in announcing
items they have downloaded to the DHT.

Every subscriber node SHOULD store items in long term storage, across sessions,
in order to keep items alive for as long as possible, with as few sources as possible.

Subscribers to a feed SHOULD also announce items that they know of, to the feed.
Since a feed may have many subscribers and many items, subscribers should re-announce
items according to the following algorithm.

.. parsed-literal::

	1. pick one random item (*i*) from the local repository (except
	   items already announced this round)
	2. If all items in the local repository have been announced
	  2.1 terminate
	3. look up item *i* in the DHT
	4. If fewer than 8 nodes returned the item
	  4.1 announce *i* to the DHT
	  4.2 goto 1

This ensures a balanced load on the DHT while still keeping items alive

timeouts
--------

Items SHOULD be announced to the DHT every 30 minutes. A storage node MAY time
out an item after 60 minutes of no one announcing it.

A storing node MAY extend the timeout when it receives a request for it. Since
items are immutable, the data doesn't go stale. Therefore it doesn't matter if
the storing node no longer is in the set of the 8 closest nodes.

RSS feeds
---------

For RSS feeds, following keys are mandatory in the list item's ``item`` dictionary.

ih
	The torrent's info hash

size
	The size (in bytes) of all files the torrent

n
	name of the torrent

example
.......

This is an example of an ``announce_item`` message:

.. parsed-literal::

	{
	   "a":
	   {
	      "item":
	      {
	         "key": "6bc1de5443d1a7c536cdf69433ac4a7163d3c63e2f9c92d
	            78f6011cf63dbcd5b638bbc2119cdad0c57e4c61bc69ba5e2c08
	            b918c2db8d1848cf514bd9958d307",
	         "info-hash": "7ea94c240691311dc0916a2a91eb7c3db2c6f3e4",
	         "size": 24315329,
	         "n": "my stuff",
	         "next": "c68f29156404e8e0aas8761ef5236bcagf7f8f2e"
	      }
	      "sig": *<signature>*
	      "id": "b46989156404e8e0acdb751ef553b210ef77822e",
	      "target": "b4692ef0005639e86d7165bf378474107bf3a762"
	      "token": "23ba"
	   },
	   "y": "q",
	   "q": "announce_item",
	"t": "a421"
	}

Strings are printed in hex for printability, but actual encoding is binary.

Note that ``target`` is in fact SHA1 hash of the same data the signature ``sig``
is the signature of, i.e.::

	d9:info-hash20:7ea94c240691311dc0916a2a91eb7c3db2c6f3e43:key64:6bc1de5443d1
	a7c536cdf69433ac4a7163d3c63e2f9c92d78f6011cf63dbcd5b638bbc2119cdad0c57e4c61
	bc69ba5e2c08b918c2db8d1848cf514bd9958d3071:n8:my stuff4:next20:c68f29156404
	e8e0aas8761ef5236bcagf7f8f2e4:sizei24315329ee

(note that binary data is printed as hex)

RSS feed URI scheme
--------------------

The proposed URI scheme for DHT feeds is:

.. parsed-literal::

	magnet:?xt=btfd:*<base16-curve25519-public-key>* &dn= *<feed name>*

Note that a difference from regular torrent magnet links is the **btfd**
versus **btih** used in regular magnet links to torrents.

The *feed name* is mandatory since it is used in the request and when
calculating the target ID.

rationale
---------

The reason to use curve25519_ instead of, for instance, RSA is compactness. According to
http://cr.yp.to/, curve25519 is free from patent claims and there are open implementations
in both C and Java.

.. _curve25519: http://cr.yp.to/ecdh.html

