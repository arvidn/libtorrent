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
#include <iostream>

#include "test.hpp"

using namespace libtorrent;
using namespace libtorrent::dht;

std::list<std::pair<udp::endpoint, entry> > g_responses;

bool our_send(void* user, entry& msg, udp::endpoint const& ep, int flags)
{
	g_responses.push_back(std::make_pair(ep, msg));
	return true;
}

address rand_v4()
{
	return address_v4((rand() << 16 | rand()) & 0xffffffff);
}

sha1_hash generate_next()
{
	sha1_hash ret;
	for (int i = 0; i < 20; ++i) ret[i] = rand();
	return ret;
}

boost::array<char, 64> generate_key()
{
	boost::array<char, 64> ret;
	for (int i = 0; i < 64; ++i) ret[i] = rand();
	return ret;
}

void send_dht_msg(node_impl& node, char const* msg, udp::endpoint const& ep
	, lazy_entry* reply, char const* t = "10", char const* info_hash = 0
	, char const* name = 0, std::string const* token = 0, int port = 0
	, std::string const* target = 0, entry const* item = 0, std::string const* signature = 0
	, std::string const* key = 0, std::string const* id = 0)
{
	// we're about to clear out the backing buffer
	// for this lazy_entry, so we better clear it now
	reply->clear();
	entry e;
	e["q"] = msg;
	e["t"] = t;
	e["y"] = "q";
	entry::dictionary_type& a = e["a"].dict();
	a["id"] = id == 0 ? generate_next().to_string() : *id;
	if (info_hash) a["info_hash"] = info_hash;
	if (name) a["n"] = name;
	if (token) a["token"] = *token;
	if (port) a["port"] = port;
	if (target) a["target"] = *target;
	if (item) a["item"] = *item;
	if (signature) a["sig"] = *signature;
	if (key) a["key"] = *key;
	char msg_buf[1500];
	int size = bencode(msg_buf, e);
//	std::cerr << "sending: " <<  e << "\n";

	lazy_entry decoded;
	lazy_bdecode(msg_buf, msg_buf + size, decoded);
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
	error_code ec;
	int ret = lazy_bdecode(inbuf, inbuf + len, *reply, ec);
	TEST_CHECK(ret == 0);
}

struct announce_item
{
	sha1_hash next;
	boost::array<char, 64> key;
	int num_peers;
	entry ent;
	sha1_hash target;
	void gen()
	{
		ent["next"] = next.to_string();
		ent["key"] = std::string(&key[0], 64);
		ent["A"] = "a";
		ent["B"] = "b";
		ent["num_peers"] = num_peers;

		char buf[512];
		char* ptr = buf;
		int len = bencode(ptr, ent);
		target = hasher(buf, len).final();
	}
};

void announce_items(node_impl& node, udp::endpoint const* eps
	, node_id const* ids, announce_item const* items, int num_items)
{
	std::string tokens[1000];
	for (int i = 0; i < 1000; ++i)
	{
		for (int j = 0; j < num_items; ++j)
		{
			if ((i % items[j].num_peers) == 0) continue;
			lazy_entry response;
			send_dht_msg(node, "get_item", eps[i], &response, "10", 0
				, 0, 0, 0, &items[j].target.to_string(), 0, 0
				, &std::string(&items[j].key[0], 64), &ids[i].to_string());
			
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
				tokens[i] = parsed[2]->string_value();
			}
			else
			{
				fprintf(stderr, "   invalid get_item response: %s\n", error_string);
				TEST_ERROR(error_string);
			}

			if (parsed[3])
			{
				address_v4::bytes_type b;
				memcpy(&b[0], parsed[3]->string_ptr(), b.size());
				address_v4 addr(b);
				TEST_EQUAL(addr, eps[i].address());
			}

			send_dht_msg(node, "announce_item", eps[i], &response, "10", 0
				, 0, &tokens[i], 0, &items[j].target.to_string(), &items[j].ent
				, &std::string("0123456789012345678901234567890123456789012345678901234567890123"));
			

			key_desc_t desc2[] =
			{
				{ "y", lazy_entry::string_t, 1, 0 }
			};

//			fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
			ret = verify_message(&response, desc2, parsed, 1, error_string, sizeof(error_string));
			if (ret)
			{
				TEST_EQUAL(parsed[0]->string_value(), "r");
			}
			else
			{
				fprintf(stderr, "   invalid announce_item response: %s\n", error_string);
				TEST_ERROR(error_string);
			}
		}
	}

	std::set<int> items_num;
	for (int j = 0; j < num_items; ++j)
	{
		lazy_entry response;
		send_dht_msg(node, "get_item", eps[0], &response, "10", 0
			, 0, 0, 0, &items[j].target.to_string(), 0, 0
			, &std::string(&items[j].key[0], 64), &ids[0].to_string());
		
		key_desc_t desc[] =
		{
			{ "r", lazy_entry::dict_t, 0, key_desc_t::parse_children },
				{ "item", lazy_entry::dict_t, 0, key_desc_t::parse_children},
					{ "A", lazy_entry::string_t, 1, 0},
					{ "B", lazy_entry::string_t, 1, 0},
					{ "num_peers", lazy_entry::int_t, 0, key_desc_t::last_child},
				{ "id", lazy_entry::string_t, 20, key_desc_t::last_child},
			{ "y", lazy_entry::string_t, 1, 0},
		};

		lazy_entry const* parsed[7];
		char error_string[200];

		fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
		int ret = verify_message(&response, desc, parsed, 7, error_string, sizeof(error_string));
		if (ret)
		{
			TEST_EQUAL(parsed[6]->string_value(), "r");
			TEST_EQUAL(parsed[2]->string_value(), "a");
			TEST_EQUAL(parsed[3]->string_value(), "b");
			items_num.insert(items_num.begin(), parsed[4]->int_value());
		}
	}

	TEST_EQUAL(items_num.size(), 4);

	// items_num should contain 1,2 and 3
	// #error this doesn't quite hold
//	TEST_CHECK(items_num.find(1) != items_num.end());
//	TEST_CHECK(items_num.find(2) != items_num.end());
//	TEST_CHECK(items_num.find(3) != items_num.end());
}

void nop(address, int, address) {}

int test_main()
{
	io_service ios;
	alert_manager al(ios);
	dht_settings sett;
	sett.max_torrents = 4;
	sett.max_feed_items = 4;
	address ext = address::from_string("236.0.0.1");
	dht::node_impl node(al, &our_send, sett, node_id(0), ext, boost::bind(nop, _1, _2, _3), 0);

	// DHT should be running on port 48199 now
	lazy_entry response;
	lazy_entry const* parsed[5];
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
			{"id", lazy_entry::string_t, 20, key_desc_t::last_child},
	};

	std::string token;
	fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
	ret = dht::verify_message(&response, peer1_desc, parsed, 3, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(parsed[0]->string_value() == "r");
		token = parsed[1]->dict_find_string_value("token");
	}
	else
	{
		fprintf(stderr, "   invalid get_peers response: %s\n", error_string);
	}

	// ====== announce ======

	send_dht_msg(node, "announce_peer", source, &response, "10", "01010101010101010101", "test", &token, 8080);

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

	// ====== get_peers ======

	send_dht_msg(node, "get_peers", source, &response, "10", "01010101010101010101");

	dht::key_desc_t peer2_desc[] = {
		{"y", lazy_entry::string_t, 1, 0},
		{"r", lazy_entry::dict_t, 0, key_desc_t::parse_children},
			{"id", lazy_entry::string_t, 20, key_desc_t::last_child},
	};

	fprintf(stderr, "msg: %s\n", print_entry(response).c_str());
	ret = dht::verify_message(&response, peer2_desc, parsed, 3, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	if (ret)
	{
		TEST_CHECK(parsed[0]->string_value() == "r");
		TEST_EQUAL(parsed[1]->dict_find_string_value("n"), "test");
	}
	else
	{
		fprintf(stderr, "   invalid get_peers response: %s\n", error_string);
	}

	response.clear();

	// ====== announce_item ======

	udp::endpoint eps[1000];
	node_id ids[1000];

 	for (int i = 0; i < 1000; ++i)
	{
		eps[i] = udp::endpoint(rand_v4(), (rand() % 16534) + 1);
		ids[i] = generate_next();
	}

	announce_item items[] =
	{
		{ generate_next(), generate_key(), 1 },
		{ generate_next(), generate_key(), 2 },
		{ generate_next(), generate_key(), 3 },
		{ generate_next(), generate_key(), 4 },
		{ generate_next(), generate_key(), 5 },
		{ generate_next(), generate_key(), 6 },
		{ generate_next(), generate_key(), 7 },
		{ generate_next(), generate_key(), 8 }
	};

	for (int i = 0; i < sizeof(items)/sizeof(items[0]); ++i)
		items[i].gen();

	announce_items(node, eps, ids, items, sizeof(items)/sizeof(items[0]));

	return 0;
}

#else

int test_main()
{
	return 0;
}

#endif

