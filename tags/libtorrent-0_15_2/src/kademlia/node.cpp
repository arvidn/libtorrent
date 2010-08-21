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

#include "libtorrent/pch.hpp"

#include <utility>
#include <boost/bind.hpp>
#include <boost/optional.hpp>
#include <boost/function.hpp>
#include <boost/iterator_adaptors.hpp>

#include "libtorrent/io.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/random_sample.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/rpc_manager.hpp"
#include "libtorrent/kademlia/routing_table.hpp"
#include "libtorrent/kademlia/node.hpp"

#include "libtorrent/kademlia/refresh.hpp"
#include "libtorrent/kademlia/closest_nodes.hpp"
#include "libtorrent/kademlia/find_data.hpp"

namespace libtorrent { namespace dht
{

#ifdef _MSC_VER
namespace
{
	char rand() { return (char)std::rand(); }
}
#endif

// TODO: configurable?
enum { announce_interval = 30 };

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DEFINE_LOG(node)
#endif

// remove peers that have timed out
void purge_peers(std::set<peer_entry>& peers)
{
	for (std::set<peer_entry>::iterator i = peers.begin()
		  , end(peers.end()); i != end;)
	{
		// the peer has timed out
		if (i->added + minutes(int(announce_interval * 1.5f)) < time_now())
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(node) << "peer timed out at: " << i->addr;
#endif
			peers.erase(i++);
		}
		else
			++i;
	}
}

void nop() {}

node_impl::node_impl(libtorrent::aux::session_impl& ses
	, boost::function<void(msg const&)> const& f
	, dht_settings const& settings
	, boost::optional<node_id> nid)
	: m_settings(settings)
	, m_id(nid ? *nid : generate_id())
	, m_table(m_id, 8, settings)
	, m_rpc(boost::bind(&node_impl::incoming_request, this, _1)
		, m_id, m_table, f)
	, m_last_tracker_tick(time_now())
	, m_ses(ses)
{
	m_secret[0] = std::rand();
	m_secret[1] = std::rand();
}

bool node_impl::verify_token(msg const& m)
{
	std::string const& token = m.write_token;
	if (token.length() != 4)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(node) << "token of incorrect length: " << token.length();
#endif
		return false;
	}

	hasher h1;
	error_code ec;
	std::string address = m.addr.address().to_string(ec);
	if (ec) return false;
	h1.update(&address[0], address.length());
	h1.update((char*)&m_secret[0], sizeof(m_secret[0]));
	h1.update((char*)&m.info_hash[0], sha1_hash::size);
	
	sha1_hash h = h1.final();
	if (std::equal(token.begin(), token.end(), (signed char*)&h[0]))
		return true;
		
	hasher h2;
	h2.update(&address[0], address.length());
	h2.update((char*)&m_secret[1], sizeof(m_secret[1]));
	h2.update((char*)&m.info_hash[0], sha1_hash::size);
	h = h2.final();
	if (std::equal(token.begin(), token.end(), (signed char*)&h[0]))
		return true;
	return false;
}

std::string node_impl::generate_token(msg const& m)
{
	std::string token;
	token.resize(4);
	hasher h;
	error_code ec;
	std::string address = m.addr.address().to_string(ec);
	TORRENT_ASSERT(!ec);
	h.update(&address[0], address.length());
	h.update((char*)&m_secret[0], sizeof(m_secret[0]));
	h.update((char*)&m.info_hash[0], sha1_hash::size);

	sha1_hash hash = h.final();
	std::copy(hash.begin(), hash.begin() + 4, (signed char*)&token[0]);
	return token;
}

void node_impl::refresh(node_id const& id
	, boost::function0<void> f)
{
	// use the 'bucket size' closest nodes
	// to start the refresh with
	std::vector<node_entry> start;
	start.reserve(m_table.bucket_size());
	m_table.find_node(id, start, routing_table::include_failed);
	new dht::refresh(*this, id, start.begin(), start.end(), f);
}

void node_impl::bootstrap(std::vector<udp::endpoint> const& nodes
	, boost::function0<void> f)
{
/*
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(node) << "bootrapping: " << nodes.size();
	for (std::vector<udp::endpoint>::const_iterator i = nodes.begin()
		, end(nodes.end()); i != end; ++i)
		TORRENT_LOG(node) << "  " << *i;
#endif
*/
	std::vector<node_entry> start;
	start.reserve(nodes.size());
	std::copy(nodes.begin(), nodes.end(), std::back_inserter(start));
	new dht::refresh(*this, m_id, start.begin(), start.end(), f);
}

void node_impl::refresh()
{
	std::vector<node_entry> start;
	start.reserve(m_table.size().get<0>());
	std::copy(m_table.begin(), m_table.end(), std::back_inserter(start));

	new dht::refresh(*this, m_id, start.begin(), start.end(), boost::bind(&nop));
}

int node_impl::bucket_size(int bucket)
{
	return m_table.bucket_size(bucket);
}

void node_impl::new_write_key()
{
	m_secret[1] = m_secret[0];
	m_secret[0] = std::rand();
}

void node_impl::refresh_bucket(int bucket)
{
	TORRENT_ASSERT(bucket >= 0 && bucket < 160);
	
	// generate a random node_id within the given bucket
	node_id target = generate_id();
	int num_bits = 160 - bucket;
	node_id mask(0);
	for (int i = 0; i < num_bits; ++i)
	{
		int byte = i / 8;
		mask[byte] |= 0x80 >> (i % 8);
	}

	node_id root = m_id;
	root &= mask;
	target &= ~mask;
	target |= root;

	// make sure this is in another subtree than m_id
	// clear the (num_bits - 1) bit and then set it to the
	// inverse of m_id's corresponding bit.
	target[(num_bits - 1) / 8] &= ~(0x80 >> ((num_bits - 1) % 8));
	target[(num_bits - 1) / 8] |=
		(~(m_id[(num_bits - 1) / 8])) & (0x80 >> ((num_bits - 1) % 8));

	TORRENT_ASSERT(distance_exp(m_id, target) == bucket);

	std::vector<node_entry> start;
	start.reserve(m_table.bucket_size());
	m_table.find_node(target, start, routing_table::include_failed);

	new dht::refresh(*this, target, start.begin(), start.end(), boost::bind(&nop));
	m_table.touch_bucket(bucket);
}

void node_impl::unreachable(udp::endpoint const& ep)
{
	m_rpc.unreachable(ep);
}

void node_impl::incoming(msg const& m)
{
	if (m_rpc.incoming(m))
	{
		refresh();
	}
}

namespace
{
	void announce_fun(std::vector<std::pair<node_entry, std::string> > const& v
		, rpc_manager& rpc, int listen_port, sha1_hash const& ih)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(node) << "sending announce_peer [ ih: " << ih
			<< " p: " << listen_port
			<< " nodes: " << v.size() << " ]" ;
#endif
		
		// store on the first k nodes
		for (std::vector<std::pair<node_entry, std::string> >::const_iterator i = v.begin()
			, end(v.end()); i != end; ++i)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(node) << "  distance: " << (160 - distance_exp(ih, i->first.id));
#endif

			void* ptr = rpc.allocator().malloc();
			if (ptr == 0) return;
			rpc.allocator().set_next_size(10);
			observer_ptr o(new (ptr) announce_observer(
				rpc.allocator(), ih, listen_port, i->second));
#ifdef TORRENT_DEBUG
			o->m_in_constructor = false;
#endif
			rpc.invoke(messages::announce_peer, i->first.ep(), o);
		}
	}
}

void node_impl::add_router_node(udp::endpoint router)
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(node) << "adding router node: " << router;
#endif
	m_table.add_router_node(router);
}

void node_impl::add_node(udp::endpoint node)
{
	// ping the node, and if we get a reply, it
	// will be added to the routing table
	void* ptr = m_rpc.allocator().malloc();
	if (ptr == 0) return;
	m_rpc.allocator().set_next_size(10);
	observer_ptr o(new (ptr) null_observer(m_rpc.allocator()));
#ifdef TORRENT_DEBUG
	o->m_in_constructor = false;
#endif
	m_rpc.invoke(messages::ping, node, o);
}

void node_impl::announce(sha1_hash const& info_hash, int listen_port
	, boost::function<void(std::vector<tcp::endpoint> const&)> f)
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(node) << "announcing [ ih: " << info_hash << " p: " << listen_port << " ]" ;
#endif
	// search for nodes with ids close to id or with peers
	// for info-hash id. then send announce_peer to them.
	new find_data(*this, info_hash, f, boost::bind(&announce_fun, _1, boost::ref(m_rpc)
		, listen_port, info_hash));
}

time_duration node_impl::refresh_timeout()
{
	int refresh = -1;
	ptime now = time_now();
	ptime next = now + minutes(15);
	for (int i = 0; i < 160; ++i)
	{
		ptime r = m_table.next_refresh(i);
		if (r <= next)
		{
			refresh = i;
			next = r;
		}
	}
	if (next < now)
	{
		TORRENT_ASSERT(refresh > -1);
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(node) << "refreshing bucket: " << refresh;
#endif
		refresh_bucket(refresh);
	}

	time_duration next_refresh = next - now;
	time_duration min_next_refresh
		= minutes(15) / m_table.num_active_buckets();
	if (min_next_refresh > seconds(40))
		min_next_refresh = seconds(40);

	if (next_refresh < min_next_refresh)
		next_refresh = min_next_refresh;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(node) << "next refresh: " << total_seconds(next_refresh) << " seconds";
#endif

	return next_refresh;
}

time_duration node_impl::connection_timeout()
{
	time_duration d = m_rpc.tick();
	ptime now(time_now());
	if (now - m_last_tracker_tick < minutes(10)) return d;
	m_last_tracker_tick = now;

	// look through all peers and see if any have timed out
	for (data_iterator i = begin_data(), end(end_data()); i != end;)
	{
		torrent_entry& t = i->second;
		node_id const& key = i->first;
		++i;
		purge_peers(t.peers);

		// if there are no more peers, remove the entry altogether
		if (t.peers.empty())
		{
			table_t::iterator i = m_map.find(key);
			if (i != m_map.end()) m_map.erase(i);
		}
	}

	return d;
}

void node_impl::on_announce(msg const& m, msg& reply)
{
	if (m_ses.m_alerts.should_post<dht_announce_alert>())
		m_ses.m_alerts.post_alert(dht_announce_alert(
			m.addr.address(), m.port, m.info_hash));

	if (!verify_token(m))
	{
		reply.message_id = messages::error;
		reply.error_code = 203;
		reply.error_msg = "Incorrect token in announce_peer";
		return;
	}

	// the token was correct. That means this
	// node is not spoofing its address. So, let
	// the table get a chance to add it.
	m_table.node_seen(m.id, m.addr);

	torrent_entry& v = m_map[m.info_hash];
	peer_entry e;
	e.addr = tcp::endpoint(m.addr.address(), m.port);
	e.added = time_now();
	std::set<peer_entry>::iterator i = v.peers.find(e);
	if (i != v.peers.end()) v.peers.erase(i++);
	v.peers.insert(i, e);
}

namespace
{
	tcp::endpoint get_endpoint(peer_entry const& p)
	{
		return p.addr;
	}
}

void node_impl::status(session_status& s)
{
	mutex_t::scoped_lock l(m_mutex);

	m_table.status(s);
	s.dht_torrents = int(m_map.size());
	s.active_requests.clear();
	for (std::set<traversal_algorithm*>::iterator i = m_running_requests.begin()
		, end(m_running_requests.end()); i != end; ++i)
	{
		s.active_requests.push_back(dht_lookup());
		dht_lookup& l = s.active_requests.back();
		(*i)->status(l);
	}
}

bool node_impl::on_find(msg const& m, std::vector<tcp::endpoint>& peers) const
{
	if (m_ses.m_alerts.should_post<dht_get_peers_alert>())
		m_ses.m_alerts.post_alert(dht_get_peers_alert(m.info_hash));

	table_t::const_iterator i = m_map.find(m.info_hash);
	if (i == m_map.end()) return false;

	torrent_entry const& v = i->second;

	int num = (std::min)((int)v.peers.size(), m_settings.max_peers_reply);
	peers.clear();
	peers.reserve(num);
	random_sample_n(boost::make_transform_iterator(v.peers.begin(), &get_endpoint)
		, boost::make_transform_iterator(v.peers.end(), &get_endpoint)
		, std::back_inserter(peers), num);
/*
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	for (std::vector<tcp::endpoint>::iterator i = peers.begin()
		, end(peers.end()); i != end; ++i)
	{
		TORRENT_LOG(node) << "   " << *i;
	}
#endif
*/
	return true;
}

void node_impl::incoming_request(msg const& m)
{
	msg reply;
	reply.message_id = m.message_id;
	reply.addr = m.addr;
	reply.reply = true;
	reply.transaction_id = m.transaction_id;

	switch (m.message_id)
	{
	case messages::ping:
		break;
	case messages::get_peers:
		{
			reply.info_hash = m.info_hash;
			reply.write_token = generate_token(m);
			
			on_find(m, reply.peers);
			// always return nodes as well as peers
			m_table.find_node(m.info_hash, reply.nodes, 0);
/*
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			for (std::vector<node_entry>::iterator i = reply.nodes.begin()
				, end(reply.nodes.end()); i != end; ++i)
			{
				TORRENT_LOG(node) << "	" << i->id << " " << i->ep();
			}
#endif
*/
		}
		break;
	case messages::find_node:
		{
			reply.info_hash = m.info_hash;

			m_table.find_node(m.info_hash, reply.nodes, 0);
/*
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			for (std::vector<node_entry>::iterator i = reply.nodes.begin()
				, end(reply.nodes.end()); i != end; ++i)
			{
				TORRENT_LOG(node) << "	" << i->id << " " << i->ep();
			}
#endif
*/
		}
		break;
	case messages::announce_peer:
		on_announce(m, reply);
		break;
	default:
		TORRENT_ASSERT(false);
	};

	m_table.heard_about(m.id, m.addr);
	m_rpc.reply(reply);
}


} } // namespace libtorrent::dht
