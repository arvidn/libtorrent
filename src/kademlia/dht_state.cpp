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

	bool read_immutable_data(dht_immutable_data& d, bdecode_node const& e)
	{
		if (e.type() != bdecode_node::dict_t) return false;

		auto t = e.dict_find_string_value("t");
		if (t.size() != 20) return false;
		d.target = sha1_hash(t);

		auto v = e.dict_find_string("v");
		if (!v) return false;
		d.value = {v.string_ptr(), v.string_ptr() + v.string_length()};

		auto ips = e.dict_find_string("ips");
		if (!ips || ips.string_length() != 128) return false;
		d.ips.from_string(ips.string_ptr());

		auto ts = e.dict_find_int("ts");
		if (!ts) return false;
		d.last_seen = ts.int_value();

		return true;
	}

	entry save_immutable_data(dht_immutable_data const& d)
	{
		entry ret(entry::dictionary_t);

		ret["t"] = d.target.to_string();
		ret["v"] = d.value;
		ret["ips"] = d.ips.to_string();
		ret["ts"] = d.last_seen;

		return ret;
	}

	std::vector<dht_immutable_data> read_immutable_items(bdecode_node const& e)
	{
		if (!e || e.type() != bdecode_node::list_t) return {};

		std::vector<dht_immutable_data> ret;
		int const size = e.list_size();
		for (int i = 0; i < size; i++)
		{
			dht_immutable_data d;
			if (!read_immutable_data(d, e.list_at(i))) continue;

			ret.push_back(std::move(d));
		}

		return ret;
	}

	entry save_immutable_items(std::vector<dht_immutable_data> items)
	{
		entry ret(entry::list_t);
		entry::list_type& list = ret.list();
		for (auto const& item : items)
		{
			list.push_back(save_immutable_data(item));
		}
		return ret;
	}

	std::vector<dht_mutable_data> read_mutable_items(bdecode_node const& e)
	{
		if (!e || e.type() != bdecode_node::list_t) return {};

		std::vector<dht_mutable_data> ret;
		int const size = e.list_size();
		for (int i = 0; i < size; i++)
		{
			dht_mutable_data d;
			if (!read_immutable_data(d, e.list_at(i))) continue;

			auto sig = e.dict_find_string("sig");
			if (!sig || sig.string_length() != signature::len) continue;
			d.sig = signature(sig.string_ptr());

			auto seq = e.dict_find_int("seq");
			if (!seq) continue;
			d.seq = sequence_number(std::uint64_t(seq.int_value()));

			auto k = e.dict_find_string("key");
			if (!k || k.string_length() != public_key::len) continue;
			d.key = public_key(k.string_ptr());

			auto salt = e.dict_find_string("slt");
			if (!salt) continue;
			d.salt = salt.string_value().to_string();

			ret.push_back(std::move(d));
		}

		return ret;
	}

	entry save_mutable_items(std::vector<dht_mutable_data> items)
	{
		entry ret(entry::list_t);
		entry::list_type& list = ret.list();
		for (auto const& item : items)
		{
			entry e = save_immutable_data(item);

			e["sig"] = item.sig.bytes;
			e["seq"] = std::int64_t(item.seq.value);
			e["key"] = item.key.bytes;
			e["slt"] = item.salt;

			list.push_back(e);
		}
		return ret;
	}

} // anonymous namespace

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

		ret.items.immutables = read_immutable_items(e.dict_find_dict("immutables-items"));
		ret.items.mutables = read_mutable_items(e.dict_find_dict("mutables-items"));

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
		entry const immutables_items = save_immutable_items(state.items.immutables);
		if (!immutables_items.list().empty())
			ret["immutables-items"] = immutables_items;
		entry const mutables_items = save_mutable_items(state.items.mutables);
		if (!mutables_items.list().empty())
			ret["mutables-items"] = mutables_items;
		return ret;
	}
}}
