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
		asio::strand& str
		, tracker_manager& man
		, tracker_request const& req
		, std::string const& hostname
		, unsigned short port
		, address bind_infc
		, boost::weak_ptr<request_callback> c
		, session_settings const& stn)
		: tracker_connection(man, req, str, bind_infc, c)
		, m_man(man)
		, m_strand(str)
		, m_name_lookup(m_strand.io_service())
		, m_transaction_id(0)
		, m_connection_id(0)
		, m_settings(stn)
		, m_attempts(0)
	{
		udp::resolver::query q(hostname, boost::lexical_cast<std::string>(port));
		m_name_lookup.async_resolve(q
			, m_strand.wrap(boost::bind(
			&udp_tracker_connection::name_lookup, self(), _1, _2)));
		set_timeout(m_settings.tracker_completion_timeout
			, m_settings.tracker_receive_timeout);
	}

	void udp_tracker_connection::name_lookup(asio::error_code const& error
		, udp::resolver::iterator i) try
	{
		if (error == asio::error::operation_aborted) return;
		if (!m_socket) return; // the operation was aborted
		if (error || i == udp::resolver::iterator())
		{
			fail(-1, error.message().c_str());
			return;
		}

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester()) requester().debug_log("udp tracker name lookup successful");
#endif
		restart_read_timeout();
		
		// look for an address that has the same kind as the one
		// we're listening on. To make sure the tracker get our
		// correct listening address.
		udp::resolver::iterator target = i;
		udp::resolver::iterator end;
		udp::endpoint target_address = *i;
		for (; target != end && target->endpoint().address().is_v4()
			!= bind_interface().is_v4(); ++target);
		if (target == end)
		{
			assert(target_address.address().is_v4() != bind_interface().is_v4());
			if (has_requester())
			{
				std::string tracker_address_type = target_address.address().is_v4() ? "IPv4" : "IPv6";
				std::string bind_address_type = bind_interface().is_v4() ? "IPv4" : "IPv6";
				requester().tracker_warning("the tracker only resolves to an "
					+ tracker_address_type + " address, and you're listening on an "
					+ bind_address_type + " socket. This may prevent you from receiving incoming connections.");
			}
		}
		else
		{
			target_address = *target;
		}
		
		if (has_requester()) requester().m_tracker_address = tcp::endpoint(target_address.address(), target_address.port());
		m_target = target_address;
		m_socket.reset(new datagram_socket(m_name_lookup.io_service()));
		m_socket->open(target_address.protocol());
		m_socket->bind(udp::endpoint(bind_interface(), 0));
		m_socket->connect(target_address);
		send_udp_connect();
	}
	catch (std::exception& e)
	{
		fail(-1, e.what());
	};

	void udp_tracker_connection::on_timeout()
	{
		m_socket.reset();
		m_name_lookup.cancel();
		fail_timeout();
	}

	void udp_tracker_connection::send_udp_connect()
	{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester())
		{
			requester().debug_log("==> UDP_TRACKER_CONNECT ["
				+ lexical_cast<std::string>(tracker_req().info_hash) + "]");
		}
#endif
		if (!m_socket) return; // the operation was aborted

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
		++m_attempts;
		m_buffer.resize(udp_buffer_size);
		m_socket->async_receive_from(asio::buffer(m_buffer), m_sender
			, boost::bind(&udp_tracker_connection::connect_response, self(), _1, _2));
	}

	void udp_tracker_connection::connect_response(asio::error_code const& error
		, std::size_t bytes_transferred) try
	{
		if (error == asio::error::operation_aborted) return;
		if (!m_socket) return; // the operation was aborted
		if (error)
		{
			fail(-1, error.message().c_str());
			return;
		}

		if (m_target != m_sender)
		{
			// this packet was not received from the tracker
			m_socket->async_receive_from(asio::buffer(m_buffer), m_sender
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

		restart_read_timeout();

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

		if (tracker_req().kind == tracker_request::announce_request)
			send_udp_announce();
		else if (tracker_req().kind == tracker_request::scrape_request)
			send_udp_scrape();
	}
	catch (std::exception& e)
	{
		fail(-1, e.what());
	}
	
	void udp_tracker_connection::send_udp_announce()
	{
		if (m_transaction_id == 0)
			m_transaction_id = rand() ^ (rand() << 16);

		if (!m_socket) return; // the operation was aborted

		std::vector<char> buf;
		std::back_insert_iterator<std::vector<char> > out(buf);
		
		tracker_request const& req = tracker_req();

		// connection_id
		detail::write_int64(m_connection_id, out);
		// action (announce)
		detail::write_int32(action_announce, out);
		// transaction_id
		detail::write_int32(m_transaction_id, out);
		// info_hash
		std::copy(req.info_hash.begin(), req.info_hash.end(), out);
		// peer_id
		std::copy(req.pid.begin(), req.pid.end(), out);
		// downloaded
		detail::write_int64(req.downloaded, out);
		// left
		detail::write_int64(req.left, out);
		// uploaded
		detail::write_int64(req.uploaded, out);
		// event
		detail::write_int32(req.event, out);
		// ip address
		detail::write_int32(0, out);
		// key
		detail::write_int32(req.key, out);
		// num_want
		detail::write_int32(req.num_want, out);
		// port
		detail::write_uint16(req.listen_port, out);
		// extensions
		detail::write_uint16(0, out);

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		if (has_requester())
		{
			requester().debug_log("==> UDP_TRACKER_ANNOUNCE ["
				+ lexical_cast<std::string>(req.info_hash) + "]");
		}
#endif

		m_socket->send(asio::buffer(buf), 0);
		++m_attempts;

		m_socket->async_receive_from(asio::buffer(m_buffer), m_sender
			, bind(&udp_tracker_connection::announce_response, self(), _1, _2));
	}

	void udp_tracker_connection::send_udp_scrape()
	{
		if (m_transaction_id == 0)
			m_transaction_id = rand() ^ (rand() << 16);

		if (!m_socket) return; // the operation was aborted

		std::vector<char> buf;
		std::back_insert_iterator<std::vector<char> > out(buf);

		// connection_id
		detail::write_int64(m_connection_id, out);
		// action (scrape)
		detail::write_int32(action_scrape, out);
		// transaction_id
		detail::write_int32(m_transaction_id, out);
		// info_hash
		std::copy(tracker_req().info_hash.begin(), tracker_req().info_hash.end(), out);

		m_socket->send(asio::buffer(&buf[0], buf.size()), 0);
		++m_attempts;

		m_socket->async_receive_from(asio::buffer(m_buffer), m_sender
			, bind(&udp_tracker_connection::scrape_response, self(), _1, _2));
	}

	void udp_tracker_connection::announce_response(asio::error_code const& error
		, std::size_t bytes_transferred) try
	{
		if (error == asio::error::operation_aborted) return;
		if (!m_socket) return; // the operation was aborted
		if (error)
		{
			fail(-1, error.message().c_str());
			return;
		}

		if (m_target != m_sender)
		{
			// this packet was not received from the tracker
			m_socket->async_receive_from(asio::buffer(m_buffer), m_sender
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

		restart_read_timeout();
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
			e.pid.clear();
			peer_list.push_back(e);
		}

		requester().tracker_response(tracker_req(), peer_list, interval
			, complete, incomplete);

		m_man.remove_request(this);
		return;
	}
	catch (std::exception& e)
	{
		fail(-1, e.what());
	}; // msvc 7.1 seems to require this

	void udp_tracker_connection::scrape_response(asio::error_code const& error
		, std::size_t bytes_transferred) try
	{
		if (error == asio::error::operation_aborted) return;
		if (!m_socket) return; // the operation was aborted
		if (error)
		{
			fail(-1, error.message().c_str());
			return;
		}

		if (m_target != m_sender)
		{
			// this packet was not received from the tracker
			m_socket->async_receive_from(asio::buffer(m_buffer), m_sender
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

		restart_read_timeout();
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
		requester().tracker_response(tracker_req(), peer_list, 0
			, complete, incomplete);

		m_man.remove_request(this);
	}
	catch (std::exception& e)
	{
		fail(-1, e.what());
	}

}

