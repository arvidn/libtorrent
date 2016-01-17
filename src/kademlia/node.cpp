/*

Copyright (c) 2006-2016, Arvid Norberg
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

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <utility>
#include <boost/bind.hpp>
#include <boost/function/function1.hpp>

#ifdef TORRENT_USE_VALGRIND
#include <valgrind/memcheck.h>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/io.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/alert_types.hpp" // for dht_lookup
#include "libtorrent/performance_counters.hpp" // for counters

#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/rpc_manager.hpp"
#include "libtorrent/kademlia/routing_table.hpp"
#include "libtorrent/kademlia/node.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/kademlia/direct_request.hpp"

#include "libtorrent/kademlia/refresh.hpp"
#include "libtorrent/kademlia/get_peers.hpp"
#include "libtorrent/kademlia/get_item.hpp"

namespace libtorrent { namespace dht
{

using detail::write_endpoint;

namespace {

void nop() {}

node_id calculate_node_id(node_id const& nid, dht_observer* observer)
{
	address external_address;
	if (observer) external_address = observer->external_address();

	// if we don't have an observer, don't pretend that external_address is valid
	// generating an ID based on 0.0.0.0 would be terrible. random is better
	if (!observer || external_address == address())
	{
		return generate_random_id();
	}

	if (nid == (node_id::min)() || !verify_id(nid, external_address))
		return generate_id(external_address);

	return nid;
}

} // anonymous namespace

node::node(udp_socket_interface* sock
	, dht_settings const& settings, node_id nid
	, dht_observer* observer
	, struct counters& cnt
	, dht_storage_constructor_type storage_constructor)
	: m_settings(settings)
	, m_id(calculate_node_id(nid, observer))
	, m_table(m_id, 8, settings, observer)
	, m_rpc(m_id, m_settings, m_table, sock, observer)
	, m_observer(observer)
	, m_last_tracker_tick(aux::time_now())
	, m_last_self_refresh(min_time())
	, m_sock(sock)
	, m_counters(cnt)
	, m_storage(storage_constructor(m_id, m_settings))
{
	m_secret[0] = random();
	m_secret[1] = random();

	TORRENT_ASSERT(m_storage.get() != NULL);
}

node::~node() {}

void node::update_node_id()
{
	// if we don't have an observer, we can't ask for the external IP (and our
	// current node ID is likely not generated from an external address), so we
	// can just stop here in that case.
	if (!m_observer) return;

	// it's possible that our external address hasn't actually changed. If our
	// current ID is still valid, don't do anything.
	if (verify_id(m_id, m_observer->external_address()))
		return;

#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer) m_observer->log(dht_logger::node
		, "updating node ID (because external IP address changed)");
#endif

	m_id = generate_id(m_observer->external_address());

	m_table.update_node_id(m_id);
}

bool node::verify_token(std::string const& token, char const* info_hash
	, udp::endpoint const& addr) const
{
	if (token.length() != 4)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (m_observer)
		{
			m_observer->log(dht_logger::node, "token of incorrect length: %d"
				, int(token.length()));
		}
#endif
		return false;
	}

	hasher h1;
	error_code ec;
	std::string address = addr.address().to_string(ec);
	if (ec) return false;
	h1.update(&address[0], address.length());
	h1.update(reinterpret_cast<char const*>(&m_secret[0]), sizeof(m_secret[0]));
	h1.update(reinterpret_cast<char const*>(info_hash), sha1_hash::size);

	sha1_hash h = h1.final();
	if (std::equal(token.begin(), token.end(), reinterpret_cast<char*>(&h[0])))
		return true;

	hasher h2;
	h2.update(&address[0], address.length());
	h2.update(reinterpret_cast<char const*>(&m_secret[1]), sizeof(m_secret[1]));
	h2.update(info_hash, sha1_hash::size);
	h = h2.final();
	if (std::equal(token.begin(), token.end(), reinterpret_cast<char*>(&h[0])))
		return true;
	return false;
}

std::string node::generate_token(udp::endpoint const& addr, char const* info_hash)
{
	std::string token;
	token.resize(4);
	hasher h;
	error_code ec;
	std::string address = addr.address().to_string(ec);
	TORRENT_ASSERT(!ec);
	h.update(&address[0], address.length());
	h.update(reinterpret_cast<char*>(&m_secret[0]), sizeof(m_secret[0]));
	h.update(info_hash, sha1_hash::size);

	sha1_hash hash = h.final();
	std::copy(hash.begin(), hash.begin() + 4, reinterpret_cast<char*>(&token[0]));
	TORRENT_ASSERT(std::equal(token.begin(), token.end(), reinterpret_cast<char*>(&hash[0])));
	return token;
}

void node::bootstrap(std::vector<udp::endpoint> const& nodes
	, find_data::nodes_callback const& f)
{
	node_id target = m_id;
	make_id_secret(target);

	boost::intrusive_ptr<dht::bootstrap> r(new dht::bootstrap(*this, target, f));
	m_last_self_refresh = aux::time_now();

#ifndef TORRENT_DISABLE_LOGGING
	int count = 0;
#endif

	for (std::vector<udp::endpoint>::const_iterator i = nodes.begin()
		, end(nodes.end()); i != end; ++i)
	{
#ifndef TORRENT_DISABLE_LOGGING
		++count;
#endif
		r->add_entry(node_id(0), *i, observer::flag_initial);
	}

	// make us start as far away from our node ID as possible
	r->trim_seed_nodes();

#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer)
		m_observer->log(dht_logger::node, "bootstrapping with %d nodes", count);
#endif
	r->start();
}

int node::bucket_size(int bucket)
{
	return m_table.bucket_size(bucket);
}

void node::new_write_key()
{
	m_secret[1] = m_secret[0];
	m_secret[0] = random();
}

void node::unreachable(udp::endpoint const& ep)
{
	m_rpc.unreachable(ep);
}

void node::incoming(msg const& m)
{
	// is this a reply?
	bdecode_node y_ent = m.message.dict_find_string("y");
	if (!y_ent || y_ent.string_length() == 0)
	{
		// don't respond to this obviously broken messages. We don't
		// want to open up a magnification opportunity
//		entry e;
//		incoming_error(e, "missing 'y' entry");
//		m_sock.send_packet(e, m.addr, 0);
		return;
	}

	char y = *(y_ent.string_ptr());

	bdecode_node ext_ip = m.message.dict_find_string("ip");

	// backwards compatibility
	if (!ext_ip)
	{
		bdecode_node r = m.message.dict_find_dict("r");
		if (r)
			ext_ip = r.dict_find_string("ip");
	}

#if TORRENT_USE_IPV6
	if (ext_ip && ext_ip.string_length() >= 16)
	{
		// this node claims we use the wrong node-ID!
		address_v6::bytes_type b;
		memcpy(&b[0], ext_ip.string_ptr(), 16);
		if (m_observer)
			m_observer->set_external_address(address_v6(b)
				, m.addr.address());
	} else
#endif
	if (ext_ip && ext_ip.string_length() >= 4)
	{
		address_v4::bytes_type b;
		memcpy(&b[0], ext_ip.string_ptr(), 4);
		if (m_observer)
			m_observer->set_external_address(address_v4(b)
				, m.addr.address());
	}

	switch (y)
	{
		case 'r':
		{
			node_id id;
			m_rpc.incoming(m, &id);
			break;
		}
		case 'q':
		{
			TORRENT_ASSERT(m.message.dict_find_string_value("y") == "q");
			// When a DHT node enters the read-only state, it no longer
			// responds to 'query' messages that it receives.
			if (m_settings.read_only) break;

			entry e;
			incoming_request(m, e);
			m_sock->send_packet(e, m.addr, 0);
			break;
		}
		case 'e':
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (m_observer)
			{
				bdecode_node err = m.message.dict_find_list("e");
				if (err && err.list_size() >= 2
					&& err.list_at(0).type() == bdecode_node::int_t
					&& err.list_at(1).type() == bdecode_node::string_t
					&& m_observer)
				{
					m_observer->log(dht_logger::node, "INCOMING ERROR: (%" PRId64 ") %s"
						, err.list_int_value_at(0)
						, err.list_string_value_at(1).c_str());
				}
				else
				{
					m_observer->log(dht_logger::node, "INCOMING ERROR (malformed)");
				}
			}
#endif
			node_id id;
			m_rpc.incoming(m, &id);
			break;
		}
	}
}

namespace
{
	void announce_fun(std::vector<std::pair<node_entry, std::string> > const& v
		, node& node, int listen_port, sha1_hash const& ih, int flags)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (node.observer())
		{
			char hex_ih[41];
			to_hex(reinterpret_cast<char const*>(&ih[0]), 20, hex_ih);
			node.observer()->log(dht_logger::node, "sending announce_peer [ ih: %s "
				" p: %d nodes: %d ]", hex_ih, listen_port, int(v.size()));
		}
#endif

		// create a dummy traversal_algorithm
		boost::intrusive_ptr<traversal_algorithm> algo(
			new traversal_algorithm(node, (node_id::min)()));
		// store on the first k nodes
		for (std::vector<std::pair<node_entry, std::string> >::const_iterator i = v.begin()
			, end(v.end()); i != end; ++i)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (node.observer())
			{
				node.observer()->log(dht_logger::node, "announce-distance: %d"
					, (160 - distance_exp(ih, i->first.id)));
			}
#endif

			void* ptr = node.m_rpc.allocate_observer();
			if (ptr == 0) return;
			observer_ptr o(new (ptr) announce_observer(algo, i->first.ep(), i->first.id));
#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS
			o->m_in_constructor = false;
#endif
			entry e;
			e["y"] = "q";
			e["q"] = "announce_peer";
			entry& a = e["a"];
			a["info_hash"] = ih.to_string();
			a["port"] = listen_port;
			a["token"] = i->second;
			a["seed"] = (flags & node::flag_seed) ? 1 : 0;
			if (flags & node::flag_implied_port) a["implied_port"] = 1;
			node.stats_counters().inc_stats_counter(counters::dht_announce_peer_out);
			node.m_rpc.invoke(e, i->first.ep(), o);
		}
	}
}

void node::add_router_node(udp::endpoint router)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer)
	{
		m_observer->log(dht_logger::node, "adding router node: %s"
			, print_endpoint(router).c_str());
	}
#endif
	m_table.add_router_node(router);
}

void node::add_node(udp::endpoint node)
{
	// ping the node, and if we get a reply, it
	// will be added to the routing table
	send_single_refresh(node, m_table.num_active_buckets());
}

void node::get_peers(sha1_hash const& info_hash
	, boost::function<void(std::vector<tcp::endpoint> const&)> dcallback
	, boost::function<void(std::vector<std::pair<node_entry, std::string> > const&)> ncallback
	, bool noseeds)
{
	// search for nodes with ids close to id or with peers
	// for info-hash id. then send announce_peer to them.

	boost::intrusive_ptr<dht::get_peers> ta;
	if (m_settings.privacy_lookups)
	{
		ta.reset(new dht::obfuscated_get_peers(*this, info_hash, dcallback, ncallback, noseeds));
	}
	else
	{
		ta.reset(new dht::get_peers(*this, info_hash, dcallback, ncallback, noseeds));
	}

	ta->start();
}

void node::announce(sha1_hash const& info_hash, int listen_port, int flags
	, boost::function<void(std::vector<tcp::endpoint> const&)> f)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer)
	{
		char hex_ih[41];
		to_hex(reinterpret_cast<char const*>(&info_hash[0]), 20, hex_ih);
		m_observer->log(dht_logger::node, "announcing [ ih: %s p: %d ]"
			, hex_ih, listen_port);
	}
#endif

	get_peers(info_hash, f
		, boost::bind(&announce_fun, _1, boost::ref(*this)
		, listen_port, info_hash, flags), flags & node::flag_seed);
}

void node::direct_request(udp::endpoint ep, entry& e
	, boost::function<void(msg const&)> f)
{
	// not really a traversal
	boost::intrusive_ptr<direct_traversal> algo(
		new direct_traversal(*this, (node_id::min)(), f));

	void* ptr = m_rpc.allocate_observer();
	if (ptr == 0) return;
	observer_ptr o(new (ptr) direct_observer(algo, ep, (node_id::min)()));
#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS
	o->m_in_constructor = false;
#endif
	m_rpc.invoke(e, ep, o);
}

void node::get_item(sha1_hash const& target
	, boost::function<void(item const&)> f)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer)
	{
		char hex_target[41];
		to_hex(reinterpret_cast<char const*>(&target[0]), 20, hex_target);
		m_observer->log(dht_logger::node, "starting get for [ hash: %s ]"
			, hex_target);
	}
#endif

	boost::intrusive_ptr<dht::get_item> ta;
	ta.reset(new dht::get_item(*this, target, boost::bind(f, _1), find_data::nodes_callback()));
	ta->start();
}

void node::get_item(char const* pk, std::string const& salt
	, boost::function<void(item const&, bool)> f)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer)
	{
		char hex_key[65];
		to_hex(pk, 32, hex_key);
		m_observer->log(dht_logger::node, "starting get for [ key: %s ]", hex_key);
	}
#endif

	boost::intrusive_ptr<dht::get_item> ta;
	ta.reset(new dht::get_item(*this, pk, salt, f, find_data::nodes_callback()));
	ta->start();
}

namespace {

void put(std::vector<std::pair<node_entry, std::string> > const& nodes
	, boost::intrusive_ptr<dht::put_data> ta)
{
	ta->set_targets(nodes);
	ta->start();
}

void put_data_cb(item i, bool auth
	, boost::intrusive_ptr<put_data> ta
	, boost::function<void(item&)> f)
{
	// call data_callback only when we got authoritative data.
	if (auth)
	{
		f(i);
		ta->set_data(i);
	}
}

} // namespace

void node::put_item(sha1_hash const& target, entry const& data, boost::function<void(int)> f)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer)
	{
		char hex_target[41];
		to_hex(target.data(), 20, hex_target);
		m_observer->log(dht_logger::node, "starting get for [ hash: %s ]"
			, hex_target);
	}
#endif

	item i;
	i.assign(data);
	boost::intrusive_ptr<dht::put_data> put_ta;
	put_ta.reset(new dht::put_data(*this, boost::bind(f, _2)));
	put_ta->set_data(i);

	boost::intrusive_ptr<dht::get_item> ta;
	ta.reset(new dht::get_item(*this, target, get_item::data_callback(),
		boost::bind(&put, _1, put_ta)));
	ta->start();
}

void node::put_item(char const* pk, std::string const& salt
	, boost::function<void(item const&, int)> f
	, boost::function<void(item&)> data_cb)
{
	#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer)
	{
		char hex_key[65];
		to_hex(pk, 32, hex_key);
		m_observer->log(dht_logger::node, "starting get for [ key: %s ]", hex_key);
	}
	#endif

	boost::intrusive_ptr<dht::put_data> put_ta;
	put_ta.reset(new dht::put_data(*this, f));

	boost::intrusive_ptr<dht::get_item> ta;
	ta.reset(new dht::get_item(*this, pk, salt
		, boost::bind(&put_data_cb, _1, _2, put_ta, data_cb)
		, boost::bind(&put, _1, put_ta)));
	ta->start();
}

struct ping_observer : observer
{
	ping_observer(
		boost::intrusive_ptr<traversal_algorithm> const& algorithm
		, udp::endpoint const& ep, node_id const& id)
		: observer(algorithm, ep, id)
	{}

	// parses out "nodes"
	virtual void reply(msg const& m)
	{
		flags |= flag_done;

		bdecode_node r = m.message.dict_find_dict("r");
		if (!r)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (get_observer())
			{
				get_observer()->log(dht_logger::node
					, "[%p] missing response dict"
					, static_cast<void*>(algorithm()));
			}
#endif
			return;
		}

		// look for nodes
		bdecode_node n = r.dict_find_string("nodes");
		if (n)
		{
			char const* nodes = n.string_ptr();
			char const* end = nodes + n.string_length();

			while (end - nodes >= 26)
			{
				node_id id;
				std::copy(nodes, nodes + 20, id.begin());
				nodes += 20;
				algorithm()->get_node().m_table.heard_about(id
					, detail::read_v4_endpoint<udp::endpoint>(nodes));
			}
		}
	}
};

void node::tick()
{
	// every now and then we refresh our own ID, just to keep
	// expanding the routing table buckets closer to us.
	// if m_table.depth() < 4, means routing_table doesn't
	// have enough nodes.
	time_point now = aux::time_now();
	if (m_last_self_refresh + minutes(10) < now && m_table.depth() < 4)
	{
		node_id target = m_id;
		make_id_secret(target);
		boost::intrusive_ptr<dht::bootstrap> r(new dht::bootstrap(*this, target
			, boost::bind(&nop)));
		r->start();
		m_last_self_refresh = now;
		return;
	}

	node_entry const* ne = m_table.next_refresh();
	if (ne == NULL) return;

	// this shouldn't happen
	TORRENT_ASSERT(m_id != ne->id);
	if (ne->id == m_id) return;

	int bucket = 159 - distance_exp(m_id, ne->id);
	TORRENT_ASSERT(bucket < 160);
	send_single_refresh(ne->ep(), bucket, ne->id);
}

void node::send_single_refresh(udp::endpoint const& ep, int bucket
	, node_id const& id)
{
	TORRENT_ASSERT(id != m_id);
	void* ptr = m_rpc.allocate_observer();
	if (ptr == 0) return;

	TORRENT_ASSERT(bucket >= 0);
	TORRENT_ASSERT(bucket <= 159);

	// generate a random node_id within the given bucket
	// TODO: 2 it would be nice to have a bias towards node-id prefixes that
	// are missing in the bucket
	node_id mask = generate_prefix_mask(bucket + 1);
	node_id target = generate_secret_id() & ~mask;
	target |= m_id & mask;

	// create a dummy traversal_algorithm
	// this is unfortunately necessary for the observer
	// to free itself from the pool when it's being released
	boost::intrusive_ptr<traversal_algorithm> algo(
		new traversal_algorithm(*this, (node_id::min)()));
	observer_ptr o(new (ptr) ping_observer(algo, ep, id));
#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS
	o->m_in_constructor = false;
#endif
	entry e;
	e["y"] = "q";
	entry& a = e["a"];

	if (m_table.is_full(bucket))
	{
		// current bucket is full, just ping it.
		e["q"] = "ping";
		m_counters.inc_stats_counter(counters::dht_ping_out);
	}
	else
	{
		// use get_peers instead of find_node. We'll get nodes in the response
		// either way.
		e["q"] = "get_peers";
		a["info_hash"] = target.to_string();
		m_counters.inc_stats_counter(counters::dht_get_peers_out);
	}

	m_rpc.invoke(e, ep, o);
}

time_duration node::connection_timeout()
{
	time_duration d = m_rpc.tick();
	time_point now(aux::time_now());
	if (now - minutes(2) < m_last_tracker_tick) return d;
	m_last_tracker_tick = now;

	m_storage->tick();

	return d;
}

void node::status(std::vector<dht_routing_bucket>& table
	, std::vector<dht_lookup>& requests)
{
	mutex_t::scoped_lock l(m_mutex);

	m_table.status(table);

	for (std::set<traversal_algorithm*>::iterator i = m_running_requests.begin()
		, end(m_running_requests.end()); i != end; ++i)
	{
		requests.push_back(dht_lookup());
		dht_lookup& lookup = requests.back();
		(*i)->status(lookup);
	}
}

// TODO: in the future, this function should update all the
// dht related counter. For now, it just update the storage
// related ones.
void node::update_stats_counters(counters& c) const
{
	const dht_storage_counters& dht_cnt = m_storage->counters();
	c.set_value(counters::dht_torrents, dht_cnt.torrents);
	c.set_value(counters::dht_peers, dht_cnt.peers);
	c.set_value(counters::dht_immutable_data, dht_cnt.immutable_data);
	c.set_value(counters::dht_mutable_data, dht_cnt.mutable_data);
}

#ifndef TORRENT_NO_DEPRECATE
// TODO: 2 use the non deprecated function instead of this one
void node::status(session_status& s)
{
	mutex_t::scoped_lock l(m_mutex);

	m_table.status(s);
	s.dht_torrents = int(m_storage->num_torrents());
	s.active_requests.clear();
	s.dht_total_allocations = m_rpc.num_allocated_observers();
	for (std::set<traversal_algorithm*>::iterator i = m_running_requests.begin()
		, end(m_running_requests.end()); i != end; ++i)
	{
		s.active_requests.push_back(dht_lookup());
		dht_lookup& lookup = s.active_requests.back();
		(*i)->status(lookup);
	}
}
#endif

void node::lookup_peers(sha1_hash const& info_hash, entry& reply
	, bool noseed, bool scrape) const
{
	if (m_observer)
		m_observer->get_peers(info_hash);

	m_storage->get_peers(info_hash, noseed, scrape, reply);
}

void TORRENT_EXTRA_EXPORT write_nodes_entry(entry& r, nodes_t const& nodes)
{
	entry& n = r["nodes"];
	std::back_insert_iterator<std::string> out(n.string());
	for (nodes_t::const_iterator i = nodes.begin()
		, end(nodes.end()); i != end; ++i)
	{
		if (!i->addr().is_v4()) continue;
		std::copy(i->id.begin(), i->id.end(), out);
		write_endpoint(udp::endpoint(i->addr(), i->port()), out);
	}
}

// build response
void node::incoming_request(msg const& m, entry& e)
{
	if (!m_sock->has_quota())
		return;

	e = entry(entry::dictionary_t);
	e["y"] = "r";
	e["t"] = m.message.dict_find_string_value("t");

	key_desc_t top_desc[] = {
		{"q", bdecode_node::string_t, 0, 0},
		{"ro", bdecode_node::int_t, 0, key_desc_t::optional},
		{"a", bdecode_node::dict_t, 0, key_desc_t::parse_children},
			{"id", bdecode_node::string_t, 20, key_desc_t::last_child},
	};

	bdecode_node top_level[4];
	char error_string[200];
	if (!verify_message(m.message, top_desc, top_level, error_string
		, sizeof(error_string)))
	{
		incoming_error(e, error_string);
		return;
	}

	e["ip"] = endpoint_to_bytes(m.addr);

	bdecode_node arg_ent = top_level[2];
	bool read_only = top_level[1] && top_level[1].int_value() != 0;
	node_id id(top_level[3].string_ptr());

	// if this nodes ID doesn't match its IP, tell it what
	// its IP is with an error
	// don't enforce this yet
	if (m_settings.enforce_node_id && !verify_id(id, m.addr.address()))
	{
		incoming_error(e, "invalid node ID");
		return;
	}

	if (!read_only)
		m_table.heard_about(id, m.addr);

	entry& reply = e["r"];
	m_rpc.add_our_id(reply);

	// mirror back the other node's external port
	reply["p"] = m.addr.port();

	char const* query = top_level[0].string_ptr();
	int query_len = top_level[0].string_length();

	if (m_observer && m_observer->on_dht_request(query, query_len, m, e))
		return;

	if (query_len == 4 && memcmp(query, "ping", 4) == 0)
	{
		m_counters.inc_stats_counter(counters::dht_ping_in);
		// we already have 't' and 'id' in the response
		// no more left to add
	}
	else if (query_len == 9 && memcmp(query, "get_peers", 9) == 0)
	{
		key_desc_t msg_desc[] = {
			{"info_hash", bdecode_node::string_t, 20, 0},
			{"noseed", bdecode_node::int_t, 0, key_desc_t::optional},
			{"scrape", bdecode_node::int_t, 0, key_desc_t::optional},
		};

		bdecode_node msg_keys[3];
		if (!verify_message(arg_ent, msg_desc, msg_keys, error_string
			, sizeof(error_string)))
		{
			m_counters.inc_stats_counter(counters::dht_invalid_get_peers);
			incoming_error(e, error_string);
			return;
		}

		reply["token"] = generate_token(m.addr, msg_keys[0].string_ptr());

		m_counters.inc_stats_counter(counters::dht_get_peers_in);

		sha1_hash info_hash(msg_keys[0].string_ptr());
		nodes_t n;
		// always return nodes as well as peers
		m_table.find_node(info_hash, n, 0);
		write_nodes_entry(reply, n);

		bool noseed = false;
		bool scrape = false;
		if (msg_keys[1] && msg_keys[1].int_value() != 0) noseed = true;
		if (msg_keys[2] && msg_keys[2].int_value() != 0) scrape = true;
		lookup_peers(info_hash, reply, noseed, scrape);
#ifndef TORRENT_DISABLE_LOGGING
		if (reply.find_key("values") && m_observer)
		{
			m_observer->log(dht_logger::node, "values: %d"
				, int(reply["values"].list().size()));
		}
#endif
	}
	else if (query_len == 9 && memcmp(query, "find_node", 9) == 0)
	{
		key_desc_t msg_desc[] = {
			{"target", bdecode_node::string_t, 20, 0},
		};

		bdecode_node msg_keys[1];
		if (!verify_message(arg_ent, msg_desc, msg_keys, error_string, sizeof(error_string)))
		{
			incoming_error(e, error_string);
			return;
		}

		m_counters.inc_stats_counter(counters::dht_find_node_in);
		sha1_hash target(msg_keys[0].string_ptr());

		// TODO: 2 find_node should write directly to the response entry
		nodes_t n;
		m_table.find_node(target, n, 0);
		write_nodes_entry(reply, n);
	}
	else if (query_len == 13 && memcmp(query, "announce_peer", 13) == 0)
	{
		key_desc_t msg_desc[] = {
			{"info_hash", bdecode_node::string_t, 20, 0},
			{"port", bdecode_node::int_t, 0, 0},
			{"token", bdecode_node::string_t, 0, 0},
			{"n", bdecode_node::string_t, 0, key_desc_t::optional},
			{"seed", bdecode_node::int_t, 0, key_desc_t::optional},
			{"implied_port", bdecode_node::int_t, 0, key_desc_t::optional},
		};

		bdecode_node msg_keys[6];
		if (!verify_message(arg_ent, msg_desc, msg_keys, error_string, sizeof(error_string)))
		{
			m_counters.inc_stats_counter(counters::dht_invalid_announce);
			incoming_error(e, error_string);
			return;
		}

		int port = int(msg_keys[1].int_value());

		// is the announcer asking to ignore the explicit
		// listen port and instead use the source port of the packet?
		if (msg_keys[5] && msg_keys[5].int_value() != 0)
			port = m.addr.port();

		if (port < 0 || port >= 65536)
		{
			m_counters.inc_stats_counter(counters::dht_invalid_announce);
			incoming_error(e, "invalid port");
			return;
		}

		sha1_hash info_hash(msg_keys[0].string_ptr());

		if (m_observer)
			m_observer->announce(info_hash, m.addr.address(), port);

		if (!verify_token(msg_keys[2].string_value(), msg_keys[0].string_ptr(), m.addr))
		{
			m_counters.inc_stats_counter(counters::dht_invalid_announce);
			incoming_error(e, "invalid token");
			return;
		}

		m_counters.inc_stats_counter(counters::dht_announce_peer_in);

		// the token was correct. That means this
		// node is not spoofing its address. So, let
		// the table get a chance to add it.
		m_table.node_seen(id, m.addr, 0xffff);

		tcp::endpoint addr = tcp::endpoint(m.addr.address(), port);
		std::string name = msg_keys[3] ? msg_keys[3].string_value() : std::string();
		bool seed = msg_keys[4] && msg_keys[4].int_value();

		m_storage->announce_peer(info_hash, addr, name, seed);
	}
	else if (query_len == 3 && memcmp(query, "put", 3) == 0)
	{
		// the first 2 entries are for both mutable and
		// immutable puts
		static const key_desc_t msg_desc[] = {
			{"token", bdecode_node::string_t, 0, 0},
			{"v", bdecode_node::none_t, 0, 0},
			{"seq", bdecode_node::int_t, 0, key_desc_t::optional},
			// public key
			{"k", bdecode_node::string_t, item_pk_len, key_desc_t::optional},
			{"sig", bdecode_node::string_t, item_sig_len, key_desc_t::optional},
			{"cas", bdecode_node::int_t, 0, key_desc_t::optional},
			{"salt", bdecode_node::string_t, 0, key_desc_t::optional},
		};

		// attempt to parse the message
		bdecode_node msg_keys[7];
		if (!verify_message(arg_ent, msg_desc, msg_keys, error_string, sizeof(error_string)))
		{
			m_counters.inc_stats_counter(counters::dht_invalid_put);
			incoming_error(e, error_string);
			return;
		}

		m_counters.inc_stats_counter(counters::dht_put_in);

		// is this a mutable put?
		bool mutable_put = (msg_keys[2] && msg_keys[3] && msg_keys[4]);

		// public key (only set if it's a mutable put)
		char const* pk = NULL;
		if (msg_keys[3]) pk = msg_keys[3].string_ptr();

		// signature (only set if it's a mutable put)
		char const* sig = NULL;
		if (msg_keys[4]) sig = msg_keys[4].string_ptr();

		// pointer and length to the whole entry
		std::pair<char const*, int> buf = msg_keys[1].data_section();
		if (buf.second > 1000 || buf.second <= 0)
		{
			m_counters.inc_stats_counter(counters::dht_invalid_put);
			incoming_error(e, "message too big", 205);
			return;
		}

		std::pair<char const*, int> salt(static_cast<char const*>(NULL), 0);
		if (msg_keys[6])
			salt = std::pair<char const*, int>(
				msg_keys[6].string_ptr(), msg_keys[6].string_length());
		if (salt.second > 64)
		{
			m_counters.inc_stats_counter(counters::dht_invalid_put);
			incoming_error(e, "salt too big", 207);
			return;
		}

		sha1_hash target;
		if (pk)
			target = item_target_id(salt, pk);
		else
			target = item_target_id(buf);

//		fprintf(stderr, "%s PUT target: %s salt: %s key: %s\n"
//			, mutable_put ? "mutable":"immutable"
//			, to_hex(target.to_string()).c_str()
//			, salt.second > 0 ? std::string(salt.first, salt.second).c_str() : ""
//			, pk ? to_hex(std::string(pk, 32)).c_str() : "");

		// verify the write-token. tokens are only valid to write to
		// specific target hashes. it must match the one we got a "get" for
		if (!verify_token(msg_keys[0].string_value()
				, reinterpret_cast<char const*>(&target[0]), m.addr))
		{
			m_counters.inc_stats_counter(counters::dht_invalid_put);
			incoming_error(e, "invalid token");
			return;
		}

		if (!mutable_put)
		{
			m_storage->put_immutable_item(target, buf.first, buf.second, m.addr.address());
		}
		else
		{
			// mutable put, we must verify the signature

#ifdef TORRENT_USE_VALGRIND
			VALGRIND_CHECK_MEM_IS_DEFINED(msg_keys[4].string_ptr(), item_sig_len);
			VALGRIND_CHECK_MEM_IS_DEFINED(pk, item_pk_len);
#endif
			boost::int64_t seq = msg_keys[2].int_value();

			if (seq < 0)
			{
				m_counters.inc_stats_counter(counters::dht_invalid_put);
				incoming_error(e, "invalid (negative) sequence number");
				return;
			}

			// msg_keys[4] is the signature, msg_keys[3] is the public key
			if (!verify_mutable_item(buf, salt
				, seq, pk, sig))
			{
				m_counters.inc_stats_counter(counters::dht_invalid_put);
				incoming_error(e, "invalid signature", 206);
				return;
			}

			TORRENT_ASSERT(item_sig_len == msg_keys[4].string_length());

			boost::int64_t item_seq;
			if (!m_storage->get_mutable_item_seq(target, item_seq))
			{
				m_storage->put_mutable_item(target
					, buf.first, buf.second
					, sig, seq, pk
					, salt.first, salt.second
					, m.addr.address());
			}
			else
			{
				// this is the "cas" field in the put message
				// if it was specified, we MUST make sure the current sequence
				// number matches the expected value before replacing it
				// this is critical for avoiding race conditions when multiple
				// writers are accessing the same slot
				if (msg_keys[5] && item_seq != msg_keys[5].int_value())
				{
					m_counters.inc_stats_counter(counters::dht_invalid_put);
					incoming_error(e, "CAS mismatch", 301);
					return;
				}

				if (item_seq > seq)
				{
					m_counters.inc_stats_counter(counters::dht_invalid_put);
					incoming_error(e, "old sequence number", 302);
					return;
				}

				m_storage->put_mutable_item(target
					, buf.first, buf.second
					, sig, seq, pk
					, salt.first, salt.second
					, m.addr.address());
			}
		}

		m_table.node_seen(id, m.addr, 0xffff);
	}
	else if (query_len == 3 && memcmp(query, "get", 3) == 0)
	{
		key_desc_t msg_desc[] = {
			{"seq", bdecode_node::int_t, 0, key_desc_t::optional},
			{"target", bdecode_node::string_t, 20, 0},
		};

		// k is not used for now

		// attempt to parse the message
		bdecode_node msg_keys[2];
		if (!verify_message(arg_ent, msg_desc, msg_keys, error_string
			, sizeof(error_string)))
		{
			m_counters.inc_stats_counter(counters::dht_invalid_get);
			incoming_error(e, error_string);
			return;
		}

		m_counters.inc_stats_counter(counters::dht_get_in);
		sha1_hash target(msg_keys[1].string_ptr());

//		fprintf(stderr, "%s GET target: %s\n"
//			, msg_keys[1] ? "mutable":"immutable"
//			, to_hex(target.to_string()).c_str());

		reply["token"] = generate_token(m.addr, msg_keys[1].string_ptr());

		nodes_t n;
		// always return nodes as well as peers
		m_table.find_node(target, n, 0);
		write_nodes_entry(reply, n);

		// if the get has a sequence number it must be for a mutable item
		// so don't bother searching the immutable table
		if (!msg_keys[0])
		{
			if (!m_storage->get_immutable_item(target, reply)) // ok, check for a mutable one
			{
				m_storage->get_mutable_item(target, 0, true, reply);
			}
		}
		else
		{
			m_storage->get_mutable_item(target
				, msg_keys[0].int_value(), false
				, reply);
		}
	}
	else
	{
		// if we don't recognize the message but there's a
		// 'target' or 'info_hash' in the arguments, treat it
		// as find_node to be future compatible
		bdecode_node target_ent = arg_ent.dict_find_string("target");
		if (!target_ent || target_ent.string_length() != 20)
		{
			target_ent = arg_ent.dict_find_string("info_hash");
			if (!target_ent || target_ent.string_length() != 20)
			{
				incoming_error(e, "unknown message");
				return;
			}
		}

		sha1_hash target(target_ent.string_ptr());
		nodes_t n;
		// always return nodes as well as peers
		m_table.find_node(target, n, 0);
		write_nodes_entry(reply, n);
		return;
	}
}

} } // namespace libtorrent::dht
