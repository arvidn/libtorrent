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
#include "test.hpp"
#include "simulator/simulator.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/torrent_info.hpp"

using namespace sim;

namespace lt = libtorrent;

struct fake_peer
{
	fake_peer(simulation& sim, char const* ip)
		: m_ios(sim, asio::ip::address::from_string(ip))
		, m_acceptor(m_ios)
		, m_in_socket(m_ios)
		, m_out_socket(m_ios)
		, m_tripped(false)
	{
		boost::system::error_code ec;
		m_acceptor.open(asio::ip::tcp::v4(), ec);
		TEST_CHECK(!ec);
		m_acceptor.bind(asio::ip::tcp::endpoint(asio::ip::address_v4::any(), 6881), ec);
		TEST_CHECK(!ec);
		m_acceptor.listen(10, ec);
		TEST_CHECK(!ec);

		m_acceptor.async_accept(m_in_socket, [&] (boost::system::error_code const& ec)
		{
			// TODO: ideally we would kick off a read on the socket to verify that
			// we received a bittorrent handshake
			if (!ec) m_tripped = true;
		});
	}

	void close()
	{
		m_acceptor.close();
		m_in_socket.close();
		m_out_socket.close();
	}

	void connect_to(asio::ip::tcp::endpoint ep, lt::sha1_hash const& ih)
	{
		using namespace std::placeholders;

		boost::system::error_code ec;
		m_out_socket.async_connect(ep, std::bind(&fake_peer::write_handshake
			, this, _1, ih));
	}

	bool tripped() const { return m_tripped; }

private:

	void write_handshake(boost::system::error_code const& ec, lt::sha1_hash ih)
	{
		asio::ip::tcp::endpoint ep = m_out_socket.remote_endpoint();
		printf("fake_peer::connect (%s) -> (%d) %s\n"
			, lt::print_endpoint(ep).c_str(), ec.value()
			, ec.message().c_str());
		static char const handshake[]
		= "\x13" "BitTorrent protocol\0\0\0\0\0\0\0\x04"
			"                    " // space for info-hash
			"aaaaaaaaaaaaaaaaaaaa" // peer-id
			"\0\0\0\x01\x02"; // interested
		int const len = sizeof(handshake) - 1;
		memcpy(m_out_buffer, handshake, len);
		memcpy(&m_out_buffer[28], ih.data(), 20);

		asio::async_write(m_out_socket, asio::const_buffers_1(&m_out_buffer[0]
			, len), [=](boost::system::error_code const& ec, size_t bytes_transferred)
		{
			printf("fake_peer::write_handshake(%s) -> (%d) %s\n"
				, lt::print_endpoint(ep).c_str(), ec.value()
				, ec.message().c_str());
			this->m_out_socket.close();
		});
	}

	char m_out_buffer[300];

	asio::io_service m_ios;
	asio::ip::tcp::acceptor m_acceptor;
	asio::ip::tcp::socket m_in_socket;
	asio::ip::tcp::socket m_out_socket;
	bool m_tripped;
};

inline void add_fake_peers(lt::torrent_handle h)
{
	// add the fake peers
	for (int i = 0; i < 5; ++i)
	{
		char ep[30];
		snprintf(ep, sizeof(ep), "60.0.0.%d", i);
		h.connect_peer(lt::tcp::endpoint(
			lt::address_v4::from_string(ep), 6881));
	}
}

inline void check_tripped(std::array<fake_peer*, 5>& test_peers, std::array<bool, 5> expected)
{
	int idx = 0;
	for (auto p : test_peers)
	{
		TEST_EQUAL(p->tripped(), expected[idx]);
		++idx;
	}
}

#endif

