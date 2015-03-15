/*

Copyright (c) 2006-2014, Arvid Norberg
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
#include "libtorrent/aux_/time.hpp" // for aux::time_now()

namespace libtorrent { namespace dht
{

struct node_entry
{
	node_entry(node_id const& id_, udp::endpoint ep, int roundtriptime = 0xffff
		, bool pinged = false)
		: last_queried(pinged ? aux::time_now() : min_time())
		, id(id_)
		, a(ep.address().to_v4().to_bytes())
		, p(ep.port())
		, rtt(roundtriptime & 0xffff)
		, timeout_count(pinged ? 0 : 0xff)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		first_seen = aux::time_now();
#endif
	}

	node_entry(udp::endpoint ep)
		: last_queried(min_time())
		, id(0)
		, a(ep.address().to_v4().to_bytes())
		, p(ep.port())
		, rtt(0xffff)
		, timeout_count(0xff)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		first_seen = aux::time_now();
#endif
	}

	node_entry()
		: last_queried(min_time())
		, id(0)
		, p(0)
		, rtt(0xffff)
		, timeout_count(0xff)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		first_seen = aux::time_now();
#endif
	}
	
	bool pinged() const { return timeout_count != 0xff; }
	void set_pinged() { if (timeout_count == 0xff) timeout_count = 0; }
	void timed_out() { if (pinged() && timeout_count < 0xfe) ++timeout_count; }
	int fail_count() const { return pinged() ? timeout_count : 0; }
	void reset_fail_count() { if (pinged()) timeout_count = 0; }
	udp::endpoint ep() const { return udp::endpoint(address_v4(a), p); }
	bool confirmed() const { return timeout_count == 0; }
	void update_rtt(int new_rtt)
	{
		TORRENT_ASSERT(new_rtt <= 0xffff);
		TORRENT_ASSERT(new_rtt >= 0);
		if (new_rtt == 0xffff) return;
		if (rtt == 0xffff) rtt = new_rtt;
		else rtt = int(rtt) * 2 / 3 + int(new_rtt) / 3;
	}
	address addr() const { return address_v4(a); }
	int port() const { return p; }

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	time_point first_seen;
#endif

	// the time we last received a response for a request to this peer
	time_point last_queried;

	node_id id;

	address_v4::bytes_type a;
	boost::uint16_t p;

	// the average RTT of this node
	boost::uint16_t rtt;

	// the number of times this node has failed to
	// respond in a row
	boost::uint8_t timeout_count;
};

} } // namespace libtorrent::dht

#endif

