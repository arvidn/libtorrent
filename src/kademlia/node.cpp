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

#include <utility>
#include <cinttypes> // for PRId64 et.al.
#include <functional>
#include <tuple>
#include <array>

#ifndef TORRENT_DISABLE_LOGGING
#include "libtorrent/hex.hpp" // to_hex
#endif

#include <libtorrent/socket_io.hpp>
#include <libtorrent/session_status.hpp>
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/random.hpp"
#include <libtorrent/assert.hpp>
#include <libtorrent/aux_/time.hpp>
#include "libtorrent/alert_types.hpp" // for dht_lookup
#include "libtorrent/performance_counters.hpp" // for counters

#include "libtorrent/kademlia/node.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/kademlia/direct_request.hpp"
#include "libtorrent/kademlia/io.hpp"

#include "libtorrent/kademlia/refresh.hpp"
#include "libtorrent/kademlia/get_peers.hpp"
#include "libtorrent/kademlia/get_item.hpp"
#include "libtorrent/kademlia/msg.hpp"
#include <libtorrent/kademlia/put_data.hpp>

using namespace std::placeholders;

namespace libtorrent { namespace dht
{

namespace {

void nop() {}

node_id calculate_node_id(node_id const& nid, dht_observer* observer, udp protocol)
{
	address external_address;
	if (observer != nullptr) external_address = observer->external_address(protocol);

	// if we don't have an observer, don't pretend that external_address is valid
	// generating an ID based on 0.0.0.0 would be terrible. random is better
	if (observer == nullptr || external_address.is_unspecified())
	{
		return generate_random_id();
	}

	if (nid == node_id::min() || !verify_id(nid, external_address))
		return generate_id(external_address);

	return nid;
}

// generate an error response message
void incoming_error(entry& e, char const* msg, int error_code = 203)
{
	e["y"] = "e";
	entry::list_type& l = e["e"].list();
	l.push_back(entry(error_code));
	l.push_back(entry(msg));
}

} // anonymous namespace

node::node(udp proto, udp_socket_interface* sock
	, dht_settings const& settings
	, node_id const& nid
	, dht_observer* observer
	, counters& cnt
	, std::map<std::string, node*> const& nodes
	, dht_storage_interface& storage)
	: m_settings(settings)
	, m_id(calculate_node_id(nid, observer, proto))
	, m_table(m_id, proto, 8, settings, observer)
	, m_rpc(m_id, m_settings, m_table, sock, observer)
	, m_nodes(nodes)
	, m_observer(observer)
	, m_protocol(map_protocol_to_descriptor(proto))
	, m_last_tracker_tick(aux::time_now())
	, m_last_self_refresh(min_time())
	, m_sock(sock)
	, m_counters(cnt)
	, m_storage(storage)
{
	m_secret[0] = random(0xffffffff);
	m_secret[1] = random(0xffffffff);
}

node::~node() = default;

void node::update_node_id()
{
	// if we don't have an observer, we can't ask for the external IP (and our
	// current node ID is likely not generated from an external address), so we
	// can just stop here in that case.
	if (m_observer == nullptr) return;

	// it's possible that our external address hasn't actually changed. If our
	// current ID is still valid, don't do anything.
	if (verify_id(m_id, m_observer->external_address(protocol())))
		return;

#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer != nullptr) m_observer->log(dht_logger::node
		, "updating node ID (because external IP address changed)");
#endif

	m_id = generate_id(m_observer->external_address(protocol()));

	m_table.update_node_id(m_id);
	m_rpc.update_node_id(m_id);
}

bool node::verify_token(string_view token, sha1_hash const& info_hash
	, udp::endpoint const& addr) const
{
	if (token.length() != 4)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (m_observer != nullptr)
		{
			m_observer->log(dht_logger::node, "token of incorrect length: %d"
				, int(token.length()));
		}
#endif
		return false;
	}

	hasher h1;
	error_code ec;
	std::string const address = addr.address().to_string(ec);
	if (ec) return false;
	h1.update(address);
	h1.update(reinterpret_cast<char const*>(&m_secret[0]), sizeof(m_secret[0]));
	h1.update(info_hash);

	sha1_hash h = h1.final();
	if (std::equal(token.begin(), token.end(), reinterpret_cast<char*>(&h[0])))
		return true;

	hasher h2;
	h2.update(address);
	h2.update(reinterpret_cast<char const*>(&m_secret[1]), sizeof(m_secret[1]));
	h2.update(info_hash);
	h = h2.final();
	return std::equal(token.begin(), token.end(), reinterpret_cast<char*>(&h[0]));
}

std::string node::generate_token(udp::endpoint const& addr
	, sha1_hash const& info_hash)
{
	std::string token;
	token.resize(4);
	hasher h;
	error_code ec;
	std::string const address = addr.address().to_string(ec);
	TORRENT_ASSERT(!ec);
	h.update(address);
	h.update(reinterpret_cast<char*>(&m_secret[0]), sizeof(m_secret[0]));
	h.update(info_hash);

	sha1_hash const hash = h.final();
	std::copy(hash.begin(), hash.begin() + 4, token.begin());
	TORRENT_ASSERT(std::equal(token.begin(), token.end(), hash.data()));
	return token;
}

void node::bootstrap(std::vector<udp::endpoint> const& nodes
	, find_data::nodes_callback const& f)
{
	node_id target = m_id;
	make_id_secret(target);

	auto r = std::make_shared<dht::bootstrap>(*this, target, f);
	m_last_self_refresh = aux::time_now();

#ifndef TORRENT_DISABLE_LOGGING
	int count = 0;
#endif

	for (auto const& n : nodes)
	{
		if (n.protocol() != protocol()) continue;
#ifndef TORRENT_DISABLE_LOGGING
		++count;
#endif
		r->add_entry(node_id(), n, observer::flag_initial);
	}

	// make us start as far away from our node ID as possible
	r->trim_seed_nodes();

#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer != nullptr)
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
	m_secret[0] = random(0xffffffff);
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
//		m_sock.send_packet(e, m.addr);
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
		std::memcpy(&b[0], ext_ip.string_ptr(), 16);
		if (m_observer != nullptr)
			m_observer->set_external_address(address_v6(b)
				, m.addr.address());
	} else
#endif
	if (ext_ip && ext_ip.string_length() >= 4)
	{
		address_v4::bytes_type b;
		std::memcpy(&b[0], ext_ip.string_ptr(), 4);
		if (m_observer != nullptr)
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

			if (!native_address(m.addr)) break;

			if (!m_sock->has_quota())
			{
				m_counters.inc_stats_counter(counters::dht_messages_in_dropped);
				return;
			}

			entry e;
			incoming_request(m, e);
			m_sock->send_packet(e, m.addr);
			break;
		}
		case 'e':
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (m_observer != nullptr && m_observer->should_log(dht_logger::node))
			{
				bdecode_node err = m.message.dict_find_list("e");
				if (err && err.list_size() >= 2
					&& err.list_at(0).type() == bdecode_node::int_t
					&& err.list_at(1).type() == bdecode_node::string_t)
				{
					m_observer->log(dht_logger::node, "INCOMING ERROR: (%" PRId64 ") %s"
						, err.list_int_value_at(0)
						, err.list_string_value_at(1).to_string().c_str());
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
	void announce_fun(std::vector<std::pair<node_entry, std::string>> const& v
		, node& node, int const listen_port, sha1_hash const& ih, int const flags)
	{
#ifndef TORRENT_DISABLE_LOGGING
		auto logger = node.observer();
		if (logger != nullptr && logger->should_log(dht_logger::node))
		{
			logger->log(dht_logger::node, "sending announce_peer [ ih: %s "
				" p: %d nodes: %d ]", aux::to_hex(ih).c_str(), listen_port, int(v.size()));
		}
#endif

		// create a dummy traversal_algorithm
		auto algo = std::make_shared<traversal_algorithm>(node, node_id());
		// store on the first k nodes
		for (auto const& p : v)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (logger != nullptr && logger->should_log(dht_logger::node))
			{
				logger->log(dht_logger::node, "announce-distance: %d"
					, (160 - distance_exp(ih, p.first.id)));
			}
#endif

			auto o = node.m_rpc.allocate_observer<announce_observer>(algo
				, p.first.ep(), p.first.id);
			if (!o) return;
#if TORRENT_USE_ASSERTS
			o->m_in_constructor = false;
#endif
			entry e;
			e["y"] = "q";
			e["q"] = "announce_peer";
			entry& a = e["a"];
			a["info_hash"] = ih;
			a["port"] = listen_port;
			a["token"] = p.second;
			a["seed"] = (flags & node::flag_seed) ? 1 : 0;
			if (flags & node::flag_implied_port) a["implied_port"] = 1;
			node.stats_counters().inc_stats_counter(counters::dht_announce_peer_out);
			node.m_rpc.invoke(e, p.first.ep(), o);
		}
	}
}

void node::add_router_node(udp::endpoint const& router)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer != nullptr && m_observer->should_log(dht_logger::node))
	{
		m_observer->log(dht_logger::node, "adding router node: %s"
			, print_endpoint(router).c_str());
	}
#endif
	m_table.add_router_node(router);
}

void node::add_node(udp::endpoint const& node)
{
	if (!native_address(node)) return;
	// ping the node, and if we get a reply, it
	// will be added to the routing table
	send_single_refresh(node, m_table.num_active_buckets());
}

void node::get_peers(sha1_hash const& info_hash
	, std::function<void(std::vector<tcp::endpoint> const&)> dcallback
	, std::function<void(std::vector<std::pair<node_entry, std::string>> const&)> ncallback
	, bool noseeds)
{
	// search for nodes with ids close to id or with peers
	// for info-hash id. then send announce_peer to them.

	auto ta = m_settings.privacy_lookups
		? std::make_shared<dht::obfuscated_get_peers>(*this, info_hash, dcallback, ncallback, noseeds)
		: std::make_shared<dht::get_peers>(*this, info_hash, dcallback, ncallback, noseeds);

	ta->start();
}

void node::announce(sha1_hash const& info_hash, int const listen_port, int const flags
	, std::function<void(std::vector<tcp::endpoint> const&)> f)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer != nullptr && m_observer->should_log(dht_logger::node))
	{
		m_observer->log(dht_logger::node, "announcing [ ih: %s p: %d ]"
			, aux::to_hex(info_hash).c_str(), listen_port);
	}
#endif

	get_peers(info_hash, f
		, std::bind(&announce_fun, _1, std::ref(*this)
		, listen_port, info_hash, flags), flags & node::flag_seed);
}

void node::direct_request(udp::endpoint const& ep, entry& e
	, std::function<void(msg const&)> f)
{
	// not really a traversal
	auto algo = std::make_shared<direct_traversal>(*this, node_id(), f);

	auto o = m_rpc.allocate_observer<direct_observer>(algo, ep, node_id());
	if (!o) return;
#if TORRENT_USE_ASSERTS
	o->m_in_constructor = false;
#endif
	m_rpc.invoke(e, ep, o);
}

void node::get_item(sha1_hash const& target
	, std::function<void(item const&)> f)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer != nullptr && m_observer->should_log(dht_logger::node))
	{
		m_observer->log(dht_logger::node, "starting get for [ hash: %s ]"
			, aux::to_hex(target).c_str());
	}
#endif

	auto ta = std::make_shared<dht::get_item>(*this, target
		, std::bind(f, _1), find_data::nodes_callback());
	ta->start();
}

void node::get_item(public_key const& pk, std::string const& salt
	, std::function<void(item const&, bool)> f)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer != nullptr && m_observer->should_log(dht_logger::node))
	{
		char hex_key[65];
		aux::to_hex(pk.bytes, hex_key);
		m_observer->log(dht_logger::node, "starting get for [ key: %s ]", hex_key);
	}
#endif

	auto ta = std::make_shared<dht::get_item>(*this, pk, salt, f
		, find_data::nodes_callback());
	ta->start();
}

namespace {

void put(std::vector<std::pair<node_entry, std::string>> const& nodes
	, std::shared_ptr<put_data> ta)
{
	ta->set_targets(nodes);
	ta->start();
}

void put_data_cb(item i, bool auth
	, std::shared_ptr<put_data> ta
	, std::function<void(item&)> f)
{
	// call data_callback only when we got authoritative data.
	if (auth)
	{
		f(i);
		ta->set_data(i);
	}
}

} // namespace

void node::put_item(sha1_hash const& target, entry const& data, std::function<void(int)> f)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer != nullptr && m_observer->should_log(dht_logger::node))
	{
		m_observer->log(dht_logger::node, "starting get for [ hash: %s ]"
			, aux::to_hex(target).c_str());
	}
#endif

	item i;
	i.assign(data);
	auto put_ta = std::make_shared<dht::put_data>(*this, std::bind(f, _2));
	put_ta->set_data(i);

	auto ta = std::make_shared<dht::get_item>(*this, target
		, get_item::data_callback(), std::bind(&put, _1, put_ta));
	ta->start();
}

void node::put_item(public_key const& pk, std::string const& salt
	, std::function<void(item const&, int)> f
	, std::function<void(item&)> data_cb)
{
#ifndef TORRENT_DISABLE_LOGGING
	if (m_observer != nullptr && m_observer->should_log(dht_logger::node))
	{
		char hex_key[65];
		aux::to_hex(pk.bytes, hex_key);
		m_observer->log(dht_logger::node, "starting get for [ key: %s ]", hex_key);
	}
#endif

	auto put_ta = std::make_shared<dht::put_data>(*this, f);

	auto ta = std::make_shared<dht::get_item>(*this, pk, salt
		, std::bind(&put_data_cb, _1, _2, put_ta, data_cb)
		, std::bind(&put, _1, put_ta));
	ta->start();
}

struct ping_observer : observer
{
	ping_observer(
		std::shared_ptr<traversal_algorithm> const& algorithm
		, udp::endpoint const& ep, node_id const& id)
		: observer(algorithm, ep, id)
	{}

	// parses out "nodes"
	void reply(msg const& m) override
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
		udp const protocol = algorithm()->get_node().protocol();
		int const protocol_size = int(detail::address_size(protocol));
		char const* nodes_key = algorithm()->get_node().protocol_nodes_key();
		bdecode_node n = r.dict_find_string(nodes_key);
		if (n)
		{
			char const* nodes = n.string_ptr();
			char const* end = nodes + n.string_length();

			while (end - nodes >= 20 + protocol_size + 2)
			{
				node_endpoint nep = read_node_endpoint(protocol, nodes);
				algorithm()->get_node().m_table.heard_about(nep.id, nep.ep);
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
	time_point const now = aux::time_now();
	if (m_last_self_refresh + minutes(10) < now && m_table.depth() < 4)
	{
		node_id target = m_id;
		make_id_secret(target);
		auto const r = std::make_shared<dht::bootstrap>(*this, target, std::bind(&nop));
		r->start();
		m_last_self_refresh = now;
		return;
	}

	node_entry const* ne = m_table.next_refresh();
	if (ne == nullptr) return;

	// this shouldn't happen
	TORRENT_ASSERT(m_id != ne->id);
	if (ne->id == m_id) return;

	int const bucket = 159 - distance_exp(m_id, ne->id);
	TORRENT_ASSERT(bucket < 160);
	send_single_refresh(ne->ep(), bucket, ne->id);
}

void node::send_single_refresh(udp::endpoint const& ep, int const bucket
	, node_id const& id)
{
	TORRENT_ASSERT(id != m_id);
	TORRENT_ASSERT(bucket >= 0);
	TORRENT_ASSERT(bucket <= 159);

	// generate a random node_id within the given bucket
	// TODO: 2 it would be nice to have a bias towards node-id prefixes that
	// are missing in the bucket
	node_id mask = generate_prefix_mask(bucket + 1);
	node_id target = generate_secret_id() & ~mask;
	target |= m_id & mask;

	// create a dummy traversal_algorithm
	auto const algo = std::make_shared<traversal_algorithm>(*this, node_id());
	auto o = m_rpc.allocate_observer<ping_observer>(algo, ep, id);
	if (!o) return;
#if TORRENT_USE_ASSERTS
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

	m_storage.tick();

	return d;
}

void node::status(std::vector<dht_routing_bucket>& table
	, std::vector<dht_lookup>& requests)
{
	std::lock_guard<std::mutex> l(m_mutex);

	m_table.status(table);

	for (auto const& r : m_running_requests)
	{
		requests.push_back(dht_lookup());
		dht_lookup& lookup = requests.back();
		r->status(lookup);
	}
}

std::tuple<int, int, int> node::get_stats_counters() const
{
	int nodes, replacements;
	std::tie(nodes, replacements, std::ignore) = size();
	return std::make_tuple(nodes, replacements, m_rpc.num_allocated_observers());
}

#ifndef TORRENT_NO_DEPRECATE
// TODO: 2 use the non deprecated function instead of this one
void node::status(session_status& s)
{
	std::lock_guard<std::mutex> l(m_mutex);

	m_table.status(s);
	s.dht_total_allocations += m_rpc.num_allocated_observers();
	for (std::set<traversal_algorithm*>::iterator i = m_running_requests.begin()
		, end(m_running_requests.end()); i != end; ++i)
	{
		s.active_requests.push_back(dht_lookup());
		dht_lookup& lookup = s.active_requests.back();
		(*i)->status(lookup);
	}
}
#endif

bool node::lookup_peers(sha1_hash const& info_hash, entry& reply
	, bool noseed, bool scrape, address const& requester) const
{
	if (m_observer)
		m_observer->get_peers(info_hash);

	return m_storage.get_peers(info_hash, noseed, scrape, requester, reply);
}

entry write_nodes_entry(std::vector<node_entry> const& nodes)
{
	entry r;
	std::back_insert_iterator<std::string> out(r.string());
	for (auto const& n : nodes)
	{
		std::copy(n.id.begin(), n.id.end(), out);
		detail::write_endpoint(udp::endpoint(n.addr(), std::uint16_t(n.port())), out);
	}
	return r;
}

// build response
void node::incoming_request(msg const& m, entry& e)
{
	e = entry(entry::dictionary_t);
	e["y"] = "r";
	e["t"] = m.message.dict_find_string_value("t").to_string();

	key_desc_t const top_desc[] = {
		{"q", bdecode_node::string_t, 0, 0},
		{"ro", bdecode_node::int_t, 0, key_desc_t::optional},
		{"a", bdecode_node::dict_t, 0, key_desc_t::parse_children},
			{"id", bdecode_node::string_t, 20, key_desc_t::last_child},
	};

	bdecode_node top_level[4];
	char error_string[200];
	if (!verify_message(m.message, top_desc, top_level, error_string))
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

	string_view query = top_level[0].string_value();

	if (m_observer && m_observer->on_dht_request(query, m, e))
		return;

	if (query == "ping")
	{
		m_counters.inc_stats_counter(counters::dht_ping_in);
		// we already have 't' and 'id' in the response
		// no more left to add
	}
	else if (query == "get_peers")
	{
		key_desc_t const msg_desc[] = {
			{"info_hash", bdecode_node::string_t, 20, 0},
			{"noseed", bdecode_node::int_t, 0, key_desc_t::optional},
			{"scrape", bdecode_node::int_t, 0, key_desc_t::optional},
			{"want", bdecode_node::list_t, 0, key_desc_t::optional},
		};

		bdecode_node msg_keys[4];
		if (!verify_message(arg_ent, msg_desc, msg_keys, error_string))
		{
			m_counters.inc_stats_counter(counters::dht_invalid_get_peers);
			incoming_error(e, error_string);
			return;
		}

		sha1_hash const info_hash(msg_keys[0].string_ptr());

		m_counters.inc_stats_counter(counters::dht_get_peers_in);

		// always return nodes as well as peers
		write_nodes_entries(info_hash, msg_keys[3], reply);

		bool noseed = false;
		bool scrape = false;
		if (msg_keys[1] && msg_keys[1].int_value() != 0) noseed = true;
		if (msg_keys[2] && msg_keys[2].int_value() != 0) scrape = true;
		// If our storage is full we want to withhold the write token so that
		// announces will spill over to our neighbors. This widens the
		// perimeter of nodes which store peers for this torrent
		bool full = lookup_peers(info_hash, reply, noseed, scrape, m.addr.address());
		if (!full) reply["token"] = generate_token(m.addr, info_hash);

#ifndef TORRENT_DISABLE_LOGGING
		if (reply.find_key("values") && m_observer)
		{
			m_observer->log(dht_logger::node, "values: %d"
				, int(reply["values"].list().size()));
		}
#endif
	}
	else if (query == "find_node")
	{
		key_desc_t const msg_desc[] = {
			{"target", bdecode_node::string_t, 20, 0},
			{"want", bdecode_node::list_t, 0, key_desc_t::optional},
		};

		bdecode_node msg_keys[2];
		if (!verify_message(arg_ent, msg_desc, msg_keys, error_string))
		{
			incoming_error(e, error_string);
			return;
		}

		m_counters.inc_stats_counter(counters::dht_find_node_in);
		sha1_hash target(msg_keys[0].string_ptr());

		write_nodes_entries(target, msg_keys[1], reply);
	}
	else if (query == "announce_peer")
	{
		key_desc_t const msg_desc[] = {
			{"info_hash", bdecode_node::string_t, 20, 0},
			{"port", bdecode_node::int_t, 0, 0},
			{"token", bdecode_node::string_t, 0, 0},
			{"n", bdecode_node::string_t, 0, key_desc_t::optional},
			{"seed", bdecode_node::int_t, 0, key_desc_t::optional},
			{"implied_port", bdecode_node::int_t, 0, key_desc_t::optional},
		};

		bdecode_node msg_keys[6];
		if (!verify_message(arg_ent, msg_desc, msg_keys, error_string))
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

		if (!verify_token(msg_keys[2].string_value()
			, sha1_hash(msg_keys[0].string_ptr()), m.addr))
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

		tcp::endpoint addr = tcp::endpoint(m.addr.address(), std::uint16_t(port));
		string_view name = msg_keys[3] ? msg_keys[3].string_value() : string_view();
		bool seed = msg_keys[4] && msg_keys[4].int_value();

		m_storage.announce_peer(info_hash, addr, name, seed);
	}
	else if (query == "put")
	{
		// the first 2 entries are for both mutable and
		// immutable puts
		static const key_desc_t msg_desc[] = {
			{"token", bdecode_node::string_t, 0, 0},
			{"v", bdecode_node::none_t, 0, 0},
			{"seq", bdecode_node::int_t, 0, key_desc_t::optional},
			// public key
			{"k", bdecode_node::string_t, public_key::len, key_desc_t::optional},
			{"sig", bdecode_node::string_t, signature::len, key_desc_t::optional},
			{"cas", bdecode_node::int_t, 0, key_desc_t::optional},
			{"salt", bdecode_node::string_t, 0, key_desc_t::optional},
		};

		// attempt to parse the message
		bdecode_node msg_keys[7];
		if (!verify_message(arg_ent, msg_desc, msg_keys, error_string))
		{
			m_counters.inc_stats_counter(counters::dht_invalid_put);
			incoming_error(e, error_string);
			return;
		}

		m_counters.inc_stats_counter(counters::dht_put_in);

		// is this a mutable put?
		bool mutable_put = (msg_keys[2] && msg_keys[3] && msg_keys[4]);

		// public key (only set if it's a mutable put)
		char const* pub_key = nullptr;
		if (msg_keys[3]) pub_key = msg_keys[3].string_ptr();

		// signature (only set if it's a mutable put)
		char const* sign = nullptr;
		if (msg_keys[4]) sign = msg_keys[4].string_ptr();

		// pointer and length to the whole entry
		span<char const> buf = msg_keys[1].data_section();
		if (buf.size() > 1000 || buf.size() <= 0)
		{
			m_counters.inc_stats_counter(counters::dht_invalid_put);
			incoming_error(e, "message too big", 205);
			return;
		}

		span<char const> salt;
		if (msg_keys[6])
			salt = { msg_keys[6].string_ptr(), size_t(msg_keys[6].string_length()) };
		if (salt.size() > 64)
		{
			m_counters.inc_stats_counter(counters::dht_invalid_put);
			incoming_error(e, "salt too big", 207);
			return;
		}

		sha1_hash const target = pub_key
			? item_target_id(salt, public_key(pub_key))
			: item_target_id(buf);

//		std::fprintf(stderr, "%s PUT target: %s salt: %s key: %s\n"
//			, mutable_put ? "mutable":"immutable"
//			, aux::to_hex(target).c_str()
//			, salt.second > 0 ? std::string(salt.first, salt.second).c_str() : ""
//			, pk ? aux::to_hex(pk).c_str() : "");

		// verify the write-token. tokens are only valid to write to
		// specific target hashes. it must match the one we got a "get" for
		if (!verify_token(msg_keys[0].string_value(), target, m.addr))
		{
			m_counters.inc_stats_counter(counters::dht_invalid_put);
			incoming_error(e, "invalid token");
			return;
		}

		if (!mutable_put)
		{
			m_storage.put_immutable_item(target, buf, m.addr.address());
		}
		else
		{
			// mutable put, we must verify the signature
			sequence_number const seq(msg_keys[2].int_value());
			public_key const pk(pub_key);
			signature const sig(sign);

			if (seq < sequence_number(0))
			{
				m_counters.inc_stats_counter(counters::dht_invalid_put);
				incoming_error(e, "invalid (negative) sequence number");
				return;
			}

			// msg_keys[4] is the signature, msg_keys[3] is the public key
			if (!verify_mutable_item(buf, salt, seq, pk, sig))
			{
				m_counters.inc_stats_counter(counters::dht_invalid_put);
				incoming_error(e, "invalid signature", 206);
				return;
			}

			TORRENT_ASSERT(signature::len == msg_keys[4].string_length());

			sequence_number item_seq;
			if (!m_storage.get_mutable_item_seq(target, item_seq))
			{
				m_storage.put_mutable_item(target, buf, sig, seq, pk, salt
					, m.addr.address());
			}
			else
			{
				// this is the "cas" field in the put message
				// if it was specified, we MUST make sure the current sequence
				// number matches the expected value before replacing it
				// this is critical for avoiding race conditions when multiple
				// writers are accessing the same slot
				if (msg_keys[5] && item_seq.value != msg_keys[5].int_value())
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

				m_storage.put_mutable_item(target, buf, sig, seq, pk, salt
					, m.addr.address());
			}
		}

		m_table.node_seen(id, m.addr, 0xffff);
	}
	else if (query == "get")
	{
		key_desc_t msg_desc[] = {
			{"seq", bdecode_node::int_t, 0, key_desc_t::optional},
			{"target", bdecode_node::string_t, 20, 0},
			{"want", bdecode_node::list_t, 0, key_desc_t::optional},
		};

		// k is not used for now

		// attempt to parse the message
		bdecode_node msg_keys[3];
		if (!verify_message(arg_ent, msg_desc, msg_keys, error_string))
		{
			m_counters.inc_stats_counter(counters::dht_invalid_get);
			incoming_error(e, error_string);
			return;
		}

		m_counters.inc_stats_counter(counters::dht_get_in);
		sha1_hash target(msg_keys[1].string_ptr());

//		std::fprintf(stderr, "%s GET target: %s\n"
//			, msg_keys[1] ? "mutable":"immutable"
//			, aux::to_hex(target).c_str());

		reply["token"] = generate_token(m.addr, sha1_hash(msg_keys[1].string_ptr()));

		// always return nodes as well as peers
		write_nodes_entries(target, msg_keys[2], reply);

		// if the get has a sequence number it must be for a mutable item
		// so don't bother searching the immutable table
		if (!msg_keys[0])
		{
			if (!m_storage.get_immutable_item(target, reply)) // ok, check for a mutable one
			{
				m_storage.get_mutable_item(target, sequence_number(0)
					, true, reply);
			}
		}
		else
		{
			m_storage.get_mutable_item(target
				, sequence_number(msg_keys[0].int_value()), false
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
		// always return nodes as well as peers
		write_nodes_entries(target, arg_ent.dict_find_list("want"), reply);
		return;
	}
}

// TODO: limit number of entries in the result
void node::write_nodes_entries(sha1_hash const& info_hash
	, bdecode_node const& want, entry& r)
{
	// if no wants entry was specified, include a nodes
	// entry based on the protocol the request came in with
	if (want.type() != bdecode_node::list_t)
	{
		std::vector<node_entry> n;
		m_table.find_node(info_hash, n, 0);
		r[protocol_nodes_key()] = write_nodes_entry(n);
		return;
	}

	// if there is a wants entry then we may need to reach into
	// another node's routing table to get nodes of the requested type
	// we use a map maintained by the owning dht_tracker to find the
	// node associated with each string in the want list, which may
	// include this node
	for (int i = 0; i < want.list_size(); ++i)
	{
		bdecode_node wanted = want.list_at(i);
		if (wanted.type() != bdecode_node::string_t)
			continue;
		auto wanted_node = m_nodes.find(wanted.string_value().to_string());
		if (wanted_node == m_nodes.end()) continue;
		std::vector<node_entry> n;
		wanted_node->second->m_table.find_node(info_hash, n, 0);
		r[wanted_node->second->protocol_nodes_key()] = write_nodes_entry(n);
	}
}

node::protocol_descriptor const& node::map_protocol_to_descriptor(udp protocol)
{
	static std::array<protocol_descriptor, 2> descriptors =
	{{
		{udp::v4(), "n4", "nodes"},
		{udp::v6(), "n6", "nodes6"}
	}};

	for (auto const& d : descriptors)
	{
		if (d.protocol == protocol)
			return d;
	}

	TORRENT_ASSERT_FAIL();
#ifndef BOOST_NO_EXCEPTIONS
	throw std::out_of_range("unknown protocol");
#else
	std::terminate();
#endif
}

} } // namespace libtorrent::dht
