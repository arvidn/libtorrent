/*

Copyright (c) 2017, Arvid Norberg, Alden Torres
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

#include <libtorrent/kademlia/sample_infohashes.hpp>
#include <libtorrent/kademlia/dht_observer.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/io.hpp>
#include <libtorrent/performance_counters.hpp>
#include <libtorrent/aux_/numeric_cast.hpp>
#include <libtorrent/socket_io.hpp>

namespace libtorrent { namespace dht
{

sample_infohashes::sample_infohashes(node& dht_node
	, node_id const& target
	, data_callback const& dcallback)
	: traversal_algorithm(dht_node, target)
	, m_data_callback(dcallback) {}

char const* sample_infohashes::name() const { return "sample_infohashes"; }

void sample_infohashes::got_samples(time_duration interval
	, int num, std::vector<sha1_hash> samples
	, std::vector<std::pair<sha1_hash, udp::endpoint>> nodes)
{
	if (m_data_callback)
	{
		m_data_callback(interval, num, std::move(samples), std::move(nodes));
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
	int const protocol_size = int(detail::address_size(protocol));
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
			seconds(interval), int(num), std::move(v), std::move(nodes));
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

}} // namespace libtorrent::dht
