/*

Copyright (c) 2009-2020, Arvid Norberg
Copyright (c) 2015, Thomas Yuan
Copyright (c) 2015-2019, Steven Siloti
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2020, Fonic
Copyright (c) 2020, FranciscoPombal
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

#include "test.hpp"

#ifndef TORRENT_DISABLE_DHT

#include "libtorrent/config.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/kademlia/msg.hpp" // for verify_message
#include "libtorrent/kademlia/node.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/socket_io.hpp" // for hash_address
#include "libtorrent/aux_/ip_helpers.hpp"
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/random.hpp"
#include "libtorrent/kademlia/ed25519.hpp"
#include "libtorrent/hex.hpp" // to_hex, from_hex
#include "libtorrent/bloom_filter.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"
#include "libtorrent/aux_/session_impl.hpp"

#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/routing_table.hpp"
#include "libtorrent/kademlia/item.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"

#include <numeric>
#include <cstdarg>
#include <tuple>
#include <iostream>
#include <cstdio> // for vsnprintf

#include "setup_transfer.hpp"

using namespace lt;
using namespace lt::dht;
using namespace std::placeholders;

namespace {

void get_test_keypair(public_key& pk, secret_key& sk)
{
	aux::from_hex({"77ff84905a91936367c01360803104f92432fcd904a43511876df5cdf3e7e548", 64}
		, pk.bytes.data());
	aux::from_hex({"e06d3183d14159228433ed599221b80bd0a5ce8352e4bdf0262f76786ef1c74d"
		"b7e7a9fea2c0eb269d61e3b38e450a22e754941ac78479d6c54e1faf6037881d", 128}
		, sk.bytes.data());
}

sequence_number prev_seq(sequence_number s)
{
	return sequence_number(s.value - 1);
}

sequence_number next_seq(sequence_number s)
{
	return sequence_number(s.value + 1);
}

void add_and_replace(node_id& dst, node_id const& add)
{
	bool carry = false;
	for (int k = 19; k >= 0; --k)
	{
		int sum = dst[k] + add[k] + (carry ? 1 : 0);
		dst[k] = sum & 255;
		carry = sum > 255;
	}
}

void node_push_back(std::vector<node_entry>* nv, node_entry const& n)
{
	nv->push_back(n);
}

void nop_node() {}

// TODO: 3 make the mock_socket hold a reference to the list of where to record
// packets instead of having a global variable
std::list<std::pair<udp::endpoint, entry>> g_sent_packets;

struct mock_socket final : socket_manager
{
	bool has_quota() override { return true; }
	bool send_packet(aux::listen_socket_handle const&, entry& msg, udp::endpoint const& ep) override
	{
		// TODO: 3 ideally the mock_socket would contain this queue of packets, to
		// make tests independent
		g_sent_packets.push_back(std::make_pair(ep, msg));
		return true;
	}
};

std::shared_ptr<aux::listen_socket_t> dummy_listen_socket(udp::endpoint src)
{
	auto ret = std::make_shared<aux::listen_socket_t>();
	ret->local_endpoint = tcp::endpoint(src.address(), src.port());
	ret->external_address.cast_vote(src.address()
		, aux::session_interface::source_dht, rand_v4());
	return ret;
}

std::shared_ptr<aux::listen_socket_t> dummy_listen_socket4()
{
	auto ret = std::make_shared<aux::listen_socket_t>();
	ret->local_endpoint = tcp::endpoint(addr4("192.168.4.1"), 6881);
	ret->external_address.cast_vote(addr4("236.0.0.1")
		, aux::session_interface::source_dht, rand_v4());
	return ret;
}

std::shared_ptr<aux::listen_socket_t> dummy_listen_socket6()
{
	auto ret = std::make_shared<aux::listen_socket_t>();
	ret->local_endpoint = tcp::endpoint(addr6("2002::1"), 6881);
	ret->external_address.cast_vote(addr6("2002::1")
		, aux::session_interface::source_dht, rand_v6());
	return ret;
}

node* get_foreign_node_stub(node_id const&, std::string const&)
{
	return nullptr;
}

sha1_hash generate_next()
{
	sha1_hash ret;
	aux::random_bytes(ret);
	return ret;
}

std::list<std::pair<udp::endpoint, entry>>::iterator
find_packet(udp::endpoint ep)
{
	return std::find_if(g_sent_packets.begin(), g_sent_packets.end()
		, [&ep] (std::pair<udp::endpoint, entry> const& p)
		{ return p.first == ep; });
}

void node_from_entry(entry const& e, bdecode_node& l)
{
	error_code ec;
	static char inbuf[1500];
	int len = bencode(inbuf, e);
	int ret = bdecode(inbuf, inbuf + len, l, ec);
	TEST_CHECK(ret == 0);
}

entry write_peers(std::set<tcp::endpoint> const& peers)
{
	entry r;
	entry::list_type& pe = r.list();
	for (auto const& p : peers)
	{
		std::string endpoint(18, '\0');
		std::string::iterator out = endpoint.begin();
		lt::aux::write_endpoint(p, out);
		endpoint.resize(std::size_t(out - endpoint.begin()));
		pe.push_back(entry(endpoint));
	}
	return r;
}

struct msg_args
{
	msg_args& info_hash(char const* i)
	{ if (i) a["info_hash"] = std::string(i, 20); return *this; }

	msg_args& name(char const* n)
	{ if (n) a["n"] = n; return *this; }

	msg_args& token(std::string t)
	{ a["token"] = t; return *this; }

	msg_args& port(int p)
	{ a["port"] = p; return *this; }

	msg_args& target(sha1_hash const& t)
	{ a["target"] = t.to_string(); return *this; }

	msg_args& value(entry const& v)
	{ a["v"] = v; return *this; }

	msg_args& scrape(bool s)
	{ a["scrape"] = s ? 1 : 0; return *this; }

	msg_args& seed(bool s)
	{ a["seed"] = s ? 1 : 0; return *this; }

	msg_args& key(public_key const& k)
	{ a["k"] = k.bytes; return *this; }

	msg_args& sig(signature const& s)
	{ a["sig"] = s.bytes; return *this; }

	msg_args& seq(sequence_number s)
	{ a["seq"] = s.value; return *this; }

	msg_args& cas(sequence_number c)
	{ a["cas"] = c.value; return *this; }

	msg_args& nid(sha1_hash const& n)
	{ a["id"] = n.to_string(); return *this; }

	msg_args& salt(span<char const> s)
	{ if (!s.empty()) a["salt"] = s; return *this; }

	msg_args& want(std::string w)
	{ a["want"].list().push_back(w); return *this; }

	msg_args& nodes(std::vector<node_entry> const& n)
	{ if (!n.empty()) a["nodes"] = dht::write_nodes_entry(n); return *this; }

	msg_args& nodes6(std::vector<node_entry> const& n)
	{ if (!n.empty()) a["nodes6"] = dht::write_nodes_entry(n); return *this; }

	msg_args& peers(std::set<tcp::endpoint> const& p)
	{ if (!p.empty()) a.dict()["values"] = write_peers(p); return *this; }

	msg_args& interval(time_duration interval)
	{ a["interval"] = total_seconds(interval); return *this; }

	msg_args& num(int num)
	{ a["num"] = num; return *this; }

	msg_args& samples(std::vector<sha1_hash> const& samples)
	{
		a["samples"] = span<char const>(
			reinterpret_cast<char const*>(samples.data()), int(samples.size()) * 20);
		return *this;
	}

	entry a;
};

void send_dht_request(node& node, char const* msg, udp::endpoint const& ep
	, bdecode_node* reply, msg_args const& args = msg_args()
	, char const* t = "10", bool has_response = true)
{
	// we're about to clear out the backing buffer
	// for this bdecode_node, so we better clear it now
	reply->clear();
	entry e;
	e["q"] = msg;
	e["t"] = t;
	e["y"] = "q";
	e["a"] = args.a;
	e["a"].dict().insert(std::make_pair("id", generate_next().to_string()));
	char msg_buf[1500];
	int size = bencode(msg_buf, e);

	bdecode_node decoded;
	error_code ec;
	bdecode(msg_buf, msg_buf + size, decoded, ec);
	if (ec) std::printf("bdecode failed: %s\n", ec.message().c_str());

	dht::msg m(decoded, ep);
	node.incoming(node.m_sock, m);

	// If the request is supposed to get a response, by now the node should have
	// invoked the send function and put the response in g_sent_packets
	auto const i = find_packet(ep);
	if (has_response)
	{
		if (i == g_sent_packets.end())
		{
			TEST_ERROR("not response from DHT node");
			return;
		}

		node_from_entry(i->second, *reply);
		g_sent_packets.erase(i);

		return;
	}

	// this request suppose won't be responsed.
	if (i != g_sent_packets.end())
	{
		TEST_ERROR("shouldn't have response from DHT node");
		return;
	}
}

void send_dht_response(node& node, bdecode_node const& request, udp::endpoint const& ep
	, msg_args const& args = msg_args())
{
	entry e;
	e["y"] = "r";
	e["t"] = request.dict_find_string_value("t").to_string();
//	e["ip"] = endpoint_to_bytes(ep);
	e["r"] = args.a;
	e["r"].dict().insert(std::make_pair("id", generate_next().to_string()));
	char msg_buf[1500];
	int const size = bencode(msg_buf, e);

	bdecode_node decoded;
	error_code ec;
	bdecode(msg_buf, msg_buf + size, decoded, ec);
	if (ec) std::printf("bdecode failed: %s\n", ec.message().c_str());

	dht::msg m(decoded, ep);
	node.incoming(node.m_sock, m);
}

struct announce_item
{
	announce_item(sha1_hash nxt, int const num)
		: next(nxt)
		, num_peers(num)
	{
		num_peers = int(lt::random(5) + 2);
		ent["next"] = next.to_string();
		ent["A"] = "a";
		ent["B"] = "b";
		ent["num_peers"] = num_peers;

		char buf[512];
		char* ptr = buf;
		int len = bencode(ptr, ent);
		target = hasher(buf, len).final();
	}
	sha1_hash next;
	int num_peers;
	entry ent;
	sha1_hash target;
};

void announce_immutable_items(node& node, udp::endpoint const* eps
	, announce_item const* items, int num_items)
{
	std::string token;
	for (int i = 0; i < 1000; ++i)
	{
		for (int j = 0; j < num_items; ++j)
		{
			if ((i % items[j].num_peers) == 0) continue;
			bdecode_node response;
			send_dht_request(node, "get", eps[i], &response
				, msg_args().target(items[j].target));

			key_desc_t const desc[] =
			{
				{ "r", bdecode_node::dict_t, 0, key_desc_t::parse_children },
					{ "id", bdecode_node::string_t, 20, 0},
					{ "token", bdecode_node::string_t, 0, 0},
					{ "ip", bdecode_node::string_t, 0, key_desc_t::optional | key_desc_t::last_child},
				{ "y", bdecode_node::string_t, 1, 0},
			};

			bdecode_node parsed[5];
			char error_string[200];

//			std::printf("msg: %s\n", print_entry(response).c_str());
			int ret = verify_message(response, desc, parsed, error_string);
			if (ret)
			{
				TEST_EQUAL(parsed[4].string_value(), "r");
				token = parsed[2].string_value().to_string();
//				std::printf("got token: %s\n", token.c_str());
			}
			else
			{
				std::printf("msg: %s\n", print_entry(response).c_str());
				std::printf("   invalid get response: %s\n", error_string);
				TEST_ERROR(error_string);
			}

			if (parsed[3])
			{
				address_v4::bytes_type b;
				memcpy(&b[0], parsed[3].string_ptr(), b.size());
				address_v4 addr(b);
				TEST_EQUAL(addr, eps[i].address());
			}

			send_dht_request(node, "put", eps[i], &response
				, msg_args()
					.token(token)
					.target(items[j].target)
					.value(items[j].ent));

			key_desc_t const desc2[] =
			{
				{ "y", bdecode_node::string_t, 1, 0 }
			};

			bdecode_node parsed2[1];
			ret = verify_message(response, desc2, parsed2, error_string);
			if (ret)
			{
				if (parsed2[0].string_value() != "r")
					std::printf("msg: %s\n", print_entry(response).c_str());

				TEST_EQUAL(parsed2[0].string_value(), "r");
			}
			else
			{
				std::printf("msg: %s\n", print_entry(response).c_str());
				std::printf("   invalid put response: %s\n", error_string);
				TEST_ERROR(error_string);
			}
		}
	}

	std::set<int> items_num;
	for (int j = 0; j < num_items; ++j)
	{
		bdecode_node response;
		send_dht_request(node, "get", eps[j], &response
			, msg_args().target(items[j].target));

		key_desc_t const desc[] =
		{
			{ "r", bdecode_node::dict_t, 0, key_desc_t::parse_children },
				{ "v", bdecode_node::dict_t, 0, 0},
				{ "id", bdecode_node::string_t, 20, key_desc_t::last_child},
			{ "y", bdecode_node::string_t, 1, 0},
		};

		bdecode_node parsed[4];
		char error_string[200];

		int ret = verify_message(response, desc, parsed, error_string);
		if (ret)
		{
			items_num.insert(items_num.begin(), j);
		}
	}

	// TODO: check to make sure the "best" items are stored
	TEST_EQUAL(items_num.size(), 4);
}

int sum_distance_exp(int s, node_entry const& e, node_id const& ref)
{
	return s + distance_exp(e.id, ref);
}

std::vector<tcp::endpoint> g_got_peers;

void get_peers_cb(std::vector<tcp::endpoint> const& peers)
{
	g_got_peers.insert(g_got_peers.end(), peers.begin(), peers.end());
}

std::vector<dht::item> g_got_items;
dht::item g_put_item;
int g_put_count;

void get_mutable_item_cb(dht::item const& i, bool a)
{
	if (!a) return;
	if (!i.empty())
		g_got_items.push_back(i);
}

void put_mutable_item_data_cb(dht::item& i)
{
	if (!i.empty())
		g_got_items.push_back(i);

	TEST_CHECK(!g_put_item.empty());
	i = g_put_item;
	g_put_count++;
}

void put_mutable_item_cb(dht::item const&, int num, int expect)
{
	TEST_EQUAL(num, expect);
}

void get_immutable_item_cb(dht::item const& i)
{
	if (!i.empty())
		g_got_items.push_back(i);
}

void put_immutable_item_cb(int num, int expect)
{
	TEST_EQUAL(num, expect);
}

struct obs : dht::dht_observer
{
	void set_external_address(aux::listen_socket_handle const& s, address const& addr
		, address const& /*source*/) override
	{
		s.get()->external_address.cast_vote(addr
			, aux::session_interface::source_dht, rand_v4());
	}

	int get_listen_port(aux::transport, aux::listen_socket_handle const& s) override
	{ return s.get()->udp_external_port(); }

	void get_peers(sha1_hash const&) override {}
	void outgoing_get_peers(sha1_hash const& /*target*/
		, sha1_hash const& /*sent_target*/, udp::endpoint const&) override {}
	void announce(sha1_hash const&, address const&, int) override {}
#ifndef TORRENT_DISABLE_LOGGING
	bool should_log(module_t) const override { return true; }
	void log(dht_logger::module_t, char const* fmt, ...) override
	{
		va_list v;
		va_start(v, fmt);
		char buf[1024];
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
		std::vsnprintf(buf, sizeof(buf), fmt, v);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
		va_end(v);
		std::printf("%s\n", buf);
		m_log.emplace_back(buf);
	}
	void log_packet(message_direction_t, span<char const>
		, udp::endpoint const&) override {}
#endif
	bool on_dht_request(string_view
		, dht::msg const&, entry&) override { return false; }

	virtual ~obs() = default;

#ifndef TORRENT_DISABLE_LOGGING
	std::vector<std::string> m_log;
#endif
};

aux::session_settings test_settings()
{
	aux::session_settings sett;
	sett.set_int(settings_pack::dht_max_torrents, 4);
	sett.set_int(settings_pack::dht_max_dht_items, 4);
	sett.set_bool(settings_pack::dht_enforce_node_id, false);
	return sett;
}

struct dht_test_setup
{
	explicit dht_test_setup(udp::endpoint src)
		: sett(test_settings())
		, ls(dummy_listen_socket(src))
		, dht_storage(dht_default_storage_constructor(sett))
		, source(src)
		, dht_node(ls, &s, sett
			, node_id(nullptr), &observer, cnt, get_foreign_node_stub, *dht_storage)
	{
		dht_storage->update_node_ids({node_id::min()});
	}

	aux::session_settings sett;
	mock_socket s;
	std::shared_ptr<aux::listen_socket_t> ls;
	obs observer;
	counters cnt;
	std::unique_ptr<dht_storage_interface> dht_storage;
	udp::endpoint source;
	dht::node dht_node;
	char error_string[200];
};

dht::key_desc_t const err_desc[] = {
	{"y", bdecode_node::string_t, 1, 0},
	{"e", bdecode_node::list_t, 2, 0}
};

dht::key_desc_t const peer1_desc[] = {
	{"y", bdecode_node::string_t, 1, 0},
	{"r", bdecode_node::dict_t, 0, key_desc_t::parse_children},
		{"token", bdecode_node::string_t, 0, 0},
		{"id", bdecode_node::string_t, 20, key_desc_t::last_child},
};

dht::key_desc_t const get_item_desc[] = {
	{"y", bdecode_node::string_t, 1, 0},
	{"t", bdecode_node::string_t, 2, 0},
	{"q", bdecode_node::string_t, 3, 0},
	{"a", bdecode_node::dict_t, 0, key_desc_t::parse_children},
		{"id", bdecode_node::string_t, 20, 0},
		{"target", bdecode_node::string_t, 20, key_desc_t::last_child},
};

dht::key_desc_t const put_mutable_item_desc[] = {
	{"y", bdecode_node::string_t, 1, 0},
	{"t", bdecode_node::string_t, 2, 0},
	{"q", bdecode_node::string_t, 3, 0},
	{"a", bdecode_node::dict_t, 0, key_desc_t::parse_children},
		{"id", bdecode_node::string_t, 20, 0},
		{"cas", bdecode_node::string_t, 20, key_desc_t::optional},
		{"k", bdecode_node::string_t, public_key::len, 0},
		{"seq", bdecode_node::int_t, 0, 0},
		{"sig", bdecode_node::string_t, signature::len, 0},
		{"token", bdecode_node::string_t, 2, 0},
		{"v", bdecode_node::none_t, 0, key_desc_t::last_child},
};

dht::key_desc_t const sample_infohashes_desc[] = {
	{"y", bdecode_node::string_t, 1, 0},
	{"t", bdecode_node::string_t, 2, 0},
	{"q", bdecode_node::string_t, 17, 0},
	{"a", bdecode_node::dict_t, 0, key_desc_t::parse_children},
		{"id", bdecode_node::string_t, 20, 0},
		{"target", bdecode_node::string_t, 20, key_desc_t::last_child},
};

void print_state(std::ostream& os, routing_table const& table)
{
#define BUFFER_CURSOR_POS &buf[std::size_t(cursor)], buf.size() - std::size_t(cursor)
	std::vector<char> buf(2048);
	int cursor = 0;

	cursor += std::snprintf(BUFFER_CURSOR_POS
		, "kademlia routing table state\n"
		"bucket_size: %d\n"
		"global node count: %" PRId64 "\n"
		"node_id: %s\n\n"
		"number of nodes per bucket:\n"
		, table.bucket_size()
		, table.num_global_nodes()
		, aux::to_hex(table.id()).c_str());
	if (cursor > int(buf.size()) - 500) buf.resize(buf.size() * 3 / 2);

	int idx = 0;

	for (auto i = table.buckets().begin(), end(table.buckets().end());
		i != end; ++i, ++idx)
	{
		cursor += std::snprintf(BUFFER_CURSOR_POS
			, "%2d: ", idx);
		for (int k = 0; k < int(i->live_nodes.size()); ++k)
			cursor += std::snprintf(BUFFER_CURSOR_POS, "#");
		for (int k = 0; k < int(i->replacements.size()); ++k)
			cursor += std::snprintf(BUFFER_CURSOR_POS, "-");
		cursor += std::snprintf(BUFFER_CURSOR_POS, "\n");

		if (cursor > int(buf.size()) - 500) buf.resize(buf.size() * 3 / 2);
	}

	time_point now = aux::time_now();

	cursor += std::snprintf(BUFFER_CURSOR_POS
		, "\nnodes:");

	int bucket_index = 0;
	for (auto i = table.buckets().begin(), end(table.buckets().end());
		i != end; ++i, ++bucket_index)
	{
		cursor += std::snprintf(BUFFER_CURSOR_POS
			, "\n=== BUCKET == %d == %d|%d ==== \n"
			, bucket_index, int(i->live_nodes.size())
			, int(i->replacements.size()));
		if (cursor > int(buf.size()) - 500) buf.resize(buf.size() * 3 / 2);

		bucket_t nodes = i->live_nodes;

		std::sort(nodes.begin(), nodes.end()
			, [](node_entry const& lhs, node_entry const& rhs)
			{ return lhs.id < rhs.id; }
		);

		for (auto j = nodes.begin(); j != nodes.end(); ++j)
		{
			int const bucket_size_limit = table.bucket_limit(bucket_index);
			TORRENT_ASSERT_VAL(bucket_size_limit <= 256, bucket_size_limit);
			TORRENT_ASSERT_VAL(bucket_size_limit > 0, bucket_size_limit);

			bool const last_bucket = bucket_index + 1 == int(table.buckets().size());
			int const prefix = classify_prefix(bucket_index, last_bucket
				, bucket_size_limit, j->id);

			cursor += std::snprintf(BUFFER_CURSOR_POS
				, " prefix: %2x id: %s", prefix, aux::to_hex(j->id).c_str());

			if (j->rtt == 0xffff)
			{
				cursor += std::snprintf(BUFFER_CURSOR_POS
					, " rtt:     ");
			}
			else
			{
				cursor += std::snprintf(BUFFER_CURSOR_POS
					, " rtt: %4d", j->rtt);
			}

			cursor += std::snprintf(BUFFER_CURSOR_POS
				, " fail: %3d ping: %d dist: %3d"
				, j->fail_count()
				, j->pinged()
				, distance_exp(table.id(), j->id));

			if (j->last_queried == min_time())
			{
				cursor += std::snprintf(BUFFER_CURSOR_POS
					, " query:    ");
			}
			else
			{
				cursor += std::snprintf(BUFFER_CURSOR_POS
					, " query: %3d", int(total_seconds(now - j->last_queried)));
			}

			cursor += std::snprintf(BUFFER_CURSOR_POS
				, " ip: %s\n", print_endpoint(j->ep()).c_str());
			if (cursor > int(buf.size()) - 500) buf.resize(buf.size() * 3 / 2);
		}
	}

	cursor += std::snprintf(BUFFER_CURSOR_POS
		, "\nnode spread per bucket:\n");
	bucket_index = 0;
	for (auto i = table.buckets().begin(), end(table.buckets().end());
		i != end; ++i, ++bucket_index)
	{
		int const bucket_size_limit = table.bucket_limit(bucket_index);
		TORRENT_ASSERT_VAL(bucket_size_limit <= 256, bucket_size_limit);
		TORRENT_ASSERT_VAL(bucket_size_limit > 0, bucket_size_limit);
		std::array<bool, 256> sub_buckets;
		sub_buckets.fill(false);

		// the last bucket is special, since it hasn't been split yet, it
		// includes that top bit as well
		bool const last_bucket = bucket_index + 1 == int(table.buckets().size());

		for (auto const& e : i->live_nodes)
		{
			std::size_t const prefix = static_cast<std::size_t>(
				classify_prefix(bucket_index, last_bucket, bucket_size_limit, e.id));
			sub_buckets[prefix] = true;
		}

		cursor += std::snprintf(BUFFER_CURSOR_POS, "%2d: [", bucket_index);

		for (int j = 0; j < bucket_size_limit; ++j)
		{
			cursor += std::snprintf(BUFFER_CURSOR_POS
				, (sub_buckets[static_cast<std::size_t>(j)] ? "X" : " "));
		}
		cursor += std::snprintf(BUFFER_CURSOR_POS
			, "]\n");
		if (cursor > int(buf.size()) - 500) buf.resize(buf.size() * 3 / 2);
	}
	buf[std::size_t(cursor)] = '\0';
	os << &buf[0];
#undef BUFFER_CURSOR_POS
}

} // anonymous namespace

TORRENT_TEST(ping)
{
	dht_test_setup t(udp::endpoint(rand_v4(), 20));
	bdecode_node response;

	send_dht_request(t.dht_node, "ping", t.source, &response);

	dht::key_desc_t const pong_desc[] = {
		{"y", bdecode_node::string_t, 1, 0},
		{"t", bdecode_node::string_t, 2, 0},
		{"r", bdecode_node::dict_t, 0, key_desc_t::parse_children},
			{"id", bdecode_node::string_t, 20, key_desc_t::last_child},
	};

	bdecode_node pong_keys[4];

	std::printf("msg: %s\n", print_entry(response).c_str());
	bool ret = dht::verify_message(response, pong_desc, pong_keys, t.error_string);
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(pong_keys[0].string_value() == "r");
		TEST_CHECK(pong_keys[1].string_value() == "10");
	}
	else
	{
		std::printf("   invalid ping response: %s\n", t.error_string);
	}
}

TORRENT_TEST(invalid_message)
{
	dht_test_setup t(udp::endpoint(rand_v4(), 20));
	bdecode_node response;
	bdecode_node err_keys[2];

	send_dht_request(t.dht_node, "find_node", t.source, &response);

	std::printf("msg: %s\n", print_entry(response).c_str());
	bool ret = dht::verify_message(response, err_desc, err_keys, t.error_string);
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(err_keys[0].string_value() == "e");
		if (err_keys[1].list_at(0).type() == bdecode_node::int_t
			&& err_keys[1].list_at(1).type() == bdecode_node::string_t)
		{
			TEST_CHECK(err_keys[1].list_at(1).string_value() == "missing 'target' key");
		}
		else
		{
			TEST_ERROR("invalid error response");
		}
	}
	else
	{
		std::printf("   invalid error response: %s\n", t.error_string);
	}
}

TORRENT_TEST(node_id_testng)
{
	node_id rnd = generate_secret_id();
	TEST_CHECK(verify_secret_id(rnd));

	rnd[19] ^= 0x55;
	TEST_CHECK(!verify_secret_id(rnd));

	rnd = generate_random_id();
	make_id_secret(rnd);
	TEST_CHECK(verify_secret_id(rnd));
}

TORRENT_TEST(get_peers_announce)
{
	dht_test_setup t(udp::endpoint(rand_v4(), 20));
	bdecode_node response;

	send_dht_request(t.dht_node, "get_peers", t.source, &response
		, msg_args().info_hash("01010101010101010101"));

	bdecode_node peer1_keys[4];

	std::string token;
	std::printf("msg: %s\n", print_entry(response).c_str());
	bool ret = dht::verify_message(response, peer1_desc, peer1_keys, t.error_string);
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(peer1_keys[0].string_value() == "r");
		token = peer1_keys[2].string_value().to_string();
//		std::printf("got token: %s\n", token.c_str());
	}
	else
	{
		std::printf("msg: %s\n", print_entry(response).c_str());
		std::printf("   invalid get_peers response: %s\n", t.error_string);
	}

	send_dht_request(t.dht_node, "announce_peer", t.source, &response
		, msg_args()
			.info_hash("01010101010101010101")
			.name("test")
			.token(token)
			.port(8080));

	dht::key_desc_t const ann_desc[] = {
		{"y", bdecode_node::string_t, 1, 0},
		{"r", bdecode_node::dict_t, 0, key_desc_t::parse_children},
			{"id", bdecode_node::string_t, 20, key_desc_t::last_child},
	};

	bdecode_node ann_keys[3];

	std::printf("msg: %s\n", print_entry(response).c_str());
	ret = dht::verify_message(response, ann_desc, ann_keys, t.error_string);
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(ann_keys[0].string_value() == "r");
	}
	else
	{
		std::printf("   invalid announce response:\n");
		TEST_ERROR(t.error_string);
	}
}

namespace {

void test_scrape(address(&rand_addr)())
{
	dht_test_setup t(udp::endpoint(rand_addr(), 20));
	bdecode_node response;

	init_rand_address();

	// announce from 100 random IPs and make sure scrape works
	// 50 downloaders and 50 seeds
	for (int i = 0; i < 100; ++i)
	{
		t.source = udp::endpoint(rand_addr(), 6000);
		send_dht_request(t.dht_node, "get_peers", t.source, &response
			, msg_args().info_hash("01010101010101010101"));

		bdecode_node peer1_keys[4];
		bool ret = dht::verify_message(response, peer1_desc, peer1_keys, t.error_string);

		std::string token;
		if (ret)
		{
			TEST_CHECK(peer1_keys[0].string_value() == "r");
			token = peer1_keys[2].string_value().to_string();
		}
		else
		{
			std::printf("msg: %s\n", print_entry(response).c_str());
			std::printf("   invalid get_peers response: %s\n", t.error_string);
		}
		response.clear();
		send_dht_request(t.dht_node, "announce_peer", t.source, &response
			, msg_args()
				.info_hash("01010101010101010101")
				.name("test")
				.token(token)
				.port(8080)
				.seed(i >= 50));

		response.clear();
	}

	// ====== get_peers ======

	send_dht_request(t.dht_node, "get_peers", t.source, &response
		, msg_args().info_hash("01010101010101010101").scrape(true));

	dht::key_desc_t const peer2_desc[] = {
		{"y", bdecode_node::string_t, 1, 0},
		{"r", bdecode_node::dict_t, 0, key_desc_t::parse_children},
			{"BFpe", bdecode_node::string_t, 256, 0},
			{"BFsd", bdecode_node::string_t, 256, 0},
			{"id", bdecode_node::string_t, 20, key_desc_t::last_child},
	};

	bdecode_node peer2_keys[5];

	std::printf("msg: %s\n", print_entry(response).c_str());
	bool ret = dht::verify_message(response, peer2_desc, peer2_keys, t.error_string);
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(peer2_keys[0].string_value() == "r");
		TEST_EQUAL(peer2_keys[1].dict_find_string_value("n"), "test");

		bloom_filter<256> downloaders;
		bloom_filter<256> seeds;
		downloaders.from_string(peer2_keys[2].string_ptr());
		seeds.from_string(peer2_keys[3].string_ptr());

		std::printf("seeds: %f\n", double(seeds.size()));
		std::printf("downloaders: %f\n", double(downloaders.size()));

		TEST_CHECK(std::abs(seeds.size() - 50.f) <= 3.f);
		TEST_CHECK(std::abs(downloaders.size() - 50.f) <= 3.f);
	}
	else
	{
		std::printf("invalid get_peers response:\n");
		TEST_ERROR(t.error_string);
	}
}

} // anonymous namespace

TORRENT_TEST(scrape_v4)
{
	test_scrape(rand_v4);
}

TORRENT_TEST(scrape_v6)
{
	if (supports_ipv6())
		test_scrape(rand_v6);
}

namespace {

void test_id_enforcement(address(&rand_addr)())
{
	dht_test_setup t(udp::endpoint(rand_addr(), 20));
	bdecode_node response;

	// enable node_id enforcement
	t.sett.set_bool(settings_pack::dht_enforce_node_id, true);

	node_id nid;
	if (aux::is_v4(t.source))
	{
		// this is one of the test vectors from:
		// http://libtorrent.org/dht_sec.html
		t.source = udp::endpoint(addr("124.31.75.21"), 1);
		nid = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee401");
	}
	else
	{
		t.source = udp::endpoint(addr("2001:b829:2123:be84:e16c:d6ae:5290:49f1"), 1);
		nid = to_hash("0a8ad123be84e16cd6ae529049f1f1bbe9ebb304");
	}

	// verify that we reject invalid node IDs
	// this is now an invalid node-id for 'source'
	nid[0] = 0x18;
	// the test nodes don't get pinged so they will only get as far
	// as the replacement bucket
	int nodes_num = std::get<1>(t.dht_node.size());
	send_dht_request(t.dht_node, "find_node", t.source, &response
		, msg_args()
			.target(sha1_hash("0101010101010101010101010101010101010101"))
			.nid(nid));

	bdecode_node err_keys[2];
	bool ret = dht::verify_message(response, err_desc, err_keys, t.error_string);
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(err_keys[0].string_value() == "e");
		if (err_keys[1].list_at(0).type() == bdecode_node::int_t
			&& err_keys[1].list_at(1).type() == bdecode_node::string_t)
		{
			TEST_CHECK(err_keys[1].list_at(1).string_value() == "invalid node ID");
		}
		else
		{
			std::printf("msg: %s\n", print_entry(response).c_str());
			TEST_ERROR("invalid error response");
		}
	}
	else
	{
		std::printf("msg: %s\n", print_entry(response).c_str());
		std::printf("   invalid error response: %s\n", t.error_string);
	}

	// a node with invalid node-id shouldn't be added to routing table.
	TEST_EQUAL(std::get<1>(t.dht_node.size()), nodes_num);

	// now the node-id is valid.
	if (aux::is_v4(t.source))
		nid[0] = 0x5f;
	else
		nid[0] = 0x0a;
	send_dht_request(t.dht_node, "find_node", t.source, &response
		, msg_args()
			.target(sha1_hash("0101010101010101010101010101010101010101"))
			.nid(nid));

	dht::key_desc_t const nodes_desc[] = {
		{"y", bdecode_node::string_t, 1, 0},
		{"r", bdecode_node::dict_t, 0, key_desc_t::parse_children},
			{"id", bdecode_node::string_t, 20, key_desc_t::last_child},
	};

	bdecode_node nodes_keys[3];

	std::printf("msg: %s\n", print_entry(response).c_str());
	ret = dht::verify_message(response, nodes_desc, nodes_keys, t.error_string);
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(nodes_keys[0].string_value() == "r");
	}
	else
	{
		std::printf("msg: %s\n", print_entry(response).c_str());
		std::printf("   invalid error response: %s\n", t.error_string);
	}
	// node with valid node-id should be added to routing table.
	TEST_EQUAL(std::get<1>(t.dht_node.size()), nodes_num + 1);
}

} // anonymous namespace

TORRENT_TEST(id_enforcement_v4)
{
	test_id_enforcement(rand_v4);
}

TORRENT_TEST(id_enforcement_v6)
{
	if (supports_ipv6())
		test_id_enforcement(rand_v6);
}

TORRENT_TEST(bloom_filter)
{
	bloom_filter<256> test;
	for (int i = 0; i < 256; ++i)
	{
		char adr[50];
		std::snprintf(adr, sizeof(adr), "192.0.2.%d", i);
		address a = addr(adr);
		sha1_hash const iphash = hash_address(a);
		test.set(iphash);
	}

	if (supports_ipv6())
	{
		for (int i = 0; i < 0x3E8; ++i)
		{
			char adr[50];
			std::snprintf(adr, sizeof(adr), "2001:db8::%x", i);
			address a = addr(adr);
			sha1_hash const iphash = hash_address(a);
			test.set(iphash);
		}
	}

	// these are test vectors from BEP 33
	// http://www.bittorrent.org/beps/bep_0033.html
	std::printf("test.size: %f\n", double(test.size()));
	std::string const bf_str = test.to_string();
	std::printf("%s\n", aux::to_hex(bf_str).c_str());
	if (supports_ipv6())
	{
		TEST_CHECK(std::abs(double(test.size()) - 1224.93) < 0.001);
		TEST_CHECK(aux::to_hex(bf_str) ==
			"f6c3f5eaa07ffd91bde89f777f26fb2b"
			"ff37bdb8fb2bbaa2fd3ddde7bacfff75"
			"ee7ccbaefe5eedb1fbfaff67f6abff5e"
			"43ddbca3fd9b9ffdf4ffd3e9dff12d1b"
			"df59db53dbe9fa5b7ff3b8fdfcde1afb"
			"8bedd7be2f3ee71ebbbfe93bcdeefe14"
			"8246c2bc5dbff7e7efdcf24fd8dc7adf"
			"fd8fffdfddfff7a4bbeedf5cb95ce81f"
			"c7fcff1ff4ffffdfe5f7fdcbb7fd79b3"
			"fa1fc77bfe07fff905b7b7ffc7fefeff"
			"e0b8370bb0cd3f5b7f2bd93feb4386cf"
			"dd6f7fd5bfaf2e9ebffffeecd67adbf7"
			"c67f17efd5d75eba6ffeba7fff47a91e"
			"b1bfbb53e8abfb5762abe8ff237279bf"
			"efbfeef5ffc5febfdfe5adffadfee1fb"
			"737ffffbfd9f6aeffeee76b6fd8f72ef");
	}
	else
	{
		TEST_CHECK(std::abs(double(test.size()) - 257.854) < 0.001);
		TEST_CHECK(aux::to_hex(bf_str) ==
			"24c0004020043000102012743e004800"
			"37110820422110008000c0e302854835"
			"a05401a4045021302a306c0600018810"
			"02d8a0a3a8001901b40a800900310008"
			"d2108110c2496a0028700010d804188b"
			"01415200082004088026411104a80404"
			"8002002000080680828c400080cc4002"
			"0c042c0494447280928041402104080d"
			"4240040414a41f0205654800b0811830"
			"d2020042b002c5800004a71d0204804a"
			"0028120a004c10017801490b83400404"
			"4106005421000c86900a002050020351"
			"0060144e900100924a1018141a028012"
			"913f0041802250042280481200002004"
			"430804210101c08111c1080100108000"
			"2038008211004266848606b035001048");
	}
}

namespace {
	announce_item const items[] =
	{
		{ generate_next(), 1 },
		{ generate_next(), 2 },
		{ generate_next(), 3 },
		{ generate_next(), 4 },
		{ generate_next(), 5 },
		{ generate_next(), 6 },
		{ generate_next(), 7 },
		{ generate_next(), 8 }
	};

	lt::aux::array<node_entry, 8> build_nodes()
	{
		return lt::aux::array<node_entry, 8>(
			std::array<node_entry, 8> {
			{ { items[0].target, udp::endpoint(addr4("1.1.1.1"), 1231), 10, true}
			, { items[1].target, udp::endpoint(addr4("2.2.2.2"), 1232), 10, true}
			, { items[2].target, udp::endpoint(addr4("3.3.3.3"), 1233), 10, true}
			, { items[3].target, udp::endpoint(addr4("4.4.4.4"), 1234), 10, true}
			, { items[4].target, udp::endpoint(addr4("5.5.5.5"), 1235), 10, true}
			, { items[5].target, udp::endpoint(addr4("6.6.6.6"), 1236), 10, true}
			, { items[6].target, udp::endpoint(addr4("7.7.7.7"), 1237), 10, true}
			, { items[7].target, udp::endpoint(addr4("8.8.8.8"), 1238), 10, true} }
		});
	}

	lt::aux::array<node_entry, 9> build_nodes(sha1_hash target)
	{
		return lt::aux::array<node_entry, 9>(
			std::array<node_entry, 9> {
			{ { target, udp::endpoint(addr4("1.1.1.1"), 1231), 10, true}
			, { target, udp::endpoint(addr4("2.2.2.2"), 1232), 10, true}
			, { target, udp::endpoint(addr4("3.3.3.3"), 1233), 10, true}
			, { target, udp::endpoint(addr4("4.4.4.4"), 1234), 10, true}
			, { target, udp::endpoint(addr4("5.5.5.5"), 1235), 10, true}
			, { target, udp::endpoint(addr4("6.6.6.6"), 1236), 10, true}
			, { target, udp::endpoint(addr4("7.7.7.7"), 1237), 10, true}
			, { target, udp::endpoint(addr4("8.8.8.8"), 1238), 10, true}
			, { target, udp::endpoint(addr4("9.9.9.9"), 1239), 10, true} }
		});
	}

span<char const> const empty_salt;

// TODO: 3 split this up into smaller tests
void test_put(address(&rand_addr)())
{
	dht_test_setup t(udp::endpoint(rand_addr(), 20));

	bdecode_node response;
	bool ret;

	// ====== put ======

	init_rand_address();
	udp::endpoint eps[1000];
	for (int i = 0; i < 1000; ++i)
		eps[i] = udp::endpoint(rand_addr(), std::uint16_t(random(16534) + 1));

	announce_immutable_items(t.dht_node, eps, items, sizeof(items)/sizeof(items[0]));

	key_desc_t const desc2[] =
	{
		{ "y", bdecode_node::string_t, 1, 0 }
	};

	bdecode_node desc2_keys[1];

	key_desc_t const desc_error[] =
	{
		{ "e", bdecode_node::list_t, 2, 0 },
		{ "y", bdecode_node::string_t, 1, 0},
	};

	bdecode_node desc_error_keys[2];

	// ==== get / put mutable items ===

	span<char const> itemv;

	signature sig;
	char buffer[1200];
	sequence_number seq(4);
	public_key pk;
	secret_key sk;
	get_test_keypair(pk, sk);

	// TODO: 4 pass in the actual salt as a parameter
	for (int with_salt = 0; with_salt < 2; ++with_salt)
	{
		seq = sequence_number(4);
		std::printf("\nTEST GET/PUT%s \ngenerating ed25519 keys\n\n"
			, with_salt ? " with-salt" : " no-salt");
		std::array<char, 32> seed = ed25519_create_seed();

		std::tie(pk, sk) = ed25519_create_keypair(seed);
		std::printf("pub: %s priv: %s\n"
			, aux::to_hex(pk.bytes).c_str()
			, aux::to_hex(sk.bytes).c_str());

		std::string salt;
		if (with_salt) salt = "foobar";

		hasher h(pk.bytes);
		if (with_salt) h.update(salt);
		sha1_hash target_id = h.final();

		std::printf("target_id: %s\n"
			, aux::to_hex(target_id).c_str());

		send_dht_request(t.dht_node, "get", t.source, &response
			, msg_args().target(target_id));

		key_desc_t const desc[] =
		{
			{ "r", bdecode_node::dict_t, 0, key_desc_t::parse_children },
			{ "id", bdecode_node::string_t, 20, 0},
			{ "token", bdecode_node::string_t, 0, 0},
			{ "ip", bdecode_node::string_t, 0, key_desc_t::optional | key_desc_t::last_child},
			{ "y", bdecode_node::string_t, 1, 0},
		};

		bdecode_node desc_keys[5];

		ret = verify_message(response, desc, desc_keys, t.error_string);
		std::string token;
		if (ret)
		{
			TEST_EQUAL(desc_keys[4].string_value(), "r");
			token = desc_keys[2].string_value().to_string();
			std::printf("get response: %s\n"
				, print_entry(response).c_str());
			std::printf("got token: %s\n", aux::to_hex(token).c_str());
		}
		else
		{
			std::printf("msg: %s\n", print_entry(response).c_str());
			std::printf("   invalid get response: %s\n%s\n"
				, t.error_string, print_entry(response).c_str());
			TEST_ERROR(t.error_string);
		}

		itemv = span<char const>(buffer, bencode(buffer, items[0].ent));
		sig = sign_mutable_item(itemv, salt, seq, pk, sk);
		TEST_EQUAL(verify_mutable_item(itemv, salt, seq, pk, sig), true);

		send_dht_request(t.dht_node, "put", t.source, &response
			, msg_args()
				.token(token)
				.value(items[0].ent)
				.key(pk)
				.sig(sig)
				.seq(seq)
				.salt(salt));

		ret = verify_message(response, desc2, desc2_keys, t.error_string);
		if (ret)
		{
			std::printf("put response: %s\n"
				, print_entry(response).c_str());
			TEST_EQUAL(desc2_keys[0].string_value(), "r");
		}
		else
		{
			std::printf("   invalid put response: %s\n%s\n"
				, t.error_string, print_entry(response).c_str());
			TEST_ERROR(t.error_string);
		}

		send_dht_request(t.dht_node, "get", t.source, &response
			, msg_args().target(target_id));

		std::printf("target_id: %s\n"
			, aux::to_hex(target_id).c_str());

		key_desc_t const desc3[] =
		{
			{ "r", bdecode_node::dict_t, 0, key_desc_t::parse_children },
				{ "id", bdecode_node::string_t, 20, 0},
				{ "v", bdecode_node::none_t, 0, 0},
				{ "seq", bdecode_node::int_t, 0, 0},
				{ "sig", bdecode_node::string_t, 0, 0},
				{ "ip", bdecode_node::string_t, 0, key_desc_t::optional | key_desc_t::last_child},
			{ "y", bdecode_node::string_t, 1, 0},
		};

		bdecode_node desc3_keys[7];

		ret = verify_message(response, desc3, desc3_keys, t.error_string);
		if (ret == 0)
		{
			std::printf("msg: %s\n", print_entry(response).c_str());
			std::printf("   invalid get response: %s\n%s\n"
				, t.error_string, print_entry(response).c_str());
			TEST_ERROR(t.error_string);
		}
		else
		{
			std::printf("get response: %s\n"
				, print_entry(response).c_str());
			char value[1020];
			char* ptr = value;
			int const value_len = bencode(ptr, items[0].ent);
			TEST_EQUAL(value_len, int(desc3_keys[2].data_section().size()));
			TEST_CHECK(std::memcmp(desc3_keys[2].data_section().data(), value, std::size_t(value_len)) == 0);

			TEST_EQUAL(int(seq.value), desc3_keys[3].int_value());
		}

		// also test that invalid signatures fail!

		itemv = span<char const>(buffer, bencode(buffer, items[0].ent));
		sig = sign_mutable_item(itemv, salt, seq, pk, sk);
		TEST_EQUAL(verify_mutable_item(itemv, salt, seq, pk, sig), 1);
		// break the signature
		sig.bytes[2] ^= 0xaa;

		std::printf("PUT broken signature\n");

		TEST_CHECK(verify_mutable_item(itemv, salt, seq, pk, sig) != 1);

		send_dht_request(t.dht_node, "put", t.source, &response
			, msg_args()
				.token(token)
				.value(items[0].ent)
				.key(pk)
				.sig(sig)
				.seq(seq)
				.salt(salt));

		ret = verify_message(response, desc_error, desc_error_keys, t.error_string);
		if (ret)
		{
			std::printf("put response: %s\n", print_entry(response).c_str());
			TEST_EQUAL(desc_error_keys[1].string_value(), "e");
			// 206 is the code for invalid signature
			TEST_EQUAL(desc_error_keys[0].list_int_value_at(0), 206);
		}
		else
		{
			std::printf("   invalid put response: %s\n%s\n"
				, t.error_string, print_entry(response).c_str());
			TEST_ERROR(t.error_string);
		}

		// === test conditional get ===

		send_dht_request(t.dht_node, "get", t.source, &response
			, msg_args().target(target_id).seq(prev_seq(seq)));

		{
			bdecode_node const r = response.dict_find_dict("r");
			TEST_CHECK(r.dict_find("v"));
			TEST_CHECK(r.dict_find("k"));
			TEST_CHECK(r.dict_find("sig"));
		}

		send_dht_request(t.dht_node, "get", t.source, &response
			, msg_args().target(target_id).seq(seq));

		{
			bdecode_node r = response.dict_find_dict("r");
			TEST_CHECK(!r.dict_find("v"));
			TEST_CHECK(!r.dict_find("k"));
			TEST_CHECK(!r.dict_find("sig"));
		}

		// === test CAS put ===

		// this is the sequence number we expect to be there
		sequence_number cas = seq;

		// increment sequence number
		seq = next_seq(seq);
		// put item 1
		itemv = span<char const>(buffer, bencode(buffer, items[1].ent));
		sig = sign_mutable_item(itemv, salt, seq, pk, sk);
		TEST_EQUAL(verify_mutable_item(itemv, salt, seq, pk, sig), 1);

		TEST_CHECK(item_target_id(salt, pk) == target_id);

		std::printf("PUT CAS 1\n");

		send_dht_request(t.dht_node, "put", t.source, &response
			, msg_args()
				.token(token)
				.value(items[1].ent)
				.key(pk)
				.sig(sig)
				.seq(seq)
				.cas(cas)
				.salt(salt));

		ret = verify_message(response, desc2, desc2_keys, t.error_string);
		if (ret)
		{
			std::printf("put response: %s\n"
				, print_entry(response).c_str());
			TEST_EQUAL(desc2_keys[0].string_value(), "r");
		}
		else
		{
			std::printf("   invalid put response: %s\n%s\n"
				, t.error_string, print_entry(response).c_str());
			TEST_ERROR(t.error_string);
		}

		std::printf("PUT CAS 2\n");

		// put the same message again. This should fail because the
		// CAS hash is outdated, it's not the hash of the value that's
		// stored anymore
		send_dht_request(t.dht_node, "put", t.source, &response
			, msg_args()
				.token(token)
				.value(items[1].ent)
				.key(pk)
				.sig(sig)
				.seq(seq)
				.cas(cas)
				.salt(salt));

		ret = verify_message(response, desc_error, desc_error_keys, t.error_string);
		if (ret)
		{
			std::printf("put response: %s\n"
				, print_entry(response).c_str());
			TEST_EQUAL(desc_error_keys[1].string_value(), "e");
			// 301 is the error code for CAS hash mismatch
			TEST_EQUAL(desc_error_keys[0].list_int_value_at(0), 301);
		}
		else
		{
			std::printf("   invalid put response: %s\n%s\nExpected failure 301 (CAS hash mismatch)\n"
				, t.error_string, print_entry(response).c_str());
			TEST_ERROR(t.error_string);
		}
	}
}

} // anonymous namespace

TORRENT_TEST(put_v4)
{
	test_put(rand_v4);
}

TORRENT_TEST(put_v6)
{
	if (supports_ipv6())
		test_put(rand_v6);
}

namespace {

void test_routing_table(address(&rand_addr)())
{
	init_rand_address();

	dht_test_setup t(udp::endpoint(rand_addr(), 20));
	bdecode_node response;

	// test kademlia routing table
	aux::session_settings s;
	s.set_bool(settings_pack::dht_extended_routing_table, false);
	//	s.set_bool(settings_pack::dht_restrict_routing_ips, false);
	node_id const nid = to_hash("3123456789abcdef01232456789abcdef0123456");
	const int bucket_size = 8;
	dht::routing_table table(nid, t.source.protocol(), bucket_size, s, &t.observer);
	TEST_EQUAL(std::get<0>(table.size()), 0);

	address node_addr;
	address node_near_addr;
	if (aux::is_v6(t.source))
	{
		node_addr = addr6("2001:1111:1111:1111:1111:1111:1111:1111");
		node_near_addr = addr6("2001:1111:1111:1111:eeee:eeee:eeee:eeee");
	}
	else
	{
		node_addr = addr4("4.4.4.4");
		node_near_addr = addr4("4.4.4.5");
	}

	// test a node with the same IP:port changing ID
	node_id const tmp = generate_id_impl(node_addr, 1);
	table.node_seen(tmp, udp::endpoint(node_addr, 4), 10);

	std::vector<node_entry> nodes = table.find_node(nid, {}, 10);
	TEST_EQUAL(table.bucket_size(0), 1);
	TEST_EQUAL(std::get<0>(table.size()), 1);
	TEST_EQUAL(nodes.size(), 1);
	if (!nodes.empty())
	{
		TEST_EQUAL(nodes[0].id, tmp);
		TEST_EQUAL(nodes[0].addr(), node_addr);
		TEST_EQUAL(nodes[0].port(), 4);
		TEST_EQUAL(nodes[0].timeout_count, 0);
	}

	// set timeout_count to 1
	table.node_failed(tmp, udp::endpoint(node_addr, 4));

	nodes.clear();
	table.for_each_node(std::bind(node_push_back, &nodes, _1), nullptr);
	TEST_EQUAL(nodes.size(), 1);
	if (!nodes.empty())
	{
		TEST_EQUAL(nodes[0].id, tmp);
		TEST_EQUAL(nodes[0].addr(), node_addr);
		TEST_EQUAL(nodes[0].port(), 4);
		TEST_EQUAL(nodes[0].timeout_count, 1);
	}

	// add the exact same node again, it should set the timeout_count to 0
	table.node_seen(tmp, udp::endpoint(node_addr, 4), 10);
	nodes.clear();
	table.for_each_node(std::bind(node_push_back, &nodes, _1), nullptr);
	TEST_EQUAL(nodes.size(), 1);
	if (!nodes.empty())
	{
		TEST_EQUAL(nodes[0].id, tmp);
		TEST_EQUAL(nodes[0].addr(), node_addr);
		TEST_EQUAL(nodes[0].port(), 4);
		TEST_EQUAL(nodes[0].timeout_count, 0);
	}

	// test adding the same node ID again with a different IP (should be ignored)
	table.node_seen(tmp, udp::endpoint(node_addr, 5), 10);
	nodes = table.find_node(nid, {}, 10);
	TEST_EQUAL(table.bucket_size(0), 1);
	if (!nodes.empty())
	{
		TEST_EQUAL(nodes[0].id, tmp);
		TEST_EQUAL(nodes[0].addr(), node_addr);
		TEST_EQUAL(nodes[0].port(), 4);
	}

	// test adding a node that ends up in the same bucket with an IP
	// very close to the current one (should be ignored)
	// if restrict_routing_ips == true
	table.node_seen(tmp, udp::endpoint(node_near_addr, 5), 10);
	nodes = table.find_node(nid, {}, 10);
	TEST_EQUAL(table.bucket_size(0), 1);
	if (!nodes.empty())
	{
		TEST_EQUAL(nodes[0].id, tmp);
		TEST_EQUAL(nodes[0].addr(), node_addr);
		TEST_EQUAL(nodes[0].port(), 4);
	}

	// test adding the same IP:port again with a new node ID (should remove the node)
	{
		auto const id = generate_id_impl(node_addr, 2);
		table.node_seen(id, udp::endpoint(node_addr, 4), 10);
		nodes = table.find_node(id, {}, 10);
	}
	TEST_EQUAL(table.bucket_size(0), 0);
	TEST_EQUAL(nodes.size(), 0);

	s.set_bool(settings_pack::dht_restrict_routing_ips, false);

	{
		auto const ep = rand_udp_ep(rand_addr);
		auto const id = generate_id_impl(ep.address(), 2);
		table.node_seen(id, ep, 10);
	}

	nodes.clear();
	for (int i = 0; i < 10000; ++i)
	{
		auto const ep = rand_udp_ep(rand_addr);
		auto const id = generate_id_impl(ep.address(), 6);
		table.node_seen(id, ep, 20 + (id[19] & 0xff));
	}
	std::printf("active buckets: %d\n", table.num_active_buckets());
	TEST_CHECK(table.num_active_buckets() == 11
		|| table.num_active_buckets() == 12);
	TEST_CHECK(std::get<0>(table.size()) >= bucket_size * 10);
	//TODO: 2 test num_global_nodes
	//TODO: 2 test need_refresh

	print_state(std::cout, table);

	table.for_each_node(std::bind(node_push_back, &nodes, _1), nullptr);

	std::printf("nodes: %d\n", int(nodes.size()));


	{
		node_id const id = generate_random_id();
		std::vector<node_entry> temp = table.find_node(id, {}, int(nodes.size()) * 2);
		std::printf("returned-all: %d\n", int(temp.size()));
		TEST_EQUAL(temp.size(), nodes.size());
	}

	// This makes sure enough of the nodes returned are actually
	// part of the closest nodes
	std::set<node_id> duplicates;

	const int reps = 50;

	for (int r = 0; r < reps; ++r)
	{
		node_id const id = generate_random_id();
		std::vector<node_entry> temp = table.find_node(id, {}, bucket_size * 2);
		TEST_EQUAL(int(temp.size()), std::min(bucket_size * 2, int(nodes.size())));

		std::sort(nodes.begin(), nodes.end(), std::bind(&compare_ref
				, std::bind(&node_entry::id, _1)
				, std::bind(&node_entry::id, _2), id));

		int expected = std::accumulate(nodes.begin(), nodes.begin() + int(temp.size())
			, 0, std::bind(&sum_distance_exp, _1, _2, id));
		int sum_hits = std::accumulate(temp.begin(), temp.end()
			, 0, std::bind(&sum_distance_exp, _1, _2, id));
		TEST_EQUAL(bucket_size * 2, int(temp.size()));
		TEST_EQUAL(expected, sum_hits);

		duplicates.clear();
		// This makes sure enough of the nodes returned are actually
		// part of the closest nodes
		for (auto const& e : temp)
		{
			TEST_CHECK(duplicates.count(e.id) == 0);
			duplicates.insert(e.id);
		}
	}

	char const* ips[] = {
		"124.31.75.21",
		"21.75.31.124",
		"65.23.51.170",
		"84.124.73.14",
		"43.213.53.83",
	};

	int rs[] = { 1,86,22,65,90 };

	std::uint8_t prefixes[][3] =
	{
		{ 0x5f, 0xbf, 0xbf },
		{ 0x5a, 0x3c, 0xe9 },
		{ 0xa5, 0xd4, 0x32 },
		{ 0x1b, 0x03, 0x21 },
		{ 0xe5, 0x6f, 0x6c }
	};

	for (int i = 0; i < 5; ++i)
	{
		address const a = addr4(ips[i]);
		node_id const new_id = generate_id_impl(a, std::uint32_t(rs[i]));
		TEST_CHECK(new_id[0] == prefixes[i][0]);
		TEST_CHECK(new_id[1] == prefixes[i][1]);
		TEST_CHECK((new_id[2] & 0xf8) == (prefixes[i][2] & 0xf8));

		TEST_CHECK(new_id[19] == rs[i]);
		std::printf("IP address: %s r: %d node ID: %s\n", ips[i]
			, rs[i], aux::to_hex(new_id).c_str());
	}
}

} // anonymous namespace

TORRENT_TEST(routing_table_v4)
{
	test_routing_table(rand_v4);
}

TORRENT_TEST(routing_table_v6)
{
	if (supports_ipv6())
		test_routing_table(rand_v6);
}

namespace {

void test_bootstrap(address(&rand_addr)())
{
	dht_test_setup t(udp::endpoint(rand_addr(), 20));
	bdecode_node response;
	bool ret;

	dht::key_desc_t const find_node_desc[] = {
		{"y", bdecode_node::string_t, 1, 0},
		{"t", bdecode_node::string_t, 2, 0},
		{"q", bdecode_node::string_t, 9, 0},
		{"a", bdecode_node::dict_t, 0, key_desc_t::parse_children},
			{"id", bdecode_node::string_t, 20, 0},
			{"target", bdecode_node::string_t, 20, key_desc_t::optional},
			{"info_hash", bdecode_node::string_t, 20, key_desc_t::optional | key_desc_t::last_child},
	};

	bdecode_node find_node_keys[7];

	// bootstrap

	g_sent_packets.clear();

	udp::endpoint initial_node(rand_addr(), 1234);
	std::vector<udp::endpoint> nodesv;
	nodesv.push_back(initial_node);
	t.dht_node.bootstrap(nodesv, std::bind(&nop_node));

	TEST_EQUAL(g_sent_packets.size(), 1);
	if (g_sent_packets.empty()) return;
	TEST_EQUAL(g_sent_packets.front().first, initial_node);

	node_from_entry(g_sent_packets.front().second, response);
	ret = verify_message(response, find_node_desc, find_node_keys, t.error_string);
	if (ret)
	{
		TEST_EQUAL(find_node_keys[0].string_value(), "q");
		TEST_CHECK(find_node_keys[2].string_value() == "find_node"
			|| find_node_keys[2].string_value() == "get_peers");

		if (find_node_keys[0].string_value() != "q"
			|| (find_node_keys[2].string_value() != "find_node"
				&& find_node_keys[2].string_value() != "get_peers")) return;
	}
	else
	{
		std::printf("   invalid find_node request: %s\n", print_entry(response).c_str());
		TEST_ERROR(t.error_string);
		return;
	}

	udp::endpoint found_node(rand_addr(), 2235);
	std::vector<node_entry> nodes;
	nodes.push_back(node_entry{found_node});
	g_sent_packets.clear();
	if (aux::is_v4(initial_node))
		send_dht_response(t.dht_node, response, initial_node, msg_args().nodes(nodes));
	else
		send_dht_response(t.dht_node, response, initial_node, msg_args().nodes6(nodes));

	TEST_EQUAL(g_sent_packets.size(), 1);
	if (g_sent_packets.empty()) return;
	TEST_EQUAL(g_sent_packets.front().first, found_node);

	node_from_entry(g_sent_packets.front().second, response);
	ret = verify_message(response, find_node_desc, find_node_keys, t.error_string);
	if (ret)
	{
		TEST_EQUAL(find_node_keys[0].string_value(), "q");
		TEST_CHECK(find_node_keys[2].string_value() == "find_node"
			|| find_node_keys[2].string_value() == "get_peers");
		if (find_node_keys[0].string_value() != "q"
			|| (find_node_keys[2].string_value() != "find_node"
				&& find_node_keys[2].string_value() != "get_peers")) return;
	}
	else
	{
		std::printf("   invalid find_node request: %s\n", print_entry(response).c_str());
		TEST_ERROR(t.error_string);
		return;
	}

	g_sent_packets.clear();
	send_dht_response(t.dht_node, response, found_node);

	TEST_CHECK(g_sent_packets.empty());
	TEST_EQUAL(t.dht_node.num_global_nodes(), 3);
}

} // anonymous namespace

TORRENT_TEST(bootstrap_v4)
{
	test_bootstrap(rand_v4);
}

TORRENT_TEST(bootstrap_v6)
{
	if (supports_ipv6())
		test_bootstrap(rand_v6);
}

namespace {

void test_bootstrap_want(address(&rand_addr)())
{
	dht_test_setup t(udp::endpoint(rand_addr(), 20));
	bdecode_node response;
	bool ret;

	dht::key_desc_t const find_node_desc[] = {
		{"y", bdecode_node::string_t, 1, 0},
		{"t", bdecode_node::string_t, 2, 0},
		{"q", bdecode_node::string_t, 9, 0},
		{"a", bdecode_node::dict_t, 0, key_desc_t::parse_children},
			{"id", bdecode_node::string_t, 20, 0},
			{"target", bdecode_node::string_t, 20, key_desc_t::optional},
			{"info_hash", bdecode_node::string_t, 20, key_desc_t::optional},
			{"want", bdecode_node::list_t, 0, key_desc_t::last_child},
	};

	bdecode_node find_node_keys[8];

	g_sent_packets.clear();

	std::vector<udp::endpoint> nodesv;
	if (aux::is_v4(t.source))
		nodesv.push_back(rand_udp_ep(rand_v6));
	else
		nodesv.push_back(rand_udp_ep(rand_v4));

	t.dht_node.bootstrap(nodesv, std::bind(&nop_node));

	TEST_EQUAL(g_sent_packets.size(), 1);
	TEST_EQUAL(g_sent_packets.front().first, nodesv[0]);

	node_from_entry(g_sent_packets.front().second, response);
	ret = verify_message(response, find_node_desc, find_node_keys, t.error_string);
	if (ret)
	{
		TEST_EQUAL(find_node_keys[0].string_value(), "q");
		TEST_CHECK(find_node_keys[2].string_value() == "find_node"
			|| find_node_keys[2].string_value() == "get_peers");

		TEST_EQUAL(find_node_keys[7].list_size(), 1);
		if (aux::is_v4(t.source))
		{
			TEST_EQUAL(find_node_keys[7].list_string_value_at(0), "n4");
		}
		else
		{
			TEST_EQUAL(find_node_keys[7].list_string_value_at(0), "n6");
		}
	}
	else
	{
		std::printf("   invalid find_node request: %s\n", print_entry(response).c_str());
		TEST_ERROR(t.error_string);
	}
}

} // anonymous namespace

TORRENT_TEST(bootstrap_want_v4)
{
	test_bootstrap_want(rand_v4);
}

TORRENT_TEST(bootstrap_want_v6)
{
	test_bootstrap_want(rand_v6);
}

namespace {

// test that the node ignores a nodes entry which is too short
void test_short_nodes(address(&rand_addr)())
{
	dht_test_setup t(udp::endpoint(rand_addr(), 20));
	bdecode_node response;
	bool ret;

	dht::key_desc_t const find_node_desc[] = {
		{ "y", bdecode_node::string_t, 1, 0 },
		{ "t", bdecode_node::string_t, 2, 0 },
		{ "q", bdecode_node::string_t, 9, 0 },
		{ "a", bdecode_node::dict_t, 0, key_desc_t::parse_children },
		{ "id", bdecode_node::string_t, 20, 0 },
		{ "target", bdecode_node::string_t, 20, key_desc_t::optional },
		{ "info_hash", bdecode_node::string_t, 20, key_desc_t::optional | key_desc_t::last_child },
	};

	bdecode_node find_node_keys[7];

	// bootstrap

	g_sent_packets.clear();

	udp::endpoint initial_node(rand_addr(), 1234);
	std::vector<udp::endpoint> nodesv;
	nodesv.push_back(initial_node);
	t.dht_node.bootstrap(nodesv, std::bind(&nop_node));

	TEST_EQUAL(g_sent_packets.size(), 1);
	if (g_sent_packets.empty()) return;
	TEST_EQUAL(g_sent_packets.front().first, initial_node);

	node_from_entry(g_sent_packets.front().second, response);
	ret = verify_message(response, find_node_desc, find_node_keys, t.error_string);
	if (ret)
	{
		TEST_EQUAL(find_node_keys[0].string_value(), "q");
		TEST_CHECK(find_node_keys[2].string_value() == "find_node"
				   || find_node_keys[2].string_value() == "get_peers");

		if (find_node_keys[0].string_value() != "q"
			|| (find_node_keys[2].string_value() != "find_node"
				&& find_node_keys[2].string_value() != "get_peers")) return;
	}
	else
	{
		std::printf("   invalid find_node request: %s\n", print_entry(response).c_str());
		TEST_ERROR(t.error_string);
		return;
	}

	udp::endpoint found_node(rand_addr(), 2235);
	std::vector<node_entry> nodes;
	nodes.push_back(node_entry{found_node});
	g_sent_packets.clear();
	msg_args args;
	// chop one byte off of the nodes string
	if (aux::is_v4(initial_node))
	{
		args.nodes(nodes);
		args.a["nodes"] = args.a["nodes"].string().substr(1);
	}
	else
	{
		args.nodes6(nodes);
		args.a["nodes6"] = args.a["nodes6"].string().substr(1);
	}

	send_dht_response(t.dht_node, response, initial_node, args);

	TEST_EQUAL(g_sent_packets.size(), 0);
}

} // anonymous namespace

TORRENT_TEST(short_nodes_v4)
{
	test_short_nodes(rand_v4);
}

TORRENT_TEST(short_nodes_v6)
{
	if (supports_ipv6())
		test_short_nodes(rand_v6);
}

namespace {

void test_get_peers(address(&rand_addr)())
{
	dht_test_setup t(udp::endpoint(rand_addr(), 20));
	bdecode_node response;
	bool ret;

	dht::key_desc_t const get_peers_desc[] = {
		{"y", bdecode_node::string_t, 1, 0},
		{"t", bdecode_node::string_t, 2, 0},
		{"q", bdecode_node::string_t, 9, 0},
		{"a", bdecode_node::dict_t, 0, key_desc_t::parse_children},
			{"id", bdecode_node::string_t, 20, 0},
			{"info_hash", bdecode_node::string_t, 20, key_desc_t::last_child},
	};

	bdecode_node get_peers_keys[6];

	// get_peers

	g_sent_packets.clear();

	dht::node_id const target = to_hash("1234876923549721020394873245098347598635");

	udp::endpoint const initial_node(rand_addr(), 1234);
	dht::node_id const initial_node_id = to_hash("1111111111222222222233333333334444444444");
	t.dht_node.m_table.add_node(node_entry{initial_node_id, initial_node, 10, true});

	t.dht_node.announce(target, 1234, {}, get_peers_cb);

	TEST_EQUAL(g_sent_packets.size(), 1);
	if (g_sent_packets.empty()) return;
	TEST_EQUAL(g_sent_packets.front().first, initial_node);

	node_from_entry(g_sent_packets.front().second, response);
	ret = verify_message(response, get_peers_desc, get_peers_keys, t.error_string);
	if (ret)
	{
		TEST_EQUAL(get_peers_keys[0].string_value(), "q");
		TEST_EQUAL(get_peers_keys[2].string_value(), "get_peers");
		TEST_EQUAL(get_peers_keys[5].string_value(), target.to_string());
		if (get_peers_keys[0].string_value() != "q"
			|| get_peers_keys[2].string_value() != "get_peers")
			return;
	}
	else
	{
		std::printf("   invalid get_peers request: %s\n", print_entry(response).c_str());
		TEST_ERROR(t.error_string);
		return;
	}

	std::set<tcp::endpoint> peers[2];
	peers[0].insert(tcp::endpoint(rand_addr(), 4111));
	peers[0].insert(tcp::endpoint(rand_addr(), 4112));
	peers[0].insert(tcp::endpoint(rand_addr(), 4113));

	udp::endpoint next_node(rand_addr(), 2235);
	std::vector<node_entry> nodes;
	nodes.push_back(node_entry{next_node});

	g_sent_packets.clear();
	if (aux::is_v4(initial_node))
	{
		send_dht_response(t.dht_node, response, initial_node
			, msg_args().nodes(nodes).token("10").port(1234).peers(peers[0]));
	}
	else
	{
		send_dht_response(t.dht_node, response, initial_node
			, msg_args().nodes6(nodes).token("10").port(1234).peers(peers[0]));
	}

	TEST_EQUAL(g_sent_packets.size(), 1);
	if (g_sent_packets.empty()) return;
	TEST_EQUAL(g_sent_packets.front().first, next_node);

	node_from_entry(g_sent_packets.front().second, response);
	ret = verify_message(response, get_peers_desc, get_peers_keys, t.error_string);
	if (ret)
	{
		TEST_EQUAL(get_peers_keys[0].string_value(), "q");
		TEST_EQUAL(get_peers_keys[2].string_value(), "get_peers");
		TEST_EQUAL(get_peers_keys[5].string_value(), target.to_string());
		if (get_peers_keys[0].string_value() != "q"
			|| get_peers_keys[2].string_value() != "get_peers")
			return;
	}
	else
	{
		std::printf("   invalid get_peers request: %s\n", print_entry(response).c_str());
		TEST_ERROR(t.error_string);
		return;
	}

	peers[1].insert(tcp::endpoint(rand_addr(), 4114));
	peers[1].insert(tcp::endpoint(rand_addr(), 4115));
	peers[1].insert(tcp::endpoint(rand_addr(), 4116));

	g_sent_packets.clear();
	send_dht_response(t.dht_node, response, next_node
		, msg_args().token("11").port(1234).peers(peers[1]));

	for (auto const& p : g_sent_packets)
	{
//		std::printf(" %s:%d: %s\n", i->first.address().to_string(ec).c_str()
//			, i->first.port(), i->second.to_string().c_str());
		TEST_EQUAL(p.second["q"].string(), "announce_peer");
	}

	g_sent_packets.clear();

	for (int i = 0; i < 2; ++i)
	{
		for (auto const& peer : peers[i])
		{
			TEST_CHECK(std::find(g_got_peers.begin(), g_got_peers.end(), peer) != g_got_peers.end());
		}
	}
	g_got_peers.clear();
}

} // anonymous namespace

TORRENT_TEST(get_peers_v4)
{
	test_get_peers(rand_v4);
}

TORRENT_TEST(get_peers_v6)
{
	if (supports_ipv6())
		test_get_peers(rand_v6);
}

namespace {

// TODO: 4 pass in th actual salt as the argument
void test_mutable_get(address(&rand_addr)(), bool const with_salt)
{
	dht_test_setup t(udp::endpoint(rand_addr(), 20));

	public_key pk;
	secret_key sk;
	get_test_keypair(pk, sk);

	char buffer[1200];
	sequence_number seq(4);
	span<char const> itemv;
	bdecode_node response;

	std::string salt;
	if (with_salt) salt = "foobar";

	// mutable get

	g_sent_packets.clear();

	udp::endpoint const initial_node(rand_addr(), 1234);
	dht::node_id const initial_node_id = to_hash("1111111111222222222233333333334444444444");
	t.dht_node.m_table.add_node(node_entry{initial_node_id, initial_node, 10, true});

	g_put_item.assign(items[0].ent, salt, seq, pk, sk);
	t.dht_node.put_item(pk, std::string()
		, std::bind(&put_mutable_item_cb, _1, _2, 0)
		, put_mutable_item_data_cb);

	TEST_EQUAL(g_sent_packets.size(), 1);

	// mutable_get

	g_sent_packets.clear();

	t.dht_node.get_item(pk, salt, get_mutable_item_cb);

	TEST_EQUAL(g_sent_packets.size(), 1);
	if (g_sent_packets.empty()) return;
	TEST_EQUAL(g_sent_packets.front().first, initial_node);

	node_from_entry(g_sent_packets.front().second, response);
	bdecode_node get_item_keys[6];
	bool const ret = verify_message(response, get_item_desc, get_item_keys, t.error_string);
	if (ret)
	{
		TEST_EQUAL(get_item_keys[0].string_value(), "q");
		TEST_EQUAL(get_item_keys[2].string_value(), "get");
		if (get_item_keys[0].string_value() != "q"
			|| get_item_keys[2].string_value() != "get")
			return;
	}
	else
	{
		std::printf("   invalid get request: %s\n", print_entry(response).c_str());
		TEST_ERROR(t.error_string);
		return;
	}

	g_sent_packets.clear();

	signature sig;
	itemv = span<char const>(buffer, bencode(buffer, items[0].ent));
	sig = sign_mutable_item(itemv, salt, seq, pk, sk);
	send_dht_response(t.dht_node, response, initial_node
		, msg_args()
			.token("10")
			.port(1234)
			.value(items[0].ent)
			.key(pk)
			.sig(sig)
			.salt(salt)
			.seq(seq));

	TEST_CHECK(g_sent_packets.empty());
	TEST_EQUAL(g_got_items.size(), 1);
	if (g_got_items.empty()) return;

	TEST_EQUAL(g_got_items.front().value(), items[0].ent);
	TEST_CHECK(g_got_items.front().pk() == pk);
	TEST_CHECK(g_got_items.front().sig() == sig);
	TEST_CHECK(g_got_items.front().seq() == seq);
	g_got_items.clear();
}

} // anonymous namespace

TORRENT_TEST(mutable_get_v4)
{
	test_mutable_get(rand_v4, false);
}

TORRENT_TEST(mutable_get_salt_v4)
{
	test_mutable_get(rand_v4, true);
}

TORRENT_TEST(mutable_get_v6)
{
	if (supports_ipv6())
		test_mutable_get(rand_v6, false);
}

TORRENT_TEST(mutable_get_salt_v6)
{
	if (supports_ipv6())
		test_mutable_get(rand_v6, true);
}

TORRENT_TEST(immutable_get)
{
	dht_test_setup t(udp::endpoint(rand_v4(), 20));
	bdecode_node response;

	// immutable get

	g_sent_packets.clear();

	udp::endpoint initial_node(addr4("4.4.4.4"), 1234);
	dht::node_id const initial_node_id = to_hash("1111111111222222222233333333334444444444");
	t.dht_node.m_table.add_node(node_entry{initial_node_id, initial_node, 10, true});

	t.dht_node.get_item(items[0].target, get_immutable_item_cb);

	TEST_EQUAL(g_sent_packets.size(), 1);
	if (g_sent_packets.empty()) return;
	TEST_EQUAL(g_sent_packets.front().first, initial_node);

	node_from_entry(g_sent_packets.front().second, response);
	bdecode_node get_item_keys[6];
	bool const ret = verify_message(response, get_item_desc, get_item_keys, t.error_string);
	if (ret)
	{
		TEST_EQUAL(get_item_keys[0].string_value(), "q");
		TEST_EQUAL(get_item_keys[2].string_value(), "get");
		TEST_EQUAL(get_item_keys[5].string_value(), items[0].target.to_string());
		if (get_item_keys[0].string_value() != "q" || get_item_keys[2].string_value() != "get") return;
	}
	else
	{
		std::printf("   invalid get request: %s\n", print_entry(response).c_str());
		TEST_ERROR(t.error_string);
		return;
	}

	g_sent_packets.clear();
	send_dht_response(t.dht_node, response, initial_node
		, msg_args().token("10").port(1234).value(items[0].ent));

	TEST_CHECK(g_sent_packets.empty());
	TEST_EQUAL(g_got_items.size(), 1);
	if (g_got_items.empty()) return;

	TEST_EQUAL(g_got_items.front().value(), items[0].ent);
	g_got_items.clear();
}

TORRENT_TEST(immutable_put)
{
	bdecode_node response;
	span<char const> itemv;
	char buffer[1200];

	dht::key_desc_t const put_immutable_item_desc[] = {
		{"y", bdecode_node::string_t, 1, 0},
		{"t", bdecode_node::string_t, 2, 0},
		{"q", bdecode_node::string_t, 3, 0},
		{"a", bdecode_node::dict_t, 0, key_desc_t::parse_children},
			{"id", bdecode_node::string_t, 20, 0},
			{"token", bdecode_node::string_t, 2, 0},
			{"v", bdecode_node::none_t, 0, key_desc_t::last_child},
	};

	bdecode_node put_immutable_item_keys[7];

	// immutable put
	g_sent_packets.clear();
	for (int loop = 0; loop < 9; loop++)
	{
		dht_test_setup t(udp::endpoint(rand_v4(), 20));

		// set the branching factor to k to make this a little easier
		t.sett.set_int(settings_pack::dht_search_branching, 8);

		lt::aux::array<node_entry, 8> const nodes = build_nodes();

		for (node_entry const& n : nodes)
			t.dht_node.m_table.add_node(n);

		entry put_data;
		put_data = "Hello world";
		std::string flat_data;
		bencode(std::back_inserter(flat_data), put_data);
		sha1_hash target = item_target_id(flat_data);

		t.dht_node.put_item(target, put_data, std::bind(&put_immutable_item_cb, _1, loop));

		TEST_EQUAL(g_sent_packets.size(), 8);
		if (g_sent_packets.size() != 8) break;

		int idx = -1;
		for (auto& node : nodes)
		{
			++idx;
			auto const packet = find_packet(node.ep());
			TEST_CHECK(packet != g_sent_packets.end());
			if (packet == g_sent_packets.end()) continue;

			node_from_entry(packet->second, response);
			bdecode_node get_item_keys[6];
			bool const ret = verify_message(response, get_item_desc, get_item_keys, t.error_string);
			if (!ret)
			{
				std::printf("   invalid get request: %s\n", print_entry(response).c_str());
				TEST_ERROR(t.error_string);
				continue;
			}
			char tok[11];
			std::snprintf(tok, sizeof(tok), "%02d", idx);

			msg_args args;
			args.token(tok).port(1234).nid(node.id).nodes({node});
			send_dht_response(t.dht_node, response, node.ep(), args);
			g_sent_packets.erase(packet);
		}

		TEST_EQUAL(g_sent_packets.size(), 8);
		if (g_sent_packets.size() != 8) break;

		itemv = span<char const>(buffer, bencode(buffer, put_data));

		idx = -1;
		for (auto& node : nodes)
		{
			++idx;
			auto const packet = find_packet(node.ep());
			TEST_CHECK(packet != g_sent_packets.end());
			if (packet == g_sent_packets.end()) continue;

			node_from_entry(packet->second, response);
			bool const ret = verify_message(response, put_immutable_item_desc, put_immutable_item_keys
				, t.error_string);
			if (ret)
			{
				TEST_EQUAL(put_immutable_item_keys[0].string_value(), "q");
				TEST_EQUAL(put_immutable_item_keys[2].string_value(), "put");
				span<const char> const v = put_immutable_item_keys[6].data_section();
				TEST_EQUAL(v, span<char const>(flat_data));
				char tok[11];
				std::snprintf(tok, sizeof(tok), "%02d", idx);
				TEST_EQUAL(put_immutable_item_keys[5].string_value(), tok);
				if (put_immutable_item_keys[0].string_value() != "q"
					|| put_immutable_item_keys[2].string_value() != "put") continue;

				if (idx < loop) send_dht_response(t.dht_node, response, node.ep());
			}
			else
			{
				std::printf("   invalid immutable put request: %s\n", print_entry(response).c_str());
				TEST_ERROR(t.error_string);
				continue;
			}
		}
		g_sent_packets.clear();
		g_put_item.clear();
		g_put_count = 0;
	}
}

TORRENT_TEST(mutable_put)
{
	bdecode_node response;
	span<char const> itemv;
	char buffer[1200];
	bdecode_node put_mutable_item_keys[11];
	public_key pk;
	secret_key sk;
	get_test_keypair(pk, sk);

	sequence_number seq(4);

	// mutable put
	g_sent_packets.clear();
	for (int loop = 0; loop < 9; loop++)
	{
		dht_test_setup t(udp::endpoint(rand_v4(), 20));

		// set the branching factor to k to make this a little easier
		t.sett.set_int(settings_pack::dht_search_branching, 8);

		enum { num_test_nodes = 8 };
		lt::aux::array<node_entry, num_test_nodes> const nodes = build_nodes();

		for (auto const& n : nodes)
			t.dht_node.m_table.add_node(n);

		g_put_item.assign(items[0].ent, empty_salt, seq, pk, sk);
		signature const sig = g_put_item.sig();
		t.dht_node.put_item(pk, std::string()
			, std::bind(&put_mutable_item_cb, _1, _2, loop)
			, put_mutable_item_data_cb);

		TEST_EQUAL(g_sent_packets.size(), 8);
		if (g_sent_packets.size() != 8) break;

		int idx = -1;
		for (auto& node : nodes)
		{
			++idx;
			auto const packet = find_packet(node.ep());
			TEST_CHECK(packet != g_sent_packets.end());
			if (packet == g_sent_packets.end()) continue;

			node_from_entry(packet->second, response);
			bdecode_node get_item_keys[6];
			bool const ret = verify_message(response, get_item_desc, get_item_keys, t.error_string);
			if (!ret)
			{
				std::printf("   invalid get request: %s\n", print_entry(response).c_str());
				TEST_ERROR(t.error_string);
				continue;
			}
			char tok[11];
			std::snprintf(tok, sizeof(tok), "%02d", idx);

			msg_args args;
			args.token(tok).port(1234).nid(node.id).nodes({node});
			send_dht_response(t.dht_node, response, node.ep(), args);
			g_sent_packets.erase(packet);
		}

		TEST_EQUAL(g_sent_packets.size(), 8);
		if (g_sent_packets.size() != 8) break;

		itemv = span<char const>(buffer, bencode(buffer, items[0].ent));

		idx = -1;
		for (auto& node : nodes)
		{
			++idx;
			auto const packet = find_packet(node.ep());
			TEST_CHECK(packet != g_sent_packets.end());
			if (packet == g_sent_packets.end()) continue;

			node_from_entry(packet->second, response);
			bool const ret = verify_message(response, put_mutable_item_desc, put_mutable_item_keys
				, t.error_string);
			if (ret)
			{
				TEST_EQUAL(put_mutable_item_keys[0].string_value(), "q");
				TEST_EQUAL(put_mutable_item_keys[2].string_value(), "put");
				TEST_EQUAL(put_mutable_item_keys[6].string_value()
					, std::string(pk.bytes.data(), public_key::len));
				TEST_EQUAL(put_mutable_item_keys[7].int_value(), int(seq.value));
				TEST_EQUAL(put_mutable_item_keys[8].string_value()
					, std::string(sig.bytes.data(), signature::len));
				span<const char> const v = put_mutable_item_keys[10].data_section();
				TEST_CHECK(v == itemv);
				char tok[11];
				std::snprintf(tok, sizeof(tok), "%02d", idx);
				TEST_EQUAL(put_mutable_item_keys[9].string_value(), tok);
				if (put_mutable_item_keys[0].string_value() != "q"
					|| put_mutable_item_keys[2].string_value() != "put") continue;

				if (idx < loop) send_dht_response(t.dht_node, response, node.ep());
			}
			else
			{
				std::printf("   invalid put request: %s\n", print_entry(response).c_str());
				TEST_ERROR(t.error_string);
				continue;
			}
		}
		g_sent_packets.clear();
		g_put_item.clear();
		g_put_count = 0;
	}
}

TORRENT_TEST(traversal_done)
{
	dht_test_setup t(udp::endpoint(rand_v4(), 20));

	// set the branching factor to k to make this a little easier
	t.sett.set_int(settings_pack::dht_search_branching, 8);

	public_key pk;
	secret_key sk;
	get_test_keypair(pk, sk);

	sequence_number seq(4);
	bdecode_node response;

	// verify that done() is only invoked once
	// See PR 252
	g_sent_packets.clear();

	sha1_hash const target = hasher(pk.bytes).final();
	constexpr int num_test_nodes = 9; // we need K + 1 nodes to create the failing sequence

	lt::aux::array<node_entry, num_test_nodes> nodes = build_nodes(target);

	// invert the ith most significant byte so that the test nodes are
	// progressively closer to the target item
	for (int i = 0; i < num_test_nodes; ++i)
		nodes[i].id[i] = static_cast<std::uint8_t>(~nodes[i].id[i]);

	// add the first k nodes to the subject's routing table
	for (int i = 0; i < 8; ++i)
		t.dht_node.m_table.add_node(nodes[i]);

	// kick off a mutable put request
	g_put_item.assign(items[0].ent, empty_salt, seq, pk, sk);
	t.dht_node.put_item(pk, std::string()
		, std::bind(&put_mutable_item_cb, _1, _2, 0)
		, put_mutable_item_data_cb);
	TEST_EQUAL(g_sent_packets.size(), 8);
	if (g_sent_packets.size() != 8) return;

	// first send responses for the k closest nodes
	for (int i = 1;; ++i)
	{
		// once the k closest nodes have responded, send the final response
		// from the farthest node, this shouldn't trigger a second call to
		// get_item_cb
		if (i == num_test_nodes) i = 0;

		auto const packet = find_packet(nodes[i].ep());
		TEST_CHECK(packet != g_sent_packets.end());
		if (packet == g_sent_packets.end()) continue;

		node_from_entry(packet->second, response);
		bdecode_node get_item_keys[6];
		bool const ret = verify_message(response, get_item_desc, get_item_keys, t.error_string);
		if (!ret)
		{
			std::printf("   invalid get request: %s\n", print_entry(response).c_str());
			TEST_ERROR(t.error_string);
			continue;
		}
		char tok[11];
		std::snprintf(tok, sizeof(tok), "%02d", i);

		msg_args args;
		args.token(tok).port(1234).nid(nodes[i].id);

		// add the address of the closest node to the first response
		if (i == 1)
			args.nodes({nodes[8]});

		send_dht_response(t.dht_node, response, nodes[i].ep(), args);
		g_sent_packets.erase(packet);

		// once we've sent the response from the farthest node, we're done
		if (i == 0) break;
	}

	TEST_EQUAL(g_put_count, 1);
	// k nodes should now have outstanding put requests
	TEST_EQUAL(g_sent_packets.size(), 8);

	g_sent_packets.clear();
	g_put_item.clear();
	g_put_count = 0;
}

TORRENT_TEST(dht_dual_stack)
{
	// TODO: 3 use dht_test_setup class to simplify the node setup
	auto sett = test_settings();
	mock_socket s;
	auto sock4 = dummy_listen_socket4();
	auto sock6 = dummy_listen_socket6();
	obs observer;
	counters cnt;
	node* node4p = nullptr, *node6p = nullptr;
	auto get_foreign_node = [&](node_id const&, std::string const& family)
	{
		if (family == "n4") return node4p;
		if (family == "n6") return node6p;
		TEST_CHECK(false);
		return static_cast<node*>(nullptr);
	};
	std::unique_ptr<dht_storage_interface> dht_storage(dht_default_storage_constructor(sett));
	dht_storage->update_node_ids({node_id(nullptr)});
	dht::node node4(sock4, &s, sett, node_id(nullptr), &observer, cnt, get_foreign_node, *dht_storage);
	dht::node node6(sock6, &s, sett, node_id(nullptr), &observer, cnt, get_foreign_node, *dht_storage);
	node4p = &node4;
	node6p = &node6;

	// DHT should be running on port 48199 now
	bdecode_node response;
	char error_string[200];
	bool ret;

	node_id id = to_hash("3123456789abcdef01232456789abcdef0123456");
	node4.m_table.node_seen(id, udp::endpoint(addr("4.4.4.4"), 4440), 10);
	node6.m_table.node_seen(id, udp::endpoint(addr("4::4"), 4441), 10);

	// v4 node requesting v6 nodes

	udp::endpoint source(addr("10.0.0.1"), 20);

	send_dht_request(node4, "find_node", source, &response
		, msg_args()
			.target(sha1_hash("0101010101010101010101010101010101010101"))
			.want("n6"));

	dht::key_desc_t const nodes6_desc[] = {
		{ "y", bdecode_node::string_t, 1, 0 },
		{ "r", bdecode_node::dict_t, 0, key_desc_t::parse_children },
		{ "id", bdecode_node::string_t, 20, 0 },
		{ "nodes6", bdecode_node::string_t, 38, key_desc_t::last_child }
	};

	bdecode_node nodes6_keys[4];

	ret = verify_message(response, nodes6_desc, nodes6_keys, error_string);

	if (ret)
	{
		char const* nodes_ptr = nodes6_keys[3].string_ptr();
		TEST_CHECK(memcmp(nodes_ptr, id.data(), id.size()) == 0);
		nodes_ptr += id.size();
		udp::endpoint rep = aux::read_v6_endpoint<udp::endpoint>(nodes_ptr);
		TEST_EQUAL(rep, udp::endpoint(addr("4::4"), 4441));
	}
	else
	{
		std::printf("find_node response: %s\n", print_entry(response).c_str());
		TEST_ERROR(error_string);
	}

	// v6 node requesting v4 nodes

	source.address(addr("10::1"));

	send_dht_request(node6, "get_peers", source, &response
		, msg_args().info_hash("0101010101010101010101010101010101010101").want("n4"));

	dht::key_desc_t const nodes_desc[] = {
		{ "y", bdecode_node::string_t, 1, 0 },
		{ "r", bdecode_node::dict_t, 0, key_desc_t::parse_children },
		{ "id", bdecode_node::string_t, 20, 0 },
		{ "nodes", bdecode_node::string_t, 26, key_desc_t::last_child }
	};

	bdecode_node nodes_keys[4];

	ret = verify_message(response, nodes_desc, nodes_keys, error_string);

	if (ret)
	{
		char const* nodes_ptr = nodes_keys[3].string_ptr();
		TEST_CHECK(memcmp(nodes_ptr, id.data(), id.size()) == 0);
		nodes_ptr += id.size();
		udp::endpoint rep = aux::read_v4_endpoint<udp::endpoint>(nodes_ptr);
		TEST_EQUAL(rep, udp::endpoint(addr("4.4.4.4"), 4440));
	}
	else
	{
		std::printf("find_node response: %s\n", print_entry(response).c_str());
		TEST_ERROR(error_string);
	}

	// v6 node requesting both v4 and v6 nodes

	send_dht_request(node6, "find_nodes", source, &response
		, msg_args().info_hash("0101010101010101010101010101010101010101")
			.want("n4")
			.want("n6"));

	dht::key_desc_t const nodes46_desc[] = {
		{ "y", bdecode_node::string_t, 1, 0 },
		{ "r", bdecode_node::dict_t, 0, key_desc_t::parse_children },
		{ "id", bdecode_node::string_t, 20, 0 },
		{ "nodes", bdecode_node::string_t, 26, 0 },
		{ "nodes6", bdecode_node::string_t, 38, key_desc_t::last_child }
	};

	bdecode_node nodes46_keys[5];

	ret = verify_message(response, nodes46_desc, nodes46_keys, error_string);

	if (ret)
	{
		char const* nodes_ptr = nodes46_keys[3].string_ptr();
		TEST_CHECK(memcmp(nodes_ptr, id.data(), id.size()) == 0);
		nodes_ptr += id.size();
		udp::endpoint rep = aux::read_v4_endpoint<udp::endpoint>(nodes_ptr);
		TEST_EQUAL(rep, udp::endpoint(addr("4.4.4.4"), 4440));

		nodes_ptr = nodes46_keys[4].string_ptr();
		TEST_CHECK(memcmp(nodes_ptr, id.data(), id.size()) == 0);
		nodes_ptr += id.size();
		rep = aux::read_v6_endpoint<udp::endpoint>(nodes_ptr);
		TEST_EQUAL(rep, udp::endpoint(addr("4::4"), 4441));
	}
	else
	{
		std::printf("find_node response: %s\n", print_entry(response).c_str());
		TEST_ERROR(error_string);
	}
}

TORRENT_TEST(multi_home)
{
	// send a request with a different listen socket and make sure the node ignores it
	dht_test_setup t(udp::endpoint(rand_v4(), 20));
	bdecode_node response;

	entry e;
	e["q"] = "ping";
	e["t"] = "10";
	e["y"] = "q";
	e["a"].dict().insert(std::make_pair("id", generate_next().to_string()));
	char msg_buf[1500];
	int size = bencode(msg_buf, e);

	bdecode_node decoded;
	error_code ec;
	bdecode(msg_buf, msg_buf + size, decoded, ec);
	if (ec) std::printf("bdecode failed: %s\n", ec.message().c_str());

	dht::msg m(decoded, t.source);
	t.dht_node.incoming(dummy_listen_socket(udp::endpoint(rand_v4(), 21)), m);
	TEST_CHECK(g_sent_packets.empty());
	g_sent_packets.clear();
}

TORRENT_TEST(signing_test1)
{
	// test vector 1

	// test content
	span<char const> test_content("12:Hello World!", 15);
	// test salt
	span<char const> test_salt("foobar", 6);

	public_key pk;
	secret_key sk;
	get_test_keypair(pk, sk);

	signature sig;
	sig = sign_mutable_item(test_content, empty_salt, sequence_number(1), pk, sk);

	TEST_EQUAL(aux::to_hex(sig.bytes)
		, "305ac8aeb6c9c151fa120f120ea2cfb923564e11552d06a5d856091e5e853cff"
		"1260d3f39e4999684aa92eb73ffd136e6f4f3ecbfda0ce53a1608ecd7ae21f01");

	sha1_hash const target_id = item_target_id(empty_salt, pk);
	TEST_EQUAL(aux::to_hex(target_id), "4a533d47ec9c7d95b1ad75f576cffc641853b750");
}

TORRENT_TEST(signing_test2)
{
	public_key pk;
	secret_key sk;
	get_test_keypair(pk, sk);

	// test content
	span<char const> test_content("12:Hello World!", 15);

	signature sig;
	// test salt
	span<char const> test_salt("foobar", 6);

	// test vector 2 (the keypair is the same as test 1)
	sig = sign_mutable_item(test_content, test_salt, sequence_number(1), pk, sk);

	TEST_EQUAL(aux::to_hex(sig.bytes)
		, "6834284b6b24c3204eb2fea824d82f88883a3d95e8b4a21b8c0ded553d17d17d"
		"df9a8a7104b1258f30bed3787e6cb896fca78c58f8e03b5f18f14951a87d9a08");

	sha1_hash target_id = item_target_id(test_salt, pk);
	TEST_EQUAL(aux::to_hex(target_id), "411eba73b6f087ca51a3795d9c8c938d365e32c1");
}

TORRENT_TEST(signing_test3)
{
	// test vector 3

	// test content
	span<char const> test_content("12:Hello World!", 15);

	sha1_hash target_id = item_target_id(test_content);
	TEST_EQUAL(aux::to_hex(target_id), "e5f96f6f38320f0f33959cb4d3d656452117aadb");
}

// TODO: 2 split this up into smaller test cases
TORRENT_TEST(verify_message)
{
	char error_string[200];

	// test verify_message
	static const key_desc_t msg_desc[] = {
		{"A", bdecode_node::string_t, 4, 0},
		{"B", bdecode_node::dict_t, 0, key_desc_t::optional | key_desc_t::parse_children},
			{"B1", bdecode_node::string_t, 0, 0},
			{"B2", bdecode_node::string_t, 0, key_desc_t::last_child},
		{"C", bdecode_node::dict_t, 0, key_desc_t::optional | key_desc_t::parse_children},
			{"C1", bdecode_node::string_t, 0, 0},
			{"C2", bdecode_node::string_t, 0, key_desc_t::last_child},
	};

	bdecode_node msg_keys[7];

	bdecode_node ent;

	error_code ec;
	char const test_msg[] = "d1:A4:test1:Bd2:B15:test22:B25:test3ee";
	bdecode(test_msg, test_msg + sizeof(test_msg)-1, ent, ec);
	std::printf("%s\n", print_entry(ent).c_str());

	bool ret = verify_message(ent, msg_desc, msg_keys, error_string);
	TEST_CHECK(ret);
	TEST_CHECK(msg_keys[0]);
	if (msg_keys[0]) TEST_EQUAL(msg_keys[0].string_value(), "test");
	TEST_CHECK(msg_keys[1]);
	TEST_CHECK(msg_keys[2]);
	if (msg_keys[2]) TEST_EQUAL(msg_keys[2].string_value(), "test2");
	TEST_CHECK(msg_keys[3]);
	if (msg_keys[3]) TEST_EQUAL(msg_keys[3].string_value(), "test3");
	TEST_CHECK(!msg_keys[4]);
	TEST_CHECK(!msg_keys[5]);
	TEST_CHECK(!msg_keys[6]);

	char const test_msg2[] = "d1:A4:test1:Cd2:C15:test22:C25:test3ee";
	bdecode(test_msg2, test_msg2 + sizeof(test_msg2)-1, ent, ec);
	std::printf("%s\n", print_entry(ent).c_str());

	ret = verify_message(ent, msg_desc, msg_keys, error_string);
	TEST_CHECK(ret);
	TEST_CHECK(msg_keys[0]);
	if (msg_keys[0]) TEST_EQUAL(msg_keys[0].string_value(), "test");
	TEST_CHECK(!msg_keys[1]);
	TEST_CHECK(!msg_keys[2]);
	TEST_CHECK(!msg_keys[3]);
	TEST_CHECK(msg_keys[4]);
	TEST_CHECK(msg_keys[5]);
	if (msg_keys[5]) TEST_EQUAL(msg_keys[5].string_value(), "test2");
	TEST_CHECK(msg_keys[6]);
	if (msg_keys[6]) TEST_EQUAL(msg_keys[6].string_value(), "test3");


	char const test_msg3[] = "d1:Cd2:C15:test22:C25:test3ee";
	bdecode(test_msg3, test_msg3 + sizeof(test_msg3)-1, ent, ec);
	std::printf("%s\n", print_entry(ent).c_str());

	ret = verify_message(ent, msg_desc, msg_keys, error_string);
	TEST_CHECK(!ret);
	std::printf("%s\n", error_string);
	TEST_EQUAL(error_string, std::string("missing 'A' key"));

	char const test_msg4[] = "d1:A6:foobare";
	bdecode(test_msg4, test_msg4 + sizeof(test_msg4)-1, ent, ec);
	std::printf("%s\n", print_entry(ent).c_str());

	ret = verify_message(ent, msg_desc, msg_keys, error_string);
	TEST_CHECK(!ret);
	std::printf("%s\n", error_string);
	TEST_EQUAL(error_string, std::string("invalid value for 'A'"));

	char const test_msg5[] = "d1:A4:test1:Cd2:C15:test2ee";
	bdecode(test_msg5, test_msg5 + sizeof(test_msg5)-1, ent, ec);
	std::printf("%s\n", print_entry(ent).c_str());

	ret = verify_message(ent, msg_desc, msg_keys, error_string);
	TEST_CHECK(!ret);
	std::printf("%s\n", error_string);
	TEST_EQUAL(error_string, std::string("missing 'C2' key"));

	// test empty strings [ { "":1 }, "" ]
	char const test_msg6[] = "ld0:i1ee0:e";
	bdecode(test_msg6, test_msg6 + sizeof(test_msg6)-1, ent, ec);
	std::printf("%s\n", print_entry(ent).c_str());
	TEST_CHECK(ent.type() == bdecode_node::list_t);
	if (ent.type() == bdecode_node::list_t)
	{
		TEST_CHECK(ent.list_size() == 2);
		if (ent.list_size() == 2)
		{
			TEST_CHECK(ent.list_at(0).dict_find_int_value("") == 1);
			TEST_CHECK(ent.list_at(1).string_value() == "");
		}
	}
}

TORRENT_TEST(routing_table_uniform)
{
	// test routing table
	auto sett = test_settings();
	obs observer;

	sett.set_bool(settings_pack::dht_extended_routing_table, false);
	// it's difficult to generate valid nodes with specific node IDs, so just
	// turn off that check
	sett.set_bool(settings_pack::dht_prefer_verified_node_ids, false);
	node_id id = to_hash("1234876923549721020394873245098347598635");
	node_id diff = to_hash("15764f7459456a9453f8719b09547c11d5f34061");

	routing_table tbl(id, udp::v4(), 8, sett, &observer);

	// insert 256 nodes evenly distributed across the ID space.
	// we expect to fill the top 5 buckets
	for (int i = 255; i >= 0; --i)
	{
		// test a node with the same IP:port changing ID
		add_and_replace(id, diff);
		// in order to make this node-load a bit more realistic, start from
		// distant nodes and work our way in closer to the node id
		// the routing table will reject nodes that are too imbalanced (if all
		// nodes are very close to our ID and none are far away, it's
		// suspicious).
		id[0] ^= i;
		tbl.node_seen(id, rand_udp_ep(), 20 + (id[19] & 0xff));

		// restore the first byte of the node ID
		id[0] ^= i;
	}
	std::printf("num_active_buckets: %d\n", tbl.num_active_buckets());
	// number of nodes per tree level (when adding 256 evenly distributed
	// nodes):
	// 0: 128
	// 1: 64
	// 2: 32
	// 3: 16
	// 4: 8
	// i.e. no more than 5 levels
	TEST_EQUAL(tbl.num_active_buckets(), 6);

	print_state(std::cout, tbl);
}

TORRENT_TEST(routing_table_balance)
{
	auto sett = test_settings();
	obs observer;

	sett.set_bool(settings_pack::dht_extended_routing_table, false);
	sett.set_bool(settings_pack::dht_prefer_verified_node_ids, false);
	node_id id = to_hash("1234876923549721020394873245098347598635");

	routing_table tbl(id, udp::v4(), 8, sett, &observer);

	// insert nodes in the routing table that will force it to split
	// and make sure we don't end up with a table completely out of balance
	for (int i = 0; i < 32; ++i)
	{
		id[0] = (i << 3) & 0xff;
		tbl.node_seen(id, rand_udp_ep(), 20 + (id[19] & 0xff));
	}
	std::printf("num_active_buckets: %d\n", tbl.num_active_buckets());
	TEST_EQUAL(tbl.num_active_buckets(), 2);

	print_state(std::cout, tbl);
}

TORRENT_TEST(routing_table_extended)
{
	auto sett = test_settings();
	obs observer;
	sett.set_bool(settings_pack::dht_extended_routing_table, true);
	sett.set_bool(settings_pack::dht_prefer_verified_node_ids, false);
	node_id id = to_hash("1234876923549721020394873245098347598635");
	node_id diff = to_hash("15764f7459456a9453f8719b09547c11d5f34061");

	// we can't add the nodes in straight 0,1,2,3 order. That way the routing
	// table would get unbalanced and intermediate nodes would be dropped
	std::vector<std::uint8_t> node_id_prefix;
	node_id_prefix.reserve(256);
	for (int i = 0; i < 256; ++i) node_id_prefix.push_back(i & 0xff);
	aux::random_shuffle(node_id_prefix);

	routing_table tbl(id, udp::v4(), 8, sett, &observer);
	for (std::size_t i = 0; i < 256; ++i)
	{
		add_and_replace(id, diff);
		id[0] = node_id_prefix[i];
		tbl.node_seen(id, rand_udp_ep(), 20 + (id[19] & 0xff));
	}
	TEST_EQUAL(tbl.num_active_buckets(), 6);

	print_state(std::cout, tbl);
}

namespace {
void inserter(std::set<node_id>* nodes, node_entry const& ne)
{
	nodes->insert(nodes->begin(), ne.id);
}
} // anonymous namespace

TORRENT_TEST(routing_table_set_id)
{
	auto sett = test_settings();
	sett.set_bool(settings_pack::dht_enforce_node_id, false);
	sett.set_bool(settings_pack::dht_extended_routing_table, false);
	sett.set_bool(settings_pack::dht_prefer_verified_node_ids, false);
	obs observer;
	node_id id = to_hash("0000000000000000000000000000000000000000");

	// we can't add the nodes in straight 0,1,2,3 order. That way the routing
	// table would get unbalanced and intermediate nodes would be dropped
	std::vector<std::uint8_t> node_id_prefix;
	node_id_prefix.reserve(256);
	for (int i = 0; i < 256; ++i) node_id_prefix.push_back(i & 0xff);
	aux::random_shuffle(node_id_prefix);
	routing_table tbl(id, udp::v4(), 8, sett, &observer);
	for (std::size_t i = 0; i < 256; ++i)
	{
		id[0] = node_id_prefix[i];
		tbl.node_seen(id, rand_udp_ep(), 20 + (id[19] & 0xff));
	}
	TEST_EQUAL(tbl.num_active_buckets(), 6);

	std::set<node_id> original_nodes;
	tbl.for_each_node(std::bind(&inserter, &original_nodes, _1));

	print_state(std::cout, tbl);

	id = to_hash("ffffffffffffffffffffffffffffffffffffffff");

	tbl.update_node_id(id);

	TEST_CHECK(tbl.num_active_buckets() <= 4);
	std::set<node_id> remaining_nodes;
	tbl.for_each_node(std::bind(&inserter, &remaining_nodes, _1));

	std::set<node_id> intersection;
	std::set_intersection(remaining_nodes.begin(), remaining_nodes.end()
		, original_nodes.begin(), original_nodes.end()
		, std::inserter(intersection, intersection.begin()));

	// all remaining nodes also exist in the original nodes
	TEST_EQUAL(intersection.size(), remaining_nodes.size());

	print_state(std::cout, tbl);
}

TORRENT_TEST(routing_table_for_each)
{
	auto sett = test_settings();
	obs observer;

	sett.set_bool(settings_pack::dht_extended_routing_table, false);
	sett.set_bool(settings_pack::dht_prefer_verified_node_ids, false);
	node_id id = to_hash("1234876923549721020394873245098347598635");

	routing_table tbl(id, udp::v4(), 2, sett, &observer);

	for (int i = 0; i < 32; ++i)
	{
		id[0] = (i << 3) & 0xff;
		tbl.node_seen(id, rand_udp_ep(), 20 + (id[19] & 0xff));
	}

	int nodes;
	int replacements;
	std::tie(nodes, replacements, std::ignore) = tbl.size();

	std::printf("num_active_buckets: %d\n", tbl.num_active_buckets());
	std::printf("live nodes: %d\n", nodes);
	std::printf("replacements: %d\n", replacements);

	TEST_EQUAL(tbl.num_active_buckets(), 2);
	TEST_EQUAL(nodes, 4);
	TEST_EQUAL(replacements, 4);

	print_state(std::cout, tbl);

	std::vector<node_entry> v;
	tbl.for_each_node([&](node_entry const& e) { v.push_back(e); }, nullptr);
	TEST_EQUAL(v.size(), 4);
	v.clear();
	tbl.for_each_node(nullptr, [&](node_entry const& e) { v.push_back(e); });
	TEST_EQUAL(v.size(), 4);
	v.clear();
	tbl.for_each_node([&](node_entry const& e) { v.push_back(e); });
	TEST_EQUAL(v.size(), 8);
}

TORRENT_TEST(node_set_id)
{
	dht_test_setup t(udp::endpoint(rand_v4(), 20));
	node_id old_nid = t.dht_node.nid();
	// put in a few votes to make sure the address really changes
	for (int i = 0; i < 25; ++i)
		t.observer.set_external_address(aux::listen_socket_handle(t.ls), addr4("237.0.0.1"), rand_v4());
	t.dht_node.update_node_id();
	TEST_CHECK(old_nid != t.dht_node.nid());
	// now that we've changed the node's id,  make sure the id sent in outgoing messages
	// reflects the change

	bdecode_node response;
	send_dht_request(t.dht_node, "ping", t.source, &response);

	dht::key_desc_t const pong_desc[] = {
		{ "y", bdecode_node::string_t, 1, 0 },
		{ "t", bdecode_node::string_t, 2, 0 },
		{ "r", bdecode_node::dict_t, 0, key_desc_t::parse_children },
		{ "id", bdecode_node::string_t, 20, key_desc_t::last_child },
	};
	bdecode_node pong_keys[4];
	bool ret = dht::verify_message(response, pong_desc, pong_keys, t.error_string);
	TEST_CHECK(ret);
	if (!ret) return;

	TEST_EQUAL(node_id(pong_keys[3].string_ptr()), t.dht_node.nid());
}

TORRENT_TEST(read_only_node)
{
	// TODO: 3 use dht_test_setup class to simplify the node setup
	auto sett = test_settings();
	sett.set_bool(settings_pack::dht_read_only, true);
	mock_socket s;
	auto ls = dummy_listen_socket4();
	obs observer;
	counters cnt;

	std::unique_ptr<dht_storage_interface> dht_storage(dht_default_storage_constructor(sett));
	dht_storage->update_node_ids({node_id(nullptr)});
	dht::node node(ls, &s, sett, node_id(nullptr), &observer, cnt, get_foreign_node_stub, *dht_storage);
	udp::endpoint source(addr("10.0.0.1"), 20);
	bdecode_node response;
	msg_args args;

	// for incoming requests, read_only node won't response.
	send_dht_request(node, "ping", source, &response, args, "10", false);
	TEST_EQUAL(response.type(), bdecode_node::none_t);

	args.target(sha1_hash("01010101010101010101"));
	send_dht_request(node, "get", source, &response, args, "10", false);
	TEST_EQUAL(response.type(), bdecode_node::none_t);

	// also, the sender shouldn't be added to routing table.
	TEST_EQUAL(std::get<0>(node.size()), 0);

	// for outgoing requests, read_only node will add 'ro' key (value == 1)
	// in top-level of request.
	bdecode_node parsed[7];
	char error_string[200];
	udp::endpoint initial_node(addr("4.4.4.4"), 1234);
	dht::node_id const initial_node_id = to_hash("1111111111222222222233333333334444444444");
	node.m_table.add_node(node_entry{initial_node_id, initial_node, 10, true});
	bdecode_node request;
	sha1_hash target = generate_next();

	node.get_item(target, get_immutable_item_cb);
	TEST_EQUAL(g_sent_packets.size(), 1);
	TEST_EQUAL(g_sent_packets.front().first, initial_node);

	dht::key_desc_t const get_item_desc_ro[] = {
		{"y", bdecode_node::string_t, 1, 0},
		{"t", bdecode_node::string_t, 2, 0},
		{"q", bdecode_node::string_t, 3, 0},
		{"ro", bdecode_node::int_t, 4, key_desc_t::optional},
		{"a", bdecode_node::dict_t, 0, key_desc_t::parse_children},
			{"id", bdecode_node::string_t, 20, 0},
			{"target", bdecode_node::string_t, 20, key_desc_t::last_child},
	};

	node_from_entry(g_sent_packets.front().second, request);
	bool ret = verify_message(request, get_item_desc_ro, parsed, error_string);

	TEST_CHECK(ret);
	TEST_EQUAL(parsed[3].int_value(), 1);

	// should have one node now, which is 4.4.4.4:1234
	TEST_EQUAL(std::get<0>(node.size()), 1);
	// and no replacement nodes
	TEST_EQUAL(std::get<1>(node.size()), 0);

	// now, disable read_only, try again.
	g_sent_packets.clear();
	sett.set_bool(settings_pack::dht_read_only, false);

	send_dht_request(node, "get", source, &response);
	// sender should be added to repacement bucket
	TEST_EQUAL(std::get<1>(node.size()), 1);

	g_sent_packets.clear();
#if 0
	// TODO: this won't work because the second node isn't pinged so it wont
	// be added to the routing table
	target = generate_next();
	node.get_item(target, get_immutable_item_cb);

	// since we have 2 nodes, we should have two packets.
	TEST_EQUAL(g_sent_packets.size(), 2);

	// both of them shouldn't have a 'ro' key.
	node_from_entry(g_sent_packets.front().second, request);
	ret = verify_message(request, get_item_desc_ro, parsed, error_string);

	TEST_CHECK(ret);
	TEST_CHECK(!parsed[3]);

	node_from_entry(g_sent_packets.back().second, request);
	ret = verify_message(request, get_item_desc_ro, parsed, error_string);

	TEST_CHECK(ret);
	TEST_CHECK(!parsed[3]);
#endif
}

#ifndef TORRENT_DISABLE_LOGGING
// these tests rely on logging being enabled

TORRENT_TEST(invalid_error_msg)
{
	// TODO: 3 use dht_test_setup class to simplify the node setup
	auto sett = test_settings();
	mock_socket s;
	auto ls = dummy_listen_socket4();
	obs observer;
	counters cnt;

	std::unique_ptr<dht_storage_interface> dht_storage(dht_default_storage_constructor(sett));
	dht_storage->update_node_ids({node_id(nullptr)});
	dht::node node(ls, &s, sett, node_id(nullptr), &observer, cnt, get_foreign_node_stub, *dht_storage);
	udp::endpoint source(addr("10.0.0.1"), 20);

	entry e;
	e["y"] = "e";
	e["e"].string() = "Malformed Error";
	char msg_buf[1500];
	int size = bencode(msg_buf, e);

	bdecode_node decoded;
	error_code ec;
	bdecode(msg_buf, msg_buf + size, decoded, ec);
	if (ec) std::printf("bdecode failed: %s\n", ec.message().c_str());

	dht::msg m(decoded, source);
	node.incoming(node.m_sock, m);

	bool found = false;
	for (auto const& log : observer.m_log)
	{
		if (log.find("INCOMING ERROR") != std::string::npos
			&& log.find("(malformed)") != std::string::npos)
			found = true;

		std::printf("%s\n", log.c_str());
	}

	TEST_EQUAL(found, true);
}

struct test_algo : dht::traversal_algorithm
{
	test_algo(node& dht_node, node_id const& target)
		: traversal_algorithm(dht_node, target)
	{}

	void done() override { this->dht::traversal_algorithm::done(); }

	std::vector<observer_ptr> const& results() const { return m_results; }

	using traversal_algorithm::num_sorted_results;
};

TORRENT_TEST(unsorted_traversal_results)
{
	init_rand_address();

	// make sure the handling of an unsorted tail of nodes is correct in the
	// traversal algorithm. Initial nodes (that we bootstrap from) remain
	// unsorted, since we don't know their node IDs

	dht_test_setup t(udp::endpoint(rand_v4(), 20));

	node_id const our_id = t.dht_node.nid();
	auto algo = std::make_shared<test_algo>(t.dht_node, our_id);

	std::vector<udp::endpoint> eps;
	for (int i = 0; i < 10; ++i)
	{
		eps.push_back(rand_udp_ep(rand_v4));
		algo->add_entry(node_id(), eps.back(), observer::flag_initial);
	}

	// we should have 10 unsorted nodes now
	TEST_CHECK(algo->num_sorted_results() == 0);
	auto results = algo->results();
	TEST_CHECK(results.size() == eps.size());
	for (std::size_t i = 0; i < eps.size(); ++i)
		TEST_CHECK(eps[i] == results[i]->target_ep());

	// setting the node ID, regardless of what we set it to, should cause this
	// observer to become sorted. i.e. be moved to the beginning of the result
	// list.
	results[5]->set_id(node_id("abababababababababab"));

	TEST_CHECK(algo->num_sorted_results() == 1);
	results = algo->results();
	TEST_CHECK(results.size() == eps.size());
	TEST_CHECK(eps[5] == results[0]->target_ep());
	algo->done();
}

TORRENT_TEST(rpc_invalid_error_msg)
{
	// TODO: 3 use dht_test_setup class to simplify the node setup
	auto sett = test_settings();
	mock_socket s;
	auto ls = dummy_listen_socket4();
	obs observer;
	counters cnt;

	dht::routing_table table(node_id(), udp::v4(), 8, sett, &observer);
	dht::rpc_manager rpc(node_id(), sett, table, ls, &s, &observer);
	std::unique_ptr<dht_storage_interface> dht_storage(dht_default_storage_constructor(sett));
	dht_storage->update_node_ids({node_id(nullptr)});
	dht::node node(ls, &s, sett, node_id(nullptr), &observer, cnt, get_foreign_node_stub, *dht_storage);

	udp::endpoint source(addr("10.0.0.1"), 20);

	// we need this to create an entry for this transaction ID, otherwise the
	// incoming message will just be dropped
	entry req;
	req["y"] = "q";
	req["q"] = "bogus_query";
	req["t"] = "\0\0\0\0";

	g_sent_packets.clear();
	auto algo = std::make_shared<dht::traversal_algorithm>(node, node_id());

	auto o = rpc.allocate_observer<null_observer>(std::move(algo), source, node_id());
#if TORRENT_USE_ASSERTS
	o->m_in_constructor = false;
#endif
	rpc.invoke(req, source, o);

	// here's the incoming (malformed) error message
	entry err;
	err["y"] = "e";
	err["e"].string() = "Malformed Error";
	err["t"] = g_sent_packets.begin()->second["t"].string();
	char msg_buf[1500];
	int size = bencode(msg_buf, err);

	bdecode_node decoded;
	error_code ec;
	bdecode(msg_buf, msg_buf + size, decoded, ec);
	if (ec) std::printf("bdecode failed: %s\n", ec.message().c_str());

	dht::msg m(decoded, source);
	node_id nid;
	rpc.incoming(m, &nid);

	bool found = false;
	for (auto const& log : observer.m_log)
	{
		if (log.find("reply with") != std::string::npos
			&& log.find("(malformed)") != std::string::npos
			&& log.find("error") != std::string::npos)
			found = true;

		std::printf("%s\n", log.c_str());
	}

	TEST_EQUAL(found, true);
}
#endif

// test bucket distribution
TORRENT_TEST(node_id_bucket_distribution)
{
	init_rand_address();

	int nodes_per_bucket[160] = {0};
	dht::node_id reference_id = generate_id(rand_v4());
	int const num_samples = 100000;
	for (int i = 0; i < num_samples; ++i)
	{
		dht::node_id nid = generate_id(rand_v4());
		int const bucket = 159 - distance_exp(reference_id, nid);
		++nodes_per_bucket[bucket];
	}

	for (int i = 0; i < 25; ++i)
	{
		std::printf("%3d ", nodes_per_bucket[i]);
	}
	std::printf("\n");

	int expected = num_samples / 2;
	for (int i = 0; i < 25; ++i)
	{
		TEST_CHECK(std::abs(nodes_per_bucket[i] - expected) < num_samples / 20);
		expected /= 2;
	}
}

TORRENT_TEST(node_id_min_distance_exp)
{
	node_id const n1 = to_hash("0000000000000000000000000000000000000002");
	node_id const n2 = to_hash("0000000000000000000000000000000000000004");
	node_id const n3 = to_hash("0000000000000000000000000000000000000008");

	std::vector<node_id> ids;

	ids.push_back(n1);

	TEST_EQUAL(min_distance_exp(sha1_hash::min(), ids), 1);

	ids.push_back(n1);
	ids.push_back(n2);

	TEST_EQUAL(min_distance_exp(sha1_hash::min(), ids), 1);

	ids.push_back(n1);
	ids.push_back(n2);
	ids.push_back(n3);

	TEST_EQUAL(min_distance_exp(sha1_hash::min(), ids), 1);

	ids.clear();
	ids.push_back(n3);
	ids.push_back(n2);
	ids.push_back(n2);

	TEST_EQUAL(min_distance_exp(sha1_hash::min(), ids), 2);
}

TORRENT_TEST(dht_verify_node_address)
{
	obs observer;
	// initial setup taken from dht test above
	aux::session_settings s;
	s.set_bool(settings_pack::dht_extended_routing_table, false);
	node_id id = to_hash("3123456789abcdef01232456789abcdef0123456");
	const int bucket_size = 8;
	dht::routing_table table(id, udp::v4(), bucket_size, s, &observer);
	std::vector<node_entry> nodes;
	TEST_EQUAL(std::get<0>(table.size()), 0);

	node_id tmp = id;
	node_id diff = to_hash("15764f7459456a9453f8719b09547c11d5f34061");

	add_and_replace(tmp, diff);
	table.node_seen(tmp, udp::endpoint(addr("4.4.4.4"), 4), 10);
	nodes = table.find_node(id, {}, 10);
	TEST_EQUAL(std::get<0>(table.size()), 1);
	TEST_EQUAL(nodes.size(), 1);

	// incorrect data, wrong IP
	table.node_seen(tmp
		, udp::endpoint(addr("4.4.4.6"), 4), 10);
	nodes = table.find_node(id, {}, 10);

	TEST_EQUAL(std::get<0>(table.size()), 1);
	TEST_EQUAL(nodes.size(), 1);

	// incorrect data, wrong id, should cause node to be removed
	table.node_seen(to_hash("0123456789abcdef01232456789abcdef0123456")
		, udp::endpoint(addr("4.4.4.4"), 4), 10);
	nodes = table.find_node(id, {}, 10);

	TEST_EQUAL(std::get<0>(table.size()), 0);
	TEST_EQUAL(nodes.size(), 0);
}

TORRENT_TEST(generate_prefix_mask)
{
	std::vector<std::pair<int, char const*>> const test = {
		{   0, "0000000000000000000000000000000000000000" },
		{   1, "8000000000000000000000000000000000000000" },
		{   2, "c000000000000000000000000000000000000000" },
		{  11, "ffe0000000000000000000000000000000000000" },
		{  17, "ffff800000000000000000000000000000000000" },
		{  37, "fffffffff8000000000000000000000000000000" },
		{ 160, "ffffffffffffffffffffffffffffffffffffffff" },
	};

	for (auto const& i : test)
	{
		TEST_EQUAL(generate_prefix_mask(i.first), to_hash(i.second));
	}
}

TORRENT_TEST(distance_exp)
{
	// distance_exp


	using tst = std::tuple<char const*, char const*, int>;

	std::vector<std::tuple<char const*, char const*, int>> const distance_tests = {
		tst{ "ffffffffffffffffffffffffffffffffffffffff"
		, "0000000000000000000000000000000000000000", 159 },

		tst{ "ffffffffffffffffffffffffffffffffffffffff"
		, "7fffffffffffffffffffffffffffffffffffffff", 159 },

		tst{ "ffffffffffffffffffffffffffffffffffffffff"
		, "ffffffffffffffffffffffffffffffffffffffff", 0 },

		tst{ "ffffffffffffffffffffffffffffffffffffffff"
		, "fffffffffffffffffffffffffffffffffffffffe", 0 },

		tst{ "8000000000000000000000000000000000000000"
		, "fffffffffffffffffffffffffffffffffffffffe", 158 },

		tst{ "c000000000000000000000000000000000000000"
		, "fffffffffffffffffffffffffffffffffffffffe", 157 },

		tst{ "e000000000000000000000000000000000000000"
		, "fffffffffffffffffffffffffffffffffffffffe", 156 },

		tst{ "f000000000000000000000000000000000000000"
		, "fffffffffffffffffffffffffffffffffffffffe", 155 },

		tst{ "f8f2340985723049587230495872304958703294"
		, "f743589043r890f023980f90e203980d090c3840", 155 },

		tst{ "ffff740985723049587230495872304958703294"
		, "ffff889043r890f023980f90e203980d090c3840", 159 - 16 },
	};

	for (auto const& t : distance_tests)
	{
		std::printf("%s %s: %d\n"
			, std::get<0>(t), std::get<1>(t), std::get<2>(t));

		TEST_EQUAL(distance_exp(to_hash(std::get<0>(t))
				, to_hash(std::get<1>(t))), std::get<2>(t));
	}
}

TORRENT_TEST(compare_ip_cidr)
{
	using tst = std::tuple<char const*, char const*, bool>;
	std::vector<tst> const v4tests = {
		tst{"10.255.255.0", "10.255.255.255", true},
		tst{"11.0.0.0", "10.255.255.255", false},
		tst{"0.0.0.0", "128.255.255.255", false},
		tst{"0.0.0.0", "127.255.255.255", false},
		tst{"255.255.255.0", "255.255.255.255", true},
		tst{"255.254.255.0", "255.255.255.255", false},
		tst{"0.0.0.0", "0.0.0.0", true},
		tst{"255.255.255.255", "255.255.255.255", true},
	};

	for (auto const& t : v4tests)
	{
		std::printf("%s %s\n", std::get<0>(t), std::get<1>(t));
		TEST_EQUAL(compare_ip_cidr(
			addr4(std::get<0>(t)), addr4(std::get<1>(t))), std::get<2>(t));
	}

	std::vector<tst> const v6tests = {
		tst{"::1", "::ffff:ffff:ffff:ffff", true},
		tst{"::2:0000:0000:0000:0000", "::1:ffff:ffff:ffff:ffff", false},
		tst{"::ff:0000:0000:0000:0000", "::ffff:ffff:ffff:ffff", false},
		tst{"::caca:0000:0000:0000:0000", "::ffff:ffff:ffff:ffff:ffff", false},
		tst{"::a:0000:0000:0000:0000", "::b:ffff:ffff:ffff:ffff", false},
		tst{"::7f:0000:0000:0000:0000", "::ffff:ffff:ffff:ffff", false},
		tst{"7f::", "ff::", false},
		tst{"ff::", "ff::", true},
		tst{"::", "::", true},
		tst{"ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", true},
	};

	for (auto const& t : v6tests)
	{
		TEST_EQUAL(compare_ip_cidr(
			addr6(std::get<0>(t)), addr6(std::get<1>(t))), std::get<2>(t));
	}
}

TORRENT_TEST(dht_state)
{
	dht_state s;

	s.nids.emplace_back(make_address("1.1.1.1"), to_hash("0000000000000000000000000000000000000001"));
	s.nodes.push_back(uep("1.1.1.1", 1));
	s.nodes.push_back(uep("2.2.2.2", 2));
	// remove these for now because they will only get used if the host system has IPv6 support
	// hopefully in the future we can rely on the test system supporting IPv6
	//s.nids.emplace_back(make_address("1::1"), to_hash("0000000000000000000000000000000000000002"));
	//s.nodes6.push_back(uep("1::1", 3));
	//s.nodes6.push_back(uep("2::2", 4));

	entry const e = save_dht_state(s);

	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), e);

	bdecode_node n;
	error_code ec;
	int r = bdecode(&tmp[0], &tmp[0] + tmp.size(), n, ec);
	TEST_CHECK(!r);

	dht_state const s1 = read_dht_state(n);
	TEST_CHECK(s1.nids == s.nids);
	TEST_CHECK(s1.nodes == s.nodes);

	// empty
	bdecode_node n1;
	dht_state const s2 = read_dht_state(n1);
	TEST_CHECK(s2.nids.empty());
	TEST_CHECK(s2.nodes.empty());
}

TORRENT_TEST(sample_infohashes)
{
	init_rand_address();

	dht_test_setup t(rand_udp_ep());
	bdecode_node response;

	g_sent_packets.clear();

	node_id nid = generate_random_id();
	udp::endpoint initial_node = rand_udp_ep();
	t.dht_node.m_table.add_node(node_entry{initial_node});

	// nodes
	sha1_hash const h1 = rand_hash();
	sha1_hash const h2 = rand_hash();
	udp::endpoint const ep1 = rand_udp_ep(rand_v4);
	udp::endpoint const ep2 = rand_udp_ep(rand_v4);

	t.dht_node.sample_infohashes(initial_node, items[0].target,
		[nid, h1, ep1, h2, ep2](node_id const& node_id
			, time_duration interval
			, int num
			, std::vector<sha1_hash> samples
			, std::vector<std::pair<sha1_hash, udp::endpoint>> const& nodes)
	{
		TEST_EQUAL(node_id, nid);
		TEST_EQUAL(total_seconds(interval), 10);
		TEST_EQUAL(num, 2);
		TEST_EQUAL(samples.size(), 1);
		TEST_EQUAL(samples[0], to_hash("1000000000000000000000000000000000000001"));
		TEST_EQUAL(nodes.size(), 2);
		TEST_EQUAL(nodes[0].first, h1);
		TEST_EQUAL(nodes[0].second, ep1);
		TEST_EQUAL(nodes[1].first, h2);
		TEST_EQUAL(nodes[1].second, ep2);
	});

	TEST_EQUAL(g_sent_packets.size(), 1);
	if (g_sent_packets.empty()) return;
	TEST_EQUAL(g_sent_packets.front().first, initial_node);

	node_from_entry(g_sent_packets.front().second, response);
	bdecode_node sample_infohashes_keys[6];
	bool const ret = verify_message(response
		, sample_infohashes_desc, sample_infohashes_keys, t.error_string);
	if (ret)
	{
		TEST_EQUAL(sample_infohashes_keys[0].string_value(), "q");
		TEST_EQUAL(sample_infohashes_keys[2].string_value(), "sample_infohashes");
		TEST_EQUAL(sample_infohashes_keys[5].string_value(), items[0].target.to_string());
	}
	else
	{
		std::printf("   invalid sample_infohashes request: %s\n", print_entry(response).c_str());
		TEST_ERROR(t.error_string);
		return;
	}

	std::vector<node_entry> nodes;
	nodes.emplace_back(h1, ep1);
	nodes.emplace_back(h2, ep2);

	g_sent_packets.clear();
	send_dht_response(t.dht_node, response, initial_node
		, msg_args()
			.nid(nid)
			.interval(seconds(10))
			.num(2)
			.samples({to_hash("1000000000000000000000000000000000000001")})
			.nodes(nodes));

	TEST_CHECK(g_sent_packets.empty());
}

namespace {
node_entry fake_node(bool verified, int rtt = 0)
{
	node_entry e(rand_udp_ep());
	e.verified = verified;
	e.rtt = static_cast<std::uint16_t>(rtt);
	return e;
}
}

TORRENT_TEST(node_entry_comparison)
{
	// being verified or not always trumps RTT in sort order
	TEST_CHECK(fake_node(true, 10) < fake_node(false, 5));
	TEST_CHECK(fake_node(true, 5) < fake_node(false, 10));
	TEST_CHECK(!(fake_node(false, 10) < fake_node(true, 5)));
	TEST_CHECK(!(fake_node(false, 5) < fake_node(true, 10)));

	// if both are verified, lower RTT is better
	TEST_CHECK(fake_node(true, 5) < fake_node(true, 10));
	TEST_CHECK(!(fake_node(true, 10) < fake_node(true, 5)));

	// if neither are verified, lower RTT is better
	TEST_CHECK(fake_node(false, 5) < fake_node(false, 10));
	TEST_CHECK(!(fake_node(false, 10) < fake_node(false, 5)));
}

TORRENT_TEST(mostly_verified_nodes)
{
	// an empty bucket is OK
	TEST_CHECK(mostly_verified_nodes({}));
	TEST_CHECK(mostly_verified_nodes({fake_node(true)}));
	TEST_CHECK(mostly_verified_nodes({fake_node(true), fake_node(false)}));
	TEST_CHECK(mostly_verified_nodes({fake_node(true), fake_node(true), fake_node(false)}));
	TEST_CHECK(mostly_verified_nodes({fake_node(true), fake_node(true), fake_node(true), fake_node(false)}));

	// a large bucket with only half of the nodes verified, does not count as
	// "mostly"
	TEST_CHECK(!mostly_verified_nodes({fake_node(true), fake_node(false)
		, fake_node(true), fake_node(false)
		, fake_node(true), fake_node(false)
		, fake_node(true), fake_node(false)
		, fake_node(true), fake_node(false)
		, fake_node(true), fake_node(false)}));

	// 1 of 3 is not "mostly"
	TEST_CHECK(!mostly_verified_nodes({fake_node(false), fake_node(true), fake_node(false)}));

	TEST_CHECK(!mostly_verified_nodes({fake_node(false)}));
	TEST_CHECK(!mostly_verified_nodes({fake_node(false), fake_node(false)}));
	TEST_CHECK(!mostly_verified_nodes({fake_node(false), fake_node(false), fake_node(false)}));
}

TORRENT_TEST(classify_prefix)
{
	// the last bucket in the routing table
	TEST_EQUAL(int(classify_prefix(0, true, 8, to_hash("0cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 0);
	TEST_EQUAL(int(classify_prefix(0, true, 8, to_hash("2cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 1);
	TEST_EQUAL(int(classify_prefix(0, true, 8, to_hash("4cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 2);
	TEST_EQUAL(int(classify_prefix(0, true, 8, to_hash("6cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 3);
	TEST_EQUAL(int(classify_prefix(0, true, 8, to_hash("8cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 4);
	TEST_EQUAL(int(classify_prefix(0, true, 8, to_hash("acdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 5);
	TEST_EQUAL(int(classify_prefix(0, true, 8, to_hash("ccdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 6);
	TEST_EQUAL(int(classify_prefix(0, true, 8, to_hash("ecdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 7);
	TEST_EQUAL(int(classify_prefix(0, true, 8, to_hash("fcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 7);

	TEST_EQUAL(int(classify_prefix(4, true, 8, to_hash("c0cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 0);
	TEST_EQUAL(int(classify_prefix(4, true, 8, to_hash("c2cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 1);
	TEST_EQUAL(int(classify_prefix(4, true, 8, to_hash("c4cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 2);
	TEST_EQUAL(int(classify_prefix(4, true, 8, to_hash("c6cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 3);
	TEST_EQUAL(int(classify_prefix(4, true, 8, to_hash("c8cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 4);
	TEST_EQUAL(int(classify_prefix(4, true, 8, to_hash("cacdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 5);
	TEST_EQUAL(int(classify_prefix(4, true, 8, to_hash("cccdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 6);
	TEST_EQUAL(int(classify_prefix(4, true, 8, to_hash("cecdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 7);
	TEST_EQUAL(int(classify_prefix(4, true, 8, to_hash("cfcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 7);

	TEST_EQUAL(int(classify_prefix(8, true, 8, to_hash("dc0cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 0);
	TEST_EQUAL(int(classify_prefix(8, true, 8, to_hash("dc2cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 1);
	TEST_EQUAL(int(classify_prefix(8, true, 8, to_hash("dc4cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 2);
	TEST_EQUAL(int(classify_prefix(8, true, 8, to_hash("dc6cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 3);
	TEST_EQUAL(int(classify_prefix(8, true, 8, to_hash("dc8cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 4);
	TEST_EQUAL(int(classify_prefix(8, true, 8, to_hash("dcacdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 5);
	TEST_EQUAL(int(classify_prefix(8, true, 8, to_hash("dcccdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 6);
	TEST_EQUAL(int(classify_prefix(8, true, 8, to_hash("dcecdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 7);
	TEST_EQUAL(int(classify_prefix(8, true, 8, to_hash("dcfcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc"))), 7);

	TEST_EQUAL(int(classify_prefix(12, true, 8, to_hash("cdc0cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 0);
	TEST_EQUAL(int(classify_prefix(12, true, 8, to_hash("cdc2cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 1);
	TEST_EQUAL(int(classify_prefix(12, true, 8, to_hash("cdc4cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 2);
	TEST_EQUAL(int(classify_prefix(12, true, 8, to_hash("cdc6cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 3);
	TEST_EQUAL(int(classify_prefix(12, true, 8, to_hash("cdc8cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 4);
	TEST_EQUAL(int(classify_prefix(12, true, 8, to_hash("cdcacdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 5);
	TEST_EQUAL(int(classify_prefix(12, true, 8, to_hash("cdcccdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 6);
	TEST_EQUAL(int(classify_prefix(12, true, 8, to_hash("cdcecdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 7);
	TEST_EQUAL(int(classify_prefix(12, true, 8, to_hash("cdcfcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 7);

	// not the last bucket in the routing table
	TEST_EQUAL(int(classify_prefix(11, false, 8, to_hash("cdc0cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 0);
	TEST_EQUAL(int(classify_prefix(11, false, 8, to_hash("cdc2cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 1);
	TEST_EQUAL(int(classify_prefix(11, false, 8, to_hash("cdc4cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 2);
	TEST_EQUAL(int(classify_prefix(11, false, 8, to_hash("cdc6cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 3);
	TEST_EQUAL(int(classify_prefix(11, false, 8, to_hash("cdc8cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 4);
	TEST_EQUAL(int(classify_prefix(11, false, 8, to_hash("cdcacdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 5);
	TEST_EQUAL(int(classify_prefix(11, false, 8, to_hash("cdcccdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 6);
	TEST_EQUAL(int(classify_prefix(11, false, 8, to_hash("cdcecdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 7);
	TEST_EQUAL(int(classify_prefix(11, false, 8, to_hash("cdcfcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 7);

	TEST_EQUAL(int(classify_prefix(12, false, 8, to_hash("cdc8cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 0);
	TEST_EQUAL(int(classify_prefix(12, false, 8, to_hash("cdc9cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 1);
	TEST_EQUAL(int(classify_prefix(12, false, 8, to_hash("cdcacdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 2);
	TEST_EQUAL(int(classify_prefix(12, false, 8, to_hash("cdcbcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 3);
	TEST_EQUAL(int(classify_prefix(12, false, 8, to_hash("cdcccdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 4);
	TEST_EQUAL(int(classify_prefix(12, false, 8, to_hash("cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 5);
	TEST_EQUAL(int(classify_prefix(12, false, 8, to_hash("cdcecdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 6);
	TEST_EQUAL(int(classify_prefix(12, false, 8, to_hash("cdcfcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 7);
	TEST_EQUAL(int(classify_prefix(12, false, 8, to_hash("cdc7cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 7);

	// larger bucket
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdc0cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 0);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdc1cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 1);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdc2cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 2);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdc3cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 3);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdc4cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 4);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdc5cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 5);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdc6cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 6);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdc7cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 7);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdc8cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 8);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdc9cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 9);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdcacdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 10);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdcbcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 11);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdcccdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 12);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 13);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdcecdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 14);
	TEST_EQUAL(int(classify_prefix(12, true, 16, to_hash("cdcfcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"))), 15);
}

namespace {
node_entry n(ip_set* ips, char const* nid, bool verified = true, int rtt = 0, int failed = 0)
{
	node_entry e(rand_udp_ep());
	if (ips) ips->insert(e.addr());
	e.verified = verified;
	e.rtt = static_cast<std::uint16_t>(rtt);
	e.id = to_hash(nid);
	if (failed != 0) e.timeout_count = static_cast<std::uint8_t>(failed);
	return e;
}
}

#ifndef TORRENT_DISABLE_LOGGING
#define LOGGER , nullptr
#else
#define LOGGER
#endif
TORRENT_TEST(replace_node_impl)
{
	// replace specific prefix "slot"
	{
	ip_set p;
	dht::bucket_t b = {
		n(&p, "1fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "3fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "5fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "7fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "9fffffffffffffffffffffffffffffffffffffff", true, 50), // <== replaced
		n(&p, "bfffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "dfffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "ffffffffffffffffffffffffffffffffffffffff", true, 50),
	};
	TEST_EQUAL(p.size(), 8);
	TEST_CHECK(
		replace_node_impl(n(nullptr, "9fcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd")
	, b, p, 0, 8, true LOGGER) == routing_table::node_added);
	TEST_CHECK(b[4].id == to_hash("9fcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));
	TEST_EQUAL(p.size(), 8);
	}

	// only try to replace specific prefix "slot", and if we fail (RTT is
	// higher), don't replace anything else
	{
	ip_set p;
	dht::bucket_t b = {
		n(&p, "1fffffffffffffffffffffffffffffffffffffff", true, 500),
		n(&p, "3fffffffffffffffffffffffffffffffffffffff", true, 500),
		n(&p, "5fffffffffffffffffffffffffffffffffffffff", true, 500),
		n(&p, "7fffffffffffffffffffffffffffffffffffffff", true, 500),
		n(&p, "9fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "bfffffffffffffffffffffffffffffffffffffff", true, 500),
		n(&p, "dfffffffffffffffffffffffffffffffffffffff", true, 500),
		n(&p, "ffffffffffffffffffffffffffffffffffffffff", true, 500),
	};
	TEST_EQUAL(p.size(), 8);
	TEST_CHECK(
		replace_node_impl(n(nullptr, "9fcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd", true, 100)
	, b, p, 0, 8, true LOGGER) != routing_table::node_added);
	TEST_CHECK(b[4].id == to_hash("9fffffffffffffffffffffffffffffffffffffff"));
	TEST_EQUAL(p.size(), 8);
	}

	// if there are multiple candidates to replace, pick the one with the highest
	// RTT. We're picking the prefix slots with duplicates
	{
	ip_set p;
	dht::bucket_t b = {
		n(&p, "1fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "3fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "5fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "7fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "bfffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "bfffffffffffffffffffffffffffffffffffffff", true, 51), // <== replaced
		n(&p, "dfffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "ffffffffffffffffffffffffffffffffffffffff", true, 50),
	};
	TEST_EQUAL(p.size(), 8);
	TEST_CHECK(
		replace_node_impl(n(nullptr, "9fcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd", true, 50)
	, b, p, 0, 8, true LOGGER) == routing_table::node_added);
	TEST_CHECK(b[5].id == to_hash("9fcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));
	TEST_EQUAL(p.size(), 8);
	}

	// if there is a node with fail count > 0, replaec that, regardless of
	// anything else
	{
	ip_set p;
	dht::bucket_t b = {
		n(&p, "1fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "3fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "5fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "7fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "9fffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "bfffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "dfffffffffffffffffffffffffffffffffffffff", true, 50),
		n(&p, "ffffffffffffffffffffffffffffffffffffffff", true, 50, 1), // <== replaced
	};
	TEST_EQUAL(p.size(), 8);
	TEST_CHECK(
		replace_node_impl(n(nullptr, "9fcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd", true, 50)
	, b, p, 0, 8, true LOGGER) == routing_table::node_added);
	TEST_CHECK(b[7].id == to_hash("9fcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));
	TEST_EQUAL(p.size(), 8);
	}
}

TORRENT_TEST(all_in_same_bucket)
{
	TEST_CHECK(all_in_same_bucket({}, to_hash("8000000000000000000000000000000000000000"), 0) == true);
	TEST_CHECK(all_in_same_bucket({}, to_hash("8000000000000000000000000000000000000001"), 1) == true);
	TEST_CHECK(all_in_same_bucket({}, to_hash("8000000000000000000000000000000000000002"), 2) == true);
	TEST_CHECK(all_in_same_bucket({}, to_hash("8000000000000000000000000000000000000003"), 3) == true);
	{
		dht::bucket_t b = {
			n(nullptr, "0000000000000000000000000000000000000000"),
		};
		TEST_CHECK(all_in_same_bucket(b, to_hash("8000000000000000000000000000000000000000"), 0) == false);
	}

	{
		dht::bucket_t b = {
			n(nullptr, "8000000000000000000000000000000000000000"),
			n(nullptr, "f000000000000000000000000000000000000000"),
		};
		TEST_CHECK(all_in_same_bucket(b, to_hash("8000000000000000000000000000000000000000"), 0) == true);
	}
	{
		dht::bucket_t b = {
			n(nullptr, "8000000000000000000000000000000000000000"),
			n(nullptr, "0000000000000000000000000000000000000000"),
		};
		TEST_CHECK(all_in_same_bucket(b, to_hash("8000000000000000000000000000000000000000"), 0) == false);
	}
	{
		dht::bucket_t b = {
			n(nullptr, "0800000000000000000000000000000000000000"),
			n(nullptr, "0000000000000000000000000000000000000000"),
		};
		TEST_CHECK(all_in_same_bucket(b, to_hash("0800000000000000000000000000000000000000"), 4) == false);
	}
	{
		dht::bucket_t b = {
			n(nullptr, "0800000000000000000000000000000000000000"),
			n(nullptr, "0800000000000000000000000000000000000000"),
		};

		TEST_CHECK(all_in_same_bucket(b, to_hash("0800000000000000000000000000000000000000"), 4) == true);
	}
	{
		dht::bucket_t b = {
			n(nullptr, "0007000000000000000000000000000000000000"),
			n(nullptr, "0004000000000000000000000000000000000000"),
		};

		TEST_CHECK(all_in_same_bucket(b, to_hash("0005000000000000000000000000000000000000"), 13) == true);
	}
}

namespace {

void test_rate_limit(aux::session_settings const& sett, std::function<void(lt::dht::socket_manager&)> f)
{
	io_context ios;
	obs observer;
	counters cnt;
	auto dht_storage = dht_default_storage_constructor(sett);
	dht_state s;

	lt::dht::dht_tracker::send_fun_t send;

	dht_tracker dht(&observer, ios, send, sett, cnt, *dht_storage, std::move(s));
	f(dht);
}
}

TORRENT_TEST(rate_limit_int_max)
{
	aux::session_settings sett;
	sett.set_int(settings_pack::dht_upload_rate_limit, std::numeric_limits<int>::max());

	test_rate_limit(sett, [](lt::dht::socket_manager& sm) {
		TEST_CHECK(sm.has_quota());
		std::this_thread::sleep_for(milliseconds(10));

		// *any* increment to the quote above INT_MAX may overflow. Ensure it
		// doesn't
		TEST_CHECK(sm.has_quota());
	});
}

TORRENT_TEST(rate_limit_large_delta)
{
	aux::session_settings sett;
	sett.set_int(settings_pack::dht_upload_rate_limit, std::numeric_limits<int>::max());

	test_rate_limit(sett, [](lt::dht::socket_manager& sm) {
		TEST_CHECK(sm.has_quota());
		std::this_thread::sleep_for(milliseconds(2500));

		// even though we have headroom in the limit itself, this long delay
		// would increment the quota past INT_MAX
		TEST_CHECK(sm.has_quota());
	});
}

TORRENT_TEST(rate_limit_accrue_limit)
{
	aux::session_settings sett;
	sett.set_int(settings_pack::dht_upload_rate_limit, std::numeric_limits<int>::max());

	test_rate_limit(sett, [](lt::dht::socket_manager& sm) {
		TEST_CHECK(sm.has_quota());
		for (int i = 0; i < 10; ++i)
		{
			std::this_thread::sleep_for(milliseconds(500));
			TEST_CHECK(sm.has_quota());
		}
	});
}


// TODO: test obfuscated_get_peers

#else
TORRENT_TEST(dht)
{
	// dummy dht test
	TEST_CHECK(true);
}

#endif
