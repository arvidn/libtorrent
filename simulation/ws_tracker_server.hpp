/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SIMULATION_WS_TRACKER_SERVER_HPP_INCLUDED
#define TORRENT_SIMULATION_WS_TRACKER_SERVER_HPP_INCLUDED

#include "libtorrent/config.hpp"

#if TORRENT_USE_RTC

#include "simulator/simulator.hpp"
// pulls in the beast teardown()/async_teardown()/beast_close_socket()
// customization point overloads for sim::asio::ip::tcp::socket (needed by
// websocket::stream<sim_tcp::socket> below; without them beast's generic
// teardown template static_asserts on an "unknown socket type").
#include "libtorrent/aux_/websocket_stream.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <string>
#include <vector>

// a minimal, single-connection WebSocket "tracker" used to drive
// websocket_tracker_connection through a multiplexing scenario that can't
// otherwise be triggered deterministically: several torrents' announces in
// flight on one shared connection at once. Only handles the exact protocol
// shape websocket_tracker_connection::do_send() produces; it is not a
// general-purpose WebSocket server. Driven entirely by the simulator's
// deterministic event loop rather than a background thread.
struct sim_ws_tracker
{
	using sim_tcp = sim::asio::ip::tcp;

	sim_ws_tracker(sim::asio::io_context& ios, int port);

private:
	static std::string extract_info_hash(std::string const& msg);

	// reads up to 3 announces, answering only the first one received, then
	// tears the connection down without ever answering the other two --
	// this exercises both the on_read() success path (for the one we
	// answer, once a later request has become the connection's "current"
	// one) and the close() error path (for the two left hanging).
	void read_next();

	sim_tcp::acceptor m_acceptor;
	boost::beast::websocket::stream<sim_tcp::socket> m_ws;
	boost::beast::flat_buffer m_buffer;
	std::vector<std::string> m_received;
	std::string m_response;
};

#endif // TORRENT_USE_RTC

#endif // TORRENT_SIMULATION_WS_TRACKER_SERVER_HPP_INCLUDED
