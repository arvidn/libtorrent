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

#if TORRENT_USE_RTC
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#include <thread>
#endif

using namespace lt;
using namespace lt::aux;
using namespace std::placeholders;
using namespace std::chrono_literals;

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

namespace {

	using boost::asio::ip::tcp;
	namespace websocket = boost::beast::websocket;

	// a minimal, single-connection WebSocket "tracker" used to drive
	// websocket_tracker_connection through a multiplexing scenario that can't
	// otherwise be triggered deterministically: several torrents' announces in
	// flight on one shared connection at once. Only handles the exact protocol
	// shape websocket_tracker_connection::do_send() produces, and only ASCII
	// info_hashes (so the wire encoding needs no unescaping); it is not a
	// general-purpose WebSocket or JSON implementation.
	struct fake_ws_tracker
	{
		fake_ws_tracker()
			: m_ws(m_ios)
		{
			error_code ec;
			m_acceptor.open(tcp::v4(), ec);
			m_acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
			m_acceptor.bind(tcp::endpoint(boost::asio::ip::address_v4::any(), 0), ec);
			m_port = m_acceptor.local_endpoint(ec).port();
			m_acceptor.listen(1, ec);
			m_thread = std::thread(&fake_ws_tracker::thread_fun, this);
		}

		~fake_ws_tracker()
		{
			error_code ignore;
			m_acceptor.cancel(ignore);
			m_acceptor.close(ignore);
			m_ws.next_layer().close(ignore);
			if (m_thread.joinable()) m_thread.join();
		}

		int port() const { return m_port; }

	private:
		void run_until(bool const& done)
		{
			while (!done)
			{
				m_ios.run_for(50ms);
				m_ios.restart();
			}
		}

		static std::string extract_info_hash(std::string const& msg)
		{
			std::string const key = "\"info_hash\":\"";
			auto const pos = msg.find(key);
			if (pos == std::string::npos) return {};
			auto const start = pos + key.size();
			auto const end = msg.find('"', start);
			if (end == std::string::npos) return {};
			return msg.substr(start, end - start);
		}

		void thread_fun()
		{
			error_code ec;
			bool done = false;
			m_acceptor.async_accept(m_ws.next_layer(), [&](error_code const& e) {
				ec = e;
				done = true;
			});
			run_until(done);
			if (ec) return;

			done = false;
			m_ws.async_accept([&](error_code const& e) {
				ec = e;
				done = true;
			});
			run_until(done);
			if (ec) return;

			// read up to 3 announces, answering only the first one received,
			// then tear the connection down without ever answering the other
			// two -- this exercises both the on_read() success path (for the
			// one we answer, once a later request has become the connection's
			// "current" one) and the close() error path (for the two left
			// hanging).
			std::vector<std::string> received;
			for (int i = 0; i < 3 && !ec; ++i)
			{
				boost::beast::flat_buffer buffer;
				done = false;
				m_ws.async_read(buffer, [&](error_code const& e, std::size_t) {
					ec = e;
					done = true;
				});
				run_until(done);
				if (ec) break;

				auto const text = boost::beast::buffers_to_string(buffer.data());
				received.push_back(extract_info_hash(text));
			}

			if (!received.empty() && !ec)
			{
				std::string const out = "{\"info_hash\":\"" + received.front()
					+ "\",\"interval\":120,\"min_interval\":60}";
				done = false;
				m_ws.async_write(
					boost::asio::buffer(out), [&](error_code const&, std::size_t) { done = true; });
				run_until(done);
			}

			error_code ignore;
			m_ws.next_layer().close(ignore);
		}

		io_context m_ios;
		tcp::acceptor m_acceptor{m_ios};
		websocket::stream<tcp::socket> m_ws;
		int m_port = 0;
		std::thread m_thread;
	};

	struct recording_callback : ws_request_callback
	{
		std::vector<tracker_request> responses;
		std::vector<tracker_request> errors;

		void tracker_response(tracker_request const& r,
			address const&,
			std::list<address> const&,
			struct tracker_response const&) override
		{
			responses.push_back(r);
		}
		void tracker_request_error(tracker_request const& r,
			error_code const&,
			operation_t,
			std::string const&,
			seconds32) override
		{
			errors.push_back(r);
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
	fake_ws_tracker tracker;

	io_context ios;
	aux::session_settings sett;
	tracker_manager_handler h{ios, sett};

	std::string const url = "ws://127.0.0.1:" + std::to_string(tracker.port()) + "/announce";

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
		h.m_tracker_manager.queue_request(ios, std::move(r), sett, cb);
	};

	// queued (and sent, in this order) as a, b, c, all onto the same
	// connection: by the time all 3 are on the wire, the connection's
	// "current" request (tracker_req()) is c's.
	queue(hash_a, cb_a);
	queue(hash_b, cb_b);
	queue(hash_c, cb_c);

	// drive the client side until the tracker has answered a and torn the
	// connection down without answering b or c, or a generous deadline
	// elapses (in which case the TEST_CHECKs below will fail rather than
	// hang forever).
	auto const deadline = std::chrono::steady_clock::now() + 10s;
	while (std::chrono::steady_clock::now() < deadline
		&& ((cb_a->responses.empty() && cb_a->errors.empty()) || cb_b->errors.empty()
			|| cb_c->errors.empty()))
	{
		ios.run_for(50ms);
		ios.restart();
	}

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
	if (!cb_a->responses.empty()) TEST_CHECK(cb_a->responses[0].info_hash == hash_a);
	if (!cb_b->errors.empty()) TEST_CHECK(cb_b->errors[0].info_hash == hash_b);
	if (!cb_c->errors.empty()) TEST_CHECK(cb_c->errors[0].info_hash == hash_c);
}

#endif // TORRENT_USE_RTC
