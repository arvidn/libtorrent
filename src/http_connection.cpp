/*

Copyright (c) 2007-2014, Arvid Norberg
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
#include "libtorrent/socket_type.hpp" // for async_shutdown

#if defined TORRENT_ASIO_DEBUGGING
#include "libtorrent/debug.hpp"
#endif

#include <boost/bind.hpp>
#include <string>
#include <algorithm>

namespace libtorrent {

http_connection::http_connection(io_service& ios, connection_queue& cc
	, http_handler const& handler
	, bool bottled
	, int max_bottled_buffer_size
	, http_connect_handler const& ch
	, http_filter_handler const& fh
#ifdef TORRENT_USE_OPENSSL
	, boost::asio::ssl::context* ssl_ctx
#endif
	)
	: m_sock(ios)
#if TORRENT_USE_I2P
	, m_i2p_conn(0)
#endif
	, m_read_pos(0)
	, m_resolver(ios)
	, m_handler(handler)
	, m_connect_handler(ch)
	, m_filter_handler(fh)
	, m_timer(ios)
	, m_last_receive(time_now())
	, m_start_time(time_now())
	, m_bottled(bottled)
	, m_max_bottled_buffer_size(max_bottled_buffer_size)
	, m_called(false)
#ifdef TORRENT_USE_OPENSSL
	, m_ssl_ctx(ssl_ctx)
	, m_own_ssl_context(false)
#endif
	, m_rate_limit(0)
	, m_download_quota(0)
	, m_limiter_timer_active(false)
	, m_limiter_timer(ios)
	, m_redirects(5)
	, m_connection_ticket(-1)
	, m_cc(cc)
	, m_ssl(false)
	, m_priority(0)
	, m_abort(false)
{
	TORRENT_ASSERT(!m_handler.empty());
}

http_connection::~http_connection()
{
	TORRENT_ASSERT(m_connection_ticket == -1);
#ifdef TORRENT_USE_OPENSSL
	if (m_own_ssl_context) delete m_ssl_ctx;
#endif
}

void http_connection::get(std::string const& url, time_duration timeout, int prio
	, proxy_settings const* ps, int handle_redirects, std::string const& user_agent
	, address const& bind_addr
#if TORRENT_USE_I2P
	, i2p_connection* i2p_conn
#endif
	)
{
	m_user_agent = user_agent;

	std::string protocol;
	std::string auth;
	std::string hostname;
	std::string path;
	error_code ec;
	int port;

	boost::tie(protocol, auth, hostname, port, path)
		= parse_url_components(url, ec);

	int default_port = protocol == "https" ? 443 : 80;
	if (port == -1) port = default_port;

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
	
	char request[4096];
	char* end = request + sizeof(request);
	char* ptr = request;

#define APPEND_FMT(fmt) ptr += snprintf(ptr, end - ptr, fmt)
#define APPEND_FMT1(fmt, arg) ptr += snprintf(ptr, end - ptr, fmt, arg)
#define APPEND_FMT2(fmt, arg1, arg2) ptr += snprintf(ptr, end - ptr, fmt, arg1, arg2)

	// exclude ssl here, because SSL assumes CONNECT support in the
	// proxy and is handled at the lower layer
	if (ps && (ps->type == proxy_settings::http
		|| ps->type == proxy_settings::http_pw)
		&& !ssl)
	{
		// if we're using an http proxy and not an ssl
		// connection, just do a regular http proxy request
		APPEND_FMT1("GET %s HTTP/1.1\r\n", url.c_str());
		if (ps->type == proxy_settings::http_pw)
			APPEND_FMT1("Proxy-Authorization: Basic %s\r\n", base64encode(
				ps->username + ":" + ps->password).c_str());

		hostname = ps->hostname;
		port = ps->port;

		APPEND_FMT1("Host: %s", hostname.c_str());
		if (port != default_port) APPEND_FMT1(":%d\r\n", port);
		else APPEND_FMT("\r\n");
	}
	else
	{
		APPEND_FMT2("GET %s HTTP/1.1\r\n"
			"Host: %s", path.c_str(), hostname.c_str());
		if (port != default_port) APPEND_FMT1(":%d\r\n", port);
		else APPEND_FMT("\r\n");
	}

//	APPEND_FMT("Accept: */*\r\n");

	if (!m_user_agent.empty())
		APPEND_FMT1("User-Agent: %s\r\n", m_user_agent.c_str());
	
	if (m_bottled)
		APPEND_FMT("Accept-Encoding: gzip\r\n");

	if (!auth.empty())
		APPEND_FMT1("Authorization: Basic %s\r\n", base64encode(auth).c_str());

	APPEND_FMT("Connection: close\r\n\r\n");

	sendbuffer.assign(request);
	m_url = url;
	start(hostname, to_string(port).elems, timeout, prio
		, ps, ssl, handle_redirects, bind_addr
#if TORRENT_USE_I2P
		, i2p_conn
#endif
		);
}

void http_connection::start(std::string const& hostname, std::string const& port
	, time_duration timeout, int prio, proxy_settings const* ps, bool ssl, int handle_redirects
	, address const& bind_addr
#if TORRENT_USE_I2P
	, i2p_connection* i2p_conn
#endif
	)
{
	TORRENT_ASSERT(prio >= 0 && prio < 3);

	m_redirects = handle_redirects;
	if (ps) m_proxy = *ps;

	// keep ourselves alive even if the callback function
	// deletes this object
	boost::shared_ptr<http_connection> me(shared_from_this());

	m_completion_timeout = timeout;
	m_read_timeout = (std::max)(seconds(5), timeout / 5);
	error_code ec;
	m_timer.expires_from_now(m_completion_timeout, ec);
#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("http_connection::on_timeout");
#endif
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
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("http_connection::on_write");
#endif
		async_write(m_sock, asio::buffer(sendbuffer)
			, boost::bind(&http_connection::on_write, me, _1));
	}
	else
	{
		m_ssl = ssl;
		m_bind_addr = bind_addr;
		error_code ec;
		if (m_sock.is_open()) m_sock.close(ec);

#if TORRENT_USE_I2P
		bool is_i2p = false;
		char const* top_domain = strrchr(hostname.c_str(), '.');
		if (top_domain && strcmp(top_domain, ".i2p") == 0 && i2p_conn)
		{
			// this is an i2p name, we need to use the sam connection
			// to do the name lookup
			is_i2p = true;
			m_i2p_conn = i2p_conn;
			// quadruple the timeout for i2p destinations
			// because i2p is sloooooow
			m_completion_timeout *= 4;
			m_read_timeout *= 4;
		}
#endif

#if TORRENT_USE_I2P
		if (is_i2p && i2p_conn->proxy().type != proxy_settings::i2p_proxy)
		{
			m_resolver.get_io_service().post(boost::bind(&http_connection::callback
				, me, error_code(errors::no_i2p_router, get_libtorrent_category()), (char*)0, 0));
			return;
		}
#endif

		proxy_settings const* proxy = ps;
#if TORRENT_USE_I2P
		if (is_i2p) proxy = &i2p_conn->proxy();
#endif

		// in this case, the upper layer is assumed to have taken
		// care of the proxying already. Don't instantiate the socket
		// with this proxy
		if (proxy && (proxy->type == proxy_settings::http
			|| proxy->type == proxy_settings::http_pw)
			&& !ssl)
		{
			proxy = 0;
		}
		proxy_settings null_proxy;

		void* userdata = 0;
#ifdef TORRENT_USE_OPENSSL
		if (m_ssl)
		{
			if (m_ssl_ctx == 0)
			{
				m_ssl_ctx = new (std::nothrow) boost::asio::ssl::context(
					m_resolver.get_io_service(), asio::ssl::context::sslv23_client);
				if (m_ssl_ctx)
				{
					m_own_ssl_context = true;
					error_code ec;
					m_ssl_ctx->set_verify_mode(asio::ssl::context::verify_none, ec);
					TORRENT_ASSERT(!ec);
				}
			}
			userdata = m_ssl_ctx;
		}
#endif
		instantiate_connection(m_resolver.get_io_service()
			, proxy ? *proxy : null_proxy, m_sock, userdata);

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

		setup_ssl_hostname(m_sock, hostname, ec);
		if (ec)
		{
			m_resolver.get_io_service().post(boost::bind(&http_connection::callback
				, me, ec, (char*)0, 0));
			return;
		}

#if TORRENT_USE_I2P
		if (is_i2p)
		{
#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("http_connection::on_i2p_resolve");
#endif
			i2p_conn->async_name_lookup(hostname.c_str(), boost::bind(&http_connection::on_i2p_resolve
				, me, _1, _2));
		}
		else
#endif
		if (ps && ps->proxy_hostnames
			&& (ps->type == proxy_settings::socks5
				|| ps->type == proxy_settings::socks5_pw))
		{
			m_hostname = hostname;
			m_port = port;
			m_endpoints.push_back(tcp::endpoint(address(), atoi(port.c_str())));
			queue_connect();
		}
		else
		{
#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("http_connection::on_resolve");
#endif
			m_endpoints.clear();
			tcp::resolver::query query(hostname, port);
			m_resolver.async_resolve(query, boost::bind(&http_connection::on_resolve
				, me, _1, _2));
		}
		m_hostname = hostname;
		m_port = port;
	}
}

void http_connection::on_connect_timeout()
{
	TORRENT_ASSERT(m_connection_ticket > -1);

	// keep ourselves alive even if the callback function
	// deletes this object
	boost::shared_ptr<http_connection> me(shared_from_this());

	error_code ec;
	m_sock.close(ec);
}

void http_connection::on_timeout(boost::weak_ptr<http_connection> p
	, error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("http_connection::on_timeout");
#endif
	boost::shared_ptr<http_connection> c = p.lock();
	if (!c) return;

	if (e == asio::error::operation_aborted) return;

	if (c->m_abort) return;

	ptime now = time_now_hires();

	if (c->m_start_time + c->m_completion_timeout < now
		|| c->m_last_receive + c->m_read_timeout < now)
	{
		if (c->m_connection_ticket > -1 && !c->m_endpoints.empty())
		{
#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("http_connection::on_timeout");
#endif
			error_code ec;
			async_shutdown(c->m_sock, c);
			c->m_timer.expires_at((std::min)(
				c->m_last_receive + c->m_read_timeout
				, c->m_start_time + c->m_completion_timeout), ec);
			c->m_timer.async_wait(boost::bind(&http_connection::on_timeout, p, _1));
		}
		else
		{
			c->callback(asio::error::timed_out);
			c->close(true);
		}
		return;
	}

	if (!c->m_sock.is_open()) return;
#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("http_connection::on_timeout");
#endif
	error_code ec;
	c->m_timer.expires_at((std::min)(
		c->m_last_receive + c->m_read_timeout
		, c->m_start_time + c->m_completion_timeout), ec);
	c->m_timer.async_wait(boost::bind(&http_connection::on_timeout, p, _1));
}

void http_connection::close(bool force)
{
	if (m_abort) return;

	error_code ec;
	m_timer.cancel(ec);
	m_resolver.cancel();
	m_limiter_timer.cancel(ec);

	if (force)
		m_sock.close(ec);
	else
		async_shutdown(m_sock, shared_from_this());

	m_hostname.clear();
	m_port.clear();
	m_handler.clear();
	m_abort = true;
}

#if TORRENT_USE_I2P
void http_connection::on_i2p_resolve(error_code const& e
	, char const* destination)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("http_connection::on_i2p_resolve");
#endif
	if (e)
	{
		callback(e);
		close();
		return;
	}

#ifdef TORRENT_USE_OPENSSL
	TORRENT_ASSERT(m_ssl == false);
	TORRENT_ASSERT(m_sock.get<socket_type>());
	TORRENT_ASSERT(m_sock.get<socket_type>()->get<i2p_stream>());
	m_sock.get<socket_type>()->get<i2p_stream>()->set_destination(destination);
	m_sock.get<socket_type>()->get<i2p_stream>()->set_command(i2p_stream::cmd_connect);
	m_sock.get<socket_type>()->get<i2p_stream>()->set_session_id(m_i2p_conn->session_id());
#else
	m_sock.get<i2p_stream>()->set_destination(destination);
	m_sock.get<i2p_stream>()->set_command(i2p_stream::cmd_connect);
	m_sock.get<i2p_stream>()->set_session_id(m_i2p_conn->session_id());
#endif
#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("http_connection::on_connect");
#endif
	m_sock.async_connect(tcp::endpoint(), boost::bind(&http_connection::on_connect
		, shared_from_this(), _1));
}
#endif

void http_connection::on_resolve(error_code const& e
	, tcp::resolver::iterator i)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("http_connection::on_resolve");
#endif
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
		, m_read_timeout, m_priority);
}

void http_connection::connect(int ticket, tcp::endpoint target_address)
{
	if (ticket == -1)
	{
		close();
		return;
	}

	m_connection_ticket = ticket;
	if (m_proxy.proxy_hostnames
		&& (m_proxy.type == proxy_settings::socks5
			|| m_proxy.type == proxy_settings::socks5_pw))
	{
		// we're using a socks proxy and we're resolving
		// hostnames through it
#ifdef TORRENT_USE_OPENSSL
		if (m_ssl)
		{
			TORRENT_ASSERT(m_sock.get<ssl_stream<socks5_stream> >());
			m_sock.get<ssl_stream<socks5_stream> >()->next_layer().set_dst_name(m_hostname);
		}
		else
#endif
		{
			TORRENT_ASSERT(m_sock.get<socks5_stream>());
			m_sock.get<socks5_stream>()->set_dst_name(m_hostname);
		}
	}
#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("http_connection::on_connect");
#endif
	m_sock.async_connect(target_address, boost::bind(&http_connection::on_connect
		, shared_from_this(), _1));
}

void http_connection::on_connect(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("http_connection::on_connect");
#endif
	if (m_connection_ticket >= 0)
	{
		m_cc.done(m_connection_ticket);
		m_connection_ticket = -1;
	}

	m_last_receive = time_now_hires();
	m_start_time = m_last_receive;
	if (!e)
	{ 
		if (m_connect_handler) m_connect_handler(*this);
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("http_connection::on_write");
#endif
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

void http_connection::callback(error_code e, char* data, int size)
{
	if (m_bottled && m_called) return;

	std::vector<char> buf;
	if (data && m_bottled && m_parser.header_finished())
	{
		size = m_parser.collapse_chunk_headers((char*)data, size);

		std::string const& encoding = m_parser.header("content-encoding");
		if ((encoding == "gzip" || encoding == "x-gzip") && size > 0 && data)
		{
			error_code ec;
			inflate_gzip(data, size, buf, m_max_bottled_buffer_size, ec);

			if (ec)
			{
				if (m_handler) m_handler(ec, m_parser, data, size, *this);
				close();
				return;
			}
			size = int(buf.size());
			data = size == 0 ? 0 : &buf[0];
		}

		// if we completed the whole response, no need
		// to tell the user that the connection was closed by
		// the server or by us. Just clear any error
		if (m_parser.finished()) e.clear();
	}
	m_called = true;
	error_code ec;
	m_timer.cancel(ec);
	if (m_handler) m_handler(e, m_parser, data, size, *this);
}

void http_connection::on_write(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("http_connection::on_write");
#endif

	if (e == asio::error::operation_aborted) return;

	if (e)
	{
		boost::shared_ptr<http_connection> me(shared_from_this());
		callback(e);
		close();
		return;
	}

	if (m_abort) return;

	std::string().swap(sendbuffer);
	m_recvbuffer.resize(4096);

	int amount_to_read = m_recvbuffer.size() - m_read_pos;
	if (m_rate_limit > 0 && amount_to_read > m_download_quota)
	{
		amount_to_read = m_download_quota;
		if (m_download_quota == 0)
		{
			if (!m_limiter_timer_active)
			{
#if defined TORRENT_ASIO_DEBUGGING
				add_outstanding_async("http_connection::on_assign_bandwidth");
#endif
				on_assign_bandwidth(error_code());
			}
			return;
		}
	}
#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("http_connection::on_read");
#endif
	m_sock.async_read_some(asio::buffer(&m_recvbuffer[0] + m_read_pos
		, amount_to_read)
		, boost::bind(&http_connection::on_read
			, shared_from_this(), _1, _2));
}

void http_connection::on_read(error_code const& e
	, std::size_t bytes_transferred)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("http_connection::on_read");
#endif

	if (m_rate_limit)
	{
		m_download_quota -= bytes_transferred;
		TORRENT_ASSERT(m_download_quota >= 0);
	}

	if (e == asio::error::operation_aborted) return;

	if (m_abort) return;

	// keep ourselves alive even if the callback function
	// deletes this object
	boost::shared_ptr<http_connection> me(shared_from_this());

	// when using the asio SSL wrapper, it seems like
	// we get the shut_down error instead of EOF
	if (e == asio::error::eof || e == asio::error::shut_down)
	{
		error_code ec = asio::error::eof;
		TORRENT_ASSERT(bytes_transferred == 0);
		char* data = 0;
		std::size_t size = 0;
		if (m_bottled && m_parser.header_finished())
		{
			data = &m_recvbuffer[0] + m_parser.body_start();
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

			if (is_redirect(code))
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
				// it would be nice to gracefully shut down SSL here
				// but then we'd have to do all the reconnect logic
				// in its handler. For now, just kill the connection.
//				async_shutdown(m_sock, shared_from_this());
				m_sock.close(ec);

				std::string url = resolve_redirect_location(m_url, location);
				get(url, m_completion_timeout, m_priority, &m_proxy, m_redirects - 1
					, m_user_agent, m_bind_addr
#if TORRENT_USE_I2P
					, m_i2p_conn
#endif
					);
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
			callback(e, &m_recvbuffer[0] + m_parser.body_start(), m_parser.get_body().left());
		}
	}
	else
	{
		TORRENT_ASSERT(!m_bottled);
		callback(e, &m_recvbuffer[0], m_read_pos);
		m_read_pos = 0;
		m_last_receive = time_now_hires();
	}

	// if we've hit the limit, double the buffer size
	if (int(m_recvbuffer.size()) == m_read_pos)
		m_recvbuffer.resize((std::min)(m_read_pos * 2, m_max_bottled_buffer_size));

	if (m_read_pos == m_max_bottled_buffer_size)
	{
		// if we've reached the size limit, terminate the connection and
		// report the error
		callback(error_code(boost::system::errc::file_too_large, generic_category()));
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
			{
#if defined TORRENT_ASIO_DEBUGGING
				add_outstanding_async("http_connection::on_assign_bandwidth");
#endif
				on_assign_bandwidth(error_code());
			}
			return;
		}
	}
#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("http_connection::on_read");
#endif
	m_sock.async_read_some(asio::buffer(&m_recvbuffer[0] + m_read_pos
		, amount_to_read)
		, boost::bind(&http_connection::on_read
			, me, _1, _2));
}

void http_connection::on_assign_bandwidth(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("http_connection::on_assign_bandwidth");
#endif
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

#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("http_connection::on_read");
#endif
	m_sock.async_read_some(asio::buffer(&m_recvbuffer[0] + m_read_pos
		, amount_to_read)
		, boost::bind(&http_connection::on_read
			, shared_from_this(), _1, _2));

	error_code ec;
	m_limiter_timer_active = true;
	m_limiter_timer.expires_from_now(milliseconds(250), ec);
#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("http_connection::on_assign_bandwidth");
#endif
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
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("http_connection::on_assign_bandwidth");
#endif
		m_limiter_timer.async_wait(boost::bind(&http_connection::on_assign_bandwidth
			, shared_from_this(), _1));
	}
	m_rate_limit = limit;
}

}

