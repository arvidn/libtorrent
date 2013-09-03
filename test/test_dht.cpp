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

#include "libtorrent/session.hpp"
#include "libtorrent/kademlia/node.hpp" // for verify_message
#include "libtorrent/bencode.hpp"
#include "libtorrent/socket_io.hpp" // for hash_address
#include "libtorrent/broadcast_socket.hpp" // for supports_ipv6
#include "libtorrent/alert_dispatcher.hpp"

#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/routing_table.hpp"
#include <iostream>

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;
using namespace libtorrent::dht;

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

std::list<std::pair<udp::endpoint, entry> > g_responses;

struct mock_socket : udp_socket_interface
{
	bool send_packet(entry& msg, udp::endpoint const& ep, int flags)
	{
		g_responses.push_back(std::make_pair(ep, msg));
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
	for (int i = 0; i < 20; ++i) ret[i] = rand();
	return ret;
}

node_id random_id()
{
	node_id ret;
	for (int i = 0; i < 20; ++i)
		ret[i] = rand();
	return ret;
}

boost::array<char, 64> generate_key()
{
	boost::array<char, 64> ret;
	for (int i = 0; i < 64; ++i) ret[i] = rand();
	return ret;
}

static const std::string no;

void send_dht_msg(node_impl& node, char const* msg, udp::endpoint const& ep
	, lazy_entry* reply, char const* t = "10", char const* info_hash = 0
	, char const* name = 0, std::string const token = std::string(), int port = 0
	, char const* target = 0, entry const* value = 0
	, bool scrape = false, bool seed = false
	, std::string const key = std::string(), std::string const sig = std::string()
	, int seq = -1, char const* cas = 0)
{
	// we're about to clear out the backing buffer
	// for this lazy_entry, so we better clear it now
	reply->clear();
	entry e;
	e["q"] = msg;
	e["t"] = t;
	e["y"] = "q";
	entry::dictionary_type& a = e["a"].dict();
	a["id"] = generate_next().to_string();
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
	if (cas) a["cas"] = std::string(cas, 20);
	char msg_buf[1500];
	int size = bencode(msg_buf, e);
#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
// this yields a lot of output. too much
//	std::cerr << "sending: " <<  e << "\n";
#endif

	lazy_entry decoded;
	error_code ec;
	lazy_bdecode(msg_buf, msg_buf + size, decoded, ec);
	if (ec) fprintf(stderr, "lazy_bdecode failed: %s\n", ec.message().c_str());

	dht::msg m(decoded, ep);
	node.incoming(m);

	// by now the node should have invoked the send function and put the
	// response in g_responses

	std::list<std::pair<udp::endpoint, entry> >::iterator i
		= std::find_if(g_responses.begin(), g_responses.end()
			, boost::bind(&std::pair<udp::endpoint, entry>::first, _1) == ep);
	if (i == g_responses.end())
	{
		TEST_ERROR("not response from DHT node");
		return;
	}

	static char inbuf[1500];
	int len = bencode(inbuf, i->second);
	g_responses.erase(i);
	int ret = lazy_bdecode(inbuf, inbuf + len, *reply, ec);
	TEST_CHECK(ret == 0);
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
			send_dht_msg(node, "get", eps[i], &response, "10", 0
				, 0, no, 0, (char const*)&items[j].target[0]);
			
			key_desc_t desc[] =
			{
				{ "r", lazy_entry::dict_t, 0, key_desc_t::parse_children },
					{ "id", lazy_entry::string_t, 20, 0},
					{ "token", lazy_entry::string_t, 0, 0},
					{ "ip", lazy_entry::string_t, 0, key_desc_t::optional | key_desc_t::last_child},
				{ "y", lazy_entry::string_t, 1, 0},
			};

			lazy_entry const* parsed[5];
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

			send_dht_msg(node, "put", eps[i], &response, "10", 0
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
		send_dht_msg(node, "get", eps[j], &response, "10", 0
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

int test_main()
{
	dht_settings sett;
	sett.max_torrents = 4;
	sett.max_dht_items = 4;
	address ext = address::from_string("236.0.0.1");
	mock_socket s;
	print_alert ad;
	dht::node_impl node(&ad, &s, sett, node_id(0), ext, 0);

	// DHT should be running on port 48199 now
	lazy_entry response;
	lazy_entry const* parsed[10];
	char error_string[200];
	bool ret;

	// ====== ping ======
	udp::endpoint source(address::from_string("10.0.0.1"), 20);
	send_dht_msg(node, "ping", source, &response, "10");

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

	send_dht_msg(node, "find_node", source, &response, "10");

	dht::key_desc_t err_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"e", lazy_entry::list_t, 2, 0},
		{"r", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"id", lazy_entry::string_t, 20, key_desc_t::last_child},
	};

	fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
	ret = dht::verify_message(&response, err_desc, parsed, 4, error_string, sizeof(error_string));
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

	send_dht_msg(node, "get_peers", source, &response, "10", "01010101010101010101");

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

	send_dht_msg(node, "announce_peer", source, &response, "10", "01010101010101010101", "test", token, 8080);

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
		send_dht_msg(node, "get_peers", source, &response, "10", "01010101010101010101");
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
		send_dht_msg(node, "announce_peer", source, &response, "10", "01010101010101010101"
			, "test", token, 8080, 0, 0, false, i >= 50);
		response.clear();
	}

	// ====== get_peers ======

	send_dht_msg(node, "get_peers", source, &response, "10", "01010101010101010101"
		, 0, no, 0, 0, 0, true);

	dht::key_desc_t peer2_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"r", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"BFpe", lazy_entry::string_t, 256, 0},
			{"BFse", lazy_entry::string_t, 256, 0},
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

	// ==== get / put mutable items ===

	fprintf(stderr, "generating ed25519 keys\n");
	unsigned char seed[32];
	ed25519_create_seed(seed);
	unsigned char private_key[64];
	unsigned char public_key[32];

	ed25519_create_keypair(public_key, private_key, seed);
	fprintf(stderr, "pub: %s priv: %s\n"
		, to_hex(std::string((char*)public_key, 32)).c_str()
		, to_hex(std::string((char*)private_key, 64)).c_str());

	TEST_CHECK(ret);

	send_dht_msg(node, "get", source, &response, "10", 0
		, 0, no, 0, (char*)&hasher((char*)public_key, 32).final()[0]
		, 0, false, false, std::string(), std::string(), 64);
			
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
		fprintf(stderr, "got token: %s\n", token.c_str());
	}
	else
	{
		fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
		fprintf(stderr, "   invalid get response: %s\n%s\n"
			, error_string, print_entry(response).c_str());
		TEST_ERROR(error_string);
	}

	unsigned char signature[64];
	char buffer[1200];
	int seq = 4;
	int pos = snprintf(buffer, sizeof(buffer), "3:seqi%de1:v", seq);
	char* ptr = buffer + pos;
	pos += bencode(ptr, items[0].ent);
	ed25519_sign(signature, (unsigned char*)buffer, pos, private_key, public_key);

	send_dht_msg(node, "put", source, &response, "10", 0
		, 0, token, 0, 0, &items[0].ent, false, false
		, std::string((char*)public_key, 32)
		, std::string((char*)signature, 64), seq);

	key_desc_t desc2[] =
	{
		{ "y", lazy_entry::string_t, 1, 0 }
	};

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

	send_dht_msg(node, "get", source, &response, "10", 0
		, 0, no, 0, (char*)&hasher((char*)public_key, 32).final()[0]
		, 0, false, false, std::string(), std::string(), 64);
			
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
		char value[1020];
		char* ptr = value;
		int value_len = bencode(ptr, items[0].ent);
		TEST_EQUAL(value_len, parsed[2]->data_section().second);
		TEST_CHECK(memcmp(parsed[2]->data_section().first, value, value_len) == 0);

		TEST_EQUAL(seq, parsed[3]->int_value());

	}

	// === test CAS put ===

	// this is the hash that we expect to be there
	sha1_hash cas = hasher(buffer, pos).final();
	// increment sequence number
	++seq;
	pos = snprintf(buffer, sizeof(buffer), "3:seqi%de1:v", seq);
	ptr = buffer + pos;
	// put item 1
	pos += bencode(ptr, items[1].ent);
	ed25519_sign(signature, (unsigned char*)buffer, pos, private_key, public_key);

	send_dht_msg(node, "put", source, &response, "10", 0
		, 0, token, 0, 0, &items[1].ent, false, false
		, std::string((char*)public_key, 32)
		, std::string((char*)signature, 64), seq
		, (char const*)&cas[0]);

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

	// put the same message again. This should fail because the
	// CAS hash is outdated, it's not the hash of the value that's
	// stored anymore
	send_dht_msg(node, "put", source, &response, "10", 0
		, 0, token, 0, 0, &items[1].ent, false, false
		, std::string((char*)public_key, 32)
		, std::string((char*)signature, 64), seq
		, (char const*)&cas[0]);


	key_desc_t desc4[] =
	{
		{ "e", lazy_entry::list_t, 2, 0 },
		{ "y", lazy_entry::string_t, 1, 0},
	};

	ret = verify_message(&response, desc4, parsed, 2, error_string, sizeof(error_string));
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

// test routing table

	{
		sett.extended_routing_table = false;
		routing_table tbl(random_id(), 8, sett);
   
		// insert 256 nodes evenly distributed across the ID space.
		// we expect to fill the top 5 buckets
		for (int i = 0; i < 256; ++i)
		{
			node_id id = random_id();
			id[0] = i;
			tbl.node_seen(id, rand_ep(), 50);
		}
		TEST_EQUAL(tbl.num_active_buckets(), 6);
   
#if defined TORRENT_DHT_VERBOSE_LOGGING || defined TORRENT_DEBUG
		tbl.print_state(std::cerr);
#endif
	}

	{
		sett.extended_routing_table = true;
		routing_table tbl(random_id(), 8, sett);
		for (int i = 0; i < 256; ++i)
		{
			node_id id = random_id();
			id[0] = i;
			tbl.node_seen(id, rand_ep(), 50);
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

	// test kademlia functions

	using namespace libtorrent::dht;

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

	{
		// test kademlia routing table
		dht_settings s;
		//	s.restrict_routing_ips = false;
		node_id id = to_hash("3123456789abcdef01232456789abcdef0123456");
		dht::routing_table table(id, 10, s);
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
			table.node_seen(tmp, udp::endpoint(rand_v4(), rand()), 10);
			add_and_replace(tmp, diff);
		}
		TEST_EQUAL(table.num_active_buckets(), 11);
		TEST_CHECK(table.size().get<0>() > 10 * 10);
		//#error test num_global_nodes
		//#error test need_refresh

#if defined TORRENT_DHT_VERBOSE_LOGGING || defined TORRENT_DEBUG
		table.print_state(std::cerr);
#endif

		table.for_each_node(node_push_back, nop, &nodes);

		std::cout << "nodes: " << nodes.size() << std::endl;

		std::vector<node_entry> temp;

		std::generate(tmp.begin(), tmp.end(), &std::rand);
		table.find_node(tmp, temp, 0, nodes.size() * 2);
		std::cout << "returned: " << temp.size() << std::endl;
		TEST_EQUAL(temp.size(), nodes.size());

		std::generate(tmp.begin(), tmp.end(), &std::rand);
		table.find_node(tmp, temp, 0, 7);
		std::cout << "returned: " << temp.size() << std::endl;
		TEST_EQUAL(temp.size(), 7);

		std::sort(nodes.begin(), nodes.end(), boost::bind(&compare_ref
				, boost::bind(&node_entry::id, _1)
				, boost::bind(&node_entry::id, _2), tmp));

		int hits = 0;
		// This makes sure enough of the nodes returned are actually
		// part of the closest nodes
		for (std::vector<node_entry>::iterator i = temp.begin()
			, end(temp.end()); i != end; ++i)
		{
			int hit = std::find_if(nodes.begin(), nodes.end()
				, boost::bind(&node_entry::id, _1) == i->id) - nodes.begin();
			//		std::cerr << hit << std::endl;
			if (hit < int(temp.size())) ++hits;
		}
		std::cout << "hits: " << hits << std::endl;
		TEST_CHECK(hits == int(temp.size()));

		std::generate(tmp.begin(), tmp.end(), &std::rand);
		table.find_node(tmp, temp, 0, 15);
		std::cout << "returned: " << temp.size() << std::endl;
		TEST_EQUAL(int(temp.size()), (std::min)(15, int(nodes.size())));

		std::sort(nodes.begin(), nodes.end(), boost::bind(&compare_ref
				, boost::bind(&node_entry::id, _1)
				, boost::bind(&node_entry::id, _2), tmp));

		hits = 0;
		// This makes sure enough of the nodes returned are actually
		// part of the closest nodes
		for (std::vector<node_entry>::iterator i = temp.begin()
			, end(temp.end()); i != end; ++i)
		{
			int hit = std::find_if(nodes.begin(), nodes.end()
				, boost::bind(&node_entry::id, _1) == i->id) - nodes.begin();
			//		std::cerr << hit << std::endl;
			if (hit < int(temp.size())) ++hits;
		}
		std::cout << "hits: " << hits << std::endl;
		TEST_CHECK(hits == int(temp.size()));

		using namespace libtorrent::dht;

		char const* ips[] = {
			"124.31.75.21",
			"21.75.31.124",
			"65.23.51.170",
			"84.124.73.14",
			"43.213.53.83",
		};

		int rs[] = { 1,86,22,65,90 };

		boost::uint8_t prefixes[][4] =
		{
			{ 0x17, 0x12, 0xf6, 0xc7 },
			{ 0x94, 0x64, 0x06, 0xc1 },
			{ 0xfe, 0xfd, 0x92, 0x20 },
			{ 0xaf, 0x15, 0x46, 0xdd },
			{ 0xa9, 0xe9, 0x20, 0xbf }
		};

		for (int i = 0; i < 5; ++i)
		{
			address a = address_v4::from_string(ips[i]);
			node_id id = generate_id_impl(a, rs[i]);
			for (int j = 0; j < 4; ++j)
				TEST_CHECK(id[j] == prefixes[i][j]);
			TEST_CHECK(id[19] == rs[i]);
			fprintf(stderr, "IP address: %s r: %d node ID: %s\n", ips[i]
				, rs[i], to_hex(id.to_string()).c_str());
		}
	}
	return 0;
}

#else

int test_main()
{
	return 0;
}

#endif

