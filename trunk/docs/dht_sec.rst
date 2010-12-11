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

node IDs
--------

The proposed formula for restricting node IDs is that the 4 first bytes of
the node ID MUST match the 4 first bytes of ``SHA-1(IP_address)``. That is,
the raw, big endian, storage of the address, either IPv4 or IPv6, hashed
with SHA-1.

Example:

	An IP address 89.5.5.5 has a big endian byte representation of
	``0x59 0x05 0x05 0x05``. The SHA-1 hash of this byte sequence is
	``656d41da810a0a6d92fd2f6a8ba3b466e35ab368``. The DHT node must choose
	a node ID which starts with ``656d41da``.

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

Write tokens from peers whose node ID does not match its external IP should be
considered dropped. In other words, a peer that uses a non-matching ID MUST
never be used to store information on, regardless of which request. In the
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

