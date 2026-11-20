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

// a minimal, single-connection WebSocket tracker for multiplexing and
// proxy tests, driven by the simulator's deterministic event loop.
// Only handles the protocol shape of websocket_tracker_connection::do_send().
struct sim_ws_tracker
{
	using sim_tcp = sim::asio::ip::tcp;

	sim_ws_tracker(sim::asio::io_context& ios,
		int port,
		int announces_before_reply = 3,
		bool keep_alive = false);

	std::vector<sim::asio::ip::address> const& connections() const { return m_connections; }
	int announces() const { return int(m_received.size()); }
	bool closed() const { return m_closed; }

private:
	static std::string extract_info_hash(std::string const& msg);

	// waits for m_announces_before_reply announces, then answers the first
	// and closes the connection, or answers the latest and keeps reading
	// if m_keep_alive is set.
	void read_next();

	sim_tcp::acceptor m_acceptor;
	boost::beast::websocket::stream<sim_tcp::socket> m_ws;
	boost::beast::flat_buffer m_buffer;
	std::vector<std::string> m_received;
	std::string m_response;
	std::vector<sim::asio::ip::address> m_connections;
	int const m_announces_before_reply;
	bool const m_keep_alive;
	bool m_closed = false;
};

#endif // TORRENT_USE_RTC

#endif // TORRENT_SIMULATION_WS_TRACKER_SERVER_HPP_INCLUDED
