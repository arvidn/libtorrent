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

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

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

using namespace boost::posix_time;
using boost::bind;
using boost::lexical_cast;

namespace libtorrent
{

	udp_tracker_connection::udp_tracker_connection(
		demuxer& d
		, tracker_manager& man
		, tracker_request const& req
		, std::string const& hostname
		, unsigned short port
		, boost::weak_ptr<request_callback> c
		, const http_settings& stn)
		: tracker_connection(c)
		, m_man(man)
		, m_name_lookup(d)
		, m_port(port)
		, m_req(req)
		, m_transaction_id(0)
		, m_connection_id(0)
		, m_settings(stn)
		, m_attempts(0)
	{
		m_socket.reset(new datagram_socket(d));
		m_name_lookup.async_by_name(m_host, hostname.c_str()
			, bind(&udp_tracker_connection::name_lookup, self(), _1));
	}

	void udp_tracker_connection::name_lookup(asio::error const& error)
	{
		if (error)
		{
			fail(-1, error.what());
			return;
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester()) requester().debug_log("udp tracker name lookup successful");
#endif

		m_target = udp::endpoint(m_port, m_host.address(0));
		if (has_requester()) requester().m_tracker_address
			= tcp::endpoint(m_port, m_host.address(0));
		m_socket->connect(m_target);
		send_udp_connect();
	}

	void udp_tracker_connection::fail(int code, char const* msg)
	{
		if (has_requester()) requester().tracker_request_error(
			m_req, code, msg);
		m_man.remove_request(this);
	}
	
	bool udp_tracker_connection::send_finished() const
	{
		using namespace boost::posix_time;

		time_duration d = second_clock::universal_time() - m_request_time;
		return (m_transaction_id != 0
			&& m_connection_id != 0)
			|| d > seconds(m_settings.tracker_timeout);
	}

	void udp_tracker_connection::send_udp_connect()
	{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester())
		{
			requester().debug_log("==> UDP_TRACKER_CONNECT ["
				+ lexical_cast<std::string>(m_req.info_hash) + "]");
		}
#endif

		char send_buf[16];
		char* ptr = send_buf;

		if (m_transaction_id == 0)
			m_transaction_id = rand() ^ (rand() << 16);

		// connection_id
		detail::write_uint32(0x417, ptr);
		detail::write_uint32(0x27101980, ptr);
		// action (connect)
		detail::write_int32(action_connect, ptr);
		// transaction_id
		detail::write_int32(m_transaction_id, ptr);

		m_socket->send(asio::buffer((void*)send_buf, 16), 0);
		m_request_time = second_clock::universal_time();
		++m_attempts;
		m_buffer.resize(udp_buffer_size);
		m_socket->async_receive_from(asio::buffer(m_buffer), 0, m_sender
			, boost::bind(&udp_tracker_connection::connect_response, self(), _1, _2));
	}

	void udp_tracker_connection::connect_response(asio::error const& error
		, std::size_t bytes_transferred)
	{
		if (error)
		{
			fail(-1, error.what());
			return;
		}

		if (m_target != m_sender)
		{
			// this packet was not received from the tracker
			m_socket->async_receive_from(asio::buffer(m_buffer), 0, m_sender
				, boost::bind(&udp_tracker_connection::connect_response, self(), _1, _2));
			return;
		}

		if (bytes_transferred >= udp_buffer_size)
		{
			fail(-1, "udp response too big");
			return;
		}

		if (bytes_transferred < 8)
		{
			fail(-1, "got a message with size < 8");
			return;
		}

		const char* ptr = &m_buffer[0];
		int action = detail::read_int32(ptr);
		int transaction = detail::read_int32(ptr);

		if (action == action_error)
		{
			fail(-1, std::string(ptr, bytes_transferred - 8).c_str());
			return;
		}

		if (action != action_connect)
		{
			fail(-1, "invalid action in connect reply");
			return;
		}

		if (m_transaction_id != transaction)
		{
			fail(-1, "incorrect transaction id");
			return;
		}

		if (bytes_transferred < 16)
		{
			fail(-1, "udp_tracker_connection: "
				"got a message with size < 16");
			return;
		}
		// reset transaction
		m_transaction_id = 0;
		m_attempts = 0;
		m_connection_id = detail::read_int64(ptr);

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester())
		{
			requester().debug_log("<== UDP_TRACKER_CONNECT_RESPONSE ["
				+ lexical_cast<std::string>(m_connection_id) + "]");
		}
#endif

		if (m_req.kind == tracker_request::announce_request)
			send_udp_announce();
		else if (m_req.kind == tracker_request::scrape_request)
			send_udp_scrape();
	}
	

	
/*
	bool udp_tracker_connection::tick()
	{
		using namespace boost::posix_time;

		time_duration d = second_clock::universal_time() - m_request_time;
		if (m_connection_id == 0
			&& d > seconds(udp_connect_timeout))
		{
			if (m_attempts >= udp_connection_retries)
			{
				if (has_requester())
					requester().tracker_request_timed_out(m_request);
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
				if (has_requester())
					requester().tracker_request_timed_out(m_request);
				return true;
			}

			if (m_request.kind == tracker_request::announce_request)
				send_udp_announce();
			else if (m_request.kind == tracker_request::scrape_request)
				send_udp_scrape();
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
			if (has_requester())
				requester().tracker_request_error(
					m_request, -1, "tracker reply too big");
			return true;
		}

		if (m_connection_id == 0)
		{
			return parse_connect_response(buf, ret);
		}
		else if (m_request.kind == tracker_request::announce_request)
		{
			return parse_announce_response(buf, ret);
		}
		else if (m_request.kind == tracker_request::scrape_request)
		{
			return parse_scrape_response(buf, ret);
		}

		assert(false);
		return false;
	}
*/
	void udp_tracker_connection::send_udp_announce()
	{
		if (m_transaction_id == 0)
			m_transaction_id = rand() ^ (rand() << 16);

		std::vector<char> buf;
		std::back_insert_iterator<std::vector<char> > out(buf);

		// connection_id
		detail::write_int64(m_connection_id, out);
		// action (announce)
		detail::write_int32(action_announce, out);
		// transaction_id
		detail::write_int32(m_transaction_id, out);
		// info_hash
		std::copy(m_req.info_hash.begin(), m_req.info_hash.end(), out);
		// peer_id
		std::copy(m_req.id.begin(), m_req.id.end(), out);
		// downloaded
		detail::write_int64(m_req.downloaded, out);
		// left
		detail::write_int64(m_req.left, out);
		// uploaded
		detail::write_int64(m_req.uploaded, out);
		// event
		detail::write_int32(m_req.event, out);
		// ip address
		detail::write_int32(0, out);
		// key
		detail::write_int32(m_req.key, out);
		// num_want
		detail::write_int32(m_req.num_want, out);
		// port
		detail::write_uint16(m_req.listen_port, out);
		// extensions
		detail::write_uint16(0, out);

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester())
		{
			requester().debug_log("==> UDP_TRACKER_ANNOUNCE ["
				+ lexical_cast<std::string>(m_req.info_hash) + "]");
		}
#endif

		m_socket->send(asio::buffer(buf), 0);
		m_request_time = second_clock::universal_time();
		++m_attempts;

		m_socket->async_receive_from(asio::buffer(m_buffer), 0, m_sender
			, bind(&udp_tracker_connection::announce_response, self(), _1, _2));
	}

	void udp_tracker_connection::send_udp_scrape()
	{
		if (m_transaction_id == 0)
			m_transaction_id = rand() ^ (rand() << 16);

		std::vector<char> buf;
		std::back_insert_iterator<std::vector<char> > out(buf);

		// connection_id
		detail::write_int64(m_connection_id, out);
		// action (scrape)
		detail::write_int32(action_scrape, out);
		// transaction_id
		detail::write_int32(m_transaction_id, out);
		// info_hash
		std::copy(m_req.info_hash.begin(), m_req.info_hash.end(), out);

		m_socket->send(asio::buffer(&buf[0], buf.size()), 0);
		m_request_time = second_clock::universal_time();
		++m_attempts;

		m_socket->async_receive_from(asio::buffer(m_buffer), 0, m_sender
			, bind(&udp_tracker_connection::scrape_response, self(), _1, _2));
	}

	void udp_tracker_connection::announce_response(asio::error const& error
		, std::size_t bytes_transferred)
	{
		if (error)
		{
			fail(-1, error.what());
			return;
		}

		if (m_target != m_sender)
		{
			// this packet was not received from the tracker
			m_socket->async_receive_from(asio::buffer(m_buffer), 0, m_sender
				, bind(&udp_tracker_connection::connect_response, self(), _1, _2));
			return;
		}

		if (bytes_transferred >= udp_buffer_size)
		{
			fail(-1, "udp response too big");
			return;
		}

		if (bytes_transferred < 8)
		{
			fail(-1, "got a message with size < 8");
			return;
		}

		char* buf = &m_buffer[0];
		int action = detail::read_int32(buf);
		int transaction = detail::read_int32(buf);

		if (transaction != m_transaction_id)
		{
			fail(-1, "incorrect transaction id");
			return;
		}

		if (action == action_error)
		{
			fail(-1, std::string(buf, bytes_transferred - 8).c_str());
			return;
		}

		if (action != action_announce)
		{
			fail(-1, "invalid action in announce response");
			return;
		}

		if (bytes_transferred < 20)
		{
			fail(-1, "got a message with size < 20");
			return;
		}

		int interval = detail::read_int32(buf);
		int incomplete = detail::read_int32(buf);
		int complete = detail::read_int32(buf);
		int num_peers = (bytes_transferred - 20) / 6;
		if ((bytes_transferred - 20) % 6 != 0)
		{
			fail(-1, "invalid udp tracker response length");
			return;
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester())
		{
			requester().debug_log("<== UDP_TRACKER_ANNOUNCE_RESPONSE");
		}
#endif

		if (!has_requester())
		{
			m_man.remove_request(this);
			return;
		}

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

		requester().tracker_response(m_req, peer_list, interval
			, complete, incomplete);

		m_man.remove_request(this);
		return;
	}

	void udp_tracker_connection::scrape_response(asio::error const& error
		, std::size_t bytes_transferred)
	{
		if (error)
		{
			fail(-1, error.what());
			return;
		}

		if (m_target != m_sender)
		{
			// this packet was not received from the tracker
			m_socket->async_receive_from(asio::buffer(m_buffer), 0, m_sender
				, bind(&udp_tracker_connection::connect_response, self(), _1, _2));
			return;
		}

		if (bytes_transferred >= udp_buffer_size)
		{
			fail(-1, "udp response too big");
			return;
		}

		if (bytes_transferred < 8)
		{
			fail(-1, "got a message with size < 8");
			return;
		}

		char* buf = &m_buffer[0];
		int action = detail::read_int32(buf);
		int transaction = detail::read_int32(buf);

		if (transaction != m_transaction_id)
		{
			fail(-1, "incorrect transaction id");
			return;
		}

		if (action == action_error)
		{
			fail(-1, std::string(buf, bytes_transferred - 8).c_str());
			return;
		}

		if (action != action_scrape)
		{
			fail(-1, "invalid action in announce response");
			return;
		}

		if (bytes_transferred < 20)
		{
			fail(-1, "got a message with size < 20");
			return;
		}

		int complete = detail::read_int32(buf);
		/*int downloaded = */detail::read_int32(buf);
		int incomplete = detail::read_int32(buf);

		if (!has_requester())
		{
			m_man.remove_request(this);
			return;
		}
		
		std::vector<peer_entry> peer_list;
		requester().tracker_response(m_req, peer_list, 0
			, complete, incomplete);

		m_man.remove_request(this);
	}

}

