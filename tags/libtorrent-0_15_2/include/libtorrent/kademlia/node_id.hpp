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
#ifndef NODE_ID_HPP
#define NODE_ID_HPP

#include <algorithm>

#include <boost/cstdint.hpp>
#include "libtorrent/config.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent { namespace dht
{

typedef libtorrent::big_number node_id;

// returns the distance between the two nodes
// using the kademlia XOR-metric
node_id TORRENT_EXPORT distance(node_id const& n1, node_id const& n2);

// returns true if: distance(n1, ref) < distance(n2, ref)
bool TORRENT_EXPORT compare_ref(node_id const& n1, node_id const& n2, node_id const& ref);

// returns n in: 2^n <= distance(n1, n2) < 2^(n+1)
// usefult for finding out which bucket a node belongs to
int TORRENT_EXPORT distance_exp(node_id const& n1, node_id const& n2);

node_id TORRENT_EXPORT generate_id();

} } // namespace libtorrent::dht

#endif // NODE_ID_HPP

