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

#include "libtorrent/thread.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/time.hpp"

#include <boost/detail/atomic_count.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>

#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
#include <iostream>
#endif

using namespace libtorrent;

struct dht_server
{

	boost::asio::io_service m_ios;
	boost::detail::atomic_count m_dht_requests;
	udp::socket m_socket;
	int m_port;

	boost::shared_ptr<libtorrent::thread> m_thread;

	dht_server()
		: m_dht_requests(0)
		, m_socket(m_ios)
	{
		error_code ec;
		m_socket.open(udp::v4(), ec);
		if (ec)
		{
			fprintf(stderr, "Error opening listen DHT socket: %s\n", ec.message().c_str());
			return;
		}

		m_socket.bind(udp::endpoint(address_v4::any(), 0), ec);
		if (ec)
		{
			fprintf(stderr, "Error binding DHT socket to port 0: %s\n", ec.message().c_str());
			return;
		}
		m_port = m_socket.local_endpoint(ec).port();
		if (ec)
		{
			fprintf(stderr, "Error getting local endpoint of DHT socket: %s\n", ec.message().c_str());
			return;
		}

		fprintf(stderr, "%s: DHT initialized on port %d\n", time_now_string(), m_port);

		m_thread.reset(new thread(boost::bind(&dht_server::thread_fun, this)));
	}

	~dht_server()
	{
		m_socket.close();
		if (m_thread) m_thread->join();
	}

	int port() const { return m_port; }

	int num_hits() const { return m_dht_requests; }

	void thread_fun()
	{
		char buffer[2000];
	
		for (;;)
		{
			error_code ec;
			udp::endpoint from;
			int bytes_transferred = m_socket.receive_from(
				asio::buffer(buffer, sizeof(buffer)), from, 0, ec);
			if (ec == boost::asio::error::operation_aborted
				|| ec == boost::asio::error::bad_descriptor) return;

			if (ec)
			{
				fprintf(stderr, "Error receiving on DHT socket: %s\n", ec.message().c_str());
				return;
			}

			try
			{
				entry msg = bdecode(buffer, buffer + bytes_transferred);

#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
				std::cerr << msg << std::endl;
#endif
				++m_dht_requests;
			}
			catch (std::exception& e)
			{
				fprintf(stderr, "failed to decode DHT message: %s\n", e.what());
			}
		}
	}
};

boost::shared_ptr<dht_server> g_dht;

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
	fprintf(stderr, "%s: stop_dht()\n", time_now_string());
	g_dht.reset();
	fprintf(stderr, "%s: stop_dht() done\n", time_now_string());
}

