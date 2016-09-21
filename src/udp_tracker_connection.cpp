/*

Copyright (c) 2003-2016, Arvid Norberg
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
#include <cctype>
#include <mutex>
#include <functional>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <tuple>

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
#include "libtorrent/aux_/io.hpp"
#include "libtorrent/span.hpp"

#ifndef TORRENT_DISABLE_LOGGING
#include "libtorrent/socket_io.hpp"
#endif

using namespace std::placeholders;

namespace libtorrent
{

	std::map<address, udp_tracker_connection::connection_cache_entry>
		udp_tracker_connection::m_connection_cache;

	std::mutex udp_tracker_connection::m_cache_mutex;

	udp_tracker_connection::udp_tracker_connection(
		io_service& ios
		, tracker_manager& man
		, tracker_request const& req
		, std::weak_ptr<request_callback> c)
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

		using std::ignore;
		std::tie(protocol, ignore, hostname, port, ignore)
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
			ADD_OUTSTANDING_ASYNC("udp_tracker_connection::name_lookup");
			// when stopping, pass in the prefer cache flag, because we
			// don't want to get stuck on DNS lookups when shutting down
			// if we can avoid it
			m_man.host_resolver().async_resolve(hostname
				, tracker_req().event == tracker_request::stopped
					? resolver_interface::prefer_cache
					: resolver_interface::abort_on_shutdown
				, std::bind(&udp_tracker_connection::name_lookup
					, shared_from_this(), _1, _2, port));

#ifndef TORRENT_DISABLE_LOGGING
			std::shared_ptr<request_callback> cb = requester();
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
		std::shared_ptr<request_callback> cb = requester();
		if (cb && cb->should_log())
		{
			cb->debug_log("*** UDP_TRACKER [ host: \"%s\" ip: \"%s\" | error: \"%s\" ]"
				, m_hostname.c_str(), print_endpoint(m_target).c_str(), ec.message().c_str());
		}
#endif

		// pick another target endpoint and try again
		m_target = pick_target_endpoint();

#ifndef TORRENT_DISABLE_LOGGING
		if (cb && cb->should_log())
		{
			cb->debug_log("*** UDP_TRACKER trying next IP [ host: \"%s\" ip: \"%s\" ]"
				, m_hostname.c_str(), print_endpoint(m_target).c_str());
		}
#endif
		get_io_service().post(std::bind(
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
		COMPLETE_ASYNC("udp_tracker_connection::name_lookup");
		if (m_abort) return;
		if (error == boost::asio::error::operation_aborted) return;
		if (error || addresses.empty())
		{
			fail(error);
			return;
		}

		std::shared_ptr<request_callback> cb = requester();
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
					if (cb && cb->should_log())
					{
						cb->debug_log("*** UDP_TRACKER [ IP blocked by filter: %s ]"
							, print_address(k->address()).c_str());
					}
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

		if (bind_interface() != address_v4::any())
		{
			// find first endpoint that matches our bind interface type
			for (; iter != m_endpoints.end() && iter->address().is_v4()
				!= bind_interface().is_v4(); ++iter);

			if (iter == m_endpoints.end())
			{
				TORRENT_ASSERT(target.address().is_v4() != bind_interface().is_v4());
				std::shared_ptr<request_callback> cb = requester();
				if (cb)
				{
					char const* tracker_address_type = target.address().is_v4() ? "IPv4" : "IPv6";
					char const* bind_address_type = bind_interface().is_v4() ? "IPv4" : "IPv6";
					char msg[200];
					std::snprintf(msg, sizeof(msg)
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
		std::unique_lock<std::mutex> l(m_cache_mutex);
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
		std::shared_ptr<request_callback> cb = requester();
		if (cb) cb->debug_log("*** UDP_TRACKER [ timed out url: %s ]", tracker_req().url.c_str());
#endif
		fail(error_code(errors::timed_out));
	}

	void udp_tracker_connection::close()
	{
		error_code ec;
		tracker_connection::close();
	}

	bool udp_tracker_connection::on_receive_hostname(char const* hostname
		, span<char const> buf)
	{
		TORRENT_UNUSED(hostname);
		// just ignore the hostname this came from, pretend that
		// it's from the same endpoint we sent it to (i.e. the same
		// port). We have so many other ways of confirming this packet
		// comes from the tracker anyway, so it's not a big deal
		return on_receive(m_target, buf);
	}

	bool udp_tracker_connection::on_receive(udp::endpoint const& ep
		, span<char const> const buf)
	{
#ifndef TORRENT_DISABLE_LOGGING
		std::shared_ptr<request_callback> cb = requester();
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
			if (cb && cb->should_log())
			{
				cb->debug_log("<== UDP_TRACKER [ unexpected source IP: %s "
					"expected: %s ]"
					, print_endpoint(ep).c_str()
					, print_endpoint(m_target).c_str());
			}
#endif
			return false;
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (cb) cb->debug_log("<== UDP_TRACKER_PACKET [ size: %d ]"
			, int(buf.size()));
#endif

		// ignore packets smaller than 8 bytes
		if (buf.size() < 8) return false;

		span<const char> ptr = buf;
		int const action = aux::read_int32(ptr);
		std::uint32_t const transaction = aux::read_uint32(ptr);

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
			fail(error_code(errors::tracker_failure), -1
				, std::string(buf.data(), buf.size()).c_str());
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
				return on_connect_response(buf);
			case action_announce:
				return on_announce_response(buf);
			case action_scrape:
				return on_scrape_response(buf);
			default: break;
		}
		return false;
	}

	void udp_tracker_connection::update_transaction_id()
	{
		std::uint32_t new_tid;

		// don't use 0, because that has special meaning (unintialized)
		new_tid = random(0xfffffffe) + 1;

		if (m_transaction_id != 0)
			m_man.update_transaction_id(shared_from_this(), new_tid);
		m_transaction_id = new_tid;
	}

	bool udp_tracker_connection::on_connect_response(span<char const> buf)
	{
		// ignore packets smaller than 16 bytes
		if (buf.size() < 16) return false;

		restart_read_timeout();

		// skip header
		buf = buf.subspan(8);

		// reset transaction
		update_transaction_id();
		std::uint64_t const connection_id = aux::read_int64(buf);

		std::lock_guard<std::mutex> l(m_cache_mutex);
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
		std::shared_ptr<request_callback> cb = requester();
#endif

		if (m_abort)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (cb) cb->debug_log("==> UDP_TRACKER_CONNECT [ skipped, m_abort ]");
#endif
			return;
		}

		char buf[16];
		span<char> view = buf;

		TORRENT_ASSERT(m_transaction_id != 0);

		aux::write_uint32(0x417, view);
		aux::write_uint32(0x27101980, view); // connection_id
		aux::write_int32(action_connect, view); // action (connect)
		aux::write_int32(m_transaction_id, view); // transaction_id
		TORRENT_ASSERT(view.size() == 0);

		error_code ec;
		if (!m_hostname.empty())
		{
			m_man.send_hostname(m_hostname.c_str()
				, m_target.port(), buf, ec
				, udp_socket::tracker_connection);
		}
		else
		{
			m_man.send(m_target, buf, ec
				, udp_socket::tracker_connection);
		}

		++m_attempts;
		if (ec)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (cb && cb->should_log())
			{
				cb->debug_log("==> UDP_TRACKER_CONNECT [ failed: %s ]"
					, ec.message().c_str());
			}
#endif
			fail(ec);
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (cb && cb->should_log())
		{
			char hex_ih[41];
			aux::to_hex(tracker_req().info_hash, hex_ih);
			cb->debug_log("==> UDP_TRACKER_CONNECT [ to: %s ih: %s]"
				, m_hostname.empty()
					? print_endpoint(m_target).c_str()
					: (m_hostname + ":" + to_string(m_target.port()).data()).c_str()
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
		span<char> view = buf;

		aux::write_int64(i->second.connection_id, view); // connection_id
		aux::write_int32(action_scrape, view); // action (scrape)
		aux::write_int32(m_transaction_id, view); // transaction_id
		// info_hash
		std::copy(tracker_req().info_hash.begin(), tracker_req().info_hash.end()
			, view.data());
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(view.size() == 20);
#endif

		error_code ec;
		if (!m_hostname.empty())
		{
			m_man.send_hostname(m_hostname.c_str(), m_target.port()
				, buf, ec, udp_socket::tracker_connection);
		}
		else
		{
			m_man.send(m_target, buf, ec
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

	bool udp_tracker_connection::on_announce_response(span<char const> buf)
	{
		if (buf.size() < 20) return false;

		buf = buf.subspan(8);
		restart_read_timeout();

		tracker_response resp;

		resp.interval = aux::read_int32(buf);
		resp.min_interval = 60;
		resp.incomplete = aux::read_int32(buf);
		resp.complete = aux::read_int32(buf);
		int const num_peers = int(buf.size()) / 6;
		if ((buf.size() % 6) != 0)
		{
			fail(error_code(errors::invalid_tracker_response_length));
			return false;
		}

		std::shared_ptr<request_callback> cb = requester();
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

		std::vector<peer_entry> peer_list;
		resp.peers4.reserve(num_peers);
		for (int i = 0; i < num_peers; ++i)
		{
			ipv4_peer_entry e;
			memcpy(&e.ip[0], buf.data(), 4);
			buf = buf.subspan(4);
			e.port = aux::read_uint16(buf);
			resp.peers4.push_back(e);
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

	bool udp_tracker_connection::on_scrape_response(span<char const> buf)
	{
		using namespace libtorrent::aux;

		restart_read_timeout();
		int const action = aux::read_int32(buf);
		std::uint32_t const transaction = read_uint32(buf);

		if (transaction != m_transaction_id)
		{
			fail(error_code(errors::invalid_tracker_transaction_id));
			return false;
		}

		if (action == action_error)
		{
			fail(error_code(errors::tracker_failure), -1
				, std::string(buf.data(), buf.size()).c_str());
			return true;
		}

		if (action != action_scrape)
		{
			fail(error_code(errors::invalid_tracker_action));
			return true;
		}

		if (buf.size() < 12)
		{
			fail(error_code(errors::invalid_tracker_response_length));
			return true;
		}

		int const complete = aux::read_int32(buf);
		int const downloaded = aux::read_int32(buf);
		int const incomplete = aux::read_int32(buf);

		std::shared_ptr<request_callback> cb = requester();
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
		span<char> out = buf;

		tracker_request const& req = tracker_req();
		aux::session_settings const& settings = m_man.settings();

		std::map<address, connection_cache_entry>::iterator i
			= m_connection_cache.find(m_target.address());
		// this isn't really supposed to happen
		TORRENT_ASSERT(i != m_connection_cache.end());
		if (i == m_connection_cache.end()) return;

		aux::write_int64(i->second.connection_id, out); // connection_id
		aux::write_int32(action_announce, out); // action (announce)
		aux::write_int32(m_transaction_id, out); // transaction_id
		std::copy(req.info_hash.begin(), req.info_hash.end(), out.data()); // info_hash
		out.subspan(20);
		std::copy(req.pid.begin(), req.pid.end(), out.data()); // peer_id
		out.subspan(20);
		aux::write_int64(req.downloaded, out); // downloaded
		aux::write_int64(req.left, out); // left
		aux::write_int64(req.uploaded, out); // uploaded
		aux::write_int32(req.event, out); // event
		// ip address
		address_v4 announce_ip;

		if (!settings.get_bool(settings_pack::anonymous_mode)
			&& !settings.get_str(settings_pack::announce_ip).empty())
		{
			error_code ec;
			address ip = address::from_string(settings.get_str(settings_pack::announce_ip).c_str(), ec);
			if (!ec && ip.is_v4()) announce_ip = ip.to_v4();
		}
		aux::write_uint32(announce_ip.to_ulong(), out);
		aux::write_int32(req.key, out); // key
		aux::write_int32(req.num_want, out); // num_want
		aux::write_uint16(req.listen_port, out); // port

		std::string request_string;
		error_code ec;
		using std::ignore;
		std::tie(ignore, ignore, ignore, ignore, request_string)
			= parse_url_components(req.url, ec);
		if (ec) request_string.clear();

		if (!request_string.empty())
		{
			int str_len = (std::min)(int(request_string.size()), 255);
			request_string.resize(str_len);

			aux::write_uint8(2, out);
			aux::write_uint8(str_len, out);
			aux::write_string(request_string, out);
		}

#ifndef TORRENT_DISABLE_LOGGING
		std::shared_ptr<request_callback> cb = requester();
		if (cb && cb->should_log())
		{
			char hex_ih[41];
			aux::to_hex(req.info_hash, hex_ih);
			cb->debug_log("==> UDP_TRACKER_ANNOUNCE [%s]", hex_ih);
		}
#endif

		if (!m_hostname.empty())
		{
			m_man.send_hostname(m_hostname.c_str()
				, m_target.port(), span<char const>(buf
					, int(sizeof(buf) - out.size())), ec
				, udp_socket::tracker_connection);
		}
		else
		{
			m_man.send(m_target, span<char const>(buf
					, int(sizeof(buf) - out.size())), ec
				, udp_socket::tracker_connection);
		}
		m_state = action_announce;
		sent_bytes(int(sizeof(buf) - out.size()) + 28); // assuming UDP/IP header
		++m_attempts;
		if (ec)
		{
			fail(ec);
			return;
		}
	}

}

