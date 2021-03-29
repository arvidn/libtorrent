/*

Copyright (c) 2017, 2019-2020, Arvid Norberg
Copyright (c) 2017, 2021, Alden Torres
Copyright (c) 2020, Fonic
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <libtorrent/kademlia/sample_infohashes.hpp>
#include <libtorrent/kademlia/dht_observer.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/io.hpp>
#include <libtorrent/performance_counters.hpp>
#include <libtorrent/aux_/numeric_cast.hpp>
#include <libtorrent/aux_/socket_io.hpp>

namespace lt::dht
{

sample_infohashes::sample_infohashes(node& dht_node
	, node_id const& target
	, data_callback dcallback)
	: traversal_algorithm(dht_node, target)
	, m_data_callback(std::move(dcallback)) {}

char const* sample_infohashes::name() const { return "sample_infohashes"; }

void sample_infohashes::got_samples(sha1_hash const& nid
	, time_duration interval
	, int num, std::vector<sha1_hash> samples
	, std::vector<std::pair<sha1_hash, udp::endpoint>> nodes)
{
	if (m_data_callback)
	{
		m_data_callback(nid, interval, num, std::move(samples), std::move(nodes));
		m_data_callback = nullptr;
		done();
	}
}

sample_infohashes_observer::sample_infohashes_observer(
	std::shared_ptr<traversal_algorithm> algorithm
	, udp::endpoint const& ep, node_id const& id)
	: traversal_observer(std::move(algorithm), ep, id) {}

void sample_infohashes_observer::reply(msg const& m)
{
	bdecode_node r = m.message.dict_find_dict("r");
	if (!r)
	{
#ifndef TORRENT_DISABLE_LOGGING
		get_observer()->log(dht_logger::traversal, "[%u] missing response dict"
			, algorithm()->id());
#endif
		timeout();
		return;
	}

	// look for nodes
	std::vector<std::pair<sha1_hash, udp::endpoint>> nodes;
	udp const protocol = algorithm()->get_node().protocol();
	int const protocol_size = int(aux::address_size(protocol));
	char const* nodes_key = algorithm()->get_node().protocol_nodes_key();
	bdecode_node const n = r.dict_find_string(nodes_key);
	if (n)
	{
		char const* ptr = n.string_ptr();
		char const* end = ptr + n.string_length();

		while (end - ptr >= 20 + protocol_size + 2)
		{
			node_endpoint nep = read_node_endpoint(protocol, ptr);
			nodes.emplace_back(nep.id, nep.ep);
		}
	}

	bdecode_node const id = r.dict_find_string("id");
	if (!id || id.string_length() != 20)
	{
#ifndef TORRENT_DISABLE_LOGGING
		get_observer()->log(dht_logger::traversal, "[%u] wrong or missing id value"
			, algorithm()->id());
#endif
		timeout();
		return;
	}

	std::int64_t const interval = r.dict_find_int_value("interval", -1);
	if (interval < 0 || interval > 21600) // TODO: put constant in a common place
	{
#ifndef TORRENT_DISABLE_LOGGING
		get_observer()->log(dht_logger::traversal, "[%u] wrong or missing interval value"
			, algorithm()->id());
#endif
		timeout();
		return;
	}

	std::int64_t const num = r.dict_find_int_value("num", -1);
	if (num < 0 || num > std::numeric_limits<int>::max())
	{
#ifndef TORRENT_DISABLE_LOGGING
		get_observer()->log(dht_logger::traversal, "[%u] wrong or missing num value"
			, algorithm()->id());
#endif
		timeout();
		return;
	}

	bdecode_node samples = r.dict_find_string("samples");
	if (samples && (samples.string_length() % 20 == 0))
	{
		std::vector<sha1_hash> v(aux::numeric_cast<std::size_t>(samples.string_length() / 20));
		std::memcpy(v.data(), samples.string_ptr(), v.size() * 20);

		static_cast<sample_infohashes*>(algorithm())->got_samples(
			node_id(id.string_ptr()), seconds(interval), int(num), std::move(v), std::move(nodes));
	}
	else
	{
#ifndef TORRENT_DISABLE_LOGGING
		get_observer()->log(dht_logger::traversal, "[%u] wrong or missing samples value"
			, algorithm()->id());
#endif
		timeout();
	}

	// we deliberately do not call
	traversal_observer::reply(m);
	// this is necessary to play nice with
	// observer::abort(), observer::done() and observer::timeout()
	flags |= flag_done;
}

} // namespace lt::dht
