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

namespace libtorrent { namespace dht {

	node_ids_t extract_node_ids(bdecode_node const& e, string_view key)
	{
		if (e.type() != bdecode_node::dict_t) return node_ids_t();
		node_ids_t ret;
		// first look for an old-style nid
		auto const old_nid = e.dict_find_string_value(key);
		if (old_nid.size() == 20)
		{
			ret.emplace_back(address(), node_id(old_nid));
			return ret;
		}
		auto const nids = e.dict_find_list(key);
		if (!nids) return ret;
		for (int i = 0; i < nids.list_size(); i++)
		{
			bdecode_node nid = nids.list_at(i);
			if (nid.type() != bdecode_node::string_t) continue;
			if (nid.string_length() < 20) continue;
			char const* in = nid.string_ptr();
			node_id id(in);
			in += id.size();
			address addr;
			if (nid.string_length() == 24)
				addr = detail::read_v4_address(in);
			else if (nid.string_length() == 36)
				addr = detail::read_v6_address(in);
			else
				continue;
			ret.emplace_back(addr, id);
		}

		return ret;
	}

namespace {
	entry save_nodes(std::vector<udp::endpoint> const& nodes)
	{
		entry ret(entry::list_t);
		entry::list_type& list = ret.list();
		for (auto const& ep : nodes)
		{
			std::string node;
			std::back_insert_iterator<std::string> out(node);
			detail::write_endpoint(ep, out);
			list.emplace_back(node);
		}
		return ret;
	}
} // anonymous namespace

	void dht_state::clear()
	{
		nids.clear();
		nids.shrink_to_fit();

		nodes.clear();
		nodes.shrink_to_fit();
		nodes6.clear();
		nodes6.shrink_to_fit();
	}

	dht_state read_dht_state(bdecode_node const& e)
	{
		dht_state ret;

		if (e.type() != bdecode_node::dict_t) return ret;

		ret.nids = extract_node_ids(e, "node-id");

		if (bdecode_node const nodes = e.dict_find_list("nodes"))
			ret.nodes = detail::read_endpoint_list<udp::endpoint>(nodes);
		if (bdecode_node const nodes = e.dict_find_list("nodes6"))
			ret.nodes6 = detail::read_endpoint_list<udp::endpoint>(nodes);
		return ret;
	}

	entry save_dht_state(dht_state const& state)
	{
		entry ret(entry::dictionary_t);
		auto& nids = ret["node-id"].list();
		for (auto const& n : state.nids)
		{
			std::string nid;
			std::copy(n.second.begin(), n.second.end(), std::back_inserter(nid));
			detail::write_address(n.first, std::back_inserter(nid));
			nids.emplace_back(std::move(nid));
		}
		entry const nodes = save_nodes(state.nodes);
		if (!nodes.list().empty()) ret["nodes"] = nodes;
		entry const nodes6 = save_nodes(state.nodes6);
		if (!nodes6.list().empty()) ret["nodes6"] = nodes6;
		return ret;
	}
}}
