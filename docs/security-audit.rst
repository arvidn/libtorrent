============================
Security audit of libtorrent
============================

:Author: Arvid Norberg, arvid@libtorrent.org

In the 4th quarter of 2020 `Mozilla Open Source Support Awards`__ commissioned a
security audit of libtorrent, to be performed by `include security`_.

__ https://www.mozilla.org/en-US/moss/
.. _`include security`: https://includesecurity.com/

The full report from the audit can be found here_.

.. _here: 2020\ Q4\ Mozilla\ Libtorrent\ Report\ Public\ Report.pdf

This document discusses the issues raised by the report as well as describes the
changes made to libtorrent in response to it. These changes were included in
libtorrent version 1.2.12 and version 2.0.2.

Comments on this document are welcome through any of these means:

* email them to ``arvid@libtorrent.org``
* email to libtorrent `mailing list`_
* an issue on github_.

.. _`mailing list`: https://sourceforge.net/projects/libtorrent/lists/libtorrent-discuss
.. _github: https://github.com/arvidn/libtorrent/issues

.. contents:: issues brought up in the report

F1: Server-Side Request Forgery (SSRF)
======================================

For background, see `OWASP definition of SSRF`__.

__ https://owasp.org/www-community/attacks/Server_Side_Request_Forgery

Running a tracker on the local network is an established use case for
BitTorrent (here__). Filtering all tracker requests to the local network is not feasible.
Running a tracker on the loopback device would seem to only make sense for
testing.

__ https://github.com/AVBIT/retracker_local

The SSRF issue is not limited to tracker URLs, but also applies to web seeds. A
web seed can be embedded in a .torrent file as well as included in a magnet
link.

The report says:

	If user-controllable URLs must be requested then sanitizing them in a manner
	similar to the SafeCurl library is recommended (see the link in the reference
	section).

The `SafeCurl library`__, as I understand is, sanitizes URLs based on include-
and exclude lists of host names, IP addresses, ports, schemes.

__ https://github.com/wkcaj/safecurl/blob/master/src/fin1te/SafeCurl/Url.php

tracker and web seed protocols
------------------------------

Tracker URLs can be arbitrary URLs that libtorrent appends certain query string
parameters to (like ``&info_hash=`` etc.). The path component of a tracker URL
is typically not relevant, and most trackers follow the convention of using
``/announce``.

A web seed for a multi-file torrent cannot include any query string arguments
and libtorrent will append the path to the file that's being requested. However,
the response from the web seed can *redirect* to any arbitrary URL, including on
the local network. A web seed for a single-file torrent can be any arbitrary URL.

Web seed HTTP requests will almost always be a range request (unless the file is
so small to fit in one or a few pieces).

What heuristics and restrictions could libtorrent implement to mitigate attacks?

Both trackers and web seeds only use HTTP ``GET`` request, i.e. no ``POST`` for
example. This ought to protect certain APIs that mutate state.

The examples in the OWASP article are:

Cloud server meta-data
----------------------

	Cloud services such as AWS provide a REST interface on
	``http://169.254.169.254/`` where important configuration and sometimes even
	authentication keys can be extracted

The response from a REST API would have to be compatible with the
BitTorrent tracker protocol, which is a bencoded structure with specific keys
being mandatory (the protocol is defined here__, with amendments here__,
here__ and here__).

__ https://www.bittorrent.org/beps/bep_0003.html#trackers
__ https://www.bittorrent.org/beps/bep_0023.html
__ https://www.bittorrent.org/beps/bep_0007.html
__ https://www.bittorrent.org/beps/bep_0048.html

A tracker response that doesn't match this protocol will be ignored by libtorrent.
The response will not be published and made available anywhere, including the logs.
Therefore it's not likely there would be a way to *extract* data from a REST API
via a tracker request.

Database HTTP interfaces
------------------------

	NoSQL database such as MongoDB provide REST interfaces on HTTP ports. If the
	database is expected to only be available to internally, authentication may
	be disabled and the attacker can extract data

Since libtorrent doesn't make the response from a tracker request available to
anybody, especially not if it's not a valid BitTorrent tracker response, it's
not likely data can be extracted via such tracker URL. See previous section for
details.

Internal REST interfaces
------------------------

libtorrent can definitely hit a REST interface and may affect configuration
changes in other software that's installed on the local machine. This is
assuming that the software does not use any authentication other than checking
the source IP being the localhost.

As mentioned earlier, extracting data from a REST API via a tracker URL is not
likely to be possible.

It is established practice to include arbitrary URL query parameters in tracker
URLs, and clients amend them with the query parameters required by the tracker
protocol. This makes it difficult to sanitize the query string.

One way to mitigate hitting REST APIs on local host is to require that tracker
URLs, for local host specifically, use the request path ``/announce``. This is
the convention for bittorrent trackers.

Web seeds that resolve to a local network address are not allowed to have query
string parameters.

This SSRF mitigation was implemented for trackers in `#5303`__ and for web seeds in `#5319`__.

__ https://github.com/arvidn/libtorrent/pull/5303
__ https://github.com/arvidn/libtorrent/pull/5319

Web Seeds that resolve to a *global* address (i.e. not loopback, local network
or multicast address) are not allowed to redirect to a non-global IP. This
mitigation was implemented in `#5846`__, for libtorrent-2.0.3.

__ https://github.com/arvidn/libtorrent/pull/5846

Files
-----

	The attacker may be able to read files using ``<file://>`` URIs


libtorrent only supports ``http``, ``https`` and ``udp`` protocol schemes, and
will reject any other tracker URL. Specifically, libtorrent does not support
the ``file://`` URL scheme.

Additionally, `#5346`__ implements checks for tracker URLs that include query
string arguments that are supposed to be added by clients.

__ https://github.com/arvidn/libtorrent/pull/5346

F2: Compile Options Can Remove Assert Security Validation
=========================================================

The comments have been addressed in `#5308`__. The changes include:

__ https://github.com/arvidn/libtorrent/pull/5308

* use ``span<char>`` to simplify updates of pointer + length
* use ``span<char const>`` for (immutable) write buffers, to improve const
  correctness and avoid a ``const_cast``
* introduce additional sanity checks that no buffer lengths are < 0
* introduce additional check to ensure buffer lengths fit in unsigned 16 bit
  field (in the case where it's stored in one)
* generally reduce signed <-> unsigned casts

F3: Confidential and Security Relevant Information Stored in Logs
=================================================================

The secret keys for protocol encryption are not particularly sensitive, since
it's primarily an obfuscation feature. However, I have never had to use these
keys for debugging, so they don't have much value in the log anyway.

Addressed in `#5299`__.

__ https://github.com/arvidn/libtorrent/pull/5299

F4: Pseudo Random Number Generator Is Vulnerable to Prediction Attack
=====================================================================

These are the places ``random_bytes()``, ``random()`` and ``random_shuffle()``
are used in libtorrent. The "crypto" column indicates whether the random number
is sensitive and must be hard to predict, i.e. have high entropy.

.. list-table::
	:widths: auto
	:header-rows: 1

	* - crypto
	  - Use
	  - Description
	* - **Yes**
	  - PCP nonce
	  - generating a nonce for PCP (Port Control Protocol). The `PCP RFC section 11.2`__
	    references `RFC 4086 Randomness Requirements for Security`__ for
	    the nonce generation.

	    This was fixed.

	    __ https://tools.ietf.org/html/rfc6887#section-11.2
	    __ https://tools.ietf.org/html/rfc4086
	* - **Yes**
	  - DHT ed25519 keys
	  - used for kademlia mutable put feature. These keys are sensitive an
	    should use an appropriate entropy source. This is not done as part of
	    normal libtorrent operations, it's a utility function a client using the mutable
	    PUT-feature can call. This functionality is exposed in the
	    ``ed25519_create_seed()`` function.

	    This was fixed.
	* - **Maybe**
	  - DHT write-token
	  - The DHT maintains a secret 32 bit number which is updated every 5
	    minutes to a new random number. The secret from the last 5 minute period
	    is also remembered. In responses to ``get`` and ``get_peers`` messages a
	    *write token* is generated and included. The write token is the first 32
	    bits of a SHA-1 of the source IP address, the current secret and the
	    info_hash. ``put`` and ``announce_peer`` requests are ignored if the
	    write token is invalid given the current or the last secret. This is
	    like a SYN-cookie.

	    This was changed to use cryptographic random numbers.
	* - **Maybe**
	  - DHT transaction ID
	  - Each DHT request that is sent to a node includes a 16 bit transaction ID
	    that must be returned in the response. This is used to map responses to
	    the correct request (required when making multiple requests to the same
	    IP), but also to make it harder for a 3rd party to spoof the source IP
	    and fake a response. Presumably the fact that there are only 65536
	    different transaction IDs would be a problem before someone guesses the
	    random number. Additionally, a request is only valid for a few tens of
	    seconds, which further mitigates spoofed responses.

	    This has been left using pseudo random numbers.
	* - **Maybe**
	  - uTP sequence numbers
	  - When connecting a uTP socket, the initial sequence number is chosen at
	    random.

	    This has been left using pseudo random numbers.
	* - No
	  - protocol encryption (obfuscation)
	  - both key generation for DH handshake as well as random
	    padding ahead of handshake. The protocol encryption feature
	    is not intended to provide any authentication or confidentiality.
	* - No
	  - i2p session-id
	  - generation of the session ID, not key generation. All crypto,
	    including key generation is done by the i2p daemon implementing
	    the SAM bridge.
	* - No
	  - DHT node-id
	  - The node ID does not need to be hard to guess, just uniformly
	    distributed.
	* - No
	  - DHT node-id fingerprint
	  - Used to identify announces to fake info-hashes. More info here__.

	    __ https://blog.libtorrent.org/2014/11/dht-routing-table-maintenance/
	* - No
	  - DHT peer storage
	  - When returning peers from peer storage, in response to a DHT
	    ``get_peers`` request, we pick *n* of *m* random peers.
	* - No
	  - peer-id
	  - In bittorrent, each peer generates a random peer-id used in interactions
	    with other peers as well as HTTP(S) trackers. The peer-id is not secret
	    and does not need to be hard to guess. In fact, for each peer libtorrent
	    connects to, it generates a different peer-id. Additionally, each torrent
	    has a unique peer-id that's advertised to trackers. Trackers need a
	    consistent peer-id for its book keeping.
	* - No
	  - ip_voter
	  - The ip_voter maintains a list of possible external IP addresses, based
	    on how many peer interactions we've seen telling us that's our external
	    IP as observed by them. Knowing our external IP is not critical, it's
	    primarily used to generate our DHT node ID according to this__.

	    __ http://libtorrent.org/dht_sec.html

	    The ip_voter uses ``random()`` to probabilistically drop a record of a
	    possible external IP, if there are too many.
	* - No
	  - local service discovery
	  - In order to ignore our own service discovery messages sent on a
	    multi-cast group, we include a "cookie". If we see our own cookie, we
	    ignore the message. The cookie is generated by ``random()``.
	* - No
	  - piece picker
	  - The order pieces are picked in is rarest first. Pieces of the same
	    rarity are picked in random order, using ``random()``.
	* - No
	  - smart-ban
	  - If a piece fails the hash check, we may not know which peer sent the
	    corrupt data. The smart ban function will record the hashes of all blocks
	    of the failed piece. Once the piece passes, it can compare the passing
	    blocks against the failing one, identifying exactly which peer sent corrupt
	    data. This is a property of how bittorrent *checks* data at the piece
	    level, but downloads smaller parts (called "blocks") from potentially
	    different peers.

	    In earlier version of libtorrent, the block hash would use CRC32, and a
	    secret salt to prevent trivial exploiting by malicious peers. This is no
	    longer the case, smart-ban uses SHA-1 now, so there is no need for the salt.

	    It was removed in `#5295`__.

	    __ https://github.com/arvidn/libtorrent/pull/5295
	* - No
	  - peer-list pruning
	  - When the peer list has too many peers in it, random low quality peers
	    are pruned.
	* - No
	  - peer-list duplicate peer
	  - When receiving a connection from an IP we're already connected to, the
	    connection to keep and which one to disconnect is based on the local and
	    remote port numbers. If the ports are the same, one of the two connections
	    are closed randomly.
	* - No
	  - UPnP external port
	  - When the external port of a mapping conflicts with an existing map, the
	    port mapping is re-attempted with a random external port.
	* - No
	  - ut_metadata re-request timeout
	  - When a peer responds to a metadata request with "don't have", we delay
	    randomly between 20 - 70 seconds before re-requesting.
	* - No
	  - web seeds
	  - Web seeds are shuffled, to attempt connecting to them in random order
	* - No
	  - trackers
	  - Trackers within the same tier are shuffled, to try them in random order
	    (for load balancing)
	* - No
	  - resume data peers
	  - When saving resume data and we have more than 100 peers, once "high
	    quality peers" have been saved, pick low quality peers at random to save.
	* - No
	  - share mode seeds
	  - In share mode, where libtorrent attempts to maximize its upload to
	    download ratio, if we're connected to too many seeds, some random seeds
	    are disconnected.
	* - No
	  - share mode pick
	  - In share mode, when more than one piece has the lowest availability, one
	    of them is picked at random
	* - No
	  - http_connection endpoints
	  - After a successful hostname lookup, the endpoints are randomized to try
	    them in an arbitrary order, for load balancing.
	* - No
	  - super seeding piece picking
	  - In Super seeding mode, the rarest piece is selected for upload. If
	    there's a tie, a piece is chosen at random.
	* - No
	  - UDP listen socket
	  - When using a proxy, but not connecting peer via the proxy, the local UDP
	    socket, used for uTP and DHT traffic will bind to the listen socket of
	    the first configured listen interface. If there is no listen interface
	    configured, a random port is chosen.
	* - No
	  - bind outgoing uTP socket
	  - When bind-outgoing-sockets is enabled, uTP sockets are bound to the
	    listen interface matching the target IP. If there is no match, an
	    interface is picked at random to bind the outgoing socket to.
	* - No
	  - uTP send ID
	  - uTP connections are assigned send ID, to allow multiple connections to
	    the same IP. Similar to port number, but all uTP connections run over a
	    single UDP socket.

The following issues were addressed:

* the existing ``random_bytes()`` function was made to unconditionally produce
  pseudo random bytes.
* increase amount of entropy to seed the pseudo random number generator.
* a new function ``crypto_random_bytes()`` was added which unconditionally
  use a strong entropy source.
* If no specialized API is available for high-entropy random numbers is
  available (like ``libcrypto`` or CryptoAPI on windows) random numbers are
  pulled from ``/dev/urandom``.
* The PCP nonce was changed to use ``crypto_random_bytes()``
* The ed25519 key seed function was changed to use ``crypto_random_bytes()``

Addressed in `#5298`__.

__ https://github.com/arvidn/libtorrent/pull/5298

F5: Potential Null Pointer Dereference Issues
=============================================

This was fundamentally caused by the boost.pool default allocator using ``new
(std::nothrow)``, rather than plain (throwing) ``new``. The code using the pool
added to the confusion by checking for a ``nullptr`` return value, but further
up the call chain that check was not made. The fix was to remove the check for
``nullptr`` and replace the boost.pool allocator to throw ``std::bad_alloc`` on
memory exhaustion.

Addressed in `#5293`__.

__ https://github.com/arvidn/libtorrent/pull/5293

F6: Integer Overflow
====================

This was a bug in the fuzzer itself, not in the production code (as far as I
could find). The parse_int fuzzer used an uninitialized variable.

Addressed in `#5292`__.

__ https://github.com/arvidn/libtorrent/pull/5292

F7: Magnet URIs Allow IDNA Domain Names
=======================================

My understanding of this attack is that a tracker hostname could be crafted to
look like a well known host, but in fact be a different host, by using
look-alike unicode characters in the hostname.

For example, the well-known tracker ``http://bt1.archive.org:6969/announce``
could be spoofed by using ``bt1.archivｅ.org`` (the ``e`` at the end is really
`U+ff45`__).

__ https://unicode-table.com/en/FF45/

The issue of trusting trackers goes beyond tracker host names in magnet links.
Normal .torrent files also contain tracker URLs, and they could also use
misleading tracker host names. However, this highlights a more fundamental issue
that libtorrent does not provide an API for clients to vet trackers before
announcing to them. libtorrent provides an IP filter that will block announcing
to trackers, but not the URLs or host names directly.

Having an ability to vet trackers before using them would also mitigate the
`F1: Server-Side Request Forgery (SSRF)`_.

This issue also goes beyond trackers. Web seeds are also URLs embedded in
.torrent files or magnet links which libtorrent will make requests to.

These are the changes I'm making to mitigate this issue:

* enable ``validate_https_trackers`` by default. `#5314`__. The name of this
  setting is misleading. It does not only affect trackers, but also web seeds.
* Support loading the system certificate store on windows, to authenticate
  trackers with, `#5313`__.
* add an option to allow IDNA domain names, and disable it by default. This
  applies to both trackers and web seeds. `#5316`__.

__ https://github.com/arvidn/libtorrent/pull/5314
__ https://github.com/arvidn/libtorrent/pull/5313
__ https://github.com/arvidn/libtorrent/pull/5316


I1: Additional Documentation and Automation
===========================================

Addressed in:

* `#5337`__.

__ https://github.com/arvidn/libtorrent/pull/5337

I2: Automated Fuzzer Generation
===============================

No effort has been put into generating fuzzers with FuzzGen_, but it's an
intriguing project I hope to have time to put some effort towards in the future.

.. _FuzzGen: https://github.com/HexHive/FuzzGen

I3: Type Confusion and Integer Overflow Improvements
====================================================

Addressed in:

* `#5308`__.

__ https://github.com/arvidn/libtorrent/pull/5308
