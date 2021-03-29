/*

Copyright (c) 2006, 2008, 2013, 2015-2016, 2019-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/
#ifndef NODE_ID_HPP
#define NODE_ID_HPP

#include <vector>
#include <cstdint>

#include <libtorrent/config.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/address.hpp>

namespace lt {
namespace dht {

using node_id = lt::sha1_hash;

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

} // namespace dht
} // namespace lt

#endif // NODE_ID_HPP
