/*

Copyright (c) 2006-2016, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_SESSION_STATUS_HPP_INCLUDED
#define TORRENT_SESSION_STATUS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/size_type.hpp"
#include <vector>

namespace libtorrent
{

	// holds statistics about a current dht_lookup operation.
	// a DHT lookup is the travesal of nodes, looking up a
	// set of target nodes in the DHT for retrieving and possibly
	// storing information in the DHT
	struct TORRENT_EXPORT dht_lookup
	{
		// string literal indicating which kind of lookup this is
		char const* type;

		// the number of outstanding request to individual nodes
		// this lookup has right now
		int outstanding_requests;

		// the total number of requests that have timed out so far
		// for this lookup
		int timeouts;

		// the total number of responses we have received for this
		// lookup so far for this lookup
		int responses;

		// the branch factor for this lookup. This is the number of
		// nodes we keep outstanding requests to in parallel by default.
		// when nodes time out we may increase this.
		int branch_factor;

		// the number of nodes left that could be queries for this
		// lookup. Many of these are likely to be part of the trail
		// while performing the lookup and would never end up actually
		// being queried.
		int nodes_left;

		// the number of seconds ago the
		// last message was sent that's still
		// outstanding
		int last_sent;

		// the number of outstanding requests
		// that have exceeded the short timeout
		// and are considered timed out in the
		// sense that they increased the branch
		// factor
		int first_timeout;
	};

	// holds dht routing table stats
	struct TORRENT_EXPORT dht_routing_bucket
	{
		// the total number of nodes and replacement nodes
		// in the routing table
		int num_nodes;
		int num_replacements;

#ifndef TORRENT_NO_DEPRECATE
		// number of seconds since last activity
		int last_active;
#endif
	};

	// holds counters and gauges for the uTP sockets
	struct TORRENT_EXPORT utp_status
	{
		// gauges. These are snapshots of the number of
		// uTP sockets in each respective state
		int num_idle;
		int num_syn_sent;
		int num_connected;
		int num_fin_sent;
		int num_close_wait;

		// counters. These are monotonically increasing
		// and cumulative counters for their respective event.
		boost::uint64_t packet_loss;
		boost::uint64_t timeout;
		boost::uint64_t packets_in;
		boost::uint64_t packets_out;
		boost::uint64_t fast_retransmit;
		boost::uint64_t packet_resend;
		boost::uint64_t samples_above_target;
		boost::uint64_t samples_below_target;
		boost::uint64_t payload_pkts_in;
		boost::uint64_t payload_pkts_out;
		boost::uint64_t invalid_pkts_in;
		boost::uint64_t redundant_pkts_in;
	};

	// contains session wide state and counters
	struct TORRENT_EXPORT session_status
	{
		// false as long as no incoming connections have been
		// established on the listening socket. Every time you change the listen port, this will
		// be reset to false.
		bool has_incoming_connections;

		// the total download and upload rates accumulated
		// from all torrents. This includes bittorrent protocol, DHT and an estimated TCP/IP
		// protocol overhead.
		int upload_rate;
		int download_rate;

		// the total number of bytes downloaded and
		// uploaded to and from all torrents. This also includes all the protocol overhead.
		size_type total_download;
		size_type total_upload;

		// the rate of the payload
		// down- and upload only.
		int payload_upload_rate;
		int payload_download_rate;

		// the total transfers of payload
		// only. The payload does not include the bittorrent protocol overhead, but only parts of the
		// actual files to be downloaded.
		size_type total_payload_download;
		size_type total_payload_upload;

		// the estimated TCP/IP overhead in each direction.
		int ip_overhead_upload_rate;
		int ip_overhead_download_rate;
		size_type total_ip_overhead_download;
		size_type total_ip_overhead_upload;

		// the upload and download rate used by DHT traffic. Also the total number
		// of bytes sent and received to and from the DHT.
		int dht_upload_rate;
		int dht_download_rate;
		size_type total_dht_download;
		size_type total_dht_upload;

		// the upload and download rate used by tracker traffic. Also the total number
		// of bytes sent and received to and from trackers.
		int tracker_upload_rate;
		int tracker_download_rate;
		size_type total_tracker_download;
		size_type total_tracker_upload;

		// the number of bytes that has been received more than once.
		// This can happen if a request from a peer times out and is requested from a different
		// peer, and then received again from the first one. To make this lower, increase the
		// ``request_timeout`` and the ``piece_timeout`` in the session settings.
		size_type total_redundant_bytes;

		// the number of bytes that was downloaded which later failed
		// the hash-check.
		size_type total_failed_bytes;

		// the total number of peer connections this session has. This includes
		// incoming connections that still hasn't sent their handshake or outgoing connections
		// that still hasn't completed the TCP connection. This number may be slightly higher
		// than the sum of all peers of all torrents because the incoming connections may not
		// be assigned a torrent yet.
		int num_peers;

		// the current number of unchoked peers.
		int num_unchoked;

		// the current allowed number of unchoked peers.
		int allowed_upload_slots;

		// the number of peers that are
		// waiting for more bandwidth quota from the torrent rate limiter.
		int up_bandwidth_queue;
		int down_bandwidth_queue;

		// count the number of
		// bytes the connections are waiting for to be able to send and receive.
		int up_bandwidth_bytes_queue;
		int down_bandwidth_bytes_queue;

		// tells the number of
		// seconds until the next optimistic unchoke change and the start of the next
		// unchoke interval. These numbers may be reset prematurely if a peer that is
		// unchoked disconnects or becomes notinterested.
		int optimistic_unchoke_counter;
		int unchoke_counter;

		// the number of peers currently
		// waiting on a disk write or disk read to complete before it receives or sends
		// any more data on the socket. It'a a metric of how disk bound you are.
		int disk_write_queue;
		int disk_read_queue;

		// only available when
		// built with DHT support. They are all set to 0 if the DHT isn't running. When
		// the DHT is running, ``dht_nodes`` is set to the number of nodes in the routing
		// table. This number only includes *active* nodes, not cache nodes. The
		// ``dht_node_cache`` is set to the number of nodes in the node cache. These nodes
		// are used to replace the regular nodes in the routing table in case any of them
		// becomes unresponsive.
		int dht_nodes;
		int dht_node_cache;

		// the number of torrents tracked by the DHT at the moment.
		int dht_torrents;

		// an estimation of the total number of nodes in the DHT
		// network.
		size_type dht_global_nodes;

		// a vector of the currently running DHT lookups.
		std::vector<dht_lookup> active_requests;

		// contains information about every bucket in the DHT routing
		// table.
		std::vector<dht_routing_bucket> dht_routing_table;

		// the number of nodes allocated dynamically for a
		// particular DHT lookup. This represents roughly the amount of memory used
		// by the DHT.
		int dht_total_allocations;

		// statistics on the uTP sockets.
		utp_status utp_stats;

		// the number of known peers across all torrents. These are not necessarily
		// connected peers, just peers we know of.
		int peerlist_size;
	};

}

#endif // TORRENT_SESSION_STATUS_HPP_INCLUDED

