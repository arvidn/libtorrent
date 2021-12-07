/*

Copyright (c) 2013-2021, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/bencode.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/bdecode.hpp"
#include "dht_server.hpp"
#include "test_utils.hpp"

#if TORRENT_USE_IOSTREAM
#include <iostream>
#endif

#include <thread>
#include <atomic>
#include <functional>
#include <memory>

using namespace lt;
using namespace std::placeholders;

struct dht_server
{

	lt::io_context m_ios;
	std::atomic<int> m_dht_requests;
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

		std::printf("%s: DHT initialized on port %d\n", time_now_string().c_str(), m_port);

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
		std::array<char, 2000> buffer;

		for (;;)
		{
			error_code ec;
			udp::endpoint from;
			size_t bytes_transferred;
			bool done = false;
			m_socket.async_receive_from(
				boost::asio::buffer(buffer.data(), buffer.size()), from, 0
				, std::bind(&incoming_packet, _1, _2, &bytes_transferred, &ec, &done));
			while (!done)
			{
				m_ios.poll_one();
				m_ios.restart();
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
				entry msg = bdecode(span<char const>(buffer).first(int(bytes_transferred)));

#if TORRENT_USE_IOSTREAM
				std::cout << msg << std::endl;
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

namespace {
std::shared_ptr<dht_server> g_dht;
}

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

