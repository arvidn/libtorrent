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

#include <cctype>

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/udp_tracker_connection.hpp"
#include "libtorrent/aux_/io.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/socket_io.hpp"

#ifdef TORRENT_USE_OPENSSL
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ssl/context.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

using namespace std::placeholders;

namespace libtorrent {

	timeout_handler::timeout_handler(io_service& ios)
		: m_start_time(clock_type::now())
		, m_read_time(m_start_time)
		, m_timeout(ios)
	{}

	timeout_handler::~timeout_handler()
	{
		TORRENT_ASSERT(m_outstanding_timer_wait == 0);
	}

	void timeout_handler::set_timeout(int completion_timeout, int read_timeout)
	{
		m_completion_timeout = completion_timeout;
		m_read_timeout = read_timeout;
		m_start_time = m_read_time = clock_type::now();

		TORRENT_ASSERT(completion_timeout > 0 || read_timeout > 0);

		if (m_abort) return;

		int timeout = 0;
		if (m_read_timeout > 0) timeout = m_read_timeout;
		if (m_completion_timeout > 0)
		{
			timeout = timeout == 0
				? m_completion_timeout
				: std::min(m_completion_timeout, timeout);
		}

		ADD_OUTSTANDING_ASYNC("timeout_handler::timeout_callback");
		error_code ec;
		m_timeout.expires_at(m_read_time + seconds(timeout), ec);
		m_timeout.async_wait(std::bind(
			&timeout_handler::timeout_callback, shared_from_this(), _1));
#if TORRENT_USE_ASSERTS
		++m_outstanding_timer_wait;
#endif
	}

	void timeout_handler::restart_read_timeout()
	{
		m_read_time = clock_type::now();
	}

	void timeout_handler::cancel()
	{
		m_abort = true;
		m_completion_timeout = 0;
		error_code ec;
		m_timeout.cancel(ec);
	}

	void timeout_handler::timeout_callback(error_code const& error)
	{
		COMPLETE_ASYNC("timeout_handler::timeout_callback");
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(m_outstanding_timer_wait > 0);
		--m_outstanding_timer_wait;
#endif
		if (m_abort) return;

		time_point now = clock_type::now();
		time_duration receive_timeout = now - m_read_time;
		time_duration completion_timeout = now - m_start_time;

		if ((m_read_timeout
				&& m_read_timeout <= total_seconds(receive_timeout))
			|| (m_completion_timeout
				&& m_completion_timeout <= total_seconds(completion_timeout))
			|| error)
		{
			on_timeout(error);
			return;
		}

		int timeout = 0;
		if (m_read_timeout > 0) timeout = m_read_timeout;
		if (m_completion_timeout > 0)
		{
			timeout = timeout == 0
				? int(m_completion_timeout - total_seconds(m_read_time - m_start_time))
				: std::min(int(m_completion_timeout - total_seconds(m_read_time - m_start_time)), timeout);
		}
		ADD_OUTSTANDING_ASYNC("timeout_handler::timeout_callback");
		error_code ec;
		m_timeout.expires_at(m_read_time + seconds(timeout), ec);
		m_timeout.async_wait(
			std::bind(&timeout_handler::timeout_callback, shared_from_this(), _1));
#if TORRENT_USE_ASSERTS
		++m_outstanding_timer_wait;
#endif
	}

	tracker_connection::tracker_connection(
		tracker_manager& man
		, tracker_request const& req
		, io_service& ios
		, std::weak_ptr<request_callback> r)
		: timeout_handler(ios)
		, m_req(req)
		, m_requester(std::move(r))
		, m_man(man)
	{}

	std::shared_ptr<request_callback> tracker_connection::requester() const
	{
		return m_requester.lock();
	}

	void tracker_connection::fail(error_code const& ec
		, char const* msg, seconds32 const interval, seconds32 const min_interval)
	{
		// we need to post the error to avoid deadlock
		get_io_service().post(std::bind(&tracker_connection::fail_impl
			, shared_from_this(), ec, std::string(msg), interval, min_interval));
	}

	void tracker_connection::fail_impl(error_code const& ec
		, std::string const msg, seconds32 const interval, seconds32 const min_interval)
	{
		std::shared_ptr<request_callback> cb = requester();
		if (cb) cb->tracker_request_error(m_req, ec, msg
			, interval.count() == 0 ? min_interval : interval);
		close();
	}

	address tracker_connection::bind_interface() const
	{
		return m_req.outgoing_socket.get_local_endpoint().address();
	}

	void tracker_connection::sent_bytes(int bytes)
	{
		m_man.sent_bytes(bytes);
	}

	void tracker_connection::received_bytes(int bytes)
	{
		m_man.received_bytes(bytes);
	}

	tracker_manager::tracker_manager(send_fun_t const& send_fun
		, send_fun_hostname_t const& send_fun_hostname
		, counters& stats_counters
		, resolver_interface& resolver
		, aux::session_settings const& sett
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
		, aux::session_logger& ses
#endif
		)
		: m_send_fun(send_fun)
		, m_send_fun_hostname(send_fun_hostname)
		, m_host_resolver(resolver)
		, m_settings(sett)
		, m_stats_counters(stats_counters)
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
		, m_ses(ses)
#endif
	{}

	tracker_manager::~tracker_manager()
	{
		abort_all_requests(true);
	}

	void tracker_manager::sent_bytes(int bytes)
	{
		TORRENT_ASSERT(m_ses.is_single_thread());
		m_stats_counters.inc_stats_counter(counters::sent_tracker_bytes, bytes);
	}

	void tracker_manager::received_bytes(int bytes)
	{
		TORRENT_ASSERT(m_ses.is_single_thread());
		m_stats_counters.inc_stats_counter(counters::recv_tracker_bytes, bytes);
	}

	void tracker_manager::remove_request(http_tracker_connection const* c)
	{
		TORRENT_ASSERT(is_single_thread());
		auto const i = std::find_if(m_http_conns.begin(), m_http_conns.end()
			, [c] (std::shared_ptr<http_tracker_connection> const& ptr) { return ptr.get() == c; });
		if (i != m_http_conns.end())
		{
			m_http_conns.erase(i);
			if (!m_queued.empty())
			{
				auto conn = std::move(m_queued.front());
				m_queued.pop_front();
				m_http_conns.push_back(std::move(conn));
				m_http_conns.back()->start();
			}
			return;
		}

		auto const j = std::find_if(m_queued.begin(), m_queued.end()
			, [c] (std::shared_ptr<http_tracker_connection> const& ptr) { return ptr.get() == c; });
		if (j != m_queued.end())
		{
			m_queued.erase(j);
		}
	}

	void tracker_manager::remove_request(udp_tracker_connection const* c)
	{
		TORRENT_ASSERT(is_single_thread());
		m_udp_conns.erase(c->transaction_id());
	}

	void tracker_manager::update_transaction_id(
		std::shared_ptr<udp_tracker_connection> c
		, std::uint32_t tid)
	{
		TORRENT_ASSERT(is_single_thread());
		m_udp_conns.erase(c->transaction_id());
		m_udp_conns[tid] = c;
	}

	void tracker_manager::queue_request(
		io_service& ios
		, tracker_request&& req
		, aux::session_settings const& sett
		, std::weak_ptr<request_callback> c)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(req.num_want >= 0);
		TORRENT_ASSERT(!m_abort || req.event == tracker_request::stopped);
		if (m_abort && req.event != tracker_request::stopped) return;

#ifndef TORRENT_DISABLE_LOGGING
		std::shared_ptr<request_callback> cb = c.lock();
		if (cb) cb->debug_log("*** QUEUE_TRACKER_REQUEST [ listen_port: %d ]"
			, req.listen_port);
#endif

		std::string const protocol = req.url.substr(0, req.url.find(':'));

#ifdef TORRENT_USE_OPENSSL
		if (protocol == "http" || protocol == "https")
#else
		if (protocol == "http")
#endif
		{
			auto con = std::make_shared<http_tracker_connection>(ios, *this, std::move(req), c);
			if (m_http_conns.size() < std::size_t(sett.get_int(settings_pack::max_concurrent_http_announces)))
			{
				m_http_conns.push_back(std::move(con));
				m_http_conns.back()->start();
			}
			else
			{
				m_queued.push_back(std::move(con));
			}
			return;
		}
		else if (protocol == "udp")
		{
			auto con = std::make_shared<udp_tracker_connection>(ios, *this, std::move(req), c);
			m_udp_conns[con->transaction_id()] = con;
			con->start();
			return;
		}

		// we need to post the error to avoid deadlock
		if (auto r = c.lock())
			ios.post(std::bind(&request_callback::tracker_request_error, r, std::move(req)
				, errors::unsupported_url_protocol
				, "", seconds32(0)));
	}

	bool tracker_manager::incoming_packet(udp::endpoint const& ep
		, span<char const> const buf)
	{
		TORRENT_ASSERT(is_single_thread());
		// ignore packets smaller than 8 bytes
		if (buf.size() < 8)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (m_ses.should_log())
			{
				m_ses.session_log("incoming packet from %s, not a UDP tracker message "
					"(%d Bytes)", print_endpoint(ep).c_str(), int(buf.size()));
			}
#endif
			return false;
		}

		// the first word is the action, if it's not [0, 3]
		// it's not a valid udp tracker response
		span<const char> ptr = buf;
		std::uint32_t const action = aux::read_uint32(ptr);
		if (action > 3) return false;

		std::uint32_t const transaction = aux::read_uint32(ptr);
		auto const i = m_udp_conns.find(transaction);

		if (i == m_udp_conns.end())
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (m_ses.should_log())
			{
				m_ses.session_log("incoming UDP tracker packet from %s has invalid "
					"transaction ID (%x)", print_endpoint(ep).c_str()
					, transaction);
			}
#endif
			return false;
		}

		std::shared_ptr<udp_tracker_connection> const p = i->second;
		// on_receive() may remove the tracker connection from the list
		return p->on_receive(ep, buf);
	}

	void tracker_manager::incoming_error(error_code const&
		, udp::endpoint const&)
	{
		TORRENT_ASSERT(is_single_thread());
		// TODO: 2 implement
	}

	bool tracker_manager::incoming_packet(char const* hostname
		, span<char const> const buf)
	{
		TORRENT_ASSERT(is_single_thread());
		// ignore packets smaller than 8 bytes
		if (buf.size() < 16) return false;

		// the first word is the action, if it's not [0, 3]
		// it's not a valid udp tracker response
		span<const char> ptr = buf;
		std::uint32_t const action = aux::read_uint32(ptr);
		if (action > 3) return false;

		std::uint32_t const transaction = aux::read_uint32(ptr);
		auto const i = m_udp_conns.find(transaction);

		if (i == m_udp_conns.end())
		{
#ifndef TORRENT_DISABLE_LOGGING
			// now, this may not have been meant to be a tracker response,
			// but chances are pretty good, so it's probably worth logging
			m_ses.session_log("incoming UDP tracker packet from %s has invalid "
				"transaction ID (%x)", hostname, int(transaction));
#endif
			return false;
		}

		std::shared_ptr<udp_tracker_connection> const p = i->second;
		// on_receive() may remove the tracker connection from the list
		return p->on_receive_hostname(hostname, buf);
	}

	void tracker_manager::send_hostname(aux::listen_socket_handle const& sock
		, char const* hostname, int const port
		, span<char const> p, error_code& ec, udp_send_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());
		m_send_fun_hostname(sock, hostname, port, p, ec, flags);
	}

	void tracker_manager::send(aux::listen_socket_handle const& sock
		, udp::endpoint const& ep
		, span<char const> p
		, error_code& ec, udp_send_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());
		m_send_fun(sock, ep, p, ec, flags);
	}

	void tracker_manager::abort_all_requests(bool all)
	{
		// this is called from the destructor too, which is not subject to the
		// single-thread requirement.
		TORRENT_ASSERT(all || is_single_thread());
		// removes all connections except 'event=stopped'-requests

		m_abort = true;
		std::vector<std::shared_ptr<http_tracker_connection>> close_http_connections;
		std::vector<std::shared_ptr<udp_tracker_connection>> close_udp_connections;

		for (auto const& c : m_http_conns)
		{
			tracker_request const& req = c->tracker_req();
			if (req.event == tracker_request::stopped && !all)
				continue;

			close_http_connections.push_back(c);

#ifndef TORRENT_DISABLE_LOGGING
			std::shared_ptr<request_callback> rc = c->requester();
			if (rc) rc->debug_log("aborting: %s", req.url.c_str());
#endif
		}
		for (auto const& p : m_udp_conns)
		{
			auto const& c = p.second;
			tracker_request const& req = c->tracker_req();
			if (req.event == tracker_request::stopped && !all)
				continue;

			close_udp_connections.push_back(c);

#ifndef TORRENT_DISABLE_LOGGING
			std::shared_ptr<request_callback> rc = c->requester();
			if (rc) rc->debug_log("aborting: %s", req.url.c_str());
#endif
		}

		for (auto const& c : close_http_connections)
			c->close();

		for (auto const& c : close_udp_connections)
			c->close();
	}

	bool tracker_manager::empty() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_http_conns.empty() && m_udp_conns.empty();
	}

	int tracker_manager::num_requests() const
	{
		TORRENT_ASSERT(is_single_thread());
		return int(m_http_conns.size() + m_udp_conns.size());
	}
}
