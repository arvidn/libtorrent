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
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/io_service.hpp"
#include "peer_server.hpp"
#include "test_utils.hpp"

#include <boost/detail/atomic_count.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>

using namespace libtorrent;

struct peer_server
{

	libtorrent::io_service m_ios;
	boost::detail::atomic_count m_peer_requests;
	tcp::acceptor m_acceptor;
	int m_port;

	boost::shared_ptr<libtorrent::thread> m_thread;

	peer_server()
		: m_peer_requests(0)
		, m_acceptor(m_ios)
		, m_port(0)
	{
		error_code ec;
		m_acceptor.open(tcp::v4(), ec);
		if (ec)
		{
			fprintf(stderr, "PEER Error opening peer listen socket: %s\n", ec.message().c_str());
			return;
		}

		m_acceptor.bind(tcp::endpoint(address_v4::any(), 0), ec);
		if (ec)
		{
			fprintf(stderr, "PEER Error binding peer socket to port 0: %s\n", ec.message().c_str());
			return;
		}
		m_port = m_acceptor.local_endpoint(ec).port();
		if (ec)
		{
			fprintf(stderr, "PEER Error getting local endpoint of peer socket: %s\n", ec.message().c_str());
			return;
		}
		m_acceptor.listen(10, ec);
		if (ec)
		{
			fprintf(stderr, "PEER Error listening on peer socket: %s\n", ec.message().c_str());
			return;
		}

		fprintf(stderr, "%s: PEER peer initialized on port %d\n", time_now_string(), m_port);

		m_thread.reset(new thread(boost::bind(&peer_server::thread_fun, this)));
	}

	~peer_server()
	{
		m_acceptor.cancel();
		m_acceptor.close();
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
			condition_variable cond;
			bool done = false;
			m_acceptor.async_accept(socket, from, boost::bind(&new_connection, _1, &ec, &done));
			while (!done)
			{
				m_ios.poll_one();
				m_ios.reset();
			}

			if (ec == boost::asio::error::operation_aborted
				|| ec == boost::asio::error::bad_descriptor) return;

			if (ec)
			{
				fprintf(stderr, "PEER Error accepting connection on peer socket: %s\n", ec.message().c_str());
				return;
			}

			fprintf(stderr, "%s: PEER incoming peer connection\n", time_now_string());
			++m_peer_requests;
			socket.close(ec);
		}
	}
};

boost::shared_ptr<peer_server> g_peer;

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

