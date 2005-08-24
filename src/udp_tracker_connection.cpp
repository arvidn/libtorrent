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

using namespace boost::posix_time;

namespace libtorrent
{

	udp_tracker_connection::udp_tracker_connection(
		tracker_request const& req
		, std::string const& hostname
		, unsigned short port
		, boost::weak_ptr<request_callback> c
		, const http_proxy& http_proxy)
		: tracker_connection(c)
		, m_request_time(second_clock::universal_time())
		, m_request(req)
		, m_transaction_id(0)
		, m_connection_id(0)
		, m_http_proxy(http_proxy)
		, m_attempts(0)
	{
		m_name_lookup = dns_lookup(hostname.c_str(), port);
		m_socket.reset(new socket(socket::udp, false));

	}

	bool udp_tracker_connection::send_finished() const
	{
		using namespace boost::posix_time;

		time_duration d = second_clock::universal_time() - m_request_time;
		return (m_transaction_id != 0
			&& m_connection_id != 0)
			|| d > seconds(m_http_proxy.tracker_timeout);
	}

	bool udp_tracker_connection::tick()
	{
		using namespace boost::posix_time;

		if (m_name_lookup.running())
		{
			if (!m_name_lookup.finished()) return false;

			if (m_name_lookup.failed())
			{
				if (has_requester()) requester().tracker_request_error(
					m_request, -1, m_name_lookup.error());
				return true;
			}
			address a(m_name_lookup.ip());
			if (has_requester()) requester().m_tracker_address = a;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester()) requester().debug_log("name lookup successful");
#endif

			m_socket->connect(a);
			send_udp_connect();
		
			// clear the lookup entry so it will not be
			// marked as running anymore
			m_name_lookup = dns_lookup();
		}

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

	void udp_tracker_connection::send_udp_announce()
	{
		if (m_transaction_id == 0)
			m_transaction_id = rand() ^ (rand() << 16);

		std::vector<char> buf;
		std::back_insert_iterator<std::vector<char> > out(buf);

		// connection_id
		detail::write_int64(m_connection_id, out);
		// action (announce)
		detail::write_int32(announce, out);
		// transaction_id
		detail::write_int32(m_transaction_id, out);
		// info_hash
		std::copy(m_request.info_hash.begin(), m_request.info_hash.end(), out);
		// peer_id
		std::copy(m_request.id.begin(), m_request.id.end(), out);
		// downloaded
		detail::write_int64(m_request.downloaded, out);
		// left
		detail::write_int64(m_request.left, out);
		// uploaded
		detail::write_int64(m_request.uploaded, out);
		// event
		detail::write_int32(m_request.event, out);
		// ip address
		detail::write_int32(0, out);
		// key
		detail::write_int32(m_request.key, out);
		// num_want
		detail::write_int32(m_request.num_want, out);
		// port
		detail::write_uint16(m_request.listen_port, out);
		// extensions
		detail::write_uint16(0, out);

		m_socket->send(&buf[0], buf.size());
		m_request_time = second_clock::universal_time();
		++m_attempts;
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
		detail::write_int32(scrape, out);
		// transaction_id
		detail::write_int32(m_transaction_id, out);
		// info_hash
		std::copy(m_request.info_hash.begin(), m_request.info_hash.end(), out);

		m_socket->send(&buf[0], buf.size());
		m_request_time = second_clock::universal_time();
		++m_attempts;
	}

	void udp_tracker_connection::send_udp_connect()
	{
		char send_buf[16];
		char* ptr = send_buf;

		if (m_transaction_id == 0)
			m_transaction_id = rand() ^ (rand() << 16);

		// connection_id
		detail::write_uint32(0x417, ptr);
		detail::write_uint32(0x27101980, ptr);
		// action (connect)
		detail::write_int32(connect, ptr);
		// transaction_id
		detail::write_int32(m_transaction_id, ptr);

		m_socket->send(send_buf, 16);
		m_request_time = second_clock::universal_time();
		++m_attempts;
	}

	bool udp_tracker_connection::parse_announce_response(const char* buf, int len)
	{
		assert(buf != 0);
		assert(len > 0);

		if (len < 8)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			if (has_requester())
				requester().debug_log("udp_tracker_connection: "
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
			if (has_requester())
				requester().tracker_request_error(
					m_request, -1, std::string(buf, buf + len - 8));
			return true;
		}
		if (action != announce) return false;

		if (len < 20)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			if (has_requester())
				requester().debug_log("udp_tracker_connection: "
				"got a message with size < 20, ignoring");
#endif
			return false;
		}
		int interval = detail::read_int32(buf);
		int incomplete = detail::read_int32(buf);
		int complete = detail::read_int32(buf);
		int num_peers = (len - 20) / 6;
		if ((len - 20) % 6 != 0)
		{
			if (has_requester())
				requester().tracker_request_error(
					m_request, -1, "invalid tracker response");
			return true;
		}

		if (!has_requester()) return true;

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

		requester().tracker_response(m_request, peer_list, interval
			, complete, incomplete);
		return true;
	}

	bool udp_tracker_connection::parse_scrape_response(const char* buf, int len)
	{
		assert(buf != 0);
		assert(len > 0);

		if (len < 8)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			if (has_requester())
				requester().debug_log("udp_tracker_connection: "
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
			if (has_requester())
				requester().tracker_request_error(
					m_request, -1, std::string(buf, buf + len - 8));
			return true;
		}
		if (action != scrape) return false;

		if (len < 20)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			if (has_requester())
				requester().debug_log("udp_tracker_connection: "
				"got a message with size < 20, ignoring");
#endif
			return false;
		}
		int complete = detail::read_int32(buf);
		/*int downloaded = */detail::read_int32(buf);
		int incomplete = detail::read_int32(buf);

		if (!has_requester()) return true;

		std::vector<peer_entry> peer_list;
		requester().tracker_response(m_request, peer_list, 0
			, complete, incomplete);
		return true;
	}


	bool udp_tracker_connection::parse_connect_response(const char* buf, int len)
	{
		assert(buf != 0);
		assert(len > 0);

		if (len < 8)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			if (has_requester())
				requester().debug_log("udp_tracker_connection: "
				"got a message with size < 8, ignoring");
#endif
			return false;
		}
		const char* ptr = buf;
		int action = detail::read_int32(ptr);
		int transaction = detail::read_int32(ptr);

		if (action == error)
		{
			if (has_requester())
				requester().tracker_request_error(
					m_request, -1, std::string(ptr, buf + len));
			return true;
		}
		if (action != connect) return false;
		if (m_transaction_id != transaction)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			if (has_requester())
				requester().debug_log("udp_tracker_connection: "
				"got a message with incorrect transaction id, ignoring");
#endif
			return false;
		}

		if (len < 16)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			if (has_requester())
				requester().debug_log("udp_tracker_connection: "
				"got a connection message size < 16, ignoring");
#endif
			return false;
		}
		// reset transaction
		m_transaction_id = 0;
		m_attempts = 0;
		m_connection_id = detail::read_int64(ptr);

		if (m_request.kind == tracker_request::announce_request)
			send_udp_announce();
		else if (m_request.kind == tracker_request::scrape_request)
			send_udp_scrape();

		return false;
	}


}

