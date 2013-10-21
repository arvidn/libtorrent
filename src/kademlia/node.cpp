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
#include <boost/function/function1.hpp>

#include "libtorrent/io.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/rpc_manager.hpp"
#include "libtorrent/kademlia/routing_table.hpp"
#include "libtorrent/kademlia/node.hpp"

#include "libtorrent/kademlia/refresh.hpp"
#include "libtorrent/kademlia/find_data.hpp"
#include "libtorrent/rsa.hpp"

namespace libtorrent { namespace dht
{

void incoming_error(entry& e, char const* msg);

using detail::write_endpoint;

// TODO: configurable?
enum { announce_interval = 30 };

#ifdef TORRENT_DHT_VERBOSE_LOGGING
TORRENT_DEFINE_LOG(node)
extern int g_announces;
extern int g_failed_announces;
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

node_impl::node_impl(libtorrent::alert_manager& alerts
	, bool (*f)(void*, entry&, udp::endpoint const&, int)
	, dht_settings const& settings, node_id nid, address const& external_address
	, external_ip_fun ext_ip, void* userdata)
	: m_settings(settings)
	, m_id(nid == (node_id::min)() || !verify_id(nid, external_address) ? generate_id(external_address) : nid)
	, m_table(m_id, 8, settings)
	, m_rpc(m_id, m_table, f, userdata)
	, m_ext_ip(ext_ip)
	, m_last_tracker_tick(time_now())
	, m_alerts(alerts)
	, m_send(f)
	, m_userdata(userdata)
{
	m_secret[0] = random();
	m_secret[1] = std::rand();
}

bool node_impl::verify_token(std::string const& token, char const* info_hash
	, udp::endpoint const& addr)
{
	if (token.length() != 4)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(node) << "token of incorrect length: " << token.length();
#endif
		return false;
	}

	hasher h1;
	error_code ec;
	std::string address = addr.address().to_string(ec);
	if (ec) return false;
	h1.update(&address[0], address.length());
	h1.update((char*)&m_secret[0], sizeof(m_secret[0]));
	h1.update((char*)info_hash, sha1_hash::size);
	
	sha1_hash h = h1.final();
	if (std::equal(token.begin(), token.end(), (char*)&h[0]))
		return true;
		
	hasher h2;
	h2.update(&address[0], address.length());
	h2.update((char*)&m_secret[1], sizeof(m_secret[1]));
	h2.update((char*)info_hash, sha1_hash::size);
	h = h2.final();
	if (std::equal(token.begin(), token.end(), (char*)&h[0]))
		return true;
	return false;
}

std::string node_impl::generate_token(udp::endpoint const& addr, char const* info_hash)
{
	std::string token;
	token.resize(4);
	hasher h;
	error_code ec;
	std::string address = addr.address().to_string(ec);
	TORRENT_ASSERT(!ec);
	h.update(&address[0], address.length());
	h.update((char*)&m_secret[0], sizeof(m_secret[0]));
	h.update(info_hash, sha1_hash::size);

	sha1_hash hash = h.final();
	std::copy(hash.begin(), hash.begin() + 4, (char*)&token[0]);
	TORRENT_ASSERT(std::equal(token.begin(), token.end(), (char*)&hash[0]));
	return token;
}

void node_impl::refresh(node_id const& id
	, find_data::nodes_callback const& f)
{
	boost::intrusive_ptr<dht::refresh> r(new dht::refresh(*this, id, f));
	r->start();
}

void node_impl::bootstrap(std::vector<udp::endpoint> const& nodes
	, find_data::nodes_callback const& f)
{
	boost::intrusive_ptr<dht::refresh> r(new dht::bootstrap(*this, m_id, f));

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	int count = 0;
#endif

	for (std::vector<udp::endpoint>::const_iterator i = nodes.begin()
		, end(nodes.end()); i != end; ++i)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		++count;
#endif
		r->add_entry(node_id(0), *i, observer::flag_initial);
	}
	
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(node) << "bootstrapping with " << count << " nodes";
#endif
	r->start();
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

void node_impl::unreachable(udp::endpoint const& ep)
{
	m_rpc.unreachable(ep);
}

void node_impl::incoming(msg const& m)
{
	// is this a reply?
	lazy_entry const* y_ent = m.message.dict_find_string("y");
	if (!y_ent || y_ent->string_length() == 0)
	{
		entry e;
		incoming_error(e, "missing 'y' entry");
		m_send(m_userdata, e, m.addr, 0);
		return;
	}

	char y = *(y_ent->string_ptr());

	lazy_entry const* ext_ip = m.message.dict_find_string("ip");
	if (ext_ip && ext_ip->string_length() >= 4)
	{
		address_v4::bytes_type b;
		memcpy(&b[0], ext_ip->string_ptr(), 4);
		m_ext_ip(address_v4(b), aux::session_impl::source_dht, m.addr.address());
	}
#if TORRENT_USE_IPV6
	else if (ext_ip && ext_ip->string_length() >= 16)
	{
		address_v6::bytes_type b;
		memcpy(&b[0], ext_ip->string_ptr(), 16);
		m_ext_ip(address_v6(b), aux::session_impl::source_dht, m.addr.address());
	}
#endif

	switch (y)
	{
		case 'r':
		{
			node_id id;
			if (m_rpc.incoming(m, &id))
				refresh(id, boost::bind(&nop));
			break;
		}
		case 'q':
		{
			TORRENT_ASSERT(m.message.dict_find_string_value("y") == "q");
			entry e;
			incoming_request(m, e);
			m_send(m_userdata, e, m.addr, 0);
			break;
		}
		case 'e':
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			lazy_entry const* err = m.message.dict_find_list("e");
			if (err && err->list_size() >= 2)
			{
				TORRENT_LOG(node) << "INCOMING ERROR: " << err->list_string_value_at(1);
			}
#endif
			break;
		}
	}
}

namespace
{
	void announce_fun(std::vector<std::pair<node_entry, std::string> > const& v
		, node_impl& node, int listen_port, sha1_hash const& ih, bool seed)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(node) << "sending announce_peer [ ih: " << ih
			<< " p: " << listen_port
			<< " nodes: " << v.size() << " ]" ;
#endif

		// create a dummy traversal_algorithm		
		boost::intrusive_ptr<traversal_algorithm> algo(
			new traversal_algorithm(node, (node_id::min)()));

		// store on the first k nodes
		for (std::vector<std::pair<node_entry, std::string> >::const_iterator i = v.begin()
			, end(v.end()); i != end; ++i)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(node) << "  distance: " << (160 - distance_exp(ih, i->first.id));
#endif

			void* ptr = node.m_rpc.allocate_observer();
			if (ptr == 0) return;
			observer_ptr o(new (ptr) announce_observer(algo, i->first.ep(), i->first.id));
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			o->m_in_constructor = false;
#endif
			entry e;
			e["y"] = "q";
			e["q"] = "announce_peer";
			entry& a = e["a"];
			a["info_hash"] = ih.to_string();
			a["port"] = listen_port;
			a["token"] = i->second;
			a["seed"] = int(seed);
			node.m_rpc.invoke(e, i->first.ep(), o);
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
	void* ptr = m_rpc.allocate_observer();
	if (ptr == 0) return;

	// create a dummy traversal_algorithm		
	// this is unfortunately necessary for the observer
	// to free itself from the pool when it's being released
	boost::intrusive_ptr<traversal_algorithm> algo(
		new traversal_algorithm(*this, (node_id::min)()));
	observer_ptr o(new (ptr) null_observer(algo, node, node_id(0)));
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
	o->m_in_constructor = false;
#endif
	entry e;
	e["y"] = "q";
	e["q"] = "ping";
	m_rpc.invoke(e, node, o);
}

void node_impl::announce(sha1_hash const& info_hash, int listen_port, bool seed
	, boost::function<void(std::vector<tcp::endpoint> const&)> f)
{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_LOG(node) << "announcing [ ih: " << info_hash << " p: " << listen_port << " ]" ;
#endif
	// search for nodes with ids close to id or with peers
	// for info-hash id. then send announce_peer to them.
	boost::intrusive_ptr<find_data> ta(new find_data(*this, info_hash, f
		, boost::bind(&announce_fun, _1, boost::ref(*this)
		, listen_port, info_hash, seed), seed));
	ta->start();
}

void node_impl::tick()
{
	node_id target;
	if (m_table.need_refresh(target))
		refresh(target, boost::bind(&nop));
}

time_duration node_impl::connection_timeout()
{
	time_duration d = m_rpc.tick();
	ptime now(time_now());
	if (now - m_last_tracker_tick < minutes(2)) return d;
	m_last_tracker_tick = now;

	for (dht_immutable_table_t::iterator i = m_immutable_table.begin();
		i != m_immutable_table.end();)
	{
		if (i->second.last_seen + minutes(60) > now)
		{
			++i;
			continue;
		}
		free(i->second.value);
		m_immutable_table.erase(i++);
	}

	// look through all peers and see if any have timed out
	for (table_t::iterator i = m_map.begin(), end(m_map.end()); i != end;)
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

void node_impl::status(session_status& s)
{
	mutex_t::scoped_lock l(m_mutex);

	m_table.status(s);
	s.dht_torrents = int(m_map.size());
	s.active_requests.clear();
	s.dht_total_allocations = m_rpc.num_allocated_observers();
	for (std::set<traversal_algorithm*>::iterator i = m_running_requests.begin()
		, end(m_running_requests.end()); i != end; ++i)
	{
		s.active_requests.push_back(dht_lookup());
		dht_lookup& l = s.active_requests.back();
		(*i)->status(l);
	}
}

void node_impl::lookup_peers(sha1_hash const& info_hash, int prefix, entry& reply
	, bool noseed, bool scrape) const
{
	if (m_alerts.should_post<dht_get_peers_alert>())
		m_alerts.post_alert(dht_get_peers_alert(info_hash));

	table_t::const_iterator i = m_map.lower_bound(info_hash);
	if (i == m_map.end()) return;
	if (i->first != info_hash && prefix == 20) return;
	if (prefix != 20)
	{
		sha1_hash mask = sha1_hash::max();
		mask <<= (20 - prefix) * 8;
		if ((i->first & mask) != (info_hash & mask)) return;
	}

	torrent_entry const& v = i->second;

	if (!v.name.empty()) reply["n"] = v.name;

	if (scrape)
	{
		bloom_filter<256> downloaders;
		bloom_filter<256> seeds;

		for (std::set<peer_entry>::const_iterator i = v.peers.begin()
			, end(v.peers.end()); i != end; ++i)
		{
			sha1_hash iphash;
			hash_address(i->addr.address(), iphash);
			if (i->seed) seeds.set(iphash);
			else downloaders.set(iphash);
		}

		reply["BFpe"] = downloaders.to_string();
		reply["BFse"] = seeds.to_string();
	}
	else
	{
		int num = (std::min)((int)v.peers.size(), m_settings.max_peers_reply);
		std::set<peer_entry>::const_iterator iter = v.peers.begin();
		entry::list_type& pe = reply["values"].list();
		std::string endpoint;

		for (int t = 0, m = 0; m < num && iter != v.peers.end(); ++iter, ++t)
		{
			if ((random() / float(UINT_MAX + 1.f)) * (num - t) >= num - m) continue;
			if (noseed && iter->seed) continue;
			endpoint.resize(18);
			std::string::iterator out = endpoint.begin();
			write_endpoint(iter->addr, out);
			endpoint.resize(out - endpoint.begin());
			pe.push_back(entry(endpoint));

			++m;
		}
	}
	return;
}

namespace
{
	void write_nodes_entry(entry& r, nodes_t const& nodes)
	{
		bool ipv6_nodes = false;
		entry& n = r["nodes"];
		std::back_insert_iterator<std::string> out(n.string());
		for (nodes_t::const_iterator i = nodes.begin()
			, end(nodes.end()); i != end; ++i)
		{
			if (!i->addr.is_v4())
			{
				ipv6_nodes = true;
				continue;
			}
			std::copy(i->id.begin(), i->id.end(), out);
			write_endpoint(udp::endpoint(i->addr, i->port), out);
		}

		if (ipv6_nodes)
		{
			entry& p = r["nodes2"];
			std::string endpoint;
			for (nodes_t::const_iterator i = nodes.begin()
				, end(nodes.end()); i != end; ++i)
			{
				if (!i->addr.is_v6()) continue;
				endpoint.resize(18 + 20);
				std::string::iterator out = endpoint.begin();
				std::copy(i->id.begin(), i->id.end(), out);
				out += 20;
				write_endpoint(udp::endpoint(i->addr, i->port), out);
				endpoint.resize(out - endpoint.begin());
				p.list().push_back(entry(endpoint));
			}
		}
	}
}

// verifies that a message has all the required
// entries and returns them in ret
bool verify_message(lazy_entry const* msg, key_desc_t const desc[], lazy_entry const* ret[]
	, int size , char* error, int error_size)
{
	// clear the return buffer
	memset(ret, 0, sizeof(ret[0]) * size);

	// when parsing child nodes, this is the stack
	// of lazy_entry pointers to return to
	lazy_entry const* stack[5];
	int stack_ptr = -1;

	if (msg->type() != lazy_entry::dict_t)
	{
		snprintf(error, error_size, "not a dictionary");
		return false;
	}
	++stack_ptr;
	stack[stack_ptr] = msg;
	for (int i = 0; i < size; ++i)
	{
		key_desc_t const& k = desc[i];

//		fprintf(stderr, "looking for %s in %s\n", k.name, print_entry(*msg).c_str());

		ret[i] = msg->dict_find(k.name);
		// none_t means any type
		if (ret[i] && ret[i]->type() != k.type && k.type != lazy_entry::none_t) ret[i] = 0;
		if (ret[i] == 0 && (k.flags & key_desc_t::optional) == 0)
		{
			// the key was not found, and it's not an optional key
			snprintf(error, error_size, "missing '%s' key", k.name);
			return false;
		}

		if (k.size > 0
			&& ret[i]
			&& k.type == lazy_entry::string_t)
		{
			bool invalid = false;
			if (k.flags & key_desc_t::size_divisible)
				invalid = (ret[i]->string_length() % k.size) != 0;
			else
				invalid = ret[i]->string_length() != k.size;

			if (invalid)
			{
				// the string was not of the required size
				ret[i] = 0;
				if ((k.flags & key_desc_t::optional) == 0)
				{
					snprintf(error, error_size, "invalid value for '%s'", k.name);
					return false;
				}
			}
		}
		if (k.flags & key_desc_t::parse_children)
		{
			TORRENT_ASSERT(k.type == lazy_entry::dict_t);

			if (ret[i])
			{
				++stack_ptr;
				TORRENT_ASSERT(stack_ptr < int(sizeof(stack)/sizeof(stack[0])));
				msg = ret[i];
				stack[stack_ptr] = msg;
			}
			else
			{
				// skip all children
				while (i < size && (desc[i].flags & key_desc_t::last_child) == 0) ++i;
				// if this assert is hit, desc is incorrect
				TORRENT_ASSERT(i < size);
			}
		}
		else if (k.flags & key_desc_t::last_child)
		{
			TORRENT_ASSERT(stack_ptr > 0);
			// this can happen if the specification passed
			// in is unbalanced. i.e. contain more last_child
			// nodes than parse_children
			if (stack_ptr == 0) return false;
			--stack_ptr;
			msg = stack[stack_ptr];
		}
	}
	return true;
}

void incoming_error(entry& e, char const* msg)
{
	e["y"] = "e";
	entry::list_type& l = e["e"].list();
	l.push_back(entry(203));
	l.push_back(entry(msg));
}

// build response
void node_impl::incoming_request(msg const& m, entry& e)
{
	e = entry(entry::dictionary_t);
	e["y"] = "r";
	e["t"] = m.message.dict_find_string_value("t");

	key_desc_t top_desc[] = {
		{"q", lazy_entry::string_t, 0, 0},
		{"a", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"id", lazy_entry::string_t, 20, key_desc_t::last_child},
	};

	lazy_entry const* top_level[3];
	char error_string[200];
	if (!verify_message(&m.message, top_desc, top_level, 3, error_string, sizeof(error_string)))
	{
		incoming_error(e, error_string);
		return;
	}

	e["ip"] = endpoint_to_bytes(m.addr);
/*
	// if this nodes ID doesn't match its IP, tell it what
	// its IP is with an error
	// don't enforce this yet
	if (!verify_id(id, m.addr.address()))
	{
		incoming_error(e, "invalid node ID");
		return;
	}
*/
	char const* query = top_level[0]->string_cstr();

	lazy_entry const* arg_ent = top_level[1];

	node_id id(top_level[2]->string_ptr());

	m_table.heard_about(id, m.addr);

	entry& reply = e["r"];
	m_rpc.add_our_id(reply);

	if (strcmp(query, "ping") == 0)
	{
		// we already have 't' and 'id' in the response
		// no more left to add
	}
	else if (strcmp(query, "get_peers") == 0)
	{
		key_desc_t msg_desc[] = {
			{"info_hash", lazy_entry::string_t, 20, 0},
			{"ifhpfxl", lazy_entry::int_t, 0, key_desc_t::optional},
			{"noseed", lazy_entry::int_t, 0, key_desc_t::optional},
			{"scrape", lazy_entry::int_t, 0, key_desc_t::optional},
		};

		lazy_entry const* msg_keys[4];
		if (!verify_message(arg_ent, msg_desc, msg_keys, 4, error_string, sizeof(error_string)))
		{
			incoming_error(e, error_string);
			return;
		}

		reply["token"] = generate_token(m.addr, msg_keys[0]->string_ptr());
		
		sha1_hash info_hash(msg_keys[0]->string_ptr());
		nodes_t n;
		// always return nodes as well as peers
		m_table.find_node(info_hash, n, 0);
		write_nodes_entry(reply, n);

		int prefix = msg_keys[1] ? int(msg_keys[1]->int_value()) : 20;
		if (prefix > 20) prefix = 20;
		else if (prefix < 4) prefix = 4;

		bool noseed = false;
		bool scrape = false;
		if (msg_keys[2] && msg_keys[2]->int_value() != 0) noseed = true;
		if (msg_keys[3] && msg_keys[3]->int_value() != 0) scrape = true;
		lookup_peers(info_hash, prefix, reply, noseed, scrape);
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		if (reply.find_key("values")) TORRENT_LOG(node) << " values: " << reply["values"].list().size();
#endif
	}
	else if (strcmp(query, "find_node") == 0)
	{
		key_desc_t msg_desc[] = {
			{"target", lazy_entry::string_t, 20, 0},
		};

		lazy_entry const* msg_keys[1];
		if (!verify_message(arg_ent, msg_desc, msg_keys, 1, error_string, sizeof(error_string)))
		{
			incoming_error(e, error_string);
			return;
		}

		sha1_hash target(msg_keys[0]->string_ptr());

		// TODO: find_node should write directly to the response entry
		nodes_t n;
		m_table.find_node(target, n, 0);
		write_nodes_entry(reply, n);
	}
	else if (strcmp(query, "announce_peer") == 0)
	{
		key_desc_t msg_desc[] = {
			{"info_hash", lazy_entry::string_t, 20, 0},
			{"port", lazy_entry::int_t, 0, 0},
			{"token", lazy_entry::string_t, 0, 0},
			{"n", lazy_entry::string_t, 0, key_desc_t::optional},
			{"seed", lazy_entry::int_t, 0, key_desc_t::optional},
			{"implied_port", lazy_entry::int_t, 0, key_desc_t::optional},
		};

		lazy_entry const* msg_keys[6];
		if (!verify_message(arg_ent, msg_desc, msg_keys, 6, error_string, sizeof(error_string)))
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			++g_failed_announces;
#endif
			incoming_error(e, error_string);
			return;
		}

		int port = int(msg_keys[1]->int_value());

		// is the announcer asking to ignore the explicit
		// listen port and instead use the source port of the packet?
		if (msg_keys[5] && msg_keys[5]->int_value() != 0)
			port = m.addr.port();

		if (port < 0 || port >= 65536)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			++g_failed_announces;
#endif
			incoming_error(e, "invalid port");
			return;
		}

		sha1_hash info_hash(msg_keys[0]->string_ptr());

		if (m_alerts.should_post<dht_announce_alert>())
			m_alerts.post_alert(dht_announce_alert(
				m.addr.address(), port, info_hash));

		if (!verify_token(msg_keys[2]->string_value(), msg_keys[0]->string_ptr(), m.addr))
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			++g_failed_announces;
#endif
			incoming_error(e, "invalid token");
			return;
		}

		// the token was correct. That means this
		// node is not spoofing its address. So, let
		// the table get a chance to add it.
		m_table.node_seen(id, m.addr);

		if (!m_map.empty() && int(m_map.size()) >= m_settings.max_torrents)
		{
			// we need to remove some. Remove the ones with the
			// fewest peers
			int num_peers = m_map.begin()->second.peers.size();
			table_t::iterator candidate = m_map.begin();
			for (table_t::iterator i = m_map.begin()
				, end(m_map.end()); i != end; ++i)
			{
				if (int(i->second.peers.size()) > num_peers) continue;
				if (i->first == info_hash) continue;
				num_peers = i->second.peers.size();
				candidate = i;
			}
			m_map.erase(candidate);
		}
		torrent_entry& v = m_map[info_hash];

		// the peer announces a torrent name, and we don't have a name
		// for this torrent. Store it.
		if (msg_keys[3] && v.name.empty())
		{
			std::string name = msg_keys[3]->string_value();
			if (name.size() > 50) name.resize(50);
			v.name = name;
		}

		peer_entry peer;
		peer.addr = tcp::endpoint(m.addr.address(), port);
		peer.added = time_now();
		peer.seed = msg_keys[4] && msg_keys[4]->int_value();
		std::set<peer_entry>::iterator i = v.peers.find(peer);
		if (i != v.peers.end()) v.peers.erase(i++);
		v.peers.insert(i, peer);
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		++g_announces;
#endif
	}
	else if (strcmp(query, "put") == 0)
	{
		// the first 2 entries are for both mutable and
		// immutable puts
		const static key_desc_t msg_desc[] = {
			{"token", lazy_entry::string_t, 0, 0},
			{"v", lazy_entry::none_t, 0, 0},
			{"seq", lazy_entry::int_t, 0, key_desc_t::optional},
			// public key
			{"k", lazy_entry::string_t, 268, key_desc_t::optional},
			{"sig", lazy_entry::string_t, 256, key_desc_t::optional},
		};

		// attempt to parse the message
		lazy_entry const* msg_keys[5];
		if (!verify_message(arg_ent, msg_desc, msg_keys, 5, error_string, sizeof(error_string)))
		{
			incoming_error(e, error_string);
			return;
		}

		// is this a mutable put?
		bool mutable_put = (msg_keys[2] && msg_keys[3] && msg_keys[4]);

		// pointer and length to the whole entry
		std::pair<char const*, int> buf = msg_keys[1]->data_section();
		if (buf.second > 767 || buf.second <= 0)
		{
			incoming_error(e, "message too big");
			return;
		}

		sha1_hash target;
		if (!mutable_put)
			target = hasher(buf.first, buf.second).final();
		else
			target = sha1_hash(msg_keys[3]->string_ptr());

//		fprintf(stderr, "%s PUT target: %s\n"
//			, mutable_put ? "mutable":"immutable"
//			, to_hex(target.to_string()).c_str());

		// verify the write-token. tokens are only valid to write to
		// specific target hashes. it must match the one we got a "get" for
		if (!verify_token(msg_keys[0]->string_value(), (char const*)&target[0], m.addr))
		{
			incoming_error(e, "invalid token");
			return;
		}

		dht_immutable_item* f = 0;

		if (!mutable_put)
		{
			dht_immutable_table_t::iterator i = m_immutable_table.find(target);
			if (i == m_immutable_table.end())
			{
				// make sure we don't add too many items
				if (int(m_immutable_table.size()) >= m_settings.max_dht_items)
				{
					// delete the least important one (i.e. the one
					// the fewest peers are announcing)
					dht_immutable_table_t::iterator j = std::min_element(m_immutable_table.begin()
						, m_immutable_table.end()
						, boost::bind(&dht_immutable_item::num_announcers
							, boost::bind(&dht_immutable_table_t::value_type::second, _1))
							< boost::bind(&dht_immutable_item::num_announcers
							, boost::bind(&dht_immutable_table_t::value_type::second, _2)));
					TORRENT_ASSERT(j != m_immutable_table.end());
					free(j->second.value);
					m_immutable_table.erase(j);
				}
				dht_immutable_item to_add;
				to_add.value = (char*)malloc(buf.second);
				to_add.size = buf.second;
				memcpy(to_add.value, buf.first, buf.second);
		
				boost::tie(i, boost::tuples::ignore) = m_immutable_table.insert(
					std::make_pair(target, to_add));
			}

//			fprintf(stderr, "added immutable item (%d)\n", int(m_immutable_table.size()));

			f = &i->second;
		}
		else
		{
			// mutable put, we must verify the signature
			// generate the message digest by merging the sequence number and the
			hasher digest;
			char seq[20];
			int len = snprintf(seq, sizeof(seq), "3:seqi%" PRId64 "e1:v", msg_keys[2]->int_value());
			digest.update(seq, len);
			std::pair<char const*, int> buf = msg_keys[1]->data_section();
			digest.update(buf.first, buf.second);

#ifdef TORRENT_USE_OPENSSL
			if (!verify_rsa(digest.final(), msg_keys[3]->string_ptr(), msg_keys[3]->string_length()
				, msg_keys[4]->string_ptr(), msg_keys[4]->string_length()))
			{
				incoming_error(e, "invalid signature");
				return;
			}
#else
			incoming_error(e, "unsupported");
			return;
#endif

			sha1_hash target = hasher(msg_keys[3]->string_ptr(), msg_keys[3]->string_length()).final();
			dht_mutable_table_t::iterator i = m_mutable_table.find(target);
			if (i == m_mutable_table.end())
			{
				// make sure we don't add too many items
				if (int(m_mutable_table.size()) >= m_settings.max_dht_items)
				{
					// delete the least important one (i.e. the one
					// the fewest peers are announcing)
					dht_mutable_table_t::iterator j = std::min_element(m_mutable_table.begin()
						, m_mutable_table.end()
						, boost::bind(&dht_immutable_item::num_announcers
							, boost::bind(&dht_mutable_table_t::value_type::second, _1)));
					TORRENT_ASSERT(j != m_mutable_table.end());
					free(j->second.value);
					m_mutable_table.erase(j);
				}
				dht_mutable_item to_add;
				to_add.value = (char*)malloc(buf.second);
				to_add.size = buf.second;
				to_add.seq = msg_keys[2]->int_value();
				memcpy(to_add.sig, msg_keys[4]->string_ptr(), sizeof(to_add.sig));
				TORRENT_ASSERT(sizeof(to_add.sig) == msg_keys[4]->string_length());
				memcpy(to_add.value, buf.first, buf.second);
				memcpy(&to_add.key, msg_keys[3]->string_ptr(), sizeof(to_add.key));
		
				boost::tie(i, boost::tuples::ignore) = m_mutable_table.insert(
					std::make_pair(target, to_add));

//				fprintf(stderr, "added mutable item (%d)\n", int(m_mutable_table.size()));
			}
			else
			{
				dht_mutable_item* item = &i->second;

				if (item->seq > msg_keys[2]->int_value())
				{
					incoming_error(e, "old sequence number");
					return;
				}

				if (item->seq < msg_keys[2]->int_value())
				{
					if (item->size != buf.second)
					{
						free(item->value);
						item->value = (char*)malloc(buf.second);
						item->size = buf.second;
					}
					item->seq = msg_keys[2]->int_value();
					memcpy(item->sig, msg_keys[4]->string_ptr(), sizeof(item->sig));
					TORRENT_ASSERT(sizeof(item->sig) == msg_keys[4]->string_length());
					memcpy(item->value, buf.first, buf.second);
				}
			}

			f = &i->second;
		}

		m_table.node_seen(id, m.addr);

		f->last_seen = time_now();

		// maybe increase num_announcers if we haven't seen this IP before
		sha1_hash iphash;
		hash_address(m.addr.address(), iphash);
		if (!f->ips.find(iphash))
		{
			f->ips.set(iphash);
			++f->num_announcers;
		}
	}
	else if (strcmp(query, "get") == 0)
	{
		key_desc_t msg_desc[] = {
			{"target", lazy_entry::string_t, 20, 0},
		};

		// k is not used for now

		// attempt to parse the message
		lazy_entry const* msg_keys[1];
		if (!verify_message(arg_ent, msg_desc, msg_keys, 1, error_string, sizeof(error_string)))
		{
			incoming_error(e, error_string);
			return;
		}

		sha1_hash target(msg_keys[0]->string_ptr());

//		fprintf(stderr, "%s GET target: %s\n"
//			, msg_keys[1] ? "mutable":"immutable"
//			, to_hex(target.to_string()).c_str());

		reply["token"] = generate_token(m.addr, msg_keys[0]->string_ptr());
		
		nodes_t n;
		// always return nodes as well as peers
		m_table.find_node(target, n, 0);
		write_nodes_entry(reply, n);

		dht_immutable_table_t::iterator i = m_immutable_table.find(target);
		if (i != m_immutable_table.end())
		{
			dht_immutable_item const& f = i->second;
			reply["v"] = bdecode(f.value, f.value + f.size);
		}
		else
		{
			dht_mutable_table_t::iterator i = m_mutable_table.find(target);
			if (i != m_mutable_table.end())
			{
				dht_mutable_item const& f = i->second;
				reply["v"] = bdecode(f.value, f.value + f.size);
				reply["seq"] = f.seq;
				reply["sig"] = std::string(f.sig, f.sig + 256);
				reply["k"] = std::string(f.key.bytes, f.key.bytes + sizeof(f.key.bytes));
			}
		}
	}
	else
	{
		// if we don't recognize the message but there's a
		// 'target' or 'info_hash' in the arguments, treat it
		// as find_node to be future compatible
		lazy_entry const* target_ent = arg_ent->dict_find_string("target");
		if (target_ent == 0 || target_ent->string_length() != 20)
		{
			target_ent = arg_ent->dict_find_string("info_hash");
			if (target_ent == 0 || target_ent->string_length() != 20)
			{
				incoming_error(e, "unknown message");
				return;
			}
		}

		sha1_hash target(target_ent->string_ptr());
		nodes_t n;
		// always return nodes as well as peers
		m_table.find_node(target, n, 0);
		write_nodes_entry(reply, n);
		return;
	}
}


} } // namespace libtorrent::dht

