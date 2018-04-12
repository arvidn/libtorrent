/*

Copyright (c) 2006-2018, Arvid Norberg & Daniel Wallin
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

#include <libtorrent/kademlia/find_data.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/dht_observer.hpp>
#include <libtorrent/io.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/socket_io.hpp>

#ifndef TORRENT_DISABLE_LOGGING
#include <libtorrent/hex.hpp> // to_hex
#endif

namespace libtorrent { namespace dht {

void find_data_observer::reply(msg const& m)
{
	bdecode_node const r = m.message.dict_find_dict("r");
	if (!r)
	{
#ifndef TORRENT_DISABLE_LOGGING
		get_observer()->log(dht_logger::traversal, "[%u] missing response dict"
			, algorithm()->id());
#endif
		timeout();
		return;
	}

	bdecode_node const id = r.dict_find_string("id");
	if (!id || id.string_length() != 20)
	{
#ifndef TORRENT_DISABLE_LOGGING
		get_observer()->log(dht_logger::traversal, "[%u] invalid id in response"
			, algorithm()->id());
#endif
		timeout();
		return;
	}
	bdecode_node const token = r.dict_find_string("token");
	if (token)
	{
		static_cast<find_data*>(algorithm())->got_write_token(
			node_id(id.string_ptr()), token.string_value().to_string());
	}

	traversal_observer::reply(m);
	done();
}

find_data::find_data(
	node& dht_node
	, node_id const& target
	, nodes_callback const& ncallback)
	: traversal_algorithm(dht_node, target)
	, m_nodes_callback(ncallback)
	, m_done(false)
{
}

void find_data::start()
{
	// if the user didn't add seed-nodes manually, grab k (bucket size)
	// nodes from routing table.
	if (m_results.empty())
	{
		std::vector<node_entry> nodes;
		m_node.m_table.find_node(target(), nodes, routing_table::include_failed);

		for (auto const& n : nodes)
		{
			add_entry(n.id, n.ep(), observer::flag_initial);
		}
	}

	traversal_algorithm::start();
}

void find_data::got_write_token(node_id const& n, std::string write_token)
{
#ifndef TORRENT_DISABLE_LOGGING
	auto logger = get_node().observer();
	if (logger != nullptr && logger->should_log(dht_logger::traversal))
	{
		logger->log(dht_logger::traversal
			, "[%u] adding write token '%s' under id '%s'"
			, id(), aux::to_hex(write_token).c_str()
			, aux::to_hex(n).c_str());
	}
#endif
	m_write_tokens[n] = std::move(write_token);
}

observer_ptr find_data::new_observer(udp::endpoint const& ep
	, node_id const& id)
{
	auto o = m_node.m_rpc.allocate_observer<find_data_observer>(self(), ep, id);
#if TORRENT_USE_ASSERTS
	if (o) o->m_in_constructor = false;
#endif
	return o;
}

char const* find_data::name() const { return "find_data"; }

void find_data::done()
{
	m_done = true;

#ifndef TORRENT_DISABLE_LOGGING
	auto logger = get_node().observer();
	if (logger != nullptr)
	{
		logger->log(dht_logger::traversal, "[%u] %s DONE"
			, id(), name());
	}
#endif

	std::vector<std::pair<node_entry, std::string>> results;
	int num_results = m_node.m_table.bucket_size();
	for (auto i = m_results.begin()
		, end(m_results.end()); i != end && num_results > 0; ++i)
	{
		observer_ptr const& o = *i;
		if (!(o->flags & observer::flag_alive))
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (logger != nullptr && logger->should_log(dht_logger::traversal))
			{
				logger->log(dht_logger::traversal, "[%u] not alive: %s"
					, id(), print_endpoint(o->target_ep()).c_str());
			}
#endif
			continue;
		}
		auto j = m_write_tokens.find(o->id());
		if (j == m_write_tokens.end())
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (logger != nullptr && logger->should_log(dht_logger::traversal))
			{
				logger->log(dht_logger::traversal, "[%u] no write token: %s"
					, id(), print_endpoint(o->target_ep()).c_str());
			}
#endif
			continue;
		}
		results.emplace_back(node_entry(o->id(), o->target_ep()), j->second);
#ifndef TORRENT_DISABLE_LOGGING
		if (logger != nullptr && logger->should_log(dht_logger::traversal))
		{
			logger->log(dht_logger::traversal, "[%u] %s"
				, id(), print_endpoint(o->target_ep()).c_str());
		}
#endif
		--num_results;
	}

	if (m_nodes_callback) m_nodes_callback(results);

	traversal_algorithm::done();
}

} } // namespace libtorrent::dht
