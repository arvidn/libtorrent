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

#include "http_connection.hpp"
#include <asio/ip/tcp.hpp>

namespace libtorrent
{

void http_connection::get(std::string const& url, boost::posix_time::time_duration timeout)
{
	std::string protocol;
	std::string hostname;
	std::string path;
	int port;
	boost::tie(protocol, hostname, port, path) = parse_url_components(url);
	std::stringstream headers;
	headers << "GET " << path << " HTTP/1.0\r\n"
		"Host:" << hostname <<
		"Connection: close\r\n"
		"\r\n\r\n";
	sendbuffer = headers.str();
	start(hostname, boost::lexical_cast<std::string>(port), timeout);
}

void http_connection::start(std::string const& hostname, std::string const& port
	, boost::posix_time::time_duration timeout)
{
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

void http_connection::on_timeout(boost::weak_ptr<http_connection> p
	, asio::error_code const& e)
{
	if (e == asio::error::operation_aborted) return;
	boost::shared_ptr<http_connection> c = p.lock();
	if (!c) return;
	if (c->m_bottled && c->m_called) return;
	c->m_called = true;
	c->m_handler(asio::error::timed_out, c->m_parser, 0, 0);
}

void http_connection::close()
{
	m_timer.cancel();
	m_sock.close();
	m_hostname.clear();
	m_port.clear();
}

void http_connection::on_resolve(asio::error_code const& e
		, tcp::resolver::iterator i)
{
	if (e)
	{
		close();
		if (m_bottled && m_called) return;
		m_called = true;
		m_handler(e, m_parser, 0, 0);
		return;
	}
	assert(i != tcp::resolver::iterator());
	m_sock.async_connect(*i, boost::bind(&http_connection::on_connect
		, shared_from_this(), _1/*, ++i*/));
}

void http_connection::on_connect(asio::error_code const& e
	/*, tcp::resolver::iterator i*/)
{
	if (!e)
	{ 
		m_timer.expires_from_now(m_timeout);
		m_timer.async_wait(bind(&http_connection::on_timeout, shared_from_this(), _1));
		asio::async_write(m_sock, asio::buffer(sendbuffer)
			, bind(&http_connection::on_write, shared_from_this(), _1));
	}
/*	else if (i != tcp::resolver::iterator())
	{
		// The connection failed. Try the next endpoint in the list.
		m_sock.close();
		m_sock.async_connect(*i, bind(&http_connection::on_connect
			, shared_from_this(), _1, ++i));
	} 
*/	else
	{ 
		close();
		if (m_bottled && m_called) return;
		m_called = true;
		m_handler(e, m_parser, 0, 0);
	}
}

void http_connection::on_write(asio::error_code const& e)
{
	if (e)
	{
		close();
		if (m_bottled && m_called) return;
		m_called = true;
		m_handler(e, m_parser, 0, 0);
		return;
	}

	std::string().swap(sendbuffer);
	m_recvbuffer.resize(4096);
	m_sock.async_read_some(asio::buffer(&m_recvbuffer[0] + m_read_pos
		, m_recvbuffer.size() - m_read_pos)
		, bind(&http_connection::on_read, shared_from_this(), _1, _2));
}

void http_connection::on_read(asio::error_code const& e
	, std::size_t bytes_transferred)
{
	if (e == asio::error::eof)
	{
		close();
		if (m_bottled && m_called) return;
		m_called = true;
		m_handler(asio::error_code(), m_parser, 0, 0);
		return;
	}

	if (e)
	{
		close();
		if (m_bottled && m_called) return;
		m_called = true;
		m_handler(e, m_parser, 0, 0);
		return;
	}

	m_read_pos += bytes_transferred;
	assert(m_read_pos <= int(m_recvbuffer.size()));

	if (m_bottled || !m_parser.header_finished())
	{
		libtorrent::buffer::const_interval rcv_buf(&m_recvbuffer[0]
			, &m_recvbuffer[0] + m_read_pos);
		m_parser.incoming(rcv_buf);
		if (!m_bottled && m_parser.header_finished())
		{
			if (m_read_pos > m_parser.body_start())
				m_handler(e, m_parser, &m_recvbuffer[0] + m_parser.body_start()
					, m_read_pos - m_parser.body_start());
			m_read_pos = 0;
			m_timer.expires_from_now(m_timeout);
			m_timer.async_wait(bind(&http_connection::on_timeout, shared_from_this(), _1));
		}
		else if (m_bottled && m_parser.finished())
		{
			m_timer.cancel();
			if (m_bottled && m_called) return;
			m_called = true;
			m_handler(e, m_parser, 0, 0);
		}
	}
	else
	{
		assert(!m_bottled);
		m_handler(e, m_parser, &m_recvbuffer[0], m_read_pos);
		m_read_pos = 0;
	}

	if (int(m_recvbuffer.size()) == m_read_pos)
		m_recvbuffer.resize(std::min(m_read_pos + 2048, 1024*500));
	if (m_read_pos == 1024 * 500)
	{
		close();
		if (m_bottled && m_called) return;
		m_called = true;
		m_handler(asio::error::eof, m_parser, 0, 0);
		return;
	}
	m_sock.async_read_some(asio::buffer(&m_recvbuffer[0] + m_read_pos
		, m_recvbuffer.size() - m_read_pos)
		, bind(&http_connection::on_read
		, shared_from_this(), _1, _2));
}

}

