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

#include "simulator/simulator.hpp"
#include "socks_server.hpp"

#include <functional>
#include <cstdint>
#include <cinttypes>
#include <cstdio> // for printf

using namespace sim::asio;
using namespace sim::asio::ip;

using boost::system::error_code;

namespace sim {
using namespace aux;

socks_server::socks_server(
	io_context& ios, unsigned short listen_port, int version, std::uint32_t const flags)
	: m_ios(ios)
	, m_listen_socket(ios)
	, m_conn(std::make_shared<socks_connection>(m_ios, version, m_cmd_counts, flags, m_bind_port))
	, m_version(version)
	, m_flags(flags)
{
	m_cmd_counts.fill(0);
	address local_ip = ios.get_ips().front();
	if (local_ip.is_v4())
	{
		m_listen_socket.open(tcp::v4());
		m_listen_socket.bind(tcp::endpoint(address_v4::any(), listen_port));
	}
	else
	{
		m_listen_socket.open(tcp::v6());
		m_listen_socket.bind(tcp::endpoint(address_v6::any(), listen_port));
	}
	m_listen_socket.listen();

	m_listen_socket.async_accept(
		m_conn->socket(), m_ep, std::bind(&socks_server::on_accept, this, std::placeholders::_1));
}

void socks_server::on_accept(error_code const& ec)
{
	if (ec == asio::error::operation_aborted)
		return;

	if (ec)
	{
		std::printf("socks_server::on_accept: (%d) %s\n", ec.value(), ec.message().c_str());
		return;
	}

	std::printf("socks_server accepted connection from: %s : %d\n",
		m_ep.address().to_string().c_str(),
		m_ep.port());

	m_conn->start();

	// create a new connection to accept into
	m_conn =
		std::make_shared<socks_connection>(m_ios, m_version, m_cmd_counts, m_flags, m_bind_port);

	// now we can accept another connection
	m_listen_socket.async_accept(
		m_conn->socket(), m_ep, std::bind(&socks_server::on_accept, this, std::placeholders::_1));
}

void socks_server::stop()
{
	m_close = true;
	m_listen_socket.close();
}

socks_connection::socks_connection(asio::io_context& ios,
	int version,
	std::array<int, 3>& cmd_counts,
	std::uint32_t const flags,
	int& bind_port)
	: m_bind_port(bind_port)
	, m_ios(ios)
	, m_udp_resolver(ios)
	, m_resolver(m_ios)
	, m_client_connection(ios)
	, m_server_connection(m_ios)
	, m_bind_socket(m_ios)
	, m_udp_associate(m_ios)
	, m_num_out_bytes(0)
	, m_num_in_bytes(0)
	, m_version(version)
	, m_command(0)
	, m_cmd_counts(cmd_counts)
	, m_flags(flags)
{}

void socks_connection::start()
{
	if (m_version == 4)
	{
		asio::async_read(m_client_connection,
			asio::buffer(&m_out_buffer[0], 9),
			std::bind(&socks_connection::on_request1,
				shared_from_this(),
				std::placeholders::_1,
				std::placeholders::_2));
	}
	else
	{
		// read protocol version and number of auth-methods
		asio::async_read(m_client_connection,
			asio::buffer(&m_out_buffer[0], 2),
			std::bind(&socks_connection::on_handshake1,
				shared_from_this(),
				std::placeholders::_1,
				std::placeholders::_2));
	}
}

void socks_connection::on_handshake1(error_code const& ec, size_t bytes_transferred)
{
	if (ec || bytes_transferred != 2)
	{
		std::printf("socks_connection::on_handshake1: (%d) %s\n", ec.value(), ec.message().c_str());
		close_connection();
		return;
	}

	if (m_out_buffer[0] != 4 && m_out_buffer[0] != 5)
	{
		std::printf("socks_connection::on_handshake1: unexpected socks protocol version: %d",
			int(m_out_buffer[0]));
		close_connection();
		return;
	}

	int num_methods = unsigned(m_out_buffer[1]);

	// read list of auth-methods
	asio::async_read(m_client_connection,
		asio::buffer(&m_out_buffer[0], num_methods),
		std::bind(&socks_connection::on_handshake2,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2));
}

void socks_connection::on_handshake2(error_code const& ec, size_t bytes_transferred)
{
	if (ec)
	{
		std::printf("socks_connection::on_handshake2: (%d) %s\n", ec.value(), ec.message().c_str());
		close_connection();
		return;
	}

	if (std::count(m_out_buffer, m_out_buffer + bytes_transferred, 0) == 0)
	{
		std::printf(
			"socks_connection: could not find auth-method 0 (no-auth) in socks handshake\n");
		close_connection();
		return;
	}

	m_in_buffer[0] = 5; // socks version
	m_in_buffer[1] = 0; // auth-method (no-auth)

	asio::async_write(m_client_connection,
		asio::buffer(&m_in_buffer[0], 2),
		std::bind(&socks_connection::on_handshake3,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2));
}

void socks_connection::on_handshake3(error_code const& ec, size_t bytes_transferred)
{
	if (ec || bytes_transferred != 2)
	{
		std::printf("socks_connection::on_handshake3: (%d) %s\n", ec.value(), ec.message().c_str());
		close_connection();
		return;
	}

	asio::async_read(m_client_connection,
		asio::buffer(&m_out_buffer[0], 10),
		std::bind(&socks_connection::on_request1,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2));
}

void socks_connection::on_request1(error_code const& ec, size_t bytes_transferred)
{
	size_t const expected = m_version == 4 ? 9 : 10;
	if (ec || bytes_transferred != expected)
	{
		std::printf("socks_connection::on_request1: (%d) %s\n", ec.value(), ec.message().c_str());
		close_connection();
		return;
	}

	// +----+-----+-------+------+----------+----------+
	// |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
	// +----+-----+-------+------+----------+----------+
	// | 1  |  1  | X'00' |  1   | Variable |    2     |
	// +----+-----+-------+------+----------+----------+

	int const version = m_out_buffer[0];
	int const command = m_out_buffer[1];
	m_command = command;
	++m_cmd_counts[command - 1];

	if (version != m_version)
	{
		std::printf(
			"socks_connection::on_request1: unexpected socks protocol version: %d expected: %d\n",
			int(m_out_buffer[0]),
			m_version);
		close_connection();
		return;
	}

	if (m_version == 4)
	{
		if (command != 1 && command != 2)
		{
			std::printf("socks_connection::on_request1: unexpected socks command: %d\n", command);
			close_connection();
			return;
		}

		std::uint16_t port = m_out_buffer[2] & 0xff;
		port <<= 8;
		port |= m_out_buffer[3] & 0xff;

		std::uint32_t addr = m_out_buffer[4] & 0xff;
		addr <<= 8;
		addr |= m_out_buffer[5] & 0xff;
		addr <<= 8;
		addr |= m_out_buffer[6] & 0xff;
		addr <<= 8;
		addr |= m_out_buffer[7] & 0xff;

		if (m_out_buffer[8] != 0)
		{
			// in this case, we would have to read one byte at a time until we
			// get to the null terminator.
			std::printf("socks_connection::on_request1: username in SOCKS4 mode not supported\n");
			close_connection();
			return;
		}
		asio::ip::tcp::endpoint target(asio::ip::address_v4(addr), port);
		if (command == 1)
		{
			open_forward_connection(target);
		}
		else if (command == 2)
		{
			bind_connection(target);
		}

		return;
	}

	if (command != 1 && command != 2 && command != 3)
	{
		std::printf("socks_connection::on_request1: unexpected command: %d\n", command);
		close_connection();
		return;
	}

	if (m_out_buffer[2] != 0)
	{
		std::printf(
			"socks_connection::on_request1: reserved byte is non-zero: %d\n", int(m_out_buffer[2]));
		close_connection();
		return;
	}

	int atyp = unsigned(m_out_buffer[3]);

	if (atyp != 1 && atyp != 3 && atyp != 4)
	{
		std::printf(
			"socks_connection::on_request1: unexpected address type in SOCKS request: %d\n", atyp);
		close_connection();
		return;
	}

	std::printf("socks_connection: received %s request address type: %d\n",
		command == 1	   ? "CONNECT"
			: command == 2 ? "BIND"
						   : "UDP_ASSOCIATE",
		atyp);

	switch (atyp)
	{
		case 1: { // IPv4 address (we have the whole request already)

			// +----+-----+-------+------+----------+----------+
			// |VER | CMD |  RSV  | ATYP | BND.ADDR | BND.PORT |
			// +----+-----+-------+------+----------+----------+
			// | 1  |  1  | X'00' |  1   | 4        |    2     |
			// +----+-----+-------+------+----------+----------+

			std::uint32_t addr = m_out_buffer[4] & 0xff;
			addr <<= 8;
			addr |= m_out_buffer[5] & 0xff;
			addr <<= 8;
			addr |= m_out_buffer[6] & 0xff;
			addr <<= 8;
			addr |= m_out_buffer[7] & 0xff;

			std::uint16_t port = m_out_buffer[8] & 0xff;
			port <<= 8;
			port |= m_out_buffer[9] & 0xff;

			asio::ip::tcp::endpoint target(asio::ip::address_v4(addr), port);
			if (command == 1)
			{
				open_forward_connection(target);
			}
			else if (command == 2)
			{
				bind_connection(target);
			}
			else if (command == 3)
			{
				if (target.address() == address())
				{
					target.address(m_client_connection.remote_endpoint().address());
				}
				udp_associate(target);
			}

			break;
		}
		case 3: { // domain name

			// +----+-----+-------+------+-----+----------+----------+
			// |VER | CMD |  RSV  | ATYP | LEN | BND.ADDR | BND.PORT |
			// +----+-----+-------+------+-----+----------+----------+
			// | 1  |  1  | X'00' |  1   | 1   | Variable |    2     |
			// +----+-----+-------+------+-----+----------+----------+

			if (command == 2)
			{
				std::printf(
					"ERROR: cannot BIND to hostname address (only IPv4 or IPv6 addresses)\n");
				close_connection();
				return;
			}

			const int len = std::uint8_t(m_out_buffer[4]);
			// we already read an address of length 4, assuming it was an IPv4
			// address. Now, with a domain name, one of those bytes was the
			// length-prefix, but we still read 3 bytes already.
			const int additional_bytes = len - 3;
			asio::async_read(m_client_connection,
				asio::buffer(&m_out_buffer[10], additional_bytes),
				std::bind(&socks_connection::on_request_domain_name,
					shared_from_this(),
					std::placeholders::_1,
					std::placeholders::_2));
			break;
		}
		case 4: // IPv6 address

			// +----+-----+-------+------+----------+----------+
			// |VER | CMD |  RSV  | ATYP | BND.ADDR | BND.PORT |
			// +----+-----+-------+------+----------+----------+
			// | 1  |  1  | X'00' |  1   | 16       |    2     |
			// +----+-----+-------+------+----------+----------+

			std::printf("ERROR: unsupported address type %d\n", atyp);
			close_connection();
	}
}

void socks_connection::on_request_domain_name(error_code const& ec, size_t bytes_transferred)
{
	if (ec)
	{
		std::printf("socks_connection::on_request_domain_name(%s): (%d) %s\n",
			command(),
			ec.value(),
			ec.message().c_str());
		close_connection();
		return;
	}

	int const buffer_size = int(10 + bytes_transferred);

	std::uint16_t port = m_out_buffer[buffer_size - 2] & 0xff;
	port <<= 8;
	port |= m_out_buffer[buffer_size - 1] & 0xff;

	std::string hostname(&m_out_buffer[5], std::uint8_t(m_out_buffer[4]));
	std::printf("socks_connection::on_request_domain_name(%s): hostname: %s port: %d\n",
		command(),
		hostname.c_str(),
		port);

	char port_str[10];
	std::snprintf(port_str, sizeof(port_str), "%d", port);
	m_resolver.async_resolve(hostname,
		port_str,
		std::bind(&socks_connection::on_request_domain_lookup,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2));
}

void socks_connection::on_request_domain_lookup(
	boost::system::error_code const& ec, asio::ip::tcp::resolver::results_type const ips)
{
	if (ec || ips.empty())
	{
		if (ec)
		{
			std::printf("socks_connection::on_request_domain_lookup(%s): (%d) %s\n",
				command(),
				ec.value(),
				ec.message().c_str());
		}
		else
		{
			std::printf(
				"socks_connection::on_request_domain_lookup(%s): empty response\n", command());
		}

		// +----+-----+-------+------+----------+----------+
		// |VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
		// +----+-----+-------+------+----------+----------+
		// | 1  |  1  | X'00' |  1   | Variable |    2     |
		// +----+-----+-------+------+----------+----------+

		m_in_buffer[0] = char(m_version); // version
		m_in_buffer[1] = 4; // response (host unreachable)
		m_in_buffer[2] = 0; // reserved
		m_in_buffer[3] = 1; // IPv4
		memset(&m_in_buffer[4], 0, 4);
		m_in_buffer[8] = 0; // port
		m_in_buffer[9] = 0;

		auto self = shared_from_this();
		asio::async_write(m_client_connection,
			asio::buffer(&m_in_buffer[0], 10),
			[=](boost::system::error_code const&, size_t) { self->close_connection(); });
		return;
	}

	std::printf("socks_connection::on_request_domain_lookup(%s): connecting to: %s port: %d\n",
		command(),
		ips.front().endpoint().address().to_string().c_str(),
		ips.front().endpoint().port());
	open_forward_connection(ips.front().endpoint());
}

void socks_connection::open_forward_connection(const asio::ip::tcp::endpoint& target)
{
	std::printf("socks_connection::open_forward_connection(%s): connecting to %s port %d\n",
		command(),
		target.address().to_string().c_str(),
		target.port());

	m_server_connection.open(target.protocol());
	m_server_connection.async_connect(target,
		std::bind(&socks_connection::on_connected, shared_from_this(), std::placeholders::_1));
}

void socks_connection::bind_connection(const asio::ip::tcp::endpoint& target)
{
	std::printf("socks_connection::bind_connection(%s): binding to %s port %d\n",
		command(),
		target.address().to_string().c_str(),
		target.port());

	error_code ec;
	m_bind_socket.open(target.protocol(), ec);
	if (ec)
	{
		std::printf("ERROR: open bind socket failed: (%d) %s\n", ec.value(), ec.message().c_str());
	}
	else
	{
		m_bind_socket.bind(target, ec);
	}

	int const response = ec ? (m_version == 4 ? 91 : 1) : (m_version == 4 ? 90 : 0);
	tcp::endpoint ep = m_bind_socket.local_endpoint();
	int const len = format_response(ep.address(), ep.port(), response);

	if (ec)
	{
		std::printf("ERROR: binding socket to %s %d failed: (%d) %s\n",
			target.address().to_string().c_str(),
			target.port(),
			ec.value(),
			ec.message().c_str());

		auto self = shared_from_this();

		asio::async_write(m_client_connection,
			asio::buffer(&m_in_buffer[0], len),
			[=](boost::system::error_code const&, size_t) { self->close_connection(); });
		return;
	}

	// send response
	asio::async_write(m_client_connection,
		asio::buffer(&m_in_buffer[0], len),
		std::bind(&socks_connection::start_accept, shared_from_this(), std::placeholders::_1));
}

void socks_connection::udp_associate(const asio::ip::tcp::endpoint& target)
{
	std::printf("socks_connection::udp_associate(%s): %s:%d\n",
		command(),
		target.address().to_string().c_str(),
		target.port());

	m_udp_associate_ep.address(target.address());
	m_udp_associate_ep.port(target.port());

	error_code ec;
	m_udp_associate.open(m_udp_associate_ep.protocol(), ec);
	if (ec)
	{
		std::printf(
			"ERROR: open UDP associate socket failed: (%d) %s\n", ec.value(), ec.message().c_str());
	}
	else
	{
		m_udp_associate.bind(udp::endpoint(address_v4(), std::uint16_t(m_bind_port++)), ec);
		if (ec)
		{
			std::printf(
				"ERROR: binding socket failed: (%d) %s\n", ec.value(), ec.message().c_str());
		}
		else
		{
			m_udp_associate.non_blocking(true);
			m_udp_associate.async_receive_from(boost::asio::buffer(m_udp_buffer),
				m_udp_from,
				0,
				std::bind(&socks_connection::on_read_udp,
					this,
					std::placeholders::_1,
					std::placeholders::_2));
		}
	}

	int const response = ec ? 1 : 0;
	udp::endpoint ep = m_udp_associate.local_bound_to();
	int const len = (m_flags & udp_associate_respond_empty_hostname)
		? format_hostname_response("foobar", ep.port(), response)
		: format_response(ep.address(), ep.port(), response);

	if (ec)
	{
		auto self = shared_from_this();

		asio::async_write(m_client_connection,
			asio::buffer(&m_in_buffer[0], len),
			[=](boost::system::error_code const&, size_t) { self->close_connection(); });
		return;
	}

	// send response
	asio::async_write(m_client_connection,
		asio::buffer(&m_in_buffer[0], len),
		std::bind(&socks_connection::wait_for_eof,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2));
}

void socks_connection::wait_for_eof(boost::system::error_code const& ec, std::size_t)
{
	if (ec)
	{
		std::printf("socks_connection::wait_for_eof: %s\n", ec.message().c_str());
		m_udp_associate.close();
		m_udp_associate_ep = udp::endpoint();
		m_client_connection.close();
		return;
	}

	if (m_flags & socks_flag::disconnect_udp_associate)
	{
		std::printf("socks_connection::wait_for_eof: closing connection prematurely\n");
		m_client_connection.close();
		return;
	}

	m_client_connection.async_read_some(asio::buffer(m_out_buffer),
		std::bind(&socks_connection::wait_for_eof,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2));
}

void socks_connection::on_read_udp(
	boost::system::error_code const& ec, std::size_t bytes_transferred)
{
	std::printf("socks_connection::on_read_udp\n");
	if (ec)
	{
		std::printf("socks_connection::on_read_udp: %s\n", ec.message().c_str());
		return;
	}

	// if the client didn't specify an IP and port it would send packets from,
	// we assumed the same IP as the TCP connection and assume the port is the
	// same as the first UDP packet from that host
	if (m_udp_associate_ep.port() == 0 && m_udp_from.address() == m_udp_associate_ep.address())
	{
		m_udp_associate_ep.port(m_udp_from.port());
	}

	if (m_udp_from == m_udp_associate_ep)
	{
		// read UDP ASSICOATE header and forward outgoing packet
		// +----+------+------+----------+----------+----------+
		// |RSV | FRAG | ATYP | DST.ADDR | DST.PORT |   DATA   |
		// +----+------+------+----------+----------+----------+
		// | 2  |  1   |  1   | Variable |    2     | Variable |
		// +----+------+------+----------+----------+----------+

		char const* buf = m_udp_buffer.data();
		if (buf[2] != 0)
			std::printf("fragment != 0, not supported\n");

		int const atyp = buf[3];
		if (atyp == 3)
		{
			// hostname
			int const len = buf[4];

			buf += 5;
			bytes_transferred -= 5;
			std::string const hostname(buf, len);
			buf += len;
			bytes_transferred -= len;

			std::uint16_t port = buf[0] & 0xff;
			port <<= 8;
			port |= buf[1] & 0xff;
			buf += 2;
			bytes_transferred -= 2;

			auto it = m_name_mapping.right.find(hostname);
			if (it != m_name_mapping.right.end())
			{
				error_code err;
				m_udp_associate.send_to(boost::asio::buffer(buf, bytes_transferred),
					udp::endpoint(it->second, port),
					0,
					err);
				if (err)
					std::printf("send_to failed: %s\n", err.message().c_str());
				return;
			}

			std::vector<char> forward_buffer(buf, buf + bytes_transferred);

			m_udp_resolver.async_resolve(hostname.c_str(),
				std::to_string(port).c_str(),
				[buf = std::move(forward_buffer), hostname, this](
					error_code const& ec, asio::ip::udp::resolver::results_type ips) {
					if (ec)
					{
						std::printf("resolve failed: %s\n", ec.message().c_str());
						return;
					}

					for (auto const& ip : ips)
					{
						auto const target = ip.endpoint();
						error_code err;
						m_udp_associate.send_to(boost::asio::buffer(buf), target, 0, err);
						if (!err)
						{
							m_name_mapping.insert({target.address(), hostname});
							break;
						}
						std::printf("send_to failed: %s\n", err.message().c_str());
					}
				});
		}
		else if (atyp == 1)
		{
			// IPv4
			std::uint32_t addr = buf[4] & 0xff;
			addr <<= 8;
			addr |= buf[5] & 0xff;
			addr <<= 8;
			addr |= buf[6] & 0xff;
			addr <<= 8;
			addr |= buf[7] & 0xff;

			std::uint16_t port = buf[8] & 0xff;
			port <<= 8;
			port |= buf[9] & 0xff;

			buf += 10;
			bytes_transferred -= 10;

			asio::ip::udp::endpoint const target(address_v4(addr), port);

			error_code err;
			m_udp_associate.send_to(boost::asio::buffer(buf, bytes_transferred), target, 0, err);
			if (err)
				std::printf("send_to failed: %s\n", err.message().c_str());
		}
		else
		{
			std::printf("only supports IPv4 and hostname. ATYP: %d\n", atyp);
		}
	}
	else
	{
		std::uint16_t const from_port = m_udp_from.port();

		auto it = m_name_mapping.left.find(m_udp_from.address());
		if (it != m_name_mapping.left.end())
		{
			std::vector<char> header(7 + it->second.size());
			header[0] = 0; // RSV
			header[1] = 0;
			header[2] = 0; // fragment
			header[3] = 3; // ATYP
			header[4] = static_cast<char>(it->second.size());
			int idx = 5;
			std::copy(it->second.begin(), it->second.end(), header.data() + idx);
			idx += static_cast<int>(it->second.size());
			header[idx] = (from_port >> 8) & 0xff;
			header[idx + 1] = from_port & 0xff;

			std::array<boost::asio::const_buffer, 2> vec{
				{{header.data(), header.size()}, {m_udp_buffer.data(), bytes_transferred}}};

			error_code err;
			m_udp_associate.send_to(vec, m_udp_associate_ep, 0, err);
			if (err)
				std::printf("send_to failed: %s\n", err.message().c_str());
		}
		else
		{
			// add UDP ASSOCIATE header and forward to client
			std::uint32_t const from_addr = m_udp_from.address().to_v4().to_uint();
			std::array<char, 10> header;
			header[0] = 0; // RSV
			header[1] = 0;
			header[2] = 0; // fragment
			header[3] = 1; // ATYP
			header[4] = (from_addr >> 24) & 0xff; // Address
			header[5] = (from_addr >> 16) & 0xff;
			header[6] = (from_addr >> 8) & 0xff;
			header[7] = (from_addr) & 0xff;
			header[8] = (from_port >> 8) & 0xff;
			header[9] = from_port & 0xff;

			std::array<boost::asio::const_buffer, 2> vec{
				{{header.data(), header.size()}, {m_udp_buffer.data(), bytes_transferred}}};

			error_code err;
			m_udp_associate.send_to(vec, m_udp_associate_ep, 0, err);
			if (err)
				std::printf("send_to failed: %s\n", err.message().c_str());
		}
	}

	m_udp_associate.async_receive_from(boost::asio::buffer(m_udp_buffer),
		m_udp_from,
		0,
		std::bind(
			&socks_connection::on_read_udp, this, std::placeholders::_1, std::placeholders::_2));
}

void socks_connection::start_accept(boost::system::error_code const& ec)
{
	if (ec)
	{
		std::printf("socks_connection(%s): error writing to client: (%d) %s\n",
			command(),
			ec.value(),
			ec.message().c_str());
		close_connection();
		return;
	}

	m_bind_socket.listen();
	m_bind_socket.async_accept(m_server_connection,
		std::bind(&socks_connection::on_connected, shared_from_this(), std::placeholders::_1));

	m_client_connection.async_read_some(sim::asio::buffer(m_out_buffer),
		std::bind(&socks_connection::on_client_receive,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2));
}

int socks_connection::format_response(address const& addr, int const port, int const response)
{
	int i = 0;
	if (m_version == 5)
	{
		// +----+-----+-------+------+----------+----------+
		// |VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
		// +----+-----+-------+------+----------+----------+
		// | 1  |  1  | X'00' |  1   | Variable |    2     |
		// +----+-----+-------+------+----------+----------+

		m_in_buffer[i++] = char(m_version); // version
		m_in_buffer[i++] = char(response); // response
		m_in_buffer[i++] = 0; // reserved
		if (addr.is_v4())
		{
			m_in_buffer[i++] = 1; // IPv4
			address_v4::bytes_type b = addr.to_v4().to_bytes();
			memcpy(&m_in_buffer[i], &b[0], b.size());
			i += int(b.size());
		}
		else
		{
			m_in_buffer[i++] = 4; // IPv6
			address_v6::bytes_type b = addr.to_v6().to_bytes();
			memcpy(&m_in_buffer[i], &b[0], b.size());
			i += int(b.size());
		}

		m_in_buffer[i++] = (port >> 8) & 0xff;
		m_in_buffer[i++] = port & 0xff;
	}
	else
	{
		m_in_buffer[i++] = 0; // response version
		m_in_buffer[i++] = char(response); // return code

		assert(addr.is_v4());

		m_in_buffer[i++] = (port >> 8) & 0xff;
		m_in_buffer[i++] = port & 0xff;

		address_v4::bytes_type b = addr.to_v4().to_bytes();
		memcpy(&m_in_buffer[i], &b[0], b.size());
		i += int(b.size());
	}
	return i;
}

int socks_connection::format_hostname_response(
	char const* hostname, int const port, int const response)
{
	int i = 0;
	if (m_version != 5)
	{
		std::printf("socks_connection: hostname response requires SOCKS v5\n");
		close_connection();
		return 0;
	}
	// +----+-----+-------+------+----------+----------+
	// |VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
	// +----+-----+-------+------+----------+----------+
	// | 1  |  1  | X'00' |  1   | Variable |    2     |
	// +----+-----+-------+------+----------+----------+

	m_in_buffer[i++] = char(m_version); // version
	m_in_buffer[i++] = char(response); // response
	m_in_buffer[i++] = 0; // reserved
	m_in_buffer[i++] = 3; // DOMAINNAME
	m_in_buffer[i++] = std::uint8_t(::strlen(hostname));
	for (; *hostname != '\0'; ++hostname)
		m_in_buffer[i++] = *hostname;

	m_in_buffer[i++] = (port >> 8) & 0xff;
	m_in_buffer[i++] = port & 0xff;
	return i;
}

void socks_connection::on_connected(boost::system::error_code const& ec)
{
	std::printf(
		"socks_connection(%s): on_connect: (%d) %s\n", command(), ec.value(), ec.message().c_str());

	if (ec == asio::error::operation_aborted || ec == boost::system::errc::bad_file_descriptor)
	{
		return;
	}

	boost::system::error_code err;
	asio::ip::tcp::endpoint const ep = m_server_connection.remote_endpoint(err);
	if (!err)
	{
		std::printf("socks_connection(%s): remote_endpoint: %s %d\n",
			command(),
			ep.address().to_string().c_str(),
			ep.port());
	}

	int const response = ec ? (m_version == 4 ? 91 : 5) : (m_version == 4 ? 90 : 0);
	int const len = format_response(ep.address(), ep.port(), response);

	if (ec)
	{
		std::printf(
			"socks_connection(%s): failed to connect to/accept from target server: (%d) %s\n",
			command(),
			ec.value(),
			ec.message().c_str());

		auto self = shared_from_this();

		asio::async_write(m_client_connection,
			asio::buffer(&m_in_buffer[0], len),
			[=](boost::system::error_code const&, size_t) { self->close_connection(); });
		return;
	}

	auto self = shared_from_this();

	asio::async_write(m_client_connection,
		asio::buffer(&m_in_buffer[0], len),
		[this, self](boost::system::error_code const& ec, size_t) {
			if (ec)
			{
				std::printf("socks_connection(%s): error writing to client: (%d) %s\n",
					command(),
					ec.value(),
					ec.message().c_str());
				return;
			}

			// read from the client and from the server
			self->m_server_connection.async_read_some(sim::asio::buffer(m_in_buffer),
				std::bind(&socks_connection::on_server_receive,
					self,
					std::placeholders::_1,
					std::placeholders::_2));
			self->m_client_connection.async_read_some(sim::asio::buffer(m_out_buffer),
				std::bind(&socks_connection::on_client_receive,
					self,
					std::placeholders::_1,
					std::placeholders::_2));
		});
}

// we received some data from the client, forward it to the server
void socks_connection::on_client_receive(
	boost::system::error_code const& ec, std::size_t bytes_transferred)
{
	// bad file descriptor means the socket has been closed. Whoever closed
	// the socket will have opened a new one, we cannot call
	// close_connection()
	if (ec == asio::error::operation_aborted || ec == boost::system::errc::bad_file_descriptor)
		return;

	if (ec)
	{
		std::printf("socks_connection (%s): error reading from client: (%d) %s\n",
			command(),
			ec.value(),
			ec.message().c_str());
		close_connection();
		return;
	}
	asio::async_write(m_server_connection,
		asio::buffer(&m_out_buffer[0], bytes_transferred),
		std::bind(&socks_connection::on_client_forward,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2));
}

void socks_connection::on_client_forward(error_code const& ec, size_t /* bytes_transferred */)
{
	if (ec)
	{
		std::printf("socks_connection(%s): error writing to server: (%d) %s\n",
			command(),
			ec.value(),
			ec.message().c_str());
		close_connection();
		return;
	}

	m_client_connection.async_read_some(sim::asio::buffer(m_out_buffer),
		std::bind(&socks_connection::on_client_receive,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2));
}

// we received some data from the server, forward it to the server
void socks_connection::on_server_receive(
	boost::system::error_code const& ec, std::size_t bytes_transferred)
{
	if (ec)
	{
		std::printf("socks_connection(%s): error reading from server: (%d) %s\n",
			command(),
			ec.value(),
			ec.message().c_str());
		close_connection();
		return;
	}

	asio::async_write(m_client_connection,
		asio::buffer(&m_in_buffer[0], bytes_transferred),
		std::bind(&socks_connection::on_server_forward,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2));
}

void socks_connection::on_server_forward(error_code const& ec, size_t /* bytes_transferred */)
{
	if (ec)
	{
		std::printf("socks_connection(%s): error writing to client: (%d) %s\n",
			command(),
			ec.value(),
			ec.message().c_str());
		close_connection();
		return;
	}

	m_server_connection.async_read_some(sim::asio::buffer(m_in_buffer),
		std::bind(&socks_connection::on_server_receive,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2));
}

void socks_connection::close_connection()
{
	error_code err;
	m_client_connection.close(err);
	if (err)
	{
		std::printf("socks_connection::close: failed to close client connection (%d) %s\n",
			err.value(),
			err.message().c_str());
	}
	m_server_connection.close(err);
	if (err)
	{
		std::printf("socks_connection::close: failed to close server connection (%d) %s\n",
			err.value(),
			err.message().c_str());
	}

	m_bind_socket.close(err);
	if (err)
	{
		std::printf("socks_connection::close: failed to close bind socket (%d) %s\n",
			err.value(),
			err.message().c_str());
	}
}

char const* socks_connection::command() const
{
	switch (m_command)
	{
		case 1:
			return "CONNECT";
		case 2:
			return "BIND";
		case 3:
			return "UDP_ASSOCIATE";
		default:
			return "UNKNOWN";
	}
}
}
