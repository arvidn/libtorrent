/*

Copyright (c) 2006-2018, Arvid Norberg
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
#include "libtorrent/aux_/export.hpp"
#include <vector>

#if TORRENT_ABI_VERSION == 1
// for dht_lookup and dht_routing_bucket
#include "libtorrent/alert_types.hpp"
#endif

#if TORRENT_ABI_VERSION == 1
namespace libtorrent {

	// holds counters and gauges for the uTP sockets
	// deprecated in 1.1 in favor of session_stats counters, which is a more
	// flexible, extensible and performant mechanism for stats.
	struct TORRENT_DEPRECATED_EXPORT utp_status
	{
		// gauges. These are snapshots of the number of
		// uTP sockets in each respective state
		int num_idle;
		int num_syn_sent;
		int num_connected;
		int num_fin_sent;
		int num_close_wait;

		// These are monotonically increasing
		// and cumulative counters for their respective event.
		std::uint64_t packet_loss;
		std::uint64_t timeout;
		std::uint64_t packets_in;
		std::uint64_t packets_out;
		std::uint64_t fast_retransmit;
		std::uint64_t packet_resend;
		std::uint64_t samples_above_target;
		std::uint64_t samples_below_target;
		std::uint64_t payload_pkts_in;
		std::uint64_t payload_pkts_out;
		std::uint64_t invalid_pkts_in;
		std::uint64_t redundant_pkts_in;
	};

	// contains session wide state and counters
	// deprecated in 1.1 in favor of session_stats counters, which is a more
	// flexible, extensible and performant mechanism for stats.
	struct TORRENT_DEPRECATED_EXPORT session_status
	{
		// false as long as no incoming connections have been
		// established on the listening socket. Every time you change the listen port, this will
		// be reset to false.
		bool has_incoming_connections;

		// the total download and upload rates accumulated
		// from all torrents. This includes bittorrent protocol, DHT and an estimated TCP/IP
		// protocol overhead.
		// deprecated, use session_stats_metrics "net.recv_bytes" + "net.recv_ip_overhead_bytes"
		// they does include payload + protocol + ip overhead bytes
		int upload_rate;
		int download_rate;

		// the total number of bytes downloaded and
		// uploaded to and from all torrents. This also includes all the protocol overhead.
		// deprecated, use session_stats_metrics "net.recv_bytes" + "net.recv_ip_overhead_bytes"
		// they does include payload + protocol + ip overhead bytes
		std::int64_t total_download;
		std::int64_t total_upload;

		// the rate of the payload
		// down- and upload only.
		// deprecated, use session_stats_metrics "net.recv_payload_bytes"
		int payload_upload_rate;
		// deprecated, use session_stats_metrics "net.sent_payload_bytes"
		int payload_download_rate;

		// the total transfers of payload
		// only. The payload does not include the bittorrent protocol overhead, but only parts of the
		// actual files to be downloaded.
		// ``total_payload_download`` is deprecated, use session_stats_metrics
		// "net.recv_payload_bytes" ``total_payload_upload`` is deprecated, use
		// session_stats_metrics "net.sent_payload_bytes"
		std::int64_t total_payload_download;
		std::int64_t total_payload_upload;

		// the estimated TCP/IP overhead in each direction.
		int ip_overhead_upload_rate;
		int ip_overhead_download_rate;
		std::int64_t total_ip_overhead_download;
		std::int64_t total_ip_overhead_upload;

		// the upload and download rate used by DHT traffic. Also the total number
		// of bytes sent and received to and from the DHT.
		int dht_upload_rate;
		int dht_download_rate;
		std::int64_t total_dht_download;
		std::int64_t total_dht_upload;

		// the upload and download rate used by tracker traffic. Also the total number
		// of bytes sent and received to and from trackers.
		int tracker_upload_rate;
		int tracker_download_rate;
		std::int64_t total_tracker_download;
		std::int64_t total_tracker_upload;

		// the number of bytes that has been received more than once.
		// This can happen if a request from a peer times out and is requested from a different
		// peer, and then received again from the first one. To make this lower, increase the
		// ``request_timeout`` and the ``piece_timeout`` in the session settings.
		std::int64_t total_redundant_bytes;

		// the number of bytes that was downloaded which later failed
		// the hash-check.
		std::int64_t total_failed_bytes;

		// the total number of peer connections this session has. This includes
		// incoming connections that still hasn't sent their handshake or outgoing connections
		// that still hasn't completed the TCP connection. This number may be slightly higher
		// than the sum of all peers of all torrents because the incoming connections may not
		// be assigned a torrent yet.
		int num_peers;

		int num_dead_peers;

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
		// unchoked disconnects or becomes not interested.
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
		// deprecated, use session_stats_metrics "dht.dht_nodes" and "dht.dht_nodes_cache"
		int dht_nodes;
		int dht_node_cache;

		// the number of torrents tracked by the DHT at the moment.
		int dht_torrents;

		// an estimation of the total number of nodes in the DHT
		// network.
		std::int64_t dht_global_nodes;

		// a vector of the currently running DHT lookups.
		std::vector<dht_lookup> active_requests;

		// contains information about every bucket in the DHT routing
		// table.
		std::vector<dht_routing_bucket> dht_routing_table;

		// the number of nodes allocated dynamically for a
		// particular DHT lookup. This represents roughly the amount of memory used
		// by the DHT.
		int dht_total_allocations;

#include "libtorrent/aux_/disable_warnings_push.hpp"

		// statistics on the uTP sockets.
		utp_status utp_stats;

#include "libtorrent/aux_/disable_warnings_pop.hpp"

		// the number of known peers across all torrents. These are not necessarily
		// connected peers, just peers we know of.
		int peerlist_size;

		// the number of torrents in the
		// session and the number of them that are currently paused, respectively.
		int num_torrents;
		int num_paused_torrents;
	};
}
#endif // TORRENT_ABI_VERSION

#endif // TORRENT_SESSION_STATUS_HPP_INCLUDED

