/*

Copyright (c) 2015, Arvid Norberg
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

#ifndef SIMULATION_FAKE_PEER_HPP
#define SIMULATION_FAKE_PEER_HPP

#include <array>
#include <cstdio> // for snprintf

#include <functional>
#include "test.hpp"
#include "simulator/simulator.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/bdecode.hpp"

using namespace sim;


struct fake_peer
{
	fake_peer(simulation& sim, char const* ip)
		: m_ios(sim, asio::ip::address::from_string(ip))
	{
		boost::system::error_code ec;
		m_acceptor.open(asio::ip::tcp::v4(), ec);
		TEST_CHECK(!ec);
		m_acceptor.bind(asio::ip::tcp::endpoint(asio::ip::address_v4::any(), 6881), ec);
		TEST_CHECK(!ec);
		m_acceptor.listen(10, ec);
		TEST_CHECK(!ec);

		m_acceptor.async_accept(m_socket, [&] (boost::system::error_code const& ec)
		{
			using namespace std::placeholders;
			if (ec) return;

			asio::async_read(m_socket, asio::mutable_buffers_1(&m_out_buffer[0], 68)
				, std::bind(&fake_peer::read_handshake, this, _1, _2));

			m_accepted = true;
		});
	}

	void close()
	{
		m_acceptor.close();
		m_socket.close();
	}

	void connect_to(asio::ip::tcp::endpoint ep, lt::sha1_hash const& ih)
	{
		using namespace std::placeholders;

		m_info_hash = ih;

		std::printf("fake_peer::connect_to(%s)\n", lt::print_endpoint(ep).c_str());
		m_socket.async_connect(ep, std::bind(&fake_peer::write_handshake
			, this, _1, ih));
	}

	bool accepted() const { return m_accepted; }
	bool connected() const { return m_connected; }
	bool disconnected() const { return m_disconnected; }

	void send_interested()
	{
		m_send_buffer.resize(m_send_buffer.size() + 5);
		char* ptr = m_send_buffer.data() + m_send_buffer.size() - 5;

		lt::detail::write_uint32(1, ptr);
		lt::detail::write_uint8(2, ptr);
	}

	void send_bitfield(std::vector<bool> const& pieces)
	{
		int const bytes = (int(pieces.size()) + 7) / 8;
		m_send_buffer.resize(m_send_buffer.size() + 5 + bytes);
		char* ptr = m_send_buffer.data() + m_send_buffer.size() - 5 - bytes;

		lt::detail::write_uint32(1 + bytes, ptr);
		lt::detail::write_uint8(5, ptr);

		std::uint8_t b = 0;
		int cnt = 7;
		for (std::vector<bool>::const_iterator i = pieces.begin()
			, end(pieces.end()); i != end; ++i)
		{
			if (*i) b |= 1 << cnt;
			--cnt;
			if (cnt < 0)
			{
				lt::detail::write_uint8(b, ptr);
				b = 0;
				cnt = 7;
			}
		}
		lt::detail::write_uint8(b, ptr);
	}

private:

	void write_handshake(boost::system::error_code const& ec
		, lt::sha1_hash ih)
	{
		using namespace std::placeholders;

		asio::ip::tcp::endpoint const ep = m_socket.remote_endpoint();
		std::printf("fake_peer::connect(%s) -> (%d) %s\n"
			, lt::print_endpoint(ep).c_str()
			, ec.value(), ec.message().c_str());
		if (ec) return;

		static char const handshake[]
		= "\x13" "BitTorrent protocol\0\0\0\0\0\0\0\x04"
			"                    " // space for info-hash
			"aaaaaaaaaaaaaaaaaaaa"; // peer-id
		int const len = sizeof(handshake) - 1;
		memcpy(m_out_buffer, handshake, len);
		memcpy(&m_out_buffer[28], ih.data(), 20);

		asio::async_write(m_socket, asio::const_buffers_1(&m_out_buffer[0]
			, len), [this, ep](boost::system::error_code const& ec
			, size_t /* bytes_transferred */)
		{
			std::printf("fake_peer::write_handshake(%s) -> (%d) %s\n"
				, lt::print_endpoint(ep).c_str(), ec.value()
				, ec.message().c_str());
			if (!m_send_buffer.empty())
			{
				asio::async_write(m_socket, asio::const_buffers_1(
					m_send_buffer.data(), m_send_buffer.size())
					, std::bind(&fake_peer::write_send_buffer, this, _1, _2));
			}
			else
			{
				asio::async_read(m_socket, asio::mutable_buffers_1(&m_out_buffer[0], 68)
					, std::bind(&fake_peer::read_handshake, this, _1, _2));
			}
		});
	}

	void read_handshake(lt::error_code const& ec, size_t /* bytes_transferred */)
	{
		using namespace std::placeholders;

		std::printf("fake_peer::read_handshake -> (%d) %s\n"
			, ec.value(), ec.message().c_str());
		if (ec)
		{
			m_socket.close();
			return;
		}

		if (memcmp(&m_out_buffer[0], "\x13" "BitTorrent protocol", 20) != 0)
		{
			std::printf("  invalid protocol specifier\n");
			m_socket.close();
			return;
		}

		// if this peer accepted an incoming connection, we don't know what the
		// info hash is supposed to be
		if (!m_info_hash.is_all_zeros()
			&& memcmp(&m_out_buffer[28], m_info_hash.data(), 20) != 0)
		{
			std::printf("  invalid info hash\n");
			m_socket.close();
			return;
		}

		m_connected = true;

		// keep reading until we receie EOF, then set m_disconnected = true
		m_socket.async_read_some(asio::mutable_buffers_1(&m_out_buffer[0]
			, sizeof(m_out_buffer))
			, std::bind(&fake_peer::on_read, this, _1, _2));
	}

	void on_read(lt::error_code const& ec, size_t /* bytes_transferred */)
	{
		using namespace std::placeholders;

		std::printf("fake_peer::on_read -> (%d) %s\n"
			, ec.value(), ec.message().c_str());
		if (ec)
		{
			std::printf("  closing\n");
			m_disconnected = true;
			m_socket.close();
			return;
		}

		m_socket.async_read_some(asio::mutable_buffers_1(&m_out_buffer[0]
			, sizeof(m_out_buffer))
			, std::bind(&fake_peer::on_read, this, _1, _2));
	}

	void write_send_buffer(boost::system::error_code const& ec
		, size_t /* bytes_transferred */)
	{
		using namespace std::placeholders;

		printf("fake_peer::write_send_buffer() -> (%d) %s\n"
			, ec.value(), ec.message().c_str());

		asio::async_read(m_socket, asio::mutable_buffers_1(&m_out_buffer[0], 68)
			, std::bind(&fake_peer::read_handshake, this, _1, _2));
	}

	char m_out_buffer[300];

	asio::io_service m_ios;
	asio::ip::tcp::acceptor m_acceptor{m_ios};
	asio::ip::tcp::socket m_socket{m_ios};
	lt::sha1_hash m_info_hash;

	// set to true if this peer received an incoming connection
	// if this is an outgoing connection, this will always be false
	bool m_accepted = false;

	// set to true if this peer completed a bittorrent handshake
	bool m_connected = false;

	// set to true if this peer has been disconnected by the other end
	bool m_disconnected = false;

	std::vector<char> m_send_buffer;
};

inline void add_fake_peer(lt::torrent_handle& h, int const i)
{
	char ep[30];
	std::snprintf(ep, sizeof(ep), "60.0.0.%d", i);
	h.connect_peer(lt::tcp::endpoint(
		lt::address_v4::from_string(ep), 6881));
}

inline void add_fake_peers(lt::torrent_handle& h, int const n = 5)
{
	// add the fake peers
	for (int i = 0; i < n; ++i)
	{
		add_fake_peer(h, i);
	}
}

struct udp_server
{
	udp_server(simulation& sim, char const* ip, int port
		, std::function<std::vector<char>(char const*, int)> handler)
		: m_ios(sim, asio::ip::address::from_string(ip))
		, m_handler(handler)
	{
		boost::system::error_code ec;
		m_socket.open(asio::ip::udp::v4(), ec);
		TEST_CHECK(!ec);
		m_socket.bind(asio::ip::udp::endpoint(asio::ip::address_v4::any()
			, static_cast<std::uint16_t>(port)), ec);
		TEST_CHECK(!ec);

		m_socket.non_blocking(true);

		std::printf("udp_server::async_read_some\n");
		using namespace std::placeholders;
		m_socket.async_receive_from(boost::asio::buffer(m_in_buffer)
			, m_from, 0, std::bind(&udp_server::on_read, this, _1, _2));
	}

	void close() { m_socket.close(); }

private:

	void on_read(boost::system::error_code const& ec, size_t bytes_transferred)
	{
		std::printf("udp_server::async_read_some callback. ec: %s transferred: %d\n"
			, ec.message().c_str(), int(bytes_transferred));
		if (ec) return;

		std::vector<char> send_buffer = m_handler(m_in_buffer.data(), int(bytes_transferred));

		if (!send_buffer.empty())
		{
			lt::error_code err;
			m_socket.send_to(boost::asio::buffer(send_buffer), m_from, 0, err);
			if (err)
			{
				std::printf("send_to FAILED: %s\n", err.message().c_str());
			}
			else
			{
				std::printf("udp_server responding with %d bytes\n"
					, int(send_buffer.size()));
			}
		}

		std::printf("udp_server::async_read_some\n");
		using namespace std::placeholders;
		m_socket.async_receive_from(boost::asio::buffer(m_in_buffer)
			, m_from, 0, std::bind(&udp_server::on_read, this, _1, _2));
	}

	std::array<char, 1500> m_in_buffer;

	asio::io_service m_ios;
	asio::ip::udp::socket m_socket{m_ios};
	asio::ip::udp::endpoint m_from;

	std::function<std::vector<char>(char const*, int)> m_handler;
};

struct fake_node : udp_server
{
	fake_node(simulation& sim, char const* ip, int port = 6881)
		: udp_server(sim, ip, port, [&](char const* incoming, int size)
		{
			lt::bdecode_node n;
			boost::system::error_code err;
			int const ret = bdecode(incoming, incoming + size, n, err, nullptr, 10, 200);
			TEST_EQUAL(ret, 0);

			m_incoming_packets.emplace_back(incoming, incoming + size);

			// TODO: ideally we would validate the DHT message
			m_tripped = true;
			return std::vector<char>();
		})
	{}

	bool tripped() const { return m_tripped; }

	std::vector<std::vector<char>> const& incoming_packets() const
	{ return m_incoming_packets; }

private:

	std::vector<std::vector<char>> m_incoming_packets;
	bool m_tripped = false;
};

template<unsigned long N>
void check_accepted(std::array<fake_peer*, N>& test_peers
	, std::array<bool, N> expected)
{
	int idx = 0;
	for (auto p : test_peers)
	{
		TEST_EQUAL(p->accepted(), expected[idx]);
		++idx;
	}
}

template<unsigned long N>
void check_connected(std::array<fake_peer*, N>& test_peers
	, std::array<bool, N> expected)
{
	int idx = 0;
	for (auto p : test_peers)
	{
		TEST_EQUAL(p->connected(), expected[idx]);
		++idx;
	}
}

template<unsigned long N>
void check_disconnected(std::array<fake_peer*, N>& test_peers
	, std::array<bool, N> expected)
{
	int idx = 0;
	for (auto p : test_peers)
	{
		TEST_EQUAL(p->disconnected(), expected[idx]);
		++idx;
	}
}

#endif

