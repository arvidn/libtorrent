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

#else

TORRENT_TEST(websocket_multiplexed_requests_get_correct_tracker_request) {}
TORRENT_TEST(websocket_partial_abort_closes_connection_synchronously) {}

#endif // TORRENT_USE_RTC
