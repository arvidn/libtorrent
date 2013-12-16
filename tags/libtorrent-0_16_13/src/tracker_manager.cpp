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

#include <boost/bind.hpp>

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/udp_tracker_connection.hpp"
#include "libtorrent/aux_/session_impl.hpp"

using boost::tuples::make_tuple;
using boost::tuples::tuple;

namespace
{
	enum
	{
		minimum_tracker_response_length = 3,
		http_buffer_size = 2048
	};

}

namespace libtorrent
{
	timeout_handler::timeout_handler(io_service& ios)
		: m_start_time(time_now_hires())
		, m_read_time(m_start_time)
		, m_timeout(ios)
		, m_completion_timeout(0)
		, m_read_timeout(0)
		, m_abort(false)
	{}

	void timeout_handler::set_timeout(int completion_timeout, int read_timeout)
	{
		m_completion_timeout = completion_timeout;
		m_read_timeout = read_timeout;
		m_start_time = m_read_time = time_now_hires();

		TORRENT_ASSERT(completion_timeout > 0 || read_timeout > 0);

		if (m_abort) return;

		int timeout = 0;
		if (m_read_timeout > 0) timeout = m_read_timeout;
		if (m_completion_timeout > 0)
		{
			timeout = timeout == 0
				? m_completion_timeout
				: (std::min)(m_completion_timeout, timeout);
		}

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("timeout_handler::timeout_callback");
#endif
		error_code ec;
		m_timeout.expires_at(m_read_time + seconds(timeout), ec);
		m_timeout.async_wait(boost::bind(
			&timeout_handler::timeout_callback, self(), _1));
	}

	void timeout_handler::restart_read_timeout()
	{
		m_read_time = time_now_hires();
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
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("timeout_handler::timeout_callback");
#endif
		if (m_abort) return;

		ptime now = time_now_hires();
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
				? m_completion_timeout - total_seconds(m_read_time - m_start_time)
				: (std::min)(m_completion_timeout  - total_seconds(m_read_time - m_start_time), timeout);
		}
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("timeout_handler::timeout_callback");
#endif
		error_code ec;
		m_timeout.expires_at(m_read_time + seconds(timeout), ec);
		m_timeout.async_wait(
			boost::bind(&timeout_handler::timeout_callback, self(), _1));
	}

	tracker_connection::tracker_connection(
		tracker_manager& man
		, tracker_request const& req
		, io_service& ios
		, boost::weak_ptr<request_callback> r)
		: timeout_handler(ios)
		, m_requester(r)
		, m_man(man)
		, m_req(req)
	{}

	boost::shared_ptr<request_callback> tracker_connection::requester() const
	{
		return m_requester.lock();
	}

	void tracker_connection::fail(error_code const& ec, int code
		, char const* msg, int interval, int min_interval)
	{
		// we need to post the error to avoid deadlock
			get_io_service().post(boost::bind(&tracker_connection::fail_impl
					, self(), ec, code, std::string(msg), interval, min_interval));
	}

	void tracker_connection::fail_impl(error_code const& ec, int code
		, std::string msg, int interval, int min_interval)
	{
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->tracker_request_error(m_req, code, ec, msg.c_str()
			, interval == 0 ? min_interval : interval);
		close();
	}

	void tracker_connection::sent_bytes(int bytes)
	{
		m_man.sent_bytes(bytes);
	}

	void tracker_connection::received_bytes(int bytes)
	{
		m_man.received_bytes(bytes);
	}

	void tracker_connection::close()
	{
		cancel();
		m_man.remove_request(this);
	}

	tracker_manager::~tracker_manager()
	{
		TORRENT_ASSERT(m_abort);
		abort_all_requests(true);
	}

	void tracker_manager::sent_bytes(int bytes)
	{
		TORRENT_ASSERT(m_ses.is_network_thread());
		m_ses.m_stat.sent_tracker_bytes(bytes);
	}

	void tracker_manager::received_bytes(int bytes)
	{
		TORRENT_ASSERT(m_ses.is_network_thread());
		m_ses.m_stat.received_tracker_bytes(bytes);
	}

	void tracker_manager::remove_request(tracker_connection const* c)
	{
		mutex_t::scoped_lock l(m_mutex);

		tracker_connections_t::iterator i = std::find(m_connections.begin()
			, m_connections.end(), boost::intrusive_ptr<const tracker_connection>(c));
		if (i == m_connections.end()) return;

		m_connections.erase(i);
	}

	void tracker_manager::queue_request(
		io_service& ios
		, connection_queue& cc
		, tracker_request req
		, std::string const& auth
		, boost::weak_ptr<request_callback> c)
	{
		mutex_t::scoped_lock l(m_mutex);
		TORRENT_ASSERT(req.num_want >= 0);
		TORRENT_ASSERT(!m_abort || req.event == tracker_request::stopped);
		if (m_abort && req.event != tracker_request::stopped) return;
		if (req.event == tracker_request::stopped)
			req.num_want = 0;

		TORRENT_ASSERT(!m_abort || req.event == tracker_request::stopped);
		if (m_abort && req.event != tracker_request::stopped)
			return;

		std::string protocol = req.url.substr(0, req.url.find(':'));

		boost::intrusive_ptr<tracker_connection> con;

#ifdef TORRENT_USE_OPENSSL
		if (protocol == "http" || protocol == "https")
#else
		if (protocol == "http")
#endif
		{
			con = new http_tracker_connection(
				ios, cc, *this, req, c
				, m_ses, m_proxy, auth
#if TORRENT_USE_I2P
				, &m_ses.m_i2p_conn
#endif
				);
		}
		else if (protocol == "udp")
		{
			con = new udp_tracker_connection(
				ios, cc, *this, req , c, m_ses
				, m_proxy);
		}
		else
		{
			// we need to post the error to avoid deadlock
			if (boost::shared_ptr<request_callback> r = c.lock())
				ios.post(boost::bind(&request_callback::tracker_request_error, r, req
					, -1, error_code(errors::unsupported_url_protocol)
					, "", 0));
			return;
		}

		m_connections.push_back(con);

		boost::shared_ptr<request_callback> cb = con->requester();
		if (cb) cb->m_manager = this;
		con->start();
	}

	bool tracker_manager::incoming_udp(error_code const& e
		, udp::endpoint const& ep, char const* buf, int size)
	{
		for (tracker_connections_t::iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			boost::intrusive_ptr<tracker_connection> p = *i;
			++i;
			// on_receive() may remove the tracker connection from the list
			if (p->on_receive(e, ep, buf, size)) return true;
		}
		return false;
	}

	bool tracker_manager::incoming_udp(error_code const& e
		, char const* hostname, char const* buf, int size)
	{
		for (tracker_connections_t::iterator i = m_connections.begin();
			i != m_connections.end();)
		{
			boost::intrusive_ptr<tracker_connection> p = *i;
			++i;
			// on_receive() may remove the tracker connection from the list
			if (p->on_receive_hostname(e, hostname, buf, size)) return true;
		}
		return false;
	}

	void tracker_manager::abort_all_requests(bool all)
	{
		// removes all connections from m_connections
		// except 'event=stopped'-requests
		mutex_t::scoped_lock l(m_mutex);

		m_abort = true;
		tracker_connections_t close_connections;

		for (tracker_connections_t::iterator i = m_connections.begin()
			, end(m_connections.end()); i != end; ++i)
		{
			intrusive_ptr<tracker_connection> c = *i;
			tracker_request const& req = c->tracker_req();
			if (req.event == tracker_request::stopped && !all)
				continue;

			close_connections.push_back(c);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			boost::shared_ptr<request_callback> rc = c->requester();
			if (rc) rc->debug_log("aborting: %s", req.url.c_str());
#endif
		}
		l.unlock();

		for (tracker_connections_t::iterator i = close_connections.begin()
			, end(close_connections.end()); i != end; ++i)
		{
			(*i)->close();
		}
	}
	
	bool tracker_manager::empty() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_connections.empty();
	}

	int tracker_manager::num_requests() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_connections.size();
	}
}
