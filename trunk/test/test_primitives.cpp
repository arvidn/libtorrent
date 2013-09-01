/*

Copyright (c) 2008-2012, Arvid Norberg
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

#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/packet_buffer.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/timestamp_history.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/bloom_filter.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/rsa.hpp"
#include "libtorrent/ip_voter.hpp"
#include <boost/bind.hpp>
#include <iostream>
#include <set>

#include "test.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;

namespace libtorrent {
	TORRENT_EXPORT std::string sanitize_path(std::string const& p);
}

sha1_hash to_hash(char const* s)
{
	sha1_hash ret;
	from_hex(s, 40, (char*)&ret[0]);
	return ret;
}

namespace libtorrent
{
	// defined in torrent_info.cpp
	TORRENT_EXPORT bool verify_encoding(std::string& target, bool path = true);
}

address rand_v4()
{
	return address_v4((rand() << 16 | rand()) & 0xffffffff);
}

#if TORRENT_USE_IPV6
address rand_v6()
{
	address_v6::bytes_type bytes;
	for (int i = 0; i < bytes.size(); ++i) bytes[i] = rand();
	return address_v6(bytes);
}
#endif

int test_main()
{
	using namespace libtorrent;
	using namespace libtorrent::dht;
	error_code ec;
	int ret = 0;

	// make sure the retry interval keeps growing
	// on failing announces
	announce_entry ae("dummy");
	int last = 0;
	session_settings sett;
	sett.tracker_backoff = 250;
	for (int i = 0; i < 10; ++i)
	{
		ae.failed(sett, 5);
		int delay = ae.next_announce_in();
		TEST_CHECK(delay > last);
		last = delay;
		fprintf(stderr, "%d, ", delay);
	}
	fprintf(stderr, "\n");

#if defined TORRENT_USE_OPENSSL
	// test sign_rsa and verify_rsa
	char private_key[1192];
	int private_len = sizeof(private_key);
	char public_key[268];
	int public_len = sizeof(public_key);

	ret = generate_rsa_keys(public_key, &public_len, private_key, &private_len, 2048);
	fprintf(stderr, "keysizes: pub: %d priv: %d\n", public_len, private_len);

	TEST_CHECK(ret);

	char test_message[1024];
	std::generate(test_message, test_message + 1024, &std::rand);

	char signature[256];
	int sig_len = sign_rsa(hasher(test_message, sizeof(test_message)).final()
		, private_key, private_len, signature, sizeof(signature));

	TEST_CHECK(sig_len == 256);

	ret = verify_rsa(hasher(test_message, sizeof(test_message)).final()
		, public_key, public_len, signature, sig_len);
	TEST_CHECK(ret == 1);
#endif

	// test external ip voting
	external_ip ipv1;

	// test a single malicious node
	// adds 50 legitimate responses from different peers
	// and 50 malicious responses from the same peer
	address real_external = address_v4::from_string("5.5.5.5", ec);
	TEST_CHECK(!ec);
	address malicious = address_v4::from_string("4.4.4.4", ec);
	TEST_CHECK(!ec);
	for (int i = 0; i < 50; ++i)
	{
		ipv1.cast_vote(real_external, aux::session_impl::source_dht, rand_v4());
		ipv1.cast_vote(rand_v4(), aux::session_impl::source_dht, malicious);
	}
	TEST_CHECK(ipv1.external_address(rand_v4()) == real_external);

	external_ip ipv2;

	// test a single malicious node
	// adds 50 legitimate responses from different peers
	// and 50 consistent malicious responses from the same peer
	address real_external1 = address_v4::from_string("5.5.5.5", ec);
	TEST_CHECK(!ec);
	address real_external2;
#if TORRENT_USE_IPV6
	if (supports_ipv6())
	{
		real_external2 = address_v6::from_string("2f80::", ec);
		TEST_CHECK(!ec);
	}
#endif
	malicious = address_v4::from_string("4.4.4.4", ec);
	TEST_CHECK(!ec);
	address malicious_external = address_v4::from_string("3.3.3.3", ec);
	TEST_CHECK(!ec);
	for (int i = 0; i < 50; ++i)
	{
		ipv2.cast_vote(real_external1, aux::session_impl::source_dht, rand_v4());
#if TORRENT_USE_IPV6
		if (supports_ipv6())
			ipv2.cast_vote(real_external2, aux::session_impl::source_dht, rand_v6());
#endif
		ipv2.cast_vote(malicious_external, aux::session_impl::source_dht, malicious);
	}
	TEST_CHECK(ipv2.external_address(rand_v4()) == real_external1);
#if TORRENT_USE_IPV6
	if (supports_ipv6())
		TEST_CHECK(ipv2.external_address(rand_v6()) == real_external2);
#endif

	// test bloom_filter
	bloom_filter<32> filter;
	sha1_hash k1 = hasher("test1", 5).final();
	sha1_hash k2 = hasher("test2", 5).final();
	sha1_hash k3 = hasher("test3", 5).final();
	sha1_hash k4 = hasher("test4", 5).final();
	TEST_CHECK(!filter.find(k1));
	TEST_CHECK(!filter.find(k2));
	TEST_CHECK(!filter.find(k3));
	TEST_CHECK(!filter.find(k4));

	filter.set(k1);
	TEST_CHECK(filter.find(k1));
	TEST_CHECK(!filter.find(k2));
	TEST_CHECK(!filter.find(k3));
	TEST_CHECK(!filter.find(k4));

	filter.set(k4);
	TEST_CHECK(filter.find(k1));
	TEST_CHECK(!filter.find(k2));
	TEST_CHECK(!filter.find(k3));
	TEST_CHECK(filter.find(k4));

	// test timestamp_history
	{
		timestamp_history h;
		TEST_EQUAL(h.add_sample(0x32, false), 0);
		TEST_EQUAL(h.base(), 0x32);
		TEST_EQUAL(h.add_sample(0x33, false), 0x1);
		TEST_EQUAL(h.base(), 0x32);
		TEST_EQUAL(h.add_sample(0x3433, false), 0x3401);
		TEST_EQUAL(h.base(), 0x32);
		TEST_EQUAL(h.add_sample(0x30, false), 0);
		TEST_EQUAL(h.base(), 0x30);

		// test that wrapping of the timestamp is properly handled
		h.add_sample(0xfffffff3, false);
		TEST_EQUAL(h.base(), 0xfffffff3);


		// TODO: test the case where we have > 120 samples (and have the base delay actually be updated)
		// TODO: test the case where a sample is lower than the history entry but not lower than the base
	}

	// test packet_buffer
	{
		packet_buffer pb;

		TEST_EQUAL(pb.capacity(), 0);
		TEST_EQUAL(pb.size(), 0);
		TEST_EQUAL(pb.span(), 0);

		pb.insert(123, (void*)123);
		TEST_EQUAL(pb.at(123 + 16), 0);
		
		TEST_CHECK(pb.at(123) == (void*)123);
		TEST_CHECK(pb.capacity() > 0);
		TEST_EQUAL(pb.size(), 1);
		TEST_EQUAL(pb.span(), 1);
		TEST_EQUAL(pb.cursor(), 123);

		pb.insert(125, (void*)125);

		TEST_CHECK(pb.at(125) == (void*)125);
		TEST_EQUAL(pb.size(), 2);
		TEST_EQUAL(pb.span(), 3);
		TEST_EQUAL(pb.cursor(), 123);

		pb.insert(500, (void*)500);
		TEST_EQUAL(pb.size(), 3);
		TEST_EQUAL(pb.span(), 501 - 123);
		TEST_EQUAL(pb.capacity(), 512);

		pb.insert(500, (void*)501);
		TEST_EQUAL(pb.size(), 3);
		pb.insert(500, (void*)500);
		TEST_EQUAL(pb.size(), 3);

		TEST_CHECK(pb.remove(123) == (void*)123);
		TEST_EQUAL(pb.size(), 2);
		TEST_EQUAL(pb.span(), 501 - 125);
		TEST_EQUAL(pb.cursor(), 125);
		TEST_CHECK(pb.remove(125) == (void*)125);
		TEST_EQUAL(pb.size(), 1);
		TEST_EQUAL(pb.span(), 1);
		TEST_EQUAL(pb.cursor(), 500);

		TEST_CHECK(pb.remove(500) == (void*)500);
		TEST_EQUAL(pb.size(), 0);
		TEST_EQUAL(pb.span(), 0);

		for (int i = 0; i < 0xff; ++i)
		{
			int index = (i + 0xfff0) & 0xffff;
			pb.insert(index, (void*)(index + 1));
			fprintf(stderr, "insert: %u (mask: %x)\n", index, int(pb.capacity() - 1));
			TEST_EQUAL(pb.capacity(), 512);
			if (i >= 14)
			{
				index = (index - 14) & 0xffff;
				fprintf(stderr, "remove: %u\n", index);
				TEST_CHECK(pb.remove(index) == (void*)(index + 1));
				TEST_EQUAL(pb.size(), 14);
			}
		}
	}

	{
		// test wrapping the indices
		packet_buffer pb;

		TEST_EQUAL(pb.size(), 0);

		pb.insert(0xfffe, (void*)1);
		TEST_CHECK(pb.at(0xfffe) == (void*)1);

		pb.insert(2, (void*)2);
		TEST_CHECK(pb.at(2) == (void*)2);

		pb.remove(0xfffe);
		TEST_CHECK(pb.at(0xfffe) == (void*)0);
		TEST_CHECK(pb.at(2) == (void*)2);
	}

	{
		// test wrapping the indices
		packet_buffer pb;

		TEST_EQUAL(pb.size(), 0);

		pb.insert(0xfff3, (void*)1);
		TEST_CHECK(pb.at(0xfff3) == (void*)1);

		int new_index = (0xfff3 + pb.capacity()) & 0xffff;
		pb.insert(new_index, (void*)2);
		TEST_CHECK(pb.at(new_index) == (void*)2);

		void* old = pb.remove(0xfff3);
		TEST_CHECK(old == (void*)1);
		TEST_CHECK(pb.at(0xfff3) == (void*)0);
		TEST_CHECK(pb.at(new_index) == (void*)2);
	}

	{
		// test wrapping the indices backwards
		packet_buffer pb;

		TEST_EQUAL(pb.size(), 0);

		pb.insert(0xfff3, (void*)1);
		TEST_CHECK(pb.at(0xfff3) == (void*)1);

		int new_index = (0xfff3 + pb.capacity()) & 0xffff;
		pb.insert(new_index, (void*)2);
		TEST_CHECK(pb.at(new_index) == (void*)2);

		void* old = pb.remove(0xfff3);
		TEST_CHECK(old == (void*)1);
		TEST_CHECK(pb.at(0xfff3) == (void*)0);
		TEST_CHECK(pb.at(new_index) == (void*)2);

		pb.insert(0xffff, (void*)0xffff);
	}

	// test error codes
	TEST_CHECK(error_code(errors::http_error).message() == "HTTP error");
	TEST_CHECK(error_code(errors::missing_file_sizes).message() == "missing or invalid 'file sizes' entry");
	TEST_CHECK(error_code(errors::unsupported_protocol_version).message() == "unsupported protocol version");
	TEST_CHECK(error_code(errors::no_i2p_router).message() == "no i2p router is set up");
	TEST_CHECK(error_code(errors::http_parse_error).message() == "Invalid HTTP header");
	TEST_CHECK(error_code(errors::error_code_max).message() == "Unknown error");

	TEST_CHECK(error_code(errors::unauthorized, get_http_category()).message() == "401 Unauthorized");
	TEST_CHECK(error_code(errors::service_unavailable, get_http_category()).message() == "503 Service Unavailable");

	session_proxy p1;
	session_proxy p2;
	{
	// test session state load/restore
	session* s = new session(fingerprint("LT",0,0,0,0), 0);

	session_settings sett;
	sett.user_agent = "test";
	sett.tracker_receive_timeout = 1234;
	sett.file_pool_size = 543;
	sett.urlseed_wait_retry = 74;
	sett.file_pool_size = 754;
	sett.initial_picker_threshold = 351;
	sett.upnp_ignore_nonrouters = 5326;
	sett.coalesce_writes = 623;
	sett.auto_scrape_interval = 753;
	sett.close_redundant_connections = 245;
	sett.auto_scrape_interval = 235;
	sett.auto_scrape_min_interval = 62;
	s->set_settings(sett);

#ifndef TORRENT_DISABLE_DHT
	dht_settings dhts;
	dhts.max_peers_reply = 70;
	s->set_dht_settings(dhts);
#endif
/*
#ifndef TORRENT_DISABLE_DHT
	dht_settings dht_sett;
	s->set_dht_settings(dht_sett);
#endif
*/
	entry session_state;
	s->save_state(session_state);

	// test magnet link parsing
	add_torrent_params p;
	p.save_path = ".";
	error_code ec;
	p.url = "magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&tr=http://1"
		"&tr=http://2"
		"&tr=http://3"
		"&dn=foo"
		"&dht=127.0.0.1:43";
	torrent_handle t = s->add_torrent(p, ec);
	TEST_CHECK(!ec);
	if (ec) fprintf(stderr, "%s\n", ec.message().c_str());

	std::vector<announce_entry> trackers = t.trackers();
	TEST_EQUAL(trackers.size(), 3);
	std::set<std::string> trackers_set;
	for (std::vector<announce_entry>::iterator i = trackers.begin()
		, end(trackers.end()); i != end; ++i)
		trackers_set.insert(i->url);
	

	TEST_CHECK(trackers_set.count("http://1") == 1);
	TEST_CHECK(trackers_set.count("http://2") == 1);
	TEST_CHECK(trackers_set.count("http://3") == 1);

	p.url = "magnet:"
		"?tr=http://1"
		"&tr=http://2"
		"&dn=foo"
		"&dht=127.0.0.1:43"
		"&xt=urn:btih:c352cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd";
	torrent_handle t2 = s->add_torrent(p, ec);
	TEST_CHECK(!ec);
	if (ec) fprintf(stderr, "%s\n", ec.message().c_str());

	trackers = t2.trackers();
	TEST_EQUAL(trackers.size(), 2);

	p.url = "magnet:"
		"?tr=udp%3A%2F%2Ftracker.openbittorrent.com%3A80"
		"&tr=udp%3A%2F%2Ftracker.publicbt.com%3A80"
		"&tr=udp%3A%2F%2Ftracker.ccc.de%3A80"
		"&xt=urn:btih:a38d02c287893842a32825aa866e00828a318f07"
		"&dn=Ubuntu+11.04+%28Final%29";
	torrent_handle t3 = s->add_torrent(p, ec);
	TEST_CHECK(!ec);
	if (ec) fprintf(stderr, "%s\n", ec.message().c_str());

	trackers = t3.trackers();
	TEST_EQUAL(trackers.size(), 3);
	if (trackers.size() > 0)
	{
		TEST_EQUAL(trackers[0].url, "udp://tracker.openbittorrent.com:80");
		fprintf(stderr, "1: %s\n", trackers[0].url.c_str());
	}
	if (trackers.size() > 1)
	{
		TEST_EQUAL(trackers[1].url, "udp://tracker.publicbt.com:80");
		fprintf(stderr, "2: %s\n", trackers[1].url.c_str());
	}
	if (trackers.size() > 2)
	{
		TEST_EQUAL(trackers[2].url, "udp://tracker.ccc.de:80");
		fprintf(stderr, "3: %s\n", trackers[2].url.c_str());
	}

	TEST_EQUAL(to_hex(t.info_hash().to_string()), "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");

	p1 = s->abort();
	delete s;
	s = new session(fingerprint("LT",0,0,0,0), 0);

	std::vector<char> buf;
	bencode(std::back_inserter(buf), session_state);
	lazy_entry session_state2;
	ret = lazy_bdecode(&buf[0], &buf[0] + buf.size(), session_state2, ec);
	TEST_CHECK(ret == 0);

	fprintf(stderr, "session_state\n%s\n", print_entry(session_state2).c_str());

	// parse_magnet_uri
	parse_magnet_uri("magnet:?dn=foo&dht=127.0.0.1:43", p, ec);
	TEST_CHECK(ec == error_code(errors::missing_info_hash_in_uri));
	ec.clear();

	parse_magnet_uri("magnet:?xt=blah&dn=foo&dht=127.0.0.1:43", p, ec);
	TEST_CHECK(ec == error_code(errors::missing_info_hash_in_uri));
	ec.clear();

#ifndef TORRENT_DISABLE_DHT
	parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd&dn=foo&dht=127.0.0.1:43", p, ec);
	TEST_CHECK(!ec);
	if (ec) fprintf(stderr, "%s\n", ec.message().c_str());
	ec.clear();

	TEST_CHECK(p.dht_nodes.size() == 1);
	TEST_CHECK(p.dht_nodes[0].first == "127.0.0.1");
	TEST_CHECK(p.dht_nodes[0].second == 43);
#endif

	// make sure settings that haven't been changed from their defaults are not saved
	TEST_CHECK(session_state2.dict_find("settings")->dict_find("optimistic_disk_retry") == 0);

	s->load_state(session_state2);
#define CMP_SET(x) TEST_CHECK(s->settings().x == sett.x)

	CMP_SET(user_agent);
	CMP_SET(tracker_receive_timeout);
	CMP_SET(file_pool_size);
	CMP_SET(urlseed_wait_retry);
	CMP_SET(file_pool_size);
	CMP_SET(initial_picker_threshold);
	CMP_SET(upnp_ignore_nonrouters);
	CMP_SET(coalesce_writes);
	CMP_SET(auto_scrape_interval);
	CMP_SET(close_redundant_connections);
	CMP_SET(auto_scrape_interval);
	CMP_SET(auto_scrape_min_interval);
	CMP_SET(max_peerlist_size);
	CMP_SET(max_paused_peerlist_size);
	CMP_SET(min_announce_interval);
	CMP_SET(prioritize_partial_pieces);
	CMP_SET(auto_manage_startup);
	CMP_SET(rate_limit_ip_overhead);
	CMP_SET(announce_to_all_trackers);
	CMP_SET(announce_to_all_tiers);
	CMP_SET(prefer_udp_trackers);
	CMP_SET(strict_super_seeding);
	CMP_SET(seeding_piece_quota);
	p2 = s->abort();
	delete s;
	}

	// test snprintf

	char msg[10];
	snprintf(msg, sizeof(msg), "too %s format string", "long");
	TEST_CHECK(strcmp(msg, "too long ") == 0);

	// test sanitize_path

#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize_path("/a/b/c"), "a\\b\\c");
	TEST_EQUAL(sanitize_path("a/../c"), "a\\c");
#else
	TEST_EQUAL(sanitize_path("/a/b/c"), "a/b/c");
	TEST_EQUAL(sanitize_path("a/../c"), "a/c");
#endif
	TEST_EQUAL(sanitize_path("/.././c"), "c");
	TEST_EQUAL(sanitize_path("dev:"), "");
	TEST_EQUAL(sanitize_path("c:/b"), "b");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize_path("c:\\.\\c"), "c");
	TEST_EQUAL(sanitize_path("\\c"), "c");
#else
	TEST_EQUAL(sanitize_path("//./c"), "c");
#endif

	// make sure the time classes have correct semantics

	TEST_EQUAL(total_milliseconds(milliseconds(100)), 100);
	TEST_EQUAL(total_milliseconds(milliseconds(1)),  1);
	TEST_EQUAL(total_milliseconds(seconds(1)), 1000);


	if (supports_ipv6())
	{
		// make sure the assumption we use in policy's peer list hold
		std::multimap<address, int> peers;
		std::multimap<address, int>::iterator i;
		peers.insert(std::make_pair(address::from_string("::1", ec), 0));
		peers.insert(std::make_pair(address::from_string("::2", ec), 3));
		peers.insert(std::make_pair(address::from_string("::3", ec), 5));
		i = peers.find(address::from_string("::2", ec));
		TEST_CHECK(i != peers.end());
		if (i != peers.end())
		{
			TEST_CHECK(i->first == address::from_string("::2", ec));
			TEST_CHECK(i->second == 3);
		}
	}

	// test identify_client

	TEST_CHECK(identify_client(peer_id("-AZ1234-............")) == "Azureus 1.2.3.4");
	TEST_CHECK(identify_client(peer_id("-AZ1230-............")) == "Azureus 1.2.3");
	TEST_CHECK(identify_client(peer_id("S123--..............")) == "Shadow 1.2.3");
	TEST_CHECK(identify_client(peer_id("M1-2-3--............")) == "Mainline 1.2.3");

	// verify_encoding
	std::string test = "\b?filename=4";
	TEST_CHECK(!verify_encoding(test));
#ifdef TORRENT_WINDOWS
	TEST_CHECK(test == "__filename=4");
#else
	TEST_CHECK(test == "_?filename=4");
#endif

	test = "filename=4";
	TEST_CHECK(verify_encoding(test));
	TEST_CHECK(test == "filename=4");

	// valid 2-byte sequence
	test = "filename\xc2\xa1";
	TEST_CHECK(verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename\xc2\xa1");

	// truncated 2-byte sequence
	test = "filename\xc2";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// valid 3-byte sequence
	test = "filename\xe2\x9f\xb9";
	TEST_CHECK(verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename\xe2\x9f\xb9");

	// truncated 3-byte sequence
	test = "filename\xe2\x9f";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// truncated 3-byte sequence
	test = "filename\xe2";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// valid 4-byte sequence
	test = "filename\xf0\x9f\x92\x88";
	TEST_CHECK(verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename\xf0\x9f\x92\x88");

	// truncated 4-byte sequence
	test = "filename\xf0\x9f\x92";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// 5-byte utf-8 sequence (not allowed)
	test = "filename\xf8\x9f\x9f\x9f\x9f""foobar";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_____foobar");

	// trim_path_element

	fprintf(stderr, "TORRENT_MAX_PATH: %d\n", TORRENT_MAX_PATH);

	// 1100 characters
	test = "abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij012345.txt";
	std::string comparison = test;
	trim_path_element(test);
	if (comparison.size() > TORRENT_MAX_PATH)
	{
		comparison.resize(TORRENT_MAX_PATH - 4);
		comparison += ".txt"; // the extension is supposed to be preserved
	}
	TEST_EQUAL(test, comparison);

	// extensions > 15 characters are ignored
	test = "abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij.123456789abcdefghij0123456789";
	comparison = test;
	trim_path_element(test);
	if (comparison.size() > TORRENT_MAX_PATH)
		comparison.resize(TORRENT_MAX_PATH);
	TEST_EQUAL(test, comparison);

	// test network functions

	TEST_CHECK(is_local(address::from_string("192.168.0.1", ec)));
	TEST_CHECK(is_local(address::from_string("10.1.1.56", ec)));
	TEST_CHECK(!is_local(address::from_string("14.14.251.63", ec)));
	TEST_CHECK(is_loopback(address::from_string("127.0.0.1", ec)));
#if TORRENT_USE_IPV6
	if (supports_ipv6())
	{
		TEST_CHECK(is_loopback(address::from_string("::1", ec)));
		TEST_CHECK(is_any(address_v6::any()));
	}
#endif
	TEST_CHECK(is_any(address_v4::any()));
	TEST_CHECK(!is_any(address::from_string("31.53.21.64", ec)));
	
	TEST_CHECK(match_addr_mask(
		address::from_string("10.0.1.3", ec),
		address::from_string("10.0.3.3", ec),
		address::from_string("255.255.0.0", ec)));

	TEST_CHECK(!match_addr_mask(
		address::from_string("10.0.1.3", ec),
		address::from_string("10.1.3.3", ec),
		address::from_string("255.255.0.0", ec)));

	// test torrent parsing

	entry info;
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["name.utf-8"] = "test1";
	info["name"] = "test__";
	info["piece length"] = 16 * 1024;
	info["length"] = 3245;
	entry torrent;
	torrent["info"] = info;

	std::vector<char> buf;
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti(&buf[0], buf.size(), ec);
	std::cerr << ti.name() << std::endl;
	TEST_CHECK(ti.name() == "test1");

#ifdef TORRENT_WINDOWS
	info["name.utf-8"] = "c:/test1/test2/test3";
#else
	info["name.utf-8"] = "/test1/test2/test3";
#endif
	torrent["info"] = info;
	buf.clear();
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti2(&buf[0], buf.size(), ec);
	std::cerr << ti2.name() << std::endl;
#ifdef TORRENT_WINDOWS
	TEST_CHECK(ti2.name() == "test1\\test2\\test3");
#else
	TEST_CHECK(ti2.name() == "test1/test2/test3");
#endif

	info["name.utf-8"] = "test2/../test3/.././../../test4";
	torrent["info"] = info;
	buf.clear();
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti3(&buf[0], buf.size(), ec);
	std::cerr << ti3.name() << std::endl;
#ifdef TORRENT_WINDOWS
	TEST_CHECK(ti3.name() == "test2\\test3\\test4");
#else
	TEST_CHECK(ti3.name() == "test2/test3/test4");
#endif

	// test peer_id/sha1_hash type

	sha1_hash h1(0);
	sha1_hash h2(0);
	TEST_CHECK(h1 == h2);
	TEST_CHECK(!(h1 != h2));
	TEST_CHECK(!(h1 < h2));
	TEST_CHECK(!(h1 < h2));
	TEST_CHECK(h1.is_all_zeros());

	h1 = to_hash("0123456789012345678901234567890123456789");
	h2 = to_hash("0113456789012345678901234567890123456789");

	TEST_CHECK(h2 < h1);
	TEST_CHECK(h2 == h2);
	TEST_CHECK(h1 == h1);
	h2.clear();
	TEST_CHECK(h2.is_all_zeros());
	
	h2 = to_hash("ffffffffff0000000000ffffffffff0000000000");
	h1 = to_hash("fffff00000fffff00000fffff00000fffff00000");
	h1 &= h2;
	TEST_CHECK(h1 == to_hash("fffff000000000000000fffff000000000000000"));

	h2 = to_hash("ffffffffff0000000000ffffffffff0000000000");
	h1 = to_hash("fffff00000fffff00000fffff00000fffff00000");
	h1 |= h2;
	TEST_CHECK(h1 == to_hash("fffffffffffffff00000fffffffffffffff00000"));
	
	h2 = to_hash("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
	h1 ^= h2;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("f0f0f0f0f0f0f0ff0f0ff0f0f0f0f0f0f0ff0f0f"));
	TEST_CHECK(h1 != h2);

	h2 = sha1_hash("                    ");
	TEST_CHECK(h2 == to_hash("2020202020202020202020202020202020202020"));

	h1 = to_hash("ffffffffff0000000000ffffffffff0000000000");
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	h1 <<= 12;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("fffffff0000000000ffffffffff0000000000000"));
	h1 >>= 12;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("000fffffff0000000000ffffffffff0000000000"));

	h1 = to_hash("7000000000000000000000000000000000000000");
	h1 <<= 1;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("e000000000000000000000000000000000000000"));

	h1 = to_hash("0000000000000000000000000000000000000007");
	h1 <<= 1;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("000000000000000000000000000000000000000e"));

	h1 = to_hash("0000000000000000000000000000000000000007");
	h1 >>= 1;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("0000000000000000000000000000000000000003"));

	h1 = to_hash("7000000000000000000000000000000000000000");
	h1 >>= 1;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("3800000000000000000000000000000000000000"));
	
	// CIDR distance test
	h1 = to_hash("0123456789abcdef01232456789abcdef0123456");
	h2 = to_hash("0123456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 160);
	h2 = to_hash("0120456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 14);
	h2 = to_hash("012f456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 12);
	h2 = to_hash("0123456789abcdef11232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 16 * 4 + 3);


	// test bitfield
	bitfield test1(10, false);
	TEST_CHECK(test1.count() == 0);
	test1.set_bit(9);
	TEST_CHECK(test1.count() == 1);
	test1.clear_bit(9);
	TEST_CHECK(test1.count() == 0);
	test1.set_bit(2);
	TEST_CHECK(test1.count() == 1);
	test1.set_bit(1);
	test1.set_bit(9);
	TEST_CHECK(test1.count() == 3);
	TEST_CHECK(test1.all_set() == false);
	test1.clear_bit(2);
	TEST_CHECK(test1.count() == 2);
	int distance = std::distance(test1.begin(), test1.end());
	std::cerr << distance << std::endl;
	TEST_CHECK(distance == 10);

	test1.set_all();
	TEST_CHECK(test1.count() == 10);

	test1.clear_all();
	TEST_CHECK(test1.count() == 0);

	test1.resize(2);
	test1.set_bit(0);
	test1.resize(16, true);
	TEST_CHECK(test1.count() == 15);
	test1.resize(20, true);
	TEST_CHECK(test1.count() == 19);
	test1.set_bit(1);
	test1.resize(1);
	TEST_CHECK(test1.count() == 1);

	test1.resize(100, true);
	TEST_CHECK(test1.all_set() == true);

	// test merkle_*() functions

	// this is the structure:
	//             0
	//      1              2
	//   3      4       5       6
	//  7 8    9 10   11 12   13 14
	// num_leafs = 8

	TEST_EQUAL(merkle_num_leafs(1), 1);
	TEST_EQUAL(merkle_num_leafs(2), 2);
	TEST_EQUAL(merkle_num_leafs(3), 4);
	TEST_EQUAL(merkle_num_leafs(4), 4);
	TEST_EQUAL(merkle_num_leafs(5), 8);
	TEST_EQUAL(merkle_num_leafs(6), 8);
	TEST_EQUAL(merkle_num_leafs(7), 8);
	TEST_EQUAL(merkle_num_leafs(8), 8);
	TEST_EQUAL(merkle_num_leafs(9), 16);
	TEST_EQUAL(merkle_num_leafs(10), 16);
	TEST_EQUAL(merkle_num_leafs(11), 16);
	TEST_EQUAL(merkle_num_leafs(12), 16);
	TEST_EQUAL(merkle_num_leafs(13), 16);
	TEST_EQUAL(merkle_num_leafs(14), 16);
	TEST_EQUAL(merkle_num_leafs(15), 16);
	TEST_EQUAL(merkle_num_leafs(16), 16);
	TEST_EQUAL(merkle_num_leafs(17), 32);
	TEST_EQUAL(merkle_num_leafs(18), 32);

	// parents
	TEST_EQUAL(merkle_get_parent(1), 0);
	TEST_EQUAL(merkle_get_parent(2), 0);
	TEST_EQUAL(merkle_get_parent(3), 1);
	TEST_EQUAL(merkle_get_parent(4), 1);
	TEST_EQUAL(merkle_get_parent(5), 2);
	TEST_EQUAL(merkle_get_parent(6), 2);
	TEST_EQUAL(merkle_get_parent(7), 3);
	TEST_EQUAL(merkle_get_parent(8), 3);
	TEST_EQUAL(merkle_get_parent(9), 4);
	TEST_EQUAL(merkle_get_parent(10), 4);
	TEST_EQUAL(merkle_get_parent(11), 5);
	TEST_EQUAL(merkle_get_parent(12), 5);
	TEST_EQUAL(merkle_get_parent(13), 6);
	TEST_EQUAL(merkle_get_parent(14), 6);

	// siblings
	TEST_EQUAL(merkle_get_sibling(1), 2);
	TEST_EQUAL(merkle_get_sibling(2), 1);
	TEST_EQUAL(merkle_get_sibling(3), 4);
	TEST_EQUAL(merkle_get_sibling(4), 3);
	TEST_EQUAL(merkle_get_sibling(5), 6);
	TEST_EQUAL(merkle_get_sibling(6), 5);
	TEST_EQUAL(merkle_get_sibling(7), 8);
	TEST_EQUAL(merkle_get_sibling(8), 7);
	TEST_EQUAL(merkle_get_sibling(9), 10);
	TEST_EQUAL(merkle_get_sibling(10), 9);
	TEST_EQUAL(merkle_get_sibling(11), 12);
	TEST_EQUAL(merkle_get_sibling(12), 11);
	TEST_EQUAL(merkle_get_sibling(13), 14);
	TEST_EQUAL(merkle_get_sibling(14), 13);

	// total number of nodes given the number of leafs
	TEST_EQUAL(merkle_num_nodes(1), 1);
	TEST_EQUAL(merkle_num_nodes(2), 3);
	TEST_EQUAL(merkle_num_nodes(4), 7);
	TEST_EQUAL(merkle_num_nodes(8), 15);
	TEST_EQUAL(merkle_num_nodes(16), 31);

	// make_magnet_uri
	{
		entry info;
		info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
		info["name"] = "slightly shorter name, it's kind of sad that people started the trend of incorrectly encoding the regular name field and then adding another one with correct encoding";
		info["name.utf-8"] = "this is a long ass name in order to try to make make_magnet_uri overflow and hopefully crash. Although, by the time you read this that particular bug should have been fixed";
		info["piece length"] = 16 * 1024;
		info["length"] = 3245;
		entry torrent;
		torrent["info"] = info;
		entry::list_type& al1 = torrent["announce-list"].list();
		al1.push_back(entry::list_type());
		entry::list_type& al = al1.back().list();
		al.push_back(entry("http://bigtorrent.org:2710/announce"));
		al.push_back(entry("http://bt.careland.com.cn:6969/announce"));
		al.push_back(entry("http://bt.e-burg.org:2710/announce"));
		al.push_back(entry("http://bttrack.9you.com/announce"));
		al.push_back(entry("http://coppersurfer.tk:6969/announce"));
		al.push_back(entry("http://erdgeist.org/arts/software/opentracker/announce"));
		al.push_back(entry("http://exodus.desync.com/announce"));
		al.push_back(entry("http://fr33dom.h33t.com:3310/announce"));
		al.push_back(entry("http://genesis.1337x.org:1337/announce"));
		al.push_back(entry("http://inferno.demonoid.me:3390/announce"));
		al.push_back(entry("http://inferno.demonoid.ph:3390/announce"));
		al.push_back(entry("http://ipv6.tracker.harry.lu/announce"));
		al.push_back(entry("http://lnxroot.com:6969/announce"));
		al.push_back(entry("http://nemesis.1337x.org/announce"));
		al.push_back(entry("http://puto.me:6969/announce"));
		al.push_back(entry("http://sline.net:2710/announce"));
		al.push_back(entry("http://tracker.beeimg.com:6969/announce"));
		al.push_back(entry("http://tracker.ccc.de/announce"));
		al.push_back(entry("http://tracker.coppersurfer.tk/announce"));
		al.push_back(entry("http://tracker.coppersurfer.tk:6969/announce"));
		al.push_back(entry("http://tracker.cpleft.com:2710/announce"));
		al.push_back(entry("http://tracker.istole.it/announce"));
		al.push_back(entry("http://tracker.kamyu.net/announce"));
		al.push_back(entry("http://tracker.novalayer.org:6969/announce"));
		al.push_back(entry("http://tracker.torrent.to:2710/announce"));
		al.push_back(entry("http://tracker.torrentbay.to:6969/announce"));
		al.push_back(entry("udp://tracker.openbittorrent.com:80"));
		al.push_back(entry("udp://tracker.publicbt.com:80"));

		std::vector<char> buf;
		bencode(std::back_inserter(buf), torrent);
		printf("%s\n", &buf[0]);
		torrent_info ti(&buf[0], buf.size(), ec);

		TEST_EQUAL(al.size(), ti.trackers().size());

		std::string magnet = make_magnet_uri(ti);
		printf("%s len: %d\n", magnet.c_str(), int(magnet.size()));
	}
	return 0;
}

