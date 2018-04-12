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
#ifndef NODE_ID_HPP
#define NODE_ID_HPP

#include <vector>
#include <cstdint>

#include <libtorrent/config.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/address.hpp>

namespace libtorrent { namespace dht {

using node_id = libtorrent::sha1_hash;

// returns the distance between the two nodes
// using the kademlia XOR-metric
TORRENT_EXTRA_EXPORT node_id distance(node_id const& n1, node_id const& n2);

// returns true if: distance(n1, ref) < distance(n2, ref)
TORRENT_EXTRA_EXPORT bool compare_ref(node_id const& n1, node_id const& n2, node_id const& ref);

// returns n in: 2^n <= distance(n1, n2) < 2^(n+1)
// useful for finding out which bucket a node belongs to
// the value that's returned is the number of trailing bits
// after the shared bit prefix of ``n1`` and ``n2``.
// if the first bits are different, that's 160.
TORRENT_EXTRA_EXPORT int distance_exp(node_id const& n1, node_id const& n2);
TORRENT_EXTRA_EXPORT int min_distance_exp(node_id const& n1, std::vector<node_id> const& ids);

TORRENT_EXTRA_EXPORT node_id generate_id(address const& external_ip);
TORRENT_EXTRA_EXPORT node_id generate_random_id();
TORRENT_EXTRA_EXPORT void make_id_secret(node_id& in);
TORRENT_EXTRA_EXPORT node_id generate_secret_id();
TORRENT_EXTRA_EXPORT bool verify_secret_id(node_id const& nid);
TORRENT_EXTRA_EXPORT node_id generate_id_impl(address const& ip_, std::uint32_t r);

TORRENT_EXTRA_EXPORT bool verify_id(node_id const& nid, address const& source_ip);
TORRENT_EXTRA_EXPORT bool matching_prefix(node_id const& nid, int mask, int prefix, int offset);
TORRENT_EXTRA_EXPORT node_id generate_prefix_mask(int bits);

} } // namespace libtorrent::dht

#endif // NODE_ID_HPP
