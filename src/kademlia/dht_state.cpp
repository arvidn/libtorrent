/*

Copyright (c) 2016, Arvid Norberg, Alden Torres
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

#include "libtorrent/kademlia/dht_state.hpp"

#include <libtorrent/bdecode.hpp>
#include <libtorrent/socket_io.hpp>

namespace libtorrent {
namespace dht
{
namespace
{
	node_id extract_node_id(bdecode_node const& e, string_view key)
	{
		if (e.type() != bdecode_node::dict_t) return node_id();
		auto nid = e.dict_find_string_value(key);
		if (nid.size() != 20) return node_id();
		return node_id(nid);
	}

	entry save_nodes(std::vector<udp::endpoint> const& nodes)
	{
		entry ret(entry::list_t);
		entry::list_type& list = ret.list();
		for (auto const& ep : nodes)
		{
			std::string node;
			std::back_insert_iterator<std::string> out(node);
			detail::write_endpoint(ep, out);
			list.push_back(entry(node));
		}
		return ret;
	}
} // anonymous namespace

	void dht_state::clear()
	{
		nid.clear();
		nid6.clear();

		nodes.clear();
		nodes.shrink_to_fit();
		nodes6.clear();
		nodes6.shrink_to_fit();
	}

	dht_state read_dht_state(bdecode_node const& e)
	{
		dht_state ret;

		if (e.type() != bdecode_node::dict_t) return ret;

		ret.nid = extract_node_id(e, "node-id");
#if TORRENT_USE_IPV6
		ret.nid6 = extract_node_id(e, "node-id6");
#endif

		if (bdecode_node const nodes = e.dict_find_list("nodes"))
			ret.nodes = detail::read_endpoint_list<udp::endpoint>(nodes);
#if TORRENT_USE_IPV6
		if (bdecode_node const nodes = e.dict_find_list("nodes6"))
			ret.nodes6 = detail::read_endpoint_list<udp::endpoint>(nodes);
#endif

		return ret;
	}

	entry save_dht_state(dht_state const& state)
	{
		entry ret(entry::dictionary_t);
		ret["node-id"] = state.nid.to_string();
		entry const nodes = save_nodes(state.nodes);
		if (!nodes.list().empty()) ret["nodes"] = nodes;
#if TORRENT_USE_IPV6
		ret["node-id6"] = state.nid6.to_string();
		entry const nodes6 = save_nodes(state.nodes6);
		if (!nodes6.list().empty()) ret["nodes6"] = nodes6;
#endif
		return ret;
	}
}}
