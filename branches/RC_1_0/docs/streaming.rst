Streaming implementation
========================

This documents describes the algorithm libtorrent uses to satisfy time critical
piece requests, i.e. streaming.

piece picking
-------------

The standard bittorrent piece picker is peer-centric. A peer unchokes us or we
complte a block from a peer and we want to make another request to that peer.
The piece picker answers the question: which block should we request from this
peer.

When streaming, we have a number of *time critical* pieces, the ones the video
or audio player will need next to keep up with the stream. To keep the deadlines
of these pieces, we need a mechanism to answer the question: I want to request
blocks from this piece, which peer is the most likely to be able to deliver it
to me the soonest.

This question is answered by ``torrent::request_time_critical_pieces()`` in
libtorrent.

At a high level, this algorithm keeps a list of peers, sorted by the estimated
download queue time. That is, the estimated time for a new request to this
peer to be received. The bottom 10th percentile of the peers (the 10% slowest
peers) are ignored and not included in the peer list. Peers that have choked
us, are not interesting, is on parole, disconnecting, have too many outstanding
block requests or is snubbed are also excluded from the peer list.

The time critical pieces are also kept sorted by their deadline. Pieces with
an earlier deadline first. This list of pieces is iterated, starting at the
top, and blocks are requested from a piece until we cannot make any more
requests from it. We then move on to the next piece and request blocks from it
until we cannot make any more. The peer each request is sent to is the one
with the lowest `download queue time`_. Each time a request is made, this
estimate is updated and the peer is resorted in this list.

Any peer that doesn't have the piece is ignored until we move on to the next
piece.

If the top peer's download queue time is more than 2 seconds, the loop is
terminated. This is to not over-request. ``request_time_critical_pieces()``
is called once per second, so this will keep the queue full with margin.

download queue time
-------------------

Each peer maintains the number of bytes that have been requested from it but
not yet been received. This is referred to as ``outstanding_bytes``. This number
is incremented by the size of each outgoing request and decremented for each
*payload* byte received.

This counter is divided by an estimated download rate from the peer to form
the estimated *download queue time*. That is, the estimated time it will take
any new request to this peer to begin being received.

The estimated download rate of a peer is not trivial. There may not be any
outstanding requests to the peer, in which case the payload download rate
will be zero. That would not be a reasonable estimate of the rate we would see
once we make a request.

If we have not received any payload from a peer in the last 30 seconds, we
must use an alternative estimate of the download rate. If we have received
payload from this peer previously, we can use the peak download rate.

If we have received less than 2 blocks (32 kiB) and we have been unchoked for
less than 5 seconds ago, use the average download rate of all peers (that have
outstanding requests).

timeouts
--------

An observation that is useful to keep in mind when streaming is that your
download capacity is likely to be saturated by your peers. In this case, if the
swarm is well seeded, most peers will send data to you at close to the same
rate. This makes it important to support streaming from many slow peers. For
instance, this means you can't make assumptions about the download time of a
block being less than some absolute time. You may be downloading at well above
the bitrate of the video, but each individual peer only transfers at 5 kiB/s.

In this state, your download rate is a zero-sum-game. Any block you request
that is not urgent, will take away from the bandwidth you get for peers that
are urgent. Make sure to limit requests to useful blocks only.

Some requests will stall. It appears to be very hard to have enough accuracy in
the prediction of download queue time such that all requests come back within a
reasonable amount of time.

To support adaptive timeuts, each torrent maintains a running average of how
long it takes to complete a piece. There is also a running average of the
deviation from the mean download time.

This download time is used as the benchmark to determine when blocks have
timed out, and should be re-requested from another peer.

If any time-critical piece has taken more than the average piece download
time + a half average deviation form that, the piece is considered to have
timed out. This means we are allowed to double-request blocks. Subsequent
passes over this piece will make sure that any blocks we don't already have
are requested one more time.

In fact, this scales to multiple time-outs. The time since a download was
started is divided by average download time + average deviation time / 2.
The resulting integer is the number if *times* the piece has timed out.

Each time a piece times out, another *busy request* is allowed to try to make
it complete sooner. A busy request is where a block is requested from a peer
even though it has already been requested from another peer.

This has the effect of getting more and more aggressive in requesting blocks
the longer it takes to complete the piece. If this mechanism is too aggressive,
a significant amount of bandwidht may be lost in redundant download (keep in
mind the zero-sum game).

It never makes sense to request a block twice from the same peer. There is logic
in place to prevent this.

optimizations
-------------

One optimization is to buffer all piece requests while looping over the time-
critical pieces and not send them until one round is complete. This increases
the chances that the request messages are coalesced into the same packet.
This in turn lowers the number of system calls and network overhead.

