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

#include <algorithm>

#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/node_entry.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/broadcast_socket.hpp" // for is_local et.al
#include "libtorrent/socket_io.hpp" // for hash_address
#include "libtorrent/random.hpp" // for random
#include "libtorrent/hasher.hpp" // for hasher
#include "libtorrent/crc32c.hpp" // for crc32c

namespace libtorrent { namespace dht
{

// returns the distance between the two nodes
// using the kademlia XOR-metric
node_id distance(node_id const& n1, node_id const& n2)
{
	node_id ret;
	node_id::iterator k = ret.begin();
	// TODO: 3 the XORing should be done at full words instead of bytes
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
	// TODO: 3 the XORing should be done at full words instead of bytes
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
	// TODO: 3 the xoring should be done at full words and _builtin_clz() could
	// be used as the last step
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
		int const bit = byte * 8;
		for (int b = 7; b >= 0; --b)
			if (t >= (1 << b)) return bit + b;
		return bit;
	}

	return 0;
}

node_id generate_id_impl(address const& ip_, boost::uint32_t r)
{
	boost::uint8_t* ip = 0;

	static const boost::uint8_t v4mask[] = { 0x03, 0x0f, 0x3f, 0xff };
#if TORRENT_USE_IPV6
	static const boost::uint8_t v6mask[] = { 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff };
#endif
	boost::uint8_t const* mask = 0;
	int num_octets = 0;

	address_v4::bytes_type b4;
#if TORRENT_USE_IPV6
	address_v6::bytes_type b6;
	if (ip_.is_v6())
	{
		b6 = ip_.to_v6().to_bytes();
		ip = &b6[0];
		num_octets = 8;
		mask = v6mask;
	}
	else
#endif
	{
		b4 = ip_.to_v4().to_bytes();
		ip = &b4[0];
		num_octets = 4;
		mask = v4mask;
	}

	for (int i = 0; i < num_octets; ++i)
		ip[i] &= mask[i];

	ip[0] |= (r & 0x7) << 5;

	// this is the crc32c (Castagnoli) polynomial
	boost::uint32_t c;
	if (num_octets == 4)
	{
		c = crc32c_32(*reinterpret_cast<boost::uint32_t*>(ip));
	}
	else
	{
		TORRENT_ASSERT(num_octets == 8);
		c = crc32c(reinterpret_cast<boost::uint64_t*>(ip), 1);
	}
	node_id id;

	id[0] = (c >> 24) & 0xff;
	id[1] = (c >> 16) & 0xff;
	id[2] = ((c >> 8) & 0xf8) | (random() & 0x7);

	for (int i = 3; i < 19; ++i) id[i] = random() & 0xff;
	id[19] = r & 0xff;

	return id;
}

static boost::uint32_t secret = 0;

void make_id_secret(node_id& in)
{
	if (secret == 0) secret = (random() % 0xfffffffe) + 1;

	boost::uint32_t rand = random();

	// generate the last 4 bytes as a "signature" of the previous 4 bytes. This
	// lets us verify whether a hash came from this function or not in the future.
	hasher h(reinterpret_cast<char*>(&secret), 4);
	h.update(reinterpret_cast<char*>(&rand), 4);
	sha1_hash secret_hash = h.final();
	memcpy(&in[20-4], &secret_hash[0], 4);
	memcpy(&in[20-8], &rand, 4);
}

node_id generate_random_id()
{
	char r[20];
	for (int i = 0; i < 20; ++i) r[i] = random() & 0xff;
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
	h.update(reinterpret_cast<char const*>(&nid[20-8]), 4);
	sha1_hash secret_hash = h.final();
	return memcmp(&nid[20-4], &secret_hash[0], 4) == 0;
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
	return generate_id_impl(ip, random());
}

bool matching_prefix(node_entry const& n, int mask, int prefix, int bucket_index)
{
	node_id id = n.id;
	id <<= bucket_index + 1;
	return (id[0] & mask) == prefix;
}

node_id generate_prefix_mask(int bits)
{
	TORRENT_ASSERT(bits >= 0);
	TORRENT_ASSERT(bits <= 160);
	node_id mask(0);
	int b = 0;
	for (; b < bits - 7; b += 8) mask[b/8] |= 0xff;
	if (bits < 160) mask[b/8] |= (0xff << (8 - (bits&7))) & 0xff;
	return mask;
}

} }  // namespace libtorrent::dht

