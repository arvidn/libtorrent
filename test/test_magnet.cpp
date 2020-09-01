/*

Copyright (c) 2013-2020, Arvid Norberg
Copyright (c) 2016, 2018, Steven Siloti
Copyright (c) 2016, Pavel Pimenov
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2017, Jan Berkel
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent_info.hpp" // for announce_entry
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "settings.hpp"

using namespace lt;

#if TORRENT_ABI_VERSION == 1
namespace {
void test_remove_url(std::string url)
{
	lt::session s(settings());
	add_torrent_params p;
	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	p.url = url;
	p.save_path = ".";
	torrent_handle h = s.add_torrent(p);
	std::vector<torrent_handle> handles = s.get_torrents();
	TEST_EQUAL(handles.size(), 1);

	TEST_NOTHROW(s.remove_torrent(h));

	handles = s.get_torrents();
	TEST_EQUAL(handles.size(), 0);
}
} // anonymous namespace

TORRENT_TEST(remove_url)
{
	test_remove_url("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567");
}
#endif

TORRENT_TEST(magnet)
{
	session_proxy p1;
	session_proxy p2;

	// test session state load/restore
	settings_pack pack = settings();
	pack.set_str(settings_pack::user_agent, "test");
	pack.set_int(settings_pack::tracker_receive_timeout, 1234);
	pack.set_int(settings_pack::file_pool_size, 543);
	pack.set_int(settings_pack::urlseed_wait_retry, 74);
	pack.set_int(settings_pack::initial_picker_threshold, 351);
	pack.set_bool(settings_pack::close_redundant_connections, false);
	pack.set_int(settings_pack::auto_scrape_interval, 235);
	pack.set_int(settings_pack::auto_scrape_min_interval, 62);
	pack.set_int(settings_pack::dht_max_peers_reply, 70);
	auto s = std::make_unique<lt::session>(pack);

	TEST_EQUAL(pack.get_str(settings_pack::user_agent), "test");
	TEST_EQUAL(pack.get_int(settings_pack::tracker_receive_timeout), 1234);

	entry session_state = write_session_params(s->session_state());

	// test magnet link parsing
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&tr=http://1"
		"&tr=http://2"
		"&tr=http://3"
		"&tr=http://3"
		"&dn=foo"
		"&dht=127.0.0.1:43");

	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	p.save_path = ".";

	error_code ec;
	torrent_handle t = s->add_torrent(p, ec);
	TEST_CHECK(!ec);
	if (ec) std::printf("%s\n", ec.message().c_str());

	std::vector<announce_entry> trackers = t.trackers();
	TEST_EQUAL(trackers.size(), 3);
	std::set<std::string> trackers_set;
	for (std::vector<announce_entry>::iterator i = trackers.begin()
		, end(trackers.end()); i != end; ++i)
		trackers_set.insert(i->url);

	TEST_CHECK(trackers_set.count("http://1") == 1);
	TEST_CHECK(trackers_set.count("http://2") == 1);
	TEST_CHECK(trackers_set.count("http://3") == 1);

	p = parse_magnet_uri("magnet:"
		"?tr=http://1"
		"&tr=http://2"
		"&dn=foo"
		"&dht=127.0.0.1:43"
		"&xt=urn:ed2k:a0a9277894123b27945224fbac8366c9"
		"&xt=urn:btih:c352cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	p.save_path = ".";
	torrent_handle t2 = s->add_torrent(p, ec);
	TEST_CHECK(!ec);
	if (ec) std::printf("%s\n", ec.message().c_str());

	trackers = t2.trackers();
	TEST_EQUAL(trackers.size(), 2);
	TEST_EQUAL(trackers[0].tier, 0);
	TEST_EQUAL(trackers[1].tier, 1);

	p = parse_magnet_uri("magnet:"
		"?tr=udp%3A%2F%2Ftracker.openbittorrent.com%3A80"
		"&tr=udp%3A%2F%2Ftracker.publicbt.com%3A80"
		"&tr=udp%3A%2F%2Ftracker.ccc.de%3A80"
		"&xt=urn:btih:a38d02c287893842a32825aa866e00828a318f07"
		"&dn=Ubuntu+11.04+%28Final%29");
	p.flags &= ~torrent_flags::paused;
	p.flags &= ~torrent_flags::auto_managed;
	p.save_path = ".";
	torrent_handle t3 = s->add_torrent(p, ec);
	TEST_CHECK(!ec);
	if (ec) std::printf("%s\n", ec.message().c_str());

	trackers = t3.trackers();
	TEST_EQUAL(trackers.size(), 3);
	if (trackers.size() > 0)
	{
		TEST_EQUAL(trackers[0].url, "udp://tracker.openbittorrent.com:80");
		TEST_EQUAL(trackers[0].tier, 0);
		std::printf("1: %s\n", trackers[0].url.c_str());
	}
	if (trackers.size() > 1)
	{
		TEST_EQUAL(trackers[1].url, "udp://tracker.publicbt.com:80");
		TEST_EQUAL(trackers[1].tier, 1);
		std::printf("2: %s\n", trackers[1].url.c_str());
	}
	if (trackers.size() > 2)
	{
		TEST_EQUAL(trackers[2].url, "udp://tracker.ccc.de:80");
		TEST_EQUAL(trackers[2].tier, 2);
		std::printf("3: %s\n", trackers[2].url.c_str());
	}

	sha1_hash const ih = t.info_hashes().v1;
	TEST_EQUAL(aux::to_hex(ih), "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");

	p1 = s->abort();

	std::vector<char> buf;
	bencode(std::back_inserter(buf), session_state);
	bdecode_node session_state2;
	int ret = bdecode(&buf[0], &buf[0] + buf.size(), session_state2, ec);
	TEST_CHECK(ret == 0);

	std::printf("session_state\n%s\n", print_entry(session_state2).c_str());

	// make sure settings that haven't been changed from their defaults are not saved
	TEST_CHECK(!session_state2.dict_find("settings")
		.dict_find("optimistic_disk_retry"));

	s.reset(new lt::session(read_session_params(session_state2)));

#define CMP_SET(x) std::printf(#x ": %d %d\n"\
	, s->get_settings().get_int(settings_pack:: x)\
	, pack.get_int(settings_pack:: x)); \
	TEST_EQUAL(s->get_settings().get_int(settings_pack:: x), pack.get_int(settings_pack:: x))

	CMP_SET(tracker_receive_timeout);
	CMP_SET(file_pool_size);
	CMP_SET(urlseed_wait_retry);
	CMP_SET(initial_picker_threshold);
	CMP_SET(auto_scrape_interval);
	CMP_SET(auto_scrape_min_interval);
	p2 = s->abort();
}

TORRENT_TEST(parse_escaped_hash_parameter)
{
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn%3Abtih%3Acdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
	TEST_EQUAL(aux::to_hex(p.info_hashes.v1), "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
}

TORRENT_TEST(parse_escaped_hash_parameter_in_hex)
{
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdc%64");
	TEST_EQUAL(aux::to_hex(p.info_hashes.v1), "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
}

TORRENT_TEST(parse_invalid_escaped_hash_parameter)
{
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn%%3A", ec);
	TEST_EQUAL(ec, error_code(errors::invalid_escaped_string));
}

TORRENT_TEST(throwing_overload)
{
	TEST_THROW(parse_magnet_uri("magnet:?xt=urn%%3A"));
}

TORRENT_TEST(parse_missing_hash)
{
	// parse_magnet_uri
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?dn=foo&dht=127.0.0.1:43", ec);
	TEST_EQUAL(ec, error_code(errors::missing_info_hash_in_uri));
}

TORRENT_TEST(parse_base32_hash)
{
	// parse_magnet_uri
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:MFRGCYTBMJQWEYLCMFRGCYTBMJQWEYLC");
	TEST_EQUAL(p.info_hashes.v1, sha1_hash("abababababababababab"));
}

TORRENT_TEST(parse_web_seeds)
{
	// parse_magnet_uri
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&ws=http://foo.com/bar&ws=http://bar.com/foo");
	TEST_EQUAL(p.url_seeds.size(), 2);
	TEST_EQUAL(p.url_seeds[0], "http://foo.com/bar");
	TEST_EQUAL(p.url_seeds[1], "http://bar.com/foo");
}

TORRENT_TEST(parse_missing_hash2)
{
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?xt=blah&dn=foo&dht=127.0.0.1:43", ec);
	TEST_EQUAL(ec, error_code(errors::missing_info_hash_in_uri));
}

TORRENT_TEST(parse_short_hash)
{
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:abababab", ec);
	TEST_EQUAL(ec, error_code(errors::invalid_info_hash));
}

TORRENT_TEST(parse_long_hash)
{
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:ababababababababababab", ec);
	TEST_EQUAL(ec, error_code(errors::invalid_info_hash));
}

TORRENT_TEST(parse_space_hash)
{
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih: abababababababababab", ec);
	TEST_EQUAL(ec, error_code(errors::invalid_info_hash));
}

TORRENT_TEST(parse_v2_hash)
{
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btmh:1220cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
	TEST_EQUAL(aux::to_hex(p.info_hashes.v2), "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
	TEST_EQUAL(aux::to_hex(p.info_hashes.v1), "0000000000000000000000000000000000000000");
}

TORRENT_TEST(parse_v2_short_hash)
{
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btmh:1220cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdccdcdcdcdcdcdcd", ec);
	TEST_EQUAL(ec, error_code(errors::invalid_info_hash));
}

TORRENT_TEST(parse_v2_invalid_hash_prefix)
{
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btmh:1221cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd", ec);
	TEST_EQUAL(ec, error_code(errors::invalid_info_hash));
}

TORRENT_TEST(parse_hybrid_uri)
{
	add_torrent_params p = parse_magnet_uri("magnet:?"
		"xt=urn:btmh:1220cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
	TEST_EQUAL(aux::to_hex(p.info_hashes.v1), "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
	TEST_EQUAL(aux::to_hex(p.info_hashes.v2), "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
}

TORRENT_TEST(parse_peer)
{
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&x.pe=127.0.0.1:43&x.pe=<invalid1>&x.pe=<invalid2>:100&x.pe=[::1]:45");
	TEST_EQUAL(p.peers.size(), 2);
	TEST_EQUAL(p.peers[0], ep("127.0.0.1", 43));
	TEST_EQUAL(p.peers[1], ep("::1", 45));
}

#ifndef TORRENT_DISABLE_DHT
TORRENT_TEST(parse_dht_node)
{
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&dht=127.0.0.1:43&dht=10.0.0.1:1337");

	TEST_EQUAL(p.dht_nodes.size(), 2);
	TEST_EQUAL(p.dht_nodes[0].first, "127.0.0.1");
	TEST_EQUAL(p.dht_nodes[0].second, 43);

	TEST_EQUAL(p.dht_nodes[1].first, "10.0.0.1");
	TEST_EQUAL(p.dht_nodes[1].second, 1337);
}
#endif

TORRENT_TEST(make_magnet_uri)
{
	// make_magnet_uri
	entry info;
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["name"] = "slightly shorter name, it's kind of sad that people started "
		"the trend of incorrectly encoding the regular name field and then adding "
		"another one with correct encoding";
	info["name.utf-8"] = "this is a long ass name in order to try to make "
		"make_magnet_uri overflow and hopefully crash. Although, by the time "
		"you read this that particular bug should have been fixed";
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
	std::printf("%s\n", &buf[0]);
	error_code ec;
	torrent_info ti(buf, ec, from_span);

	TEST_EQUAL(al.size(), ti.trackers().size());

	std::string magnet = make_magnet_uri(ti);
	std::printf("%s len: %d\n", magnet.c_str(), int(magnet.size()));
}

TORRENT_TEST(make_magnet_uri2)
{
	// make_magnet_uri
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
	std::printf("%s\n", &buf[0]);
	error_code ec;
	torrent_info ti(buf, ec, from_span);

	std::string magnet = make_magnet_uri(ti);
	std::printf("%s len: %d\n", magnet.c_str(), int(magnet.size()));
	TEST_CHECK(magnet.find("&ws=http%3a%2f%2ffoo.com%2fbar") != std::string::npos);
}

TORRENT_TEST(make_magnet_uri_v2)
{
	auto ti = ::create_torrent(nullptr, "temporary", 16 * 1024, 13
		, true, lt::create_torrent::v2_only);

	std::string magnet = make_magnet_uri(*ti);
	std::printf("%s len: %d\n", magnet.c_str(), int(magnet.size()));
	TEST_CHECK(magnet.find("xt=urn:btmh:1220") != std::string::npos);
	TEST_CHECK(magnet.find("xt=urn:btih:") == std::string::npos);
}

TORRENT_TEST(make_magnet_uri_hybrid)
{
	auto ti = ::create_torrent(nullptr, "temporary", 16 * 1024, 13);

	std::string magnet = make_magnet_uri(*ti);
	std::printf("%s len: %d\n", magnet.c_str(), int(magnet.size()));
	TEST_CHECK(magnet.find("xt=urn:btih:") != std::string::npos);
	TEST_CHECK(magnet.find("xt=urn:btmh:1220") != std::string::npos);
}

TORRENT_TEST(make_magnet_uri_v1)
{
	auto ti = ::create_torrent(nullptr, "temporary", 16 * 1024, 13, true, lt::create_torrent::v1_only);

	std::string magnet = make_magnet_uri(*ti);
	std::printf("%s len: %d\n", magnet.c_str(), int(magnet.size()));
	TEST_CHECK(magnet.find("xt=urn:btih:") != std::string::npos);
	TEST_CHECK(magnet.find("xt=urn:btmh:1220") == std::string::npos);
}

TORRENT_TEST(trailing_whitespace)
{
	session ses(settings());
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:abaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n", ec);
	p.save_path = ".";
	// invalid hash
	TEST_CHECK(ec);
	TEST_THROW(ses.add_torrent(p));

	ec.clear();
	p = parse_magnet_uri("magnet:?xt=urn:btih:abaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
	p.save_path = ".";
	// now it's valid, because there's no trailing whitespace
	torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
}

// These tests don't work because we don't hand out an incomplete torrent_info
// object. To make them work we would either have to set the correct metadata in
// the test, or change the behavior to make `h.torrent_file()` return the
// internal torrent_info object unconditionally
/*
TORRENT_TEST(preserve_trackers)
{
	session ses(settings());
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:abaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa&tr=https://test.com/announce", ec);
	p.save_path = ".";
	torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	TEST_CHECK(h.torrent_file()->trackers().size() == 1);
	TEST_CHECK(h.torrent_file()->trackers().at(0).url == "https://test.com/announce");
}

TORRENT_TEST(preserve_web_seeds)
{
	session ses(settings());
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:abaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa&ws=https://test.com/test", ec);
	p.save_path = ".";
	torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	TEST_CHECK(h.torrent_file()->web_seeds().size() == 1);
	TEST_CHECK(h.torrent_file()->web_seeds().at(0).url == "https://test.com/test");
}

TORRENT_TEST(preserve_dht_nodes)
{
	session ses(settings());
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?xt=urn:btih:abaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa&dht=test:1234", ec);
	p.save_path = ".";
	torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	TEST_CHECK(h.torrent_file()->nodes().size() == 1);
	TEST_CHECK(h.torrent_file()->nodes().at(0).first == "test");
	TEST_CHECK(h.torrent_file()->nodes().at(0).second == 1234);
}
*/
TORRENT_TEST(invalid_tracker_escaping)
{
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?tr=udp%3A%2F%2Ftracker.openjnt.com%\xf7"
		"A80&tr=udp%3A%2F%2Ftracker.pub.ciltbcom%3A80&tr=udp%3A%2F%2Ftracker.ccc.de%3A80&xt=urn:btih:a38d02c287893842a39737aa866e00828aA80&xt=urn:buntu+11.04+%28Final%29"
		, ec);
	TEST_CHECK(ec);
}

TORRENT_TEST(invalid_web_seed_escaping)
{
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?ws=udp%3A%2F%2Ftracker.openjnt.com%\xf7" "A80", ec);
	TEST_CHECK(ec);
}

TORRENT_TEST(invalid_trackers)
{
	error_code ec;
	add_torrent_params p = parse_magnet_uri("magnet:?tr=", ec);
	TEST_CHECK(p.trackers.empty());
}


namespace {

auto const yes = default_priority;
auto const no = dont_download;

void test_select_only(string_view uri, std::vector<download_priority_t> expected)
{
	add_torrent_params p = parse_magnet_uri(uri);
	TEST_CHECK(p.file_priorities == expected);
}

} // anonymous namespace

TORRENT_TEST(parse_magnet_select_only)
{
	test_select_only("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&so=0,2,4,6-8"
		, {yes, no, yes, no, yes, no, yes, yes, yes});
}

TORRENT_TEST(parse_magnet_select_only_overlap_range)
{
	test_select_only("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&so=0,2-4,3-5&dht=10.0.0.1:1337"
		, {yes, no, yes, yes, yes, yes});
}

TORRENT_TEST(parse_magnet_select_only_multiple)
{
	test_select_only("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&so=2-4&dht=10.0.0.1:1337&so=1"
		, {no, yes, yes, yes, yes});
}

TORRENT_TEST(parse_magnet_select_only_inverted_range)
{
	test_select_only("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&so=7-4,100000000&dht=10.0.0.1:1337&so=10"
		, {no, no, no, no, no, no, no, no, no, no, yes});
}

TORRENT_TEST(parse_magnet_select_only_index_bounds)
{
	test_select_only("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&so=100000000&dht=10.0.0.1:1337&so=10"
		, {no, no, no, no, no, no, no, no, no, no, yes});
}

TORRENT_TEST(parse_magnet_select_only_invalid_range1)
{
	test_select_only("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&so=-4&so=1", {no, yes});
}

TORRENT_TEST(parse_magnet_select_only_invalid_range2)
{
	test_select_only("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&so=3-&so=1", {no, yes});
}

TORRENT_TEST(parse_magnet_select_only_invalid_index_character)
{
	test_select_only("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&so=a&so=1", {no, yes});
}

TORRENT_TEST(parse_magnet_select_only_invalid_index_value)
{
	test_select_only("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&so=100000000&so=1", {no, yes});
}

TORRENT_TEST(parse_magnet_select_only_invalid_no_value)
{
	test_select_only("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&so=&dht=10.0.0.1:1337&so=", {});
}

TORRENT_TEST(parse_magnet_select_only_invalid_no_values)
{
	test_select_only("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&so=&dht=10.0.0.1:1337&so=,,1", {no, yes});
}


TORRENT_TEST(parse_magnet_select_only_invalid_quotes)
{
	test_select_only("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&dn=foo&so=\"1,2\"", {});
}

TORRENT_TEST(magnet_tr_x_uri)
{
	add_torrent_params p = parse_magnet_uri("magnet:"
		"?tr.0=udp://1"
		"&tr.1=http://2"
		"&tr=http://3"
		"&xt=urn:btih:c352cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
	TEST_CHECK((p.trackers == std::vector<std::string>{
		"udp://1", "http://2", "http://3"}));

	TEST_CHECK((p.tracker_tiers == std::vector<int>{0, 1, 2 }));

	p = parse_magnet_uri("magnet:"
		"?tr.a=udp://1"
		"&tr.1=http://2"
		"&xt=urn:btih:c352cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
	TEST_CHECK((p.trackers == std::vector<std::string>{"http://2" }));
	TEST_CHECK((p.tracker_tiers == std::vector<int>{0}));
}
