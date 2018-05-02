/*

Copyright (c) 2003-2018, Arvid Norberg
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

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <vector>
#include <cctype>

#include <boost/bind.hpp>
#include <boost/tuple/tuple.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/udp_tracker_connection.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/hex.hpp"
#include "libtorrent/broadcast_socket.hpp" // for is_any
#include "libtorrent/random.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/resolver_interface.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/aux_/time.hpp"

#ifndef TORRENT_DISABLE_LOGGING
#include "libtorrent/socket_io.hpp"
#endif

namespace libtorrent
{

	std::map<address, udp_tracker_connection::connection_cache_entry>
		udp_tracker_connection::m_connection_cache;

	mutex udp_tracker_connection::m_cache_mutex;

	udp_tracker_connection::udp_tracker_connection(
		io_service& ios
		, tracker_manager& man
		, tracker_request const& req
		, boost::weak_ptr<request_callback> c)
		: tracker_connection(man, req, ios, c)
		, m_transaction_id(0)
		, m_attempts(0)
		, m_state(action_error)
		, m_abort(false)
	{
		update_transaction_id();
	}

	void udp_tracker_connection::start()
	{
		// TODO: 2 support authentication here. tracker_req().auth
		std::string hostname;
		std::string protocol;
		int port;
		error_code ec;

		using boost::tuples::ignore;
		boost::tie(protocol, ignore, hostname, port, ignore)
			= parse_url_components(tracker_req().url, ec);
		if (port == -1) port = protocol == "http" ? 80 : 443;

		if (ec)
		{
			tracker_connection::fail(ec);
			return;
		}
		
		aux::session_settings const& settings = m_man.settings();

		if (settings.get_bool(settings_pack::proxy_hostnames)
			&& (settings.get_int(settings_pack::proxy_type) == settings_pack::socks5
				|| settings.get_int(settings_pack::proxy_type) == settings_pack::socks5_pw))
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
			// when stopping, pass in the cache-only flag, because we
			// don't want to get stuck on DNS lookups when shutting down
			m_man.host_resolver().async_resolve(hostname
				, tracker_req().event == tracker_request::stopped
					? resolver_interface::cache_only : 0
					| resolver_interface::abort_on_shutdown
				, boost::bind(&udp_tracker_connection::name_lookup
					, shared_from_this(), _1, _2, port));

#ifndef TORRENT_DISABLE_LOGGING
			boost::shared_ptr<request_callback> cb = requester();
			if (cb) cb->debug_log("*** UDP_TRACKER [ initiating name lookup: \"%s\" ]"
				, hostname.c_str());
#endif
		}

		set_timeout(tracker_req().event == tracker_request::stopped
			? settings.get_int(settings_pack::stop_tracker_timeout)
			: settings.get_int(settings_pack::tracker_completion_timeout)
			, settings.get_int(settings_pack::tracker_receive_timeout));
	}

	void udp_tracker_connection::fail(error_code const& ec, int code
		, char const* msg, int interval, int min_interval)
	{
		// m_target failed. remove it from the endpoint list
		std::vector<tcp::endpoint>::iterator i = std::find(m_endpoints.begin()
			, m_endpoints.end(), tcp::endpoint(m_target.address(), m_target.port()));

		if (i != m_endpoints.end()) m_endpoints.erase(i);

		// if that was the last one, fail the whole announce
		if (m_endpoints.empty())
		{
			tracker_connection::fail(ec, code, msg, interval, min_interval);
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->debug_log("*** UDP_TRACKER [ host: \"%s\" ip: \"%s\" | error: \"%s\" ]"
			, m_hostname.c_str(), print_endpoint(m_target).c_str(), ec.message().c_str());
#endif

		// pick another target endpoint and try again
		m_target = pick_target_endpoint();

#ifndef TORRENT_DISABLE_LOGGING
		if (cb) cb->debug_log("*** UDP_TRACKER trying next IP [ host: \"%s\" ip: \"%s\" ]"
			, m_hostname.c_str(), print_endpoint(m_target).c_str());
#endif
		get_io_service().post(boost::bind(
			&udp_tracker_connection::start_announce, shared_from_this()));

		aux::session_settings const& settings = m_man.settings();
		set_timeout(tracker_req().event == tracker_request::stopped
			? settings.get_int(settings_pack::stop_tracker_timeout)
			: settings.get_int(settings_pack::tracker_completion_timeout)
			, settings.get_int(settings_pack::tracker_receive_timeout));
	}

	void udp_tracker_connection::name_lookup(error_code const& error
		, std::vector<address> const& addresses, int port)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("udp_tracker_connection::name_lookup");
#endif
		if (m_abort) return;
		if (error == boost::asio::error::operation_aborted) return;
		if (error || addresses.empty())
		{
			fail(error);
			return;
		}

		boost::shared_ptr<request_callback> cb = requester();
#ifndef TORRENT_DISABLE_LOGGING
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

		for (std::vector<address>::const_iterator i = addresses.begin()
			, end(addresses.end()); i != end; ++i)
			m_endpoints.push_back(tcp::endpoint(*i, port));

		if (tracker_req().filter)
		{
			// remove endpoints that are filtered by the IP filter
			for (std::vector<tcp::endpoint>::iterator k = m_endpoints.begin();
				k != m_endpoints.end();)
			{
				if (tracker_req().filter->access(k->address()) == ip_filter::blocked) 
				{
#ifndef TORRENT_DISABLE_LOGGING
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

		start_announce();
	}

	udp::endpoint udp_tracker_connection::pick_target_endpoint() const
	{
		std::vector<tcp::endpoint>::const_iterator iter = m_endpoints.begin();
		udp::endpoint target = udp::endpoint(iter->address(), iter->port());

		if (tracker_req().bind_ip)
		{
			// find first endpoint that matches our bind interface type
			for (; iter != m_endpoints.end() && iter->address().is_v4()
				!= tracker_req().bind_ip->is_v4(); ++iter);

			if (iter == m_endpoints.end())
			{
				TORRENT_ASSERT(target.address().is_v4() != tracker_req().bind_ip->is_v4());
				boost::shared_ptr<request_callback> cb = requester();
				if (cb)
				{
					char const* tracker_address_type = target.address().is_v4() ? "IPv4" : "IPv6";
					char const* bind_address_type = tracker_req().bind_ip->is_v4() ? "IPv4" : "IPv6";
					char msg[200];
					snprintf(msg, sizeof(msg)
						, "the tracker only resolves to an %s address, and you're "
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
			if (aux::time_now() < cc->second.expires)
			{
				if (0 == (tracker_req().kind & tracker_request::scrape_request))
					send_udp_announce();
				else if (0 != (tracker_req().kind & tracker_request::scrape_request))
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

#ifndef TORRENT_DISABLE_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->debug_log("*** UDP_TRACKER [ timed out url: %s ]", tracker_req().url.c_str());
#endif
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
		TORRENT_UNUSED(hostname);
		// just ignore the hostname this came from, pretend that
		// it's from the same endpoint we sent it to (i.e. the same
		// port). We have so many other ways of confirming this packet
		// comes from the tracker anyway, so it's not a big deal
		return on_receive(e, m_target, buf, size);
	}

	bool udp_tracker_connection::on_receive(error_code const& e
		, udp::endpoint const& ep, char const* buf, int size)
	{
#ifndef TORRENT_DISABLE_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
#endif

		// ignore resposes before we've sent any requests
		if (m_state == action_error)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (cb) cb->debug_log("<== UDP_TRACKER [ m_action == error ]");
#endif
			return false;
		}

		if (m_abort)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (cb) cb->debug_log("<== UDP_TRACKER [ aborted]");
#endif
			return false;
		}

		// ignore packet not sent from the tracker
		// if m_target is inaddr_any, it suggests that we
		// sent the packet through a proxy only knowing
		// the hostname, in which case this packet might be for us
		if (!is_any(m_target.address()) && m_target != ep)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (cb) cb->debug_log("<== UDP_TRACKER [ unexpected source IP: %s "
				"expected: %s ]"
				, print_endpoint(ep).c_str()
				, print_endpoint(m_target).c_str());
#endif
			return false;
		}

		if (e) fail(e);

#ifndef TORRENT_DISABLE_LOGGING
		if (cb) cb->debug_log("<== UDP_TRACKER_PACKET [ size: %d ]", size);
#endif

		// ignore packets smaller than 8 bytes
		if (size < 8) return false;

		const char* ptr = buf;
		int action = detail::read_int32(ptr);
		boost::uint32_t transaction = detail::read_uint32(ptr);

#ifndef TORRENT_DISABLE_LOGGING
		if (cb) cb->debug_log("*** UDP_TRACKER_PACKET [ action: %d ]", action);
#endif

		// ignore packets with incorrect transaction id
		if (m_transaction_id != transaction)
		{
#ifndef TORRENT_DISABLE_LOGGING
		if (cb) cb->debug_log("*** UDP_TRACKER_PACKET [ tid: %x ]"
				, int(transaction));
#endif
			return false;
		}

		if (action == action_error)
		{
			fail(error_code(errors::tracker_failure), -1, std::string(ptr, size - 8).c_str());
			return true;
		}

		// ignore packets that's not a response to our message
		if (action != m_state)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (cb) cb->debug_log("*** UDP_TRACKER_PACKET [ unexpected action: %d "
				" expected: %d ]", action, m_state);
#endif
			return false;
		}

		restart_read_timeout();

#ifndef TORRENT_DISABLE_LOGGING
		if (cb)
			cb->debug_log("*** UDP_TRACKER_RESPONSE [ tid: %x ]"
				, int(transaction));
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

	void udp_tracker_connection::update_transaction_id()
	{
		boost::uint32_t new_tid;

		// don't use 0, because that has special meaning (unintialized)
		do {
			new_tid = random();
		} while (new_tid == 0);

		if (m_transaction_id != 0)
			m_man.update_transaction_id(shared_from_this(), new_tid);
		m_transaction_id = new_tid;
	}

	bool udp_tracker_connection::on_connect_response(char const* buf, int size)
	{
		// ignore packets smaller than 16 bytes
		if (size < 16) return false;

		restart_read_timeout();
		buf += 8; // skip header

		// reset transaction
		update_transaction_id();
		boost::uint64_t connection_id = detail::read_int64(buf);

		mutex::scoped_lock l(m_cache_mutex);
		connection_cache_entry& cce = m_connection_cache[m_target.address()];
		cce.connection_id = connection_id;
		cce.expires = aux::time_now() + seconds(m_man.settings().get_int(settings_pack::udp_tracker_token_expiry));

		if (0 == (tracker_req().kind & tracker_request::scrape_request))
			send_udp_announce();
		else if (0 != (tracker_req().kind & tracker_request::scrape_request))
			send_udp_scrape();
		return true;
	}

	void udp_tracker_connection::send_udp_connect()
	{
#ifndef TORRENT_DISABLE_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
#endif

		if (m_abort)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (cb) cb->debug_log("==> UDP_TRACKER_CONNECT [ skipped, m_abort ]");
#endif
			return;
		}

		char buf[16];
		char* ptr = buf;

		TORRENT_ASSERT(m_transaction_id != 0);

		detail::write_uint32(0x417, ptr);
		detail::write_uint32(0x27101980, ptr); // connection_id
		detail::write_int32(action_connect, ptr); // action (connect)
		detail::write_int32(m_transaction_id, ptr); // transaction_id
		TORRENT_ASSERT(ptr - buf == sizeof(buf));

		error_code ec;
		if (!m_hostname.empty())
		{
			m_man.get_udp_socket().send_hostname(m_hostname.c_str()
				, m_target.port(), buf, 16, ec
				, udp_socket::tracker_connection);
		}
		else
		{
			m_man.get_udp_socket().send(m_target, buf, 16, ec
				, udp_socket::tracker_connection);
		}

		++m_attempts;
		if (ec)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (cb) cb->debug_log("==> UDP_TRACKER_CONNECT [ failed: %s ]"
				, ec.message().c_str());
#endif
			fail(ec);
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (cb)
		{
			char hex_ih[41];
			to_hex(tracker_req().info_hash.data(), 20, hex_ih);
			cb->debug_log("==> UDP_TRACKER_CONNECT [ to: %s ih: %s]"
				, m_hostname.empty()
					? print_endpoint(m_target).c_str()
					: (m_hostname + ":" + to_string(m_target.port()).elems).c_str()
				, hex_ih);
		}
#endif

		m_state = action_connect;
		sent_bytes(16 + 28); // assuming UDP/IP header
	}

	void udp_tracker_connection::send_udp_scrape()
	{
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
#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS
		out += 20;
		TORRENT_ASSERT(out - buf == sizeof(buf));
#endif

		error_code ec;
		if (!m_hostname.empty())
		{
			m_man.get_udp_socket().send_hostname(m_hostname.c_str(), m_target.port()
				, buf, sizeof(buf), ec, udp_socket::tracker_connection);
		}
		else
		{
			m_man.get_udp_socket().send(m_target, buf, sizeof(buf), ec
				, udp_socket::tracker_connection);
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

		tracker_response resp;

		resp.interval = detail::read_int32(buf);
		resp.min_interval = 60;
		resp.incomplete = detail::read_int32(buf);
		resp.complete = detail::read_int32(buf);

		std::size_t const ip_stride =
#if TORRENT_USE_IPV6
			m_target.address().is_v6() ? 18 :
#endif
			6;

		int const num_peers = (size - 20) / ip_stride;
		if ((size - 20) % ip_stride != 0)
		{
			fail(error_code(errors::invalid_tracker_response_length));
			return false;
		}

		boost::shared_ptr<request_callback> cb = requester();
#ifndef TORRENT_DISABLE_LOGGING
		if (cb)
		{
			cb->debug_log("<== UDP_TRACKER_RESPONSE [ url: %s ]", tracker_req().url.c_str());
		}
#endif

		if (!cb)
		{
			close();
			return true;
		}

#if TORRENT_USE_IPV6
		if (m_target.address().is_v6())
		{
			resp.peers6.reserve(std::size_t(num_peers));
			for (int i = 0; i < num_peers; ++i)
			{
				ipv6_peer_entry e;
				std::memcpy(&e.ip[0], buf, 16);
				buf += 16;
				e.port = detail::read_uint16(buf);
				resp.peers6.push_back(e);
			}
		}
		else
#endif
		{
			resp.peers4.reserve(std::size_t(num_peers));
			for (int i = 0; i < num_peers; ++i)
			{
				ipv4_peer_entry e;
				memcpy(&e.ip[0], buf, 4);
				buf += 4;
				e.port = detail::read_uint16(buf);
				resp.peers4.push_back(e);
			}
		}

		std::list<address> ip_list;
		for (std::vector<tcp::endpoint>::const_iterator i = m_endpoints.begin()
			, end(m_endpoints.end()); i != end; ++i)
		{
			ip_list.push_back(i->address());
		}

		cb->tracker_response(tracker_req(), m_target.address(), ip_list
			, resp);

		close();
		return true;
	}

	bool udp_tracker_connection::on_scrape_response(char const* buf, int size)
	{
		restart_read_timeout();
		int action = detail::read_int32(buf);
		boost::uint32_t transaction = detail::read_uint32(buf);

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
		if (m_abort) return;

		char buf[800];
		char* out = buf;

		tracker_request const& req = tracker_req();
		const bool stats = req.send_stats;
		aux::session_settings const& settings = m_man.settings();

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

		if (!settings.get_bool(settings_pack::anonymous_mode)
			&& !settings.get_str(settings_pack::announce_ip).empty())
		{
			error_code ec;
			address ip = address::from_string(settings.get_str(settings_pack::announce_ip).c_str(), ec);
			if (!ec && ip.is_v4()) announce_ip = ip.to_v4();
		}
		detail::write_uint32(announce_ip.to_ulong(), out);
		detail::write_int32(req.key, out); // key
		detail::write_int32(req.num_want, out); // num_want
		detail::write_uint16(req.listen_port, out); // port

		std::string request_string;
		error_code ec;
		using boost::tuples::ignore;
		boost::tie(ignore, ignore, ignore, ignore, request_string)
			= parse_url_components(req.url, ec);
		if (ec) request_string.clear();

		if (!request_string.empty())
		{
			int str_len = (std::min)(int(request_string.size()), 255);
			request_string.resize(str_len);

			detail::write_uint8(2, out);
			detail::write_uint8(str_len, out);
			detail::write_string(request_string, out);
		}

		TORRENT_ASSERT(out - buf <= int(sizeof(buf)));

#ifndef TORRENT_DISABLE_LOGGING
		boost::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			char hex_ih[41];
			to_hex(req.info_hash.data(), 20, hex_ih);
			cb->debug_log("==> UDP_TRACKER_ANNOUNCE [%s]", hex_ih);
		}
#endif

		if (!m_hostname.empty())
		{
			m_man.get_udp_socket().send_hostname(m_hostname.c_str()
				, m_target.port(), buf, out - buf, ec
				, udp_socket::tracker_connection);
		}
		else
		{
			m_man.get_udp_socket().send(m_target, buf, out - buf, ec
				, udp_socket::tracker_connection);
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

