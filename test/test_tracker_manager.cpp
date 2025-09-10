/*

Copyright (c) 2020-2021, Alden Torres
Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/resolver.hpp"
#ifdef TORRENT_USE_LIBCURL
#include "libtorrent/aux_/curl_thread_manager.hpp"
#endif

using namespace lt;
using namespace lt::aux;
using namespace std::placeholders;

struct tracker_manager_handler : aux::session_logger {

	tracker_manager_handler(io_context& ios, aux::session_settings& sett)
		: m_host_resolver(ios)
#ifdef TORRENT_USE_LIBCURL
		, m_curl_thread_manager(aux::curl_thread_manager::create(ios, sett))
#endif
		, m_tracker_manager(
			std::bind(&tracker_manager_handler::send_fn, this, _1, _2, _3, _4, _5)
			, std::bind(&tracker_manager_handler::send_fn_hostname, this, _1, _2, _3, _4, _5, _6)
			, m_stats_counters
			, m_host_resolver
			, sett
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
			, *this
#endif
#ifdef TORRENT_USE_LIBCURL
			, m_curl_thread_manager
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
#ifdef TORRENT_USE_LIBCURL
	std::shared_ptr<aux::curl_thread_manager> m_curl_thread_manager;
#endif
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
	void tracker_request_error(
		tracker_request const&
		, error_code const&
		, operation_t
		, const std::string&
		, seconds32) override {}
#if TORRENT_USE_RTC
	void generate_rtc_offers(int
		, std::function<void(error_code const&, std::vector<aux::rtc_offer>)> handler) override
	{
		handler(error_code(), {});
	}
	void on_rtc_offer(aux::rtc_offer const&) override {}
	void on_rtc_answer(aux::rtc_answer const&) override {}
#endif
#ifndef TORRENT_DISABLE_LOGGING
	bool should_log() const override { return false; }
	void debug_log(const char*, ...) const noexcept override TORRENT_FORMAT(2,3) {}
#endif
};

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
		auto cb = std::make_shared<ws_request_callback>();
		h.m_tracker_manager.queue_request(ios, std::move(r3), sett, cb);
		TEST_EQUAL(h.m_tracker_manager.num_requests(), 3);
#endif
	}
}
