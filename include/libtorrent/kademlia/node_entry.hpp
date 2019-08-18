/*

Copyright (c) 2006, 2008-2009, 2013-2016, 2019, Arvid Norberg
Copyright (c) 2015, Steven Siloti
Copyright (c) 2016, Alden Torres
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

#ifndef KADEMLIA_NODE_ENTRY_HPP
#define KADEMLIA_NODE_ENTRY_HPP

#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/union_endpoint.hpp"
#include "libtorrent/time.hpp" // for time_point
#include "libtorrent/aux_/time.hpp" // for time_now

namespace libtorrent {
namespace dht {

struct TORRENT_EXTRA_EXPORT node_entry
{
	node_entry(node_id const& id_, udp::endpoint const& ep, int roundtriptime = 0xffff
		, bool pinged = false);
	explicit node_entry(udp::endpoint const& ep);
	node_entry() = default;
	void update_rtt(int new_rtt);

	bool pinged() const { return timeout_count != 0xff; }
	void set_pinged() { if (timeout_count == 0xff) timeout_count = 0; }
	void timed_out() { if (pinged() && timeout_count < 0xfe) ++timeout_count; }
	int fail_count() const { return pinged() ? timeout_count : 0; }
	void reset_fail_count() { if (pinged()) timeout_count = 0; }
	udp::endpoint ep() const { return endpoint; }
	bool confirmed() const { return timeout_count == 0; }
	address addr() const { return endpoint.address(); }
	int port() const { return endpoint.port; }

	// compares which node_entry is "better". Smaller is better
	bool operator<(node_entry const& rhs) const
	{
		return std::make_tuple(!verified, rtt) < std::make_tuple(!rhs.verified, rhs.rtt);
	}

#ifndef TORRENT_DISABLE_LOGGING
	time_point first_seen = aux::time_now();
#endif

	// the time we last received a response for a request to this peer
	time_point last_queried = min_time();

	node_id id{nullptr};

	union_endpoint endpoint;

	// the average RTT of this node
	std::uint16_t rtt = 0xffff;

	// the number of times this node has failed to
	// respond in a row
	// 0xff is a special value to indicate we have not pinged this node yet
	std::uint8_t timeout_count = 0xff;

	bool verified = false;
};

} // namespace dht
} // namespace libtorrent

#endif
