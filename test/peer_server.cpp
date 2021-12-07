/*

Copyright (c) 2013, 2015-2021, Arvid Norberg
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
#include "libtorrent/io_context.hpp"
#include "peer_server.hpp"
#include "test_utils.hpp"

#include <functional>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <memory>

using namespace lt;
using namespace std::placeholders;

struct peer_server
{
	lt::io_context m_ios;
	std::atomic<int> m_peer_requests{0};
	tcp::acceptor m_acceptor{m_ios};
	int m_port = 0;

	std::shared_ptr<std::thread> m_thread;

	peer_server()
	{
		error_code ec;
		m_acceptor.open(tcp::v4(), ec);
		if (ec)
		{
			std::printf("PEER Error opening peer listen socket: %s\n", ec.message().c_str());
			return;
		}

		m_acceptor.bind(tcp::endpoint(address_v4::any(), 0), ec);
		if (ec)
		{
			std::printf("PEER Error binding peer socket to port 0: %s\n", ec.message().c_str());
			return;
		}
		m_port = m_acceptor.local_endpoint(ec).port();
		if (ec)
		{
			std::printf("PEER Error getting local endpoint of peer socket: %s\n", ec.message().c_str());
			return;
		}
		m_acceptor.listen(10, ec);
		if (ec)
		{
			std::printf("PEER Error listening on peer socket: %s\n", ec.message().c_str());
			return;
		}

		std::printf("%s: PEER peer initialized on port %d\n", time_now_string().c_str(), m_port);

		m_thread = std::make_shared<std::thread>(&peer_server::thread_fun, this);
	}

	~peer_server()
	{
		error_code ignore;
		m_acceptor.cancel(ignore);
		m_acceptor.close(ignore);
		if (m_thread) m_thread->join();
	}

	int port() const { return m_port; }

	int num_hits() const { return m_peer_requests; }

	static void new_connection(error_code const& ec, error_code* ret, bool* done)
	{
		*ret = ec;
		*done = true;
	}

	void thread_fun()
	{
		for (;;)
		{
			error_code ec;
			tcp::endpoint from;
			tcp::socket socket(m_ios);
			std::condition_variable cond;
			bool done = false;
			m_acceptor.async_accept(socket, from, std::bind(&new_connection, _1, &ec, &done));
			while (!done)
			{
				m_ios.poll_one();
				m_ios.restart();
			}

			if (ec == boost::asio::error::operation_aborted
				|| ec == boost::asio::error::bad_descriptor) return;

			if (ec)
			{
				std::printf("PEER Error accepting connection on peer socket: %s\n", ec.message().c_str());
				return;
			}

			std::printf("%s: PEER incoming peer connection\n", time_now_string().c_str());
			++m_peer_requests;
			socket.close(ec);
		}
	}
};

namespace {

std::shared_ptr<peer_server> g_peer;

}

int start_peer()
{
	g_peer.reset(new peer_server);
	return g_peer->port();
}

// the number of DHT messages received
int num_peer_hits()
{
	if (g_peer) return g_peer->num_hits();
	return 0;
}

void stop_peer()
{
	g_peer.reset();
}

