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
#include <cctype>

#include "zlib.h"

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/udp_tracker_connection.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/escape_string.hpp"

namespace libtorrent
{

	std::map<address, udp_tracker_connection::connection_cache_entry>
		udp_tracker_connection::m_connection_cache;

	boost::mutex udp_tracker_connection::m_cache_mutex;

	udp_tracker_connection::udp_tracker_connection(
		io_service& ios
		, connection_queue& cc
		, tracker_manager& man
		, tracker_request const& req
		, boost::weak_ptr<request_callback> c
		, aux::session_impl const& ses
		, proxy_settings const& proxy)
		: tracker_connection(man, req, ios, c)
		, m_man(man)
		, m_name_lookup(ios)
		, m_socket(ios, boost::bind(&udp_tracker_connection::on_receive, self(), _1, _2, _3, _4), cc)
		, m_transaction_id(0)
		, m_ses(ses)
		, m_attempts(0)
		, m_state(action_error)
	{
		TORRENT_ASSERT(refcount() == 1);
		m_socket.set_proxy_settings(proxy);
	}

	void udp_tracker_connection::start()
	{
		std::string hostname;
		int port;
		error_code ec;

		using boost::tuples::ignore;
		boost::tie(ignore, ignore, hostname, port, ignore)
			= parse_url_components(tracker_req().url, ec);

		if (ec)
		{
			// never call fail() when the session mutex is locked!
			m_socket.get_io_service().post(boost::bind(
				&tracker_connection::fail_disp, self(), -1, ec.message()));
			return;
		}
		
		session_settings const& settings = m_ses.settings();

		udp::resolver::query q(hostname, to_string(port).elems);
		m_name_lookup.async_resolve(q
			, boost::bind(
			&udp_tracker_connection::name_lookup, self(), _1, _2));
		set_timeout(tracker_req().event == tracker_request::stopped
			? settings.stop_tracker_timeout
			: settings.tracker_completion_timeout
			, settings.tracker_receive_timeout);
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

		std::transform(i, udp::resolver::iterator(), std::back_inserter(m_endpoints)
			, boost::bind(&udp::resolver::iterator::value_type::endpoint, _1));

		// remove endpoints that are filtered by the IP filter
		for (std::list<udp::endpoint>::iterator i = m_endpoints.begin();
			i != m_endpoints.end();)
		{
			if (m_ses.m_ip_filter.access(i->address()) == ip_filter::blocked) 
			{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
				if (cb) cb->debug_log("*** UDP_TRACKER [ IP blocked by filter: " + print_address(i->address()) + " ]");
#endif
				i = m_endpoints.erase(i);
			}
			else
				++i;
		}

		if (m_endpoints.empty())
		{
			fail(-1, "blocked by IP filter");
			return;
		}
		
		std::list<udp::endpoint>::iterator iter = m_endpoints.begin();
		m_target = *iter;

		if (bind_interface() != address_v4::any())
		{
			// find first endpoint that matches our bind interface type
			for (; iter != m_endpoints.end() && iter->address().is_v4()
				!= bind_interface().is_v4(); ++iter);

			if (iter == m_endpoints.end())
			{
				TORRENT_ASSERT(m_target.address().is_v4() != bind_interface().is_v4());
				if (cb)
				{
					char const* tracker_address_type = m_target.address().is_v4() ? "IPv4" : "IPv6";
					char const* bind_address_type = bind_interface().is_v4() ? "IPv4" : "IPv6";
					char msg[200];
					snprintf(msg, sizeof(msg)
						, "the tracker only resolves to an %s  address, and you're "
						"listening on an %s socket. This may prevent you from receiving "
						"incoming connections."
						, tracker_address_type, bind_address_type);

					cb->tracker_warning(tracker_req(), msg);
				}
			}
			else
			{
				m_target = *iter;
			}
		}

		if (cb) cb->m_tracker_address = tcp::endpoint(m_target.address(), m_target.port());

		error_code ec;
		m_socket.bind(udp::endpoint(bind_interface(), 0), ec);
		if (ec)
		{
			fail(-1, ec.message().c_str());
			return;
		}

		boost::mutex::scoped_lock l(m_cache_mutex);
		std::map<address, connection_cache_entry>::iterator cc
			= m_connection_cache.find(m_target.address());
		if (cc != m_connection_cache.end())
		{
			// we found a cached entry! Now, we can only
			// use if if it hasn't expired
			if (time_now() < cc->second.expires)
			{
				if (tracker_req().kind == tracker_request::announce_request)
					send_udp_announce();
				else if (tracker_req().kind == tracker_request::scrape_request)
					send_udp_scrape();
				return;
			}
			// if it expired, remove it from the cache
			m_connection_cache.erase(cc);
		}
		l.unlock();

		send_udp_connect();
	}

	void udp_tracker_connection::on_timeout(error_code const& ec)
	{
		if (ec)
		{
			fail(-1, ec.message().c_str());
			return;
		}

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		char msg[200];
		snprintf(msg, 200, "*** UDP_TRACKER [ timed out url: %s ]", tracker_req().url.c_str());
		if (cb) cb->debug_log(msg);
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
		
		received_bytes(size + 28); // assuming UDP/IP header
		if (e) fail(-1, e.message().c_str());

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			char msg[200];
			snprintf(msg, 200, "<== UDP_TRACKER_PACKET [ size: %d ]", size);
			cb->debug_log(msg);
		}
#endif

		// ignore packets smaller than 8 bytes
		if (size < 8) return;

		const char* ptr = buf;
		int action = detail::read_int32(ptr);
		int transaction = detail::read_int32(ptr);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		if (cb)
		{
			char msg[200];
			snprintf(msg, 200, "*** UDP_TRACKER_PACKET [ action: %d ]", action);
			cb->debug_log(msg);
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

		restart_read_timeout();

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		if (cb)
		{
			char msg[200];
			snprintf(msg, 200, "*** UDP_TRACKER_RESPONSE [ tid: %x ]"
				, int(transaction));
			cb->debug_log(msg);
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
		boost::uint64_t connection_id = detail::read_int64(buf);

		boost::mutex::scoped_lock l(m_cache_mutex);
		connection_cache_entry& cce = m_connection_cache[m_target.address()];
		cce.connection_id = connection_id;
		cce.expires = time_now() + seconds(m_ses.m_settings.udp_tracker_token_expiry);

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
			char hex_ih[41];
			to_hex((char const*)&tracker_req().info_hash[0], 20, hex_ih);
			char msg[200];
			snprintf(msg, 200, "==> UDP_TRACKER_CONNECT [%s]", hex_ih);
			cb->debug_log(msg);
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
		sent_bytes(16 + 28); // assuming UDP/IP header
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

		std::map<address, connection_cache_entry>::iterator i
			= m_connection_cache.find(m_target.address());
		// this isn't really supposed to happen
		TORRENT_ASSERT(i != m_connection_cache.end());
		if (i == m_connection_cache.end()) return;

		char buf[8 + 4 + 4 + 20];
		char* out = buf;

		detail::write_int64(i->second.connection_id, out); // connection_id
		detail::write_int32(action_scrape, out); // action (scrape)
		detail::write_int32(m_transaction_id, out); // transaction_id
		// info_hash
		std::copy(tracker_req().info_hash.begin(), tracker_req().info_hash.end(), out);
		out += 20;
		TORRENT_ASSERT(out - buf == sizeof(buf));

		error_code ec;
		m_socket.send(m_target, buf, sizeof(buf), ec);
		m_state = action_scrape;
		sent_bytes(sizeof(buf) + 28); // assuming UDP/IP header
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

		buf += 8; // skip header
		restart_read_timeout();
		int interval = detail::read_int32(buf);
		int min_interval = 60;
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
			boost::shared_ptr<request_callback> cb = requester();
			char msg[200];
			snprintf(msg, 200, "<== UDP_TRACKER_RESPONSE [ url: %s ]", tracker_req().url.c_str());
			cb->debug_log(msg);
		}
#endif

		if (!cb)
		{
			close();
			return;
		}

		std::vector<peer_entry> peer_list;
		for (int i = 0; i < num_peers; ++i)
		{
			// TODO: don't use a string here
			peer_entry e;
			char ip_string[100];
			unsigned int a = detail::read_uint8(buf);
			unsigned int b = detail::read_uint8(buf);
			unsigned int c = detail::read_uint8(buf);
			unsigned int d = detail::read_uint8(buf);
			snprintf(ip_string, 100, "%u.%u.%u.%u", a, b, c, d);
			e.ip = ip_string;
			e.port = detail::read_uint16(buf);
			e.pid.clear();
			peer_list.push_back(e);
		}

		std::list<address> ip_list;
		for (std::list<udp::endpoint>::const_iterator i = m_endpoints.begin()
			, end(m_endpoints.end()); i != end; ++i)
		{
			ip_list.push_back(i->address());
		}

		cb->tracker_response(tracker_req(), m_target.address(), ip_list
			, peer_list, interval, min_interval, complete, incomplete, address());

		close();
	}

	void udp_tracker_connection::on_scrape_response(char const* buf, int size)
	{
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
		const bool stats = req.send_stats;
		session_settings const& settings = m_ses.settings();

		std::map<address, connection_cache_entry>::iterator i
			= m_connection_cache.find(m_target.address());
		// this isn't really supposed to happen
		TORRENT_ASSERT(i != m_connection_cache.end());
		if (i == m_connection_cache.end()) return;

		detail::write_int64(i->second.connection_id, out); // connection_id
		detail::write_int32(action_announce, out); // action (announce)
		detail::write_int32(m_transaction_id, out); // transaction_id
		std::copy(req.info_hash.begin(), req.info_hash.end(), out); // info_hash
		out += 20;
		std::copy(req.pid.begin(), req.pid.end(), out); // peer_id
		out += 20;
		detail::write_int64(stats ? req.downloaded : 0, out); // downloaded
		detail::write_int64(stats ? req.left : 0, out); // left
		detail::write_int64(stats ? req.uploaded : 0, out); // uploaded
		detail::write_int32(req.event, out); // event
		// ip address
		if (settings.announce_ip != address() && settings.announce_ip.is_v4())
			detail::write_uint32(settings.announce_ip.to_v4().to_ulong(), out);
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
			char hex_ih[41];
			to_hex((char const*)&req.info_hash[0], 20, hex_ih);
			char msg[200];
			snprintf(msg, 200, "==> UDP_TRACKER_ANNOUNCE [%s]", hex_ih);
			cb->debug_log(msg);
		}
#endif

		error_code ec;
		m_socket.send(m_target, buf, sizeof(buf), ec);
		m_state = action_announce;
		sent_bytes(sizeof(buf) + 28); // assuming UDP/IP header
		++m_attempts;
		if (ec)
		{
			fail(-1, ec.message().c_str());
			return;
		}
	}

}

