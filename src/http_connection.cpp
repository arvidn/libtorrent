/*

Copyright (c) 2007-2018, Arvid Norberg
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
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/instantiate_connection.hpp"
#include "libtorrent/gzip.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/socket_type.hpp" // for async_shutdown
#include "libtorrent/resolver_interface.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/random.hpp"

#if defined TORRENT_ASIO_DEBUGGING
#include "libtorrent/debug.hpp"
#endif

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/bind.hpp>
#include <string>
#include <algorithm>
#include <sstream>

#ifdef TORRENT_USE_OPENSSL
#include <boost/asio/ssl/context.hpp>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

http_connection::http_connection(io_service& ios
	, resolver_interface& resolver
	, http_handler const& handler
	, bool bottled
	, int max_bottled_buffer_size
	, http_connect_handler const& ch
	, http_filter_handler const& fh
#ifdef TORRENT_USE_OPENSSL
	, ssl::context* ssl_ctx
#endif
	)
	: m_next_ep(0)
	, m_sock(ios)
#ifdef TORRENT_USE_OPENSSL
	, m_ssl_ctx(ssl_ctx)
	, m_own_ssl_context(false)
#endif
#if TORRENT_USE_I2P
	, m_i2p_conn(0)
#endif
	, m_resolver(resolver)
	, m_handler(handler)
	, m_connect_handler(ch)
	, m_filter_handler(fh)
	, m_timer(ios)
	, m_read_timeout(seconds(5))
	, m_completion_timeout(seconds(5))
	, m_limiter_timer(ios)
	, m_last_receive(aux::time_now())
	, m_start_time(aux::time_now())
	, m_read_pos(0)
	, m_redirects(5)
	, m_max_bottled_buffer_size(max_bottled_buffer_size)
	, m_rate_limit(0)
	, m_download_quota(0)
	, m_priority(0)
	, m_resolve_flags(0)
	, m_port(0)
	, m_bottled(bottled)
	, m_called(false)
	, m_limiter_timer_active(false)
	, m_ssl(false)
	, m_abort(false)
	, m_connecting(false)
{
	TORRENT_ASSERT(!m_handler.empty());
}

http_connection::~http_connection()
{
#ifdef TORRENT_USE_OPENSSL
	if (m_own_ssl_context) delete m_ssl_ctx;
#endif
}

void http_connection::get(std::string const& url, time_duration timeout, int prio
	, aux::proxy_settings const* ps, int handle_redirects, std::string const& user_agent
	, boost::optional<address> bind_addr, int resolve_flags, std::string const& auth_
#if TORRENT_USE_I2P
	, i2p_connection* i2p_conn
#endif
	)
{
	m_user_agent = user_agent;
	m_resolve_flags = resolve_flags;

	std::string protocol;
	std::string auth;
	std::string hostname;
	std::string path;
	error_code ec;
	int port;

	boost::tie(protocol, auth, hostname, port, path)
		= parse_url_components(url, ec);

	if (auth.empty()) auth = auth_;

	m_auth = auth;

	int default_port = protocol == "https" ? 443 : 80;
	if (port == -1) port = default_port;

	// keep ourselves alive even if the callback function
	// deletes this object
	boost::shared_ptr<http_connection> me(shared_from_this());

	if (ec)
	{
		m_timer.get_io_service().post(boost::bind(&http_connection::callback
			, me, ec, static_cast<char*>(NULL), 0));
		return;
	}

	if (protocol != "http"
#ifdef TORRENT_USE_OPENSSL
		&& protocol != "https"
#endif
		)
	{
		error_code err(errors::unsupported_url_protocol);
		m_timer.get_io_service().post(boost::bind(&http_connection::callback
			, me, err, static_cast<char*>(NULL), 0));
		return;
	}

	TORRENT_ASSERT(prio >= 0 && prio < 3);

	bool ssl = false;
	if (protocol == "https") ssl = true;

	std::stringstream request;

	// exclude ssl here, because SSL assumes CONNECT support in the
	// proxy and is handled at the lower layer
	if (ps && (ps->type == settings_pack::http
		|| ps->type == settings_pack::http_pw)
		&& !ssl)
	{
		// if we're using an http proxy and not an ssl
		// connection, just do a regular http proxy request
		request << "GET " << url << " HTTP/1.1\r\n";
		if (ps->type == settings_pack::http_pw)
			request << "Proxy-Authorization: Basic " << base64encode(
				ps->username + ":" + ps->password) << "\r\n";

		hostname = ps->hostname;
		port = ps->port;

		request << "Host: " << hostname;
		if (port != default_port) request << ":" << port << "\r\n";
		else request << "\r\n";
	}
	else
	{
		request << "GET " << path << " HTTP/1.1\r\nHost: " << hostname;
		if (port != default_port) request << ":" << port << "\r\n";
		else request << "\r\n";
	}

//	request << "Accept: */*\r\n";

	if (!m_user_agent.empty())
		request << "User-Agent: " << m_user_agent << "\r\n";

	if (m_bottled)
		request << "Accept-Encoding: gzip\r\n";

	if (!auth.empty())
		request << "Authorization: Basic " << base64encode(auth) << "\r\n";

	request << "Connection: close\r\n\r\n";

	m_sendbuffer.assign(request.str());
	m_url = url;
	start(hostname, port, timeout, prio
		, ps, ssl, handle_redirects, bind_addr, m_resolve_flags
#if TORRENT_USE_I2P
		, i2p_conn
#endif
		);
}

void http_connection::start(std::string const& hostname, int port
	, time_duration timeout, int prio, aux::proxy_settings const* ps, bool ssl
	, int handle_redirects
	, boost::optional<address> bind_addr
	, int resolve_flags
#if TORRENT_USE_I2P
	, i2p_connection* i2p_conn
#endif
	)
{
	TORRENT_ASSERT(prio >= 0 && prio < 3);

	m_redirects = handle_redirects;
	m_resolve_flags = resolve_flags;
	if (ps) m_proxy = *ps;

	// keep ourselves alive even if the callback function
	// deletes this object
	boost::shared_ptr<http_connection> me(shared_from_this());

	m_completion_timeout = timeout;
	m_read_timeout = seconds(5);
	if (m_read_timeout < timeout / 5) m_read_timeout = timeout / 5;
	error_code ec;
	m_timer.expires_from_now((std::min)(
		m_read_timeout, m_completion_timeout), ec);
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
		m_timer.get_io_service().post(boost::bind(&http_connection::callback
			, me, ec, static_cast<char*>(NULL), 0));
		return;
	}

	if (m_sock.is_open() && m_hostname == hostname && m_port == port
		&& m_ssl == ssl && m_bind_addr == bind_addr)
	{
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("http_connection::on_write");
#endif
		async_write(m_sock, boost::asio::buffer(m_sendbuffer)
			, boost::bind(&http_connection::on_write, me, _1));
	}
	else
	{
		m_ssl = ssl;
		m_bind_addr = bind_addr;
		error_code err;
		if (m_sock.is_open()) m_sock.close(err);

		aux::proxy_settings const* proxy = ps;

#if TORRENT_USE_I2P
		bool is_i2p = false;
		char const* top_domain = strrchr(hostname.c_str(), '.');
		aux::proxy_settings i2p_proxy;
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

			if (i2p_conn->proxy().type != settings_pack::i2p_proxy)
			{
				m_timer.get_io_service().post(boost::bind(&http_connection::callback
					, me, error_code(errors::no_i2p_router), static_cast<char*>(NULL), 0));
				return;
			}

			i2p_proxy = i2p_conn->proxy();
			proxy = &i2p_proxy;
		}
#endif

		// in this case, the upper layer is assumed to have taken
		// care of the proxying already. Don't instantiate the socket
		// with this proxy
		if (proxy && (proxy->type == settings_pack::http
			|| proxy->type == settings_pack::http_pw)
			&& !ssl)
		{
			proxy = 0;
		}
		aux::proxy_settings null_proxy;

		void* userdata = 0;
#ifdef TORRENT_USE_OPENSSL
		if (m_ssl)
		{
			if (m_ssl_ctx == 0)
			{
				m_ssl_ctx = new (std::nothrow) ssl::context(ssl::context::sslv23_client);
				if (m_ssl_ctx)
				{
					m_own_ssl_context = true;
					m_ssl_ctx->set_verify_mode(ssl::context::verify_none, ec);
					if (ec)
					{
						m_timer.get_io_service().post(boost::bind(&http_connection::callback
								, me, ec, static_cast<char*>(NULL), 0));
						return;
					}
				}
			}
			userdata = m_ssl_ctx;
		}
#endif
		// assume this is not a tracker connection. Tracker connections that
		// shouldn't be subject to the proxy should pass in NULL as the proxy
		// pointer.
		instantiate_connection(m_timer.get_io_service()
			, proxy ? *proxy : null_proxy, m_sock, userdata, NULL, false, false);

		if (m_bind_addr)
		{
			m_sock.open(m_bind_addr->is_v4()?tcp::v4():tcp::v6(), ec);
			m_sock.bind(tcp::endpoint(*m_bind_addr, 0), ec);
			if (ec)
			{
				m_timer.get_io_service().post(boost::bind(&http_connection::callback
					, me, ec, static_cast<char*>(NULL), 0));
				return;
			}
		}

		setup_ssl_hostname(m_sock, hostname, ec);
		if (ec)
		{
			m_timer.get_io_service().post(boost::bind(&http_connection::callback
				, me, ec, static_cast<char*>(NULL), 0));
			return;
		}

		m_endpoints.clear();
		m_next_ep = 0;

#if TORRENT_USE_I2P
		if (is_i2p)
		{
			if (hostname.length() < 516) // Base64 encoded  destination with optional .i2p
			{
#if defined TORRENT_ASIO_DEBUGGING
				add_outstanding_async("http_connection::on_i2p_resolve");
#endif
				i2p_conn->async_name_lookup(hostname.c_str(), boost::bind(&http_connection::on_i2p_resolve
					, me, _1, _2));
			}
			else
				connect_i2p_tracker(hostname.c_str());
		}
		else
#endif
		if (ps && ps->proxy_hostnames
			&& (ps->type == settings_pack::socks5
				|| ps->type == settings_pack::socks5_pw))
		{
			m_hostname = hostname;
			m_port = port;
			m_endpoints.push_back(tcp::endpoint(address(), port));
			connect();
		}
		else
		{
#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("http_connection::on_resolve");
#endif
			m_resolver.async_resolve(hostname, m_resolve_flags
				, boost::bind(&http_connection::on_resolve
				, me, _1, _2));
		}
		m_hostname = hostname;
		m_port = port;
	}
}

void http_connection::on_timeout(boost::weak_ptr<http_connection> p
	, error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("http_connection::on_timeout");
#endif
	boost::shared_ptr<http_connection> c = p.lock();
	if (!c) return;

	if (e == boost::asio::error::operation_aborted) return;

	if (c->m_abort) return;

	time_point now = clock_type::now();

	if (c->m_start_time + c->m_completion_timeout <= now
		|| c->m_last_receive + c->m_read_timeout <= now)
	{
		// the connection timed out. If we have more endpoints to try, just
		// close this connection. The on_connect handler will try the next
		// endpoint in the list.
		if (c->m_next_ep < c->m_endpoints.size())
		{
			error_code ec;
			c->m_sock.close(ec);
			if (!c->m_connecting) c->connect();
			c->m_last_receive = now;
			c->m_start_time = c->m_last_receive;
		}
		else
		{
			c->callback(boost::asio::error::timed_out);
			return;
		}
	}
	else
	{
		if (!c->m_sock.is_open()) return;
	}

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
	if (force)
		m_sock.close(ec);
	else
		async_shutdown(m_sock, shared_from_this());

	m_timer.cancel(ec);
	m_limiter_timer.cancel(ec);

	m_hostname.clear();
	m_port = 0;
	m_handler.clear();
	m_abort = true;
}

#if TORRENT_USE_I2P
void http_connection::connect_i2p_tracker(char const* destination)
{
	TORRENT_ASSERT(m_sock.get<i2p_stream>());
#ifdef TORRENT_USE_OPENSSL
	TORRENT_ASSERT(m_ssl == false);
#endif
	m_sock.get<i2p_stream>()->set_destination(destination);
	m_sock.get<i2p_stream>()->set_command(i2p_stream::cmd_connect);
	m_sock.get<i2p_stream>()->set_session_id(m_i2p_conn->session_id());
#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("http_connection::on_connect");
#endif
	TORRENT_ASSERT(!m_connecting);
	m_connecting = true;
	m_sock.async_connect(tcp::endpoint(), boost::bind(&http_connection::on_connect
		, shared_from_this(), _1));
}

void http_connection::on_i2p_resolve(error_code const& e, char const* destination)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("http_connection::on_i2p_resolve");
#endif
	if (e)
	{
		callback(e);
		return;
	}
	connect_i2p_tracker(destination);
}
#endif

void http_connection::on_resolve(error_code const& e
	, std::vector<address> const& addresses)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("http_connection::on_resolve");
#endif
	if (e)
	{
		callback(e);
		return;
	}
	TORRENT_ASSERT(!addresses.empty());

	for (std::vector<address>::const_iterator i = addresses.begin()
		, end(addresses.end()); i != end; ++i)
		m_endpoints.push_back(tcp::endpoint(*i, m_port));

	if (m_filter_handler) m_filter_handler(*this, m_endpoints);
	if (m_endpoints.empty())
	{
		close();
		return;
	}

	std::random_shuffle(m_endpoints.begin(), m_endpoints.end(), randint);

	// The following statement causes msvc to crash (ICE). Since it's not
	// necessary in the vast majority of cases, just ignore the endpoint
	// order for windows
#if !defined _MSC_VER || _MSC_VER > 1310
	// sort the endpoints so that the ones with the same IP version as our
	// bound listen socket are first. So that when contacting a tracker,
	// we'll talk to it from the same IP that we're listening on
	if (m_bind_addr)
		std::partition(m_endpoints.begin(), m_endpoints.end()
			, boost::bind(&address::is_v4, boost::bind(&tcp::endpoint::address, _1))
				== m_bind_addr->is_v4());
#endif

	connect();
}

void http_connection::connect()
{
	TORRENT_ASSERT(m_next_ep < m_endpoints.size());

	boost::shared_ptr<http_connection> me(shared_from_this());

	if (m_proxy.proxy_hostnames
		&& (m_proxy.type == settings_pack::socks5
			|| m_proxy.type == settings_pack::socks5_pw))
	{
		// test to see if m_hostname really just is an IP (and not a hostname). If it
		// is, ec will be represent "success". If so, don't set it as the socks5
		// hostname, just connect to the IP
		error_code ec;
		address adr = address::from_string(m_hostname, ec);

		if (ec)
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
		else
		{
			m_endpoints[0].address(adr);
		}
	}

	TORRENT_ASSERT(m_next_ep < m_endpoints.size());
	if (m_next_ep >= m_endpoints.size()) return;

	tcp::endpoint target_address = m_endpoints[m_next_ep];
	++m_next_ep;

#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("http_connection::on_connect");
#endif
	TORRENT_ASSERT(!m_connecting);
	m_connecting = true;
	m_sock.async_connect(target_address, boost::bind(&http_connection::on_connect
		, shared_from_this(), _1));
}

void http_connection::on_connect(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("http_connection::on_connect");
#endif
	TORRENT_ASSERT(m_connecting);
	m_connecting = false;

	m_last_receive = clock_type::now();
	m_start_time = m_last_receive;
	if (!e)
	{
		if (m_connect_handler) m_connect_handler(*this);
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("http_connection::on_write");
#endif
		async_write(m_sock, boost::asio::buffer(m_sendbuffer)
			, boost::bind(&http_connection::on_write, shared_from_this(), _1));
	}
	else if (m_next_ep < m_endpoints.size() && !m_abort)
	{
		// The connection failed. Try the next endpoint in the list.
		error_code ec;
		m_sock.close(ec);
		connect();
	}
	else
	{
		callback(e);
	}
}

void http_connection::callback(error_code e, char* data, int size)
{
	if (m_bottled && m_called) return;

	std::vector<char> buf;
	if (data && m_bottled && m_parser.header_finished())
	{
		size = m_parser.collapse_chunk_headers(data, size);

		std::string const& encoding = m_parser.header("content-encoding");
		if ((encoding == "gzip" || encoding == "x-gzip") && size > 0 && data)
		{
			error_code ec;
			inflate_gzip(data, size, buf, m_max_bottled_buffer_size, ec);

			if (ec)
			{
				if (m_handler) m_handler(ec, m_parser, data, size, *this);
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

	if (e == boost::asio::error::operation_aborted) return;

	if (e)
	{
		callback(e);
		return;
	}

	if (m_abort) return;

	std::string().swap(m_sendbuffer);
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
	m_sock.async_read_some(boost::asio::buffer(&m_recvbuffer[0] + m_read_pos
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

	if (e == boost::asio::error::operation_aborted) return;

	if (m_abort) return;

	// keep ourselves alive even if the callback function
	// deletes this object
	boost::shared_ptr<http_connection> me(shared_from_this());

	// when using the asio SSL wrapper, it seems like
	// we get the shut_down error instead of EOF
	if (e == boost::asio::error::eof || e == boost::asio::error::shut_down)
	{
		error_code ec = boost::asio::error::eof;
		TORRENT_ASSERT(bytes_transferred == 0);
		char* data = 0;
		std::size_t size = 0;
		if (m_bottled && m_parser.header_finished())
		{
			data = &m_recvbuffer[0] + m_parser.body_start();
			size = m_parser.get_body().left();
		}
		callback(ec, data, size);
		return;
	}

	if (e)
	{
		TORRENT_ASSERT(bytes_transferred == 0);
		callback(e);
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
					, m_user_agent, m_bind_addr, m_resolve_flags, m_auth
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
			m_last_receive = clock_type::now();
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
		m_last_receive = clock_type::now();
	}

	// if we've hit the limit, double the buffer size
	if (int(m_recvbuffer.size()) == m_read_pos)
		m_recvbuffer.resize((std::min)(m_read_pos * 2, m_max_bottled_buffer_size));

	if (m_read_pos == m_max_bottled_buffer_size)
	{
		// if we've reached the size limit, terminate the connection and
		// report the error
		callback(error_code(boost::system::errc::file_too_large, generic_category()));
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
	m_sock.async_read_some(boost::asio::buffer(&m_recvbuffer[0] + m_read_pos
		, amount_to_read)
		, boost::bind(&http_connection::on_read
			, me, _1, _2));
}

void http_connection::on_assign_bandwidth(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("http_connection::on_assign_bandwidth");
#endif
	if ((e == boost::asio::error::operation_aborted
		&& m_limiter_timer_active)
		|| !m_sock.is_open())
	{
		callback(boost::asio::error::eof);
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
	m_sock.async_read_some(boost::asio::buffer(&m_recvbuffer[0] + m_read_pos
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

