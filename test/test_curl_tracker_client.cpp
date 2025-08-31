/*

Copyright (c) 2025, libtorrent project
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
#include "libtorrent/config.hpp"

#ifdef TORRENT_USE_LIBCURL

#include "libtorrent/aux_/curl_tracker_client.hpp"
#include "libtorrent/aux_/curl_thread_manager.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "setup_transfer.hpp"
#include <curl/curl.h>
#include <signal.h>
#include <future>
#include <vector>
#include <chrono>
#include <sstream>

using namespace libtorrent;
using namespace libtorrent::aux;
using namespace std::chrono_literals;

namespace {
	struct curl_initializer {
		curl_initializer() {
			signal(SIGPIPE, SIG_IGN);
			curl_global_init(CURL_GLOBAL_DEFAULT);
		}
	} g_curl_init;
}

TORRENT_TEST(curl_tracker_client_creation)
{
	int const port = start_web_server(false);
	
	io_context ios;
	settings_pack settings;
	session_settings sett(settings);
	auto curl_mgr = curl_thread_manager::create(ios, sett);
	
	std::stringstream tracker_url;
	tracker_url << "http://127.0.0.1:" << port << "/announce";
	auto client = std::make_unique<curl_tracker_client>(ios, tracker_url.str(), settings, curl_mgr);
	
	TEST_CHECK(client != nullptr);
	TEST_CHECK(client->can_reuse());
	
	stop_web_server();
}

TORRENT_TEST(curl_tracker_client_announce_url)
{
	int const port = start_web_server(false);
	
	io_context ios;
	settings_pack settings;
	session_settings sett(settings);
	auto curl_mgr = curl_thread_manager::create(ios, sett);
	
	std::stringstream tracker_url;
	tracker_url << "http://127.0.0.1:" << port << "/announce";
	auto client = std::make_unique<curl_tracker_client>(ios, tracker_url.str(), settings, curl_mgr);
	
	tracker_request req;
	req.info_hash = sha1_hash("01234567890123456789");
	req.pid = peer_id("ABCDEFGHIJKLMNOPQRST");
	req.uploaded = 1024;
	req.downloaded = 2048;
	req.left = 4096;
	req.corrupt = 0;
	req.redundant = 0;
	req.listen_port = 6881;
	req.event = event_t::started;
	req.key = 12345;
	req.num_want = 50;
	
	std::promise<bool> url_valid;
	auto future = url_valid.get_future();
	
	client->announce(req, [&url_valid](error_code const& /*ec*/, tracker_response const& /*resp*/) {
		url_valid.set_value(true);
	});
	
	ios.run_for(1s);
	
	TEST_CHECK(true);
	
	stop_web_server();
}

TORRENT_TEST(curl_tracker_client_scrape_url)
{
	int const port = start_web_server(false);
	
	io_context ios;
	settings_pack settings;
	
	std::stringstream tracker_url;
	tracker_url << "http://127.0.0.1:" << port << "/announce";
	session_settings sett(settings);
	auto curl_mgr = curl_thread_manager::create(ios, sett);
	auto client = std::make_unique<curl_tracker_client>(ios, tracker_url.str(), settings, curl_mgr);
	
	tracker_request req;
	req.info_hash = sha1_hash("01234567890123456789");
	
	std::promise<bool> scrape_called;
	auto future = scrape_called.get_future();
	
	client->scrape(req, [&scrape_called](error_code const& /*ec*/, tracker_response const& /*resp*/) {
		scrape_called.set_value(true);
	});
	
	ios.run_for(1s);
	
	TEST_CHECK(true);
	
	stop_web_server();
}

TORRENT_TEST(curl_tracker_client_parse_announce)
{
	entry announce_resp;
	announce_resp["interval"] = 1800;
	announce_resp["complete"] = 10;
	announce_resp["incomplete"] = 5;
	
	entry::list_type& peers_list = announce_resp["peers"].list();
	entry peer1;
	peer1["ip"] = "192.168.1.1";
	peer1["port"] = 6881;
	peer1["peer id"] = "ABCDEFGHIJKLMNOPQRST";
	peers_list.push_back(peer1);
	
	std::vector<char> buffer;
	bencode(std::back_inserter(buffer), announce_resp);
	
	error_code ec;
	bdecode_node node;
	bdecode(buffer.data(), buffer.data() + buffer.size(), node, ec);
	
	TEST_CHECK(!ec);
	TEST_EQUAL(node.dict_find_int_value("interval"), 1800);
	TEST_EQUAL(node.dict_find_int_value("complete"), 10);
	TEST_EQUAL(node.dict_find_int_value("incomplete"), 5);
}

TORRENT_TEST(curl_tracker_client_parse_error)
{
	entry error_resp;
	error_resp["failure reason"] = "Torrent not registered";
	
	std::vector<char> buffer;
	bencode(std::back_inserter(buffer), error_resp);
	
	error_code ec;
	bdecode_node node;
	bdecode(buffer.data(), buffer.data() + buffer.size(), node, ec);
	
	TEST_CHECK(!ec);
	TEST_CHECK(node.dict_find_string_value("failure reason") == "Torrent not registered");
}

TORRENT_TEST(curl_tracker_client_parse_scrape)
{
	entry scrape_resp;
	entry& files = scrape_resp["files"];
	
	std::string info_hash(20, '1');
	entry& file_info = files[info_hash];
	file_info["complete"] = 15;
	file_info["incomplete"] = 8;
	file_info["downloaded"] = 100;
	
	std::vector<char> buffer;
	bencode(std::back_inserter(buffer), scrape_resp);
	
	error_code ec;
	bdecode_node node;
	bdecode(buffer.data(), buffer.data() + buffer.size(), node, ec);
	
	TEST_CHECK(!ec);
	TEST_CHECK(node.dict_find("files"));
}

TORRENT_TEST(curl_tracker_client_connection_reuse)
{
	io_context ios;
	settings_pack settings;
	
	std::string tracker_url = "http://tracker.example.com/announce";
	session_settings sett(settings);
	auto curl_mgr = curl_thread_manager::create(ios, sett);
	auto client = std::make_unique<curl_tracker_client>(ios, tracker_url, settings, curl_mgr);
	
	TEST_CHECK(client->can_reuse());
	
	tracker_request req;
	req.info_hash = sha1_hash("01234567890123456789");
	
	int requests_made = 0;
	for (int i = 0; i < 3; ++i) {
		client->announce(req, [&requests_made](error_code const& /*ec*/, tracker_response const& /*resp*/) {
			requests_made++;
		});
	}
	
	TEST_CHECK(client->can_reuse());
	
	client->close();
}

TORRENT_TEST(curl_tracker_client_http2)
{
	io_context ios;
	settings_pack settings;
	// TODO: Add enable_http2_trackers setting
	// settings.set_bool(settings_pack::enable_http2_trackers, true);
	
	std::string tracker_url = "https://tracker.example.com/announce";
	session_settings sett(settings);
	auto curl_mgr = curl_thread_manager::create(ios, sett);
	auto client = std::make_unique<curl_tracker_client>(ios, tracker_url, settings, curl_mgr);
	
	TEST_CHECK(client != nullptr);
	TEST_CHECK(client->can_reuse());
}

TORRENT_TEST(curl_tracker_client_timeout)
{
	io_context ios;
	settings_pack settings;
	settings.set_int(settings_pack::tracker_completion_timeout, 1);
	settings.set_int(settings_pack::tracker_receive_timeout, 1);
	
	std::string tracker_url = "http://10.255.255.255/announce";
	session_settings sett(settings);
	auto curl_mgr = curl_thread_manager::create(ios, sett);
	auto client = std::make_unique<curl_tracker_client>(ios, tracker_url, settings, curl_mgr);
	
	tracker_request req;
	req.info_hash = sha1_hash("01234567890123456789");
	
	std::promise<error_code> promise;
	auto future = promise.get_future();
	
	auto start = std::chrono::steady_clock::now();
	
	client->announce(req, [&promise](error_code const& ec, tracker_response const& /*resp*/) {
		promise.set_value(ec);
	});
	
	ios.run_for(3s);
	
	if (future.wait_for(0s) == std::future_status::ready) {
		auto ec = future.get();
		auto duration = std::chrono::steady_clock::now() - start;
		
		TEST_CHECK(ec);
		TEST_CHECK(duration < 2s);
	}
}

TORRENT_TEST(curl_tracker_client_invalid_url)
{
	io_context ios;
	settings_pack settings;
	session_settings sett(settings);
	auto curl_mgr = curl_thread_manager::create(ios, sett);
	
	std::vector<std::string> invalid_urls = {
		"not-a-url",
		"http://",
		"ftp://tracker.com/announce", // Wrong protocol
		""
	};
	
	for (auto const& url : invalid_urls) {
		auto client = std::make_unique<curl_tracker_client>(ios, url, settings, curl_mgr);
		TEST_CHECK(client != nullptr);
		
		tracker_request req;
		req.info_hash = sha1_hash("01234567890123456789");
		
		client->announce(req, [](error_code const& ec, tracker_response const& /*resp*/) {
			TEST_CHECK(ec);
		});
	}
	
	ios.run_for(1s);
}

TORRENT_TEST(curl_tracker_client_ipv6_parsing_fuzzing)
{
	io_context ios;
	settings_pack settings;
	std::string tracker_url = "http://tracker.example.com/announce";
	session_settings sett(settings);
	auto curl_mgr = curl_thread_manager::create(ios, sett);
	auto client = std::make_unique<curl_tracker_client>(ios, tracker_url, settings, curl_mgr);
	
	{
		entry resp;
		resp["peers6"] = std::string("");
		
		std::vector<char> buffer;
		bencode(std::back_inserter(buffer), resp);
		
		error_code ec;
		tracker_response parsed = aux::parse_announce_response(
			span<char const>(buffer.data(), buffer.size()), ec);
		TEST_CHECK(parsed.peers.empty());
	}
	
	{
		entry resp;
		resp["peers6"] = std::string(17, 'x'); // invalid length
		
		std::vector<char> buffer;
		bencode(std::back_inserter(buffer), resp);
		
		error_code ec;
		tracker_response parsed = aux::parse_announce_response(
			span<char const>(buffer.data(), buffer.size()), ec);
		TEST_CHECK(parsed.peers.empty());
	}
	
	{
		entry resp;
		std::string peer_data(18, '\0');
		peer_data[0] = 0x20; peer_data[1] = 0x01; // IPv6 prefix
		peer_data[16] = 0x1A; peer_data[17] = static_cast<char>(0xE1); // Port 6881
		resp["peers6"] = peer_data;
		
		std::vector<char> buffer;
		bencode(std::back_inserter(buffer), resp);
		
		error_code ec;
		tracker_response parsed = aux::parse_announce_response(
			span<char const>(buffer.data(), buffer.size()), ec);
		TEST_CHECK(!ec);
		TEST_EQUAL(parsed.peers.size(), 1);
		if (!parsed.peers.empty()) {
			TEST_EQUAL(parsed.peers[0].port, 6881);
		}
	}
	
	{
		entry resp;
		std::string peer_data(18 * 3, '\0');
		for (int i = 0; i < 3; ++i) {
			int offset = i * 18;
			peer_data[offset] = 0x20;
			peer_data[offset + 1] = 0x01;
			peer_data[offset + 16] = 0x1A;
			peer_data[offset + 17] = 0xE1 + i;
		}
		resp["peers6"] = peer_data;
		
		std::vector<char> buffer;
		bencode(std::back_inserter(buffer), resp);
		
		error_code ec;
		tracker_response parsed = aux::parse_announce_response(
			span<char const>(buffer.data(), buffer.size()), ec);
		TEST_CHECK(!ec);
		TEST_EQUAL(parsed.peers.size(), 3);
	}
	
	{
		entry resp;
		std::string peer_data(18 * 1000, '\x01');
		resp["peers6"] = peer_data;
		
		std::vector<char> buffer;
		bencode(std::back_inserter(buffer), resp);
		
		error_code ec;
		tracker_response parsed = aux::parse_announce_response(
			span<char const>(buffer.data(), buffer.size()), ec);
		TEST_CHECK(!ec);
		TEST_EQUAL(parsed.peers.size(), 1000);
	}
	
	{
		entry resp;
		std::string ipv4_data(6 * 2, '\0'); // 2 IPv4 peers
		ipv4_data[4] = 0x1A; ipv4_data[5] = static_cast<char>(0xE1); // Port
		ipv4_data[10] = 0x1A; ipv4_data[11] = static_cast<char>(0xE2);
		resp["peers"] = ipv4_data;
		
		std::string ipv6_data(18 * 2, '\0');
		resp["peers6"] = ipv6_data;
		
		std::vector<char> buffer;
		bencode(std::back_inserter(buffer), resp);
		
		error_code ec;
		tracker_response parsed = aux::parse_announce_response(
			span<char const>(buffer.data(), buffer.size()), ec);
		TEST_CHECK(!ec);
		TEST_EQUAL(parsed.peers.size(), 4);
	}
}

#else // TORRENT_USE_LIBCURL

TORRENT_TEST(curl_tracker_client_not_available)
{
	TEST_CHECK(true);
}

#endif // TORRENT_USE_LIBCURL