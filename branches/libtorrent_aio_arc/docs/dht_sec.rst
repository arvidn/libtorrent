=================================
BitTorrent DHT security extension
=================================

:Author: Arvid Norberg, arvid@rasterbar.com
:Version: Draft

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

The expression to calculate a valid ID prefix (from an IPv4 address) is::

	sha1((A * (B * (C * (D * (rand() % 8) % 0x100) % 0x4000) % 0x100000)) % 0x4000000)

Where ``A``, ``B``, ``C`` and ``D`` are the four octets of an IPv4 address.

The pattern is that the modulus constant is shifted left by 6 for each octet.
It generalizes to IPv6 by only considering the first 64 bit of the IP (since
the low 64 bits are controlled by the host) and shifting the modulus by 3 for
each octet instead.

The details of implementing this is to evaluate the expression, store the
result in a big endian 32 bit integer and hash those 4 bytes with SHA-1.
The first 4 bytes of the node ID used in the DHT MUST match the first 4
bytes in the resulting hash. The last byte of the hash MUST match the
random number used to generate the hash.

.. image:: ip_id_v4.png
.. image:: ip_id_v6.png

Example code code for calculating a valid node ID::

	uint8_t* ip; // our external IPv4 or IPv6 address (network byte order)
	int num_octets; // the number of octets to consider in ip (4 or 8)
	uint8_t node_id[20]; // resulting node ID

	uint32_t rand = rand() & 0xff;
	uint32_t modulus = 0x100;
	uint32_t seed = rand & 0x7;
	int mod_shift = 6 * 4 / num_octets; // 6 or 3, depending on IPv4 and IPv6
	while (num_octets)
	{
		seed = (uint64_t(seed) * ip[num_octets-1]) & (modulus-1);
		modulus <<= mod_shift;
		--num_octets;
	}

	seed = htonl(seed);
	SHA_CTX ctx;
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, (unsigned char*)&seed, sizeof(seed));
	SHA1_Final(&ctx, node_id);
	for (int i = 4; i < 19; ++i) node_id[i] = rand();
	node_id[19] = rand;

Example code to verify a node ID::

	uint8_t* ip; // incoming IPv4 or IPv6 address (network byte order)
	int num_octets; // the number of octets to consider in ip (4 or 8)
	uint8_t node_id[20]; // incoming node ID

	uint32_t modulus = 0x100;
	uint32_t seed = node_id[19] & 0x7;
	int mod_shift = 6 * 4 / num_octets; // 6 or 3, depending on IPv4 and IPv6
	while (num_octets)
	{
		seed = (uint64_t(seed) * ip[num_octets-1]) & (modulus-1);
		modulus <<= mod_shift;
		--num_octets;
	}

	seed = htonl(seed);
	SHA_CTX ctx;
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, (unsigned char*)&seed, sizeof(seed));
	uint8_t digest[20];
	SHA1_Final(&ctx, digest);
	if (memcmp(digest, node_id, 4) != 0)
		return false; // failed verification
	else
		return true; // verification passed

test vectors:

bootstrapping
-------------

In order to set ones initial node ID, the external IP needs to be known. This
is not a trivial problem. WIth this extension, *all* DHT requests whose node
ID does not match its IP address MUST be serviced and MUST also include one
extra result value (inside the ``r`` dictionary) called ``ip``. The IP field
contains the raw (big endian) byte representation of the external IP address.
This is the same byte sequence passed to SHA-1.

A DHT node which receives an ``ip`` result in a request SHOULD consider restarting
its DHT node with a new node ID, taking this IP into account. Since a single node
can not be trusted, there should be some mechanism of determining whether or
not the node has a correct understanding of its external IP or not. This could
be done by voting, or only restart the DHT once at least a certain number of
nodes, from separate searches, tells you your node ID is incorrect.

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

