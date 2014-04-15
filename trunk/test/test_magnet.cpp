/*

Copyright (c) 2012, Arvid Norberg
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
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/bencode.hpp"

using namespace libtorrent;

int test_main()
{
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
	int ret = lazy_bdecode(&buf[0], &buf[0] + buf.size(), session_state2, ec);
	TEST_CHECK(ret == 0);

	fprintf(stderr, "session_state\n%s\n", print_entry(session_state2).c_str());

	// parse_magnet_uri
	parse_magnet_uri("magnet:?dn=foo&dht=127.0.0.1:43", p, ec);
	TEST_CHECK(ec == error_code(errors::missing_info_hash_in_uri));
	ec.clear();

	// parse_magnet_uri
	parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd&ws=http://foo.com/bar&ws=http://bar.com/foo", p, ec);
	TEST_CHECK(!ec);
	TEST_EQUAL(p.url_seeds.size(), 2);
	TEST_EQUAL(p.url_seeds[0], "http://foo.com/bar");
	TEST_EQUAL(p.url_seeds[1], "http://bar.com/foo");
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
		buf.push_back('\0');
		printf("%s\n", &buf[0]);
		error_code ec;
		torrent_info ti(&buf[0], buf.size(), ec);

		TEST_EQUAL(al.size(), ti.trackers().size());

		std::string magnet = make_magnet_uri(ti);
		printf("%s len: %d\n", magnet.c_str(), int(magnet.size()));
	}

	// make_magnet_uri
	{
		entry info;
		info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
		info["name"] = "test";
		info["name.utf-8"] = "test";
		info["piece length"] = 16 * 1024;
		info["length"] = 3245;
		entry torrent;
		torrent["info"] = info;

		torrent["url-list"] = "http://foo.com/bar";

		std::vector<char> buf;
		bencode(std::back_inserter(buf), torrent);
		buf.push_back('\0');
		printf("%s\n", &buf[0]);
		error_code ec;
		torrent_info ti(&buf[0], buf.size(), ec);

		std::string magnet = make_magnet_uri(ti);
		printf("%s len: %d\n", magnet.c_str(), int(magnet.size()));
		TEST_CHECK(magnet.find("&ws=http%3a%2f%2ffoo.com%2fbar") != std::string::npos);
	}

	return 0;
}

