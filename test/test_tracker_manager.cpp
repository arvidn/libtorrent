/*

Copyright (c) 2020-2021, Alden Torres
Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <functional>

#include "test.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/resolver.hpp"
#if TORRENT_USE_RTC
#include "libtorrent/aux_/websocket_stream.hpp"
#endif

using namespace lt;
using namespace lt::aux;
using namespace std::placeholders;

struct tracker_manager_handler : aux::session_logger {

	tracker_manager_handler(io_context& ios, aux::session_settings& sett)
		: m_host_resolver(ios)
		, m_tracker_manager(
			std::bind(&tracker_manager_handler::send_fn, this, _1, _2, _3, _4, _5)
			, std::bind(&tracker_manager_handler::send_fn_hostname, this, _1, _2, _3, _4, _5, _6)
			, m_stats_counters
			, m_host_resolver
			, sett
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
			, *this
#endif
			)
	{}

	virtual ~tracker_manager_handler() {}

	void send_fn(aux::listen_socket_handle const&
		, udp::endpoint const&
		, span<char const>
		, error_code&
		, aux::udp_send_flags_t const)
	{}

	void send_fn_hostname(aux::listen_socket_handle const&
		, char const*
		, int
		, span<char const>
		, error_code&
		, aux::udp_send_flags_t const)
	{}

#ifndef TORRENT_DISABLE_LOGGING
	bool should_log() const override { return false; }
	void session_log(char const*, ...) const override TORRENT_FORMAT(2,3) {}
#endif

#if TORRENT_USE_ASSERTS
	bool is_single_thread() const override { return false; }
	bool has_peer(aux::peer_connection const*) const override { return false; }
	bool any_torrent_has_peer(aux::peer_connection const*) const override { return false; }
	bool is_posting_torrent_updates() const override { return false; }
#endif

	io_context m_io_context;
	counters m_stats_counters;
	aux::resolver m_host_resolver;
	tracker_manager m_tracker_manager;
};

struct ws_request_callback : request_callback
{
	ws_request_callback() {}
	virtual ~ws_request_callback() override {}
	void tracker_warning(tracker_request const&
		, std::string const&) override {}
	void tracker_scrape_response(tracker_request const&
		, int , int , int
		, int ) override {}
	void tracker_response(
		tracker_request const&
		, address const&
		, std::list<address> const&
		, struct tracker_response const&) override {}
	void tracker_request_error(tracker_request const&,
		error_code const& ec,
		operation_t,
		const std::string&,
		seconds32) override
	{
		last_error = ec;
	}
#if TORRENT_USE_RTC
	void generate_rtc_offers(int
		, std::function<void(error_code const&, std::vector<aux::rtc_offer>)> handler) override
	{
		++num_offer_requests;
		if (defer_offers)
			pending_offers = std::move(handler);
		else
			handler(offer_error, {});
	}
	void on_rtc_offer(aux::rtc_offer const&) override {}
	void on_rtc_answer(aux::rtc_answer const&) override {}
#endif
#ifndef TORRENT_DISABLE_LOGGING
	bool should_log() const override { return false; }
	void debug_log(const char*, ...) const noexcept override TORRENT_FORMAT(2,3) {}
#endif
	error_code last_error;
	error_code offer_error;
	bool defer_offers = false;
#if TORRENT_USE_RTC
	std::function<void(error_code const&, std::vector<aux::rtc_offer>)> pending_offers;
#endif
	int num_offer_requests = 0;
};

TORRENT_TEST(tracker_protocol_supported)
{
	TEST_CHECK(is_tracker_protocol_supported("http://tracker.com/announce"));
	TEST_CHECK(is_tracker_protocol_supported("udp://tracker.com:80/announce"));
	TEST_CHECK(!is_tracker_protocol_supported("foo://tracker.com/announce"));
	TEST_CHECK(!is_tracker_protocol_supported("tracker.com/announce"));

#if TORRENT_USE_SSL
	TEST_CHECK(is_tracker_protocol_supported("https://tracker.com/announce"));
#else
	TEST_CHECK(!is_tracker_protocol_supported("https://tracker.com/announce"));
#endif

#if TORRENT_USE_RTC
	TEST_CHECK(is_tracker_protocol_supported("ws://tracker.com/announce"));
	TEST_CHECK(is_tracker_protocol_supported("wss://tracker.com/announce"));
#else
	TEST_CHECK(!is_tracker_protocol_supported("ws://tracker.com/announce"));
	TEST_CHECK(!is_tracker_protocol_supported("wss://tracker.com/announce"));
#endif
}

TORRENT_TEST(empty_and_num_requests)
{
	io_context ios;
	aux::session_settings sett;

	// for http
	{
		tracker_manager_handler h{ios, sett};
		TEST_CHECK(h.m_tracker_manager.empty());
		TEST_EQUAL(h.m_tracker_manager.num_requests(), 0);

		tracker_request r;
		r.url = "http://tracker.com";
		h.m_tracker_manager.queue_request(ios, std::move(r), sett);
		TEST_CHECK(!h.m_tracker_manager.empty());
		TEST_EQUAL(h.m_tracker_manager.num_requests(), 1);
	}

	// for udp
	{
		tracker_manager_handler h{ios, sett};
		TEST_CHECK(h.m_tracker_manager.empty());
		TEST_EQUAL(h.m_tracker_manager.num_requests(), 0);

		tracker_request r;
		r.url = "udp://:A/"; // fail the parse, still add the request
		h.m_tracker_manager.queue_request(ios, std::move(r), sett);
		TEST_CHECK(!h.m_tracker_manager.empty());
		TEST_EQUAL(h.m_tracker_manager.num_requests(), 1);
	}

#if TORRENT_USE_RTC
	// for ws
	{
		tracker_manager_handler h{ios, sett};
		TEST_CHECK(h.m_tracker_manager.empty());
		TEST_EQUAL(h.m_tracker_manager.num_requests(), 0);

		tracker_request r;
		r.url = "ws://tracker.com";
		r.num_want = 1;
		auto cb = std::make_shared<ws_request_callback>();
		h.m_tracker_manager.queue_request(ios, std::move(r), sett, cb);
		TEST_CHECK(!h.m_tracker_manager.empty());
		TEST_EQUAL(h.m_tracker_manager.num_requests(), 1);
	}
#endif

	// for http+udp+ws
	{
		tracker_manager_handler h{ios, sett};

		tracker_request r1;
		r1.url = "http://tracker.com";
		h.m_tracker_manager.queue_request(ios, std::move(r1), sett);
		TEST_EQUAL(h.m_tracker_manager.num_requests(), 1);

		tracker_request r2;
		r2.url = "udp://:A/"; // fail the parse, still add the request
		h.m_tracker_manager.queue_request(ios, std::move(r2), sett);
		TEST_EQUAL(h.m_tracker_manager.num_requests(), 2);

#if TORRENT_USE_RTC
		tracker_request r3;
		r3.url = "ws://tracker.com";
		r3.num_want = 1;
		auto cb = std::make_shared<ws_request_callback>();
		h.m_tracker_manager.queue_request(ios, std::move(r3), sett, cb);
		TEST_EQUAL(h.m_tracker_manager.num_requests(), 3);
#endif
	}
}

#if TORRENT_USE_RTC
TORRENT_TEST(is_websocket_tracker_url)
{
	TEST_CHECK(is_websocket_tracker_url("ws://tracker.com/announce"));
	TEST_CHECK(is_websocket_tracker_url("wss://tracker.com/announce"));
	TEST_CHECK(!is_websocket_tracker_url("http://tracker.com/announce"));
	TEST_CHECK(!is_websocket_tracker_url("https://tracker.com/announce"));
	TEST_CHECK(!is_websocket_tracker_url("udp://tracker.com:80/announce"));
	TEST_CHECK(!is_websocket_tracker_url("wsx://tracker.com/announce"));
	TEST_CHECK(!is_websocket_tracker_url(""));
}

TORRENT_TEST(webrtc_allowed)
{
	aux::session_settings sett;
	TEST_CHECK(webrtc_allowed(sett));

	sett.set_int(settings_pack::proxy_type, settings_pack::socks5);
	TEST_CHECK(!webrtc_allowed(sett));

	sett.set_int(settings_pack::proxy_type, settings_pack::http);
	TEST_CHECK(!webrtc_allowed(sett));

	// Tracker-only proxying is permitted; WebRTC peers remain direct.
	sett.set_bool(settings_pack::proxy_peer_connections, false);
	TEST_CHECK(webrtc_allowed(sett));

	sett.set_bool(settings_pack::proxy_tracker_connections, false);
	TEST_CHECK(webrtc_allowed(sett));

	sett.set_bool(settings_pack::proxy_peer_connections, true);
	TEST_CHECK(!webrtc_allowed(sett));

	sett.set_int(settings_pack::proxy_type, settings_pack::none);
	sett.set_bool(settings_pack::proxy_tracker_connections, true);
	TEST_CHECK(webrtc_allowed(sett));
}

TORRENT_TEST(websocket_tracker_refused_with_proxy)
{
	io_context ios;
	aux::session_settings sett;
	sett.set_int(settings_pack::proxy_type, settings_pack::socks5);
	sett.set_str(settings_pack::proxy_hostname, "127.0.0.1");
	sett.set_int(settings_pack::proxy_port, 1);

	tracker_manager_handler h{ios, sett};

	tracker_request r;
	r.url = "ws://tracker.com";
	r.num_want = 1;
	auto cb = std::make_shared<ws_request_callback>();
	h.m_tracker_manager.queue_request(ios, std::move(r), sett, cb);
	ios.poll();

	TEST_CHECK(h.m_tracker_manager.empty());
	TEST_EQUAL(cb->num_offer_requests, 0);
	TEST_EQUAL(
		cb->last_error, error_code(boost::system::errc::permission_denied, generic_category()));
}

// e.g. an i2p torrent with allow_i2p_mixed disabled
TORRENT_TEST(websocket_tracker_refused_rtc_offers)
{
	io_context ios;
	aux::session_settings sett;
	tracker_manager_handler h{ios, sett};

	tracker_request r;
	r.url = "ws://tracker.com";
	r.num_want = 1;
	auto cb = std::make_shared<ws_request_callback>();
	cb->offer_error = boost::asio::error::operation_not_supported;
	h.m_tracker_manager.queue_request(ios, std::move(r), sett, cb);
	ios.poll();

	TEST_CHECK(h.m_tracker_manager.empty());
	TEST_EQUAL(cb->num_offer_requests, 1);
	TEST_EQUAL(cb->last_error, error_code(boost::asio::error::operation_not_supported));
}

TORRENT_TEST(websocket_tracker_proxy_update_closes_connections)
{
	io_context ios;
	aux::session_settings sett;
	tracker_manager_handler h{ios, sett};

	auto cb1 = std::make_shared<ws_request_callback>();
	auto cb2 = std::make_shared<ws_request_callback>();
	auto cb3 = std::make_shared<ws_request_callback>();

	tracker_request r;
	r.url = "ws://127.0.0.1:1/announce";
	r.num_want = 1;
	h.m_tracker_manager.queue_request(ios, tracker_request(r), sett, cb1);
	h.m_tracker_manager.queue_request(ios, tracker_request(r), sett, cb2);
	r.url = "ws://127.0.0.1:2/announce";
	h.m_tracker_manager.queue_request(ios, std::move(r), sett, cb3);
	TEST_EQUAL(h.m_tracker_manager.num_requests(), 2);

	h.m_tracker_manager.close_websocket_connections();
	TEST_CHECK(h.m_tracker_manager.empty());
	TEST_EQUAL(cb1->last_error, error_code(errors::announce_skipped));
	TEST_EQUAL(cb2->last_error, error_code(errors::announce_skipped));
	TEST_EQUAL(cb3->last_error, error_code(errors::announce_skipped));

	ios.poll();
	TEST_CHECK(h.m_tracker_manager.empty());
}

TORRENT_TEST(websocket_tracker_proxy_enabled_during_offers)
{
	io_context ios;
	aux::session_settings sett;
	tracker_manager_handler h{ios, sett};

	tracker_request r;
	r.url = "ws://tracker.com";
	r.num_want = 1;
	auto cb = std::make_shared<ws_request_callback>();
	cb->defer_offers = true;
	h.m_tracker_manager.queue_request(ios, std::move(r), sett, cb);
	TEST_EQUAL(cb->num_offer_requests, 1);
	TEST_CHECK(cb->pending_offers);

	sett.set_int(settings_pack::proxy_type, settings_pack::socks5);
	sett.set_str(settings_pack::proxy_hostname, "127.0.0.1");
	sett.set_int(settings_pack::proxy_port, 1);
	cb->pending_offers(error_code(), {});
	ios.poll();

	TEST_CHECK(h.m_tracker_manager.empty());
	TEST_EQUAL(
		cb->last_error, error_code(boost::system::errc::permission_denied, generic_category()));
}

#if TORRENT_USE_I2P
namespace {
struct recording_resolver final : aux::resolver_interface
{
	void async_resolve(std::string const& host, aux::resolver_flags, callback_t) override
	{
		hosts.push_back(host);
	}
	void abort() override {}
	void set_cache_timeout(seconds) override {}
	std::vector<std::string> hosts;
};
}

TORRENT_TEST(websocket_stream_i2p_not_resolved)
{
	io_context ios;
	recording_resolver resolver;
	auto ws = std::make_shared<aux::websocket_stream>(ios, resolver, nullptr);

	error_code result;
	bool called = false;
	ws->async_connect("ws://tracker.i2p/announce", [&](error_code const& ec) {
		called = true;
		result = ec;
	});
	ios.poll();

	TEST_CHECK(called);
	TEST_EQUAL(result, error_code(errors::unsupported_url_protocol));
	TEST_CHECK(resolver.hosts.empty());
}
#endif
#endif
