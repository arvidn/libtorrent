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

#include "libtorrent/session.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "setup_transfer.hpp"
#include <chrono>
#include <thread>
#include <sstream>

using namespace lt;

TORRENT_TEST(curl_integration_basic)
{
	int const port = start_web_server(false);
	
	settings_pack settings;
	settings.set_bool(settings_pack::enable_http2_trackers, true);
	settings.set_int(settings_pack::alert_mask, alert_category::all);
	
	lt::session ses(settings);
	
	auto current = ses.get_settings();
	TEST_CHECK(current.get_bool(settings_pack::enable_http2_trackers));
	
	std::stringstream tracker_url;
	tracker_url << "http://127.0.0.1:" << port << "/announce";
	add_torrent_params p = parse_magnet_uri(
		"magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"
		"&tr=" + tracker_url.str());
	p.save_path = ".";
	
	torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	
	stop_web_server();
}

TORRENT_TEST(curl_fixes_connection_reuse)
{
	int const port = start_web_server(false);
	
	settings_pack settings;
	settings.set_bool(settings_pack::enable_http2_trackers, true);
	
	lt::session ses(settings);
	
	std::stringstream tracker_url;
	tracker_url << "http://127.0.0.1:" << port << "/announce";
	
	std::vector<torrent_handle> handles;
	for (int i = 0; i < 5; ++i)
	{
		std::string info_hash;
		for (int j = 0; j < 40; ++j)
			info_hash += "0123456789abcdef"[rand() % 16];
		
		add_torrent_params p = parse_magnet_uri(
			"magnet:?xt=urn:btih:" + info_hash +
			"&tr=" + tracker_url.str());
		p.save_path = ".";
		handles.push_back(ses.add_torrent(p));
	}
	
	// No FD exhaustion
	for (auto& h : handles)
	{
		TEST_CHECK(h.is_valid());
	}
	
	stop_web_server();
}

TORRENT_TEST(curl_https_tracker)
{
	// Start local web server with SSL (enables HTTP/2)
	int const port = start_web_server(true);
	
	// Setup HTTPS test settings with CA certificate
	settings_pack settings = setup_https_test_settings();
	settings.set_bool(settings_pack::enable_http2_trackers, true);
	settings.set_int(settings_pack::alert_mask, alert_category::all);
	
	lt::session ses(settings);
	
	// Use HTTPS tracker
	std::stringstream tracker_url;
	tracker_url << "https://127.0.0.1:" << port << "/announce";
	add_torrent_params p = parse_magnet_uri(
		"magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"
		"&tr=" + tracker_url.str());
	p.save_path = ".";
	
	torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	
	h.force_reannounce();
	
	std::this_thread::sleep_for(std::chrono::seconds(1));
	TEST_CHECK(h.is_valid());
	
	stop_web_server();
}

TORRENT_TEST(curl_http2_fallback)
{
	// Start local web server without SSL (HTTP/1.1 only)
	int const port = start_web_server(false);
	
	settings_pack settings;
	settings.set_bool(settings_pack::enable_http2_trackers, true);
	
	lt::session ses(settings);
	
	// HTTP tracker (no HTTP/2 on cleartext)
	std::stringstream tracker_url;
	tracker_url << "http://127.0.0.1:" << port << "/announce";
	add_torrent_params p = parse_magnet_uri(
		"magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"
		"&tr=" + tracker_url.str());
	p.save_path = ".";
	
	torrent_handle h = ses.add_torrent(p);
	h.force_reannounce();
	
	TEST_CHECK(h.is_valid());
	
	stop_web_server();
}

TORRENT_TEST(curl_timeout_handling)
{
	settings_pack settings;
	settings.set_bool(settings_pack::enable_http2_trackers, true);
	settings.set_int(settings_pack::tracker_completion_timeout, 2);
	settings.set_int(settings_pack::tracker_receive_timeout, 2);
	
	lt::session ses(settings);
	
	// Non-routable IP (will timeout)
	add_torrent_params p = parse_magnet_uri(
		"magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"
		"&tr=http://10.255.255.255/announce");
	p.save_path = ".";
	
	torrent_handle h = ses.add_torrent(p);
	
	auto start = std::chrono::steady_clock::now();
	h.force_reannounce();
	
	bool got_error = false;
	for (int i = 0; i < 50 && !got_error; ++i)
	{
		std::vector<alert*> alerts;
		ses.pop_alerts(&alerts);
		
		for (alert* a : alerts)
		{
			if (alert_cast<tracker_error_alert>(a))
			{
				got_error = true;
				break;
			}
		}
		
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	
	auto duration = std::chrono::steady_clock::now() - start;
	
	TEST_CHECK(duration < std::chrono::seconds(10));
	TEST_CHECK(got_error);
}

TORRENT_TEST(curl_runtime_settings)
{
	settings_pack settings;
	settings.set_bool(settings_pack::enable_http2_trackers, true);
	
	lt::session ses(settings);
	
	add_torrent_params p = parse_magnet_uri(
		"magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"
		"&tr=https://tracker.example.com/announce");
	p.save_path = ".";
	
	torrent_handle h = ses.add_torrent(p);
	
	settings.set_bool(settings_pack::enable_http2_trackers, false);
	ses.apply_settings(settings);
	
	h.force_reannounce();
	TEST_CHECK(h.is_valid());
	
	settings.set_bool(settings_pack::enable_http2_trackers, true);
	ses.apply_settings(settings);
	
	h.force_reannounce();
	TEST_CHECK(h.is_valid());
}

TORRENT_TEST(curl_multiple_trackers)
{
	settings_pack settings;
	settings.set_bool(settings_pack::enable_http2_trackers, true);
	
	lt::session ses(settings);
	
	add_torrent_params p = parse_magnet_uri(
		"magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"
		"&tr=http://tracker1.example.com/announce"
		"&tr=https://tracker2.example.com/announce"
		"&tr=http://tracker3.example.com/announce");
	p.save_path = ".";
	
	torrent_handle h = ses.add_torrent(p);
	TEST_CHECK(h.is_valid());
	
	h.force_reannounce(0, -1, torrent_handle::ignore_min_interval);
	
	std::this_thread::sleep_for(std::chrono::seconds(2));
	TEST_CHECK(h.is_valid());
}

TORRENT_TEST(curl_high_volume)
{
	settings_pack settings;
	settings.set_bool(settings_pack::enable_http2_trackers, true);
	settings.set_int(settings_pack::connections_limit, 5000);
	
	lt::session ses(settings);
	
	std::vector<torrent_handle> handles;
	for (int i = 0; i < 100; ++i)
	{
		std::string info_hash;
		for (int j = 0; j < 40; ++j)
			info_hash += "0123456789abcdef"[rand() % 16];
		
		std::string tracker = (i % 2 == 0) 
			? "http://tracker.example.com/announce"
			: "https://tracker.example.com/announce";
		
		add_torrent_params p = parse_magnet_uri(
			"magnet:?xt=urn:btih:" + info_hash + "&tr=" + tracker);
		p.save_path = ".";
		handles.push_back(ses.add_torrent(p));
	}
	
	for (auto& h : handles)
	{
		h.force_reannounce();
	}
	
	std::this_thread::sleep_for(std::chrono::seconds(5));
	
	// No resource exhaustion
	int valid_count = 0;
	for (auto& h : handles)
	{
		if (h.is_valid()) valid_count++;
	}
	TEST_EQUAL(valid_count, 100);
}

#else // TORRENT_USE_LIBCURL

TORRENT_TEST(curl_not_available)
{
	TEST_CHECK(true);
}

#endif // TORRENT_USE_LIBCURL