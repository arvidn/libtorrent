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

#include <boost/bind.hpp>

#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/udp_tracker_connection.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/peer_connection.hpp"

using namespace libtorrent;
using boost::tuples::make_tuple;
using boost::tuples::tuple;
using boost::bind;

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
		: m_start_time(time_now())
		, m_read_time(time_now())
		, m_timeout(ios)
		, m_completion_timeout(0)
		, m_read_timeout(0)
		, m_abort(false)
	{}

	void timeout_handler::set_timeout(int completion_timeout, int read_timeout)
	{
		m_completion_timeout = completion_timeout;
		m_read_timeout = read_timeout;
		m_start_time = m_read_time = time_now();

		if (m_abort) return;

		int timeout = (std::min)(
			m_read_timeout, (std::min)(m_completion_timeout, m_read_timeout));
		error_code ec;
		m_timeout.expires_at(m_read_time + seconds(timeout), ec);
		m_timeout.async_wait(bind(
			&timeout_handler::timeout_callback, self(), _1));
	}

	void timeout_handler::restart_read_timeout()
	{
		m_read_time = time_now();
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
		if (error) return;
		if (m_completion_timeout == 0) return;
		
		ptime now(time_now());
		time_duration receive_timeout = now - m_read_time;
		time_duration completion_timeout = now - m_start_time;
		
		if (m_read_timeout
			< total_seconds(receive_timeout)
			|| m_completion_timeout
			< total_seconds(completion_timeout))
		{
			on_timeout();
			return;
		}

		if (m_abort) return;

		int timeout = (std::min)(
			m_read_timeout, (std::min)(m_completion_timeout, m_read_timeout));
		error_code ec;
		m_timeout.expires_at(m_read_time + seconds(timeout), ec);
		m_timeout.async_wait(
			bind(&timeout_handler::timeout_callback, self(), _1));
	}

	tracker_connection::tracker_connection(
		tracker_manager& man
		, tracker_request const& req
		, io_service& ios
		, address bind_interface_
		, boost::weak_ptr<request_callback> r)
		: timeout_handler(ios)
		, m_requester(r)
		, m_bind_interface(bind_interface_)
		, m_man(man)
		, m_req(req)
	{}

	boost::shared_ptr<request_callback> tracker_connection::requester()
	{
		return m_requester.lock();
	}

	void tracker_connection::fail(int code, char const* msg)
	{
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->tracker_request_error(m_req, code, msg);
		close();
	}

	void tracker_connection::fail_timeout()
	{
		boost::shared_ptr<request_callback> cb = requester();
		if (cb) cb->tracker_request_timed_out(m_req);
		close();
	}
	
	void tracker_connection::close()
	{
		cancel();
		m_man.remove_request(this);
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
		, address bind_infc
		, boost::weak_ptr<request_callback> c)
	{
		mutex_t::scoped_lock l(m_mutex);
		TORRENT_ASSERT(req.num_want >= 0);
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
				ios, cc, *this, req, bind_infc, c
				, m_settings, m_proxy, auth);
		}
		else if (protocol == "udp")
		{
			con = new udp_tracker_connection(
				ios, cc, *this, req, bind_infc
				, c, m_settings, m_proxy);
		}
		else
		{
			if (boost::shared_ptr<request_callback> r = c.lock())
				r->tracker_request_error(req, -1, "unknown protocol in tracker url: "
					+ req.url);
			return;
		}

		m_connections.push_back(con);

		boost::shared_ptr<request_callback> cb = con->requester();
		if (cb) cb->m_manager = this;
		con->start();
	}

	void tracker_manager::abort_all_requests()
	{
		// removes all connections from m_connections
		// except those with a requester == 0 (since those are
		// 'event=stopped'-requests)
		mutex_t::scoped_lock l(m_mutex);

		m_abort = true;
		tracker_connections_t keep_connections;

		while (!m_connections.empty())
		{
			boost::intrusive_ptr<tracker_connection>& c = m_connections.back();
			if (!c)
			{
				m_connections.pop_back();
				continue;
			}
			tracker_request const& req = c->tracker_req();
			if (req.event == tracker_request::stopped)
			{
				keep_connections.push_back(c);
				m_connections.pop_back();
				continue;
			}
			// close will remove the entry from m_connections
			// so no need to pop

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			boost::shared_ptr<request_callback> rc = c->requester();
			if (rc) rc->debug_log("aborting: " + req.url);
#endif
			c->close();
		}

		std::swap(m_connections, keep_connections);
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
