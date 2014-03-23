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

#ifndef KADEMLIA_NODE_ENTRY_HPP
#define KADEMLIA_NODE_ENTRY_HPP

#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"

namespace libtorrent { namespace dht
{

struct node_entry
{
	node_entry(node_id const& id_, udp::endpoint ep, bool pinged = false)
		: addr(ep.address())
		, port(ep.port())
		, timeout_count(pinged ? 0 : 0xffff)
		, id(id_)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		first_seen = time_now();
#endif
	}

	node_entry(udp::endpoint ep)
		: addr(ep.address())
		, port(ep.port())
		, timeout_count(0xffff)
		, id(0)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		first_seen = time_now();
#endif
	}

	node_entry()
		: timeout_count(0xffff)
		, id(0)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		first_seen = time_now();
#endif
	}
	
	bool pinged() const { return timeout_count != 0xffff; }
	void set_pinged() { if (timeout_count == 0xffff) timeout_count = 0; }
	void timed_out() { if (pinged()) ++timeout_count; }
	int fail_count() const { return pinged() ? timeout_count : 0; }
	void reset_fail_count() { if (pinged()) timeout_count = 0; }
	udp::endpoint ep() const { return udp::endpoint(addr, port); }
	bool confirmed() const { return timeout_count == 0; }

	// TODO: replace with a union of address_v4 and address_v6
	address addr;
	boost::uint16_t port;
	// the number of times this node has failed to
	// respond in a row
	boost::uint16_t timeout_count;
	node_id id;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	ptime first_seen;
#endif
};

} } // namespace libtorrent::dht

#endif

