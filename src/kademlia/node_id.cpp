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

#include <algorithm>

#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/node_entry.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/broadcast_socket.hpp" // for is_local et.al
#include "libtorrent/random.hpp" // for random
#include "libtorrent/hasher.hpp" // for hasher
#include "libtorrent/crc32c.hpp" // for crc32c

namespace libtorrent { namespace dht {

// returns the distance between the two nodes
// using the kademlia XOR-metric
node_id distance(node_id const& n1, node_id const& n2)
{
	return n1 ^ n2;
}

// returns true if: distance(n1, ref) < distance(n2, ref)
bool compare_ref(node_id const& n1, node_id const& n2, node_id const& ref)
{
	node_id const lhs = n1 ^ ref;
	node_id const rhs = n2 ^ ref;
	return lhs < rhs;
}

// returns n in: 2^n <= distance(n1, n2) < 2^(n+1)
// useful for finding out which bucket a node belongs to
int distance_exp(node_id const& n1, node_id const& n2)
{
	// TODO: it's a little bit weird to return 159 - leading zeroes. It should
	// probably be 160 - leading zeroes, but all other code in here is tuned to
	// this expectation now, and it doesn't really matter (other than complexity)
	return std::max(159 - distance(n1, n2).count_leading_zeroes(), 0);
}

int min_distance_exp(node_id const& n1, std::vector<node_id> const& ids)
{
	TORRENT_ASSERT(ids.size() > 0);

	int min = 160; // see distance_exp for the why of this constant
	for (auto const& node_id : ids)
	{
		min = std::min(min, distance_exp(n1, node_id));
	}

	return min;
}

node_id generate_id_impl(address const& ip_, std::uint32_t r)
{
	std::uint8_t* ip = nullptr;

	static std::uint8_t const v4mask[] = { 0x03, 0x0f, 0x3f, 0xff };
	static std::uint8_t const v6mask[] = { 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff };
	std::uint8_t const* mask = nullptr;
	int num_octets = 0;

	address_v4::bytes_type b4{};
	address_v6::bytes_type b6{};
	if (ip_.is_v6())
	{
		b6 = ip_.to_v6().to_bytes();
		ip = b6.data();
		num_octets = 8;
		mask = v6mask;
	}
	else
	{
		b4 = ip_.to_v4().to_bytes();
		ip = b4.data();
		num_octets = 4;
		mask = v4mask;
	}

	for (int i = 0; i < num_octets; ++i)
		ip[i] &= mask[i];

	ip[0] |= (r & 0x7) << 5;

	// this is the crc32c (Castagnoli) polynomial
	std::uint32_t c;
	if (num_octets == 4)
	{
		c = crc32c_32(*reinterpret_cast<std::uint32_t*>(ip));
	}
	else
	{
		TORRENT_ASSERT(num_octets == 8);
		c = crc32c(reinterpret_cast<std::uint64_t*>(ip), 1);
	}
	node_id id;

	id[0] = (c >> 24) & 0xff;
	id[1] = (c >> 16) & 0xff;
	id[2] = (((c >> 8) & 0xf8) | random(0x7)) & 0xff;

	for (int i = 3; i < 19; ++i) id[i] = random(0xff) & 0xff;
	id[19] = r & 0xff;

	return id;
}

static std::uint32_t secret = 0;

void make_id_secret(node_id& in)
{
	if (secret == 0) secret = random(0xfffffffe) + 1;

	std::uint32_t const rand = random(0xffffffff);

	// generate the last 4 bytes as a "signature" of the previous 4 bytes. This
	// lets us verify whether a hash came from this function or not in the future.
	hasher h(reinterpret_cast<char const*>(&secret), 4);
	h.update(reinterpret_cast<char const*>(&rand), 4);
	sha1_hash const secret_hash = h.final();
	std::memcpy(&in[20 - 4], &secret_hash[0], 4);
	std::memcpy(&in[20 - 8], &rand, 4);
}

node_id generate_random_id()
{
	char r[20];
	aux::random_bytes(r);
	return hasher(r, 20).final();
}

node_id generate_secret_id()
{
	node_id ret = generate_random_id();
	make_id_secret(ret);
	return ret;
}

bool verify_secret_id(node_id const& nid)
{
	if (secret == 0) return false;

	hasher h(reinterpret_cast<char*>(&secret), 4);
	h.update(reinterpret_cast<char const*>(&nid[20 - 8]), 4);
	sha1_hash secret_hash = h.final();
	return std::memcmp(&nid[20 - 4], &secret_hash[0], 4) == 0;
}

// verifies whether a node-id matches the IP it's used from
// returns true if the node-id is OK coming from this source
// and false otherwise.
bool verify_id(node_id const& nid, address const& source_ip)
{
	// no need to verify local IPs, they would be incorrect anyway
	if (is_local(source_ip)) return true;

	node_id h = generate_id_impl(source_ip, nid[19]);
	return nid[0] == h[0] && nid[1] == h[1] && (nid[2] & 0xf8) == (h[2] & 0xf8);
}

node_id generate_id(address const& ip)
{
	return generate_id_impl(ip, random(0xffffffff));
}

bool matching_prefix(node_id const& nid, int mask, int prefix, int offset)
{
	node_id id = nid;
	id <<= offset;
	return (id[0] & mask) == prefix;
}

node_id generate_prefix_mask(int const bits)
{
	TORRENT_ASSERT(bits >= 0);
	TORRENT_ASSERT(bits <= 160);
	node_id mask;
	std::size_t b = 0;
	for (; int(b) < bits - 7; b += 8) mask[b / 8] |= 0xff;
	if (bits < 160) mask[b / 8] |= (0xff << (8 - (bits & 7))) & 0xff;
	return mask;
}

} }  // namespace libtorrent::dht
