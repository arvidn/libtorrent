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

#include "libtorrent/kademlia/node_entry.hpp"
#include "libtorrent/aux_/time.hpp" // for aux::time_now()

namespace libtorrent { namespace dht {

	node_entry::node_entry(node_id const& id_, udp::endpoint const& ep
		, int roundtriptime
		, bool pinged)
		: last_queried(pinged ? aux::time_now() : min_time())
		, id(id_)
		, endpoint(ep)
		, rtt(roundtriptime & 0xffff)
		, timeout_count(pinged ? 0 : 0xff)
		, verified(verify_id(id_, ep.address()))
	{
	}

	node_entry::node_entry(udp::endpoint const& ep)
		: endpoint(ep)
	{}

	void node_entry::update_rtt(int const new_rtt)
	{
		TORRENT_ASSERT(new_rtt <= 0xffff);
		TORRENT_ASSERT(new_rtt >= 0);
		if (new_rtt == 0xffff) return;
		if (rtt == 0xffff) rtt = std::uint16_t(new_rtt);
		else rtt = std::uint16_t(int(rtt) * 2 / 3 + new_rtt / 3);
	}

}}
