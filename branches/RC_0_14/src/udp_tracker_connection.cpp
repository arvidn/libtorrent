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

#include "libtorrent/pch.hpp"

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

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
#include <boost/lexical_cast.hpp>
using boost::lexical_cast;
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/udp_tracker_connection.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/escape_string.hpp"

namespace
{
	enum
	{
		udp_connection_retries = 4,
		udp_announce_retries = 15,
		udp_connect_timeout = 15,
		udp_announce_timeout = 10
	};
}

using boost::bind;

namespace libtorrent
{

	udp_tracker_connection::udp_tracker_connection(
		io_service& ios
		, connection_queue& cc
		, tracker_manager& man
		, tracker_request const& req
		, address bind_infc
		, boost::weak_ptr<request_callback> c
		, session_settings const& stn
		, proxy_settings const& proxy)
		: tracker_connection(man, req, ios, bind_infc, c)
		, m_man(man)
		, m_name_lookup(ios)
		, m_socket(ios, boost::bind(&udp_tracker_connection::on_receive, self(), _1, _2, _3, _4), cc)
		, m_transaction_id(0)
		, m_connection_id(0)
		, m_settings(stn)
		, m_attempts(0)
		, m_state(action_error)
	{
		m_socket.set_proxy_settings(proxy);
	}

	void udp_tracker_connection::start()
	{
		std::string hostname;
		int port;
		char const* error;

		using boost::tuples::ignore;
		boost::tie(ignore, ignore, hostname, port, ignore, error)
			= parse_url_components(tracker_req().url);

		if (error)
		{
			fail(-1, error);
			return;
		}
		
		udp::resolver::query q(hostname, to_string(port).elems);
		m_name_lookup.async_resolve(q
			, boost::bind(
			&udp_tracker_connection::name_lookup, self(), _1, _2));
		set_timeout(tracker_req().event == tracker_request::stopped
			? m_settings.stop_tracker_timeout
			: m_settings.tracker_completion_timeout
			, m_settings.tracker_receive_timeout);
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->debug_log(("*** UDP_TRACKER [ initiating name lookup: " + hostname + " ]").c_str());
#endif
	}

	void udp_tracker_connection::name_lookup(error_code const& error
		, udp::resolver::iterator i)
	{
		if (error == asio::error::operation_aborted) return;
		if (error || i == udp::resolver::iterator())
		{
			fail(-1, error.message().c_str());
			return;
		}

		boost::shared_ptr<request_callback> cb = requester();
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		if (cb) cb->debug_log("*** UDP_TRACKER [ name lookup successful ]");
#endif
		if (cancelled())
		{
			fail(-1, "aborted");
			return;
		}

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
			TORRENT_ASSERT(target_address.address().is_v4() != bind_interface().is_v4());
			if (cb)
			{
				std::string tracker_address_type = target_address.address().is_v4() ? "IPv4" : "IPv6";
				std::string bind_address_type = bind_interface().is_v4() ? "IPv4" : "IPv6";
				cb->tracker_warning(tracker_req(), "the tracker only resolves to an "
					+ tracker_address_type + " address, and you're listening on an "
					+ bind_address_type + " socket. This may prevent you from receiving incoming connections.");
			}
		}
		else
		{
			target_address = *target;
		}
		
		if (cb) cb->m_tracker_address = tcp::endpoint(target_address.address(), target_address.port());
		m_target = target_address;
		error_code ec;
		m_socket.bind(udp::endpoint(bind_interface(), 0), ec);
		if (ec)
		{
			fail(-1, ec.message().c_str());
			return;
		}
		send_udp_connect();
	}

	void udp_tracker_connection::on_timeout()
	{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->debug_log("*** UDP_TRACKER [ timed out ]");
#endif
		m_socket.close();
		m_name_lookup.cancel();
		fail_timeout();
	}

	void udp_tracker_connection::close()
	{
		error_code ec;
		m_socket.close();
		m_name_lookup.cancel();
		tracker_connection::close();
	}

	void udp_tracker_connection::on_receive(error_code const& e
		, udp::endpoint const& ep, char const* buf, int size)
	{
		// ignore resposes before we've sent any requests
		if (m_state == action_error) return;

		if (!m_socket.is_open()) return; // the operation was aborted

		// ignore packet not sent from the tracker
		if (m_target != ep) return;
		
		if (e) fail(-1, e.message().c_str());

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			std::stringstream msg;
			msg << "<== UDP_TRACKER_PACKET [ size: " << size << " ]";
			cb->debug_log(msg.str());
		}
#endif

		// ignore packets smaller than 8 bytes
		if (size < 8) return;

		restart_read_timeout();

		const char* ptr = buf;
		int action = detail::read_int32(ptr);
		int transaction = detail::read_int32(ptr);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		if (cb)
		{
			std::stringstream msg;
			msg << "*** UDP_TRACKER_PACKET [ acton: " << action << " ]";
			cb->debug_log(msg.str());
		}
#endif

		// ignore packets with incorrect transaction id
		if (m_transaction_id != transaction) return;

		if (action == action_error)
		{
			fail(-1, std::string(ptr, size - 8).c_str());
			return;
		}

		// ignore packets that's not a response to our message
		if (action != m_state) return;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		if (cb)
		{
			std::stringstream msg;
			msg << "*** UDP_TRACKER_RESPONSE [ cid: " << m_connection_id << " ]";
			cb->debug_log(msg.str());
		}
#endif

		switch (m_state)
		{
			case action_connect:
				on_connect_response(buf, size);
				break;
			case action_announce:
				on_announce_response(buf, size);
				break;
			case action_scrape:
				on_scrape_response(buf, size);
				break;
			default: break;
		}
	}
	
	void udp_tracker_connection::on_connect_response(char const* buf, int size)
	{
		// ignore packets smaller than 16 bytes
		if (size < 16) return;

		restart_read_timeout();
		buf += 8; // skip header

		// reset transaction
		m_transaction_id = 0;
		m_attempts = 0;
		m_connection_id = detail::read_int64(buf);

		if (tracker_req().kind == tracker_request::announce_request)
			send_udp_announce();
		else if (tracker_req().kind == tracker_request::scrape_request)
			send_udp_scrape();
	}

	void udp_tracker_connection::send_udp_connect()
	{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			cb->debug_log("==> UDP_TRACKER_CONNECT ["
				+ lexical_cast<std::string>(tracker_req().info_hash) + "]");
		}
#endif
		if (!m_socket.is_open()) return; // the operation was aborted

		char buf[16];
		char* ptr = buf;

		if (m_transaction_id == 0)
			m_transaction_id = std::rand() ^ (std::rand() << 16);

		detail::write_uint32(0x417, ptr);
		detail::write_uint32(0x27101980, ptr); // connection_id
		detail::write_int32(action_connect, ptr); // action (connect)
		detail::write_int32(m_transaction_id, ptr); // transaction_id
		TORRENT_ASSERT(ptr - buf == sizeof(buf));

		error_code ec;
		m_socket.send(m_target, buf, 16, ec);
		m_state = action_connect;
		++m_attempts;
		if (ec)
		{
			fail(-1, ec.message().c_str());
			return;
		}
	}

	void udp_tracker_connection::send_udp_scrape()
	{
		if (m_transaction_id == 0)
			m_transaction_id = std::rand() ^ (std::rand() << 16);

		if (!m_socket.is_open()) return; // the operation was aborted

		char buf[8 + 4 + 4 + 20];
		char* out = buf;

		detail::write_int64(m_connection_id, out); // connection_id
		detail::write_int32(action_scrape, out); // action (scrape)
		detail::write_int32(m_transaction_id, out); // transaction_id
		// info_hash
		std::copy(tracker_req().info_hash.begin(), tracker_req().info_hash.end(), out);
		out += 20;
		TORRENT_ASSERT(out - buf == sizeof(buf));

		error_code ec;
		m_socket.send(m_target, buf, sizeof(buf), ec);
		m_state = action_scrape;
		++m_attempts;
		if (ec)
		{
			fail(-1, ec.message().c_str());
			return;
		}
	}

	void udp_tracker_connection::on_announce_response(char const* buf, int size)
	{
		if (size < 20) return;

		restart_read_timeout();

		buf += 8; // skip header
		restart_read_timeout();
		int interval = detail::read_int32(buf);
		int incomplete = detail::read_int32(buf);
		int complete = detail::read_int32(buf);
		int num_peers = (size - 20) / 6;
		if ((size - 20) % 6 != 0)
		{
			fail(-1, "invalid udp tracker response length");
			return;
		}

		boost::shared_ptr<request_callback> cb = requester();
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		if (cb)
		{
			cb->debug_log("<== UDP_TRACKER_ANNOUNCE_RESPONSE");
		}
#endif

		if (!cb)
		{
			m_man.remove_request(this);
			return;
		}

		std::vector<peer_entry> peer_list;
		for (int i = 0; i < num_peers; ++i)
		{
			// TODO: don't use a string here
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

		cb->tracker_response(tracker_req(), peer_list, interval
			, complete, incomplete, address());

		m_man.remove_request(this);
		close();
	}

	void udp_tracker_connection::on_scrape_response(char const* buf, int size)
	{
		buf += 8; // skip header

		restart_read_timeout();
		int action = detail::read_int32(buf);
		int transaction = detail::read_int32(buf);

		if (transaction != m_transaction_id)
		{
			fail(-1, "incorrect transaction id");
			return;
		}

		if (action == action_error)
		{
			fail(-1, std::string(buf, size - 8).c_str());
			return;
		}

		if (action != action_scrape)
		{
			fail(-1, "invalid action in announce response");
			return;
		}

		if (size < 20)
		{
			fail(-1, "got a message with size < 20");
			return;
		}

		int complete = detail::read_int32(buf);
		int downloaded = detail::read_int32(buf);
		int incomplete = detail::read_int32(buf);

		boost::shared_ptr<request_callback> cb = requester();
		if (!cb)
		{
			close();
			return;
		}
		
		cb->tracker_scrape_response(tracker_req()
			, complete, incomplete, downloaded);

		m_man.remove_request(this);
		close();
	}

	void udp_tracker_connection::send_udp_announce()
	{
		if (m_transaction_id == 0)
			m_transaction_id = std::rand() ^ (std::rand() << 16);

		if (!m_socket.is_open()) return; // the operation was aborted

		char buf[8 + 4 + 4 + 20 + 20 + 8 + 8 + 8 + 4 + 4 + 4 + 4 + 2 + 2];
		char* out = buf;

		tracker_request const& req = tracker_req();

		detail::write_int64(m_connection_id, out); // connection_id
		detail::write_int32(action_announce, out); // action (announce)
		detail::write_int32(m_transaction_id, out); // transaction_id
		std::copy(req.info_hash.begin(), req.info_hash.end(), out); // info_hash
		out += 20;
		std::copy(req.pid.begin(), req.pid.end(), out); // peer_id
		out += 20;
		detail::write_int64(req.downloaded, out); // downloaded
		detail::write_int64(req.left, out); // left
		detail::write_int64(req.uploaded, out); // uploaded
		detail::write_int32(req.event, out); // event
		// ip address
		if (m_settings.announce_ip != address() && m_settings.announce_ip.is_v4())
			detail::write_uint32(m_settings.announce_ip.to_v4().to_ulong(), out);
		else
			detail::write_int32(0, out);
		detail::write_int32(req.key, out); // key
		detail::write_int32(req.num_want, out); // num_want
		detail::write_uint16(req.listen_port, out); // port
		detail::write_uint16(0, out); // extensions

		TORRENT_ASSERT(out - buf == sizeof(buf));

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			cb->debug_log("==> UDP_TRACKER_ANNOUNCE ["
				+ lexical_cast<std::string>(req.info_hash) + "]");
		}
#endif

		error_code ec;
		m_socket.send(m_target, buf, sizeof(buf), ec);
		m_state = action_announce;
		++m_attempts;
		if (ec)
		{
			fail(-1, ec.message().c_str());
			return;
		}
	}

}

