/*

Copyright (c) 2016, Arvid Norberg, Alden Torres
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

#ifndef LIBTORRENT_DHT_STATE_HPP
#define LIBTORRENT_DHT_STATE_HPP

#include <libtorrent/config.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/entry.hpp>

#include <libtorrent/kademlia/node_id.hpp>

#include <vector>
#include <utility>

namespace libtorrent {

	struct bdecode_node;
}

namespace libtorrent { namespace dht {

	using node_ids_t = std::vector<std::pair<address, node_id>>;
	// This structure helps to store and load the state
	// of the ``dht_tracker``.
	// At this moment the library is only a dual stack
	// implementation of the DHT. See `BEP 32`_
	//
	// .. _`BEP 32`: https://www.bittorrent.org/beps/bep_0032.html
	struct TORRENT_EXPORT dht_state
	{
		node_ids_t nids;

		// the bootstrap nodes saved from the buckets node
		std::vector<udp::endpoint> nodes;
		// the bootstrap nodes saved from the IPv6 buckets node
		std::vector<udp::endpoint> nodes6;

		void clear();
	};

	TORRENT_EXTRA_EXPORT node_ids_t extract_node_ids(bdecode_node const& e, string_view key);
	TORRENT_EXTRA_EXPORT dht_state read_dht_state(bdecode_node const& e);
	TORRENT_EXTRA_EXPORT entry save_dht_state(dht_state const& state);
}}

#endif // LIBTORRENT_DHT_STATE_HPP
