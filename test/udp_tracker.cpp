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

#include "libtorrent/bencode.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/broadcast_socket.hpp" // for is_v6
#include "udp_tracker.hpp"
#include "test_utils.hpp"

#include <functional>
#include <thread>
#include <atomic>
#include <memory>

using namespace lt;
using namespace std::placeholders;

struct udp_tracker
{

	lt::io_service m_ios;
	std::atomic<int> m_udp_announces{0};
	udp::socket m_socket{m_ios};
	int m_port = 0;
	bool m_abort = false;

	std::shared_ptr<std::thread> m_thread;

	void on_udp_receive(error_code const& ec, size_t const bytes_transferred
		, udp::endpoint* from, char* buffer, std::size_t const size)
	{
		if (ec)
		{
			std::printf("%s: UDP tracker, read failed: %s\n", time_now_string(), ec.message().c_str());
			return;
		}

		if (bytes_transferred < 16)
		{
			std::printf("%s: UDP message too short (from: %s)\n", time_now_string(), print_endpoint(*from).c_str());
			return;
		}

		if (m_abort)
		{
			return;
		}

		std::printf("%s: UDP message %d bytes\n", time_now_string(), int(bytes_transferred));

		char* ptr = buffer;
		detail::read_uint64(ptr);
		std::uint32_t const action = detail::read_uint32(ptr);
		std::uint32_t const transaction_id = detail::read_uint32(ptr);

		error_code e;

		switch (action)
		{
			case 0: // connect

				if (bytes_transferred < 16)
				{
					std::printf("invalid connect message: %d Bytes, expected 16 Bytes\n"
						, int(bytes_transferred));
					return;
				}
				std::printf("%s: UDP connect from %s\n", time_now_string()
					, print_endpoint(*from).c_str());
				ptr = buffer;
				detail::write_uint32(0, ptr); // action = connect
				detail::write_uint32(transaction_id, ptr); // transaction_id
				detail::write_uint64(10, ptr); // connection_id
				m_socket.send_to(boost::asio::buffer(buffer, 16), *from, 0, e);
				if (e) std::printf("%s: UDP send_to failed. ERROR: %s\n"
					, time_now_string(), e.message().c_str());
				else std::printf("%s: UDP sent response to: %s\n"
					, time_now_string(), print_endpoint(*from).c_str());
				break;

			case 1: // announce

				if (bytes_transferred < 84)
				{
					std::printf("invalid announce message: %d Bytes, expected 84 Bytes\n"
						, int(bytes_transferred));
					return;
				}

				++m_udp_announces;
				std::printf("%s: UDP announce [%d]\n", time_now_string()
					, int(m_udp_announces));
				ptr = buffer;
				detail::write_uint32(1, ptr); // action = announce
				detail::write_uint32(transaction_id, ptr); // transaction_id
				detail::write_uint32(1800, ptr); // interval
				detail::write_uint32(1, ptr); // incomplete
				detail::write_uint32(1, ptr); // complete
				// 1 peers
				if (is_v6(*from))
				{
					detail::write_uint32(0, ptr);
					detail::write_uint32(0, ptr);
					detail::write_uint32(0, ptr);
					detail::write_uint8(0, ptr);
					detail::write_uint8(0, ptr);
					detail::write_uint8(0, ptr);
					detail::write_uint8(1, ptr);
					detail::write_uint16(1337, ptr);
				}
				else
				{
					detail::write_uint8(127, ptr);
					detail::write_uint8(0, ptr);
					detail::write_uint8(0, ptr);
					detail::write_uint8(2, ptr);
					detail::write_uint16(1337, ptr);
				}
				m_socket.send_to(boost::asio::buffer(buffer
					, static_cast<std::size_t>(ptr - buffer)), *from, 0, e);
				if (e) std::printf("%s: UDP send_to failed. ERROR: %s\n"
					, time_now_string(), e.message().c_str());
				else std::printf("%s: UDP sent response to: %s\n"
					, time_now_string(), print_endpoint(*from).c_str());
				break;
			case 2:
				// ignore scrapes
				std::printf("%s: UDP scrape (ignored)\n", time_now_string());
				break;
			default:
				std::printf("%s: UDP unknown message: %d\n", time_now_string()
					, action);
				break;
		}

		m_socket.async_receive_from(
			boost::asio::buffer(buffer, size), *from, 0
			, std::bind(&udp_tracker::on_udp_receive, this, _1, _2, from, buffer, size));
	}

	explicit udp_tracker(address iface)
	{
		error_code ec;
		m_socket.open(iface.is_v4() ? udp::v4() : udp::v6(), ec);
		if (ec)
		{
			std::printf("UDP Error opening listen UDP tracker socket: %s\n", ec.message().c_str());
			return;
		}

		m_socket.bind(udp::endpoint(iface, 0), ec);
		if (ec)
		{
			std::printf("UDP Error binding UDP tracker socket to port 0: %s\n", ec.message().c_str());
			return;
		}
		m_port = m_socket.local_endpoint(ec).port();
		if (ec)
		{
			std::printf("UDP Error getting local endpoint of UDP tracker socket: %s\n", ec.message().c_str());
			return;
		}

		std::printf("%s: UDP tracker [%p] initialized on port %d\n"
			, time_now_string(), static_cast<void*>(this), m_port);

		m_thread = std::make_shared<std::thread>(&udp_tracker::thread_fun, this);
	}

	void stop()
	{
		std::printf("%s: UDP tracker [%p], stop\n", time_now_string()
			, static_cast<void*>(this));
		m_abort = true;
		m_socket.cancel();
		m_socket.close();
	}

	~udp_tracker()
	{
		std::printf("%s: UDP tracker [%p], ~udp_tracker\n"
			, time_now_string(), static_cast<void*>(this));
		m_ios.post(std::bind(&udp_tracker::stop, this));
		if (m_thread) m_thread->join();
	}

	int port() const { return m_port; }

	int num_hits() const { return m_udp_announces; }

	void thread_fun()
	{
		char buffer[2000];

		error_code ec;
		udp::endpoint from;
		m_socket.async_receive_from(
			boost::asio::buffer(buffer, int(sizeof(buffer))), from, 0
			, std::bind(&udp_tracker::on_udp_receive, this, _1, _2, &from, &buffer[0], int(sizeof(buffer))));

		m_ios.run(ec);

		if (ec)
		{
			std::printf("UDP Error running UDP tracker service: %s\n", ec.message().c_str());
			return;
		}

		std::printf("UDP exiting UDP tracker [%p] thread\n", static_cast<void*>(this));
	}
};

namespace {
std::shared_ptr<udp_tracker> g_udp_tracker;
}

int start_udp_tracker(address iface)
{
	TORRENT_ASSERT(!g_udp_tracker);
	g_udp_tracker.reset(new udp_tracker(iface));
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
	g_udp_tracker.reset();
}

