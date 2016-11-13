/*

Copyright (c) 2013, Arvid Norberg
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

#include "libtorrent/bencode.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/time.hpp"
#include "dht_server.hpp"
#include "test_utils.hpp"

#include <boost/detail/atomic_count.hpp>

#if TORRENT_USE_IOSTREAM
#include <iostream>
#endif

#include <thread>
#include <functional>
#include <memory>

using namespace libtorrent;
using namespace std::placeholders;

struct dht_server
{
	libtorrent::io_service m_ios;
	boost::detail::atomic_count m_dht_requests;
	udp::socket m_socket;
	int m_port;

	std::shared_ptr<std::thread> m_thread;

	dht_server()
		: m_dht_requests(0)
		, m_socket(m_ios)
		, m_port(0)
	{
		error_code ec;
		m_socket.open(udp::v4(), ec);
		if (ec)
		{
			std::printf("Error opening listen DHT socket: %s\n", ec.message().c_str());
			return;
		}

		m_socket.bind(udp::endpoint(address_v4::any(), 0), ec);
		if (ec)
		{
			std::printf("Error binding DHT socket to port 0: %s\n", ec.message().c_str());
			return;
		}
		m_port = m_socket.local_endpoint(ec).port();
		if (ec)
		{
			std::printf("Error getting local endpoint of DHT socket: %s\n", ec.message().c_str());
			return;
		}

		std::printf("%s: DHT initialized on port %d\n", time_now_string(), m_port);

		m_thread = std::make_shared<std::thread>(&dht_server::thread_fun, this);
	}

	~dht_server()
	{
		m_socket.cancel();
		m_socket.close();
		if (m_thread) m_thread->join();
	}

	int port() const { return m_port; }

	int num_hits() const { return m_dht_requests; }

	static void incoming_packet(error_code const& ec, size_t bytes_transferred
		, size_t *ret, error_code* error, bool* done)
	{
		*ret = bytes_transferred;
		*error = ec;
		*done = true;
	}

	void thread_fun()
	{
		char buffer[2000];

		for (;;)
		{
			error_code ec;
			udp::endpoint from;
			size_t bytes_transferred;
			bool done = false;
			m_socket.async_receive_from(
				boost::asio::buffer(buffer, sizeof(buffer)), from, 0
				, std::bind(&incoming_packet, _1, _2, &bytes_transferred, &ec, &done));
			while (!done)
			{
				m_ios.poll_one();
				m_ios.reset();
			}

			if (ec == boost::asio::error::operation_aborted
				|| ec == boost::asio::error::bad_descriptor) return;

			if (ec)
			{
				std::printf("Error receiving on DHT socket: %s\n", ec.message().c_str());
				return;
			}

			try
			{
				entry msg = bdecode(buffer, buffer + bytes_transferred);

#if TORRENT_USE_IOSTREAM
				std::cerr << msg << std::endl;
#endif
				++m_dht_requests;
			}
			catch (std::exception const& e)
			{
				std::printf("failed to decode DHT message: %s\n", e.what());
			}
		}
	}
};

std::shared_ptr<dht_server> g_dht;

int start_dht()
{
	g_dht.reset(new dht_server);
	return g_dht->port();
}

// the number of DHT messages received
int num_dht_hits()
{
	if (g_dht) return g_dht->num_hits();
	return 0;
}

void stop_dht()
{
	g_dht.reset();
}

