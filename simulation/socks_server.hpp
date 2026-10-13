/*

Copyright (c) 2015, Arvid Norberg
All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef SOCKS_SERVER_HPP_INCLUDED
#define SOCKS_SERVER_HPP_INCLUDED

#include "simulator/simulator.hpp"

#ifdef __GNUC__
#pragma GCC diagnostic push
#if __GNUC__ >= 9
#pragma GCC diagnostic ignored "-Wparentheses"
#endif
#endif

#include <boost/bimap.hpp>

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#ifdef _MSC_VER
#pragma warning(push)
// warning C4251: X: class Y needs to have dll-interface to be used by clients of struct
#pragma warning(disable:4251)
#endif

namespace sim {

enum socks_flag
{
	// when this flag is set, the proxy will close the client connection
	// immediately after sending the response to a UDP ASSOCIATE command
	disconnect_udp_associate = 1,

	// when this flag is set, the reponse to UDP ASSOCIATE will contain an empty
	// hostname, rather than the relay IP address
	udp_associate_respond_empty_hostname = 2
};

struct socks_connection : std::enable_shared_from_this<socks_connection>
{
	socks_connection(asio::io_context& ios,
		int version,
		std::array<int, 3>& cmd_counts,
		std::uint32_t flags,
		int& bind_port);

	asio::ip::tcp::socket& socket() { return m_client_connection; }

	void start();

private:
	void on_handshake1(boost::system::error_code const& ec, size_t bytes_transferred);
	void on_handshake2(boost::system::error_code const& ec, size_t bytes_transferred);
	void on_handshake3(boost::system::error_code const& ec, size_t bytes_transferred);
	void on_request1(boost::system::error_code const& ec, size_t bytes_transferred);
	void on_request2(boost::system::error_code const& ec, size_t bytes_transferred);

	void on_write(boost::system::error_code const& ec, size_t bytes_transferred, bool close);
	void close_connection();

	int format_response(asio::ip::address const& addr, int port, int response);
	int format_hostname_response(char const* hostname, int port, int response);

	void on_connected(boost::system::error_code const& ec);
	void on_request_domain_name(boost::system::error_code const& ec, size_t bytes_transferred);
	void on_request_domain_lookup(
		boost::system::error_code const& ec, const asio::ip::tcp::resolver::results_type ips);

	void open_forward_connection(asio::ip::tcp::endpoint const& target);
	void bind_connection(asio::ip::tcp::endpoint const& target);
	void start_accept(boost::system::error_code const& ec);

	void udp_associate(asio::ip::tcp::endpoint const& target);
	void on_read_udp(boost::system::error_code const& ec, std::size_t bytes_transferred);
	void wait_for_eof(boost::system::error_code const& ec, std::size_t bytes_transferred);

	void on_server_receive(boost::system::error_code const& ec, std::size_t bytes_transferred);
	void on_server_forward(boost::system::error_code const& ec, size_t bytes_transferred);

	void on_client_receive(boost::system::error_code const& ec, std::size_t bytes_transferred);
	void on_client_forward(boost::system::error_code const& ec, size_t bytes_transferred);

	char const* command() const;

	int& m_bind_port;

	asio::io_context& m_ios;

	asio::ip::udp::resolver m_udp_resolver;

	asio::ip::tcp::resolver m_resolver;

	boost::bimap<asio::ip::address, std::string> m_name_mapping;

	// this is the SOCKS client connection, i.e. the client connecting to us and
	// being forwarded
	asio::ip::tcp::socket m_client_connection;

	// this is the connection to the server the socks client is being forwarded
	// to
	asio::ip::tcp::socket m_server_connection;
	asio::ip::tcp::acceptor m_bind_socket;

	asio::ip::udp::socket m_udp_associate;
	asio::ip::udp::endpoint m_udp_associate_ep;
	asio::ip::udp::endpoint m_udp_from;

	std::array<char, 1500> m_udp_buffer;

	// receive buffer for data going out, i.e. client -> proxy (us) -> server
	char m_out_buffer[65536];
	// buffer size
	int m_num_out_bytes;

	// receive buffer for data coming in, i.e. server -> proxy (us) -> client
	char m_in_buffer[65536];
	// buffer size
	int m_num_in_bytes;

	// set to true when shutting down
	bool m_close = false;

	// the SOCKS protocol version (4 or 5)
	const int m_version;

	int m_command;

	std::array<int, 3>& m_cmd_counts;

	std::uint32_t const m_flags;
};

// This is a very simple socks4 and 5 server that only supports a single
// concurrent connection
struct socks_server
{
	socks_server(asio::io_context& ios,
		unsigned short listen_port,
		int version = 5,
		std::uint32_t flags = 0);

	void stop();

	void bind_start_port(int const port) { m_bind_port = port; }

	// return the number of CONNECT, BIND and UDP_ASSOCIATE commands the proxy
	// has received
	std::array<int, 3> cmd_counts() const { return m_cmd_counts; }

private:
	void on_accept(boost::system::error_code const& ec);

	asio::io_context& m_ios;

	asio::ip::tcp::acceptor m_listen_socket;

	std::shared_ptr<socks_connection> m_conn;

	asio::ip::tcp::endpoint m_ep;

	int m_bind_port = 2048;

	// set to true when shutting down
	bool m_close = false;

	// the SOCKS protocol version (4 or 5)
	const int m_version;

	std::array<int, 3> m_cmd_counts;

	std::uint32_t const m_flags;
};

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
