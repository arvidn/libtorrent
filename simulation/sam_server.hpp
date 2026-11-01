/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef SAM_SERVER_HPP_INCLUDED
#define SAM_SERVER_HPP_INCLUDED

#include "simulator/simulator.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/sha1_hash.hpp"

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#if TORRENT_USE_I2P

namespace sim {

struct sam_connection;

struct sam_session
{
	std::string destination;
	lt::sha256_hash dest_hash;
	// "<base32 of dest_hash>.b32.i2p"
	std::string b32_name;
	asio::ip::address client;
	std::deque<std::weak_ptr<sam_connection>> acceptors;
	std::deque<std::weak_ptr<sam_connection>> connectors;
	bool accepted = false;
};

struct sam_state
{
	std::map<std::string, sam_session, std::less<>> sessions;
	std::function<void(asio::ip::address const&)> on_session;

	sam_session* find_session(std::string_view id);
	sam_session* find_destination(std::string_view b64);
	sam_session* find_b32(std::string_view name);
};

struct sam_connection : std::enable_shared_from_this<sam_connection>
{
	sam_connection(asio::io_context& ios, std::shared_ptr<sam_state> state);

	asio::ip::tcp::socket& socket() { return m_socket; }

	void start();
	bool is_open() const { return m_conn_state != state_t::closed; }

	// called on the STREAM CONNECT side
	void pair_with(std::shared_ptr<sam_connection> acceptor);

private:
	enum class state_t
	{
		hello,
		command,
		waiting,
		relay,
		// the other end of the relay closed, flush what it sent us, then close
		draining,
		closed
	};

	void read();
	void on_read(boost::system::error_code const& ec, std::size_t bytes_transferred);
	void handle_line(std::string_view line);
	void send(std::string buf);
	void do_write();
	void close(char const* reason);
	void close_after_send(char const* reason);
	void log(char const* fmt, ...) const TORRENT_FORMAT(2, 3);

	void session_create(std::string_view id);
	void naming_lookup(std::string_view name);
	void stream_connect(std::string_view id, std::string_view dest);
	void stream_accept(std::string_view id);
	// sends bytes received after STREAM CONNECT/ACCEPT, but before pairing,
	// to the peer
	void forward_residual();

	asio::ip::tcp::socket m_socket;
	std::shared_ptr<sam_state> m_state;

	state_t m_conn_state = state_t::hello;

	// set on the control connection, whose closing ends the SAM session
	std::string m_session_id;

	std::string m_src_dest;

	std::shared_ptr<sam_connection> m_peer;

	std::array<char, 16384> m_recv_buffer{};
	std::string m_line_buffer;

	std::deque<std::string> m_send_queue;
	bool m_writing = false;
	int const m_id;
};

// a minimal SAMv3 bridge implementing just the subset of the protocol
// libtorrent's i2p_stream and i2p_connection use. Streams between sessions
// on the same bridge are relayed directly, there is no network beyond it.
struct sam_server
{
	sam_server(asio::io_context& ios, unsigned short listen_port);

	void stop();

	std::optional<lt::sha256_hash> destination(asio::ip::address const& client) const;

	// called with the client's address when its session issues its first
	// STREAM ACCEPT. libtorrent does that once its SAM connection is fully
	// open, so from then on it can both accept and initiate streams
	void on_session(std::function<void(asio::ip::address const&)> f)
	{
		m_state->on_session = std::move(f);
	}

private:
	void on_accept(boost::system::error_code const& ec);

	asio::io_context& m_ios;
	asio::ip::tcp::acceptor m_listen_socket;
	std::shared_ptr<sam_state> m_state;
	std::shared_ptr<sam_connection> m_conn;
	asio::ip::tcp::endpoint m_ep;
};

}

#endif // TORRENT_USE_I2P

#endif
