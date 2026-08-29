/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "test.hpp"
#include "libtorrent/aux_/alert_manager.hpp"
#include "libtorrent/aux_/resolver.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/udp_socket.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/span.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace lt;

namespace {
struct socks5_udp_test_server
{
	explicit socks5_udp_test_server(io_context& ios)
		: m_acceptor(ios, tcp::endpoint(make_address_v4("127.0.0.1"), 0))
		, m_control_socket(ios)
		, m_udp_socket(ios, udp::endpoint(make_address_v4("127.0.0.1"), 0))
	{}

	std::uint16_t port() const { return m_acceptor.local_endpoint().port(); }

	void start()
	{
		receive_packet();
		m_acceptor.async_accept(m_control_socket, [this](error_code const& ec) {
			if (ec)
			{
				m_error = ec;
				return;
			}

			boost::asio::async_read(m_control_socket,
				boost::asio::buffer(m_control_buffer.data(), 3),
				[this](error_code const& e, std::size_t const bytes) {
					if (e)
					{
						m_error = e;
						return;
					}

					m_valid_handshake = bytes == 3 && m_control_buffer[0] == 5
						&& m_control_buffer[1] == 1 && m_control_buffer[2] == 0;
					m_control_buffer[0] = 5;
					m_control_buffer[1] = 0;
					boost::asio::async_write(m_control_socket,
						boost::asio::buffer(m_control_buffer.data(), 2),
						[this](error_code const& write_error, std::size_t) {
							if (write_error)
							{
								m_error = write_error;
								return;
							}
							read_associate_request();
						});
				});
		});
	}

	error_code m_error;
	bool m_valid_handshake = true;
	std::vector<std::vector<char>> packets;

private:
	void read_associate_request()
	{
		boost::asio::async_read(m_control_socket,
			boost::asio::buffer(m_control_buffer.data(), 10),
			[this](error_code const& ec, std::size_t const bytes) {
				if (ec)
				{
					m_error = ec;
					return;
				}

				m_valid_handshake = m_valid_handshake && bytes == 10 && m_control_buffer[0] == 5
					&& m_control_buffer[1] == 3 && m_control_buffer[2] == 0
					&& m_control_buffer[3] == 1;

				m_control_buffer = {{5, 0, 0, 1, 127, 0, 0, 1, 0, 0}};
				auto const udp_port = m_udp_socket.local_endpoint().port();
				m_control_buffer[8] = char(udp_port >> 8);
				m_control_buffer[9] = char(udp_port & 0xff);
				boost::asio::async_write(m_control_socket,
					boost::asio::buffer(m_control_buffer),
					[this](error_code const& write_error, std::size_t) {
						if (write_error)
							m_error = write_error;
					});
			});
	}

	void receive_packet()
	{
		m_udp_socket.async_receive_from(boost::asio::buffer(m_udp_buffer),
			m_sender,
			[this](error_code const& ec, std::size_t const bytes) {
				if (ec)
				{
					m_error = ec;
					return;
				}

				packets.emplace_back(m_udp_buffer.begin(), m_udp_buffer.begin() + bytes);
				if (packets.size() < 3)
					receive_packet();
			});
	}

	tcp::acceptor m_acceptor;
	tcp::socket m_control_socket;
	udp::socket m_udp_socket;
	udp::endpoint m_sender;
	std::array<char, 10> m_control_buffer{};
	std::array<char, 512> m_udp_buffer{};
};

	// build a SOCKS5 UDP forwarded packet for an IPv4 destination.
	// header layout (RFC 1928 section 7):
	//   2 bytes RSV (0x0000)
	//   1 byte  FRAG
	//   1 byte  ATYP (0x01 = IPv4)
	//   4 bytes addr
	//   2 bytes port
	//   N bytes payload
	std::vector<char> make_v4_packet(
		std::uint8_t const frag,
		std::array<std::uint8_t, 4> const addr,
		std::uint16_t const port,
		span<char const> payload
	)
	{
		std::vector<char> buf;
		buf.push_back(0);
		buf.push_back(0); // RSV
		buf.push_back(char(frag));
		buf.push_back(0x01); // ATYP = IPv4
		for (auto b : addr)
			buf.push_back(char(b));
		buf.push_back(char(port >> 8));
		buf.push_back(char(port & 0xff));
		buf.insert(buf.end(), payload.begin(), payload.end());
		return buf;
	}

	// build a SOCKS5 UDP forwarded packet for an IPv6 destination.
	// header layout: 2 RSV + 1 FRAG + 1 ATYP(0x04) + 16 addr + 2 port + payload
	std::vector<char> make_v6_packet(
		std::uint8_t const frag,
		std::array<std::uint8_t, 16> const addr,
		std::uint16_t const port,
		span<char const> payload
	)
	{
		std::vector<char> buf;
		buf.push_back(0);
		buf.push_back(0);
		buf.push_back(char(frag));
		buf.push_back(0x04);
		for (auto b : addr)
			buf.push_back(char(b));
		buf.push_back(char(port >> 8));
		buf.push_back(char(port & 0xff));
		buf.insert(buf.end(), payload.begin(), payload.end());
		return buf;
	}

	// build a SOCKS5 UDP forwarded packet for a domain-name destination.
	// header layout: 2 RSV + 1 FRAG + 1 ATYP(0x03) + 1 LEN + LEN hostname + 2 port + payload
	std::vector<char> make_hostname_packet(
		std::uint8_t const frag,
		string_view const hostname,
		std::uint16_t const port,
		span<char const> payload
	)
	{
		std::vector<char> buf;
		buf.push_back(0);
		buf.push_back(0);
		buf.push_back(char(frag));
		buf.push_back(0x03);
		buf.push_back(char(hostname.size()));
		buf.insert(buf.end(), hostname.begin(), hostname.end());
		buf.push_back(char(port >> 8));
		buf.push_back(char(port & 0xff));
		buf.insert(buf.end(), payload.begin(), payload.end());
		return buf;
	}

} // anonymous namespace

TORRENT_TEST(socks5_unwrap_ipv4)
{
	std::array<char, 4> const payload{{'a', 'b', 'c', 'd'}};
	auto buf = make_v4_packet(0, {{1, 2, 3, 4}}, 6881, payload);

	aux::udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(aux::socks5_unwrap(pack));
	TEST_EQUAL(pack.from.address().to_string(), "1.2.3.4");
	TEST_EQUAL(pack.from.port(), 6881);
	TEST_EQUAL(int(pack.data.size()), int(payload.size()));
	TEST_CHECK(std::memcmp(pack.data.data(), payload.data(), payload.size()) == 0);
}

TORRENT_TEST(socks5_unwrap_ipv6)
{
	std::array<std::uint8_t, 16> const a{
		{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01}
	};
	std::array<char, 3> const payload{{'x', 'y', 'z'}};
	auto buf = make_v6_packet(0, a, 1234, payload);

	aux::udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(aux::socks5_unwrap(pack));
	TEST_EQUAL(pack.from.address().is_v6(), true);
	TEST_EQUAL(pack.from.port(), 1234);
	TEST_EQUAL(int(pack.data.size()), int(payload.size()));
	TEST_CHECK(std::memcmp(pack.data.data(), payload.data(), payload.size()) == 0);
}

TORRENT_TEST(socks5_unwrap_hostname_resolvable)
{
	// a hostname that parses as a valid address goes into pack.from
	std::array<char, 2> const payload{{'h', 'i'}};
	auto buf = make_hostname_packet(0, "5.6.7.8", 9000, payload);

	aux::udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(aux::socks5_unwrap(pack));
	TEST_EQUAL(pack.from.address().to_string(), "5.6.7.8");
	TEST_EQUAL(pack.from.port(), 9000);
	TEST_EQUAL(int(pack.data.size()), int(payload.size()));
	TEST_CHECK(std::memcmp(pack.data.data(), payload.data(), payload.size()) == 0);
}

TORRENT_TEST(socks5_unwrap_hostname_unresolvable)
{
	// a hostname that does not parse as an address goes into pack.hostname
	std::array<char, 2> const payload{{'h', 'i'}};
	auto buf = make_hostname_packet(0, "example.org", 80, payload);

	aux::udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(aux::socks5_unwrap(pack));
	TEST_EQUAL(pack.hostname, "example.org");
	TEST_EQUAL(int(pack.data.size()), int(payload.size()));
	TEST_CHECK(std::memcmp(pack.data.data(), payload.data(), payload.size()) == 0);
}

TORRENT_TEST(socks5_wrap_hostname_length)
{
	io_context ios;
	socks5_udp_test_server server(ios);
	server.start();

	auto listen_socket = std::make_shared<aux::listen_socket_t>();
	listen_socket->local_endpoint = tcp::endpoint(make_address_v4("127.0.0.1"), 0);
	aux::udp_socket socket(ios, aux::listen_socket_handle(listen_socket));

	error_code ec;
	socket.bind(udp::endpoint(make_address_v4("127.0.0.1"), 0), ec);
	TEST_CHECK(!ec);
	if (ec)
		return;

	aux::alert_manager alerts(10);
	aux::resolver resolver(ios);
	aux::proxy_settings proxy;
	proxy.hostname = "127.0.0.1";
	proxy.port = server.port();
	proxy.type = settings_pack::socks5;
	socket.set_proxy_settings(proxy, alerts, resolver, false);

	std::vector<std::string> const hostnames{
		std::string(249, 'a'), std::string(255, 'b'), std::string(300, 'c')};
	std::array<char, 1> const payload{{'!'}};
	int num_sent = 0;

	for (int i = 0; i < 200 && server.packets.size() < hostnames.size(); ++i)
	{
		ios.restart();
		ios.run_for(std::chrono::milliseconds(10));

		if (std::size_t(num_sent) != server.packets.size() || num_sent == int(hostnames.size()))
			continue;

		ec.clear();
		socket.send_hostname(
			hostnames[std::size_t(num_sent)].c_str(), 6881 + num_sent, payload, ec);
		if (!ec)
			++num_sent;
	}

	socket.close();
	ios.restart();
	ios.poll();

	TEST_CHECK(!server.m_error);
	TEST_CHECK(server.m_valid_handshake);
	TEST_EQUAL(num_sent, int(hostnames.size()));
	TEST_EQUAL(server.packets.size(), hostnames.size());
	if (server.packets.size() != hostnames.size())
		return;

	for (std::size_t i = 0; i < hostnames.size(); ++i)
	{
		auto const& packet = server.packets[i];
		auto const hostname_size = std::min(hostnames[i].size(), std::size_t(255));
		TEST_EQUAL(packet.size(), hostname_size + 8);
		if (packet.size() != hostname_size + 8)
			continue;

		TEST_EQUAL(std::uint8_t(packet[0]), 0);
		TEST_EQUAL(std::uint8_t(packet[1]), 0);
		TEST_EQUAL(std::uint8_t(packet[2]), 0);
		TEST_EQUAL(std::uint8_t(packet[3]), 3);
		TEST_EQUAL(std::uint8_t(packet[4]), hostname_size);
		auto const hostname_end = packet.begin() + std::ptrdiff_t(5 + hostname_size);
		TEST_CHECK(std::equal(packet.begin() + 5, hostname_end, hostnames[i].begin()));
		auto const port_offset = 5 + hostname_size;
		auto const port = (std::uint16_t(std::uint8_t(packet[port_offset])) << 8)
			| std::uint8_t(packet[port_offset + 1]);
		TEST_EQUAL(port, 6881 + int(i));
		TEST_EQUAL(packet[port_offset + 2], payload[0]);
	}
}

TORRENT_TEST(socks5_unwrap_reject_fragmented)
{
	std::array<char, 1> const payload{{'!'}};
	auto buf = make_v4_packet(1, {{1, 2, 3, 4}}, 1, payload);

	aux::udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(!aux::socks5_unwrap(pack));
}

TORRENT_TEST(socks5_unwrap_reject_nonzero_reserved)
{
	std::array<char, 1> const payload{{'!'}};

	for (std::size_t const reserved_byte : {std::size_t(0), std::size_t(1)})
	{
		for (int reserved_value = 1; reserved_value < 256; ++reserved_value)
		{
			auto buf = make_v4_packet(0, {{1, 2, 3, 4}}, 1, payload);
			buf[reserved_byte] = char(reserved_value);

			aux::udp_socket::packet pack;
			pack.data = span<char>{buf.data(), int(buf.size())};

			TEST_CHECK(!aux::socks5_unwrap(pack));
		}
	}
}

TORRENT_TEST(socks5_unwrap_reject_unknown_address_type)
{
	std::array<char, 4> const payload{{'t', 'e', 's', 't'}};

	for (int address_type = 0; address_type < 256; ++address_type)
	{
		if (address_type == 1 || address_type == 3 || address_type == 4)
			continue;

		auto buf = make_v4_packet(0, {{1, 2, 3, 4}}, 1, payload);
		buf[3] = char(address_type);

		aux::udp_socket::packet pack;
		pack.data = span<char>{buf.data(), int(buf.size())};

		TEST_CHECK(!aux::socks5_unwrap(pack));
	}
}

TORRENT_TEST(socks5_unwrap_reject_too_short)
{
	// the IPv4 minimum is 10 bytes of header, plus at least one payload byte
	std::array<char, 10> buf{};
	buf[3] = 0x01; // ATYP = IPv4

	aux::udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(!aux::socks5_unwrap(pack));
}

// regression: an IPv6 forwarded packet shorter than the minimum 22-byte
// header (4 byte preamble + 16 byte address + 2 byte port) was previously
// only rejected by the IPv4-sized "size <= 10" check, which let unwrap()
// read past the end of the buffer.
TORRENT_TEST(socks5_unwrap_reject_truncated_ipv6)
{
	// 11 bytes total: passes the size > 10 check, but is well short of the
	// 22 bytes required to read a v6 endpoint.
	std::array<char, 11> buf{};
	buf[3] = 0x04; // ATYP = IPv6

	aux::udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(!aux::socks5_unwrap(pack));
}

TORRENT_TEST(socks5_unwrap_reject_ipv6_one_short)
{
	// exactly 22 bytes is still a header with no payload. The implementation
	// requires size > 22 to leave room for at least one byte of payload.
	std::array<char, 22> buf{};
	buf[3] = 0x04;

	aux::udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(!aux::socks5_unwrap(pack));
}

// regression: a hostname-ATYP packet whose length byte reaches the end of
// the buffer leaves no room for the trailing 2-byte port. The previous
// bounds check only required the hostname to fit, so unwrap() would read
// 2 bytes past the buffer for the port.
TORRENT_TEST(socks5_unwrap_reject_hostname_missing_port)
{
	// 4 byte preamble + 1 byte LEN + 5 byte hostname = 10 bytes, no port.
	std::array<char, 10> buf{};
	buf[3] = 0x03; // ATYP = hostname
	buf[4] = 5; // LEN
	buf[5] = 'h';
	buf[6] = 'e';
	buf[7] = 'l';
	buf[8] = 'l';
	buf[9] = 'o';
	// Must be > 10 bytes to reach the hostname branch, so add one trailing
	// byte that is too few for a port.
	std::vector<char> v(buf.begin(), buf.end());
	v.push_back(0); // only one of the two needed port bytes

	aux::udp_socket::packet pack;
	pack.data = span<char>{v.data(), int(v.size())};

	TEST_CHECK(!aux::socks5_unwrap(pack));
}

TORRENT_TEST(socks5_unwrap_reject_hostname_overflow)
{
	// LEN claims more bytes than are present in the buffer
	std::array<char, 12> buf{};
	buf[3] = 0x03;
	buf[4] = 50; // LEN much larger than what's in the buffer

	aux::udp_socket::packet pack;
	pack.data = span<char>{buf.data(), int(buf.size())};

	TEST_CHECK(!aux::socks5_unwrap(pack));
}
