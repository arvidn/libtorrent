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

using namespace libtorrent;
using namespace sim;
namespace lt = libtorrent;

using chrono::duration_cast;

// seconds
const int duration = 10000;

template <class T>
boost::shared_ptr<T> clone_ptr(boost::shared_ptr<T> const& ptr)
{
	return boost::make_shared<T>(*ptr);
}

void test_interval(int interval)
{
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	lt::time_point start = lt::clock_type::now();
	bool ran_to_completion = false;

	sim::asio::io_service web_server(sim, address_v4::from_string("2.2.2.2"));
	// listen on port 8080
	sim::http_server http(web_server, 8080);

	// the timestamps of all announces
	std::vector<lt::time_point> announces;

	http.register_handler("/announce"
		, [&announces,interval,start,&ran_to_completion](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		// don't collect events once we're done. We're not interested in the
		// tracker stopped announce for instance
		if (!ran_to_completion)
			announces.push_back(lt::clock_type::now());

		char response[500];
		int size = snprintf(response, sizeof(response), "d8:intervali%de5:peers0:e", interval);
		return sim::send_response(200, "OK", size) + response;
	});

	std::vector<lt::time_point> announce_alerts;

	lt::settings_pack default_settings = settings();
	lt::add_torrent_params default_add_torrent;

	setup_swarm(1, swarm_test::upload, sim, default_settings, default_add_torrent
		// add session
		, [](lt::settings_pack& pack) { }
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.trackers.push_back("http://2.2.2.2:8080/announce");
		}
		// on alert
		, [&](lt::alert const* a, lt::session& ses) {

			if (ran_to_completion) return;
			if (lt::alert_cast<lt::tracker_announce_alert>(a))
				announce_alerts.push_back(a->timestamp());
		}
		// terminate
		, [&](int ticks, lt::session& ses) -> bool {
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
		// make sure the interval is within 500 ms of what it's supposed to be
		// (this accounts for network latencies)
		int const actual_interval_ms = duration_cast<lt::milliseconds>(announces[i] - last_announce).count();
		TEST_CHECK(abs(actual_interval_ms - interval * 1000) < 500);
		last_announce = announces[i];

		int const alert_interval_ms = duration_cast<lt::milliseconds>(announce_alerts[i] - last_alert).count();
		TEST_CHECK(abs(alert_interval_ms - interval * 1000) < 500);
		last_alert = announce_alerts[i];
	}
}

TORRENT_TEST(event_completed)
{
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_service web_server(sim, address_v4::from_string("2.2.2.2"));
	// listen on port 8080
	sim::http_server http(web_server, 8080);

	// the request strings of all announces
	std::vector<std::pair<int, std::string>> announces;

	const int interval = 500;
	lt::time_point start = lt::clock_type::now();

	http.register_handler("/announce"
	, [&](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		TEST_EQUAL(method, "GET");
		int timestamp = chrono::duration_cast<lt::seconds>(
			lt::clock_type::now() - start).count();
		announces.push_back({timestamp, req});

		char response[500];
		int size = snprintf(response, sizeof(response), "d8:intervali%de5:peers0:e", interval);
		return sim::send_response(200, "OK", size) + response;
	});

	lt::settings_pack default_settings = settings();
	lt::add_torrent_params default_add_torrent;

	int completion = -1;

	setup_swarm(2, swarm_test::download, sim, default_settings, default_add_torrent
		// add session
		, [](lt::settings_pack& pack) { }
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.trackers.push_back("http://2.2.2.2:8080/announce");
		}
		// on alert
		, [&](lt::alert const* a, lt::session& ses) {}
		// terminate
		, [&](int ticks, lt::session& ses) -> bool
		{
			if (completion == -1 && is_seed(ses))
			{
				completion = chrono::duration_cast<lt::seconds>(
					lt::clock_type::now() - start).count();
			}

			return ticks > duration;
		});

	// the first announce should be event=started, the second should be
	// event=completed, then all but the last should have no event and the last
	// be event=stopped.
	for (int i = 0; i < int(announces.size()); ++i)
	{
		std::string const& str = announces[i].second;
		int timestamp = announces[i].first;

		const bool has_start = str.find("&event=started")
			!= std::string::npos;
		const bool has_completed = str.find("&event=completed")
			!= std::string::npos;
		const bool has_stopped = str.find("&event=stopped")
			!= std::string::npos;

		// we there can only be one event
		const bool has_event = str.find("&event=") != std::string::npos;

		fprintf(stdout, "- %s\n", str.c_str());

		// there is exactly 0 or 1 events.
		TEST_EQUAL(int(has_start) + int(has_completed) + int(has_stopped)
			, int(has_event));

		switch (i)
		{
			case 0:
				TEST_CHECK(has_start);
				break;
			case 1:
			{
				// the announce should have come approximately the same time we
				// completed
				TEST_CHECK(abs(completion - timestamp) <= 1);
				TEST_CHECK(has_completed);
				break;
			}
			default:
				if (i == int(announces.size()) - 1)
				{
					TEST_CHECK(has_stopped);
				}
				else
				{
					TEST_CHECK(!has_event);
				}
				break;
		}
	}
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
	chrono::high_resolution_clock::duration hostname_lookup(
		asio::ip::address const& requestor
		, std::string hostname
		, std::vector<asio::ip::address>& result
		, boost::system::error_code& ec)
	{
		if (hostname == "tracker.com")
		{
			result.push_back(address_v4::from_string("10.0.0.2"));
			result.push_back(address_v6::from_string("ff::dead:beef"));
			return duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		}

		return default_config::hostname_lookup(requestor, hostname, result, ec);
	}
};

void on_alert_notify(lt::session* ses)
{
	ses->get_io_service().post([ses] {
		std::vector<lt::alert*> alerts;
		ses->pop_alerts(&alerts);

		for (lt::alert* a : alerts)
		{
			lt::time_duration d = a->timestamp().time_since_epoch();
			boost::uint32_t millis = lt::duration_cast<lt::milliseconds>(d).count();
			printf("%4d.%03d: %s\n", millis / 1000, millis % 1000,
				a->message().c_str());
		}
	});
}

void test_ipv6_support(char const* listen_interfaces
	, int const expect_v4, int const expect_v6)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_service web_server_v4(sim, address_v4::from_string("10.0.0.2"));
	sim::asio::io_service web_server_v6(sim, address_v6::from_string("ff::dead:beef"));

	// listen on port 8080
	sim::http_server http_v4(web_server_v4, 8080);
	sim::http_server http_v6(web_server_v6, 8080);

	int v4_announces = 0;
	int v6_announces = 0;

	http_v4.register_handler("/announce"
	, [&v4_announces](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		++v4_announces;
		TEST_EQUAL(method, "GET");

		TEST_CHECK(req.find("&port=6881") != std::string::npos);
		char response[500];
		int size = snprintf(response, sizeof(response), "d8:intervali1800e5:peers0:e");
		return sim::send_response(200, "OK", size) + response;
	});

	http_v6.register_handler("/announce"
	, [&v6_announces](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		++v6_announces;
		TEST_EQUAL(method, "GET");

		TEST_CHECK(req.find("&port=6881") != std::string::npos);
		char response[500];
		int size = snprintf(response, sizeof(response), "d8:intervali1800e5:peers0:e");
		return sim::send_response(200, "OK", size) + response;
	});

	{
		lt::session_proxy zombie;

		asio::io_service ios(sim, { address_v4::from_string("10.0.0.3")
			, address_v6::from_string("ffff::1337") });
		lt::settings_pack sett = settings();
		sett.set_str(settings_pack::listen_interfaces, listen_interfaces);
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
			, [&ses](boost::system::error_code const& ec)
		{
			std::vector<lt::torrent_handle> torrents = ses->get_torrents();
			for (auto const& t : torrents)
			{
				t.pause();
			}
		});

		// then shut down 10 seconds in
		sim::timer t2(sim, lt::seconds(10)
			, [&ses,&zombie](boost::system::error_code const& ec)
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
	// 2 because there's one announce on startup and one when shutting down
	test_ipv6_support("0.0.0.0:6881", 2, 2);
}

TORRENT_TEST(ipv6_support_bind_v4_v6_any)
{
	test_ipv6_support("0.0.0.0:6881,[::0]:6881", 2, 2);
}

TORRENT_TEST(ipv6_support_bind_v6_any)
{
	test_ipv6_support("[::0]:6881", 0, 2);
}

TORRENT_TEST(ipv6_support_bind_v4)
{
	test_ipv6_support("10.0.0.3:6881", 2, 0);
}

TORRENT_TEST(ipv6_support_bind_v6)
{
	test_ipv6_support("[ffff::1337]:6881", 0, 2);
}

TORRENT_TEST(ipv6_support_bind_v4_v6)
{
	test_ipv6_support("10.0.0.3:6881,[ffff::1337]:6881", 2, 2);
}

TORRENT_TEST(ipv6_support_bind_v6_v4)
{
	test_ipv6_support("[ffff::1337]:6881,10.0.0.3:6881", 2, 2);
}

// this runs a simulation of a torrent with tracker(s), making sure the request
// received by the tracker matches the expectation.
// The Setup function is run first, giving the test an opportunity to add
// trackers to the torrent. It's expected to return the number of seconds to
// wait until test2 is called.
// The Announce function is called on http requests. Test1 is run on the session
// 5 seconds after startup. The tracker is running at 10.0.0.2 (or tracker.com)
// port 8080.
template <typename Setup, typename Announce, typename Test1, typename Test2>
void tracker_test(Setup setup, Announce a, Test1 test1, Test2 test2
	, char const* url_path = "/announce")
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_service tracker_ios(sim, address_v4::from_string("10.0.0.2"));

	// listen on port 8080
	sim::http_server http(tracker_ios, 8080);

	http.register_handler(url_path, a);

	lt::session_proxy zombie;

	asio::io_service ios(sim, { address_v4::from_string("10.0.0.3")
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
		, [&ses,&test1](boost::system::error_code const& ec)
	{
		std::vector<lt::torrent_handle> torrents = ses->get_torrents();
		TEST_EQUAL(torrents.size(), 1);
		torrent_handle h = torrents.front();
		test1(h);
	});

	sim::timer t2(sim, lt::seconds(5 + delay)
		, [&ses,&test2](boost::system::error_code const& ec)
	{
		std::vector<lt::torrent_handle> torrents = ses->get_torrents();
		TEST_EQUAL(torrents.size(), 1);
		torrent_handle h = torrents.front();
		test2(h);
	});

	// then shut down 10 seconds in
	sim::timer t3(sim, lt::seconds(10 + delay)
		, [&ses,&zombie](boost::system::error_code const& ec)
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
			, std::map<std::string, std::string>& headers)
		{
			TEST_EQUAL(method, "GET");

			char response[500];
			int size = snprintf(response, sizeof(response), "d14:failure reason4:teste");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](announce_entry const& ae)
		{
			TEST_EQUAL(ae.is_working(), false);
			TEST_EQUAL(ae.message, "test");
			TEST_EQUAL(ae.url, "http://tracker.com:8080/announce");
			TEST_EQUAL(ae.last_error, error_code(errors::tracker_failure));
			TEST_EQUAL(ae.fails, 1);
		});
}

TORRENT_TEST(test_warning)
{
	announce_entry_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>& headers)
		{
			TEST_EQUAL(method, "GET");

			char response[500];
			int size = snprintf(response, sizeof(response), "d5:peers6:aaaaaa15:warning message5:test2e");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](announce_entry const& ae)
		{
			TEST_EQUAL(ae.is_working(), true);
			TEST_EQUAL(ae.message, "test2");
			TEST_EQUAL(ae.url, "http://tracker.com:8080/announce");
			TEST_EQUAL(ae.last_error, error_code());
			TEST_EQUAL(ae.fails, 0);
		});
}

TORRENT_TEST(test_scrape_data_in_announce)
{
	announce_entry_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>& headers)
		{
			TEST_EQUAL(method, "GET");

			char response[500];
			int size = snprintf(response, sizeof(response),
				"d5:peers6:aaaaaa8:completei1e10:incompletei2e10:downloadedi3e11:downloadersi4ee");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](announce_entry const& ae)
		{
			TEST_EQUAL(ae.is_working(), true);
			TEST_EQUAL(ae.message, "");
			TEST_EQUAL(ae.url, "http://tracker.com:8080/announce");
			TEST_EQUAL(ae.last_error, error_code());
			TEST_EQUAL(ae.fails, 0);
			TEST_EQUAL(ae.scrape_complete, 1);
			TEST_EQUAL(ae.scrape_incomplete, 2);
			TEST_EQUAL(ae.scrape_downloaded, 3);
		});
}

TORRENT_TEST(test_scrape)
{
	tracker_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>& headers)
		{
			TEST_EQUAL(method, "GET");

			char response[500];
			int size = snprintf(response, sizeof(response),
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
			TEST_EQUAL(ae.scrape_incomplete, 2);
			TEST_EQUAL(ae.scrape_complete, 1);
			TEST_EQUAL(ae.scrape_downloaded, 3);
		}
		, "/scrape");
}

TORRENT_TEST(test_http_status)
{
	announce_entry_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>& headers)
		{
			TEST_EQUAL(method, "GET");
			return sim::send_response(410, "Not A Tracker", 0);
		}
		, [](announce_entry const& ae)
		{
			TEST_EQUAL(ae.is_working(), false);
			TEST_EQUAL(ae.message, "Not A Tracker");
			TEST_EQUAL(ae.url, "http://tracker.com:8080/announce");
			TEST_EQUAL(ae.last_error, error_code(410, http_category()));
			TEST_EQUAL(ae.fails, 1);
		});
}

TORRENT_TEST(test_interval)
{
	announce_entry_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>& headers)
		{
			TEST_EQUAL(method, "GET");
			char response[500];
			int size = snprintf(response, sizeof(response)
				, "d10:tracker id8:testteste");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](announce_entry const& ae)
		{
			TEST_EQUAL(ae.is_working(), true);
			TEST_EQUAL(ae.message, "");
			TEST_EQUAL(ae.url, "http://tracker.com:8080/announce");
			TEST_EQUAL(ae.last_error, error_code());
			TEST_EQUAL(ae.fails, 0);

			TEST_EQUAL(ae.trackerid, "testtest");
		});
}

TORRENT_TEST(test_invalid_bencoding)
{
	announce_entry_test(
		[](std::string method, std::string req
			, std::map<std::string, std::string>& headers)
		{
			TEST_EQUAL(method, "GET");
			char response[500];
			int size = snprintf(response, sizeof(response)
				, "d10:tracer idteste");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](announce_entry const& ae)
		{
			TEST_EQUAL(ae.is_working(), false);
			TEST_EQUAL(ae.message, "");
			TEST_EQUAL(ae.url, "http://tracker.com:8080/announce");
			TEST_EQUAL(ae.last_error, error_code(bdecode_errors::expected_value
				, bdecode_category()));
			TEST_EQUAL(ae.fails, 1);
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
			, std::map<std::string, std::string>& headers)
		{
			got_announce = true;
			TEST_EQUAL(method, "GET");

			char response[500];
			// respond with an empty peer list
			int size = snprintf(response, sizeof(response), "d5:peers0:e");
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
				fprintf(stdout, "tracker \"%s\"\n", tr[i].url.c_str());
				if (tr[i].url == "http://tracker.com:8080/announce")
				{
					TEST_EQUAL(tr[i].fails, 0);
					TEST_EQUAL(tr[i].verified, true);
				}
				else if (tr[i].url == "http://failing-tracker.com/announce")
				{
					TEST_CHECK(tr[i].fails >= 1);
					TEST_EQUAL(tr[i].verified, false);
					TEST_EQUAL(tr[i].last_error
						, error_code(boost::asio::error::host_not_found));
				}
				else if (tr[i].url == "udp://failing-tracker.com/announce")
				{
					TEST_CHECK(tr[i].fails >= 1);
					TEST_EQUAL(tr[i].verified, false);
					TEST_EQUAL(tr[i].last_error
						, error_code(boost::asio::error::host_not_found));
				}
				else
				{
					TEST_ERROR(("unexpected tracker URL: " + tr[i].url).c_str());
				}
			}
		});
	TEST_EQUAL(got_announce, true);
}

boost::shared_ptr<torrent_info> make_torrent(bool priv)
{
	file_storage fs;
	fs.add_file("foobar", 13241);
	lt::create_torrent ct(fs);

	ct.add_tracker("http://tracker.com:8080/announce");

	for (int i = 0; i < ct.num_pieces(); ++i)
		ct.set_hash(i, sha1_hash(0));

	ct.set_priv(priv);

	entry e = ct.generate();
	std::vector<char> buf;
	bencode(std::back_inserter(buf), e);
	error_code ec;
	return boost::make_shared<torrent_info>(buf.data(), buf.size(), ec);
}

// make sure we _do_ send our IPv6 address to trackers for private torrents
TORRENT_TEST(tracker_ipv6_argument)
{
	bool got_announce = false;
	bool got_ipv6 = false;
	tracker_test(
		[](lt::add_torrent_params& p, lt::session& ses)
		{
			settings_pack pack;
			pack.set_bool(settings_pack::anonymous_mode, false);
			ses.apply_settings(pack);
			p.ti = make_torrent(true);
			return 60;
		},
		[&](std::string method, std::string req
			, std::map<std::string, std::string>& headers)
		{
			got_announce = true;
			std::string::size_type pos = req.find("&ipv6=");
			TEST_CHECK(pos != std::string::npos);
			got_ipv6 = pos != std::string::npos;
			// make sure the IPv6 argument is url encoded
			TEST_CHECK(req.substr(pos + 6, req.find_first_of('&', pos + 6))
				== "ffff%3a%3a1337");
			return sim::send_response(200, "OK", 11) + "d5:peers0:e";
		}
		, [](torrent_handle h) {}
		, [](torrent_handle h) {});
	TEST_EQUAL(got_announce, true);
	TEST_EQUAL(got_ipv6, true);
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
			, std::map<std::string, std::string>& headers)
		{
			got_announce = true;
			std::string::size_type pos = req.find("&ipv6=");
			TEST_CHECK(pos == std::string::npos);
			got_ipv6 = pos != std::string::npos;
			return sim::send_response(200, "OK", 11) + "d5:peers0:e";
		}
		, [](torrent_handle h) {}
		, [](torrent_handle h) {});
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
			, std::map<std::string, std::string>& headers)
		{
			got_announce = true;
			std::string::size_type pos = req.find("&ipv6=");
			TEST_CHECK(pos == std::string::npos);
			got_ipv6 = pos != std::string::npos;
			return sim::send_response(200, "OK", 11) + "d5:peers0:e";
		}
		, [](torrent_handle h) {}
		, [](torrent_handle h) {});
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
		std::string ret = "d8:intervali60e5:peers0:e";
		return sim::send_response(200, "OK", ret.size()) + ret;
	});

	http2.register_handler("/announce"
		, [&](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		received_announce[1] = true;
		std::string ret = "d8:intervali60e5:peers0:e";
		return sim::send_response(200, "OK", ret.size()) + ret;
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
	print_alerts(*ses[0], [=](lt::session& ses, lt::alert const* a) {
		if (auto ta = alert_cast<lt::add_torrent_alert>(a))
		{
			ta->handle.connect_peer(lt::tcp::endpoint(peer1, 6881));
		}
	});

	print_alerts(*ses[1]);

	// the first peer is a downloader, the second peer is a seed
	lt::add_torrent_params params = ::create_torrent(1);
	auto ti2 = clone_ptr(params.ti);
	params.flags &= ~lt::add_torrent_params::flag_auto_managed;
	params.flags &= ~lt::add_torrent_params::flag_paused;

	// These trackers are in the same tier. libtorrent is expected to pick one at
	// random and stick to it, never announce to the other one.
	params.ti->add_tracker("http://3.0.0.1:8080/announce", 0);
	params.ti->add_tracker("http://3.0.0.2:8080/announce", 0);
	params.save_path = save_path(0);
	ses[0]->async_add_torrent(params);

	params.ti = ti2;
	params.save_path = save_path(1);
	ses[1]->async_add_torrent(params);

	sim::timer t(sim, lt::minutes(30), [&](boost::system::error_code const& ec)
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

