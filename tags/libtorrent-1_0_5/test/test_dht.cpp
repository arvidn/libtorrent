/*

Copyright (c) 2008, Arvid Norberg
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

#ifndef TORRENT_DISABLE_DHT

#include "libtorrent/config.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/kademlia/node.hpp" // for verify_message
#include "libtorrent/bencode.hpp"
#include "libtorrent/socket_io.hpp" // for hash_address
#include "libtorrent/broadcast_socket.hpp" // for supports_ipv6
#include "libtorrent/alert_dispatcher.hpp"
#include "libtorrent/random.hpp"

#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/routing_table.hpp"
#include "libtorrent/kademlia/item.hpp"
#include "libtorrent/ed25519.hpp"
#include <numeric>

#include "test.hpp"
#include "setup_transfer.hpp"

#ifdef TORRENT_USE_VALGRIND
#include <valgrind/memcheck.h>
#endif

#if TORRENT_USE_IOSTREAM
#include <iostream>
#endif

using namespace libtorrent;
using namespace libtorrent::dht;

void nop() {}

sha1_hash to_hash(char const* s)
{
	sha1_hash ret;
	from_hex(s, 40, (char*)&ret[0]);
	return ret;
}

void add_and_replace(libtorrent::dht::node_id& dst, libtorrent::dht::node_id const& add)
{
	bool carry = false;
	for (int k = 19; k >= 0; --k)
	{
		int sum = dst[k] + add[k] + (carry?1:0);
		dst[k] = sum & 255;
		carry = sum > 255;
	}
}

void node_push_back(void* userdata, libtorrent::dht::node_entry const& n)
{
	using namespace libtorrent::dht;
	std::vector<node_entry>* nv = (std::vector<node_entry>*)userdata;
	nv->push_back(n);
}

void nop(void* userdata, libtorrent::dht::node_entry const& n) {}

std::list<std::pair<udp::endpoint, entry> > g_sent_packets;

struct mock_socket : udp_socket_interface
{
	bool send_packet(entry& msg, udp::endpoint const& ep, int flags)
	{
		g_sent_packets.push_back(std::make_pair(ep, msg));
		return true;
	}
};

address rand_v4()
{
	return address_v4((rand() << 16 | rand()) & 0xffffffff);
}

udp::endpoint rand_ep()
{
	return udp::endpoint(rand_v4(), rand());
}

sha1_hash generate_next()
{
	sha1_hash ret;
	for (int i = 0; i < 20; ++i) ret[i] = rand() & 0xff;
	return ret;
}

boost::array<char, 64> generate_key()
{
	boost::array<char, 64> ret;
	for (int i = 0; i < 64; ++i) ret[i] = rand() & 0xff;
	return ret;
}

static const std::string no;

std::list<std::pair<udp::endpoint, entry> >::iterator
find_packet(udp::endpoint ep)
{
	return std::find_if(g_sent_packets.begin(), g_sent_packets.end()
		, boost::bind(&std::pair<udp::endpoint, entry>::first, _1) == ep);
}

void lazy_from_entry(entry const& e, lazy_entry& l)
{
	error_code ec;
	static char inbuf[1500];
	int len = bencode(inbuf, e);
	int ret = lazy_bdecode(inbuf, inbuf + len, l, ec);
	TEST_CHECK(ret == 0);
}

void send_dht_request(node_impl& node, char const* msg, udp::endpoint const& ep
	, lazy_entry* reply, char const* t = "10", char const* info_hash = 0
	, char const* name = 0, std::string const token = std::string(), int port = 0
	, char const* target = 0, entry const* value = 0
	, bool scrape = false, bool seed = false
	, std::string const key = std::string(), std::string const sig = std::string()
	, int seq = -1, boost::int64_t cas = -1, sha1_hash const* nid = NULL
	, char const* put_salt = NULL)
{
	// we're about to clear out the backing buffer
	// for this lazy_entry, so we better clear it now
	reply->clear();
	entry e;
	e["q"] = msg;
	e["t"] = t;
	e["y"] = "q";
	entry::dictionary_type& a = e["a"].dict();
	if (nid == NULL) a["id"] = generate_next().to_string();
	else a["id"] = nid->to_string();
	if (info_hash) a["info_hash"] = std::string(info_hash, 20);
	if (name) a["n"] = name;
	if (!token.empty()) a["token"] = token;
	if (port) a["port"] = port;
	if (target) a["target"] = std::string(target, 20);
	if (value) a["v"] = *value;
	if (!sig.empty()) a["sig"] = sig;
	if (!key.empty()) a["k"] = key;
	if (scrape) a["scrape"] = 1;
	if (seed) a["seed"] = 1;
	if (seq >= 0) a["seq"] = seq;
	if (cas != -1) a["cas"] = cas;
	if (put_salt) a["salt"] = put_salt;
	char msg_buf[1500];
	int size = bencode(msg_buf, e);
#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
// this yields a lot of output. too much
//	std::cerr << "sending: " <<  e << "\n";
#endif

#ifdef TORRENT_USE_VALGRIND
	VALGRIND_CHECK_MEM_IS_DEFINED(msg_buf, size);
#endif

	lazy_entry decoded;
	error_code ec;
	lazy_bdecode(msg_buf, msg_buf + size, decoded, ec);
	if (ec) fprintf(stderr, "lazy_bdecode failed: %s\n", ec.message().c_str());

	dht::msg m(decoded, ep);
	node.incoming(m);

	// by now the node should have invoked the send function and put the
	// response in g_sent_packets

	std::list<std::pair<udp::endpoint, entry> >::iterator i
		= find_packet(ep);
	if (i == g_sent_packets.end())
	{
		TEST_ERROR("not response from DHT node");
		return;
	}

	lazy_from_entry(i->second, *reply);
	g_sent_packets.erase(i);
}

namespace libtorrent { namespace dht { namespace detail
{
	// defined in node.cpp
	void write_nodes_entry(entry& r, nodes_t const& nodes);
} } }

void write_peers(entry::dictionary_type& r, std::set<tcp::endpoint> const& peers)
{
	entry::list_type& pe = r["values"].list();
	for (std::set<tcp::endpoint>::const_iterator it = peers.begin()
		 ; it != peers.end(); ++it)
	{
		std::string endpoint(18, '\0');
		std::string::iterator out = endpoint.begin();
		libtorrent::detail::write_endpoint(*it, out);
		endpoint.resize(out - endpoint.begin());
		pe.push_back(entry(endpoint));
	}
}

void send_dht_response(node_impl& node, lazy_entry const& request, udp::endpoint const& ep
	, nodes_t const& nodes = nodes_t()
	, std::string const token = std::string(), int port = 0
	, std::set<tcp::endpoint> const& peers = std::set<tcp::endpoint>()
	, char const* target = 0, entry const* value = 0
	, std::string const key = std::string(), std::string const sig = std::string()
	, int seq = -1, sha1_hash const* nid = NULL)
{
	entry e;
	e["y"] = "r";
	e["t"] = request.dict_find_string_value("t");
//	e["ip"] = endpoint_to_bytes(ep);
	entry::dictionary_type& r = e["r"].dict();
	if (nid == NULL) r["id"] = generate_next().to_string();
	else r["id"] = nid->to_string();
	if (!token.empty()) r["token"] = token;
	if (port) r["p"] = port;
	if (!nodes.empty()) dht::detail::write_nodes_entry(e["r"], nodes);
	if (!peers.empty()) write_peers(r, peers);
	if (value) r["v"] = *value;
	if (!sig.empty()) r["sig"] = sig;
	if (!key.empty()) r["k"] = key;
	if (seq >= 0) r["seq"] = seq;
	char msg_buf[1500];
	int size = bencode(msg_buf, e);
#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
// this yields a lot of output. too much
//	std::cerr << "sending: " <<  e << "\n";
#endif

#ifdef TORRENT_USE_VALGRIND
	VALGRIND_CHECK_MEM_IS_DEFINED(msg_buf, size);
#endif

	lazy_entry decoded;
	error_code ec;
	lazy_bdecode(msg_buf, msg_buf + size, decoded, ec);
	if (ec) fprintf(stderr, "lazy_bdecode failed: %s\n", ec.message().c_str());

	dht::msg m(decoded, ep);
	node.incoming(m);
}

struct announce_item
{
	sha1_hash next;
	int num_peers;
	entry ent;
	sha1_hash target;
	void gen()
	{
		num_peers = (rand() % 5) + 1;
		ent["next"] = next.to_string();
		ent["A"] = "a";
		ent["B"] = "b";
		ent["num_peers"] = num_peers;

		char buf[512];
		char* ptr = buf;
		int len = bencode(ptr, ent);
		target = hasher(buf, len).final();
	}
};

void announce_immutable_items(node_impl& node, udp::endpoint const* eps
	, announce_item const* items, int num_items)
{
	std::string token;
	for (int i = 0; i < 1000; ++i)
	{
		for (int j = 0; j < num_items; ++j)
		{
			if ((i % items[j].num_peers) == 0) continue;
			lazy_entry response;
			send_dht_request(node, "get", eps[i], &response, "10", 0
				, 0, no, 0, (char const*)&items[j].target[0]);
			
			key_desc_t desc[] =
			{
				{ "r", lazy_entry::dict_t, 0, key_desc_t::parse_children },
					{ "id", lazy_entry::string_t, 20, 0},
					{ "token", lazy_entry::string_t, 0, 0},
					{ "ip", lazy_entry::string_t, 0, key_desc_t::optional | key_desc_t::last_child},
				{ "y", lazy_entry::string_t, 1, 0},
			};

			lazy_entry const* parsed[6];
			char error_string[200];

//			fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
			int ret = verify_message(&response, desc, parsed, 5, error_string, sizeof(error_string));
			if (ret)
			{
				TEST_EQUAL(parsed[4]->string_value(), "r");
				token = parsed[2]->string_value();
//				fprintf(stderr, "got token: %s\n", token.c_str());
			}
			else
			{
				fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
				fprintf(stderr, "   invalid get response: %s\n", error_string);
				TEST_ERROR(error_string);
			}

			if (parsed[3])
			{
				address_v4::bytes_type b;
				memcpy(&b[0], parsed[3]->string_ptr(), b.size());
				address_v4 addr(b);
				TEST_EQUAL(addr, eps[i].address());
			}

			send_dht_request(node, "put", eps[i], &response, "10", 0
				, 0, token, 0, (char const*)&items[j].target[0], &items[j].ent);

			key_desc_t desc2[] =
			{
				{ "y", lazy_entry::string_t, 1, 0 }
			};

			ret = verify_message(&response, desc2, parsed, 1, error_string, sizeof(error_string));
			if (ret)
			{
				if (parsed[0]->string_value() != "r")
					fprintf(stderr, "msg: %s\n", print_entry(response).c_str());

				TEST_EQUAL(parsed[0]->string_value(), "r");
			}
			else
			{
				fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
				fprintf(stderr, "   invalid put response: %s\n", error_string);
				TEST_ERROR(error_string);
			}
		}
	}

	std::set<int> items_num;
	for (int j = 0; j < num_items; ++j)
	{
		lazy_entry response;
		send_dht_request(node, "get", eps[j], &response, "10", 0
			, 0, no, 0, (char const*)&items[j].target[0]);
		
		key_desc_t desc[] =
		{
			{ "r", lazy_entry::dict_t, 0, key_desc_t::parse_children },
				{ "v", lazy_entry::dict_t, 0, 0},
				{ "id", lazy_entry::string_t, 20, key_desc_t::last_child},
			{ "y", lazy_entry::string_t, 1, 0},
		};

		lazy_entry const* parsed[4];
		char error_string[200];

		int ret = verify_message(&response, desc, parsed, 4, error_string, sizeof(error_string));
		if (ret)
		{
			items_num.insert(items_num.begin(), j);
		}
	}

	TEST_EQUAL(items_num.size(), 4);

	// items_num should contain 1,2 and 3
	// #error this doesn't quite hold
//	TEST_CHECK(items_num.find(1) != items_num.end());
//	TEST_CHECK(items_num.find(2) != items_num.end());
//	TEST_CHECK(items_num.find(3) != items_num.end());
}

struct print_alert : alert_dispatcher
{
	virtual bool post_alert(alert* a)
	{
		fprintf(stderr, "ALERT: %s\n", a->message().c_str());
		delete a;
		return true;
	}
};


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

bool get_item_cb(dht::item& i)
{
	if (!i.empty())
		g_got_items.push_back(i);
	if (!g_put_item.empty())
	{
		i = g_put_item;
		g_put_count++;
		return true;
	}
	return false;
}

// TODO: 3 test obfuscated_get_peers
int test_main()
{
	random_seed(total_microseconds(time_now_hires() - min_time()));

	dht_settings sett;
	sett.max_torrents = 4;
	sett.max_dht_items = 4;
	sett.enforce_node_id = false;
	address ext = address::from_string("236.0.0.1");
	mock_socket s;
	print_alert ad;
	dht::node_impl node(&ad, &s, sett, node_id(0), ext, 0);

	// DHT should be running on port 48199 now
	lazy_entry response;
	lazy_entry const* parsed[11];
	char error_string[200];
	bool ret;

	// ====== ping ======
	udp::endpoint source(address::from_string("10.0.0.1"), 20);
	send_dht_request(node, "ping", source, &response, "10");

	dht::key_desc_t pong_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"t", lazy_entry::string_t, 2, 0},
		{"r", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"id", lazy_entry::string_t, 20, key_desc_t::last_child},
	};

	fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
	ret = dht::verify_message(&response, pong_desc, parsed, 4, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(parsed[0]->string_value() == "r");
		TEST_CHECK(parsed[1]->string_value() == "10");
	}
	else
	{
		fprintf(stderr, "   invalid ping response: %s\n", error_string);
	}

	// ====== invalid message ======

	send_dht_request(node, "find_node", source, &response, "10");

	dht::key_desc_t err_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"e", lazy_entry::list_t, 2, 0}
	};

	fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
	ret = dht::verify_message(&response, err_desc, parsed, 2, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(parsed[0]->string_value() == "e");
		if (parsed[1]->list_at(0)->type() == lazy_entry::int_t
			&& parsed[1]->list_at(1)->type() == lazy_entry::string_t)
		{
			TEST_CHECK(parsed[1]->list_at(1)->string_value() == "missing 'target' key");
		}
		else
		{
			TEST_ERROR("invalid error response");
		}
	}
	else
	{
		fprintf(stderr, "   invalid error response: %s\n", error_string);
	}

	// ====== get_peers ======

	send_dht_request(node, "get_peers", source, &response, "10", "01010101010101010101");

	dht::key_desc_t peer1_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"r", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"token", lazy_entry::string_t, 0, 0},
			{"id", lazy_entry::string_t, 20, key_desc_t::last_child},
	};

	std::string token;
	fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
	ret = dht::verify_message(&response, peer1_desc, parsed, 4, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(parsed[0]->string_value() == "r");
		token = parsed[2]->string_value();
//		fprintf(stderr, "got token: %s\n", token.c_str());
	}
	else
	{
		fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
		fprintf(stderr, "   invalid get_peers response: %s\n", error_string);
	}

	// ====== announce ======

	send_dht_request(node, "announce_peer", source, &response, "10", "01010101010101010101", "test", token, 8080);

	dht::key_desc_t ann_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"r", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"id", lazy_entry::string_t, 20, key_desc_t::last_child},
	};

	fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
	ret = dht::verify_message(&response, ann_desc, parsed, 3, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(parsed[0]->string_value() == "r");
	}
	else
	{
		fprintf(stderr, "   invalid announce response: %s\n", error_string);
	}

	// announce from 100 random IPs and make sure scrape works
	// 50 downloaders and 50 seeds
	for (int i = 0; i < 100; ++i)
	{
		source = udp::endpoint(rand_v4(), 6000);
		send_dht_request(node, "get_peers", source, &response, "10", "01010101010101010101");
		ret = dht::verify_message(&response, peer1_desc, parsed, 4, error_string, sizeof(error_string));

		if (ret)
		{
			TEST_CHECK(parsed[0]->string_value() == "r");
			token = parsed[2]->string_value();
//			fprintf(stderr, "got token: %s\n", token.c_str());
		}
		else
		{
			fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
			fprintf(stderr, "   invalid get_peers response: %s\n", error_string);
		}
		response.clear();
		send_dht_request(node, "announce_peer", source, &response, "10", "01010101010101010101"
			, "test", token, 8080, 0, 0, false, i >= 50);
		response.clear();
	}

	// ====== get_peers ======

	send_dht_request(node, "get_peers", source, &response, "10", "01010101010101010101"
		, 0, no, 0, 0, 0, true);

	dht::key_desc_t peer2_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"r", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"BFpe", lazy_entry::string_t, 256, 0},
			{"BFsd", lazy_entry::string_t, 256, 0},
			{"id", lazy_entry::string_t, 20, key_desc_t::last_child},
	};

	fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
	ret = dht::verify_message(&response, peer2_desc, parsed, 5, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(parsed[0]->string_value() == "r");
		TEST_EQUAL(parsed[1]->dict_find_string_value("n"), "test");

		bloom_filter<256> downloaders;
		bloom_filter<256> seeds;
		downloaders.from_string(parsed[2]->string_ptr());
		seeds.from_string(parsed[3]->string_ptr());

		fprintf(stderr, "seeds: %f\n", seeds.size());
		fprintf(stderr, "downloaders: %f\n", downloaders.size());

		TEST_CHECK(fabs(seeds.size() - 50.f) <= 3.f);
		TEST_CHECK(fabs(downloaders.size() - 50.f) <= 3.f);
	}
	else
	{
		fprintf(stderr, "   invalid get_peers response: %s\n", error_string);
	}

	// ====== test node ID testing =====

	{
		node_id rnd = generate_secret_id();
		TEST_CHECK(verify_secret_id(rnd));

		rnd[19] ^= 0x55;
		TEST_CHECK(!verify_secret_id(rnd));

		rnd = generate_random_id();
		make_id_secret(rnd);
		TEST_CHECK(verify_secret_id(rnd));
	}

	// ====== test node ID enforcement ======

	// enable node_id enforcement
	sett.enforce_node_id = true;

	// this is one of the test vectors from:
	// http://libtorrent.org/dht_sec.html
	source = udp::endpoint(address::from_string("124.31.75.21"), 1);
	node_id nid = to_hash("5fbfbff10c5d6a4ec8a88e4c6ab4c28b95eee401");
	send_dht_request(node, "find_node", source, &response, "10", 0, 0, std::string()
		, 0, "0101010101010101010101010101010101010101", 0, false, false, std::string(), std::string(), -1, 0, &nid);

	dht::key_desc_t nodes_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"r", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"id", lazy_entry::string_t, 20, key_desc_t::last_child},
	};

	fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
	ret = dht::verify_message(&response, nodes_desc, parsed, 3, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(parsed[0]->string_value() == "r");
	}
	else
	{
		fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
		fprintf(stderr, "   invalid error response: %s\n", error_string);
	}

	// verify that we reject invalid node IDs
	// this is now an invalid node-id for 'source'
	nid[0] = 0x18;
	send_dht_request(node, "find_node", source, &response, "10", 0, 0, std::string()
		, 0, "0101010101010101010101010101010101010101", 0, false, false, std::string(), std::string(), -1, 0, &nid);

	ret = dht::verify_message(&response, err_desc, parsed, 2, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(parsed[0]->string_value() == "e");
		if (parsed[1]->list_at(0)->type() == lazy_entry::int_t
			&& parsed[1]->list_at(1)->type() == lazy_entry::string_t)
		{
			TEST_CHECK(parsed[1]->list_at(1)->string_value() == "invalid node ID");
		}
		else
		{
			fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
			TEST_ERROR("invalid error response");
		}
	}
	else
	{
		fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
		fprintf(stderr, "   invalid error response: %s\n", error_string);
	}

	sett.enforce_node_id = false;

// ===========================

	bloom_filter<256> test;
	for (int i = 0; i < 256; ++i)
	{
		char adr[50];
		snprintf(adr, 50, "192.0.2.%d", i);
		address a = address::from_string(adr);
		sha1_hash iphash;
		hash_address(a, iphash);
		test.set(iphash);
	}

	if (supports_ipv6())
	{
		for (int i = 0; i < 0x3E8; ++i)
		{
			char adr[50];
			snprintf(adr, 50, "2001:db8::%x", i);
			address a = address::from_string(adr);
			sha1_hash iphash;
			hash_address(a, iphash);
			test.set(iphash);
		}
	}

	// these are test vectors from BEP 33
	// http://www.bittorrent.org/beps/bep_0033.html
	fprintf(stderr, "test.size: %f\n", test.size());
	fprintf(stderr, "%s\n", to_hex(test.to_string()).c_str());
	if (supports_ipv6())
	{
		TEST_CHECK(fabs(test.size() - 1224.93f) < 0.001);
		TEST_CHECK(to_hex(test.to_string()) == "f6c3f5eaa07ffd91bde89f777f26fb2bff37bdb8fb2bbaa2fd3ddde7bacfff75ee7ccbaefe5eedb1fbfaff67f6abff5e43ddbca3fd9b9ffdf4ffd3e9dff12d1bdf59db53dbe9fa5b7ff3b8fdfcde1afb8bedd7be2f3ee71ebbbfe93bcdeefe148246c2bc5dbff7e7efdcf24fd8dc7adffd8fffdfddfff7a4bbeedf5cb95ce81fc7fcff1ff4ffffdfe5f7fdcbb7fd79b3fa1fc77bfe07fff905b7b7ffc7fefeffe0b8370bb0cd3f5b7f2bd93feb4386cfdd6f7fd5bfaf2e9ebffffeecd67adbf7c67f17efd5d75eba6ffeba7fff47a91eb1bfbb53e8abfb5762abe8ff237279bfefbfeef5ffc5febfdfe5adffadfee1fb737ffffbfd9f6aeffeee76b6fd8f72ef");
	}
	else
	{
		TEST_CHECK(fabs(test.size() - 257.854f) < 0.001);
		TEST_CHECK(to_hex(test.to_string()) == "24c0004020043000102012743e00480037110820422110008000c0e302854835a05401a4045021302a306c060001881002d8a0a3a8001901b40a800900310008d2108110c2496a0028700010d804188b01415200082004088026411104a804048002002000080680828c400080cc40020c042c0494447280928041402104080d4240040414a41f0205654800b0811830d2020042b002c5800004a71d0204804a0028120a004c10017801490b834004044106005421000c86900a0020500203510060144e900100924a1018141a028012913f0041802250042280481200002004430804210101c08111c10801001080002038008211004266848606b035001048");
	}

	response.clear();

	// ====== put ======

	udp::endpoint eps[1000];

 	for (int i = 0; i < 1000; ++i)
		eps[i] = udp::endpoint(rand_v4(), (rand() % 16534) + 1);

	announce_item items[] =
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

	for (int i = 0; i < sizeof(items)/sizeof(items[0]); ++i)
		items[i].gen();

	announce_immutable_items(node, eps, items, sizeof(items)/sizeof(items[0]));

	key_desc_t desc2[] =
	{
		{ "y", lazy_entry::string_t, 1, 0 }
	};

	key_desc_t desc_error[] =
	{
		{ "e", lazy_entry::list_t, 2, 0 },
		{ "y", lazy_entry::string_t, 1, 0},
	};

	// ==== get / put mutable items ===

	std::pair<char const*, int> itemv;
	std::pair<char const*, int> empty_salt;
	empty_salt.first = NULL;
	empty_salt.second = 0;

	char signature[item_sig_len];
	char buffer[1200];
	int seq = 4;
	char private_key[item_sk_len];
	char public_key[item_pk_len];
	for (int with_salt = 0; with_salt < 2; ++with_salt)
	{
		seq = 4;
		fprintf(stderr, "\nTEST GET/PUT%s \ngenerating ed25519 keys\n\n"
			, with_salt ? " with-salt" : " no-salt");
		unsigned char seed[32];
		ed25519_create_seed(seed);

		ed25519_create_keypair((unsigned char*)public_key, (unsigned char*)private_key, seed);
		fprintf(stderr, "pub: %s priv: %s\n"
			, to_hex(std::string(public_key, item_pk_len)).c_str()
			, to_hex(std::string(private_key, item_sk_len)).c_str());

		TEST_CHECK(ret);

		std::pair<const char*, int> salt((char*)0, 0);
		if (with_salt)
			salt = std::pair<char const*, int>("foobar", 6);

		hasher h(public_key, 32);
		if (with_salt) h.update(salt.first, salt.second);
		sha1_hash target_id = h.final();

		fprintf(stderr, "target_id: %s\n"
			, to_hex(target_id.to_string()).c_str());

		send_dht_request(node, "get", source, &response, "10", 0
			, 0, no, 0, (char*)&target_id[0]
			, 0, false, false, std::string(), std::string());

		key_desc_t desc[] =
		{
			{ "r", lazy_entry::dict_t, 0, key_desc_t::parse_children },
			{ "id", lazy_entry::string_t, 20, 0},
			{ "token", lazy_entry::string_t, 0, 0},
			{ "ip", lazy_entry::string_t, 0, key_desc_t::optional | key_desc_t::last_child},
			{ "y", lazy_entry::string_t, 1, 0},
		};

		ret = verify_message(&response, desc, parsed, 5, error_string, sizeof(error_string));
		if (ret)
		{
			TEST_EQUAL(parsed[4]->string_value(), "r");
			token = parsed[2]->string_value();
			fprintf(stderr, "get response: %s\n"
				, print_entry(response).c_str());
			fprintf(stderr, "got token: %s\n", to_hex(token).c_str());
		}
		else
		{
			fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
			fprintf(stderr, "   invalid get response: %s\n%s\n"
				, error_string, print_entry(response).c_str());
			TEST_ERROR(error_string);
		}

		itemv = std::pair<char const*, int>(buffer, bencode(buffer, items[0].ent));
		sign_mutable_item(itemv, salt, seq, public_key, private_key, signature);
		TEST_EQUAL(verify_mutable_item(itemv, salt, seq, public_key, signature), true);
#ifdef TORRENT_USE_VALGRIND
		VALGRIND_CHECK_MEM_IS_DEFINED(signature, item_sig_len);
#endif

		send_dht_request(node, "put", source, &response, "10", 0
			, 0, token, 0, 0, &items[0].ent, false, false
			, std::string(public_key, item_pk_len)
			, std::string(signature, item_sig_len), seq, -1, NULL, salt.first);

		ret = verify_message(&response, desc2, parsed, 1, error_string, sizeof(error_string));
		if (ret)
		{
			fprintf(stderr, "put response: %s\n"
				, print_entry(response).c_str());
			TEST_EQUAL(parsed[0]->string_value(), "r");
		}
		else
		{
			fprintf(stderr, "   invalid put response: %s\n%s\n"
				, error_string, print_entry(response).c_str());
			TEST_ERROR(error_string);
		}

		send_dht_request(node, "get", source, &response, "10", 0
			, 0, no, 0, (char*)&target_id[0]
			, 0, false, false, std::string(), std::string());

		fprintf(stderr, "target_id: %s\n"
			, to_hex(target_id.to_string()).c_str());

		key_desc_t desc3[] =
		{
			{ "r", lazy_entry::dict_t, 0, key_desc_t::parse_children },
			{ "id", lazy_entry::string_t, 20, 0},
			{ "v", lazy_entry::none_t, 0, 0},
			{ "seq", lazy_entry::int_t, 0, 0},
			{ "sig", lazy_entry::string_t, 0, 0},
			{ "ip", lazy_entry::string_t, 0, key_desc_t::optional | key_desc_t::last_child},
			{ "y", lazy_entry::string_t, 1, 0},
		};

		ret = verify_message(&response, desc3, parsed, 7, error_string, sizeof(error_string));
		if (ret == 0)
		{
			fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
			fprintf(stderr, "   invalid get response: %s\n%s\n"
				, error_string, print_entry(response).c_str());
			TEST_ERROR(error_string);
		}
		else
		{
			fprintf(stderr, "get response: %s\n"
				, print_entry(response).c_str());
			char value[1020];
			char* ptr = value;
			int value_len = bencode(ptr, items[0].ent);
			TEST_EQUAL(value_len, parsed[2]->data_section().second);
			TEST_CHECK(memcmp(parsed[2]->data_section().first, value, value_len) == 0);

			TEST_EQUAL(seq, parsed[3]->int_value());
		}

		// also test that invalid signatures fail!

		itemv.second = bencode(buffer, items[0].ent);
		sign_mutable_item(itemv, salt, seq, public_key, private_key, signature);
		TEST_EQUAL(verify_mutable_item(itemv, salt, seq, public_key, signature), 1);
#ifdef TORRENT_USE_VALGRIND
		VALGRIND_CHECK_MEM_IS_DEFINED(signature, item_sig_len);
#endif
		// break the signature 
		signature[2] ^= 0xaa;

		fprintf(stderr, "PUT broken signature\n");

		TEST_CHECK(verify_mutable_item(itemv, salt, seq, public_key, signature) != 1);

		send_dht_request(node, "put", source, &response, "10", 0
			, 0, token, 0, 0, &items[0].ent, false, false
			, std::string(public_key, item_pk_len)
			, std::string(signature, item_sig_len), seq, -1, NULL, salt.first);

		ret = verify_message(&response, desc_error, parsed, 2, error_string, sizeof(error_string));
		if (ret)
		{
			fprintf(stderr, "put response: %s\n", print_entry(response).c_str());
			TEST_EQUAL(parsed[1]->string_value(), "e");
			// 206 is the code for invalid signature
			TEST_EQUAL(parsed[0]->list_int_value_at(0), 206);
		}
		else
		{
			fprintf(stderr, "   invalid put response: %s\n%s\n"
				, error_string, print_entry(response).c_str());
			TEST_ERROR(error_string);
		}

		// === test conditional get ===

		send_dht_request(node, "get", source, &response, "10", 0
			, 0, no, 0, (char*)&target_id[0]
			, 0, false, false, std::string(), std::string(), seq-1);

		{
			lazy_entry const* r = response.dict_find_dict("r");
			TEST_CHECK(r->dict_find("v"));
			TEST_CHECK(r->dict_find("k"));
			TEST_CHECK(r->dict_find("sig"));
		}

		send_dht_request(node, "get", source, &response, "10", 0
			, 0, no, 0, (char*)&target_id[0]
			, 0, false, false, std::string(), std::string(), seq);

		{
			lazy_entry const* r = response.dict_find_dict("r");
			TEST_CHECK(!r->dict_find("v"));
			TEST_CHECK(!r->dict_find("k"));
			TEST_CHECK(!r->dict_find("sig"));
		}

		// === test CAS put ===

		// this is the sequence number we expect to be there
		boost::uint64_t cas = seq;

		// increment sequence number
		++seq;
		// put item 1
		itemv.second = bencode(buffer, items[1].ent);
		sign_mutable_item(itemv, salt, seq, public_key, private_key, signature);
		TEST_EQUAL(verify_mutable_item(itemv, salt, seq, public_key, signature), 1);
#ifdef TORRENT_USE_VALGRIND
		VALGRIND_CHECK_MEM_IS_DEFINED(signature, item_sig_len);
#endif

		TEST_CHECK(item_target_id(salt, public_key) == target_id);

		fprintf(stderr, "PUT CAS 1\n");

		send_dht_request(node, "put", source, &response, "10", 0
			, 0, token, 0, 0, &items[1].ent, false, false
			, std::string(public_key, item_pk_len)
			, std::string(signature, item_sig_len), seq
			, cas, NULL, salt.first);

		ret = verify_message(&response, desc2, parsed, 1, error_string, sizeof(error_string));
		if (ret)
		{
			fprintf(stderr, "put response: %s\n"
				, print_entry(response).c_str());
			TEST_EQUAL(parsed[0]->string_value(), "r");
		}
		else
		{
			fprintf(stderr, "   invalid put response: %s\n%s\n"
				, error_string, print_entry(response).c_str());
			TEST_ERROR(error_string);
		}

		fprintf(stderr, "PUT CAS 2\n");

		// put the same message again. This should fail because the
		// CAS hash is outdated, it's not the hash of the value that's
		// stored anymore
		send_dht_request(node, "put", source, &response, "10", 0
			, 0, token, 0, 0, &items[1].ent, false, false
			, std::string(public_key, item_pk_len)
			, std::string(signature, item_sig_len), seq
			, cas, NULL, salt.first);

		ret = verify_message(&response, desc_error, parsed, 2, error_string, sizeof(error_string));
		if (ret)
		{
			fprintf(stderr, "put response: %s\n"
				, print_entry(response).c_str());
			TEST_EQUAL(parsed[1]->string_value(), "e");
			// 301 is the error code for CAS hash mismatch
			TEST_EQUAL(parsed[0]->list_int_value_at(0), 301);
		}
		else
		{
			fprintf(stderr, "   invalid put response: %s\n%s\nExpected failure 301 (CAS hash mismatch)\n"
				, error_string, print_entry(response).c_str());
			TEST_ERROR(error_string);
		}

	}

// test routing table

	{
		sett.extended_routing_table = false;
		node_id id = to_hash("1234876923549721020394873245098347598635");
		node_id diff = to_hash("15764f7459456a9453f8719b09547c11d5f34061");

		routing_table tbl(id, 8, sett);
   
		// insert 256 nodes evenly distributed across the ID space.
		// we expect to fill the top 5 buckets
		for (int i = 0; i < 256; ++i)
		{
			// test a node with the same IP:port changing ID
			add_and_replace(id, diff);
			id[0] = i;
			tbl.node_seen(id, rand_ep(), 20 + (id[19] & 0xff));
		}
		printf("num_active_buckets: %d\n", tbl.num_active_buckets());
		TEST_EQUAL(tbl.num_active_buckets(), 6);
   
#if defined TORRENT_DHT_VERBOSE_LOGGING || defined TORRENT_DEBUG
		tbl.print_state(std::cerr);
#endif
	}

	{
		sett.extended_routing_table = true;
		node_id id = to_hash("1234876923549721020394873245098347598635");
		node_id diff = to_hash("15764f7459456a9453f8719b09547c11d5f34061");

		routing_table tbl(id, 8, sett);
		for (int i = 0; i < 256; ++i)
		{
			add_and_replace(id, diff);
			id[0] = i;
			tbl.node_seen(id, rand_ep(), 20 + (id[19] & 0xff));
		}
		TEST_EQUAL(tbl.num_active_buckets(), 6);

#if defined TORRENT_DHT_VERBOSE_LOGGING || defined TORRENT_DEBUG
		tbl.print_state(std::cerr);
#endif
	}

	// test verify_message
	const static key_desc_t msg_desc[] = {
		{"A", lazy_entry::string_t, 4, 0},
		{"B", lazy_entry::dict_t, 0, key_desc_t::optional | key_desc_t::parse_children},
			{"B1", lazy_entry::string_t, 0, 0},
			{"B2", lazy_entry::string_t, 0, key_desc_t::last_child},
		{"C", lazy_entry::dict_t, 0, key_desc_t::optional | key_desc_t::parse_children},
			{"C1", lazy_entry::string_t, 0, 0},
			{"C2", lazy_entry::string_t, 0, key_desc_t::last_child},
	};

	lazy_entry const* msg_keys[7];

	lazy_entry ent;

	error_code ec;
	char const test_msg[] = "d1:A4:test1:Bd2:B15:test22:B25:test3ee";
	lazy_bdecode(test_msg, test_msg + sizeof(test_msg)-1, ent, ec);
	fprintf(stderr, "%s\n", print_entry(ent).c_str());

	ret = verify_message(&ent, msg_desc, msg_keys, 7, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	TEST_CHECK(msg_keys[0]);
	if (msg_keys[0]) TEST_EQUAL(msg_keys[0]->string_value(), "test");
	TEST_CHECK(msg_keys[1]);
	TEST_CHECK(msg_keys[2]);
	if (msg_keys[2]) TEST_EQUAL(msg_keys[2]->string_value(), "test2");
	TEST_CHECK(msg_keys[3]);
	if (msg_keys[3]) TEST_EQUAL(msg_keys[3]->string_value(), "test3");
	TEST_CHECK(msg_keys[4] == 0);
	TEST_CHECK(msg_keys[5] == 0);
	TEST_CHECK(msg_keys[6] == 0);

	char const test_msg2[] = "d1:A4:test1:Cd2:C15:test22:C25:test3ee";
	lazy_bdecode(test_msg2, test_msg2 + sizeof(test_msg2)-1, ent, ec);
	fprintf(stderr, "%s\n", print_entry(ent).c_str());

	ret = verify_message(&ent, msg_desc, msg_keys, 7, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	TEST_CHECK(msg_keys[0]);
	if (msg_keys[0]) TEST_EQUAL(msg_keys[0]->string_value(), "test");
	TEST_CHECK(msg_keys[1] == 0);
	TEST_CHECK(msg_keys[2] == 0);
	TEST_CHECK(msg_keys[3] == 0);
	TEST_CHECK(msg_keys[4]);
	TEST_CHECK(msg_keys[5]);
	if (msg_keys[5]) TEST_EQUAL(msg_keys[5]->string_value(), "test2");
	TEST_CHECK(msg_keys[6]);
	if (msg_keys[6]) TEST_EQUAL(msg_keys[6]->string_value(), "test3");


	char const test_msg3[] = "d1:Cd2:C15:test22:C25:test3ee";
	lazy_bdecode(test_msg3, test_msg3 + sizeof(test_msg3)-1, ent, ec);
	fprintf(stderr, "%s\n", print_entry(ent).c_str());

	ret = verify_message(&ent, msg_desc, msg_keys, 7, error_string, sizeof(error_string));
	TEST_CHECK(!ret);
	fprintf(stderr, "%s\n", error_string);
	TEST_EQUAL(error_string, std::string("missing 'A' key"));

	char const test_msg4[] = "d1:A6:foobare";
	lazy_bdecode(test_msg4, test_msg4 + sizeof(test_msg4)-1, ent, ec);
	fprintf(stderr, "%s\n", print_entry(ent).c_str());

	ret = verify_message(&ent, msg_desc, msg_keys, 7, error_string, sizeof(error_string));
	TEST_CHECK(!ret);
	fprintf(stderr, "%s\n", error_string);
	TEST_EQUAL(error_string, std::string("invalid value for 'A'"));

	char const test_msg5[] = "d1:A4:test1:Cd2:C15:test2ee";
	lazy_bdecode(test_msg5, test_msg5 + sizeof(test_msg5)-1, ent, ec);
	fprintf(stderr, "%s\n", print_entry(ent).c_str());

	ret = verify_message(&ent, msg_desc, msg_keys, 7, error_string, sizeof(error_string));
	TEST_CHECK(!ret);
	fprintf(stderr, "%s\n", error_string);
	TEST_EQUAL(error_string, std::string("missing 'C2' key"));

	// test empty strings [ { "":1 }, "" ]
	char const test_msg6[] = "ld0:i1ee0:e";
	lazy_bdecode(test_msg6, test_msg6 + sizeof(test_msg6)-1, ent, ec);
	fprintf(stderr, "%s\n", print_entry(ent).c_str());
	TEST_CHECK(ent.type() == lazy_entry::list_t);
	if (ent.type() == lazy_entry::list_t)
	{
		TEST_CHECK(ent.list_size() == 2);
		if (ent.list_size() == 2)
		{
			TEST_CHECK(ent.list_at(0)->dict_find_int_value("") == 1);
			TEST_CHECK(ent.list_at(1)->string_value() == "");
		}
	}

	// test node-id functions
	using namespace libtorrent::dht;

	TEST_EQUAL(generate_prefix_mask(0), to_hash("0000000000000000000000000000000000000000"));
	TEST_EQUAL(generate_prefix_mask(1), to_hash("8000000000000000000000000000000000000000"));
	TEST_EQUAL(generate_prefix_mask(2), to_hash("c000000000000000000000000000000000000000"));
	TEST_EQUAL(generate_prefix_mask(11), to_hash("ffe0000000000000000000000000000000000000"));
	TEST_EQUAL(generate_prefix_mask(17), to_hash("ffff800000000000000000000000000000000000"));
	TEST_EQUAL(generate_prefix_mask(160), to_hash("ffffffffffffffffffffffffffffffffffffffff"));

	// test kademlia functions

	// this is a bit too expensive to do under valgrind
#ifndef TORRENT_USE_VALGRIND
	for (int i = 0; i < 160; i += 8)
	{
		for (int j = 0; j < 160; j += 8)
		{
			node_id a(0);
			a[(159-i) / 8] = 1 << (i & 7);
			node_id b(0);
			b[(159-j) / 8] = 1 << (j & 7);
			int dist = distance_exp(a, b);

			TEST_CHECK(dist >= 0 && dist < 160);
			TEST_CHECK(dist == ((i == j)?0:(std::max)(i, j)));

			for (int k = 0; k < 160; k += 8)
			{
				node_id c(0);
				c[(159-k) / 8] = 1 << (k & 7);

				bool cmp = compare_ref(a, b, c);
				TEST_CHECK(cmp == (distance(a, c) < distance(b, c)));
			}
		}
	}
#endif

	{
		// test kademlia routing table
		dht_settings s;
		s.extended_routing_table = false;
		//	s.restrict_routing_ips = false;
		node_id id = to_hash("3123456789abcdef01232456789abcdef0123456");
		const int bucket_size = 10;
		dht::routing_table table(id, bucket_size, s);
		std::vector<node_entry> nodes;
		TEST_EQUAL(table.size().get<0>(), 0);

		node_id tmp = id;
		node_id diff = to_hash("15764f7459456a9453f8719b09547c11d5f34061");

		// test a node with the same IP:port changing ID
		add_and_replace(tmp, diff);
		table.node_seen(tmp, udp::endpoint(address::from_string("4.4.4.4"), 4), 10);
		table.find_node(id, nodes, 0, 10);
		TEST_EQUAL(table.bucket_size(0), 1);
		TEST_EQUAL(table.size().get<0>(), 1);
		TEST_EQUAL(nodes.size(), 1);
		if (!nodes.empty())
		{
			TEST_EQUAL(nodes[0].id, tmp);
			TEST_EQUAL(nodes[0].addr(), address_v4::from_string("4.4.4.4"));
			TEST_EQUAL(nodes[0].port(), 4);
			TEST_EQUAL(nodes[0].timeout_count, 0);
		}

		// set timeout_count to 1
		table.node_failed(tmp, udp::endpoint(address_v4::from_string("4.4.4.4"), 4));

		nodes.clear();
		table.for_each_node(node_push_back, nop, &nodes);
		TEST_EQUAL(nodes.size(), 1);
		if (!nodes.empty())
		{
			TEST_EQUAL(nodes[0].id, tmp);
			TEST_EQUAL(nodes[0].addr(), address_v4::from_string("4.4.4.4"));
			TEST_EQUAL(nodes[0].port(), 4);
			TEST_EQUAL(nodes[0].timeout_count, 1);
		}

		// add the exact same node again, it should set the timeout_count to 0
		table.node_seen(tmp, udp::endpoint(address::from_string("4.4.4.4"), 4), 10);
		nodes.clear();
		table.for_each_node(node_push_back, nop, &nodes);
		TEST_EQUAL(nodes.size(), 1);
		if (!nodes.empty())
		{
			TEST_EQUAL(nodes[0].id, tmp);
			TEST_EQUAL(nodes[0].addr(), address_v4::from_string("4.4.4.4"));
			TEST_EQUAL(nodes[0].port(), 4);
			TEST_EQUAL(nodes[0].timeout_count, 0);
		}

		// test adding the same IP:port again with a new node ID (should replace the old one)
		add_and_replace(tmp, diff);
		table.node_seen(tmp, udp::endpoint(address::from_string("4.4.4.4"), 4), 10);
		table.find_node(id, nodes, 0, 10);
		TEST_EQUAL(table.bucket_size(0), 1);
		TEST_EQUAL(nodes.size(), 1);
		if (!nodes.empty())
		{
			TEST_EQUAL(nodes[0].id, tmp);
			TEST_EQUAL(nodes[0].addr(), address_v4::from_string("4.4.4.4"));
			TEST_EQUAL(nodes[0].port(), 4);
		}

		// test adding the same node ID again with a different IP (should be ignored)
		table.node_seen(tmp, udp::endpoint(address::from_string("4.4.4.4"), 5), 10);
		table.find_node(id, nodes, 0, 10);
		TEST_EQUAL(table.bucket_size(0), 1);
		if (!nodes.empty())
		{
			TEST_EQUAL(nodes[0].id, tmp);
			TEST_EQUAL(nodes[0].addr(), address_v4::from_string("4.4.4.4"));
			TEST_EQUAL(nodes[0].port(), 4);
		}

		// test adding a node that ends up in the same bucket with an IP
		// very close to the current one (should be ignored)
		// if restrict_routing_ips == true
		table.node_seen(tmp, udp::endpoint(address::from_string("4.4.4.5"), 5), 10);
		table.find_node(id, nodes, 0, 10);
		TEST_EQUAL(table.bucket_size(0), 1);
		if (!nodes.empty())
		{
			TEST_EQUAL(nodes[0].id, tmp);
			TEST_EQUAL(nodes[0].addr(), address_v4::from_string("4.4.4.4"));
			TEST_EQUAL(nodes[0].port(), 4);
		}

		s.restrict_routing_ips = false;

		add_and_replace(tmp, diff);
		table.node_seen(id, udp::endpoint(rand_v4(), rand()), 10);

		nodes.clear();
		for (int i = 0; i < 7000; ++i)
		{
			table.node_seen(tmp, udp::endpoint(rand_v4(), rand()), 20 + (tmp[19] & 0xff));
			add_and_replace(tmp, diff);
		}
		printf("active buckets: %d\n", table.num_active_buckets());
		TEST_EQUAL(table.num_active_buckets(), 10);
		TEST_CHECK(table.size().get<0>() >= 10 * 10);
		//#error test num_global_nodes
		//#error test need_refresh

#if defined TORRENT_DHT_VERBOSE_LOGGING || defined TORRENT_DEBUG
		table.print_state(std::cerr);
#endif

		table.for_each_node(node_push_back, nop, &nodes);

		printf("nodes: %d\n", int(nodes.size()));

		std::vector<node_entry> temp;

		std::generate(tmp.begin(), tmp.end(), random_byte);
		table.find_node(tmp, temp, 0, nodes.size() * 2);
		printf("returned-all: %d\n", int(temp.size()));
		TEST_EQUAL(temp.size(), nodes.size());

		// This makes sure enough of the nodes returned are actually
		// part of the closest nodes
		std::set<node_id> duplicates;

#ifdef TORRENT_USE_VALGRIND
		const int reps = 3;
#else
		const int reps = 50;
#endif

		for (int r = 0; r < reps; ++r)
		{
			std::generate(tmp.begin(), tmp.end(), random_byte);
			table.find_node(tmp, temp, 0, bucket_size * 2);
			printf("returned: %d\n", int(temp.size()));
			TEST_EQUAL(int(temp.size()), (std::min)(bucket_size * 2, int(nodes.size())));

			std::sort(nodes.begin(), nodes.end(), boost::bind(&compare_ref
					, boost::bind(&node_entry::id, _1)
					, boost::bind(&node_entry::id, _2), tmp));

			int expected = std::accumulate(nodes.begin(), nodes.begin() + (bucket_size * 2)
				, 0, boost::bind(&sum_distance_exp, _1, _2, tmp));
			int sum_hits = std::accumulate(temp.begin(), temp.end()
				, 0, boost::bind(&sum_distance_exp, _1, _2, tmp));
			TEST_EQUAL(bucket_size * 2, int(temp.size()));
			printf("expected: %d actual: %d\n", expected, sum_hits);
			TEST_EQUAL(expected, sum_hits);

			duplicates.clear();
			// This makes sure enough of the nodes returned are actually
			// part of the closest nodes
			for (std::vector<node_entry>::iterator i = temp.begin()
				, end(temp.end()); i != end; ++i)
			{
				TEST_CHECK(duplicates.count(i->id) == 0);
				duplicates.insert(i->id);
			}
		}

		using namespace libtorrent::dht;

		char const* ips[] = {
			"124.31.75.21",
			"21.75.31.124",
			"65.23.51.170",
			"84.124.73.14",
			"43.213.53.83",
		};

		int rs[] = { 1,86,22,65,90 };

		boost::uint8_t prefixes[][3] =
		{
			{ 0x5f, 0xbf, 0xbf },
			{ 0x5a, 0x3c, 0xe9 },
			{ 0xa5, 0xd4, 0x32 },
			{ 0x1b, 0x03, 0x21 },
			{ 0xe5, 0x6f, 0x6c }
		};

		for (int i = 0; i < 5; ++i)
		{
			address a = address_v4::from_string(ips[i]);
			node_id id = generate_id_impl(a, rs[i]);
			TEST_CHECK(id[0] == prefixes[i][0]);
			TEST_CHECK(id[1] == prefixes[i][1]);
			TEST_CHECK((id[2] & 0xf8) == (prefixes[i][2] & 0xf8));

			TEST_CHECK(id[19] == rs[i]);
			fprintf(stderr, "IP address: %s r: %d node ID: %s\n", ips[i]
				, rs[i], to_hex(id.to_string()).c_str());
		}
	}

	// test traversal algorithms

	dht::key_desc_t find_node_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"t", lazy_entry::string_t, 2, 0},
		{"q", lazy_entry::string_t, 9, 0},
		{"a", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"id", lazy_entry::string_t, 20, 0},
			{"target", lazy_entry::string_t, 20, key_desc_t::optional},
			{"info_hash", lazy_entry::string_t, 20, key_desc_t::optional | key_desc_t::last_child},
	};

	dht::key_desc_t get_peers_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"t", lazy_entry::string_t, 2, 0},
		{"q", lazy_entry::string_t, 9, 0},
		{"a", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"id", lazy_entry::string_t, 20, 0},
			{"info_hash", lazy_entry::string_t, 20, key_desc_t::last_child},
	};

	dht::key_desc_t get_item_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"t", lazy_entry::string_t, 2, 0},
		{"q", lazy_entry::string_t, 3, 0},
		{"a", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"id", lazy_entry::string_t, 20, 0},
			{"target", lazy_entry::string_t, 20, key_desc_t::last_child},
	};

	// bootstrap

	g_sent_packets.clear();
	do
	{
		dht::node_impl node(&ad, &s, sett, node_id::min(), ext, 0);

		udp::endpoint initial_node(address_v4::from_string("4.4.4.4"), 1234);
		std::vector<udp::endpoint> nodesv;
		nodesv.push_back(initial_node);
		node.bootstrap(nodesv, boost::bind(&nop));

		TEST_EQUAL(g_sent_packets.size(), 1);
		if (g_sent_packets.empty()) break;
		TEST_EQUAL(g_sent_packets.front().first, initial_node);

		lazy_from_entry(g_sent_packets.front().second, response);
		ret = verify_message(&response, find_node_desc, parsed, 7, error_string, sizeof(error_string));
		if (ret)
		{
			TEST_EQUAL(parsed[0]->string_value(), "q");
			TEST_CHECK(parsed[2]->string_value() == "find_node"
				|| parsed[2]->string_value() == "get_peers");

			if (parsed[0]->string_value() != "q"
				|| (parsed[2]->string_value() != "find_node"
					&& parsed[2]->string_value() != "get_peers")) break;
		}
		else
		{
			fprintf(stderr, "   invalid find_node request: %s\n", print_entry(response).c_str());
			TEST_ERROR(error_string);
			break;
		}

		udp::endpoint found_node(address_v4::from_string("5.5.5.5"), 2235);
		nodes_t nodes;
		nodes.push_back(found_node);
		g_sent_packets.clear();
		send_dht_response(node, response, initial_node, nodes);

		TEST_EQUAL(g_sent_packets.size(), 1);
		if (g_sent_packets.empty()) break;
		TEST_EQUAL(g_sent_packets.front().first, found_node);

		lazy_from_entry(g_sent_packets.front().second, response);
		ret = verify_message(&response, find_node_desc, parsed, 7, error_string, sizeof(error_string));
		if (ret)
		{
			TEST_EQUAL(parsed[0]->string_value(), "q");
			TEST_CHECK(parsed[2]->string_value() == "find_node"
				|| parsed[2]->string_value() == "get_peers");
			if (parsed[0]->string_value() != "q" || (parsed[2]->string_value() != "find_node"
					&& parsed[2]->string_value() == "get_peers")) break;
		}
		else
		{
			fprintf(stderr, "   invalid find_node request: %s\n", print_entry(response).c_str());
			TEST_ERROR(error_string);
			break;
		}

		g_sent_packets.clear();
		send_dht_response(node, response, found_node);

		TEST_CHECK(g_sent_packets.empty());
		TEST_EQUAL(node.num_global_nodes(), 3);
	} while (false);

	// get_peers

	g_sent_packets.clear();
	do
	{
		dht::node_id target = to_hash("1234876923549721020394873245098347598635");
		dht::node_impl node(&ad, &s, sett, node_id::min(), ext, 0);

		udp::endpoint initial_node(address_v4::from_string("4.4.4.4"), 1234);
		node.m_table.add_node(initial_node);

		node.announce(target, 1234, false, get_peers_cb);

		TEST_EQUAL(g_sent_packets.size(), 1);
		if (g_sent_packets.empty()) break;
		TEST_EQUAL(g_sent_packets.front().first, initial_node);

		lazy_from_entry(g_sent_packets.front().second, response);
		ret = verify_message(&response, get_peers_desc, parsed, 6, error_string, sizeof(error_string));
		if (ret)
		{
			TEST_EQUAL(parsed[0]->string_value(), "q");
			TEST_EQUAL(parsed[2]->string_value(), "get_peers");
			TEST_EQUAL(parsed[5]->string_value(), target.to_string());
			if (parsed[0]->string_value() != "q" || parsed[2]->string_value() != "get_peers") break;
		}
		else
		{
			fprintf(stderr, "   invalid get_peers request: %s\n", print_entry(response).c_str());
			TEST_ERROR(error_string);
			break;
		}

		std::set<tcp::endpoint> peers[2];
		peers[0].insert(tcp::endpoint(address_v4::from_string("4.1.1.1"), 4111));
		peers[0].insert(tcp::endpoint(address_v4::from_string("4.1.1.2"), 4112));
		peers[0].insert(tcp::endpoint(address_v4::from_string("4.1.1.3"), 4113));

		udp::endpoint next_node(address_v4::from_string("5.5.5.5"), 2235);
		nodes_t nodes;
		nodes.push_back(next_node);

		g_sent_packets.clear();
		send_dht_response(node, response, initial_node, nodes, "10", 1234, peers[0]);

		TEST_EQUAL(g_sent_packets.size(), 1);
		if (g_sent_packets.empty()) break;
		TEST_EQUAL(g_sent_packets.front().first, next_node);

		lazy_from_entry(g_sent_packets.front().second, response);
		ret = verify_message(&response, get_peers_desc, parsed, 6, error_string, sizeof(error_string));
		if (ret)
		{
			TEST_EQUAL(parsed[0]->string_value(), "q");
			TEST_EQUAL(parsed[2]->string_value(), "get_peers");
			TEST_EQUAL(parsed[5]->string_value(), target.to_string());
			if (parsed[0]->string_value() != "q" || parsed[2]->string_value() != "get_peers") break;
		}
		else
		{
			fprintf(stderr, "   invalid get_peers request: %s\n", print_entry(response).c_str());
			TEST_ERROR(error_string);
			break;
		}

		peers[1].insert(tcp::endpoint(address_v4::from_string("4.1.1.4"), 4114));
		peers[1].insert(tcp::endpoint(address_v4::from_string("4.1.1.5"), 4115));
		peers[1].insert(tcp::endpoint(address_v4::from_string("4.1.1.6"), 4116));

		g_sent_packets.clear();
		send_dht_response(node, response, next_node, nodes_t(), "11", 1234, peers[1]);

		for (std::list<std::pair<udp::endpoint, entry> >::iterator i = g_sent_packets.begin()
			, end(g_sent_packets.end()); i != end; ++i)
		{
//			fprintf(stderr, " %s:%d: %s\n", i->first.address().to_string(ec).c_str()
//				, i->first.port(), i->second.to_string().c_str());
			TEST_EQUAL(i->second["q"].string(), "announce_peer");
		}

		g_sent_packets.clear();

		for (int i = 0; i < 2; ++i)
		{
			for (std::set<tcp::endpoint>::iterator peer = peers[i].begin(); peer != peers[i].end(); ++peer)
			{
				TEST_CHECK(std::find(g_got_peers.begin(), g_got_peers.end(), *peer) != g_got_peers.end());
			}
		}
		g_got_peers.clear();
	} while (false);

	// immutable get

	g_sent_packets.clear();
	do
	{
		dht::node_impl node(&ad, &s, sett, node_id::min(), ext, 0);

		udp::endpoint initial_node(address_v4::from_string("4.4.4.4"), 1234);
		node.m_table.add_node(initial_node);

		node.get_item(items[0].target, get_item_cb);

		TEST_EQUAL(g_sent_packets.size(), 1);
		if (g_sent_packets.empty()) break;
		TEST_EQUAL(g_sent_packets.front().first, initial_node);

		lazy_from_entry(g_sent_packets.front().second, response);
		ret = verify_message(&response, get_item_desc, parsed, 6, error_string, sizeof(error_string));
		if (ret)
		{
			TEST_EQUAL(parsed[0]->string_value(), "q");
			TEST_EQUAL(parsed[2]->string_value(), "get");
			TEST_EQUAL(parsed[5]->string_value(), items[0].target.to_string());
			if (parsed[0]->string_value() != "q" || parsed[2]->string_value() != "get") break;
		}
		else
		{
			fprintf(stderr, "   invalid get request: %s\n", print_entry(response).c_str());
			TEST_ERROR(error_string);
			break;
		}

		g_sent_packets.clear();
		send_dht_response(node, response, initial_node, nodes_t(), "10", 1234, std::set<tcp::endpoint>()
			, NULL, &items[0].ent);

		TEST_CHECK(g_sent_packets.empty());
		TEST_EQUAL(g_got_items.size(), 1);
		if (g_got_items.empty()) break;

		TEST_EQUAL(g_got_items.front().value(), items[0].ent);
		g_got_items.clear();

	} while (false);

	// mutable get

	g_sent_packets.clear();
	do
	{
		dht::node_impl node(&ad, &s, sett, node_id::min(), ext, 0);

		udp::endpoint initial_node(address_v4::from_string("4.4.4.4"), 1234);
		node.m_table.add_node(initial_node);

		sha1_hash target = hasher(public_key, item_pk_len).final();
		node.get_item(target, get_item_cb);

		TEST_EQUAL(g_sent_packets.size(), 1);
		if (g_sent_packets.empty()) break;
		TEST_EQUAL(g_sent_packets.front().first, initial_node);

		lazy_from_entry(g_sent_packets.front().second, response);
		ret = verify_message(&response, get_item_desc, parsed, 6, error_string, sizeof(error_string));
		if (ret)
		{
			TEST_EQUAL(parsed[0]->string_value(), "q");
			TEST_EQUAL(parsed[2]->string_value(), "get");
			TEST_EQUAL(parsed[5]->string_value(), target.to_string());
			if (parsed[0]->string_value() != "q" || parsed[2]->string_value() != "get") break;
		}
		else
		{
			fprintf(stderr, "   invalid get request: %s\n", print_entry(response).c_str());
			TEST_ERROR(error_string);
			break;
		}

		g_sent_packets.clear();

		itemv.second = bencode(buffer, items[0].ent);
		sign_mutable_item(itemv, empty_salt, seq, public_key, private_key, signature);
		send_dht_response(node, response, initial_node, nodes_t(), "10", 1234, std::set<tcp::endpoint>()
			, NULL, &items[0].ent, std::string(public_key, item_pk_len), std::string(signature, item_sig_len), seq);

		TEST_CHECK(g_sent_packets.empty());
		TEST_EQUAL(g_got_items.size(), 1);
		if (g_got_items.empty()) break;

		TEST_EQUAL(g_got_items.front().value(), items[0].ent);
		TEST_CHECK(memcmp(g_got_items.front().pk().data(), public_key, item_pk_len) == 0);
		TEST_CHECK(memcmp(g_got_items.front().sig().data(), signature, item_sig_len) == 0);
		TEST_EQUAL(g_got_items.front().seq(), seq);
		g_got_items.clear();

	} while (false);

	dht::key_desc_t put_immutable_item_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"t", lazy_entry::string_t, 2, 0},
		{"q", lazy_entry::string_t, 3, 0},
		{"a", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"id", lazy_entry::string_t, 20, 0},
			{"token", lazy_entry::string_t, 2, 0},
			{"v", lazy_entry::none_t, 0, key_desc_t::last_child},
	};

	dht::key_desc_t put_mutable_item_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"t", lazy_entry::string_t, 2, 0},
		{"q", lazy_entry::string_t, 3, 0},
		{"a", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"id", lazy_entry::string_t, 20, 0},
			{"cas", lazy_entry::string_t, 20, key_desc_t::optional},
			{"k", lazy_entry::string_t, item_pk_len, 0},
			{"seq", lazy_entry::int_t, 0, 0},
			{"sig", lazy_entry::string_t, item_sig_len, 0},
			{"token", lazy_entry::string_t, 2, 0},
			{"v", lazy_entry::none_t, 0, key_desc_t::last_child},
	};

	// immutable put
	g_sent_packets.clear();
	do
	{
		dht::node_impl node(&ad, &s, sett, node_id::min(), ext, 0);
		enum { num_test_nodes = 2 };
		node_entry nodes[num_test_nodes] =
			{ node_entry(generate_next(), udp::endpoint(address_v4::from_string("4.4.4.4"), 1234))
			, node_entry(generate_next(), udp::endpoint(address_v4::from_string("5.5.5.5"), 1235)) };

		for (int i = 0; i < num_test_nodes; ++i)
			node.m_table.add_node(nodes[i]);

		g_put_item.assign(items[0].ent);
		node.get_item(items[0].target, get_item_cb);

		TEST_EQUAL(g_sent_packets.size(), num_test_nodes);
		if (g_sent_packets.size() != num_test_nodes) break;

		for (int i = 0; i < num_test_nodes; ++i)
		{
			std::list<std::pair<udp::endpoint, entry> >::iterator packet = find_packet(nodes[i].ep());
			TEST_CHECK(packet != g_sent_packets.end());
			if (packet == g_sent_packets.end()) continue;

			lazy_from_entry(packet->second, response);
			ret = verify_message(&response, get_item_desc, parsed, 6, error_string, sizeof(error_string));
			if (!ret)
			{
				fprintf(stderr, "   invalid get request: %s\n", print_entry(response).c_str());
				TEST_ERROR(error_string);
				continue;
			}
			char t[10];
			snprintf(t, sizeof(t), "%02d", i);
			send_dht_response(node, response, nodes[i].ep(), nodes_t(), t, 1234,
				std::set<tcp::endpoint>(), 0, 0, std::string(), std::string(), -1, &nodes[i].id);
			g_sent_packets.erase(packet);
		}

		TEST_EQUAL(g_put_count, 1);
		TEST_EQUAL(g_sent_packets.size(), num_test_nodes);
		if (g_sent_packets.size() != num_test_nodes) break;

		itemv.second = bencode(buffer, items[0].ent);

		for (int i = 0; i < num_test_nodes; ++i)
		{
			std::list<std::pair<udp::endpoint, entry> >::iterator packet = find_packet(nodes[i].ep());
			TEST_CHECK(packet != g_sent_packets.end());
			if (packet == g_sent_packets.end()) continue;

			lazy_from_entry(packet->second, response);
			ret = verify_message(&response, put_immutable_item_desc, parsed, 7, error_string, sizeof(error_string));
			if (ret)
			{
				TEST_EQUAL(parsed[0]->string_value(), "q");
				TEST_EQUAL(parsed[2]->string_value(), "put");
				std::pair<const char*, int> v = parsed[6]->data_section();
				TEST_EQUAL(v.second, itemv.second);
				TEST_CHECK(memcmp(v.first, itemv.first, itemv.second) == 0);
				char t[10];
				snprintf(t, sizeof(t), "%02d", i);
				TEST_EQUAL(parsed[5]->string_value(), t);
				if (parsed[0]->string_value() != "q" || parsed[2]->string_value() != "put") continue;
			}
			else
			{
				fprintf(stderr, "   invalid immutable put request: %s\n", print_entry(response).c_str());
				TEST_ERROR(error_string);
				continue;
			}
		}

		g_sent_packets.clear();
		g_put_item.clear();
		g_put_count = 0;

	} while (false);

	// mutable put
	g_sent_packets.clear();
	do
	{
		dht::node_impl node(&ad, &s, sett, node_id::min(), ext, 0);
		enum { num_test_nodes = 2 };
		node_entry nodes[num_test_nodes] =
			{ node_entry(generate_next(), udp::endpoint(address_v4::from_string("4.4.4.4"), 1234))
			, node_entry(generate_next(), udp::endpoint(address_v4::from_string("5.5.5.5"), 1235)) };

		for (int i = 0; i < num_test_nodes; ++i)
			node.m_table.add_node(nodes[i]);

		sha1_hash target = hasher(public_key, item_pk_len).final();
		g_put_item.assign(items[0].ent, empty_salt, seq, public_key, private_key);
		std::string sig(g_put_item.sig().data(), item_sig_len);
		node.get_item(target, get_item_cb);

		TEST_EQUAL(g_sent_packets.size(), num_test_nodes);
		if (g_sent_packets.size() != num_test_nodes) break;

		for (int i = 0; i < num_test_nodes; ++i)
		{
			std::list<std::pair<udp::endpoint, entry> >::iterator packet = find_packet(nodes[i].ep());
			TEST_CHECK(packet != g_sent_packets.end());
			if (packet == g_sent_packets.end()) continue;

			lazy_from_entry(packet->second, response);
			ret = verify_message(&response, get_item_desc, parsed, 6, error_string, sizeof(error_string));
			if (!ret)
			{
				fprintf(stderr, "   invalid mutable put request: %s\n", print_entry(response).c_str());
				TEST_ERROR(error_string);
				continue;
			}
			char t[10];
			snprintf(t, sizeof(t), "%02d", i);
			send_dht_response(node, response, nodes[i].ep(), nodes_t(), t, 1234,
				std::set<tcp::endpoint>(), 0, 0, std::string(), std::string(), -1, &nodes[i].id);
			g_sent_packets.erase(packet);
		}

		TEST_EQUAL(g_put_count, 1);
		TEST_EQUAL(g_sent_packets.size(), num_test_nodes);
		if (g_sent_packets.size() != num_test_nodes) break;

		itemv.second = bencode(buffer, items[0].ent);

		for (int i = 0; i < num_test_nodes; ++i)
		{
			std::list<std::pair<udp::endpoint, entry> >::iterator packet = find_packet(nodes[i].ep());
			TEST_CHECK(packet != g_sent_packets.end());
			if (packet == g_sent_packets.end()) continue;

			lazy_from_entry(packet->second, response);
			ret = verify_message(&response, put_mutable_item_desc, parsed, 11, error_string, sizeof(error_string));
			if (ret)
			{
				TEST_EQUAL(parsed[0]->string_value(), "q");
				TEST_EQUAL(parsed[2]->string_value(), "put");
				TEST_EQUAL(parsed[6]->string_value(), std::string(public_key, item_pk_len));
				TEST_EQUAL(parsed[7]->int_value(), seq);
				TEST_EQUAL(parsed[8]->string_value(), sig);
				std::pair<const char*, int> v = parsed[10]->data_section();
				TEST_EQUAL(v.second, itemv.second);
				TEST_CHECK(memcmp(v.first, itemv.first, itemv.second) == 0);
				char t[10];
				snprintf(t, sizeof(t), "%02d", i);
				TEST_EQUAL(parsed[9]->string_value(), t);
				if (parsed[0]->string_value() != "q" || parsed[2]->string_value() != "put") continue;
			}
			else
			{
				fprintf(stderr, "   invalid put request: %s\n", print_entry(response).c_str());
				TEST_ERROR(error_string);
				continue;
			}
		}

		g_sent_packets.clear();
		g_put_item.clear();
		g_put_count = 0;

	} while (false);

	// test vector 1

	// test content
	std::pair<char const*, int> test_content("12:Hello World!", 15);
	// test salt
	std::pair<char const*, int> test_salt("foobar", 6);

	from_hex("77ff84905a91936367c01360803104f92432fcd904a43511876df5cdf3e7e548", 64, public_key);
	from_hex("e06d3183d14159228433ed599221b80bd0a5ce8352e4bdf0262f76786ef1c74d"
		"b7e7a9fea2c0eb269d61e3b38e450a22e754941ac78479d6c54e1faf6037881d", 128, private_key);

	sign_mutable_item(test_content, empty_salt, 1, public_key
		, private_key, signature);

	TEST_EQUAL(to_hex(std::string(signature, 64))
		, "305ac8aeb6c9c151fa120f120ea2cfb923564e11552d06a5d856091e5e853cff"
		"1260d3f39e4999684aa92eb73ffd136e6f4f3ecbfda0ce53a1608ecd7ae21f01");

	sha1_hash target_id = item_target_id(empty_salt, public_key);
	TEST_EQUAL(to_hex(target_id.to_string()), "4a533d47ec9c7d95b1ad75f576cffc641853b750");

	// test vector 2 (the keypair is the same as test 1)

	sign_mutable_item(test_content, test_salt, 1, public_key
		, private_key, signature);

	TEST_EQUAL(to_hex(std::string(signature, 64))
		, "6834284b6b24c3204eb2fea824d82f88883a3d95e8b4a21b8c0ded553d17d17d"
		"df9a8a7104b1258f30bed3787e6cb896fca78c58f8e03b5f18f14951a87d9a08");

	target_id = item_target_id(test_salt, public_key);
	TEST_EQUAL(to_hex(target_id.to_string()), "411eba73b6f087ca51a3795d9c8c938d365e32c1");

	// test vector 3

	target_id = item_target_id(test_content);
	TEST_EQUAL(to_hex(target_id.to_string()), "e5f96f6f38320f0f33959cb4d3d656452117aadb");

	return 0;
}

#else

int test_main()
{
	return 0;
}

#endif

