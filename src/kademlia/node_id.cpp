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

#include "libtorrent/pch.hpp"

#include <algorithm>
#include <iomanip>
#include <ctime>

#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/broadcast_socket.hpp"

namespace libtorrent { namespace dht
{

void hash_address(address const& ip, sha1_hash& h)
{
#if TORRENT_USE_IPV6
	if (ip.is_v6())
	{
		address_v6::bytes_type b = ip.to_v6().to_bytes();
		h = hasher((char*)&b[0], b.size()).final();
	}
	else
#endif
	{
		address_v4::bytes_type b = ip.to_v4().to_bytes();
		h = hasher((char*)&b[0], b.size()).final();
	}
}

// verifies whether a node-id matches the IP it's used from
// returns true if the node-id is OK coming from this source
// and false otherwise.
bool verify_id(node_id const& nid, address const& source_ip)
{
	// no need to verify local IPs, they would be incorrect anyway
	if (is_local(source_ip)) return true;

	node_id h;
	hash_address(source_ip, h);
	return memcmp(&nid[0], &h[0], 4) == 0;
}
	
// returns the distance between the two nodes
// using the kademlia XOR-metric
node_id distance(node_id const& n1, node_id const& n2)
{
	node_id ret;
	node_id::iterator k = ret.begin();
	for (node_id::const_iterator i = n1.begin(), j = n2.begin()
		, end(n1.end()); i != end; ++i, ++j, ++k)
	{
		*k = *i ^ *j;
	}
	return ret;
}

// returns true if: distance(n1, ref) < distance(n2, ref)
bool compare_ref(node_id const& n1, node_id const& n2, node_id const& ref)
{
	for (node_id::const_iterator i = n1.begin(), j = n2.begin()
		, k = ref.begin(), end(n1.end()); i != end; ++i, ++j, ++k)
	{
		boost::uint8_t lhs = (*i ^ *k);
		boost::uint8_t rhs = (*j ^ *k);
		if (lhs < rhs) return true;
		if (lhs > rhs) return false;
	}
	return false;
}

// returns n in: 2^n <= distance(n1, n2) < 2^(n+1)
// useful for finding out which bucket a node belongs to
int distance_exp(node_id const& n1, node_id const& n2)
{
	int byte = node_id::size - 1;
	for (node_id::const_iterator i = n1.begin(), j = n2.begin()
		, end(n1.end()); i != end; ++i, ++j, --byte)
	{
		TORRENT_ASSERT(byte >= 0);
		boost::uint8_t t = *i ^ *j;
		if (t == 0) continue;
		// we have found the first non-zero byte
		// return the bit-number of the first bit
		// that differs
		int bit = byte * 8;
		for (int b = 7; b >= 0; --b)
			if (t >= (1 << b)) return bit + b;
		return bit;
	}

	return 0;
}

struct static_ { static_() { std::srand(std::time(0)); } } static__;
	
node_id generate_id()
{
	char random[20];
#ifdef _MSC_VER
	std::generate(random, random + 20, &rand);
#else
	std::generate(random, random + 20, &std::rand);
#endif

	hasher h;
	h.update(random, 20);
	return h.final();
}

} }  // namespace libtorrent::dht

