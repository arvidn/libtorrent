/*

Copyright (c) 2007, Arvid Norberg
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

#include "libtorrent/http_connection.hpp"

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <asio/ip/tcp.hpp>
#include <string>

using boost::bind;

namespace libtorrent
{

	enum { max_bottled_buffer = 1024 * 1024 };

void http_connection::get(std::string const& url, time_duration timeout
	, bool handle_redirect)
{
	m_redirect = handle_redirect;
	std::string protocol;
	std::string auth;
	std::string hostname;
	std::string path;
	int port;
	boost::tie(protocol, auth, hostname, port, path) = parse_url_components(url);
	std::stringstream headers;
	headers << "GET " << path << " HTTP/1.0\r\n"
		"Host:" << hostname <<
		"\r\nConnection: close\r\n";
	if (!auth.empty())
		headers << "Authorization: Basic " << base64encode(auth) << "\r\n";
	headers << "\r\n";
	sendbuffer = headers.str();
	start(hostname, boost::lexical_cast<std::string>(port), timeout);
}

void http_connection::start(std::string const& hostname, std::string const& port
	, time_duration timeout, bool handle_redirect)
{
	m_redirect = handle_redirect;
	m_timeout = timeout;
	m_timer.expires_from_now(m_timeout);
	m_timer.async_wait(bind(&http_connection::on_timeout
		, boost::weak_ptr<http_connection>(shared_from_this()), _1));
	m_called = false;
	if (m_sock.is_open() && m_hostname == hostname && m_port == port)
	{
		m_parser.reset();
		asio::async_write(m_sock, asio::buffer(sendbuffer)
			, bind(&http_connection::on_write, shared_from_this(), _1));
	}
	else
	{
		m_sock.close();
		tcp::resolver::query query(hostname, port);
		m_resolver.async_resolve(query, bind(&http_connection::on_resolve
			, shared_from_this(), _1, _2));
		m_hostname = hostname;
		m_port = port;
	}
}

void http_connection::on_connect_timeout()
{
	if (m_connection_ticket > -1) m_cc.done(m_connection_ticket);
	m_connection_ticket = -1;

	callback(asio::error::timed_out);
	close();
}

void http_connection::on_timeout(boost::weak_ptr<http_connection> p
	, asio::error_code const& e)
{
	boost::shared_ptr<http_connection> c = p.lock();
	if (!c) return;
	if (c->m_connection_ticket > -1) c->m_cc.done(c->m_connection_ticket);
	c->m_connection_ticket = -1;

	if (e == asio::error::operation_aborted) return;

	if (c->m_last_receive + c->m_timeout < time_now())
	{
		c->callback(asio::error::timed_out);
		c->close();
		return;
	}

	if (!c->m_sock.is_open()) return;

	c->m_timer.expires_at(c->m_last_receive + c->m_timeout);
	c->m_timer.async_wait(bind(&http_connection::on_timeout, p, _1));
}

void http_connection::close()
{
	m_timer.cancel();
	m_limiter_timer.cancel();
	m_sock.close();
	m_hostname.clear();
	m_port.clear();

	if (m_connection_ticket > -1) m_cc.done(m_connection_ticket);
	m_connection_ticket = -1;

	m_handler.clear();
}

void http_connection::on_resolve(asio::error_code const& e
		, tcp::resolver::iterator i)
{
	if (e)
	{
		callback(e);
		close();
		return;
	}
	TORRENT_ASSERT(i != tcp::resolver::iterator());
	m_cc.enqueue(bind(&http_connection::connect, shared_from_this(), _1, *i)
		, bind(&http_connection::on_connect_timeout, shared_from_this())
		, m_timeout);
}

void http_connection::connect(int ticket, tcp::endpoint target_address)
{
	m_connection_ticket = ticket;
	m_sock.async_connect(target_address, boost::bind(&http_connection::on_connect
		, shared_from_this(), _1/*, ++i*/));
}

void http_connection::on_connect(asio::error_code const& e
	/*, tcp::resolver::iterator i*/)
{
	if (!e)
	{ 
		m_last_receive = time_now();
		if (m_connect_handler) m_connect_handler(*this);
		asio::async_write(m_sock, asio::buffer(sendbuffer)
			, bind(&http_connection::on_write, shared_from_this(), _1));
	}
/*	else if (i != tcp::resolver::iterator())
	{
		// The connection failed. Try the next endpoint in the list.
		m_sock.close();
		m_cc.enqueue(bind(&http_connection::connect, shared_from_this(), _1, *i)
			, bind(&http_connection::on_connect_timeout, shared_from_this())
			, m_timeout);
	} 
*/	else
	{ 
		callback(e);
		close();
	}
}

void http_connection::callback(asio::error_code const& e, char const* data, int size)
{
	if (!m_bottled || !m_called)
	{
		m_called = true;
		if (m_handler) m_handler(e, m_parser, data, size);
	}
}

void http_connection::on_write(asio::error_code const& e)
{
	if (e)
	{
		callback(e);
		close();
		return;
	}

	std::string().swap(sendbuffer);
	m_recvbuffer.resize(4096);

	int amount_to_read = m_recvbuffer.size() - m_read_pos;
	if (m_rate_limit > 0 && amount_to_read > m_download_quota)
	{
		amount_to_read = m_download_quota;
		if (m_download_quota == 0)
		{
			if (!m_limiter_timer_active)
				on_assign_bandwidth(asio::error_code());
			return;
		}
	}
	m_sock.async_read_some(asio::buffer(&m_recvbuffer[0] + m_read_pos
		, amount_to_read)
		, bind(&http_connection::on_read
		, shared_from_this(), _1, _2));
}

void http_connection::on_read(asio::error_code const& e
	, std::size_t bytes_transferred)
{
	if (m_rate_limit)
	{
		m_download_quota -= bytes_transferred;
		TORRENT_ASSERT(m_download_quota >= 0);
	}

	if (e == asio::error::eof)
	{
		char const* data = 0;
		std::size_t size = 0;
		if (m_bottled && m_parser.header_finished())
		{
			data = m_parser.get_body().begin;
			size = m_parser.get_body().left();
		}
		callback(e, data, size);
		close();
		return;
	}

	if (e)
	{
		callback(e);
		close();
		return;
	}

	m_read_pos += bytes_transferred;
	TORRENT_ASSERT(m_read_pos <= int(m_recvbuffer.size()));

	// having a nonempty path means we should handle redirects
	if (m_redirect && m_parser.header_finished())
	{
		int code = m_parser.status_code();
		if (code >= 300 && code < 400)
		{
			// attempt a redirect
			std::string const& url = m_parser.header("location");
			if (url.empty())
			{
				// missing location header
				callback(e);
				return;
			}

			m_limiter_timer_active = false;
			close();

			get(url, m_timeout);
			return;
		}
	
		m_redirect = false;
	}

	if (m_bottled || !m_parser.header_finished())
	{
		libtorrent::buffer::const_interval rcv_buf(&m_recvbuffer[0]
			, &m_recvbuffer[0] + m_read_pos);
		try
		{
			m_parser.incoming(rcv_buf);
		}
		catch (std::exception& e)
		{
			m_timer.cancel();
			m_handler(asio::error::fault, m_parser, 0, 0);
			m_handler.clear();
			return;
		}
		if (!m_bottled && m_parser.header_finished())
		{
			if (m_read_pos > m_parser.body_start())
				callback(e, &m_recvbuffer[0] + m_parser.body_start()
					, m_read_pos - m_parser.body_start());
			m_read_pos = 0;
			m_last_receive = time_now();
		}
		else if (m_bottled && m_parser.finished())
		{
			m_timer.cancel();
			callback(e, m_parser.get_body().begin, m_parser.get_body().left());
		}
	}
	else
	{
		TORRENT_ASSERT(!m_bottled);
		callback(e, &m_recvbuffer[0], m_read_pos);
		m_read_pos = 0;
		m_last_receive = time_now();
	}

	if (int(m_recvbuffer.size()) == m_read_pos)
		m_recvbuffer.resize((std::min)(m_read_pos + 2048, int(max_bottled_buffer)));
	if (m_read_pos == max_bottled_buffer)
	{
		callback(asio::error::eof);
		close();
		return;
	}
	int amount_to_read = m_recvbuffer.size() - m_read_pos;
	if (m_rate_limit > 0 && amount_to_read > m_download_quota)
	{
		amount_to_read = m_download_quota;
		if (m_download_quota == 0)
		{
			if (!m_limiter_timer_active)
				on_assign_bandwidth(asio::error_code());
			return;
		}
	}
	m_sock.async_read_some(asio::buffer(&m_recvbuffer[0] + m_read_pos
		, amount_to_read)
		, bind(&http_connection::on_read
		, shared_from_this(), _1, _2));
}

void http_connection::on_assign_bandwidth(asio::error_code const& e)
{
	if ((e == asio::error::operation_aborted
		&& m_limiter_timer_active)
		|| !m_sock.is_open())
	{
		callback(asio::error::eof);
		return;
	}
	m_limiter_timer_active = false;
	if (e) return;

	if (m_download_quota > 0) return;

	m_download_quota = m_rate_limit / 4;

	int amount_to_read = m_recvbuffer.size() - m_read_pos;
	if (amount_to_read > m_download_quota)
		amount_to_read = m_download_quota;

	if (!m_sock.is_open()) return;

	m_sock.async_read_some(asio::buffer(&m_recvbuffer[0] + m_read_pos
		, amount_to_read)
		, bind(&http_connection::on_read
		, shared_from_this(), _1, _2));

	m_limiter_timer_active = true;
	m_limiter_timer.expires_from_now(milliseconds(250));
	m_limiter_timer.async_wait(bind(&http_connection::on_assign_bandwidth
		, shared_from_this(), _1));
}

void http_connection::rate_limit(int limit)
{
	if (!m_sock.is_open()) return;

	if (!m_limiter_timer_active)
	{
		m_limiter_timer_active = true;
		m_limiter_timer.expires_from_now(milliseconds(250));
		m_limiter_timer.async_wait(bind(&http_connection::on_assign_bandwidth
			, shared_from_this(), _1));
	}
	m_rate_limit = limit;
}

}

