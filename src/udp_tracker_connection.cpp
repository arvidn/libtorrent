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

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/bind.hpp>
#include <boost/tuple/tuple.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/udp_tracker_connection.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/broadcast_socket.hpp" // for is_any
#include "libtorrent/random.hpp"

namespace libtorrent
{

	std::map<address, udp_tracker_connection::connection_cache_entry>
		udp_tracker_connection::m_connection_cache;

	mutex udp_tracker_connection::m_cache_mutex;

	udp_tracker_connection::udp_tracker_connection(
		io_service& ios
		, connection_queue& cc
		, tracker_manager& man
		, tracker_request const& req
		, boost::weak_ptr<request_callback> c
		, aux::session_impl& ses
		, proxy_settings const& proxy)
		: tracker_connection(man, req, ios, c)
//		, m_man(man)
		, m_abort(false)
		, m_transaction_id(0)
		, m_ses(ses)
		, m_attempts(0)
		, m_state(action_error)
		, m_proxy(proxy)
	{
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
			tracker_connection::fail(ec);
			return;
		}
		
		session_settings const& settings = m_ses.settings();

		if (m_proxy.proxy_hostnames
			&& (m_proxy.type == proxy_settings::socks5
				|| m_proxy.type == proxy_settings::socks5_pw))
		{
			m_hostname = hostname;
			m_target.port(port);
			start_announce();
		}
		else
		{
#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("udp_tracker_connection::name_lookup");
#endif
			tcp::resolver::query q(hostname, to_string(port).elems);
			m_ses.m_host_resolver.async_resolve(q
				, boost::bind(
				&udp_tracker_connection::name_lookup, self(), _1, _2));
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
			boost::shared_ptr<request_callback> cb = requester();
			if (cb) cb->debug_log("*** UDP_TRACKER [ initiating name lookup: \"%s\" ]"
				, hostname.c_str());
#endif
		}

		set_timeout(tracker_req().event == tracker_request::stopped
			? settings.stop_tracker_timeout
			: settings.tracker_completion_timeout
			, settings.tracker_receive_timeout);
	}

	void udp_tracker_connection::fail(error_code const& ec, int code
		, char const* msg, int interval, int min_interval)
	{
		// m_target failed. remove it from the endpoint list
		std::list<tcp::endpoint>::iterator i = std::find(m_endpoints.begin()
			, m_endpoints.end(), tcp::endpoint(m_target.address(), m_target.port()));

		if (i != m_endpoints.end()) m_endpoints.erase(i);

		// if that was the last one, fail the whole announce
		if (m_endpoints.empty())
		{
			tracker_connection::fail(ec, code, msg, interval, min_interval);
			return;
		}

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->debug_log("*** UDP_TRACKER [ host: \"%s\" ip: \"%s\" | error: \"%s\" ]"
			, m_hostname.c_str(), print_endpoint(m_target).c_str(), ec.message().c_str());
#endif

		// pick another target endpoint and try again
		m_target = pick_target_endpoint();

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		if (cb) cb->debug_log("*** UDP_TRACKER trying next IP [ host: \"%s\" ip: \"%s\" ]"
			, m_hostname.c_str(), print_endpoint(m_target).c_str());
#endif
		m_ses.m_io_service.post(boost::bind(
			&udp_tracker_connection::start_announce, self()));
	}

	void udp_tracker_connection::name_lookup(error_code const& error
		, tcp::resolver::iterator i)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("udp_tracker_connection::name_lookup");
#endif
		if (m_abort) return;
		if (error == asio::error::operation_aborted) return;
		if (error || i == tcp::resolver::iterator())
		{
			fail(error);
			return;
		}

		boost::shared_ptr<request_callback> cb = requester();
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		if (cb) cb->debug_log("*** UDP_TRACKER [ name lookup successful ]");
#endif
		if (cancelled())
		{
			fail(error_code(errors::torrent_aborted));
			return;
		}

		restart_read_timeout();
		
		// look for an address that has the same kind as the one
		// we're listening on. To make sure the tracker get our
		// correct listening address.

		std::transform(i, tcp::resolver::iterator(), std::back_inserter(m_endpoints)
			, boost::bind(&tcp::resolver::iterator::value_type::endpoint, _1));

		if (tracker_req().apply_ip_filter)
		{
			// remove endpoints that are filtered by the IP filter
			for (std::list<tcp::endpoint>::iterator k = m_endpoints.begin();
				k != m_endpoints.end();)
			{
				if (m_ses.m_ip_filter.access(k->address()) == ip_filter::blocked) 
				{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
					if (cb) cb->debug_log("*** UDP_TRACKER [ IP blocked by filter: %s ]"
						, print_address(k->address()).c_str());
#endif
					k = m_endpoints.erase(k);
				}
				else
					++k;
			}
		}

		// if all endpoints were filtered by the IP filter, we can't connect
		if (m_endpoints.empty())
		{
			fail(error_code(errors::banned_by_ip_filter));
			return;
		}
		
		m_target = pick_target_endpoint();

		if (cb) cb->m_tracker_address = tcp::endpoint(m_target.address(), m_target.port());

		start_announce();
	}

	udp::endpoint udp_tracker_connection::pick_target_endpoint() const
	{
		std::list<tcp::endpoint>::const_iterator iter = m_endpoints.begin();
		udp::endpoint target = udp::endpoint(iter->address(), iter->port());

		if (bind_interface() != address_v4::any())
		{
			// find first endpoint that matches our bind interface type
			for (; iter != m_endpoints.end() && iter->address().is_v4()
				!= bind_interface().is_v4(); ++iter);

			if (iter == m_endpoints.end())
			{
				TORRENT_ASSERT(target.address().is_v4() != bind_interface().is_v4());
				boost::shared_ptr<request_callback> cb = requester();
				if (cb)
				{
					char const* tracker_address_type = target.address().is_v4() ? "IPv4" : "IPv6";
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
				target = udp::endpoint(iter->address(), iter->port());
			}
		}

		return target;
	}

	void udp_tracker_connection::start_announce()
	{
		mutex::scoped_lock l(m_cache_mutex);
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
			fail(ec);
			return;
		}

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->debug_log("*** UDP_TRACKER [ timed out url: %s ]", tracker_req().url.c_str());
#endif
		m_abort = true;
		fail(error_code(errors::timed_out));
	}

	void udp_tracker_connection::close()
	{
		error_code ec;
		tracker_connection::close();
	}

	bool udp_tracker_connection::on_receive_hostname(error_code const& e
		, char const* hostname, char const* buf, int size)
	{
		// just ignore the hostname this came from, pretend that
		// it's from the same endpoint we sent it to (i.e. the same
		// port). We have so many other ways of confirming this packet
		// comes from the tracker anyway, so it's not a big deal
		return on_receive(e, m_target, buf, size);
	}

	bool udp_tracker_connection::on_receive(error_code const& e
		, udp::endpoint const& ep, char const* buf, int size)
	{
		// ignore resposes before we've sent any requests
		if (m_state == action_error) return false;

		if (m_abort) return false;

		// ignore packet not sent from the tracker
		// if m_target is inaddr_any, it suggests that we
		// sent the packet through a proxy only knowing
		// the hostname, in which case this packet might be for us
		if (!is_any(m_target.address()) && m_target != ep) return false;
		
		if (e) fail(e);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			cb->debug_log("<== UDP_TRACKER_PACKET [ size: %d ]", size);
		}
#endif

		// ignore packets smaller than 8 bytes
		if (size < 8) return false;

		const char* ptr = buf;
		int action = detail::read_int32(ptr);
		int transaction = detail::read_int32(ptr);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		if (cb)
		{
			cb->debug_log("*** UDP_TRACKER_PACKET [ action: %d ]", action);
		}
#endif

		// ignore packets with incorrect transaction id
		if (m_transaction_id != transaction) return false;

		if (action == action_error)
		{
			fail(error_code(errors::tracker_failure), -1, std::string(ptr, size - 8).c_str());
			return true;
		}

		// ignore packets that's not a response to our message
		if (action != m_state) return false;

		restart_read_timeout();

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		if (cb)
		{
			cb->debug_log("*** UDP_TRACKER_RESPONSE [ tid: %x ]"
				, int(transaction));
		}
#endif

		switch (m_state)
		{
			case action_connect:
				return on_connect_response(buf, size);
			case action_announce:
				return on_announce_response(buf, size);
			case action_scrape:
				return on_scrape_response(buf, size);
			default: break;
		}
		return false;
	}
	
	bool udp_tracker_connection::on_connect_response(char const* buf, int size)
	{
		// ignore packets smaller than 16 bytes
		if (size < 16) return false;

		restart_read_timeout();
		buf += 8; // skip header

		// reset transaction
		m_transaction_id = 0;
		m_attempts = 0;
		boost::uint64_t connection_id = detail::read_int64(buf);

		mutex::scoped_lock l(m_cache_mutex);
		connection_cache_entry& cce = m_connection_cache[m_target.address()];
		cce.connection_id = connection_id;
		cce.expires = time_now() + seconds(m_ses.m_settings.udp_tracker_token_expiry);

		if (tracker_req().kind == tracker_request::announce_request)
			send_udp_announce();
		else if (tracker_req().kind == tracker_request::scrape_request)
			send_udp_scrape();
		return true;
	}

	void udp_tracker_connection::send_udp_connect()
	{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			char hex_ih[41];
			to_hex((char const*)&tracker_req().info_hash[0], 20, hex_ih);
			cb->debug_log("==> UDP_TRACKER_CONNECT [%s]", hex_ih);
		}
#endif
		if (m_abort) return;

		char buf[16];
		char* ptr = buf;

		if (m_transaction_id == 0)
			m_transaction_id = random() ^ (random() << 16);

		detail::write_uint32(0x417, ptr);
		detail::write_uint32(0x27101980, ptr); // connection_id
		detail::write_int32(action_connect, ptr); // action (connect)
		detail::write_int32(m_transaction_id, ptr); // transaction_id
		TORRENT_ASSERT(ptr - buf == sizeof(buf));

		error_code ec;
		if (!m_hostname.empty())
		{
			m_ses.m_udp_socket.send_hostname(m_hostname.c_str(), m_target.port(), buf, 16, ec);
		}
		else
		{
			m_ses.m_udp_socket.send(m_target, buf, 16, ec);
		}
		m_state = action_connect;
		sent_bytes(16 + 28); // assuming UDP/IP header
		++m_attempts;
		if (ec)
		{
			fail(ec);
			return;
		}
	}

	void udp_tracker_connection::send_udp_scrape()
	{
		if (m_transaction_id == 0)
			m_transaction_id = random() ^ (random() << 16);

		if (m_abort) return;

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
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		out += 20;
		TORRENT_ASSERT(out - buf == sizeof(buf));
#endif

		error_code ec;
		if (!m_hostname.empty())
		{
			m_ses.m_udp_socket.send_hostname(m_hostname.c_str(), m_target.port(), buf, sizeof(buf), ec);
		}
		else
		{
			m_ses.m_udp_socket.send(m_target, buf, sizeof(buf), ec);
		}
		m_state = action_scrape;
		sent_bytes(sizeof(buf) + 28); // assuming UDP/IP header
		++m_attempts;
		if (ec)
		{
			fail(ec);
			return;
		}
	}

	bool udp_tracker_connection::on_announce_response(char const* buf, int size)
	{
		if (size < 20) return false;

		buf += 8; // skip header
		restart_read_timeout();
		int interval = detail::read_int32(buf);
		int min_interval = 60;
		int incomplete = detail::read_int32(buf);
		int complete = detail::read_int32(buf);
		int num_peers = (size - 20) / 6;
		if ((size - 20) % 6 != 0)
		{
			fail(error_code(errors::invalid_tracker_response_length));
			return false;
		}

		boost::shared_ptr<request_callback> cb = requester();
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		if (cb)
		{
			boost::shared_ptr<request_callback> cb = requester();
			cb->debug_log("<== UDP_TRACKER_RESPONSE [ url: %s ]", tracker_req().url.c_str());
		}
#endif

		if (!cb)
		{
			close();
			return true;
		}

		std::vector<peer_entry> peer_list;
		for (int i = 0; i < num_peers; ++i)
		{
			// TODO: don't use a string here. The problem is that
			// some trackers will respond with actual strings.
			// Especially i2p trackers
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
		for (std::list<tcp::endpoint>::const_iterator i = m_endpoints.begin()
			, end(m_endpoints.end()); i != end; ++i)
		{
			ip_list.push_back(i->address());
		}

		cb->tracker_response(tracker_req(), m_target.address(), ip_list
			, peer_list, interval, min_interval, complete, incomplete, address(), "" /*trackerid*/);

		close();
		return true;
	}

	bool udp_tracker_connection::on_scrape_response(char const* buf, int size)
	{
		restart_read_timeout();
		int action = detail::read_int32(buf);
		int transaction = detail::read_int32(buf);

		if (transaction != m_transaction_id)
		{
			fail(error_code(errors::invalid_tracker_transaction_id));
			return false;
		}

		if (action == action_error)
		{
			fail(error_code(errors::tracker_failure), -1, std::string(buf, size - 8).c_str());
			return true;
		}

		if (action != action_scrape)
		{
			fail(error_code(errors::invalid_tracker_action));
			return true;
		}

		if (size < 20)
		{
			fail(error_code(errors::invalid_tracker_response_length));
			return true;
		}

		int complete = detail::read_int32(buf);
		int downloaded = detail::read_int32(buf);
		int incomplete = detail::read_int32(buf);

		boost::shared_ptr<request_callback> cb = requester();
		if (!cb)
		{
			close();
			return true;
		}
		
		cb->tracker_scrape_response(tracker_req()
			, complete, incomplete, downloaded, -1);

		close();
		return true;
	}

	void udp_tracker_connection::send_udp_announce()
	{
		if (m_transaction_id == 0)
			m_transaction_id = random() ^ (random() << 16);

		if (m_abort) return;

		char buf[800];
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
		address_v4 announce_ip;

		if (!m_ses.settings().anonymous_mode
			&& !settings.announce_ip.empty())
		{
			error_code ec;
			address ip = address::from_string(settings.announce_ip.c_str(), ec);
			if (!ec && ip.is_v4()) announce_ip = ip.to_v4();
		}
		detail::write_uint32(announce_ip.to_ulong(), out);
		detail::write_int32(req.key, out); // key
		detail::write_int32(req.num_want, out); // num_want
		detail::write_uint16(req.listen_port, out); // port

		std::string request_string;
		error_code ec;
		using boost::tuples::ignore;
		boost::tie(ignore, ignore, ignore, ignore, request_string) = parse_url_components(req.url, ec);
		if (ec) request_string.clear();

		if (!request_string.empty())
		{
			int str_len = (std::min)(int(request_string.size()), 255);
			request_string.resize(str_len);

			detail::write_uint8(2, out);
			detail::write_uint8(str_len, out);
			detail::write_string(request_string, out);
		}

		TORRENT_ASSERT(out - buf <= sizeof(buf));

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			char hex_ih[41];
			to_hex((char const*)&req.info_hash[0], 20, hex_ih);
			cb->debug_log("==> UDP_TRACKER_ANNOUNCE [%s]", hex_ih);
		}
#endif

		if (!m_hostname.empty())
		{
			m_ses.m_udp_socket.send_hostname(m_hostname.c_str(), m_target.port(), buf, out - buf, ec);
		}
		else
		{
			m_ses.m_udp_socket.send(m_target, buf, out - buf, ec);
		}
		m_state = action_announce;
		sent_bytes(out - buf + 28); // assuming UDP/IP header
		++m_attempts;
		if (ec)
		{
			fail(ec);
			return;
		}
	}

}

