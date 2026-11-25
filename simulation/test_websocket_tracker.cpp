/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "test.hpp"

#if TORRENT_USE_RTC

#include "simulator/simulator.hpp"
#include "ws_tracker_server.hpp"
#include "socks_server.hpp"
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/beast/http.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#include <array>
#include <string_view>
#include "libtorrent/flags.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/resolver.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"

using namespace lt;
using namespace lt::aux;
using namespace sim;
using namespace std::placeholders;
using namespace std::chrono_literals;

namespace {

	struct tracker_manager_handler : lt::aux::session_logger
	{
		tracker_manager_handler(lt::io_context& ios, lt::aux::session_settings& sett)
			: m_host_resolver(ios)
			, m_tracker_manager(
				  std::bind(&tracker_manager_handler::send_fn, this, _1, _2, _3, _4, _5),
				  std::bind(
					  &tracker_manager_handler::send_fn_hostname, this, _1, _2, _3, _4, _5, _6),
				  m_stats_counters,
				  m_host_resolver,
				  sett
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
				  ,
				  *this
#endif
			  )
		{}

		virtual ~tracker_manager_handler() {}

		void send_fn(lt::aux::listen_socket_handle const&,
			udp::endpoint const&,
			span<char const>,
			error_code&,
			lt::aux::udp_send_flags_t const)
		{}

		void send_fn_hostname(lt::aux::listen_socket_handle const&,
			char const*,
			int,
			span<char const>,
			error_code&,
			lt::aux::udp_send_flags_t const)
		{}

#ifndef TORRENT_DISABLE_LOGGING
		bool should_log() const override { return false; }
		void session_log(char const*, ...) const override TORRENT_FORMAT(2, 3) {}
#endif

#if TORRENT_USE_ASSERTS
		bool is_single_thread() const override { return false; }
		bool has_peer(lt::aux::peer_connection const*) const override { return false; }
		bool any_torrent_has_peer(lt::aux::peer_connection const*) const override { return false; }
		bool is_posting_torrent_updates() const override { return false; }
#endif

		counters m_stats_counters;
		lt::aux::resolver m_host_resolver;
		tracker_manager m_tracker_manager;
	};

	struct ws_request_callback : request_callback
	{
		ws_request_callback() {}
		virtual ~ws_request_callback() override {}
		void tracker_warning(tracker_request const&, std::string const&) override {}
		void tracker_scrape_response(tracker_request const&, int, int, int, int) override {}
		void tracker_response(tracker_request const&,
			address const&,
			std::list<address> const&,
			struct tracker_response const&) override
		{}
		void tracker_request_error(tracker_request const&,
			error_code const&,
			operation_t,
			const std::string&,
			seconds32) override
		{}
		void generate_rtc_offers(
			int, std::function<void(error_code const&, std::vector<lt::aux::rtc_offer>)> handler)
			override
		{
			handler(error_code(), {});
		}
		void on_rtc_offer(lt::aux::rtc_offer const&) override {}
		void on_rtc_answer(lt::aux::rtc_answer const&) override {}
#ifndef TORRENT_DISABLE_LOGGING
		bool should_log() const override { return false; }
		void debug_log(const char*, ...) const noexcept override TORRENT_FORMAT(2, 3) {}
#endif
	};

	struct recording_callback : ws_request_callback
	{
		std::vector<tracker_request> responses;
		std::vector<tracker_request> errors;
		std::vector<error_code> error_codes;
		std::vector<operation_t> error_ops;

		void tracker_response(tracker_request const& r,
			address const&,
			std::list<address> const&,
			struct tracker_response const&) override
		{
			responses.push_back(r);
		}
		void tracker_request_error(tracker_request const& r,
			error_code const& ec,
			operation_t op,
			std::string const&,
			seconds32) override
		{
			errors.push_back(r);
			error_codes.push_back(ec);
			error_ops.push_back(op);
		}
	};

} // anonymous namespace

// websocket_tracker_connection multiplexes several torrents' announces onto
// one connection when they share a tracker URL. Reporting a torrent's
// outcome requires that torrent's own tracker_request: torrent::
// tracker_response() and torrent::tracker_request_error() key their
// bookkeeping off its info_hash/event, not the connection's single "most
// recently sent" request (tracker_req()), which generally belongs to a
// different torrent by the time an earlier one's outcome is reported. This
// drives 3 torrents' announces over one shared connection and checks each
// callback receives its own info_hash, both on the success path (on_read())
// and the close()-triggered error path.
TORRENT_TEST(websocket_multiplexed_requests_get_correct_tracker_request)
{
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context server_ios(sim, make_address_v4("10.0.0.2"));
	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));

	int const port = 8080;
	sim_ws_tracker tracker(server_ios, port);

	lt::aux::session_settings sett;
	tracker_manager_handler h{client_ios, sett};

	std::string const url = "ws://10.0.0.2:" + std::to_string(port) + "/announce";

	sha1_hash const hash_a("aaaaaaaaaaaaaaaaaaaa");
	sha1_hash const hash_b("bbbbbbbbbbbbbbbbbbbb");
	sha1_hash const hash_c("cccccccccccccccccccc");

	auto cb_a = std::make_shared<recording_callback>();
	auto cb_b = std::make_shared<recording_callback>();
	auto cb_c = std::make_shared<recording_callback>();

	auto queue = [&](sha1_hash const& ih, std::shared_ptr<recording_callback> const& cb) {
		tracker_request r;
		r.url = url;
		r.info_hash = ih;
		r.event = event_t::started;
		r.num_want = 1;
		h.m_tracker_manager.queue_request(client_ios, std::move(r), sett, cb);
	};

	// queued (and sent, in this order) as a, b, c, all onto the same
	// connection: by the time all 3 are on the wire, the connection's
	// "current" request (tracker_req()) is c's.
	queue(hash_a, cb_a);
	queue(hash_b, cb_b);
	queue(hash_c, cb_c);

	// safety net against an unexpected retry/reconnect keeping the simulation
	// from ever converging (mirrors the "generous deadline" the real-socket
	// version relied on); under normal operation this never fires because
	// all 3 requests resolve (success or error) once sim_ws_tracker tears the
	// connection down.
	lt::aux::deadline_timer safety_timer(client_ios);
	safety_timer.expires_after(10s);
	safety_timer.async_wait([&](error_code const& ec) {
		if (ec)
			return;
		h.m_tracker_manager.abort_all_requests(true);
	});

	sim.run();

	// a's outcome arrives while the connection's "current" request
	// (tracker_req()) is c's, so this only passes if a's callback_entry
	// reports its own stored request.
	TEST_EQUAL(cb_a->responses.size(), 1);
	TEST_CHECK(cb_a->errors.empty());

	// b and c's outcomes were reported via close()'s m_callbacks sweep
	// after the connection was torn down without ever answering them.
	TEST_EQUAL(cb_b->errors.size(), 1);
	TEST_EQUAL(cb_c->errors.size(), 1);

	// each callback must see its own info_hash, not whichever request
	// happened to be "current" on the shared connection when its outcome
	// was reported.
	if (!cb_a->responses.empty())
		TEST_CHECK(cb_a->responses[0].info_hash == hash_a);
	if (!cb_b->errors.empty())
		TEST_CHECK(cb_b->errors[0].info_hash == hash_b);
	if (!cb_c->errors.empty())
		TEST_CHECK(cb_c->errors[0].info_hash == hash_c);
}

// abort_all_requests(false) (a partial abort, e.g. session::pause(), as
// opposed to full shutdown/the tracker_manager destructor) fails a
// websocket connection with no stopped-event request in flight via fail()
// immediately followed by close(ec, op) -- see the comment at that call
// site in tracker_manager.cpp. fail() alone does not report anything or
// tear the connection down; skipping the close(ec, op) call would leave
// the connection registered in m_websocket_conns (and its socket open)
// until tracker_connection::fail_impl() eventually runs its deferred,
// argument-less close(), instead of synchronously as part of
// abort_all_requests() returning. This checks the connection is
// unregistered synchronously, before the io_context ever runs.
TORRENT_TEST(websocket_partial_abort_closes_connection_synchronously)
{
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));

	lt::aux::session_settings sett;
	tracker_manager_handler h{client_ios, sett};

	std::string const url = "ws://10.0.0.2:8080/announce";
	auto cb = std::make_shared<recording_callback>();

	tracker_request r;
	r.url = url;
	r.info_hash = sha1_hash("aaaaaaaaaaaaaaaaaaaa");
	r.event = event_t::started;
	r.num_want = 1;
	h.m_tracker_manager.queue_request(client_ios, std::move(r), sett, cb);

	// the connection was created and registered synchronously; nothing has
	// actually run on the (simulated) network yet.
	TEST_CHECK(!h.m_tracker_manager.empty());

	// no stopped-event request is in flight on this connection, so it must
	// be failed and fully torn down.
	h.m_tracker_manager.abort_all_requests(false);

	// prune_non_stopped_requests() reports the specific reason for the
	// still-outstanding request on its way in, before fail()/close() are
	// even reached.
	TEST_EQUAL(cb->errors.size(), 1);
	if (!cb->errors.empty())
	{
		TEST_CHECK(cb->error_codes[0] == errors::announce_skipped);
		TEST_CHECK(cb->error_ops[0] == operation_t::bittorrent);
	}

	// the connection itself must be unregistered synchronously, as part of
	// abort_all_requests() returning -- not deferred until a later
	// io_context turn.
	TEST_CHECK(h.m_tracker_manager.empty());

	// let the already-cancelled connect attempt's completion handler run,
	// so nothing is left outstanding at teardown.
	sim.run();
}


namespace {

using proxy_flags_t = lt::flags::bitfield_flag<std::uint8_t, struct websocket_proxy_test_tag>;
constexpr proxy_flags_t proxy_disabled = 0_bit;
constexpr proxy_flags_t proxy_unavailable = 1_bit;
constexpr proxy_flags_t named_tracker = 2_bit;
constexpr proxy_flags_t proxy_hostnames = 3_bit;
constexpr proxy_flags_t send_host_in_connect = 4_bit;

struct proxy_network : sim::default_config
{
	chrono::high_resolution_clock::duration hostname_lookup(sim::asio::ip::address const& requestor,
		std::string hostname,
		std::vector<sim::asio::ip::address>& result,
		error_code& ec) override
	{
		if (hostname == "tracker.hostname.org")
		{
			if (requestor == make_address_v4("10.0.0.1"))
				++client_lookups;
			if (requestor == make_address_v4("50.50.50.50"))
				++proxy_lookups;
			if (retry_first_endpoint && requestor == make_address_v4("10.0.0.1"))
				result.push_back(make_address_v4("10.0.0.3"));
			result.push_back(make_address_v4("10.0.0.2"));
			ec.clear();
			return 100ms;
		}
		return default_config::hostname_lookup(requestor, hostname, result, ec);
	}

	int client_lookups = 0;
	int proxy_lookups = 0;
	bool retry_first_endpoint = false;
};

// Forward bytes unchanged through an HTTP CONNECT tunnel.
struct connect_proxy
{
	using sim_tcp = sim::asio::ip::tcp;

	explicit connect_proxy(sim::asio::io_context& ios, std::uint16_t const port)
		: m_acceptor(ios)
		, m_client(ios)
		, m_server(ios)
		, m_resolver(ios)
	{
		m_acceptor.open(sim_tcp::v4());
		m_acceptor.bind(sim_tcp::endpoint(sim::asio::ip::address_v4::any(), port));
		m_acceptor.listen(1);
		accept();
	}

	void stop()
	{
		m_stopping = true;
		error_code ec;
		m_acceptor.close(ec);
		m_resolver.cancel();
		m_client.close(ec);
		m_server.close(ec);
	}

	bool reject_first = false;
	int replies = 0;
	bool client_closed = false;
	std::vector<std::string> targets;
	std::size_t client_bytes = 0;
	std::function<void()> on_connect_request;
	std::string host;

private:
	void accept()
	{
		m_acceptor.async_accept(m_client, [this](error_code const& ec) {
			if (ec)
				return;
			boost::beast::http::async_read(
				m_client, m_buffer, m_request, [this](error_code const& read_ec, std::size_t) {
					if (read_ec)
						return stop();
					on_request();
				});
		});
	}

	void on_request()
	{
		std::string const target(m_request.target());
		targets.push_back(target);
		host = std::string(m_request[boost::beast::http::field::host]);
		TEST_CHECK(m_request.method() == boost::beast::http::verb::connect);
		if (on_connect_request)
			on_connect_request();
		if (reject_first && targets.size() == 1)
			return reply(true);
		auto const colon = target.rfind(':');
		if (colon == std::string::npos)
			return stop();
		std::string const hostname = target.substr(0, colon);
		std::string const port = target.substr(colon + 1);
		m_resolver.async_resolve(hostname,
			port.c_str(),
			[this](error_code const& ec, sim_tcp::resolver::results_type const& ips) {
				if (ec || ips.empty())
					return stop();
				m_server.open(ips.front().endpoint().protocol());
				m_server.async_connect(
					ips.front().endpoint(), [this](error_code const& connect_ec) {
						if (connect_ec)
							return stop();
						reply(false);
					});
			});
	}

	void reply(bool const reject)
	{
		std::string_view const response = reject
			? "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n"
			: "HTTP/1.1 200 Connection established\r\n\r\n";
		sim::asio::async_write(m_client,
			sim::asio::buffer(response.data(), response.size()),
			[this, reject](error_code const& ec, std::size_t) {
				if (ec)
					return stop();
				++replies;
				if (reject)
				{
					error_code ignore;
					m_client.close(ignore);
					m_request = decltype(m_request){};
					m_buffer.consume(m_buffer.size());
					accept();
					return;
				}
				forward(m_client, m_server, m_out);
				forward(m_server, m_client, m_in);
			});
	}

	void forward(sim_tcp::socket& from, sim_tcp::socket& to, std::array<char, 4096>& buf)
	{
		from.async_read_some(sim::asio::buffer(buf),
			[this, &from, &to, &buf](error_code const& ec, std::size_t const size) {
				if (ec)
				{
					if (&from == &m_client && !m_stopping)
						client_closed = true;
					return stop();
				}
				if (&from == &m_client)
					client_bytes += size;
				sim::asio::async_write(to,
					sim::asio::buffer(buf.data(), size),
					[this, &from, &to, &buf](error_code const& write_ec, std::size_t) {
						if (write_ec)
							return stop();
						forward(from, to, buf);
					});
			});
	}

	sim_tcp::acceptor m_acceptor;
	sim_tcp::socket m_client;
	sim_tcp::socket m_server;
	sim_tcp::resolver m_resolver;
	boost::beast::flat_buffer m_buffer;
	boost::beast::http::request<boost::beast::http::empty_body> m_request;
	std::array<char, 4096> m_out{};
	std::array<char, 4096> m_in{};
	bool m_stopping = false;
};

void configure_proxy(lt::aux::session_settings& sett,
	settings_pack::proxy_type_t const type,
	proxy_flags_t const flags = {})
{
	sett.set_int(settings_pack::proxy_type, type);
	sett.set_str(settings_pack::proxy_hostname, "50.50.50.50");
	sett.set_int(settings_pack::proxy_port, 4444);
	sett.set_bool(settings_pack::proxy_peer_connections, false);
	sett.set_bool(settings_pack::proxy_tracker_connections, !bool(flags & proxy_disabled));
	sett.set_bool(settings_pack::proxy_hostnames, bool(flags & proxy_hostnames));
	sett.set_bool(settings_pack::proxy_send_host_in_connect, bool(flags & send_host_in_connect));
}

void queue_announce(tracker_manager_handler& h,
	lt::io_context& ios,
	lt::aux::session_settings const& sett,
	std::string const& url,
	std::shared_ptr<recording_callback> const& cb,
	sha1_hash const& ih = sha1_hash("aaaaaaaaaaaaaaaaaaaa"))
{
	tracker_request r;
	r.url = url;
	r.info_hash = ih;
	r.event = event_t::started;
	r.num_want = 1;
	h.m_tracker_manager.queue_request(ios, std::move(r), sett, cb);
}

void test_proxy(settings_pack::proxy_type_t const type, proxy_flags_t const flags = {})
{
	bool const enabled = !bool(flags & proxy_disabled);
	bool const available = !bool(flags & proxy_unavailable);
	bool const named = bool(flags & named_tracker);
	bool const remote_dns = bool(flags & proxy_hostnames);
	bool const send_host = bool(flags & send_host_in_connect);

	proxy_network network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));
	sim::asio::io_context server_ios(sim, make_address_v4("10.0.0.2"));
	sim::asio::io_context proxy_ios(sim, make_address_v4("50.50.50.50"));
	sim_ws_tracker tracker(server_ios, 8080, 1);

	std::unique_ptr<sim::socks_server> socks;
	std::unique_ptr<connect_proxy> http;
	if (available)
	{
		if (type == settings_pack::http)
			http = std::make_unique<connect_proxy>(proxy_ios, 4444);
		else
			socks = std::make_unique<sim::socks_server>(
				proxy_ios, 4444, type == settings_pack::socks4 ? 4 : 5);
	}

	lt::aux::session_settings sett;
	configure_proxy(sett, type, flags);
	tracker_manager_handler h{client_ios, sett};
	auto const cb = std::make_shared<recording_callback>();
	std::string const url =
		std::string("ws://") + (named ? "tracker.hostname.org" : "10.0.0.2") + ":8080/announce";
	queue_announce(h, client_ios, sett, url, cb);

	bool const success = !enabled || available;
	lt::aux::deadline_timer safety_timer(client_ios);
	safety_timer.expires_after(10s);
	safety_timer.async_wait([&](error_code const& ec) {
		if (ec)
			return;
		TEST_EQUAL(cb->responses.size(), success ? 1u : 0u);
		TEST_EQUAL(cb->errors.size(), success ? 0u : 1u);
		h.m_tracker_manager.abort_all_requests(true);
		if (socks)
			socks->stop();
		if (http)
			http->stop();
	});
	sim.run();

	TEST_EQUAL(tracker.announces(), success ? 1 : 0);
	TEST_EQUAL(tracker.connections().size(), success ? 1u : 0u);
	if (!tracker.connections().empty())
		TEST_CHECK(
			tracker.connections().front() == make_address_v4(enabled ? "50.50.50.50" : "10.0.0.1"));
	if (socks)
		TEST_EQUAL(socks->cmd_counts()[0], enabled ? 1 : 0);
	if (http)
	{
		TEST_EQUAL(http->targets.size(), enabled ? 1u : 0u);
		if (enabled)
		{
			std::string const expected =
				named && (remote_dns || send_host) ? "tracker.hostname.org:8080" : "10.0.0.2:8080";
			if (!http->targets.empty())
				TEST_EQUAL(http->targets.front(), expected);
			TEST_EQUAL(http->host, expected);
		}
	}
	if (named)
	{
		TEST_EQUAL(network_cfg.client_lookups, enabled && remote_dns ? 0 : 1);
		TEST_EQUAL(
			network_cfg.proxy_lookups, enabled && available && (remote_dns || send_host) ? 1 : 0);
	}
}

} // anonymous namespace

TORRENT_TEST(websocket_tracker_proxy_disabled)
{
	test_proxy(settings_pack::socks5, proxy_disabled);
}

TORRENT_TEST(websocket_tracker_socks5_proxy_failure)
{
	test_proxy(settings_pack::socks5, proxy_unavailable);
}

TORRENT_TEST(websocket_tracker_http_proxy_failure)
{
	test_proxy(settings_pack::http, proxy_unavailable);
}

TORRENT_TEST(websocket_tracker_socks4_proxy) { test_proxy(settings_pack::socks4); }

TORRENT_TEST(websocket_tracker_socks5_proxy) { test_proxy(settings_pack::socks5); }

TORRENT_TEST(websocket_tracker_http_proxy) { test_proxy(settings_pack::http); }

TORRENT_TEST(websocket_tracker_http_proxy_hostnames)
{
	test_proxy(settings_pack::http, named_tracker | proxy_hostnames);
}

TORRENT_TEST(websocket_tracker_http_proxy_send_host_in_connect)
{
	test_proxy(settings_pack::http, named_tracker | send_host_in_connect);
}

TORRENT_TEST(websocket_tracker_proxy_change_reconnects)
{
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));
	sim::asio::io_context server_ios(sim, make_address_v4("10.0.0.2"));
	sim::asio::io_context proxy_ios(sim, make_address_v4("50.50.50.50"));
	sim_ws_tracker tracker(server_ios, 8080, 1, true);
	std::unique_ptr<sim_ws_tracker> next_tracker;
	sim::socks_server proxy(proxy_ios, 4444);

	lt::aux::session_settings sett;
	configure_proxy(sett, settings_pack::socks5, proxy_disabled);
	tracker_manager_handler h{client_ios, sett};
	auto const direct = std::make_shared<recording_callback>();
	auto const proxied = std::make_shared<recording_callback>();
	std::string const url = "ws://10.0.0.2:8080/announce";
	queue_announce(h, client_ios, sett, url, direct);

	lt::aux::deadline_timer change_timer(client_ios);
	change_timer.expires_after(2s);
	change_timer.async_wait([&](error_code const& ec) {
		if (ec)
			return;
		TEST_EQUAL(direct->responses.size(), 1);
		TEST_CHECK(direct->errors.empty());
		TEST_CHECK(!tracker.closed());
		sett.set_bool(settings_pack::proxy_tracker_connections, true);
		h.m_tracker_manager.close_websocket_connections();
		TEST_CHECK(h.m_tracker_manager.empty());
	});

	lt::aux::deadline_timer reconnect_timer(client_ios);
	reconnect_timer.expires_after(3s);
	reconnect_timer.async_wait([&](error_code const& ec) {
		if (ec)
			return;
		TEST_CHECK(tracker.closed());
		next_tracker = std::make_unique<sim_ws_tracker>(server_ios, 8080, 1);
		queue_announce(h, client_ios, sett, url, proxied);
	});

	lt::aux::deadline_timer safety_timer(client_ios);
	safety_timer.expires_after(10s);
	safety_timer.async_wait([&](error_code const& ec) {
		if (ec)
			return;
		TEST_CHECK(tracker.closed());
		TEST_EQUAL(proxy.cmd_counts()[0], 1);
		TEST_EQUAL(proxied->responses.size(), 1);
		TEST_CHECK(proxied->errors.empty());
		h.m_tracker_manager.abort_all_requests(true);
		proxy.stop();
	});
	sim.run();

	TEST_EQUAL(tracker.announces(), 1);
	TEST_EQUAL(tracker.connections().size(), 1);
	if (!tracker.connections().empty())
		TEST_CHECK(tracker.connections().front() == make_address_v4("10.0.0.1"));
	TEST_CHECK(bool(next_tracker));
	if (next_tracker)
	{
		TEST_EQUAL(next_tracker->announces(), 1);
		TEST_EQUAL(next_tracker->connections().size(), 1);
		if (!next_tracker->connections().empty())
			TEST_CHECK(next_tracker->connections().front() == make_address_v4("50.50.50.50"));
	}
}


TORRENT_TEST(websocket_tracker_socks5_proxy_hostnames)
{
	test_proxy(settings_pack::socks5, named_tracker | proxy_hostnames);
}

TORRENT_TEST(websocket_tracker_proxy_multiplexed_requests)
{
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));
	sim::asio::io_context server_ios(sim, make_address_v4("10.0.0.2"));
	sim::asio::io_context proxy_ios(sim, make_address_v4("50.50.50.50"));
	sim_ws_tracker tracker(server_ios, 8080);
	sim::socks_server proxy(proxy_ios, 4444);

	lt::aux::session_settings sett;
	configure_proxy(sett, settings_pack::socks5);
	tracker_manager_handler h{client_ios, sett};

	std::string const url = "ws://10.0.0.2:8080/announce";
	sha1_hash const hash_a("aaaaaaaaaaaaaaaaaaaa");
	sha1_hash const hash_b("bbbbbbbbbbbbbbbbbbbb");
	sha1_hash const hash_c("cccccccccccccccccccc");
	auto const cb_a = std::make_shared<recording_callback>();
	auto const cb_b = std::make_shared<recording_callback>();
	auto const cb_c = std::make_shared<recording_callback>();

	queue_announce(h, client_ios, sett, url, cb_a, hash_a);
	queue_announce(h, client_ios, sett, url, cb_b, hash_b);
	queue_announce(h, client_ios, sett, url, cb_c, hash_c);
	TEST_EQUAL(h.m_tracker_manager.num_requests(), 1);

	lt::aux::deadline_timer safety_timer(client_ios);
	safety_timer.expires_after(10s);
	safety_timer.async_wait([&](error_code const& ec) {
		if (ec)
			return;
		TEST_CHECK(h.m_tracker_manager.empty());
		h.m_tracker_manager.abort_all_requests(true);
		proxy.stop();
	});
	sim.run();

	TEST_EQUAL(proxy.cmd_counts()[0], 1);
	TEST_EQUAL(tracker.announces(), 3);
	TEST_EQUAL(tracker.connections().size(), 1);
	if (!tracker.connections().empty())
		TEST_CHECK(tracker.connections().front() == make_address_v4("50.50.50.50"));

	TEST_EQUAL(cb_a->responses.size(), 1);
	TEST_CHECK(cb_a->errors.empty());
	TEST_CHECK(cb_b->responses.empty());
	TEST_CHECK(cb_c->responses.empty());
	TEST_EQUAL(cb_b->errors.size(), 1);
	TEST_EQUAL(cb_c->errors.size(), 1);
	if (!cb_a->responses.empty())
		TEST_CHECK(cb_a->responses.front().info_hash == hash_a);
	if (!cb_b->errors.empty())
		TEST_CHECK(cb_b->errors.front().info_hash == hash_b);
	if (!cb_c->errors.empty())
		TEST_CHECK(cb_c->errors.front().info_hash == hash_c);
}

TORRENT_TEST(websocket_tracker_proxy_abort)
{
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));
	sim::asio::io_context server_ios(sim, make_address_v4("10.0.0.2"));
	sim::asio::io_context proxy_ios(sim, make_address_v4("50.50.50.50"));
	sim_ws_tracker tracker(server_ios, 8080, 1);
	connect_proxy proxy(proxy_ios, 4444);

	lt::aux::session_settings sett;
	configure_proxy(sett, settings_pack::http);
	tracker_manager_handler h{client_ios, sett};
	auto const cb = std::make_shared<recording_callback>();

	// Abort after CONNECT is received but before the proxy replies.
	proxy.on_connect_request = [&] {
		TEST_CHECK(!h.m_tracker_manager.empty());
		TEST_CHECK(cb->errors.empty());
		TEST_CHECK(cb->responses.empty());
		h.m_tracker_manager.abort_all_requests(false);
		TEST_CHECK(h.m_tracker_manager.empty());
	};
	queue_announce(h, client_ios, sett, "ws://10.0.0.2:8080/announce", cb);

	lt::aux::deadline_timer safety_timer(client_ios);
	safety_timer.expires_after(10s);
	safety_timer.async_wait([&](error_code const& ec) {
		if (ec)
			return;
		TEST_CHECK(h.m_tracker_manager.empty());
		TEST_EQUAL(proxy.replies, 1);
		TEST_CHECK(proxy.client_closed);
		h.m_tracker_manager.abort_all_requests(true);
		proxy.stop();
	});
	sim.run();

	TEST_EQUAL(proxy.targets.size(), 1);
	TEST_EQUAL(proxy.client_bytes, 0);
	TEST_EQUAL(tracker.announces(), 0);
	TEST_CHECK(cb->responses.empty());
	TEST_EQUAL(cb->errors.size(), 1);
	if (!cb->errors.empty())
	{
		TEST_CHECK(cb->error_codes.front() == errors::announce_skipped);
		TEST_CHECK(cb->error_ops.front() == operation_t::bittorrent);
	}
	TEST_CHECK(h.m_tracker_manager.empty());
}

namespace {

void test_proxy_endpoint_retry(settings_pack::proxy_type_t const type)
{
	proxy_network network_cfg;
	network_cfg.retry_first_endpoint = true;
	sim::simulation sim{network_cfg};
	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));
	sim::asio::io_context server_ios(sim, make_address_v4("10.0.0.2"));
	sim::asio::io_context proxy_ios(sim, make_address_v4("50.50.50.50"));
	sim_ws_tracker tracker(server_ios, 8080, 1);
	std::unique_ptr<sim::socks_server> socks;
	std::unique_ptr<connect_proxy> http;
	if (type == settings_pack::http)
	{
		http = std::make_unique<connect_proxy>(proxy_ios, 4444);
		http->reject_first = true;
	}
	else
		socks = std::make_unique<sim::socks_server>(proxy_ios, 4444);

	lt::aux::session_settings sett;
	configure_proxy(sett, type);
	tracker_manager_handler h{client_ios, sett};
	auto const cb = std::make_shared<recording_callback>();
	queue_announce(h, client_ios, sett, "ws://tracker.hostname.org:8080/announce", cb);

	lt::aux::deadline_timer safety_timer(client_ios);
	safety_timer.expires_after(10s);
	safety_timer.async_wait([&](error_code const& ec) {
		if (ec)
			return;
		TEST_EQUAL(cb->responses.size(), 1);
		TEST_CHECK(cb->errors.empty());
		h.m_tracker_manager.abort_all_requests(true);
		if (socks)
			socks->stop();
		if (http)
			http->stop();
	});
	sim.run();

	TEST_EQUAL(network_cfg.client_lookups, 1);
	TEST_EQUAL(network_cfg.proxy_lookups, 0);
	if (socks)
		TEST_EQUAL(socks->cmd_counts()[0], 2);
	if (http)
	{
		TEST_EQUAL(http->replies, 2);
		TEST_EQUAL(http->targets.size(), 2);
		if (http->targets.size() == 2)
		{
			TEST_EQUAL(http->targets[0], "10.0.0.3:8080");
			TEST_EQUAL(http->targets[1], "10.0.0.2:8080");
		}
	}
	TEST_EQUAL(tracker.announces(), 1);
	TEST_EQUAL(tracker.connections().size(), 1);
	if (!tracker.connections().empty())
		TEST_CHECK(tracker.connections().front() == make_address_v4("50.50.50.50"));
}

} // anonymous namespace

TORRENT_TEST(websocket_tracker_proxy_endpoint_retry)
{
	test_proxy_endpoint_retry(settings_pack::socks5);
	test_proxy_endpoint_retry(settings_pack::http);
}

#else

TORRENT_TEST(websocket_multiplexed_requests_get_correct_tracker_request) {}
TORRENT_TEST(websocket_partial_abort_closes_connection_synchronously) {}
TORRENT_TEST(websocket_tracker_proxy_disabled) {}
TORRENT_TEST(websocket_tracker_socks5_proxy_failure) {}
TORRENT_TEST(websocket_tracker_http_proxy_failure) {}
TORRENT_TEST(websocket_tracker_socks4_proxy) {}
TORRENT_TEST(websocket_tracker_socks5_proxy) {}
TORRENT_TEST(websocket_tracker_http_proxy) {}
TORRENT_TEST(websocket_tracker_http_proxy_hostnames) {}
TORRENT_TEST(websocket_tracker_http_proxy_send_host_in_connect) {}
TORRENT_TEST(websocket_tracker_proxy_change_reconnects) {}
TORRENT_TEST(websocket_tracker_socks5_proxy_hostnames) {}
TORRENT_TEST(websocket_tracker_proxy_multiplexed_requests) {}
TORRENT_TEST(websocket_tracker_proxy_abort) {}
TORRENT_TEST(websocket_tracker_proxy_endpoint_retry) {}

#endif // TORRENT_USE_RTC
