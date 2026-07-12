/*

Copyright (c) 2015-2022, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2017-2018, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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
#include "libtorrent/session_stats.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/aux_/ip_helpers.hpp" // for is_v4

#include <boost/optional.hpp>
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

void test_interval(int interval)
{
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	bool ran_to_completion = false;

	sim::asio::io_context web_server(sim, make_address_v4("2.2.2.2"));
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
	TEST_CHECK(announces.size() % 2 == 0);

	lt::time_point last_announce = announces[0];
	lt::time_point last_alert = announce_alerts[0];
	for (int i = 2; i < int(announces.size()); i += 2)
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
std::vector<std::string> test_event(swarm_test_t const type
	, AddTorrent add_torrent
	, OnAlert on_alert)
{
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context web_server(sim, make_address_v4("2.2.2.2"));
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

	// to keep things simple, just consider one of the v1 or v2 announces, since
	// we use a hybrid torrent, we get double announces.
	std::map<std::string, std::vector<std::string>> announces_ih;
	for (auto&& a : announces)
	{
		auto const ih = a.find("info_hash=");
		TEST_CHECK(ih != std::string::npos);
		auto const key = a.substr(ih, 20);
		announces_ih[key].push_back(std::move(a));
	}

	for (auto const& entry : announces_ih)
	{
		auto const& ann = entry.second;
		TEST_CHECK(ann.size() > 2);
		TEST_CHECK(ann.front().find("&event=started") != std::string::npos);
		for (auto const& a : span<std::string const>(ann).subspan(1))
			TEST_CHECK(a.find("&event=started") == std::string::npos);

		TEST_CHECK(ann.back().find("&event=stopped") != std::string::npos);
		for (auto const& a : span<std::string const>(ann).first(ann.size() - 1))
			TEST_CHECK(a.find("&event=stopped") == std::string::npos);
	}
	return announces_ih.begin()->second;
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
	auto const announces = test_event(swarm_test::upload | swarm_test::no_auto_stop
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
	auto const announces = test_event(swarm_test::upload | swarm_test::no_auto_stop
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

// a hybrid torrent announces both its v1 and v2 info-hashes to the tracker. The
// two announces target the same server and overlap in time, so they must be
// coalesced onto a single keep-alive connection rather than each opening a new
// socket -- unless settings_pack::disable_tracker_connection_reuse is set, in
// which case each gets its own connection, as if reuse never existed.
void test_tracker_coalesce_keepalive(bool const disable_reuse)
{
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	bool ran_to_completion = false;

	sim::asio::io_context web_server(sim, make_address_v4("2.2.2.2"));
	sim::http_server http(web_server, 8080);

	int announces = 0;
	http.register_handler("/announce",
		[&](std::string /* method */,
			std::string /* req */
			,
			std::map<std::string, std::string>&) {
			if (!ran_to_completion) ++announces;
			std::string const body = "d8:intervali1800e5:peers0:e";
			return sim::send_response(200, "OK", int(body.size())) + body;
		});

	int connections = 0;

	lt::settings_pack default_settings = settings();
	default_settings.set_str(settings_pack::listen_interfaces, "0.0.0.0:6881");
	default_settings.set_bool(settings_pack::disable_tracker_connection_reuse, disable_reuse);
	lt::add_torrent_params default_add_torrent;

	setup_swarm(
		1,
		swarm_test::upload,
		sim,
		default_settings,
		default_add_torrent,
		[](lt::settings_pack&) {},
		[](lt::add_torrent_params& params) {
			params.trackers.push_back("http://2.2.2.2:8080/announce");
		},
		[&](lt::alert const*, lt::session&) {},
		[&](int const ticks, lt::session&) -> bool {
			if (ticks > 5)
			{
				ran_to_completion = true;
				// record the connection count before the stop-announce
				connections = http.accepted_connections();
				return true;
			}
			return false;
		});

	TEST_CHECK(ran_to_completion);
	// both the v1 and v2 announces reached the tracker...
	TEST_EQUAL(announces, 2);
	// ...over a single coalesced connection, unless reuse is disabled, in
	// which case each gets its own.
	TEST_EQUAL(connections, disable_reuse ? 2 : 1);
}

TORRENT_TEST(tracker_coalesce_keepalive) { test_tracker_coalesce_keepalive(false); }
TORRENT_TEST(tracker_coalesce_keepalive_disabled) { test_tracker_coalesce_keepalive(true); }

TORRENT_TEST(tracker_coalesce_keepalive_after_error)
{
	// a per-response error (a complete HTTP response with a non-200 status) on
	// the first coalesced request must not tear down the keep-alive connection:
	// the second coalesced request is still served on the same socket. Before
	// the fail-granularity change the error closed the connection and the second
	// request opened a fresh one (connections would be 2).
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	bool ran_to_completion = false;

	sim::asio::io_context web_server(sim, make_address_v4("2.2.2.2"));
	sim::http_server http(web_server, 8080);

	int announces = 0;
	http.register_handler("/announce",
		[&](std::string /* method */,
			std::string /* req */
			,
			std::map<std::string, std::string>&) {
			int const n = ran_to_completion ? -1 : announces++;
			if (n == 0)
			{
				// fail the first announce with a complete, well-framed HTTP error
				// response, so the socket is left at a clean message boundary.
				std::string const body = "d14:failure reason5:helloe";
				return sim::send_response(404, "Not Found", int(body.size())) + body;
			}
			std::string const body = "d8:intervali1800e5:peers0:e";
			return sim::send_response(200, "OK", int(body.size())) + body;
		});

	int connections = 0;

	lt::settings_pack default_settings = settings();
	default_settings.set_str(settings_pack::listen_interfaces, "0.0.0.0:6881");
	lt::add_torrent_params default_add_torrent;

	setup_swarm(
		1,
		swarm_test::upload,
		sim,
		default_settings,
		default_add_torrent,
		[](lt::settings_pack&) {},
		[](lt::add_torrent_params& params) {
			params.trackers.push_back("http://2.2.2.2:8080/announce");
		},
		[&](lt::alert const*, lt::session&) {},
		[&](int const ticks, lt::session&) -> bool {
			if (ticks > 5)
			{
				ran_to_completion = true;
				connections = http.accepted_connections();
				return true;
			}
			return false;
		});

	TEST_CHECK(ran_to_completion);
	// both announces reached the tracker (the first failed, the second succeeded)...
	TEST_EQUAL(announces, 2);
	// ...still over a single connection: the error did not close it
	TEST_EQUAL(connections, 1);
}

TORRENT_TEST(tracker_stop_announces_pipelined_on_abort)
{
	// two torrents' final "stopped" announces to the same tracker host, during
	// a real session::abort(), must both reach the tracker promptly -- the
	// second one coalesces as a follower behind the first on the same
	// connection, and the write-only fire-and-forget path dispatches it
	// immediately after the write (not after waiting for a response), rather
	// than sitting behind the first as an ordinary keep-alive follower, which
	// is only promoted once the first request completes or times out
	// (stop_tracker_timeout, 5s default) -- since the tracker below never
	// finishes responding, that would mean the second announce doesn't reach
	// the tracker until ~5s later.
	//
	// Regression test for tracker_manager::m_abort: session_impl::abort()
	// must call tracker_manager::begin_shutdown() (which sets m_abort) before
	// dispatching any torrent's stop announce (via torrent::abort() ->
	// stop_announcing()), so is_stopping() reads true for that entire first
	// wave and the write-only path engages. (A tracker_error_alert-based test
	// can't observe a failure here directly: by the time such a timeout would
	// fire, session_impl::abort() has already destroyed the torrent objects,
	// so the alert never reaches a live requester either way -- the
	// pipelining timing is the only observable difference.)
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0{sim, make_address_v4("10.0.0.1")};
	sim::asio::io_context web_server(sim, make_address_v4("2.2.2.2"));
	sim::http_server http(web_server, 8080);

	int started = 0;
	int stopped = 0;
	http.register_handler("/announce",
		[&](std::string /* method */, std::string req, std::map<std::string, std::string>&) {
			if (req.find("&event=stopped") != std::string::npos)
			{
				++stopped;
				// never actually finish responding: a client waiting for a
				// real response would sit here for the full
				// stop_tracker_timeout before dispatching anything else on
				// this connection.
				return sim::send_response(200, "OK", 1000) + std::string("incomplete");
			}
			++started;
			std::string const body = "d8:intervali1800e5:peers0:e";
			return sim::send_response(200, "OK", int(body.size())) + body;
		});

	lt::settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "10.0.0.1:6881");

	auto ses = std::make_shared<lt::session>(pack, ios0);

	auto const add = [&](int const idx) {
		lt::add_torrent_params params = ::create_torrent(idx, true, 9, lt::create_torrent::v1_only);
		params.flags &= ~lt::torrent_flags::auto_managed;
		params.flags &= ~lt::torrent_flags::paused;
		params.trackers.push_back("http://2.2.2.2:8080/announce");
		ses->async_add_torrent(std::move(params));
	};
	add(0);
	add(1);

	lt::session_proxy zombie;

	sim::timer t_abort(sim, lt::seconds(1), [&](boost::system::error_code const&) {
		// both started announces complete almost instantly on a direct IP
		// connection, well within this window.
		TEST_EQUAL(started, 2);
		// session::abort() alone does very little (just clears the alert
		// notify function); the real session_impl::abort() -- which is what
		// dispatches every torrent's stop announce -- only runs once the
		// session object is actually destroyed, so ses.reset() must happen
		// right alongside it, not in a later callback.
		zombie = ses->abort();
		ses.reset();
	});

	sim::timer t_check(sim, lt::seconds(2), [&](boost::system::error_code const&) {
		// well under stop_tracker_timeout (5s default): both stop announces
		// must already have reached the tracker.
		TEST_EQUAL(stopped, 2);
	});

	sim.run();

	TEST_EQUAL(started, 2);
	TEST_EQUAL(stopped, 2);
}

TORRENT_TEST(tracker_stop_connection_closes_promptly)
{
	// Regression test: once a write-only connection's correctly-identified
	// last follower is dispatched, it requests "Connection: close", and once
	// the drain loop observes the peer's own close (EOF), the connection
	// closes promptly, freeing its slot under max_concurrent_http_announces
	// well before stop_tracker_timeout elapses.
	//
	// Two tracker hosts, two torrents sharing each (so each connection's
	// second, correctly-known-to-be-last dispatch asks for a close), with
	// only one concurrent HTTP announce allowed: whichever host's pair grabs
	// the single slot first must free it quickly, so the other pair can use
	// it too.
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0{sim, make_address_v4("10.0.0.1")};
	sim::asio::io_context web_server1{sim, make_address_v4("2.2.2.2")};
	sim::asio::io_context web_server2{sim, make_address_v4("3.3.3.3")};
	sim::http_server http1(web_server1, 8080);
	sim::http_server http2(web_server2, 8080);

	int stopped1 = 0;
	int stopped2 = 0;
	auto const make_handler = [](int& counter) {
		return [&counter](
				   std::string /* method */, std::string req, std::map<std::string, std::string>&) {
			if (req.find("&event=stopped") != std::string::npos) ++counter;
			std::string const body = "d8:intervali1800e5:peers0:e";
			return sim::send_response(200, "OK", int(body.size())) + body;
		};
	};
	http1.register_handler("/announce", make_handler(stopped1));
	http2.register_handler("/announce", make_handler(stopped2));

	lt::settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "10.0.0.1:6881");
	pack.set_int(settings_pack::max_concurrent_http_announces, 1);
	// set far above the check window below, so a passing test demonstrates
	// the connection actually closed promptly, not merely that
	// stop_tracker_timeout itself happened to be short.
	pack.set_int(settings_pack::stop_tracker_timeout, 100);
	pack.set_int(settings_pack::torrent_connect_boost, 0);

	auto ses = std::make_shared<lt::session>(pack, ios0);

	auto const add = [&](int const idx, char const* url) {
		lt::add_torrent_params params = ::create_torrent(idx, true, 9, lt::create_torrent::v1_only);
		params.flags &= ~lt::torrent_flags::auto_managed;
		params.flags &= ~lt::torrent_flags::paused;
		params.trackers.push_back(url);
		ses->async_add_torrent(std::move(params));
	};
	add(0, "http://2.2.2.2:8080/announce");
	add(1, "http://2.2.2.2:8080/announce");
	add(2, "http://3.3.3.3:8080/announce");
	add(3, "http://3.3.3.3:8080/announce");

	lt::session_proxy zombie;
	sim::timer t_abort(sim, lt::seconds(1), [&](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});

	sim::timer t_check(sim, lt::seconds(5), [&](boost::system::error_code const&) {
		// well under the 100s stop_tracker_timeout: whichever host's pair
		// grabbed the single available slot first must have already closed
		// its connection and freed it up for the other pair.
		TEST_EQUAL(stopped1, 2);
		TEST_EQUAL(stopped2, 2);
	});

	sim.run();

	TEST_EQUAL(stopped1, 2);
	TEST_EQUAL(stopped2, 2);
}

TORRENT_TEST(tracker_stop_solo_torrent_requests_connection_close)
{
	// During shutdown, a solo torrent's write-only stop announce -- the only
	// request ever dispatched on its tracker connection -- must ask for
	// "Connection: close", not keep-alive: deferring the first write-only
	// dispatch by a tick (see send_request()) lets it see that no sibling
	// ever coalesced a follower during the shutdown wave, so it can safely
	// decline keep-alive and get the same prompt-close benefit (see
	// tracker_stop_connection_closes_promptly) as a connection shared by
	// multiple torrents. This is specific to the write-only shutdown path --
	// a steady-state stop announce (session not shutting down) is dispatched
	// as an ordinary normal-mode request and always asks for keep-alive.
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0{sim, make_address_v4("10.0.0.1")};
	sim::asio::io_context web_server{sim, make_address_v4("2.2.2.2")};
	sim::http_server http(web_server, 8080);

	std::string connection_header;
	http.register_handler("/announce",
		[&](std::string /* method */,
			std::string req,
			std::map<std::string, std::string>& headers) {
			if (req.find("&event=stopped") != std::string::npos)
				connection_header = headers["connection"];
			std::string const body = "d8:intervali1800e5:peers0:e";
			return sim::send_response(200, "OK", int(body.size())) + body;
		});

	lt::settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "10.0.0.1:6881");

	auto ses = std::make_shared<lt::session>(pack, ios0);

	lt::add_torrent_params params = ::create_torrent(0, true, 9, lt::create_torrent::v1_only);
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	params.trackers.push_back("http://2.2.2.2:8080/announce");
	ses->async_add_torrent(std::move(params));

	lt::session_proxy zombie;
	sim::timer t_abort(sim, lt::seconds(1), [&](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();

	TEST_EQUAL(connection_header, "close");
}

TORRENT_TEST(tracker_connection_rotates_after_max_requests)
{
	// Regression test: settings_pack::max_tracker_connection_requests bounds
	// how many requests get pipelined onto one connection. Once hit,
	// next_request() rotates the remaining coalesced followers onto a fresh
	// connection instead of continuing to write onto a socket a tracker (or
	// an intermediary reverse proxy) may already have decided to close after
	// its own request-count limit.
	//
	// Five torrents share one tracker host during a real shutdown; with the
	// cap set to 2, coalescing 5 stop announces two per connection needs at
	// least ceil(5/2) = 3 separate connections, and all five must still
	// reach the tracker.
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0{sim, make_address_v4("10.0.0.1")};
	sim::asio::io_context web_server{sim, make_address_v4("2.2.2.2")};
	sim::http_server http(web_server, 8080);

	int stopped = 0;
	http.register_handler("/announce",
		[&](std::string /* method */, std::string req, std::map<std::string, std::string>&) {
			if (req.find("&event=stopped") != std::string::npos) ++stopped;
			std::string const body = "d8:intervali1800e5:peers0:e";
			return sim::send_response(200, "OK", int(body.size())) + body;
		});

	lt::settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "10.0.0.1:6881");
	pack.set_int(settings_pack::max_tracker_connection_requests, 2);
	pack.set_int(settings_pack::torrent_connect_boost, 0);

	auto ses = std::make_shared<lt::session>(pack, ios0);

	auto const add = [&](int const idx) {
		lt::add_torrent_params params = ::create_torrent(idx, true, 9, lt::create_torrent::v1_only);
		params.flags &= ~lt::torrent_flags::auto_managed;
		params.flags &= ~lt::torrent_flags::paused;
		params.trackers.push_back("http://2.2.2.2:8080/announce");
		ses->async_add_torrent(std::move(params));
	};
	for (int i = 0; i < 5; ++i)
		add(i);

	lt::session_proxy zombie;
	sim::timer t_abort(sim, lt::seconds(1), [&](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();

	TEST_EQUAL(stopped, 5);
	TEST_CHECK(http.accepted_connections() >= 3);
}

TORRENT_TEST(tracker_pause_reports_dropped_follower)
{
	// Pausing a session drops any non-stop follower coalesced behind another
	// torrent's in-flight request to the same tracker host
	// (prune_followers(), called from
	// tracker_manager::abort_all_requests(false)). That follower's requester
	// is told the announce was skipped, which resets
	// announce_infohash::updating so can_announce() (which requires
	// !updating) allows that endpoint to be announced to again.
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0{sim, make_address_v4("10.0.0.1")};
	sim::asio::io_context tracker_ios{sim, make_address_v4("2.2.2.2")};
	sim::http_server http(tracker_ios, 8080);

	// never respond, so torrent 0's request stays in flight and torrent 1's
	// coalesces as a follower behind it.
	http.register_stall_handler("/announce");

	lt::settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "10.0.0.1:6881");
	pack.set_int(settings_pack::torrent_connect_boost, 0);

	auto ses = std::make_shared<lt::session>(pack, ios0);

	std::vector<torrent_handle> handles;
	bool follower_error_seen = false;
	error_code follower_error;
	ses->set_alert_notify([&] {
		post(ses->get_context(), [&] {
			std::vector<lt::alert*> alerts;
			ses->pop_alerts(&alerts);
			for (lt::alert* a : alerts)
			{
				if (auto const* at = alert_cast<add_torrent_alert>(a))
					handles.push_back(at->handle);
				else if (auto const* te = alert_cast<tracker_error_alert>(a))
				{
					if (handles.size() > 1 && te->handle == handles[1])
					{
						follower_error_seen = true;
						follower_error = te->error;
					}
				}
			}
		});
	});

	auto const add = [&](int const i) {
		lt::add_torrent_params params = ::create_torrent(i, true, 9, lt::create_torrent::v1_only);
		params.flags &= ~lt::torrent_flags::auto_managed;
		params.flags &= ~lt::torrent_flags::paused;
		params.trackers.push_back("http://2.2.2.2:8080/announce");
		ses->async_add_torrent(std::move(params));
	};
	add(0);
	add(1);

	sim::timer t_pause(
		sim, lt::seconds(2), [&](boost::system::error_code const&) { ses->pause(); });

	lt::session_proxy zombie;
	sim::timer t_end(sim, lt::seconds(3), [&](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();

	TEST_CHECK(follower_error_seen);
	TEST_CHECK(follower_error == lt::errors::announce_skipped);
}

TORRENT_TEST(tracker_pause_reports_in_flight_request)
{
	// Pausing a session while a tracker announce is genuinely in flight
	// (already dispatched, awaiting a response -- as opposed to a follower
	// still queued behind one) must report it as skipped rather than
	// silently dropping it: http_tracker_connection::close() only
	// re-dispatched m_followers, never reporting the connection's own
	// in-flight m_req/m_requester when torn down directly by
	// tracker_manager::abort_all_requests() (reachable on both session
	// pause and shutdown; pause() is used here since, unlike abort(), it
	// doesn't disable alert delivery as a side effect of the call itself).
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0{sim, make_address_v4("10.0.0.1")};
	sim::asio::io_context tracker_ios{sim, make_address_v4("2.2.2.2")};
	sim::http_server http(tracker_ios, 8080);

	// never respond, so the announce stays genuinely in flight.
	http.register_stall_handler("/announce");

	lt::settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "10.0.0.1:6881");

	auto ses = std::make_shared<lt::session>(pack, ios0);

	bool error_seen = false;
	error_code got_error;
	ses->set_alert_notify([&] {
		post(ses->get_context(), [&] {
			std::vector<lt::alert*> alerts;
			ses->pop_alerts(&alerts);
			for (lt::alert* a : alerts)
			{
				if (auto const* te = alert_cast<tracker_error_alert>(a))
				{
					error_seen = true;
					got_error = te->error;
				}
			}
		});
	});

	lt::add_torrent_params params = ::create_torrent(0, true, 9, lt::create_torrent::v1_only);
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	params.trackers.push_back("http://2.2.2.2:8080/announce");
	ses->async_add_torrent(std::move(params));

	sim::timer t_pause(
		sim, lt::seconds(2), [&](boost::system::error_code const&) { ses->pause(); });

	lt::session_proxy zombie;
	sim::timer t_end(sim, lt::seconds(3), [&](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();

	TEST_CHECK(error_seen);
	TEST_CHECK(got_error == lt::errors::announce_skipped);
}

TORRENT_TEST(ssrf_coalesced_follower)
{
	// Regression test for the SSRF-mitigation gap where a follower coalescing
	// onto an already-connected loopback tracker connection bypassed the
	// check entirely: on_filter() is only invoked at DNS-resolution time for
	// a connection's first request, never again for followers reusing the
	// same keep-alive socket. Two torrents pointed at the same loopback
	// host:port (so they coalesce onto one connection): the first uses a
	// safe /announce path and establishes the connection; the second uses an
	// unusual path and queues as a follower behind it. Without the
	// send_request()-time re-check, the second reaches the tracker; with it,
	// the follower is rejected before ever writing to the socket.
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0{sim, make_address_v4("10.0.0.1")};
	sim::asio::io_context tracker_ios{sim, make_address_v4("127.0.0.1")};
	sim::http_server http(tracker_ios, 8080);

	int announce_hits = 0;
	int unusual_hits = 0;
	http.register_handler("/announce",
		[&](std::string /* method */,
			std::string /* req */
			,
			std::map<std::string, std::string>&) {
			++announce_hits;
			std::string const body = "d8:intervali1800e5:peers0:e";
			return sim::send_response(200, "OK", int(body.size())) + body;
		});
	http.register_handler("/unusual-announce-path",
		[&](std::string /* method */,
			std::string /* req */
			,
			std::map<std::string, std::string>&) {
			++unusual_hits;
			std::string const body = "d8:intervali1800e5:peers0:e";
			return sim::send_response(200, "OK", int(body.size())) + body;
		});

	lt::settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "10.0.0.1:6881");
	pack.set_bool(settings_pack::ssrf_mitigation, true);

	auto ses = std::make_shared<lt::session>(pack, ios0);

	int ssrf_errors = 0;
	ses->set_alert_notify([&] {
		post(ses->get_context(), [&] {
			std::vector<lt::alert*> alerts;
			ses->pop_alerts(&alerts);
			for (lt::alert* a : alerts)
			{
				if (auto* e = alert_cast<tracker_error_alert>(a))
				{
					if (e->error == errors::ssrf_mitigation) ++ssrf_errors;
				}
			}
		});
	});

	auto const add = [&](int const idx, char const* path) {
		lt::add_torrent_params params = ::create_torrent(idx, true, 9, lt::create_torrent::v1_only);
		params.flags &= ~lt::torrent_flags::auto_managed;
		params.flags &= ~lt::torrent_flags::paused;
		params.trackers.push_back(std::string("http://127.0.0.1:8080") + path);
		ses->async_add_torrent(std::move(params));
	};
	add(0, "/announce");
	add(1, "/unusual-announce-path");

	lt::session_proxy zombie;
	sim::timer t_end(sim, lt::seconds(5), [&](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();

	// the safe torrent's start and stop announces both reach the tracker...
	TEST_EQUAL(announce_hits, 2);
	// ...the coalesced follower's must never reach it
	TEST_EQUAL(unusual_hits, 0);
	// ...its requester sees the SSRF-mitigation error instead
	TEST_CHECK(ssrf_errors >= 1);
}

TORRENT_TEST(tracker_queued_counter_counts_coalesced_followers)
{
	// Regression test: counters::num_queued_tracker_announces must count
	// individual pending requests, including followers coalesced onto an
	// existing connection's own queue -- not just whole connections queued
	// behind the max_concurrent_http_announces cap. Two torrents share one
	// tracker host; the first's announce never gets a response (so it stays
	// in-flight indefinitely) and the second coalesces as a follower behind
	// it on the same connection -- the counter must report that follower as
	// queued.
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0{sim, make_address_v4("10.0.0.1")};
	sim::asio::io_context tracker_ios{sim, make_address_v4("2.2.2.2")};
	sim::http_server http(tracker_ios, 8080);

	// never respond, so the first request stays in flight and the second
	// coalesces as a follower behind it.
	http.register_stall_handler("/announce");

	lt::settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "10.0.0.1:6881");
	// don't let the initial connect boost make both first announces
	// high-priority, that's not what this test is about
	pack.set_int(settings_pack::torrent_connect_boost, 0);

	auto ses = std::make_shared<lt::session>(pack, ios0);

	std::int64_t queued = -1;
	int const idx = lt::find_metric_idx("tracker.num_queued_tracker_announces");
	ses->set_alert_notify([&] {
		post(ses->get_context(), [&] {
			std::vector<lt::alert*> alerts;
			ses->pop_alerts(&alerts);
			for (lt::alert* a : alerts)
			{
				if (auto* ss = alert_cast<session_stats_alert>(a)) queued = ss->counters()[idx];
			}
		});
	});

	auto const add = [&](int const i) {
		lt::add_torrent_params params = ::create_torrent(i, true, 9, lt::create_torrent::v1_only);
		params.flags &= ~lt::torrent_flags::auto_managed;
		params.flags &= ~lt::torrent_flags::paused;
		params.trackers.push_back("http://2.2.2.2:8080/announce");
		ses->async_add_torrent(std::move(params));
	};
	add(0);
	add(1);

	sim::timer t_stats(
		sim, lt::seconds(2), [&](boost::system::error_code const&) { ses->post_session_stats(); });

	lt::session_proxy zombie;
	sim::timer t_end(sim, lt::seconds(3), [&](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();

	// torrent 0's announce is in flight (not queued); torrent 1's is the one
	// coalesced follower actually waiting.
	TEST_EQUAL(queued, 1);
}

namespace {
	// resolves a single tracker hostname slowly, so the first announce to it sits in
	// DNS resolution for a long, deterministic window while later announces to the
	// same host pile up as queued followers on the (already pooled) connection.
	struct slow_dns_config : sim::default_config
	{
		chrono::high_resolution_clock::duration hostname_lookup(asio::ip::address const& requestor,
			std::string hostname,
			std::vector<asio::ip::address>& result,
			boost::system::error_code& ec) override
		{
			if (hostname == "slowtracker.test")
			{
				result.push_back(make_address_v4("2.2.2.2"));
				return duration_cast<chrono::high_resolution_clock::duration>(chrono::seconds(2));
			}
			return default_config::hostname_lookup(requestor, hostname, result, ec);
		}
	};
}

TORRENT_TEST(tracker_high_priority_jumps_follower_queue)
{
	// A high-priority announce that coalesces onto a connection which already has
	// queued followers must jump to the front of the per-connection FIFO: it is
	// served ahead of the normal followers that were queued earlier (it cannot
	// preempt the request that is already in flight).
	//
	// This uses four separate torrents that all announce to the same slow-
	// resolving host, so their announces coalesce onto one connection (the
	// tracker connection pool is keyed by destination, not by torrent).
	// A torrent's very first announce is high priority exactly when
	// settings_pack::torrent_connect_boost is non-zero at the moment the
	// torrent is added (torrent::start_announcing() reads it once, into
	// m_connect_boost_counter). Toggling that setting between add_torrent()
	// calls produces low- and then high-priority followers, without going
	// through force_reannounce() or any tier-ordering logic.
	slow_dns_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0{sim, make_address_v4("10.0.0.1")};
	sim::asio::io_context web_server(sim, make_address_v4("2.2.2.2"));
	sim::http_server http(web_server, 8080);

	std::vector<std::string> order;
	auto const make_handler = [&order](std::string path) {
		return [&order, path](std::string /* method */,
				   std::string /* req */
				   ,
				   std::map<std::string, std::string>&) {
			order.push_back(path);
			std::string const body = "d8:intervali1800e5:peers0:e";
			return sim::send_response(200, "OK", int(body.size())) + body;
		};
	};
	http.register_handler("/announce-a", make_handler("/announce-a"));
	http.register_handler("/announce-b", make_handler("/announce-b"));
	http.register_handler("/announce-c", make_handler("/announce-c"));
	http.register_handler("/announce-d", make_handler("/announce-d"));

	lt::settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, "10.0.0.1:6881");

	auto ses = std::make_shared<lt::session>(pack, ios0);

	// v1-only torrent, so there is exactly one announce per tracker.
	auto const add = [&](int const idx, char const* path) {
		lt::add_torrent_params params = ::create_torrent(idx, true, 9, lt::create_torrent::v1_only);
		params.flags &= ~lt::torrent_flags::auto_managed;
		params.flags &= ~lt::torrent_flags::paused;
		params.trackers.push_back(std::string("http://slowtracker.test:8080") + path);
		ses->async_add_torrent(std::move(params));
	};

	sim::timer t_start(sim, lt::seconds(0), [&](boost::system::error_code const&) {
		// default torrent_connect_boost (non-zero): torrent a's first announce
		// is high priority. It becomes the in-flight request while the slow DNS
		// lookup for the shared host is outstanding.
		add(0, "/announce-a");

		// disable connect boost so the next two torrents' first announces are
		// NOT high priority: they queue as normal followers behind a.
		lt::settings_pack boost_off;
		boost_off.set_int(settings_pack::torrent_connect_boost, 0);
		ses->apply_settings(boost_off);

		add(1, "/announce-b");
		add(2, "/announce-c");

		// re-enable connect boost so this torrent's first announce is high
		// priority again -- it must jump ahead of the already-queued b, c.
		lt::settings_pack boost_on;
		boost_on.set_int(settings_pack::torrent_connect_boost, 30);
		ses->apply_settings(boost_on);

		add(3, "/announce-d");
	});

	lt::session_proxy zombie;
	sim::timer t_end(sim, lt::seconds(12), [&](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();

	auto const pos = [&](char const* p) -> int {
		for (int i = 0; i < int(order.size()); ++i)
			if (order[i] == p) return i;
		return int(order.size());
	};

	// the in-flight request (a) is served first...
	TEST_CHECK(!order.empty() && order.front() == "/announce-a");
	// ...then the high-priority d jumps ahead of the earlier-queued b and c
	TEST_CHECK(pos("/announce-d") < pos("/announce-b"));
	TEST_CHECK(pos("/announce-d") < pos("/announce-c"));
}

namespace {
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
			result.push_back(make_address_v4("123.0.0.2"));
			if (ipv6)
				result.push_back(make_address_v6("ff::dead:beef"));
			return duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		}
		if (hostname == "localhost")
		{
			result.push_back(make_address_v4("127.0.0.1"));
			if (ipv6)
				result.push_back(make_address_v6("::1"));
			return duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(1));
		}
		if (hostname == "xn--tracker-.com")
		{
			result.push_back(make_address_v4("123.0.0.2"));
			return duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		}
		if (hostname == "redirector.com")
		{
			result.push_back(make_address_v4("123.0.0.4"));
			return duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		}

		return default_config::hostname_lookup(requestor, hostname, result, ec);
	}

	bool ipv6;
};
} // anonymous namespace

void on_alert_notify(lt::session* ses)
{
	post(ses->get_context(), [ses] {
		std::vector<lt::alert*> alerts;
		ses->pop_alerts(&alerts);

		for (lt::alert* a : alerts)
		{
			lt::time_duration d = a->timestamp().time_since_epoch();
			std::uint32_t const millis = std::uint32_t(
				lt::duration_cast<lt::milliseconds>(d).count());
			std::printf("%4u.%03u: %s\n", millis / 1000, millis % 1000,
				a->message().c_str());
		}
	});
}

void test_announce()
{
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context web_server(sim, make_address_v4("2.2.2.2"));

	// listen on port 8080
	sim::http_server http(web_server, 8080);

	int announces = 0;

	// expect announced IP & port
	std::string const expect_port = "&port=1234";
	std::string const expect_ip = "&ip=1.2.3.4";

	http.register_handler("/announce"
	, [&announces, expect_port, expect_ip](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		++announces;
		TEST_EQUAL(method, "GET");
		TEST_CHECK(req.find(expect_port) != std::string::npos);
		TEST_CHECK(req.find(expect_ip) != std::string::npos);
		char response[500];
		int const size = std::snprintf(response, sizeof(response), "d8:intervali1800e5:peers0:e");
		return sim::send_response(200, "OK", size) + response;
	});

	{
		lt::session_proxy zombie;

		std::vector<asio::ip::address> ips;
		ips.push_back(make_address("123.0.0.3"));

		asio::io_context ios(sim, ips);
		lt::settings_pack sett = settings();
		sett.set_str(settings_pack::listen_interfaces, "0.0.0.0:6881");
		sett.set_str(settings_pack::announce_ip, "1.2.3.4");
		sett.set_int(settings_pack::announce_port, 1234);

		auto ses = std::make_unique<lt::session>(sett, ios);

		ses->set_alert_notify(std::bind(&on_alert_notify, ses.get()));

		lt::add_torrent_params p;
		p.name = "test-torrent";
		p.save_path = ".";
		p.info_hashes.v1.assign("abababababababababab");

		p.trackers.push_back("http://2.2.2.2:8080/announce");
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

	TEST_EQUAL(announces, 2);
}

// this test makes sure that a seed can overwrite its announced IP & port
TORRENT_TEST(announce_ip_port) {
	test_announce();
}

static const int num_interfaces = 3;

void test_ipv6_support(char const* listen_interfaces
	, int const expect_v4, int const expect_v6)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context web_server_v4(sim, make_address_v4("123.0.0.2"));
	sim::asio::io_context web_server_v6(sim, make_address_v6("ff::dead:beef"));

	// listen on port 8080
	sim::http_server http_v4(web_server_v4, 8080);
	sim::http_server http_v6(web_server_v6, 8080);

	int v4_announces = 0;
	int v6_announces = 0;

	// if we're not listening we'll just report port 0
	std::string const expect_port = (listen_interfaces && listen_interfaces == ""_sv)
		? "&port=1" : "&port=6881";

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
			ips.push_back(make_address(ep));
			std::snprintf(ep, sizeof(ep), "ffff::1337:%d", i + 1);
			ips.push_back(make_address(ep));
		}

		asio::io_context ios(sim, ips);
		lt::settings_pack sett = settings();
		if (listen_interfaces)
		{
			sett.set_str(settings_pack::listen_interfaces, listen_interfaces);
		}
		auto ses = std::make_unique<lt::session>(sett, ios);

		ses->set_alert_notify(std::bind(&on_alert_notify, ses.get()));

		lt::add_torrent_params p;
		p.name = "test-torrent";
		p.save_path = ".";
		p.info_hashes.v1.assign("abababababababababab");

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

	sim::asio::io_context web_server_v4(sim, make_address_v4("123.0.0.2"));
	sim::asio::io_context web_server_v6(sim, make_address_v6("ff::dead:beef"));

	int v4_announces = 0;
	int v6_announces = 0;

	{
		lt::session_proxy zombie;

		std::vector<asio::ip::address> ips;

		for (int i = 0; i < num_interfaces; i++)
		{
			char ep[30];
			std::snprintf(ep, sizeof(ep), "123.0.0.%d", i + 1);
			ips.push_back(make_address(ep));
			std::snprintf(ep, sizeof(ep), "ffff::1337:%d", i + 1);
			ips.push_back(make_address(ep));
		}

		asio::io_context ios(sim, ips);
		lt::settings_pack sett = settings();
		if (listen_interfaces)
		{
			sett.set_str(settings_pack::listen_interfaces, listen_interfaces);
		}
		auto ses = std::make_unique<lt::session>(sett, ios);

		// since we don't have a udp tracker to run in the sim, looking for the
		// alerts is the closest proxy
		ses->set_alert_notify([&]{
			post(ses->get_context(), [&] {
				std::vector<lt::alert*> alerts;
				ses->pop_alerts(&alerts);

				for (lt::alert* a : alerts)
				{
					lt::time_duration d = a->timestamp().time_since_epoch();
					std::uint32_t const millis = std::uint32_t(
						lt::duration_cast<lt::milliseconds>(d).count());
					std::printf("%4u.%03u: %s\n", millis / 1000, millis % 1000,
						a->message().c_str());
					if (auto tr = alert_cast<tracker_announce_alert>(a))
					{
						if (lt::aux::is_v4(tr->local_endpoint))
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
		p.info_hashes.v1.assign("abababababababababab");

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
	test_ipv6_support(nullptr, num_interfaces * 2, num_interfaces * 2);
}

TORRENT_TEST(announce_no_listen)
{
	// if we don't listen on any sockets at all we should not announce to trackers
	test_ipv6_support("", 0, 0);
}

TORRENT_TEST(announce_udp_no_listen)
{
	// if we don't listen on any sockets at all we should not announce to trackers
	test_udpv6_support("", 0, 0);
}

TORRENT_TEST(ipv6_support_bind_v4_v6_any)
{
	// 2 because there's one announce on startup and one when shutting down
	// IPv6 will send announces for each interface
	test_ipv6_support("0.0.0.0:6881,[::0]:6881", num_interfaces * 2, num_interfaces * 2);
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
	, char const* url_path = "/announce"
	, char const* redirect = "http://123.0.0.2/announce")
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context tracker_ios(sim, make_address_v4("123.0.0.2"));
	sim::asio::io_context tracker_ios6(sim, make_address_v6("ff::dead:beef"));
	sim::asio::io_context redirector_ios(sim, make_address_v4("123.0.0.4"));

	sim::asio::io_context tracker_lo_ios(sim, make_address_v4("127.0.0.1"));
	sim::asio::io_context tracker_lo_ios6(sim, make_address_v6("::1"));

	// listen on port 8080
	sim::http_server http(tracker_ios, 8080);
	sim::http_server http6(tracker_ios6, 8080);
	sim::http_server http_lo(tracker_lo_ios, 8080);
	sim::http_server http6_lo(tracker_lo_ios6, 8080);
	sim::http_server http_redirect(redirector_ios, 8080);

	http.register_handler(url_path, a);
	http6.register_handler(url_path, a);
	http_lo.register_handler(url_path, a);
	http6_lo.register_handler(url_path, a);
	http_redirect.register_redirect(url_path, redirect);

	lt::session_proxy zombie;

	asio::io_context ios(sim, { make_address_v4("123.0.0.3")
		, make_address_v6("ffff::1337") });
	lt::settings_pack sett = settings();
	auto ses = std::make_unique<lt::session>(sett, ios);

	ses->set_alert_notify(std::bind(&on_alert_notify, ses.get()));

	lt::add_torrent_params p;
	p.info_hashes.v1.assign("abababababababababab");
	int const delay = setup(p, *ses);
	p.name = "test-torrent";
	p.save_path = ".";
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

// test that we correctly omit announcing an event=stopped to a tracker we never
// managed to send an event=start to
TORRENT_TEST(omit_stop_event)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	lt::session_proxy zombie;

	asio::io_context ios(sim, { make_address_v4("123.0.0.3"), make_address_v6("ff::dead:beef")});
	lt::settings_pack sett = settings();
	std::unique_ptr<lt::session> ses(new lt::session(sett, ios));

	print_alerts(*ses);

	lt::add_torrent_params p;
	p.name = "test-torrent";
	p.save_path = ".";
	p.info_hashes.v1.assign("abababababababababab");
	p.trackers.push_back("udp://tracker.com:8080/announce");
	ses->async_add_torrent(p);

	// run the test 5 seconds in
	sim::timer t1(sim, lt::seconds(5)
		, [&ses](boost::system::error_code const&)
	{
		std::vector<lt::torrent_handle> torrents = ses->get_torrents();
		TEST_EQUAL(torrents.size(), 1);
		torrent_handle h = torrents.front();
	});

	int stop_announces = 0;

	sim::timer t2(sim, lt::seconds(1800)
		, [&](boost::system::error_code const&)
	{
		// make sure we don't announce a stopped event when stopping
		print_alerts(*ses, [&](lt::session&, lt::alert const* a) {
			if (alert_cast<lt::tracker_announce_alert>(a))
			++stop_announces;
		});
		std::vector<lt::torrent_handle> torrents = ses->get_torrents();
		TEST_EQUAL(torrents.size(), 1);
		torrent_handle h = torrents.front();
		h.set_flags(torrent_flags::paused, torrent_flags::paused | torrent_flags::auto_managed);
	});

	// then shut down 10 seconds in
	sim::timer t3(sim, lt::seconds(1810)
		, [&](boost::system::error_code const&)
	{
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();

	TEST_EQUAL(stop_announces, 0);
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
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].message, "test");
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].last_error, error_code(errors::tracker_failure));
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].fails, 1);
			}
		});
}

TORRENT_TEST(test_no_announce_path)
{
	tracker_test(
		[](lt::add_torrent_params& p, lt::session&) {
			p.trackers.push_back("http://tracker.com:8080");
			return 5;
		},
		[](std::string method, std::string req, std::map<std::string, std::string>&)
		{
			TEST_EQUAL(method, "GET");

			char response[500];
			int const size = std::snprintf(response, sizeof(response), "d5:peers6:aaaaaae");
			return sim::send_response(200, "OK", size) + response;
		}
		, [](torrent_handle h)
		{
			std::vector<announce_entry> tr = h.trackers();

			TEST_EQUAL(tr.size(), 1);
			announce_entry const& ae = tr[0];
			TEST_EQUAL(ae.url, "http://tracker.com:8080");
			TEST_EQUAL(ae.endpoints.size(), 2);
			for (auto const& aep : ae.endpoints)
			{
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].message, "");
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].last_error, error_code());
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].fails, 0);
			}
		}
		, [](torrent_handle){}
		, "/");
}

TORRENT_TEST(paused_session)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context tracker_ios(sim, make_address_v4("123.0.0.2"));
	// listen on port 8080
	sim::http_server http(tracker_ios, 8080);

	int announces = 0;

	http.register_handler("/announce",
		[&announces](std::string method, std::string req, std::map<std::string, std::string>&)
		{
			TEST_EQUAL(method, "GET");

			++announces;
			char response[500];
			int const size = std::snprintf(response, sizeof(response), "d8:intervali1800e5:peers6:aaaaaae");
			return sim::send_response(200, "OK", size) + response;
		}
	);

	lt::session_proxy zombie;

	asio::io_context ios(sim, { make_address_v4("123.0.0.3")
		, make_address_v6("ffff::1337") });
	lt::settings_pack sett = settings();
	auto ses = std::make_unique<lt::session>(sett, ios);

	ses->set_alert_notify(std::bind(&on_alert_notify, ses.get()));

	lt::add_torrent_params p;
	p.name = "test-torrent";
	p.save_path = ".";
	p.info_hashes.v1.assign("abababababababababab");
	p.trackers.push_back("http://123.0.0.2:8080/announce");
	ses->async_add_torrent(p);

	lt::seconds timeline(5);

	// pause the session
	sim::timer t1(sim, timeline
		, [&announces,&ses](boost::system::error_code const&)
	{
		// make sure we got 1 announce
		TEST_EQUAL(announces, 1);
		ses->pause();
	});

	// wait until the next tracker announce should have happened, but didn't
	// because the session is paused
	timeline += seconds(1801);

	sim::timer t2(sim, timeline
		, [&announces,&ses](boost::system::error_code const&)
	{
		// the stop is announced
		TEST_EQUAL(announces, 2);
		ses->resume();
	});

	timeline += seconds(5);

	sim::timer t3(sim, timeline
		, [&announces](boost::system::error_code const&)
	{
		// make sure we got another announce
		TEST_EQUAL(announces, 3);
	});

	timeline += seconds(5);

	// then shut down
	sim::timer t4(sim, timeline, [&ses, &zombie](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

// pausing a session while a tracker announce is genuinely in flight
// (already dispatched, awaiting a response, as opposed to paused_session
// above where the announce has already completed) must still report the
// request being torn down, via tracker_manager::abort_all_requests(), so
// announce_infohash::updating gets cleared and the tracker becomes
// eligible to be announced to again once the session is resumed.
TORRENT_TEST(paused_session_in_flight_announce)
{
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context tracker_ios(sim, make_address_v4("123.0.0.2"));
	sim::http_server http(tracker_ios, 8080);

	// never respond, so the announce stays genuinely in flight when the
	// session is paused.
	http.register_stall_handler("/announce");

	lt::session_proxy zombie;

	asio::io_context ios(sim, make_address_v4("123.0.0.3"));
	lt::settings_pack sett = settings();
	auto ses = std::make_unique<lt::session>(sett, ios);

	bool error_seen = false;
	error_code got_error;
	ses->set_alert_notify([&] {
		post(ses->get_context(), [&] {
			std::vector<lt::alert*> alerts;
			ses->pop_alerts(&alerts);
			for (lt::alert* a : alerts)
			{
				if (auto const* te = alert_cast<tracker_error_alert>(a))
				{
					error_seen = true;
					got_error = te->error;
				}
			}
		});
	});

	lt::add_torrent_params p;
	p.name = "test-torrent";
	p.save_path = ".";
	p.info_hashes.v1.assign("abababababababababab");
	p.trackers.push_back("http://123.0.0.2:8080/announce");
	ses->async_add_torrent(p);

	sim::timer t1(sim, lt::seconds(2), [&](boost::system::error_code const&) { ses->pause(); });

	sim::timer t2(sim, lt::seconds(3), [&](boost::system::error_code const&) {
		TEST_CHECK(error_seen);
		TEST_CHECK(got_error == error_code(errors::announce_skipped));

		std::vector<lt::torrent_handle> torrents = ses->get_torrents();
		TEST_EQUAL(torrents.size(), 1);
		std::vector<announce_entry> tr = torrents[0].trackers();
		TEST_EQUAL(tr.size(), 1);
		TEST_EQUAL(tr[0].endpoints.size(), 1);
		TEST_CHECK(!tr[0].endpoints[0].info_hashes[protocol_version::V1].updating);
	});

	sim::timer t3(sim, lt::seconds(4), [&ses, &zombie](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
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
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].message, "test2");
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].last_error, error_code());
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].fails, 0);
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
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].message, "");
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].last_error, error_code());
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].fails, 0);
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].scrape_complete, 1);
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].scrape_incomplete, 2);
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].scrape_downloaded, 3);
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
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].scrape_incomplete, 2);
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].scrape_complete, 1);
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].scrape_downloaded, 3);
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
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].message, "Not A Tracker");
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].last_error, error_code(410, http_category()));
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].fails, 1);
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
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].message, "");
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].last_error, error_code());
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].fails, 0);
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
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].message, "");
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].last_error, error_code(bdecode_errors::expected_value
					, bdecode_category()));
				TEST_EQUAL(aep.info_hashes[protocol_version::V1].fails, 1);
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
						TEST_EQUAL(aep.info_hashes[protocol_version::V1].fails, 0);
					}
					TEST_EQUAL(tr[i].verified, true);
				}
				else if (tr[i].url == "http://failing-tracker.com/announce")
				{
					for (auto const& aep : tr[i].endpoints)
					{
						TEST_CHECK(aep.info_hashes[protocol_version::V1].fails >= 1);
						TEST_EQUAL(aep.info_hashes[protocol_version::V1].last_error
							, error_code(boost::asio::error::host_not_found));
					}
					TEST_EQUAL(tr[i].verified, false);
				}
				else if (tr[i].url == "udp://failing-tracker.com/announce")
				{
					TEST_EQUAL(tr[i].verified, false);
					for (auto const& aep : tr[i].endpoints)
					{
						TEST_CHECK(aep.info_hashes[protocol_version::V1].fails >= 1);
						TEST_EQUAL(aep.info_hashes[protocol_version::V1].last_error
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

			TEST_EQUAL(aep.info_hashes[protocol_version::V1].fails, 0);
			TEST_CHECK(!aep.info_hashes[protocol_version::V1].last_error);
			TEST_EQUAL(aep.info_hashes[protocol_version::V1].message, "");

#if TORRENT_ABI_VERSION <= 2
			TEST_EQUAL(aep.fails, 0);
			TEST_CHECK(!aep.last_error);
			TEST_EQUAL(aep.message, "");
#endif
		});
	TEST_EQUAL(num_announces, 2);
}

// make sure a tracker announce forced with the high_priority flag jumps
// ahead of an announce that's already waiting in the tracker queue,
// instead of waiting behind it for a free announce slot.
//
// this uses 3 separate single-tracker torrents sharing the session-wide
// tracker queue:
// - "fast" completes a normal announce right away, so its tracker is no
//   longer in flight (not "updating") and is eligible to be re-announced
//   on demand, like any tracker that's already been talked to once.
// - "hold" is added next and occupies the only available announce slot
//   forever (its tracker never responds), so anything announced after it
//   has to wait in the queue.
// - "low" is added after "hold" and queues normally, behind "hold".
// - "fast" is then force-reannounced with the high_priority flag. Since
//   its tracker isn't in flight anymore, this queues a new request for
//   it, which should jump ahead of "low" in the queue.
void test_force_reannounce_high_priority_skips_queue(bool const by_url)
{
	using sim::asio::ip::address_v4;
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context fast_ios(sim, make_address_v4("3.0.0.1"));
	sim::asio::io_context hold_ios(sim, make_address_v4("3.0.0.2"));
	sim::asio::io_context low_ios(sim, make_address_v4("3.0.0.3"));

	sim::http_server fast_http(fast_ios, 8080);
	sim::http_server hold_http(hold_ios, 8080);
	sim::http_server low_http(low_ios, 8080);

	// never respond. This ties up the single available announce slot
	// until it eventually times out.
	hold_http.register_stall_handler("/announce");

	// only record announces once "tracking" is turned on, i.e. after
	// "fast"'s initial (unforced) announce has already completed
	bool tracking = false;
	std::vector<std::string> announce_order;

	fast_http.register_handler(
		"/announce", [&](std::string, std::string, std::map<std::string, std::string>&) {
			if (tracking) announce_order.push_back("fast");
			std::string const ret = "d8:intervali1800e5:peers0:e";
			return sim::send_response(200, "OK", int(ret.size())) + ret;
		});

	low_http.register_handler(
		"/announce", [&](std::string, std::string, std::map<std::string, std::string>&) {
			if (tracking) announce_order.push_back("low");
			std::string const ret = "d8:intervali1800e5:peers0:e";
			return sim::send_response(200, "OK", int(ret.size())) + ret;
		});

	lt::session_proxy zombie;

	asio::io_context ios(sim, make_address_v4("123.0.0.3"));
	lt::settings_pack sett = settings();
	sett.set_str(settings_pack::listen_interfaces, "123.0.0.3:6881");
	// only one HTTP tracker announce may be in flight at a time, so
	// whichever one doesn't get the slot has to queue
	sett.set_int(settings_pack::max_concurrent_http_announces, 1);
	// don't let the initial connect boost make the very first announces
	// high priority, that would confuse this test
	sett.set_int(settings_pack::torrent_connect_boost, 0);
	// give up on the stalled announce quickly so the queue can drain
	// within the lifetime of this test
	sett.set_int(settings_pack::tracker_completion_timeout, 2);

	auto ses = std::make_unique<lt::session>(sett, ios);
	ses->set_alert_notify(std::bind(&on_alert_notify, ses.get()));

	auto make_params = [](char const* name, char const* info_hash, char const* url) {
		lt::add_torrent_params p;
		p.name = name;
		p.save_path = ".";
		p.info_hashes.v1.assign(info_hash);
		p.trackers.push_back(url);
		// start announcing right away, instead of waiting for the auto
		// manager to un-pause the torrent half a second in
		p.flags &= ~lt::torrent_flags::auto_managed;
		p.flags &= ~lt::torrent_flags::paused;
		return p;
	};

	auto find_handle = [&](char const* name) {
		for (auto const& h : ses->get_torrents())
			if (h.status().name == name) return h;
		return torrent_handle();
	};

	// "fast" is added first and gets the only announce slot to itself,
	// so its first announce completes right away
	ses->async_add_torrent(
		make_params("fast-torrent", "aaaaaaaaaaaaaaaaaaaa", "http://3.0.0.1:8080/announce"));

	// 1 second in, "fast" has long since completed its announce and
	// freed up the only slot. "hold" grabs it and ties it up, then "low"
	// queues behind "hold". Finally "fast" is force-reannounced with the
	// high_priority flag, and should queue ahead of "low".
	sim::timer t1(sim, lt::seconds(1), [&](boost::system::error_code const&) {
		ses->async_add_torrent(
			make_params("hold-torrent", "bbbbbbbbbbbbbbbbbbbb", "http://3.0.0.2:8080/announce"));
	});

	sim::timer t2(
		sim, lt::seconds(1) + lt::milliseconds(100), [&](boost::system::error_code const&) {
			ses->async_add_torrent(
				make_params("low-torrent", "cccccccccccccccccccc", "http://3.0.0.3:8080/announce"));
		});

	sim::timer t3(
		sim, lt::seconds(1) + lt::milliseconds(200), [&](boost::system::error_code const&) {
			tracking = true;
			reannounce_flags_t const flags =
				torrent_handle::ignore_min_interval | torrent_handle::high_priority;
			torrent_handle fast = find_handle("fast-torrent");
			TEST_CHECK(fast.is_valid());
			if (by_url)
				fast.force_reannounce(0, "http://3.0.0.1:8080/announce", flags);
			else
				fast.force_reannounce(0, 0, flags);
		});

	// by 6 seconds in, "hold" has timed out (after 2s) and both "fast"
	// and "low" should have been serviced, in that order
	sim::timer t4(sim, lt::seconds(6), [&](boost::system::error_code const&) {
		TEST_EQUAL(announce_order.size(), std::size_t(2));
		if (announce_order.size() == 2)
		{
			TEST_EQUAL(announce_order[0], "fast");
			TEST_EQUAL(announce_order[1], "low");
		}
	});

	sim::timer t5(sim, lt::seconds(10), [&](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

TORRENT_TEST(force_reannounce_high_priority_skips_queue)
{
	test_force_reannounce_high_priority_skips_queue(false);
}

// this covers the same behavior via the tracker-url overload of
// force_reannounce()
TORRENT_TEST(force_reannounce_url_high_priority_skips_queue)
{
	test_force_reannounce_high_priority_skips_queue(true);
}

lt::add_torrent_params make_torrent(bool priv)
{
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("foobar", 13241);
	lt::create_torrent ct(std::move(fs));

	ct.add_tracker("http://tracker.com:8080/announce");

	for (piece_index_t i(0); i < piece_index_t(ct.num_pieces()); ++i)
		ct.set_hash(i, sha1_hash::max());

	ct.set_priv(priv);

	return lt::load_torrent_buffer(lt::bencode(ct.generate()));
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
			p = make_torrent(true);
			p.info_hashes = lt::info_hash_t{};
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
			p = make_torrent(true);
			p.info_hashes = lt::info_hash_t{};
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

	// make sure we got the same key for all listen socket interface
	TEST_EQUAL(keys.size(), 1);
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
			p = make_torrent(false);
			p.info_hashes = lt::info_hash_t{};
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
			p = make_torrent(true);
			p.info_hashes = lt::info_hash_t{};
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
			p = make_torrent(false);
			p.info_hashes = lt::info_hash_t{};
			return 60;
		},
		[&](std::string method, std::string req
			, std::map<std::string, std::string>& headers)
		{
			got_announce = true;

			// in anonymous mode we should send a generic user agent
			TEST_CHECK(headers["user-agent"] == "curl/7.81.0");
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
			p = make_torrent(true);
			p.info_hashes = lt::info_hash_t{};
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

bool test_ssrf(char const* announce_path, bool const feature_on
	, char const* tracker_url)
{
	bool got_announce = false;
	tracker_test(
		[&](lt::add_torrent_params& p, lt::session& ses)
		{
			settings_pack pack;
			pack.set_bool(settings_pack::ssrf_mitigation, feature_on);
			ses.apply_settings(pack);
			p.trackers.emplace_back(tracker_url);
			return 60;
		},
		[&](std::string method, std::string req
			, std::map<std::string, std::string>& headers)
		{
			got_announce = true;
			return sim::send_response(200, "OK", 11) + "d5:peers0:e";
		}
		, [](torrent_handle h) {}
		, [](torrent_handle h) {}
		, announce_path);
	return got_announce;
}

TORRENT_TEST(ssrf_localhost)
{
	TEST_CHECK(test_ssrf("/announce", true, "http://localhost:8080/announce"));
	TEST_CHECK(!test_ssrf("/unusual-announce-path", true, "http://localhost:8080/unusual-announce-path"));
	TEST_CHECK(test_ssrf("/unusual-announce-path", false, "http://localhost:8080/unusual-announce-path"));

	TEST_CHECK(!test_ssrf("/short", true, "http://localhost:8080/short"));
	TEST_CHECK(test_ssrf("/short", false, "http://localhost:8080/short"));
}

TORRENT_TEST(ssrf_IPv4)
{
	TEST_CHECK(test_ssrf("/announce", true, "http://127.0.0.1:8080/announce"));
	TEST_CHECK(!test_ssrf("/unusual-announce-path", true, "http://127.0.0.1:8080/unusual-announce-path"));
	TEST_CHECK(test_ssrf("/unusual-announce-path", false, "http://127.0.0.1:8080/unusual-announce-path"));
}

TORRENT_TEST(ssrf_IPv6)
{
	TEST_CHECK(test_ssrf("/announce", true, "http://[::1]:8080/announce"));
	TEST_CHECK(!test_ssrf("/unusual-announce-path", true, "http://[::1]:8080/unusual-announce-path"));
	TEST_CHECK(test_ssrf("/unusual-announce-path", false, "http://[::1]:8080/unusual-announce-path"));
}

TORRENT_TEST(ssrf_query_string)
{
	// tracker URLs that come pre-baked with query string arguments will be
	// rejected when SSRF-mitigation is enabled
	TEST_CHECK(!test_ssrf("/announce", true, "http://tracker.com:8080/announce?info_hash=abc"));
	TEST_CHECK(!test_ssrf("/announce", true, "http://tracker.com:8080/announce?iNfo_HaSh=abc"));
	TEST_CHECK(!test_ssrf("/announce", true, "http://tracker.com:8080/announce?event=abc"));
	TEST_CHECK(!test_ssrf("/announce", true, "http://tracker.com:8080/announce?EvEnT=abc"));

	TEST_CHECK(test_ssrf("/announce", false, "http://tracker.com:8080/announce?info_hash=abc"));
	TEST_CHECK(test_ssrf("/announce", false, "http://tracker.com:8080/announce?iNfo_HaSh=abc"));
	TEST_CHECK(test_ssrf("/announce", false, "http://tracker.com:8080/announce?event=abc"));
}

bool test_idna(char const* tracker_url, char const* redirect
	, bool const feature_on)
{
	bool got_announce = false;
	tracker_test(
		[&](lt::add_torrent_params& p, lt::session& ses)
		{
			settings_pack pack;
			pack.set_bool(settings_pack::allow_idna, feature_on);
			ses.apply_settings(pack);
			p.trackers.emplace_back(tracker_url);
			return 60;
		},
		[&](std::string method, std::string req
			, std::map<std::string, std::string>& headers)
		{
			got_announce = true;
			return sim::send_response(200, "OK", 11) + "d5:peers0:e";
		}
		, [](torrent_handle h) {}
		, [](torrent_handle h) {}
		, "/announce"
		, redirect ? redirect : ""
		);
	return got_announce;
}

TORRENT_TEST(tracker_idna)
{
	TEST_EQUAL(test_idna("http://tracker.com:8080/announce", nullptr, true), true);
	TEST_EQUAL(test_idna("http://tracker.com:8080/announce", nullptr, false), true);

	TEST_EQUAL(test_idna("http://xn--tracker-.com:8080/announce", nullptr, true), true);
	TEST_EQUAL(test_idna("http://xn--tracker-.com:8080/announce", nullptr, false), false);
}

TORRENT_TEST(tracker_idna_redirect)
{
	TEST_EQUAL(test_idna("http://redirector.com:8080/announce", "http://xn--tracker-.com:8080/announce", true), true);
	TEST_EQUAL(test_idna("http://redirector.com:8080/announce", "http://xn--tracker-.com:8080/announce", false), false);
}

// This test sets up two peers, one seed an one downloader. The downloader has
// two trackers, both in tier 0. The behavior we expect is that it picks one of
// the trackers at random and announces to it. Since both trackers are working,
// it should not announce to the tracker it did not initially pick.

struct tracker_ent
{
	std::string url;
	int tier;
};

void test_tracker_tiers(lt::settings_pack pack,
	std::vector<address> local_addresses,
	std::vector<tracker_ent> trackers,
	std::function<void(int (&)[7])> test,
	boost::optional<std::function<void(int (&)[7])>> test2 = boost::none,
	lt::torrent_flags_t extra_flags = {})
{
	using namespace libtorrent;

	pack.set_int(settings_pack::alert_mask, alert_category::error
		| alert_category::status
		| alert_category::torrent_log);

	// setup the simulation
	struct sim_config : sim::default_config
	{
		chrono::high_resolution_clock::duration hostname_lookup(
			asio::ip::address const& requestor
			, std::string hostname
			, std::vector<asio::ip::address>& result
			, boost::system::error_code& ec)
		{
			if (hostname == "ipv6-only-tracker.com")
			{
				result.push_back(addr("f8e0::1"));
			}
			else if (hostname == "ipv4-only-tracker.com")
			{
				result.push_back(addr("3.0.0.1"));
			}
			else if (hostname == "dual-tracker.com")
			{
				result.push_back(addr("f8e0::2"));
				result.push_back(addr("3.0.0.2"));
			}
			else return default_config::hostname_lookup(requestor, hostname, result, ec);

			return lt::duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		}
	};

	sim_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0 { sim, local_addresses};

	sim::asio::io_context tracker1(sim, addr("3.0.0.1"));
	sim::asio::io_context tracker2(sim, addr("3.0.0.2"));
	sim::asio::io_context tracker3(sim, addr("3.0.0.3"));
	sim::asio::io_context tracker4(sim, addr("3.0.0.4"));
	sim::asio::io_context tracker5(sim, addr("f8e0::1"));
	sim::asio::io_context tracker6(sim, addr("f8e0::2"));
	sim::asio::io_context tracker7(sim, addr("3.0.0.5"));
	sim::http_server http1(tracker1, 8080);
	sim::http_server http2(tracker2, 8080);
	sim::http_server http3(tracker3, 8080);
	sim::http_server http4(tracker4, 8080);
	sim::http_server http5(tracker5, 8080);
	sim::http_server http6(tracker6, 8080);
	sim::http_server http7(tracker7, 8080);

	int received_announce[7] = {0, 0, 0, 0, 0, 0, 0};

	auto const return_no_peers = [&](std::string method, std::string req
		, std::map<std::string, std::string>&, int const tracker_index)
	{
		++received_announce[tracker_index];
		std::string const ret = "d8:intervali1800e5:peers0:e";
		return sim::send_response(200, "OK", static_cast<int>(ret.size())) + ret;
	};

	auto const return_404 = [&](std::string method, std::string req
		, std::map<std::string, std::string>&, int const tracker_index)
	{
		++received_announce[tracker_index];
		return sim::send_response(404, "Not Found", 0);
	};

	using namespace std::placeholders;
	http1.register_handler("/announce", std::bind(return_no_peers, _1, _2, _3, 0));
	http2.register_handler("/announce", std::bind(return_no_peers, _1, _2, _3, 1));
	http3.register_handler("/announce", std::bind(return_no_peers, _1, _2, _3, 2));
	http4.register_handler("/announce", std::bind(return_no_peers, _1, _2, _3, 3));
	http5.register_handler("/announce", std::bind(return_no_peers, _1, _2, _3, 4));
	http6.register_handler("/announce", std::bind(return_no_peers, _1, _2, _3, 5));
	http7.register_handler("/announce", std::bind(return_404, _1, _2, _3, 6));

	lt::session_proxy zombie;

	// create session
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:6881,[::]:6881");
	auto ses = std::make_shared<lt::session>(pack, ios0);

	print_alerts(*ses);

	lt::add_torrent_params params = ::create_torrent(1);
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	params.flags |= extra_flags;

	for (auto const& t : trackers)
	{
		params.trackers.push_back("http://" + t.url + ":8080/announce");
		params.tracker_tiers.push_back(t.tier);
	}

	params.save_path = save_path(0);
	ses->async_add_torrent(params);

	sim::timer t(sim, lt::seconds(30), [&](boost::system::error_code const&)
	{
		test(received_announce);
		if (test2)
		{
			std::memset(&received_announce, 0, sizeof(received_announce));
		}
		else
		{
			zombie = ses->abort();
			ses.reset();
		}
	});

	if (test2)
	{
		sim::timer t2(sim, lt::minutes(31), [&](boost::system::error_code const&)
		{
			(*test2)(received_announce);

			zombie = ses->abort();
			ses.reset();
		});
		sim.run();
	}
	else
	{
		sim.run();
	}

}

bool one_of(int a, int b)
{
	return (a == 2 && b == 0) || (a == 0 && b == 2);
}

// the torrent is a hybrid v1 and v2 torrent, so there is one announce per
// info-hash
TORRENT_TEST(tracker_tiers_multi_homed)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, false);
	pack.set_bool(settings_pack::announce_to_all_trackers, false);
	test_tracker_tiers(pack, { addr("50.0.0.1"), addr("f8e0::10") }
		, { {"3.0.0.1", 0}, {"3.0.0.2", 0}, {"3.0.0.3", 1}, {"3.0.0.4", 1}}
		, [](int (&a)[7]) {
		TEST_CHECK(one_of(a[0], a[1]));
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
		TEST_EQUAL(a[6], 0);
	});
}

TORRENT_TEST(tracker_tiers_all_trackers_multi_homed)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, false);
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	test_tracker_tiers(pack, { addr("50.0.0.1"), addr("f8e0::10") }
		, { {"3.0.0.1", 0}, {"3.0.0.2", 0}, {"3.0.0.3", 1}, {"3.0.0.4", 1}}
		, [](int (&a)[7]) {
		TEST_EQUAL(a[0], 2);
		TEST_EQUAL(a[1], 2);
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
		TEST_EQUAL(a[6], 0);
	});
}

TORRENT_TEST(tracker_tiers_all_tiers_multi_homed)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, true);
	pack.set_bool(settings_pack::announce_to_all_trackers, false);
	test_tracker_tiers(pack, { addr("50.0.0.1"), addr("f8e0::10") }
		, { {"3.0.0.1", 0}, {"3.0.0.2", 0}, {"3.0.0.3", 1}, {"3.0.0.4", 1}}
		, [](int (&a)[7]) {
		TEST_CHECK(one_of(a[0], a[1]));
		TEST_CHECK(one_of(a[2], a[3]));
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
		TEST_EQUAL(a[6], 0);
	});
}
TORRENT_TEST(tracker_tiers_all_trackers_and_tiers_multi_homed)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, true);
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	test_tracker_tiers(pack, { addr("50.0.0.1"), addr("f8e0::10") }
		, { {"3.0.0.1", 0}, {"3.0.0.2", 0}, {"3.0.0.3", 1}, {"3.0.0.4", 1}}
		, [](int (&a)[7]) {
		TEST_EQUAL(a[0], 2);
		TEST_EQUAL(a[1], 2);
		TEST_EQUAL(a[2], 2);
		TEST_EQUAL(a[3], 2);
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
	});
}

TORRENT_TEST(tracker_tiers)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, false);
	pack.set_bool(settings_pack::announce_to_all_trackers, false);
	test_tracker_tiers(pack, { addr("50.0.0.1") }
		, { {"3.0.0.1", 0}, {"3.0.0.2", 0}, {"3.0.0.3", 1}, {"3.0.0.4", 1}}
		, [](int (&a)[7]) {
		TEST_CHECK(one_of(a[0], a[1]));
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
	});
}

TORRENT_TEST(tracker_tiers_all_trackers)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, false);
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	test_tracker_tiers(pack, { addr("50.0.0.1") }
		, { {"3.0.0.1", 0}, {"3.0.0.2", 0}, {"3.0.0.3", 1}, {"3.0.0.4", 1}}
		, [](int (&a)[7]) {
		TEST_EQUAL(a[0], 2);
		TEST_EQUAL(a[1], 2);
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
	});
}

TORRENT_TEST(tracker_tiers_all_tiers)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, true);
	pack.set_bool(settings_pack::announce_to_all_trackers, false);
	test_tracker_tiers(pack, { addr("50.0.0.1") }
		, { {"3.0.0.1", 0}, {"3.0.0.2", 0}, {"3.0.0.3", 1}, {"3.0.0.4", 1}}
		, [](int (&a)[7]) {
		TEST_CHECK(one_of(a[0], a[1]));
		TEST_CHECK(one_of(a[2], a[3]));
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
	});
}

TORRENT_TEST(tracker_tiers_all_trackers_and_tiers)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, true);
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	test_tracker_tiers(pack, { addr("50.0.0.1") }
		, { {"3.0.0.1", 0}, {"3.0.0.2", 0}, {"3.0.0.3", 1}, {"3.0.0.4", 1}}
		, [](int (&a)[7]) {
		TEST_EQUAL(a[0], 2);
		TEST_EQUAL(a[1], 2);
		TEST_EQUAL(a[2], 2);
		TEST_EQUAL(a[3], 2);
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
	});
}

// in this case, we only have an IPv4 address, and the first tracker resolves
// only to an IPv6 address. Make sure we move on to the next one in the tier
TORRENT_TEST(tracker_tiers_unreachable_tracker)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, false);
	pack.set_bool(settings_pack::announce_to_all_trackers, false);
	test_tracker_tiers(pack, { addr("50.0.0.1") }
		, { {"f8e0::1", 0}, {"3.0.0.2", 0}, {"3.0.0.3", 1}, {"3.0.0.4", 1}}
		, [](int (&a)[7]) {
		TEST_EQUAL(a[0], 0);
		TEST_EQUAL(a[1], 2);
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
	});
}

// in this test, we have both v6 and v4 connectivity, and we have two trackers
// One is v6 only and one is dual. Since the first tracker was announced to
// using IPv6, the second tracker will *only* be used for IPv4, and not to
// announce IPv6 to again.
TORRENT_TEST(tracker_tiers_v4_and_v6_same_tier)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, false);
	pack.set_bool(settings_pack::announce_to_all_trackers, false);
	test_tracker_tiers(pack, { addr("50.0.0.1"), addr("f8e0::10") }
		, { {"ipv6-only-tracker.com", 0}, {"dual-tracker.com", 0}}
		, [](int (&a)[7]) {
		TEST_EQUAL(a[0], 0);
		TEST_EQUAL(a[1], 2);
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 2);
		TEST_EQUAL(a[5], 0);
	});
}

TORRENT_TEST(tracker_tiers_v4_and_v6_different_tiers)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, false);
	pack.set_bool(settings_pack::announce_to_all_trackers, false);
	test_tracker_tiers(pack, { addr("50.0.0.1"), addr("f8e0::10") }
		, { {"ipv6-only-tracker.com", 0}, {"dual-tracker.com", 1}}
		, [](int (&a)[7]) {
		TEST_EQUAL(a[0], 0);
		TEST_EQUAL(a[1], 2);
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 2);
		TEST_EQUAL(a[5], 0);
	});
}

// in the same scenario as above, if we announce to all trackers, we expect to
// continue to visit all trackers in the tier, and announce to that additional
// IPv6 address as well
TORRENT_TEST(tracker_tiers_v4_and_v6_all_trackers)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, false);
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	test_tracker_tiers(pack, { addr("50.0.0.1"), addr("f8e0::10") }
		, { {"ipv6-only-tracker.com", 0}, {"dual-tracker.com", 0}}
		, [](int (&a)[7]) {
		TEST_EQUAL(a[0], 0);
		TEST_EQUAL(a[1], 2);
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 2);
		TEST_EQUAL(a[5], 2);
	});
}

TORRENT_TEST(tracker_tiers_v4_and_v6_different_tiers_all_trackers)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, false);
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	test_tracker_tiers(pack, { addr("50.0.0.1"), addr("f8e0::10") }
		, { {"ipv6-only-tracker.com", 0}, {"dual-tracker.com", 1}}
		, [](int (&a)[7]) {
		TEST_EQUAL(a[0], 0);
		TEST_EQUAL(a[1], 2);
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 2);
		TEST_EQUAL(a[5], 0);
	});
}

TORRENT_TEST(tracker_tiers_v4_and_v6_different_tiers_all_tiers)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, true);
	pack.set_bool(settings_pack::announce_to_all_trackers, false);
	test_tracker_tiers(pack, { addr("50.0.0.1"), addr("f8e0::10") }
		, { {"ipv6-only-tracker.com", 0}, {"dual-tracker.com", 1}}
		, [](int (&a)[7]) {
		TEST_EQUAL(a[0], 0);
		TEST_EQUAL(a[1], 2);
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 2);
		TEST_EQUAL(a[5], 2);
	});
}

TORRENT_TEST(tracker_tiers_retry_all)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, true);
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	// the torrent is a hybrid torrent, so it will announce twice, once for v1
	// and once for v2
	test_tracker_tiers(pack, { addr("50.0.0.1") }
		, { {"3.0.0.1", 0}, {"3.0.0.5", 1}}
		, [](int (&a)[7]) {
		TEST_EQUAL(a[0], 2);
		TEST_EQUAL(a[1], 0);
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
		// the failing tracker is retried 17 seconds later
		TEST_EQUAL(a[6], 4);
	}, std::function<void (int (&)[7])>([](int (&a)[7]) {
		// this is 31 minutes later
		// the working tracker is re-announced once, since interval is 1800
		TEST_EQUAL(a[0], 2);
		TEST_EQUAL(a[1], 0);
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
		// The failing tracker is retried after:
		// 72 seconds
		// 189 seconds
		// 396 seconds
		// 711 seconds
		// 1166 seconds
		// 1783 seconds
		// 6 * 2 = 12
		TEST_EQUAL(a[6], 12);
	}));
}

TORRENT_TEST(tracker_tiers_retry_all_multiple_trackers_per_tier)
{
	settings_pack pack = settings();
	pack.set_bool(settings_pack::announce_to_all_tiers, true);
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	// the torrent is a hybrid torrent, so it will announce twice, once for v1
	// and once for v2
	test_tracker_tiers(pack, { addr("50.0.0.1") }
		, { {"3.0.0.1", 0}, {"3.0.0.5", 1}, {"3.0.0.2", 1}}
		, [](int (&a)[7]) {
		TEST_EQUAL(a[0], 2);
		TEST_EQUAL(a[1], 2);
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
		// the failing tracker is retried 17 seconds later
		TEST_EQUAL(a[6], 4);
	}, std::function<void (int (&)[7])>([](int (&a)[7]) {
		// this is 31 minutes later
		// the working tracker is re-announced once, since interval is 1800
		TEST_EQUAL(a[0], 2);
		TEST_EQUAL(a[1], 2);
		TEST_EQUAL(a[2], 0);
		TEST_EQUAL(a[3], 0);
		TEST_EQUAL(a[4], 0);
		TEST_EQUAL(a[5], 0);
		// The failing tracker is retried after:
		// 72 seconds
		// 189 seconds
		// 396 seconds
		// 711 seconds
		// 1166 seconds
		// 1783 seconds
		// 6 * 2 = 12
		TEST_EQUAL(a[6], 12);
	}));
}

// TODO: test external IP
// TODO: test with different queuing settings
// TODO: test when a torrent transitions from downloading to finished and
// finished to seeding
// TODO: test that left, downloaded and uploaded are reported correctly

// TODO: test scrape

#if TORRENT_USE_I2P

// these tests exercise the interaction between announce_with_tracker() and
// update_tracker_timer() when a torrent is flagged as i2p but its tracker
// list mixes i2p and non-i2p URLs. announce_with_tracker() skips non-i2p
// trackers when allow_i2p_mixed is false; update_tracker_timer() must skip
// them too. If it doesn't, the regular tracker's default next_announce
// (time_point32::min()) snaps the timer to "now", the timer fires, the
// callback re-skips, re-arms at "now", and the simulator (or a real
// session) loops at 100% CPU. These tests would hang if that spin
// regressed.
//
// Note: the simulator can't resolve "*.i2p" hostnames, so the i2p tracker
// URLs in these tests deliberately fail DNS. That mirrors the original bug
// report's environment (i2p mode disabled / not configured) while still
// flowing through every announce_with_tracker / update_tracker_timer
// path.

struct i2p_tracker_case
{
	// identifies the case in the test output
	char const* name;
	bool announce_to_all_tiers;
	bool announce_to_all_trackers;
	bool allow_i2p_mixed;
	// whether the torrent is flagged as i2p (is_i2p() == true)
	bool i2p_torrent;
	std::vector<tracker_ent> trackers;
	// when true, exactly one of server slots 0 and 1 is announced to (it
	// gets 2 announces, the other 0) — used for the "pick one tracker in
	// the tier" cases. slots 2..6 must still match `expected`.
	// when false, every slot must match `expected` exactly.
	bool pick_one_of_01;
	int expected[7];
};

// the simulator can't resolve "*.i2p" hostnames, so i2p tracker URLs here
// deliberately fail DNS. That mirrors the original bug report's environment
// (i2p mode disabled / not configured) while still flowing through every
// announce_with_tracker() / update_tracker_timer() path.
i2p_tracker_case const i2p_tracker_cases[] = {
	// mixed i2p + regular in the same tier, allow_i2p_mixed=false: the
	// regular trackers must not be contacted and the sim must not spin.
	{"mixed_same_tier",
		false,
		false,
		false,
		true,
		{{"tracker.i2p", 0}, {"3.0.0.1", 0}, {"3.0.0.2", 0}},
		false,
		{0, 0, 0, 0, 0, 0, 0}},

	// same, but allow_i2p_mixed=true: one of the two regular trackers is
	// picked (hybrid torrent => 2 announces, one per info-hash).
	{"mixed_same_tier_allow_mixed",
		false,
		false,
		true,
		true,
		{{"tracker.i2p", 0}, {"3.0.0.1", 0}, {"3.0.0.2", 0}},
		true,
		{0, 0, 0, 0, 0, 0, 0}},

	// i2p in tier 0, regular in tier 1: tier 1 must stay at 0 even though
	// tier 0 only has an unreachable i2p tracker.
	{"i2p_tier0_regular_tier1",
		false,
		false,
		false,
		true,
		{{"tracker.i2p", 0}, {"3.0.0.1", 1}, {"3.0.0.2", 1}},
		false,
		{0, 0, 0, 0, 0, 0, 0}},

	// inverse: regular in tier 0, i2p in tier 1. The regular trackers are
	// still policy-skipped, and tier 0 being "non-working" must not make
	// update_tracker_timer spin on the skipped tier-0 entries.
	{"regular_tier0_i2p_tier1",
		false,
		false,
		false,
		true,
		{{"3.0.0.1", 0}, {"3.0.0.2", 0}, {"tracker.i2p", 1}},
		false,
		{0, 0, 0, 0, 0, 0, 0}},

	// i2p torrent with only regular trackers: every tracker is policy-
	// blocked so the announce list is effectively empty; the timer must
	// use the "nothing eligible" fallback rather than spinning.
	{"i2p_torrent_only_regular",
		false,
		false,
		false,
		true,
		{{"3.0.0.1", 0}, {"3.0.0.2", 0}, {"3.0.0.3", 1}},
		false,
		{0, 0, 0, 0, 0, 0, 0}},

	// non-i2p torrent (no flag) that happens to list an i2p tracker URL.
	// is_i2p() is false so the policy doesn't apply; regular trackers are
	// announced normally and the i2p one just fails DNS.
	{"non_i2p_torrent_with_i2p_url",
		false,
		false,
		false,
		false,
		{{"tracker.i2p", 0}, {"3.0.0.1", 0}, {"3.0.0.2", 0}},
		true,
		{0, 0, 0, 0, 0, 0, 0}},

	// only i2p trackers: the all-i2p case must also not spin or contact a
	// regular tracker.
	{"i2p_torrent_only_i2p",
		false,
		false,
		false,
		true,
		{{"tracker1.i2p", 0}, {"tracker2.i2p", 1}},
		false,
		{0, 0, 0, 0, 0, 0, 0}},
};

TORRENT_TEST(i2p_tracker_combinations)
{
	for (auto const& tc : i2p_tracker_cases)
	{
		std::printf("\n=== i2p tracker case: %s ===\n", tc.name);
		settings_pack pack = settings();
		pack.set_bool(settings_pack::announce_to_all_tiers, tc.announce_to_all_tiers);
		pack.set_bool(settings_pack::announce_to_all_trackers, tc.announce_to_all_trackers);
		pack.set_bool(settings_pack::allow_i2p_mixed, tc.allow_i2p_mixed);
		auto const flags = tc.i2p_torrent ? lt::torrent_flags::i2p_torrent : lt::torrent_flags_t{};
		test_tracker_tiers(
			pack,
			{addr("50.0.0.1")},
			tc.trackers,
			[&tc](int(&a)[7]) {
				int first = 0;
				if (tc.pick_one_of_01)
				{
					TEST_CHECK(one_of(a[0], a[1]));
					first = 2;
				}
				for (int i = first; i < 7; ++i)
					TEST_EQUAL(a[i], tc.expected[i]);
			},
			boost::none,
			flags);
	}
}

// Regression test for the shared tracker_request being reused across the
// tracker loop in announce_with_tracker(). The tracker_request::i2p bit
// used to only ever get OR-ed in, so with allow_i2p_mixed=true an i2p
// tracker that precedes a regular one would leave the bit set on the
// shared request. That made the regular tracker's compact (6-byte) peer
// list get parsed as 32-byte i2p destinations, mangling the peer count.
// Here the i2p tracker is in tier 0 (so it's processed first and fails
// DNS) and the regular tracker is in tier 1; with announce_to_all_tiers
// the regular tracker is contacted and must report all of its peers.
TORRENT_TEST(tracker_i2p_kind_not_leaked_to_regular_tracker)
{
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context tracker_ios(sim, addr("3.0.0.1"));
	sim::http_server http(tracker_ios, 8080);

	// 6 compact IPv4 peers => 36 bytes. If this response is misparsed as
	// i2p destinations (32 bytes each) it yields only 1 "peer".
	int const num_compact_peers = 6;

	int max_reported_peers = -1;
	int regular_announces = 0;

	http.register_handler("/announce",
		[&](std::string /* method */,
			std::string /* req */
			,
			std::map<std::string, std::string>&) {
			++regular_announces;
			std::string peers;
			for (int i = 0; i < num_compact_peers; ++i)
			{
				peers += char(10);
				peers += char(0);
				peers += char(0);
				peers += char(i + 1); // 10.0.0.(i+1)
				peers += char(0x1a);
				peers += char(0xe1); // port 6881
			}
			std::string const body =
				"d8:intervali1800e5:peers" + std::to_string(peers.size()) + ":" + peers + "e";
			return sim::send_response(200, "OK", int(body.size())) + body;
		});

	lt::session_proxy zombie;
	asio::io_context ios(sim, {addr("50.0.0.1")});
	settings_pack pack = settings();
	pack.set_bool(settings_pack::allow_i2p_mixed, true);
	pack.set_bool(settings_pack::announce_to_all_tiers, true);
	pack.set_bool(settings_pack::announce_to_all_trackers, false);
	pack.set_str(settings_pack::listen_interfaces, "50.0.0.1:6881");

	auto ses = std::make_shared<lt::session>(pack, ios);

	ses->set_alert_notify([&] {
		post(ios, [&] {
			std::vector<lt::alert*> alerts;
			ses->pop_alerts(&alerts);
			for (lt::alert* a : alerts)
			{
				if (auto* tr = alert_cast<tracker_reply_alert>(a))
					max_reported_peers = std::max(max_reported_peers, tr->num_peers);
			}
		});
	});

	lt::add_torrent_params params = ::create_torrent(1);
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	params.flags |= lt::torrent_flags::i2p_torrent;
	params.trackers.push_back("http://tracker.i2p:8080/announce");
	params.tracker_tiers.push_back(0);
	params.trackers.push_back("http://3.0.0.1:8080/announce");
	params.tracker_tiers.push_back(1);
	params.save_path = save_path(0);
	ses->async_add_torrent(params);

	sim::timer t(sim, lt::seconds(30), [&](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});
	sim.run();

	TEST_CHECK(regular_announces > 0);
	TEST_EQUAL(max_reported_peers, num_compact_peers);
}

#endif // TORRENT_USE_I2P
