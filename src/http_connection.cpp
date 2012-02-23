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
#include "libtorrent/escape_string.hpp"
#include "libtorrent/instantiate_connection.hpp"
#include "libtorrent/gzip.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/connection_queue.hpp"

#include <boost/bind.hpp>
#include <string>
#include <algorithm>

namespace libtorrent {

enum { max_bottled_buffer = 1024 * 1024 };

void http_connection::get(std::string const& url, time_duration timeout, int prio
	, proxy_settings const* ps, int handle_redirects, std::string const& user_agent
	, address const& bind_addr)
{
	std::string protocol;
	std::string auth;
	std::string hostname;
	std::string path;
	error_code ec;
	int port;

	boost::tie(protocol, auth, hostname, port, path)
		= parse_url_components(url, ec);

	int default_port = protocol == "https" ? 443 : 80;

	// keep ourselves alive even if the callback function
	// deletes this object
	boost::shared_ptr<http_connection> me(shared_from_this());

	if (protocol != "http"
#ifdef TORRENT_USE_OPENSSL
		&& protocol != "https"
#endif
		)
	{
		error_code ec(errors::unsupported_url_protocol);
		m_resolver.get_io_service().post(boost::bind(&http_connection::callback
			, me, ec, (char*)0, 0));
		return;
	}

	if (ec)
	{
		m_resolver.get_io_service().post(boost::bind(&http_connection::callback
			, me, ec, (char*)0, 0));
		return;
	}

	TORRENT_ASSERT(prio >= 0 && prio < 3);

	bool ssl = false;
	if (protocol == "https") ssl = true;
	
	char request[2048];
	char* end = request + sizeof(request);
	char* ptr = request;

#define APPEND_FMT(fmt) ptr += snprintf(ptr, end - ptr, fmt)
#define APPEND_FMT1(fmt, arg) ptr += snprintf(ptr, end - ptr, fmt, arg)
#define APPEND_FMT2(fmt, arg1, arg2) ptr += snprintf(ptr, end - ptr, fmt, arg1, arg2)

	if (ps && (ps->type == proxy_settings::http
		|| ps->type == proxy_settings::http_pw)
		&& !ssl)
	{
		// if we're using an http proxy and not an ssl
		// connection, just do a regular http proxy request
		APPEND_FMT1("GET %s HTTP/1.0\r\n", url.c_str());
		if (ps->type == proxy_settings::http_pw)
			APPEND_FMT1("Proxy-Authorization: Basic %s\r\n", base64encode(
				ps->username + ":" + ps->password).c_str());
		hostname = ps->hostname;
		port = ps->port;
	}
	else
	{
		APPEND_FMT2("GET %s HTTP/1.0\r\n"
			"Host: %s", path.c_str(), hostname.c_str());
		if (port != default_port) APPEND_FMT1(":%d\r\n", port);
		else APPEND_FMT("\r\n");
	}

	if (!auth.empty())
		APPEND_FMT1("Authorization: Basic %s\r\n", base64encode(auth).c_str());

	if (!user_agent.empty())
		APPEND_FMT1("User-Agent: %s\r\n", user_agent.c_str());
	
	if (m_bottled)
		APPEND_FMT("Accept-Encoding: gzip\r\n");

	APPEND_FMT("Connection: close\r\n\r\n");

	sendbuffer.assign(request);
	m_url = url;
	start(hostname, to_string(port).elems, timeout, prio
		, ps, ssl, handle_redirects, bind_addr);
}

void http_connection::start(std::string const& hostname, std::string const& port
	, time_duration timeout, int prio, proxy_settings const* ps, bool ssl, int handle_redirects
	, address const& bind_addr)
{
	TORRENT_ASSERT(prio >= 0 && prio < 3);

	m_redirects = handle_redirects;
	if (ps) m_proxy = *ps;

	// keep ourselves alive even if the callback function
	// deletes this object
	boost::shared_ptr<http_connection> me(shared_from_this());

	m_timeout = timeout;
	error_code ec;
	m_timer.expires_from_now(m_timeout, ec);
	m_timer.async_wait(boost::bind(&http_connection::on_timeout
		, boost::weak_ptr<http_connection>(me), _1));
	m_called = false;
	m_parser.reset();
	m_recvbuffer.clear();
	m_read_pos = 0;
	m_priority = prio;

	if (ec)
	{
		m_resolver.get_io_service().post(boost::bind(&http_connection::callback
			, me, ec, (char*)0, 0));
		return;
	}

	if (m_sock.is_open() && m_hostname == hostname && m_port == port
		&& m_ssl == ssl && m_bind_addr == bind_addr)
	{
		async_write(m_sock, asio::buffer(sendbuffer)
			, boost::bind(&http_connection::on_write, me, _1));
	}
	else
	{
		m_ssl = ssl;
		m_bind_addr = bind_addr;
		error_code ec;
		m_sock.close(ec);

		// in this case, the upper layer is assumed to have taken
		// care of the proxying already. Don't instantiate the socket
		// with this proxy
		if (ps && (ps->type == proxy_settings::http
			|| ps->type == proxy_settings::http_pw)
			&& !ssl)
		{
			ps = 0;
		}
		proxy_settings null_proxy;

#ifdef TORRENT_USE_OPENSSL
		if (m_ssl)
		{
			m_sock.instantiate<ssl_stream<socket_type> >(m_resolver.get_io_service());
			ssl_stream<socket_type>* s = m_sock.get<ssl_stream<socket_type> >();
			TORRENT_ASSERT(s);
			bool ret = instantiate_connection(m_resolver.get_io_service()
				, ps ? *ps : null_proxy, s->next_layer());
			TORRENT_ASSERT(ret);
		}
		else
		{
			m_sock.instantiate<socket_type>(m_resolver.get_io_service());
			bool ret = instantiate_connection(m_resolver.get_io_service()
				, ps ? *ps : null_proxy, *m_sock.get<socket_type>());
			TORRENT_ASSERT(ret);
		}
#else
		bool ret = instantiate_connection(m_resolver.get_io_service()
			, ps ? *ps : null_proxy, m_sock);
		TORRENT_ASSERT(ret);
#endif
		if (m_bind_addr != address_v4::any())
		{
			error_code ec;
			m_sock.open(m_bind_addr.is_v4()?tcp::v4():tcp::v6(), ec);
			m_sock.bind(tcp::endpoint(m_bind_addr, 0), ec);
			if (ec)
			{
				m_resolver.get_io_service().post(boost::bind(&http_connection::callback
					, me, ec, (char*)0, 0));
				return;
			}
		}

		m_endpoints.clear();
		tcp::resolver::query query(hostname, port);
		m_resolver.async_resolve(query, boost::bind(&http_connection::on_resolve
			, me, _1, _2));
		m_hostname = hostname;
		m_port = port;
	}
}

void http_connection::on_connect_timeout()
{
	if (m_connection_ticket > -1) m_cc.done(m_connection_ticket);
	m_connection_ticket = -1;

	// keep ourselves alive even if the callback function
	// deletes this object
	boost::shared_ptr<http_connection> me(shared_from_this());

	if (!m_endpoints.empty())
	{
		error_code ec;
		m_sock.close(ec);
	} 
	else
	{ 
		callback(asio::error::timed_out);
		close();
	}
}

void http_connection::on_timeout(boost::weak_ptr<http_connection> p
	, error_code const& e)
{
	boost::shared_ptr<http_connection> c = p.lock();
	if (!c) return;

	if (e == asio::error::operation_aborted) return;

	if (c->m_last_receive + c->m_timeout < time_now_hires())
	{
		if (c->m_connection_ticket > -1 && !c->m_endpoints.empty())
		{
			error_code ec;
			c->m_sock.close(ec);
			c->m_timer.expires_at(c->m_last_receive + c->m_timeout, ec);
			c->m_timer.async_wait(boost::bind(&http_connection::on_timeout, p, _1));
		}
		else
		{
			c->callback(asio::error::timed_out);
			c->close();
		}
		return;
	}

	if (!c->m_sock.is_open()) return;
	error_code ec;
	c->m_timer.expires_at(c->m_last_receive + c->m_timeout, ec);
	c->m_timer.async_wait(boost::bind(&http_connection::on_timeout, p, _1));
}

void http_connection::close()
{
	error_code ec;
	m_timer.cancel(ec);
	m_resolver.cancel();
	m_limiter_timer.cancel(ec);
	m_sock.close(ec);
	m_hostname.clear();
	m_port.clear();
	m_handler.clear();
	m_abort = true;
}

void http_connection::on_resolve(error_code const& e
	, tcp::resolver::iterator i)
{
	if (e)
	{
		boost::shared_ptr<http_connection> me(shared_from_this());

		callback(e);
		close();
		return;
	}
	TORRENT_ASSERT(i != tcp::resolver::iterator());

	std::transform(i, tcp::resolver::iterator(), std::back_inserter(m_endpoints)
		, boost::bind(&tcp::resolver::iterator::value_type::endpoint, _1));

	if (m_filter_handler) m_filter_handler(*this, m_endpoints);
	if (m_endpoints.empty())
	{
		close();
		return;
	}

	// The following statement causes msvc to crash (ICE). Since it's not
	// necessary in the vast majority of cases, just ignore the endpoint
	// order for windows
#if !defined _MSC_VER || _MSC_VER > 1310
	// sort the endpoints so that the ones with the same IP version as our
	// bound listen socket are first. So that when contacting a tracker,
	// we'll talk to it from the same IP that we're listening on
	if (m_bind_addr != address_v4::any())
		std::partition(m_endpoints.begin(), m_endpoints.end()
			, boost::bind(&address::is_v4, boost::bind(&tcp::endpoint::address, _1))
				== m_bind_addr.is_v4());
#endif

	queue_connect();
}

void http_connection::queue_connect()
{
	TORRENT_ASSERT(!m_endpoints.empty());
	tcp::endpoint target = m_endpoints.front();
	m_endpoints.pop_front();

	m_cc.enqueue(boost::bind(&http_connection::connect, shared_from_this(), _1, target)
		, boost::bind(&http_connection::on_connect_timeout, shared_from_this())
		, m_timeout, m_priority);
}

void http_connection::connect(int ticket, tcp::endpoint target_address)
{
	m_connection_ticket = ticket;
	m_sock.async_connect(target_address, boost::bind(&http_connection::on_connect
		, shared_from_this(), _1));
}

void http_connection::on_connect(error_code const& e)
{
	if (m_connection_ticket >= 0)
	{
		m_cc.done(m_connection_ticket);
		m_connection_ticket = -1;
	}

	m_last_receive = time_now_hires();
	if (!e)
	{ 
		if (m_connect_handler) m_connect_handler(*this);
		async_write(m_sock, asio::buffer(sendbuffer)
			, boost::bind(&http_connection::on_write, shared_from_this(), _1));
	}
	else if (!m_endpoints.empty() && !m_abort)
	{
		// The connection failed. Try the next endpoint in the list.
		error_code ec;
		m_sock.close(ec);
		queue_connect();
	} 
	else
	{ 
		boost::shared_ptr<http_connection> me(shared_from_this());
		callback(e);
		close();
	}
}

void http_connection::callback(error_code const& e, char const* data, int size)
{
	if (m_bottled && m_called) return;

	std::vector<char> buf;
	if (data && m_bottled && m_parser.header_finished())
	{
		std::string const& encoding = m_parser.header("content-encoding");
		if ((encoding == "gzip" || encoding == "x-gzip") && size > 0 && data)
		{
			std::string error;
			if (inflate_gzip(data, size, buf, max_bottled_buffer, error))
			{
				if (m_handler) m_handler(errors::http_failed_decompress, m_parser, data, size, *this);
				close();
				return;
			}
			size = int(buf.size());
			data = size == 0 ? 0 : &buf[0];
		}
	}
	m_called = true;
	error_code ec;
	m_timer.cancel(ec);
	if (m_handler) m_handler(e, m_parser, data, size, *this);
}

void http_connection::on_write(error_code const& e)
{
	if (e)
	{
		boost::shared_ptr<http_connection> me(shared_from_this());
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
				on_assign_bandwidth(error_code());
			return;
		}
	}
	m_sock.async_read_some(asio::buffer(&m_recvbuffer[0] + m_read_pos
		, amount_to_read)
		, boost::bind(&http_connection::on_read
			, shared_from_this(), _1, _2));
}

void http_connection::on_read(error_code const& e
	, std::size_t bytes_transferred)
{
	if (m_rate_limit)
	{
		m_download_quota -= bytes_transferred;
		TORRENT_ASSERT(m_download_quota >= 0);
	}

	// keep ourselves alive even if the callback function
	// deletes this object
	boost::shared_ptr<http_connection> me(shared_from_this());

	// when using the asio SSL wrapper, it seems like
	// we get the shut_down error instead of EOF
	if (e == asio::error::eof || e == asio::error::shut_down)
	{
		error_code ec = asio::error::eof;
		TORRENT_ASSERT(bytes_transferred == 0);
		char const* data = 0;
		std::size_t size = 0;
		if (m_bottled && m_parser.header_finished())
		{
			data = m_parser.get_body().begin;
			size = m_parser.get_body().left();
		}
		callback(ec, data, size);
		close();
		return;
	}

	if (e)
	{
		TORRENT_ASSERT(bytes_transferred == 0);
		callback(e);
		close();
		return;
	}

	m_read_pos += bytes_transferred;
	TORRENT_ASSERT(m_read_pos <= int(m_recvbuffer.size()));

	if (m_bottled || !m_parser.header_finished())
	{
		libtorrent::buffer::const_interval rcv_buf(&m_recvbuffer[0]
			, &m_recvbuffer[0] + m_read_pos);
		bool error = false;
		m_parser.incoming(rcv_buf, error);
		if (error)
		{
			// HTTP parse error
			error_code ec = errors::http_parse_error;
			callback(ec, 0, 0);
			return;
		}

		// having a nonempty path means we should handle redirects
		if (m_redirects && m_parser.header_finished())
		{
			int code = m_parser.status_code();

			if (code >= 300 && code < 400)
			{
				// attempt a redirect
				std::string const& location = m_parser.header("location");
				if (location.empty())
				{
					// missing location header
					callback(error_code(errors::http_missing_location));
					close();
					return;
				}

				error_code ec;
				m_sock.close(ec);
				using boost::tuples::ignore;
				boost::tie(ignore, ignore, ignore, ignore, ignore)
					= parse_url_components(location, ec);
				if (!ec)
				{
					get(location, m_timeout, m_priority, &m_proxy, m_redirects - 1);
				}
				else
				{
					// some broken web servers send out relative paths
					// in the location header.
					std::string url = m_url;
					// remove the leaf filename
					std::size_t i = url.find_last_of('/');
					if (i != std::string::npos)
						url.resize(i);
					if ((url.empty() || url[url.size()-1] != '/')
						&& (location.empty() || location[0] != '/'))
						url += '/';
					url += location;

					get(url, m_timeout, m_priority, &m_proxy, m_redirects - 1);
				}
				return;
			}
	
			m_redirects = 0;
		}

		if (!m_bottled && m_parser.header_finished())
		{
			if (m_read_pos > m_parser.body_start())
				callback(e, &m_recvbuffer[0] + m_parser.body_start()
					, m_read_pos - m_parser.body_start());
			m_read_pos = 0;
			m_last_receive = time_now_hires();
		}
		else if (m_bottled && m_parser.finished())
		{
			error_code ec;
			m_timer.cancel(ec);
			callback(e, m_parser.get_body().begin, m_parser.get_body().left());
		}
	}
	else
	{
		TORRENT_ASSERT(!m_bottled);
		callback(e, &m_recvbuffer[0], m_read_pos);
		m_read_pos = 0;
		m_last_receive = time_now_hires();
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
				on_assign_bandwidth(error_code());
			return;
		}
	}
	m_sock.async_read_some(asio::buffer(&m_recvbuffer[0] + m_read_pos
		, amount_to_read)
		, boost::bind(&http_connection::on_read
			, me, _1, _2));
}

void http_connection::on_assign_bandwidth(error_code const& e)
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
		, boost::bind(&http_connection::on_read
			, shared_from_this(), _1, _2));

	error_code ec;
	m_limiter_timer_active = true;
	m_limiter_timer.expires_from_now(milliseconds(250), ec);
	m_limiter_timer.async_wait(boost::bind(&http_connection::on_assign_bandwidth
		, shared_from_this(), _1));
}

void http_connection::rate_limit(int limit)
{
	if (!m_sock.is_open()) return;

	if (!m_limiter_timer_active)
	{
		error_code ec;
		m_limiter_timer_active = true;
		m_limiter_timer.expires_from_now(milliseconds(250), ec);
		m_limiter_timer.async_wait(boost::bind(&http_connection::on_assign_bandwidth
			, shared_from_this(), _1));
	}
	m_rate_limit = limit;
}

}

