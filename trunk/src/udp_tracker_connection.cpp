/*

Copyright (c) 2003, Arvid Norberg
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

#include <vector>
#include <iostream>
#include <cctype>
#include <iomanip>
#include <sstream>

#include "zlib.h"

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/udp_tracker_connection.hpp"
#include "libtorrent/io.hpp"

namespace
{
	enum
	{
		udp_connection_retries = 4,
		udp_announce_retries = 15,
		udp_connect_timeout = 15,
		udp_announce_timeout = 10,
		udp_buffer_size = 2048
	};
}

namespace libtorrent
{

	udp_tracker_connection::udp_tracker_connection(
		tracker_request const& req
		, std::string const& hostname
		, unsigned short port
		, request_callback* c
		, const http_settings& stn)
		: tracker_connection(c)
		, m_request_time(boost::posix_time::second_clock::local_time())
		, m_request(req)
		, m_transaction_id(0)
		, m_connection_id(0)
		, m_settings(stn)
		, m_attempts(0)
	{
		// TODO: this is a problem. DNS-lookup is blocking!
		// (may block up to 5 seconds)
		address a(hostname, port);
		m_socket.reset(new socket(socket::udp, false));
		m_socket->connect(a);

		send_udp_connect();
	}

	bool udp_tracker_connection::send_finished() const
	{
		using namespace boost::posix_time;

		time_duration d = second_clock::local_time() - m_request_time;
		return (m_transaction_id != 0
			&& m_connection_id != 0)
			|| d > seconds(m_settings.tracker_timeout);
	}

	bool udp_tracker_connection::tick()
	{
		using namespace boost::posix_time;

		time_duration d = second_clock::local_time() - m_request_time;
		if (m_connection_id == 0
			&& d > seconds(udp_connect_timeout))
		{
			if (m_attempts >= udp_connection_retries)
			{
				if (requester()) requester()->tracker_request_timed_out();
				return true;
			}
			send_udp_connect();
			return false;
		}

		if (m_connection_id != 0
			&& d > seconds(udp_announce_timeout))
		{
			if (m_attempts >= udp_announce_retries)
			{
				if (requester()) requester()->tracker_request_timed_out();
				return true;
			}

			send_udp_announce();
			return false;
		}

		char buf[udp_buffer_size];
		int ret = m_socket->receive(buf, udp_buffer_size);

		// if there was nothing to receive, return
		if (ret == 0) return false;
		if (ret < 0)
		{
			socket::error_code err = m_socket->last_error();
			if (err == socket::would_block) return false;
			throw network_error(m_socket->last_error());
		}
		if (ret == udp_buffer_size)
		{
			if (requester())
				requester()->tracker_request_error(-1, "tracker reply too big");
			return true;
		}

		if (m_connection_id == 0)
		{
			return parse_connect_response(buf, ret);
		}
		else
		{
			return parse_announce_response(buf, ret);
		}
	}

	void udp_tracker_connection::send_udp_announce()
	{
		if (m_transaction_id == 0)
			m_transaction_id = rand() | (rand() << 16);

		char buf[94];
		char* ptr = buf;

		// connection_id
		detail::write_int64(m_connection_id, ptr);
		// action (announce)
		detail::write_int32(announce, ptr);
		// transaction_id
		detail::write_int32(m_transaction_id, ptr);
		// info_hash
		std::copy(m_request.info_hash.begin(), m_request.info_hash.end(), ptr);
		ptr += 20;
		// peer_id
		std::copy(m_request.id.begin(), m_request.id.end(), ptr);
		ptr += 20;
		// downloaded
		detail::write_int64(m_request.downloaded, ptr);
		// left
		detail::write_int64(m_request.left, ptr);
		// uploaded
		detail::write_int64(m_request.uploaded, ptr);
		// event
		detail::write_int32(m_request.event, ptr);
		// ip address
		detail::write_int32(0, ptr);
		// num_want
		detail::write_int32(-1, ptr);
		// port
		detail::write_uint16(m_request.listen_port, ptr);
	
		m_socket->send(buf, 94);
		m_request_time = boost::posix_time::second_clock::local_time();
		++m_attempts;
	}

	void udp_tracker_connection::send_udp_connect()
	{
		char send_buf[16];
		char* ptr = send_buf;

		if (m_transaction_id == 0)
			m_transaction_id = rand() | (rand() << 16);

		// connection_id
		detail::write_int64(0, ptr);
		// action (connect)
		detail::write_int32(connect, ptr);
		// transaction_id
		detail::write_int32(m_transaction_id, ptr);

		m_socket->send(send_buf, 16);
		m_request_time = boost::posix_time::second_clock::local_time();
		++m_attempts;
	}

	bool udp_tracker_connection::parse_announce_response(const char* buf, int len)
	{
		assert(buf != 0);
		assert(len > 0);

		if (len < 8)
		{
#ifndef NDEBUG
			if (requester())
				requester()->debug_log("udp_tracker_connection: "
				"got a message with size < 8, ignoring");
#endif
			return false;
		}

		int action = detail::read_int32(buf);
		int transaction = detail::read_int32(buf);
		if (transaction != m_transaction_id)
		{
			return false;
		}

		if (action == error)
		{
			if (requester())
				requester()->tracker_request_error(-1, std::string(buf, buf + len - 8));
			return true;
		}
		if (action != announce) return false;

		if (len < 12)
		{
#ifndef NDEBUG
			if (requester())
				requester()->debug_log("udp_tracker_connection: "
				"got a message with size < 12, ignoring");
#endif
			return false;
		}
		int interval = detail::read_int32(buf);
		int num_peers = (len - 12) / 6;
		if ((len - 12) % 6 != 0)
		{
			if (requester())
				requester()->tracker_request_error(-1, "invalid tracker response");
			return true;
		}

		if (requester() == 0) return true;

		std::vector<peer_entry> peer_list;
		for (int i = 0; i < num_peers; ++i)
		{
			peer_entry e;
			std::stringstream s;
			s << (int)detail::read_uint8(buf) << ".";
			s << (int)detail::read_uint8(buf) << ".";
			s << (int)detail::read_uint8(buf) << ".";
			s << (int)detail::read_uint8(buf);
			e.ip = s.str();
			e.port = detail::read_uint16(buf);
			e.id.clear();
			peer_list.push_back(e);
		}

		requester()->tracker_response(peer_list, interval);
		return true;
	}

	bool udp_tracker_connection::parse_connect_response(const char* buf, int len)
	{
		assert(buf != 0);
		assert(len > 0);

		if (len < 8)
		{
#ifndef NDEBUG
			if (requester())
				requester()->debug_log("udp_tracker_connection: "
				"got a message with size < 8, ignoring");
#endif
			return false;
		}
		const char* ptr = buf;
		int action = detail::read_int32(ptr);
		int transaction = detail::read_int32(ptr);

		if (action == error)
		{
			if (requester())
				requester()->tracker_request_error(-1, std::string(ptr, buf + len));
			return true;
		}
		if (action != connect) return false;
		if (m_transaction_id != transaction)
		{
#ifndef NDEBUG
			if (requester())
				requester()->debug_log("udp_tracker_connection: "
				"got a message with incorrect transaction id, ignoring");
#endif
			return false;
		}

		if (len < 16)
		{
#ifndef NDEBUG
			if (requester())
				requester()->debug_log("udp_tracker_connection: "
				"got a connection message size < 16, ignoring");
#endif
			return false;
		}
		// reset transaction
		m_transaction_id = 0;
		m_attempts = 0;
		m_connection_id = detail::read_int64(ptr);

		send_udp_announce();
		return false;
	}


}
