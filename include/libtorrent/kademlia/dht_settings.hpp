/*

Copyright (c) 2006, Arvid Norberg
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

#ifndef TORRENT_DHT_SETTINGS_HPP_INCLUDED
#define TORRENT_DHT_SETTINGS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/entry.hpp"

namespace libtorrent {
namespace dht {

	// structure used to hold configuration options for the DHT
	//
	// The ``dht_settings`` struct used to contain a ``service_port`` member to
	// control which port the DHT would listen on and send messages from. This
	// field is deprecated and ignored. libtorrent always tries to open the UDP
	// socket on the same port as the TCP socket.
	struct TORRENT_EXPORT dht_settings
	{
		// the maximum number of peers to send in a reply to ``get_peers``
		int max_peers_reply = 100;

		// the number of concurrent search request the node will send when
		// announcing and refreshing the routing table. This parameter is called
		// alpha in the kademlia paper
		int search_branching = 5;

		// the maximum number of failed tries to contact a node before it is
		// removed from the routing table. If there are known working nodes that
		// are ready to replace a failing node, it will be replaced immediately,
		// this limit is only used to clear out nodes that don't have any node
		// that can replace them.
		int max_fail_count = 20;

		// the total number of torrents to track from the DHT. This is simply an
		// upper limit to make sure malicious DHT nodes cannot make us allocate
		// an unbounded amount of memory.
		int max_torrents = 2000;

		// max number of items the DHT will store
		int max_dht_items = 700;

		// the max number of peers to store per torrent (for the DHT)
		int max_peers = 500;

		// the max number of torrents to return in a torrent search query to the
		// DHT
		int max_torrent_search_reply = 20;

		// determines if the routing table entries should restrict entries to one
		// per IP. This defaults to true, which helps mitigate some attacks on
		// the DHT. It prevents adding multiple nodes with IPs with a very close
		// CIDR distance.
		//
		// when set, nodes whose IP address that's in the same /24 (or /64 for
		// IPv6) range in the same routing table bucket. This is an attempt to
		// mitigate node ID spoofing attacks also restrict any IP to only have a
		// single entry in the whole routing table
		bool restrict_routing_ips = true;

		// determines if DHT searches should prevent adding nodes with IPs with
		// very close CIDR distance. This also defaults to true and helps
		// mitigate certain attacks on the DHT.
		bool restrict_search_ips = true;

		// makes the first buckets in the DHT routing table fit 128, 64, 32 and
		// 16 nodes respectively, as opposed to the standard size of 8. All other
		// buckets have size 8 still.
		bool extended_routing_table = true;

		// slightly changes the lookup behavior in terms of how many outstanding
		// requests we keep. Instead of having branch factor be a hard limit, we
		// always keep *branch factor* outstanding requests to the closest nodes.
		// i.e. every time we get results back with closer nodes, we query them
		// right away. It lowers the lookup times at the cost of more outstanding
		// queries.
		bool aggressive_lookups = true;

		// when set, perform lookups in a way that is slightly more expensive,
		// but which minimizes the amount of information leaked about you.
		bool privacy_lookups = false;

		// when set, node's whose IDs that are not correctly generated based on
		// its external IP are ignored. When a query arrives from such node, an
		// error message is returned with a message saying "invalid node ID".
		bool enforce_node_id = false;

		// ignore DHT messages from parts of the internet we wouldn't expect to
		// see any traffic from
		bool ignore_dark_internet = true;

		// the number of seconds a DHT node is banned if it exceeds the rate
		// limit. The rate limit is averaged over 10 seconds to allow for bursts
		// above the limit.
		int block_timeout = 5 * 60;

		// the max number of packets per second a DHT node is allowed to send
		// without getting banned.
		int block_ratelimit = 5;

		// when set, the other nodes won't keep this node in their routing
		// tables, it's meant for low-power and/or ephemeral devices that
		// cannot support the DHT, it is also useful for mobile devices which
		// are sensitive to network traffic and battery life.
		// this node no longer responds to 'query' messages, and will place a
		// 'ro' key (value = 1) in the top-level message dictionary of outgoing
		// query messages.
		bool read_only = false;

		// the number of seconds a immutable/mutable item will be expired.
		// default is 0, means never expires.
		int item_lifetime = 0;

		// the number of bytes per second (on average) the DHT is allowed to send.
		// If the incoming requests causes to many bytes to be sent in responses,
		// incoming requests will be dropped until the quota has been replenished.
		int upload_rate_limit = 8000;

		// the info-hashes sample recomputation interval (in seconds).
		// The node will precompute a subset of the tracked info-hashes and return
		// that instead of calculating it upon each request. The permissible range
		// is between 0 and 21600 seconds (inclusive).
		int sample_infohashes_interval = 21600;

		// the maximum number of elements in the sampled subset of info-hashes.
		// If this number is too big, expect the DHT storage implementations
		// to clamp it in order to allow UDP packets go through
		int max_infohashes_sample_count = 20;

#if TORRENT_ABI_VERSION == 1
		// the listen port for the dht. This is a UDP port. zero means use the
		// same as the tcp interface
		int service_port = 0;
#endif
	};

	// internal
	struct settings : dht_settings
	{
		// when this is true, nodes whose IDs are derived from their source IP
		// according to BEP 42 (https://www.bittorrent.org/beps/bep_0042.html) are
		// preferred in the routing table.
		bool prefer_verified_node_ids = true;
	};


TORRENT_EXTRA_EXPORT dht_settings read_dht_settings(bdecode_node const& e);
TORRENT_EXTRA_EXPORT entry save_dht_settings(dht_settings const& settings);

}
}

#endif
