/*

Copyright (c) 2014, Arvid Norberg
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
#include "libtorrent/socket_io.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/aux_/time.hpp"
#include "udp_tracker.hpp"

#include <boost/detail/atomic_count.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>

#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
#include <iostream>
#endif

using namespace libtorrent;

struct udp_tracker
{

	boost::asio::io_service m_ios;
	boost::detail::atomic_count m_udp_announces;
	udp::socket m_socket;
	int m_port;

	boost::shared_ptr<libtorrent::thread> m_thread;

	void on_udp_receive(error_code const& ec, size_t bytes_transferred, udp::endpoint* from, char* buffer, int size)
	{
		if (ec)
		{
			fprintf(stderr, "%s: UDP tracker, read failed: %s\n", aux::time_now_string(), ec.message().c_str());
			return;
		}

		if (bytes_transferred < 16)
		{
			fprintf(stderr, "%s: UDP message too short (from: %s)\n", aux::time_now_string(), print_endpoint(*from).c_str());
			return;
		}

		fprintf(stderr, "%s: UDP message %d bytes\n", aux::time_now_string(), int(bytes_transferred));

		char* ptr = buffer;
		detail::read_uint64(ptr);
		boost::uint32_t action = detail::read_uint32(ptr);
		boost::uint32_t transaction_id = detail::read_uint32(ptr);

		error_code e;

		switch (action)
		{
			case 0: // connect

				fprintf(stderr, "%s: UDP connect from %s\n", aux::time_now_string()
					, print_endpoint(*from).c_str());
				ptr = buffer;
				detail::write_uint32(0, ptr); // action = connect
				detail::write_uint32(transaction_id, ptr); // transaction_id
				detail::write_uint64(10, ptr); // connection_id
				m_socket.send_to(asio::buffer(buffer, 16), *from, 0, e);
				if (e) fprintf(stderr, "%s: UDP send_to failed. ERROR: %s\n"
					, aux::time_now_string(), e.message().c_str());
				else fprintf(stderr, "%s: UDP sent response to: %s\n"
					, aux::time_now_string(), print_endpoint(*from).c_str());
				break;

			case 1: // announce

				++m_udp_announces;
				fprintf(stderr, "%s: UDP announce [%d]\n", aux::time_now_string()
					, int(m_udp_announces));
				ptr = buffer;
				detail::write_uint32(1, ptr); // action = announce
				detail::write_uint32(transaction_id, ptr); // transaction_id
				detail::write_uint32(1800, ptr); // interval
				detail::write_uint32(1, ptr); // incomplete
				detail::write_uint32(1, ptr); // complete
				// 0 peers
				m_socket.send_to(asio::buffer(buffer, 20), *from, 0, e);
				if (e) fprintf(stderr, "%s: UDP send_to failed. ERROR: %s\n"
					, aux::time_now_string(), e.message().c_str());
				else fprintf(stderr, "%s: UDP sent response to: %s\n"
					, aux::time_now_string(), print_endpoint(*from).c_str());
				break;
			case 2:
				// ignore scrapes
				fprintf(stderr, "%s: UDP scrape (ignored)\n", aux::time_now_string());
				break;
			default:
				fprintf(stderr, "%s: UDP unknown message: %d\n", aux::time_now_string()
					, action);
				break;
		}

		m_socket.async_receive_from(
			asio::buffer(buffer, size), *from, 0
			, boost::bind(&udp_tracker::on_udp_receive, this, _1, _2, from, buffer, size));
	}

	udp_tracker()
		: m_udp_announces(0)
		, m_socket(m_ios)
		, m_port(0)
	{
		error_code ec;
		m_socket.open(udp::v4(), ec);
		if (ec)
		{
			fprintf(stderr, "UDP Error opening listen UDP tracker socket: %s\n", ec.message().c_str());
			return;
		}

		m_socket.bind(udp::endpoint(address_v4::any(), 0), ec);
		if (ec)
		{
			fprintf(stderr, "UDP Error binding UDP tracker socket to port 0: %s\n", ec.message().c_str());
			return;
		}
		m_port = m_socket.local_endpoint(ec).port();
		if (ec)
		{
			fprintf(stderr, "UDP Error getting local endpoint of UDP tracker socket: %s\n", ec.message().c_str());
			return;
		}

		fprintf(stderr, "%s: UDP tracker initialized on port %d\n", aux::time_now_string(), m_port);

		m_thread.reset(new thread(boost::bind(&udp_tracker::thread_fun, this)));
	}

	~udp_tracker()
	{
		m_socket.cancel();
		m_socket.close();
		if (m_thread) m_thread->join();
	}

	int port() const { return m_port; }

	int num_hits() const { return m_udp_announces; }

	static void incoming_packet(error_code const& ec, size_t bytes_transferred, size_t *ret, error_code* error, bool* done)
	{
		*ret = bytes_transferred;
		*error = ec;
		*done = true;
	}

	void thread_fun()
	{
		char buffer[2000];
	
		error_code ec;
		udp::endpoint from;
		m_socket.async_receive_from(
			asio::buffer(buffer, sizeof(buffer)), from, 0
			, boost::bind(&udp_tracker::on_udp_receive, this, _1, _2, &from, &buffer[0], sizeof(buffer)));

		m_ios.run(ec);

		if (ec)
		{
			fprintf(stderr, "UDP Error running UDP tracker service: %s\n", ec.message().c_str());
			return;
		}

		fprintf(stderr, "UDP exiting UDP tracker thread\n");
	}
};

boost::shared_ptr<udp_tracker> g_udp_tracker;

int start_udp_tracker()
{
	g_udp_tracker.reset(new udp_tracker);
	return g_udp_tracker->port();
}

// the number of UDP tracker announces received
int num_udp_announces()
{
	if (g_udp_tracker) return g_udp_tracker->num_hits();
	return 0;
}

void stop_udp_tracker()
{
	fprintf(stderr, "%s: UDP stop_udp_tracker()\n", aux::time_now_string());
	g_udp_tracker.reset();
	fprintf(stderr, "%s: UDP stop_udp_tracker() done\n", aux::time_now_string());
}

