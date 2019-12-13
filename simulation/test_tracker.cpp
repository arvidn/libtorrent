/*

Copyright (c) 2010, Arvid Norberg
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
#include "settings.hpp"
#include "setup_swarm.hpp"
#include "setup_transfer.hpp" // for addr()
#include "utils.hpp" // for print_alerts
#include "create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "simulator/http_server.hpp"
#include "simulator/utils.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/torrent_info.hpp"

#include <iostream>

using namespace lt;
using namespace sim;

using chrono::duration_cast;

// seconds
const int duration = 10000;

template <typename Tp1, typename Tp2>
bool eq(Tp1 const lhs, Tp2 const rhs)
{
	return std::abs(lt::duration_cast<seconds>(lhs - rhs).count()) <= 1;
}

template <class T>
std::shared_ptr<T> clone_ptr(std::shared_ptr<T> const& ptr)
{
	return std::make_shared<T>(*ptr);
}

void test_interval(int interval)
{
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	bool ran_to_completion = false;

	sim::asio::io_service web_server(sim, address_v4::from_string("2.2.2.2"));
	// listen on port 8080
	sim::http_server http(web_server, 8080);

	// the timestamps of all announces
	std::vector<lt::time_point> announces;

	http.register_handler("/announce"
		, [&announces,interval,&ran_to_completion](std::string /* method */
			, std::string /* req */
		, std::map<std::string, std::string>&)
	{
		// don't collect events once we're done. We're not interested in the
		// tracker stopped announce for instance
		if (!ran_to_completion)
			announces.push_back(lt::clock_type::now());

		char response[500];
		int const size = std::snprintf(response, sizeof(response), "d8:intervali%de5:peers0:e", interval);
		return sim::send_response(200, "OK", size) + response;
	});

	std::vector<lt::time_point> announce_alerts;

	lt::settings_pack default_settings = settings();
	// since the test tracker is only listening on IPv4 we need to configure the
	// client to do the same so that the number of tracker_announce_alerts matches
	// the number of announces seen by the tracker
	default_settings.set_str(settings_pack::listen_interfaces, "0.0.0.0:6881");
	lt::add_torrent_params default_add_torrent;

	setup_swarm(1, swarm_test::upload, sim, default_settings, default_add_torrent
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.trackers.push_back("http://2.2.2.2:8080/announce");
		}
		// on alert
		, [&](lt::alert const* a, lt::session&) {

			if (ran_to_completion) return;
			if (lt::alert_cast<lt::tracker_announce_alert>(a))
			{
				announce_alerts.push_back(a->timestamp());
			}
		}
		// terminate
		, [&](int const ticks, lt::session&) -> bool {
			if (ticks > duration + 1)
			{
				ran_to_completion = true;
				return true;
			}
			return false;
		});

	TEST_CHECK(ran_to_completion);
	TEST_EQUAL(announce_alerts.size(), announces.size());

	lt::time_point last_announce = announces[0];
	lt::time_point last_alert = announce_alerts[0];
	for (int i = 1; i < int(announces.size()); ++i)
	{
		// make sure the interval is within 1 second of what it's supposed to be
		// (this accounts for network latencies, and the second-granularity
		// timestamps)
		TEST_CHECK(eq(duration_cast<lt::seconds>(announces[i] - last_announce), lt::seconds(interval)));
		last_announce = announces[i];

		TEST_CHECK(eq(duration_cast<lt::milliseconds>(announce_alerts[i] - last_alert), lt::seconds(interval)));
		last_alert = announce_alerts[i];
	}
}

template <typename AddTorrent, typename OnAlert>
std::vector<std::string> test_event(swarm_test const type
	, AddTorrent add_torrent
	, OnAlert on_alert)
{
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_service web_server(sim, address_v4::from_string("2.2.2.2"));
	// listen on port 8080
	sim::http_server http(web_server, 8080);

	// the request strings of all announces
	std::vector<std::string> announces;

	const int interval = 500;

	http.register_handler("/announce"
	, [&](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		TEST_EQUAL(method, "GET");
		announces.push_back(req);

		char response[500];
		int const size = std::snprintf(response, sizeof(response), "d8:intervali%de5:peers0:e", interval);
		return sim::send_response(200, "OK", size) + response;
	});

	lt::settings_pack default_settings = settings();
	lt::add_torrent_params default_add_torrent;

	setup_swarm(2, type, sim, default_settings, default_add_torrent
		// add session
		, [](lt::settings_pack&) { }
		// add torrent
		, add_torrent
		// on alert
		, on_alert
		// terminate
		, [&](int const ticks, lt::session& ses) -> bool
		{
			return ticks > duration;
		});

	// this is some basic sanity checking of the announces that should always be
	// true.
	// the first announce should be event=started then no other announce should
	// have event=started.
	// only the last announce should have event=stopped.
	TEST_CHECK(announces.size() > 2);

	if (announces.size() <= 2) return {};
	TEST_CHECK(announces.front().find("&event=started") != std::string::npos);
	for (auto const& a : span<std::string>(announces).subspan(1))
		TEST_CHECK(a.find("&event=started") == std::string::npos);

	TEST_CHECK(announces.back().find("&event=stopped") != std::string::npos);
	for (auto const& a : span<std::string>(announces).first(announces.size() - 1))
		TEST_CHECK(a.find("&event=stopped") == std::string::npos);
	return announces;
}

TORRENT_TEST(event_completed_downloading)
{
	auto const announces = test_event(swarm_test::download
		, [](lt::add_torrent_params& params) {
			params.trackers.push_back("http://2.2.2.2:8080/announce");
		}
		, [&](lt::alert const*, lt::session&) {}
	);

	// make sure there's exactly one event=completed
	TEST_CHECK(std::count_if(announces.begin(), announces.end(), [](std::string const& s)
		{ return s.find("&event=completed") != std::string::npos; }) == 1);
}

TORRENT_TEST(event_completed_downloading_replace_trackers)
{
	auto const announces = test_event(swarm_test::download
		, [](lt::add_torrent_params& params) {}
		, [&](lt::alert const* a, lt::session&) {
			if (auto const* at = alert_cast<add_torrent_alert>(a))
				at->handle.replace_trackers({announce_entry{"http://2.2.2.2:8080/announce"}});
		}
	);

	// make sure there's exactly one event=completed
	TEST_CHECK(std::count_if(announces.begin(), announces.end(), [](std::string const& s)
		{ return s.find("&event=completed") != std::string::npos; }) == 1);
}

TORRENT_TEST(event_completed_seeding)
{
	auto const announces = test_event(swarm_test::upload_no_auto_stop
		, [](lt::add_torrent_params& params) {
			params.trackers.push_back("http://2.2.2.2:8080/announce");
		}
		, [&](lt::alert const*, lt::session&) {}
		);

	// make sure there are no event=completed, since we added the torrent as a
	// seed
	TEST_CHECK(std::count_if(announces.begin(), announces.end(), [](std::string const& s)
		{ return s.find("&event=completed") != std::string::npos; }) == 0);
}


TORRENT_TEST(event_completed_seeding_replace_trackers)
{
	auto const announces = test_event(swarm_test::upload_no_auto_stop
		, [](lt::add_torrent_params& params) {}
		, [&](lt::alert const* a, lt::session&) {
			if (auto const* at = alert_cast<add_torrent_alert>(a))
				at->handle.replace_trackers({announce_entry{"http://2.2.2.2:8080/announce"}});
		}
	);

	// make sure there are no event=completed, since we added the torrent as a
	// seed
	TEST_CHECK(std::count_if(announces.begin(), announces.end(), [](std::string const& s)
		{ return s.find("&event=completed") != std::string::npos; }) == 0);
}

TORRENT_TEST(announce_interval_440)
{
	test_interval(440);
}

TORRENT_TEST(announce_interval_1800)
{
	test_interval(1800);
}

TORRENT_TEST(announce_interval_1200)
{
	test_interval(3600);
}

struct sim_config : sim::default_config
{
	explicit sim_config(bool ipv6 = true) : ipv6(ipv6) {}

	chrono::high_resolution_clock::duration hostname_lookup(
		asio::ip::address const& requestor
		, std::string hostname
		, std::vector<asio::ip::address>& result
		, boost::system::error_code& ec) override
	{
		if (hostname == "tracker.com")
		{
			result.push_back(address_v4::from_string("123.0.0.2"));
			if (ipv6)
				result.push_back(address_v6::from_string("ff::dead:beef"));
			return duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		}

		return default_config::hostname_lookup(requestor, hostname, result, ec);
	}

	bool ipv6;
};

void on_alert_notify(lt::session* ses)
{
	ses->get_io_service().post([ses] {
		std::vector<lt::alert*> alerts;
		ses->pop_alerts(&alerts);

		for (lt::alert* a : alerts)
		{
			lt::time_duration d = a->timestamp().time_since_epoch();
			std::uint32_t const millis = std::uint32_t(
				lt::duration_cast<lt::milliseconds>(d).count());
			std::printf("%4d.%03d: %s\n", millis / 1000, millis % 1000,
				a->message().c_str());
		}
	});
}

static const int num_interfaces = 3;

void test_ipv6_support(char const* listen_interfaces
	, int const expect_v4, int const expect_v6)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_service web_server_v4(sim, address_v4::from_string("123.0.0.2"));
	sim::asio::io_service web_server_v6(sim, address_v6::from_string("ff::dead:beef"));

	// listen on port 8080
	sim::http_server http_v4(web_server_v4, 8080);
	sim::http_server http_v6(web_server_v6, 8080);

	int v4_announces = 0;
	int v6_announces = 0;

	// if we're not listening we'll just report port 0
	std::string const expect_port = (listen_interfaces && listen_interfaces == ""_sv)
		? "&port=0" : "&port=6881";

	http_v4.register_handler("/announce"
	, [&v4_announces,expect_port](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		++v4_announces;
		TEST_EQUAL(method, "GET");
		TEST_CHECK(req.find(expect_port) != std::string::npos);
		char response[500];
		int const size = std::snprintf(response, sizeof(response), "d8:intervali1800e5:peers0:e");
		return sim::send_response(200, "OK", size) + response;
	});

	http_v6.register_handler("/announce"
	, [&v6_announces,expect_port](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		++v6_announces;
		TEST_EQUAL(method, "GET");

		TEST_CHECK(req.find(expect_port) != std::string::npos);
		char response[500];
		int const size = std::snprintf(response, sizeof(response), "d8:intervali1800e5:peers0:e");
		return sim::send_response(200, "OK", size) + response;
	});

	{
		lt::session_proxy zombie;

		std::vector<asio::ip::address> ips;

		for (int i = 0; i < num_interfaces; i++)
		{
			char ep[30];
			std::snprintf(ep, sizeof(ep), "123.0.0.%d", i + 1);
			ips.push_back(address::from_string(ep));
			std::snprintf(ep, sizeof(ep), "ffff::1337:%d", i + 1);
			ips.push_back(address::from_string(ep));
		}

		asio::io_service ios(sim, ips);
		lt::settings_pack sett = settings();
		if (listen_interfaces)
		{
			sett.set_str(settings_pack::listen_interfaces, listen_interfaces);
		}
		std::unique_ptr<lt::session> ses(new lt::session(sett, ios));

		ses->set_alert_notify(std::bind(&on_alert_notify, ses.get()));

		lt::add_torrent_params p;
		p.name = "test-torrent";
		p.save_path = ".";
		p.info_hash.assign("abababababababababab");

//TODO: parameterize http vs. udp here
		p.trackers.push_back("http://tracker.com:8080/announce");
		ses->async_add_torrent(p);

		// stop the torrent 5 seconds in
		sim::timer t1(sim, lt::seconds(5)
			, [&ses](boost::system::error_code const&)
		{
			std::vector<lt::torrent_handle> torrents = ses->get_torrents();
			for (auto const& t : torrents)
			{
				t.pause();
			}
		});

		// then shut down 10 seconds in
		sim::timer t2(sim, lt::seconds(10)
			, [&ses,&zombie](boost::system::error_code const&)
		{
			zombie = ses->abort();
			ses.reset();
		});

		sim.run();
	}

	TEST_EQUAL(v4_announces, expect_v4);
	TEST_EQUAL(v6_announces, expect_v6);
}

void test_udpv6_support(char const* listen_interfaces
	, int const expect_v4, int const expect_v6)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_service web_server_v4(sim, address_v4::from_string("123.0.0.2"));
	sim::asio::io_service web_server_v6(sim, address_v6::from_string("ff::dead:beef"));

	int v4_announces = 0;
	int v6_announces = 0;

	{
		lt::session_proxy zombie;

		std::vector<asio::ip::address> ips;

		for (int i = 0; i < num_interfaces; i++)
		{
			char ep[30];
			std::snprintf(ep, sizeof(ep), "123.0.0.%d", i + 1);
			ips.push_back(address::from_string(ep));
			std::snprintf(ep, sizeof(ep), "ffff::1337:%d", i + 1);
			ips.push_back(address::from_string(ep));
		}

		asio::io_service ios(sim, ips);
		lt::settings_pack sett = settings();
		if (listen_interfaces)
		{
			sett.set_str(settings_pack::listen_interfaces, listen_interfaces);
		}
		std::unique_ptr<lt::session> ses(new lt::session(sett, ios));

		// since we don't have a udp tracker to run in the sim, looking for the
		// alerts is the closest proxy
		ses->set_alert_notify([&]{
			ses->get_io_service().post([&] {
				std::vector<lt::alert*> alerts;
				ses->pop_alerts(&alerts);

				for (lt::alert* a : alerts)
				{
					lt::time_duration d = a->timestamp().time_since_epoch();
					std::uint32_t const millis = std::uint32_t(
						lt::duration_cast<lt::milliseconds>(d).count());
					std::printf("%4d.%03d: %s\n", millis / 1000, millis % 1000,
						a->message().c_str());
					if (auto tr = alert_cast<tracker_announce_alert>(a))
					{
						if (is_v4(tr->local_endpoint))
							++v4_announces;
						else
							++v6_announces;
					}
					else if (alert_cast<tracker_error_alert>(a))
					{
						TEST_ERROR("unexpected tracker error");
					}
				}
			});
		});

		lt::add_torrent_params p;
		p.name = "test-torrent";
		p.save_path = ".";
		p.info_hash.assign("abababababababababab");

		p.trackers.push_back("udp://tracker.com:8080/announce");
		ses->async_add_torrent(p);

		// stop the torrent 5 seconds in
		sim::timer t1(sim, lt::seconds(5)
			, [&ses](boost::system::error_code const&)
		{
			std::vector<lt::torrent_handle> torrents = ses->get_torrents();
			for (auto const& t : torrents)
			{
				t.pause();
			}
		});

		// then shut down 10 seconds in
		sim::timer t2(sim, lt::seconds(10)
			, [&ses,&zombie](boost::system::error_code const&)
		{
			zombie = ses->abort();
			ses.reset();
		});

		sim.run();
	}

	TEST_EQUAL(v4_announces, expect_v4);
	TEST_EQUAL(v6_announces, expect_v6);
}

// this test makes sure that a tracker whose host name resolves to both IPv6 and
// IPv4 addresses will be announced to twice, once for each address family
TORRENT_TEST(ipv6_support)
{
	// null means default
	test_ipv6_support(nullptr, 2, num_interfaces * 2);
}

TORRENT_TEST(announce_no_listen)
{
	// if we don't listen on any sockets at all (but only make outgoing peer
	// connections) we still need to make sure we announce to trackers
	test_ipv6_support("", 2, 2);
}

TORRENT_TEST(announce_udp_no_listen)
{
	// since there's no actual udp tracker in this test, we will only try to
	// announce once, and fail. We won't announce the event=stopped
	test_udpv6_support("", 1, 1);
}

TORRENT_TEST(ipv6_support_bind_v4_v6_any)
{
	// 2 because there's one announce on startup and one when shutting down
	// IPv6 will send announces for each interface
	test_ipv6_support("0.0.0.0:6881,[::0]:6881", 2, num_interfaces * 2);
}

TORRENT_TEST(ipv6_support_bind_v6_any)
{
	test_ipv6_support("[::0]:6881", 0, num_interfaces * 2);
}

TORRENT_TEST(ipv6_support_bind_v4)
{
	test_ipv6_support("123.0.0.3:6881", 2, 0);
}

TORRENT_TEST(ipv6_support_bind_v6)
{
	test_ipv6_support("[ffff::1337:1]:6881", 0, 2);
}

TORRENT_TEST(ipv6_support_bind_v6_3interfaces)
{
	test_ipv6_support("[ffff::1337:1]:6881,[ffff::1337:2]:6881,[ffff::1337:3]:6881", 0, 3 * 2);
}

TORRENT_TEST(ipv6_support_bind_v4_v6)
{
	test_ipv6_support("123.0.0.3:6881,[ffff::1337:1]:6881", 2, 2);
}

TORRENT_TEST(ipv6_support_bind_v6_v4)
{
	test_ipv6_support("[ffff::1337:1]:6881,123.0.0.3:6881", 2, 2);
}

// this runs a simulation of a torrent with tracker(s), making sure the request
// received by the tracker matches the expectation.
// The Setup function is run first, giving the test an opportunity to add
// trackers to the torrent. It's expected to return the number of seconds to
// wait until test2 is called.
// The Announce function is called on http requests. Test1 is run on the session
// 5 seconds after startup. The tracker is running at 123.0.0.2 (or tracker.com)
// port 8080.
template <typename Setup, typename Announce, typename Test1, typename Test2>
void tracker_test(Setup setup, Announce a, Test1 test1, Test2 test2
	, char const* url_path = "/announce")
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_service tracker_ios(sim, address_v4::from_string("123.0.0.2"));
	sim::asio::io_service tracker_ios6(sim, address_v6::from_string("ff::dead:beef"));

	// listen on port 8080
	sim::http_server http(tracker_ios, 8080);
	sim::http_server http6(tracker_ios6, 8080);

	http.register_handler(url_path, a);
	http6.register_handler(url_path, a);

	lt::session_proxy zombie;

	asio::io_service ios(sim, { address_v4::from_string("123.0.0.3")
		, address_v6::from_string("ffff::1337") });
	lt::settings_pack sett = settings();
	std::unique_ptr<lt::session> ses(new lt::session(sett, ios));

	ses->set_alert_notify(std::bind(&on_alert_notify, ses.get()));

	lt::add_torrent_params p;
	p.name = "test-torrent";
	p.save_path = ".";
	p.info_hash.assign("abababababababababab");
	int const delay = setup(p, *ses);
	ses->async_add_torrent(p);

	// run the test 5 seconds in
	sim::timer t1(sim, lt::seconds(5)
		, [&ses,&test1](boost::system::error_code const&)
	{
		std::vector<lt::torrent_handle> torrents = ses->get_torrents();
		TEST_EQUAL(torrents.size(), 1);
		torrent_handle h = torrents.front();
		test1(h);
	});

	sim::timer t2(sim, lt::seconds(5 + delay)
		, [&ses,&test2](boost::system::error_code const&)
	{
		std::vector<lt::torrent_handle> torrents = ses->get_torrents();
		TEST_EQUAL(torrents.size(), 1);
		torrent_handle h = torrents.front();
		test2(h);
	});

	// then shut down 10 seconds in
	sim::timer t3(sim, lt::seconds(10 + delay)
		, [&ses,&zombie](boost::system::error_code const&)
	{
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

template <typename Announce, typename Test1, typename Test2>
void tracker_test(Announce a, Test1 test1, Test2 test2, char const* url_path = "/announce")
{
	tracker_test([](lt::add_torrent_params& p, lt::session&) {
		p.trackers.push_back("http://tracker.com:8080/announce");
		return 5;
	},
	a, test1, test2, url_path);
}

template <typename Announce, typename Test>
void announce_entry_test(Announce a, Test t, char const* url_path = "/announce")
{
	tracker_test(a
		, [&t] (torrent_handle h) {
			std::vector<announce_entry> tr = h.trackers();

			TEST_EQUAL(tr.size(), 1);
			announce_entry const& ae = tr[0];
			t(ae);
		}
		, [](torrent_handle){}
		, url_path);
}

TORRENT_TEST(test_error)
{
	announce_entry_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>&)
		{
			TEST_EQUAL(method, "GET");

			char response[500];
			int const size = std::snprintf(response, sizeof(response), "d14:failure reason4:teste");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](announce_entry const& ae)
		{
			TEST_EQUAL(ae.url, "http://tracker.com:8080/announce");
			TEST_EQUAL(ae.endpoints.size(), 2);
			for (auto const& aep : ae.endpoints)
			{
				TEST_EQUAL(aep.is_working(), false);
				TEST_EQUAL(aep.message, "test");
				TEST_EQUAL(aep.last_error, error_code(errors::tracker_failure));
				TEST_EQUAL(aep.fails, 1);
			}
		});
}

TORRENT_TEST(test_warning)
{
	announce_entry_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>&)
		{
			TEST_EQUAL(method, "GET");

			char response[500];
			int const size = std::snprintf(response, sizeof(response), "d5:peers6:aaaaaa15:warning message5:test2e");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](announce_entry const& ae)
		{
			TEST_EQUAL(ae.url, "http://tracker.com:8080/announce");
			TEST_EQUAL(ae.endpoints.size(), 2);
			for (auto const& aep : ae.endpoints)
			{
				TEST_EQUAL(aep.is_working(), true);
				TEST_EQUAL(aep.message, "test2");
				TEST_EQUAL(aep.last_error, error_code());
				TEST_EQUAL(aep.fails, 0);
			}
		});
}

TORRENT_TEST(test_scrape_data_in_announce)
{
	announce_entry_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>&)
		{
			TEST_EQUAL(method, "GET");

			char response[500];
			int const size = std::snprintf(response, sizeof(response),
				"d5:peers6:aaaaaa8:completei1e10:incompletei2e10:downloadedi3e11:downloadersi4ee");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](announce_entry const& ae)
		{
			TEST_EQUAL(ae.url, "http://tracker.com:8080/announce");
			TEST_EQUAL(ae.endpoints.size(), 2);
			for (auto const& aep : ae.endpoints)
			{
				TEST_EQUAL(aep.is_working(), true);
				TEST_EQUAL(aep.message, "");
				TEST_EQUAL(aep.last_error, error_code());
				TEST_EQUAL(aep.fails, 0);
				TEST_EQUAL(aep.scrape_complete, 1);
				TEST_EQUAL(aep.scrape_incomplete, 2);
				TEST_EQUAL(aep.scrape_downloaded, 3);
			}
		});
}

TORRENT_TEST(test_scrape)
{
	tracker_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>&)
		{
			TEST_EQUAL(method, "GET");

			char response[500];
			int const size = std::snprintf(response, sizeof(response),
				"d5:filesd20:ababababababababababd8:completei1e10:downloadedi3e10:incompletei2eeee");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](torrent_handle h)
		{
			h.scrape_tracker();
		}
		, [](torrent_handle h)
		{
			std::vector<announce_entry> tr = h.trackers();

			TEST_EQUAL(tr.size(), 1);
			announce_entry const& ae = tr[0];
			TEST_EQUAL(ae.endpoints.size(), 2);
			for (auto const& aep : ae.endpoints)
			{
				TEST_EQUAL(aep.scrape_incomplete, 2);
				TEST_EQUAL(aep.scrape_complete, 1);
				TEST_EQUAL(aep.scrape_downloaded, 3);
			}
		}
		, "/scrape");
}

TORRENT_TEST(test_http_status)
{
	announce_entry_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>&)
		{
			TEST_EQUAL(method, "GET");
			return sim::send_response(410, "Not A Tracker", 0);
		}
		, [](announce_entry const& ae)
		{
			TEST_EQUAL(ae.url, "http://tracker.com:8080/announce");
			TEST_EQUAL(ae.endpoints.size(), 2);
			for (auto const& aep : ae.endpoints)
			{
				TEST_EQUAL(aep.is_working(), false);
				TEST_EQUAL(aep.message, "Not A Tracker");
				TEST_EQUAL(aep.last_error, error_code(410, http_category()));
				TEST_EQUAL(aep.fails, 1);
			}
		});
}

TORRENT_TEST(test_interval)
{
	announce_entry_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>&)
		{
			TEST_EQUAL(method, "GET");
			char response[500];
			int const size = std::snprintf(response, sizeof(response)
				, "d10:tracker id8:testteste");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](announce_entry const& ae)
		{
			TEST_EQUAL(ae.url, "http://tracker.com:8080/announce");
			TEST_EQUAL(ae.endpoints.size(), 2);
			for (auto const& aep : ae.endpoints)
			{
				TEST_EQUAL(aep.is_working(), true);
				TEST_EQUAL(aep.message, "");
				TEST_EQUAL(aep.last_error, error_code());
				TEST_EQUAL(aep.fails, 0);
			}

			TEST_EQUAL(ae.trackerid, "testtest");
		});
}

TORRENT_TEST(test_invalid_bencoding)
{
	announce_entry_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>&)
		{
			TEST_EQUAL(method, "GET");
			char response[500];
			int const size = std::snprintf(response, sizeof(response)
				, "d10:tracer idteste");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](announce_entry const& ae)
		{
			TEST_EQUAL(ae.url, "http://tracker.com:8080/announce");
			TEST_EQUAL(ae.endpoints.size(), 2);
			for (auto const& aep : ae.endpoints)
			{
				TEST_EQUAL(aep.is_working(), false);
				TEST_EQUAL(aep.message, "");
				TEST_EQUAL(aep.last_error, error_code(bdecode_errors::expected_value
					, bdecode_category()));
				TEST_EQUAL(aep.fails, 1);
			}
		});
}

TORRENT_TEST(try_next)
{
// test that we move on to try the next tier if the first one fails

	bool got_announce = false;
	tracker_test(
		[](lt::add_torrent_params& p, lt::session&)
		{
		// TODO: 3 use tracker_tiers here to put the trackers in different tiers
			p.trackers.push_back("udp://failing-tracker.com/announce");
			p.trackers.push_back("http://failing-tracker.com/announce");

			// this is the working tracker
			p.trackers.push_back("http://tracker.com:8080/announce");
			return 60;
		},
		[&](std::string method, std::string req
			, std::map<std::string, std::string>&)
		{
			got_announce = true;
			TEST_EQUAL(method, "GET");

			char response[500];
			// respond with an empty peer list
			int const size = std::snprintf(response, sizeof(response), "d5:peers0:e");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](torrent_handle h) {}
		, [](torrent_handle h)
		{
			torrent_status st = h.status();
			TEST_EQUAL(st.current_tracker, "http://tracker.com:8080/announce");

			std::vector<announce_entry> tr = h.trackers();

			TEST_EQUAL(tr.size(), 3);

			for (int i = 0; i < int(tr.size()); ++i)
			{
				std::printf("tracker \"%s\"\n", tr[i].url.c_str());
				if (tr[i].url == "http://tracker.com:8080/announce")
				{
					for (auto const& aep : tr[i].endpoints)
					{
						TEST_EQUAL(aep.fails, 0);
					}
					TEST_EQUAL(tr[i].verified, true);
				}
				else if (tr[i].url == "http://failing-tracker.com/announce")
				{
					for (auto const& aep : tr[i].endpoints)
					{
						TEST_CHECK(aep.fails >= 1);
						TEST_EQUAL(aep.last_error
							, error_code(boost::asio::error::host_not_found));
					}
					TEST_EQUAL(tr[i].verified, false);
				}
				else if (tr[i].url == "udp://failing-tracker.com/announce")
				{
					TEST_EQUAL(tr[i].verified, false);
					for (auto const& aep : tr[i].endpoints)
					{
						TEST_CHECK(aep.fails >= 1);
						TEST_EQUAL(aep.last_error
							, error_code(boost::asio::error::host_not_found));
					}
				}
				else
				{
					TEST_ERROR(("unexpected tracker URL: " + tr[i].url).c_str());
				}
			}
		});
	TEST_EQUAL(got_announce, true);
}

TORRENT_TEST(clear_error)
{
	// make sure we clear the error from a previous attempt when succeeding
	// a tracker announce

	int num_announces = 0;
	std::string last_message;
	tracker_test(
		[](lt::add_torrent_params& p, lt::session& ses)
		{
			settings_pack pack;
			// make sure we just listen on a single listen interface
			pack.set_str(settings_pack::listen_interfaces, "123.0.0.3:0");
			pack.set_int(settings_pack::min_announce_interval, 1);
			pack.set_int(settings_pack::tracker_backoff, 1);
			ses.apply_settings(pack);
			p.trackers.push_back("http://tracker.com:8080/announce");
			return 60;
		},
		[&](std::string method, std::string req, std::map<std::string, std::string>&)
		{
			// don't count the stopped event when shutting down
			if (req.find("&event=stopped&") != std::string::npos)
			{
				return sim::send_response(200, "OK", 2) + "de";
			}
			if (num_announces++ == 0)
			{
				// the first announce fails
				return std::string{};
			}

			// the second announce succeeds, with an empty peer list
			char response[500];
			int const size = std::snprintf(response, sizeof(response), "d8:intervali1800e5:peers0:e");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](torrent_handle h) {

		}
		, [&](torrent_handle h)
		{
			std::vector<announce_entry> const tr = h.trackers();
			TEST_EQUAL(tr.size(), 1);

			std::printf("tracker \"%s\"\n", tr[0].url.c_str());
			TEST_EQUAL(tr[0].url, "http://tracker.com:8080/announce");
			TEST_EQUAL(tr[0].endpoints.size(), 1);
			auto const& aep = tr[0].endpoints[0];
			std::printf("message: \"%s\" error: \"%s\"\n"
				, aep.message.c_str(), aep.last_error.message().c_str());
			TEST_EQUAL(aep.fails, 0);
			TEST_CHECK(!aep.last_error);
			TEST_EQUAL(aep.message, "");
			last_message = aep.message;
		});
	TEST_EQUAL(num_announces, 2);
	TEST_EQUAL(last_message, "");
}

std::shared_ptr<torrent_info> make_torrent(bool priv)
{
	file_storage fs;
	fs.add_file("foobar", 13241);
	lt::create_torrent ct(fs);

	ct.add_tracker("http://tracker.com:8080/announce");

	for (piece_index_t i(0); i < piece_index_t(ct.num_pieces()); ++i)
		ct.set_hash(i, sha1_hash(nullptr));

	ct.set_priv(priv);

	entry e = ct.generate();
	std::vector<char> buf;
	bencode(std::back_inserter(buf), e);
	return std::make_shared<torrent_info>(buf, from_span);
}

// make sure we _do_ send our IPv6 address to trackers for private torrents
TORRENT_TEST(tracker_ipv6_argument)
{
	bool got_announce = false;
	bool got_ipv6 = false;
	bool got_ipv4 = false;
	tracker_test(
		[](lt::add_torrent_params& p, lt::session& ses)
		{
			settings_pack pack;
			pack.set_bool(settings_pack::anonymous_mode, false);
			pack.set_str(settings_pack::listen_interfaces, "123.0.0.3:0,[ffff::1337]:0");
			ses.apply_settings(pack);
			p.ti = make_torrent(true);
			return 60;
		},
		[&](std::string method, std::string req
			, std::map<std::string, std::string>&)
		{
			got_announce = true;
			bool const stop_event = req.find("&event=stopped") != std::string::npos;
			// stop events don't need to advertise the IPv6/IPv4 address
			{
				std::string::size_type const pos = req.find("&ipv6=");
				TEST_CHECK(pos != std::string::npos || stop_event);
				got_ipv6 |= pos != std::string::npos;
				// make sure the IPv6 argument is url encoded
				TEST_EQUAL(req.substr(pos + 6, req.substr(pos + 6).find_first_of('&'))
					, "ffff%3a%3a1337");
			}

			{
				std::string::size_type const pos = req.find("&ipv4=");
				TEST_CHECK(pos != std::string::npos || stop_event);
				got_ipv4 |= pos != std::string::npos;
				TEST_EQUAL(req.substr(pos + 6, req.substr(pos + 6).find_first_of('&')), "123.0.0.3");
			}
			return sim::send_response(200, "OK", 11) + "d5:peers0:e";
		}
		, [](torrent_handle) {}
		, [](torrent_handle) {});
	TEST_EQUAL(got_announce, true);
	TEST_EQUAL(got_ipv6, true);
}

TORRENT_TEST(tracker_key_argument)
{
	std::set<std::string> keys;
	tracker_test(
		[](lt::add_torrent_params& p, lt::session&)
		{
			p.ti = make_torrent(true);
			return 60;
		},
		[&](std::string, std::string req
			, std::map<std::string, std::string>&)
		{
			auto const pos = req.find("&key=");
			TEST_CHECK(pos != std::string::npos);
			keys.insert(req.substr(pos + 5, req.find_first_of('&', pos + 5) - pos - 5));
			return sim::send_response(200, "OK", 11) + "d5:peers0:e";
		}
		, [](torrent_handle h) {}
		, [](torrent_handle h) {});

	// make sure we got two separate keys, one for each listen socket interface
	TEST_EQUAL(keys.size(), 2);
}

// make sure we do _not_ send our IPv6 address to trackers for non-private
// torrents
TORRENT_TEST(tracker_ipv6_argument_non_private)
{
	bool got_announce = false;
	bool got_ipv6 = false;
	tracker_test(
		[](lt::add_torrent_params& p, lt::session& ses)
		{
			settings_pack pack;
			pack.set_bool(settings_pack::anonymous_mode, false);
			ses.apply_settings(pack);
			p.ti = make_torrent(false);
			return 60;
		},
		[&](std::string method, std::string req
			, std::map<std::string, std::string>&)
		{
			got_announce = true;
			std::string::size_type pos = req.find("&ipv6=");
			TEST_CHECK(pos == std::string::npos);
			got_ipv6 |= pos != std::string::npos;
			return sim::send_response(200, "OK", 11) + "d5:peers0:e";
		}
		, [](torrent_handle) {}
		, [](torrent_handle) {});
	TEST_EQUAL(got_announce, true);
	TEST_EQUAL(got_ipv6, false);
}

TORRENT_TEST(tracker_ipv6_argument_privacy_mode)
{
	bool got_announce = false;
	bool got_ipv6 = false;
	tracker_test(
		[](lt::add_torrent_params& p, lt::session& ses)
		{
			settings_pack pack;
			pack.set_bool(settings_pack::anonymous_mode, true);
			ses.apply_settings(pack);
			p.ti = make_torrent(true);
			return 60;
		},
		[&](std::string method, std::string req
			, std::map<std::string, std::string>&)
		{
			got_announce = true;
			std::string::size_type pos = req.find("&ipv6=");
			TEST_CHECK(pos == std::string::npos);
			got_ipv6 |= pos != std::string::npos;
			return sim::send_response(200, "OK", 11) + "d5:peers0:e";
		}
		, [](torrent_handle) {}
		, [](torrent_handle) {});
	TEST_EQUAL(got_announce, true);
	TEST_EQUAL(got_ipv6, false);
}

TORRENT_TEST(tracker_user_agent_privacy_mode_public_torrent)
{
	bool got_announce = false;
	tracker_test(
		[](lt::add_torrent_params& p, lt::session& ses)
		{
			settings_pack pack;
			pack.set_bool(settings_pack::anonymous_mode, true);
			pack.set_str(settings_pack::user_agent, "test_agent/1.2.3");
			ses.apply_settings(pack);
			p.ti = make_torrent(false);
			return 60;
		},
		[&](std::string method, std::string req
			, std::map<std::string, std::string>& headers)
		{
			got_announce = true;

			// in anonymous mode we should not send a user agent
			TEST_CHECK(headers["user-agent"] == "");
			return sim::send_response(200, "OK", 11) + "d5:peers0:e";
		}
		, [](torrent_handle h) {}
		, [](torrent_handle h) {});
	TEST_EQUAL(got_announce, true);
}

TORRENT_TEST(tracker_user_agent_privacy_mode_private_torrent)
{
	bool got_announce = false;
	tracker_test(
		[](lt::add_torrent_params& p, lt::session& ses)
		{
			settings_pack pack;
			pack.set_bool(settings_pack::anonymous_mode, true);
			pack.set_str(settings_pack::user_agent, "test_agent/1.2.3");
			ses.apply_settings(pack);
			p.ti = make_torrent(true);
			return 60;
		},
		[&](std::string method, std::string req
			, std::map<std::string, std::string>& headers)
		{
			got_announce = true;

			// in anonymous mode we should still send the user agent for private
			// torrents (since private trackers sometimes require it)
			TEST_CHECK(headers["user-agent"] == "test_agent/1.2.3");
			return sim::send_response(200, "OK", 11) + "d5:peers0:e";
		}
		, [](torrent_handle h) {}
		, [](torrent_handle h) {});
	TEST_EQUAL(got_announce, true);
}

// This test sets up two peers, one seed an one downloader. The downloader has
// two trackers, both in tier 0. The behavior we expect is that it picks one of
// the trackers at random and announces to it. Since both trackers are working,
// it should not announce to the tracker it did not initially pick.

// #error parameterize this test over adding the trackers into different tiers
// and setting "announce_to_all_tiers"

TORRENT_TEST(tracker_tiers)
{
	using namespace libtorrent;

	char const* peer0_ip = "50.0.0.1";
	char const* peer1_ip = "50.0.0.2";

	using asio::ip::address;
	address peer0 = addr(peer0_ip);
	address peer1 = addr(peer1_ip);

	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_service ios0 { sim, peer0 };
	sim::asio::io_service ios1 { sim, peer1 };

	sim::asio::io_service tracker1(sim, address_v4::from_string("3.0.0.1"));
	sim::asio::io_service tracker2(sim, address_v4::from_string("3.0.0.2"));
	sim::http_server http1(tracker1, 8080);
	sim::http_server http2(tracker2, 8080);

	bool received_announce[2] = {false, false};
	http1.register_handler("/announce"
		, [&](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		received_announce[0] = true;
		std::string const ret = "d8:intervali60e5:peers0:e";
		return sim::send_response(200, "OK", static_cast<int>(ret.size())) + ret;
	});

	http2.register_handler("/announce"
		, [&](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		received_announce[1] = true;
		std::string const ret = "d8:intervali60e5:peers0:e";
		return sim::send_response(200, "OK", static_cast<int>(ret.size())) + ret;
	});

	lt::session_proxy zombie[2];

	// setup settings pack to use for the session (customization point)
	lt::settings_pack pack = settings();
	// create session
	std::shared_ptr<lt::session> ses[2];
	pack.set_str(settings_pack::listen_interfaces, peer0_ip + std::string(":6881"));
	ses[0] = std::make_shared<lt::session>(pack, ios0);

	pack.set_str(settings_pack::listen_interfaces, peer1_ip + std::string(":6881"));
	ses[1] = std::make_shared<lt::session>(pack, ios1);

	// only monitor alerts for session 0 (the downloader)
	print_alerts(*ses[0], [=](lt::session&, lt::alert const* a) {
		if (auto ta = alert_cast<lt::add_torrent_alert>(a))
		{
			ta->handle.connect_peer(lt::tcp::endpoint(peer1, 6881));
		}
	});

	print_alerts(*ses[1]);

	// the first peer is a downloader, the second peer is a seed
	lt::add_torrent_params params = ::create_torrent(1);
	auto ti2 = clone_ptr(params.ti);
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;

	// These trackers are in the same tier. libtorrent is expected to pick one at
	// random and stick to it, never announce to the other one.
	params.ti->add_tracker("http://3.0.0.1:8080/announce", 0);
	params.ti->add_tracker("http://3.0.0.2:8080/announce", 0);
	params.save_path = save_path(0);
	ses[0]->async_add_torrent(params);

	params.ti = ti2;
	params.save_path = save_path(1);
	ses[1]->async_add_torrent(params);

	sim::timer t(sim, lt::minutes(30), [&](boost::system::error_code const&)
	{
		TEST_CHECK(received_announce[0] != received_announce[1]);
		TEST_CHECK(ses[0]->get_torrents()[0].status().is_seeding);
		TEST_CHECK(ses[1]->get_torrents()[0].status().is_seeding);

		// shut down
		int idx = 0;
		for (auto& s : ses)
		{
			zombie[idx++] = s->abort();
			s.reset();
		}
	});

	sim.run();
}

// TODO: test external IP
// TODO: test with different queuing settings
// TODO: test when a torrent transitions from downloading to finished and
// finished to seeding
// TODO: test that left, downloaded and uploaded are reported correctly

// TODO: test scrape

