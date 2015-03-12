/*

Copyright (c) 2003-2014, Arvid Norberg
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

#include <boost/bind.hpp>
#include <boost/make_shared.hpp>

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
		: m_completion_timeout(0)
		, m_start_time(clock_type::now())
		, m_read_time(m_start_time)
		, m_timeout(ios)
		, m_read_timeout(0)
		, m_abort(false)
	{}

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
				: (std::min)(m_completion_timeout, timeout);
		}

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("timeout_handler::timeout_callback");
#endif
		error_code ec;
		m_timeout.expires_at(m_read_time + seconds(timeout), ec);
		m_timeout.async_wait(boost::bind(
			&timeout_handler::timeout_callback, shared_from_this(), _1));
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
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("timeout_handler::timeout_callback");
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
				: (std::min)(int(m_completion_timeout - total_seconds(m_read_time - m_start_time)), timeout);
		}
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("timeout_handler::timeout_callback");
#endif
		error_code ec;
		m_timeout.expires_at(m_read_time + seconds(timeout), ec);
		m_timeout.async_wait(
			boost::bind(&timeout_handler::timeout_callback, shared_from_this(), _1));
	}

	tracker_connection::tracker_connection(
		tracker_manager& man
		, tracker_request const& req
		, io_service& ios
		, boost::weak_ptr<request_callback> r)
		: timeout_handler(ios)
		, m_req(req)
		, m_requester(r)
		, m_man(man)
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
					, shared_from_this(), ec, code, std::string(msg), interval, min_interval));
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

	// TODO: 2 some of these arguments could probably be moved to the
	// tracker request itself. like the ip_filter and settings
	tracker_manager::tracker_manager(class udp_socket& sock
		, counters& stats_counters
		, resolver_interface& resolver
		, struct ip_filter& ipf
		, aux::session_settings const& sett
#if defined TORRENT_LOGGING || TORRENT_USE_ASSERTS
		, aux::session_logger& ses
#endif
		)
		: m_ip_filter(ipf)
		, m_udp_socket(sock)
		, m_host_resolver(resolver)
		, m_settings(sett)
		, m_stats_counters(stats_counters)
#if defined TORRENT_LOGGING || TORRENT_USE_ASSERTS
		, m_ses(ses)
#endif
		, m_abort(false)
	{}

	tracker_manager::~tracker_manager()
	{
		TORRENT_ASSERT(m_abort);
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

	void tracker_manager::remove_request(tracker_connection const* c)
	{
		mutex_t::scoped_lock l(m_mutex);

		http_conns_t::iterator i = std::find_if(m_http_conns.begin()
			, m_http_conns.end()
			, boost::bind(&boost::shared_ptr<http_tracker_connection>::get, _1) == c);
		if (i != m_http_conns.end())
		{
			m_http_conns.erase(i);
			return;
		}

		udp_conns_t::iterator j = std::find_if(m_udp_conns.begin()
			, m_udp_conns.end()
			, boost::bind(&boost::shared_ptr<udp_tracker_connection>::get
				, boost::bind(&udp_conns_t::value_type::second, _1)) == c);
		if (j != m_udp_conns.end())
		{
			m_udp_conns.erase(j);
			return;
		}
	}

	void tracker_manager::update_transaction_id(
		boost::shared_ptr<udp_tracker_connection> c
		, boost::uint64_t tid)
	{
		m_udp_conns.erase(c->transaction_id());
		m_udp_conns[tid] = c;
	}

	void tracker_manager::queue_request(
		io_service& ios
		, tracker_request req
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

#ifdef TORRENT_USE_OPENSSL
		if (protocol == "http" || protocol == "https")
#else
		if (protocol == "http")
#endif
		{
			boost::shared_ptr<http_tracker_connection> con
				= boost::make_shared<http_tracker_connection>(
				boost::ref(ios), boost::ref(*this), boost::cref(req), c);
			m_http_conns.push_back(con);
			con->start();
			return;
		}
		else if (protocol == "udp")
		{
			boost::shared_ptr<udp_tracker_connection> con
				= boost::make_shared<udp_tracker_connection>(
					boost::ref(ios), boost::ref(*this), boost::cref(req) , c);
			m_udp_conns[con->transaction_id()] = con;
			con->start();
			return;
		}

		// we need to post the error to avoid deadlock
		if (boost::shared_ptr<request_callback> r = c.lock())
			ios.post(boost::bind(&request_callback::tracker_request_error, r, req
				, -1, error_code(errors::unsupported_url_protocol)
				, "", 0));
	}

	bool tracker_manager::incoming_packet(error_code const& e
		, udp::endpoint const& ep, char const* buf, int size)
	{
		// ignore packets smaller than 8 bytes
		if (size < 8)
		{
#if defined TORRENT_LOGGING
			m_ses.session_log("incoming packet from %s, not a UDP tracker message "
				"(%d Bytes)", print_endpoint(ep).c_str(), size);
#endif
			return false;
		}

		const char* ptr = buf + 4;
		boost::uint32_t transaction = detail::read_uint32(ptr);
		udp_conns_t::iterator i = m_udp_conns.find(transaction);

		if (i == m_udp_conns.end())
		{
#if defined TORRENT_LOGGING
			m_ses.session_log("incoming UDP tracker packet from %s has invalid "
				"transaction ID (%" PRIu32 ")", print_endpoint(ep).c_str()
				, transaction);
#endif
			return false;
		}

		boost::shared_ptr<tracker_connection> p = i->second;
		// on_receive() may remove the tracker connection from the list
		return p->on_receive(e, ep, buf, size);
	}

	bool tracker_manager::incoming_packet(error_code const& e
		, char const* hostname, char const* buf, int size)
	{
		// ignore packets smaller than 8 bytes
		if (size < 8)
		{
#if defined TORRENT_LOGGING
			m_ses.session_log("incoming packet from %s, not a UDP tracker message "
				"(%d Bytes)", hostname, size);
#endif
			return false;
		}

		const char* ptr = buf + 4;
		boost::uint32_t transaction = detail::read_uint32(ptr);
		udp_conns_t::iterator i = m_udp_conns.find(transaction);

		if (i == m_udp_conns.end())
		{
#if defined TORRENT_LOGGING
			m_ses.session_log("incoming UDP tracker packet from %s has invalid "
				"transaction ID (%x)", hostname, int(transaction));
#endif
			return false;
		}

		boost::shared_ptr<tracker_connection> p = i->second;
		// on_receive() may remove the tracker connection from the list
		return p->on_receive_hostname(e, hostname, buf, size);
	}

	void tracker_manager::abort_all_requests(bool all)
	{
		// removes all connections except 'event=stopped'-requests
		mutex_t::scoped_lock l(m_mutex);

		m_abort = true;
		http_conns_t close_http_connections;
		std::vector<boost::shared_ptr<udp_tracker_connection> > close_udp_connections;

		for (http_conns_t::iterator i = m_http_conns.begin()
			, end(m_http_conns.end()); i != end; ++i)
		{
			http_tracker_connection* c = i->get();
			tracker_request const& req = c->tracker_req();
			if (req.event == tracker_request::stopped && !all)
				continue;

			close_http_connections.push_back(*i);

#if defined TORRENT_LOGGING
			boost::shared_ptr<request_callback> rc = c->requester();
			if (rc) rc->debug_log("aborting: %s", req.url.c_str());
#endif
		}
		for (udp_conns_t::iterator i = m_udp_conns.begin()
			, end(m_udp_conns.end()); i != end; ++i)
		{
			boost::shared_ptr<udp_tracker_connection> c = i->second;
			tracker_request const& req = c->tracker_req();
			if (req.event == tracker_request::stopped && !all)
				continue;

			close_udp_connections.push_back(c);

#if defined TORRENT_LOGGING
			boost::shared_ptr<request_callback> rc = c->requester();
			if (rc) rc->debug_log("aborting: %s", req.url.c_str());
#endif
		}
		l.unlock();

		for (http_conns_t::iterator i = close_http_connections.begin()
			, end(close_http_connections.end()); i != end; ++i)
		{
			(*i)->close();
		}

		for (std::vector<boost::shared_ptr<udp_tracker_connection> >::iterator i
			= close_udp_connections.begin()
			, end(close_udp_connections.end()); i != end; ++i)
		{
			(*i)->close();
		}
	}
	
	bool tracker_manager::empty() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_http_conns.empty() && m_udp_conns.empty();
	}

	int tracker_manager::num_requests() const
	{
		mutex_t::scoped_lock l(m_mutex);
		return m_http_conns.size() + m_udp_conns.size();
	}
}
