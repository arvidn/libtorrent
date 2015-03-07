/*

Copyright (c) 2006-2014, Arvid Norberg & Daniel Wallin
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
#include <libtorrent/kademlia/routing_table.hpp>
#include <libtorrent/kademlia/rpc_manager.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/io.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/socket_io.hpp>
#include <vector>

namespace libtorrent { namespace dht
{

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_DECLARE_LOG(traversal);
#endif

using detail::read_endpoint_list;
using detail::read_v4_endpoint;
#if TORRENT_USE_IPV6
using detail::read_v6_endpoint;
#endif

void find_data_observer::reply(msg const& m)
{
	bdecode_node r = m.message.dict_find_dict("r");
	if (!r)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << m_algorithm.get() << "] missing response dict";
#endif
		return;
	}

	bdecode_node id = r.dict_find_string("id");
	if (!id || id.string_length() != 20)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(traversal) << "[" << m_algorithm.get() << "] invalid id in response";
#endif
		return;
	}
	bdecode_node token = r.dict_find_string("token");
	if (token)
	{
		static_cast<find_data*>(m_algorithm.get())->got_write_token(
			node_id(id.string_ptr()), token.string_value());
	}

	traversal_observer::reply(m);
	done();
}

void add_entry_fun(void* userdata, node_entry const& e)
{
	traversal_algorithm* f = (traversal_algorithm*)userdata;
	f->add_entry(e.id, e.ep(), observer::flag_initial);
}

find_data::find_data(
	node_impl& node
	, node_id target
	, nodes_callback const& ncallback)
	: traversal_algorithm(node, target)
	, m_nodes_callback(ncallback)
	, m_done(false)
{
}

void find_data::start()
{
	// if the user didn't add seed-nodes manually, grab a bunch of nodes from the
	// routing table
	if (m_results.empty())
		m_node.m_table.for_each_node(&add_entry_fun, 0, (traversal_algorithm*)this);

	traversal_algorithm::start();
}

void find_data::got_write_token(node_id const& n, std::string const& write_token)
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(traversal) << "[" << this << "] adding write "
		"token '" << to_hex(write_token) << "' under id '" << to_hex(n.to_string()) << "'";
#endif
	m_write_tokens[n] = write_token;
}

observer_ptr find_data::new_observer(void* ptr
	, udp::endpoint const& ep, node_id const& id)
{
	observer_ptr o(new (ptr) find_data_observer(this, ep, id));
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
	o->m_in_constructor = false;
#endif
	return o;
}

char const* find_data::name() const { return "find_data"; }

void find_data::done()
{
	if (m_invoke_count != 0) return;

	m_done = true;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(traversal) << "[" << this << "] " << name() << " DONE";
#endif

	std::vector<std::pair<node_entry, std::string> > results;
	int num_results = m_node.m_table.bucket_size();
	for (std::vector<observer_ptr>::iterator i = m_results.begin()
		, end(m_results.end()); i != end && num_results > 0; ++i)
	{
		observer_ptr const& o = *i;
		if ((o->flags & observer::flag_alive) == 0)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(traversal) << "[" << this << "]     not alive: "
				<< o->target_ep();
#endif
			continue;
		}
		std::map<node_id, std::string>::iterator j = m_write_tokens.find(o->id());
		if (j == m_write_tokens.end())
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(traversal) << "[" << this << "]     no write token: "
				<< o->target_ep();
#endif
			continue;
		}
		results.push_back(std::make_pair(node_entry(o->id(), o->target_ep()), j->second));
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(traversal) << "[" << this << "]     "
				<< o->target_ep();
#endif
		--num_results;
	}

	if (m_nodes_callback) m_nodes_callback(results);

	traversal_algorithm::done();
}

} } // namespace libtorrent::dht

