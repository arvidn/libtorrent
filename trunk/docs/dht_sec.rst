=================================
BitTorrent DHT security extension
=================================

:Author: Arvid Norberg, arvid@rasterbar.com
:Version: 1.0.0

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

BitTorrent DHT security extension
---------------------------------

The purpose of this extension is to make it harder to launch a few
specific attacks against the BitTorrent DHT and also to make it harder
to snoop the network.

Specifically the attack this extension intends to make harder is launching
8 or more DHT nodes which node-IDs selected close to a specific target
info-hash, in order to become the main nodes hosting peers for it. Currently
this is very easy to do and lets the attacker not only see all the traffic
related to this specific info-hash but also block access to it by other
peers.

The proposed guard against this is to enforce restrictions on which node-ID
a node can choose, based on its external IP address.

considerations
--------------

One straight forward scheme to tie the node ID to an IP would be to hash
the IP and force the node ID to share the prefix of that hash. One main
draw back of this approach is that an entities control over the DHT key
space grows linearly with its control over the IP address space.

In order to successfully launch an attack, you just need to find 8 IPs
whose hash will be *closest* to the target info-hash. Given the current
size of the DHT, that is quite likely to be possible by anyone in control
of a /8 IP block.

The size of the DHT is approximately 8.4 million nodes. This is estmiated
by observing that a typical routing table typically has about 20 of its
top routing table buckets full. That means the key space is dense enough
to contain 8 nodes for every combination of the 20 top bits of node IDs.

	``2^20 * 8 = 8388608``

By controlling that many IP addresses, an attacker could snoop any info-hash.
By controlling 8 times that many IP addresses, an attacker could actually
take over any info-hash.

With IPv4, snooping would require a /8 IP block, giving access to 16.7 million
Ips.

Another problem with hashing the IP is that multiple users behind a NAT are
forced to run their DHT nodes on the same node ID.

Node ID restriction
-------------------

In order to avoid the number node IDs controlled to grow linearly by the number
of IPs, as well as allowing more than one node ID per external IP, the node
ID can be restricted at each class level of the IP.

Another important property of the restriction put on node IDs is that the
distribution of the IDs remoain uniform. This is why CRC32C (Castagnoli) was
chosen as the hash function.

The expression to calculate a valid ID prefix (from an IPv4 address) is::

	crc32c((ip & 0x030f3fff) | (r << 29))

And for an IPv6 address (``ip`` is the high 64 bits of the address)::

	crc32c((ip & 0x0103070f1f3f7fff) | (r << 61))

``r`` is a random number in the range [0, 7]. The resulting integer,
representing the masked IP address is supposed to be big-endian before
hashed. The "|" operator means bit-wise OR.

The details of implementing this is to evaluate the expression, store the
result in a big endian 64 bit integer and hash those 8 bytes with CRC32C.

The first (most significant) 21 bits of the node ID used in the DHT MUST
match the first 21 bits of the resulting hash. The last byte of the hash MUST
match the random number (``r``) used to generate the hash.

.. image:: ip_id_v4.png
.. image:: ip_id_v6.png

Example code code for calculating a valid node ID::

	uint8_t* ip; // our external IPv4 or IPv6 address (network byte order)
	int num_octets; // the number of octets to consider in ip (4 or 8)
	uint8_t node_id[20]; // resulting node ID

	uint8_t v4_mask[] = { 0x03, 0x0f, 0x3f, 0xff };
	uint8_t v6_mask[] = { 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff };
	uint8_t* mask = num_octets == 4 ? v4_mask : v6_mask;

	for (int i = 0; i < num_octets; ++i)
		ip[i] &= mask[i];

	uint32_t rand = std::rand() & 0xff;
	uint8_t r = rand & 0x7;
	ip[0] |= r << 5;

	uint32_t crc = 0;
	crc = crc32c(crc, ip, num_octets);

	// only take the top 21 bits from crc
	node_id[0] = (crc >> 24) & 0xff;
	node_id[1] = (crc >> 16) & 0xff;
	node_id[2] = ((crc >> 8) & 0xf8) | (std::rand() & 0x7);
	for (int i = 3; i < 19; ++i) node_id[i] = std::rand();
	node_id[19] = rand;

test vectors:

.. parsed-literal::

	IP           rand  example node ID
	============ ===== ==========================================
	124.31.75.21   1   **5fbfbf** f10c5d6a4ec8a88e4c6ab4c28b95eee4 **01**
	21.75.31.124  86   **5a3ce9** c14e7a08645677bbd1cfe7d8f956d532 **56**
	65.23.51.170  22   **a5d432** 20bc8f112a3d426c84764f8c2a1150e6 **16**
	84.124.73.14  65   **1b0321** dd1bb1fe518101ceef99462b947a01ff **41**
	43.213.53.83  90   **e56f6c** bf5b7c4be0237986d5243b87aa6d5130 **5a**

The bold parts of the node ID are the important parts. The rest are
random numbers. The last bold number of each row has only its most significant
bit pulled from the CRC32C function. The lower 3 bits are random.

bootstrapping
-------------

In order to set ones initial node ID, the external IP needs to be known. This
is not a trivial problem. With this extension, *all* DHT responses SHOULD include
a *top-level* field called ``ip``, containing a compact binary representation of
the requestor's IP and port. That is big endian IP followed by 2 bytes of big endian
port.

The IP portion is the same byte sequence used to verify the node ID.

It is important that the ``ip`` field is in the top level dictionary. Nodes that
enforce the node-ID will respond with an error message ("y": "e", "e": { ... }),
whereas a node that supports this extension but without enforcing it will respond
with a normal reply ("y": "r", "r": { ... }).

A DHT node which receives an ``ip`` result in a request SHOULD consider restarting
its DHT node with a new node ID, taking this IP into account. Since a single node
can not be trusted, there should be some mechanism to determine whether or
not the node has a correct understanding of its external IP or not. This could
be done by voting, or only restart the DHT once at least a certain number of
nodes, from separate searches, tells you your node ID is incorrect.

rationale
---------

The choice of using CRC32C instead of a more traditional cryptographic hash
function is justified primarily of these reasons:

1. it is a fast function
2. produces well distributed results
3. there is no need for the hash function to be one-way (the input set is
   so small that any hash function could be reversed).
4. CRC32C (Castagnoli) is supported in hardware by SSE 4.2, which can
   significantly speed up computation

There are primarily two tests run on SHA-1 and CRC32C to establish the
distribution of results. The first one is the number of bits in the output
set that contain every possible combination of bits. The CRC32C function
has a longer such prefix in its output than SHA-1. This means nodes will still
have well uniformly distributed IDs, even when IP addresses in use are not
uniformly distributed.

The following graph illustrate a few different hash functions with regard
to this property.

.. image:: complete_bit_prefixes.png

This test takes into account IP addresses that are not globally routable, i.e.
reserved for local networks, multicast and other things. It also takes into
account that some /8 blocks are not in use by end-users and exremely unlikely
to ever run a DHT node. This makes the results likely to be very similar to
what we would see in the wild.

These results indicate that CRC32C provides the best uniformity in the results
in terms of bit prefixes where all possibilities are represented, and that
no more than 21 bits should be used from the result. If more than 21 bits
were to be used, there would be certain node IDs that would be impossible to
have, which would make routing sub-optimal.

The second test is more of a sanity test for the uniform distribution property.
The target space (32 bit interger) is divided up into 1000 buckets. Every valid
IP and ``r`` input is run through the algorithm and the result is put in the
bucket it falls in. The expectation is that each bucket has roughly an equal
number of results falling into it. The following graph shows the resulting
histogram, comparing SHA-1 and CRC32C.

.. image:: hash_distribution.png

The source code for these tests can be found here_.

.. _here: https://github.com/arvidn/hash_complete_prefix

The reason to use CRC32C instead of the CRC32 implemented by zlib is that
Intel CPUs have hardware support for the CRC32C calculations. The input
being exactly 4 bytes is also deliberate, to make it fit in a single
instruction.

enforcement
-----------

Once enforced, write tokens from peers whose node ID does not match its external
IP should be considered dropped. In other words, a peer that uses a non-matching
ID MUST never be used to store information on, regardless of which request. In the
original DHT specification only ``announce_peer`` stores data in the network,
but any future extension which stores data in the network SHOULD use the same
restriction.

Any peer on a local network address is exempt from this node ID verification.
This includes the following IP blocks:

10.0.0.0/8
	reserved for local networks
172.16.0.0/12
	reserved for local networks
192.168.0.0/16
	reserved for local networks
169.254.0.0/16
	reserved for self-assigned IPs
127.0.0.0/8
	reserved for loopback


backwards compatibility and transition
--------------------------------------

During some transition period, this restriction should not be enforced, and
peers whose node ID does not match this formula relative to their external IP
should not be blocked.

Requests from peers whose node ID does not match their external IP should
always be serviced, even after the transition period. The attack this protects
from is storing data on an attacker's node, not servicing an attackers request.

forward compatibility
---------------------

If the total size of the DHT grows to the point where the inherent size limit
in this proposal is too small, the modulus constants can be updated in a new
proposal, and another transition period where both sets of modulus constants
are accepted.

