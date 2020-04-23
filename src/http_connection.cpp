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
#include "libtorrent/aux_/instantiate_connection.hpp"
#include "libtorrent/gzip.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/socket_type.hpp" // for async_shutdown
#include "libtorrent/resolver_interface.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/time.hpp"

#include <functional>
#include <string>
#include <algorithm>
#include <sstream>

#ifdef TORRENT_USE_OPENSSL
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ssl/context.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

using namespace std::placeholders;

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
#endif
#if TORRENT_USE_I2P
	, m_i2p_conn(nullptr)
#endif
	, m_resolver(resolver)
	, m_handler(handler)
	, m_connect_handler(ch)
	, m_filter_handler(fh)
	, m_timer(ios)
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
	, m_resolve_flags{}
	, m_port(0)
	, m_bottled(bottled)
	, m_called(false)
	, m_limiter_timer_active(false)
	, m_ssl(false)
	, m_abort(false)
	, m_connecting(false)
{
	TORRENT_ASSERT(m_handler);
}

http_connection::~http_connection() = default;

void http_connection::get(std::string const& url, time_duration timeout, int prio
	, aux::proxy_settings const* ps, int handle_redirects, std::string const& user_agent
	, boost::optional<address> const& bind_addr, resolver_flags const resolve_flags, std::string const& auth_
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

	std::tie(protocol, auth, hostname, port, path)
		= parse_url_components(url, ec);

	if (auth.empty()) auth = auth_;

	m_auth = auth;

	int default_port = protocol == "https" ? 443 : 80;
	if (port == -1) port = default_port;

	// keep ourselves alive even if the callback function
	// deletes this object
	std::shared_ptr<http_connection> me(shared_from_this());

	if (ec)
	{
		lt::get_io_service(m_timer).post(std::bind(&http_connection::callback
			, me, ec, span<char>{}));
		return;
	}

	if (protocol != "http"
#ifdef TORRENT_USE_OPENSSL
		&& protocol != "https"
#endif
		)
	{
		error_code err(errors::unsupported_url_protocol);
		lt::get_io_service(m_timer).post(std::bind(&http_connection::callback
			, me, err, span<char>{}));
		return;
	}

	TORRENT_ASSERT(prio >= 0 && prio < 3);

	bool const ssl = (protocol == "https");

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

		request << "Host: " << hostname;
		if (port != default_port) request << ":" << port << "\r\n";
		else request << "\r\n";

		hostname = ps->hostname;
		port = ps->port;
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
	, boost::optional<address> const& bind_addr
	, resolver_flags const resolve_flags
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
	std::shared_ptr<http_connection> me(shared_from_this());

	m_completion_timeout = timeout;
	error_code ec;
	m_timer.expires_from_now(m_completion_timeout, ec);
	ADD_OUTSTANDING_ASYNC("http_connection::on_timeout");
	m_timer.async_wait(std::bind(&http_connection::on_timeout
		, std::weak_ptr<http_connection>(me), _1));
	m_called = false;
	m_parser.reset();
	m_recvbuffer.clear();
	m_read_pos = 0;
	m_priority = prio;

#ifdef TORRENT_USE_OPENSSL
	TORRENT_ASSERT(!ssl || m_ssl_ctx != nullptr);
#endif

	if (ec)
	{
		lt::get_io_service(m_timer).post(std::bind(&http_connection::callback
			, me, ec, span<char>{}));
		return;
	}

	if (m_sock.is_open() && m_hostname == hostname && m_port == port
		&& m_ssl == ssl && m_bind_addr == bind_addr)
	{
		ADD_OUTSTANDING_ASYNC("http_connection::on_write");
		async_write(m_sock, boost::asio::buffer(m_sendbuffer)
			, std::bind(&http_connection::on_write, me, _1));
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
		if (top_domain && top_domain == ".i2p"_sv && i2p_conn)
		{
			// this is an i2p name, we need to use the sam connection
			// to do the name lookup
			is_i2p = true;
			m_i2p_conn = i2p_conn;
			// quadruple the timeout for i2p destinations
			// because i2p is sloooooow
			m_completion_timeout *= 4;

#if TORRENT_USE_I2P
			if (i2p_conn->proxy().type != settings_pack::i2p_proxy)
			{
				lt::get_io_service(m_timer).post(std::bind(&http_connection::callback
					, me, error_code(errors::no_i2p_router), span<char>{}));
				return;
			}
#endif

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
			proxy = nullptr;
		}
		aux::proxy_settings null_proxy;

		void* userdata = nullptr;
#ifdef TORRENT_USE_OPENSSL
		if (m_ssl)
		{
			TORRENT_ASSERT(m_ssl_ctx != nullptr);
			userdata = m_ssl_ctx;
		}
#endif
		// assume this is not a tracker connection. Tracker connections that
		// shouldn't be subject to the proxy should pass in nullptr as the proxy
		// pointer.
		instantiate_connection(lt::get_io_service(m_timer)
			, proxy ? *proxy : null_proxy, m_sock, userdata, nullptr, false, false);

		if (m_bind_addr)
		{
			m_sock.open(m_bind_addr->is_v4() ? tcp::v4() : tcp::v6(), ec);
			m_sock.bind(tcp::endpoint(*m_bind_addr, 0), ec);
			if (ec)
			{
				lt::get_io_service(m_timer).post(std::bind(&http_connection::callback
					, me, ec, span<char>{}));
				return;
			}
		}

		setup_ssl_hostname(m_sock, hostname, ec);
		if (ec)
		{
			lt::get_io_service(m_timer).post(std::bind(&http_connection::callback
				, me, ec, span<char>{}));
			return;
		}

		m_endpoints.clear();
		m_next_ep = 0;

#if TORRENT_USE_I2P
		if (is_i2p)
		{
			if (hostname.length() < 516) // Base64 encoded  destination with optional .i2p
			{
				ADD_OUTSTANDING_ASYNC("http_connection::on_i2p_resolve");
				i2p_conn->async_name_lookup(hostname.c_str(), std::bind(&http_connection::on_i2p_resolve
					, me, _1, _2));
			}
			else
				connect_i2p_tracker(hostname.c_str());
		}
		else
#endif
		m_hostname = hostname;
		if (ps && ps->proxy_hostnames
			&& (ps->type == settings_pack::socks5
				|| ps->type == settings_pack::socks5_pw))
		{
			m_port = std::uint16_t(port);
			m_endpoints.emplace_back(address(), m_port);
			connect();
		}
		else
		{
			ADD_OUTSTANDING_ASYNC("http_connection::on_resolve");
			m_resolver.async_resolve(hostname, m_resolve_flags
				, std::bind(&http_connection::on_resolve
				, me, _1, _2));
		}
		m_port = std::uint16_t(port);
	}
}

void http_connection::on_timeout(std::weak_ptr<http_connection> p
	, error_code const& e)
{
	COMPLETE_ASYNC("http_connection::on_timeout");
	std::shared_ptr<http_connection> c = p.lock();
	if (!c) return;

	if (e == boost::asio::error::operation_aborted) return;

	if (c->m_abort) return;

	time_point const now = clock_type::now();

	if (c->m_start_time + c->m_completion_timeout <= now)
	{
		// the connection timed out. If we have more endpoints to try, just
		// close this connection. The on_connect handler will try the next
		// endpoint in the list.
		if (c->m_next_ep < int(c->m_endpoints.size()))
		{
			error_code ec;
			c->m_sock.close(ec);
			if (!c->m_connecting) c->connect();
			c->m_last_receive = now;
			c->m_start_time = c->m_last_receive;
		}
		else
		{
			// the socket may have an outstanding operation, that keeps the
			// http_connection object alive. We want to cancel all that.
			error_code ec;
			c->m_sock.close(ec);
			c->callback(boost::asio::error::timed_out);
			return;
		}
	}
	else
	{
		if (!c->m_sock.is_open()) return;
	}

	ADD_OUTSTANDING_ASYNC("http_connection::on_timeout");
	error_code ec;
	c->m_timer.expires_at(c->m_start_time + c->m_completion_timeout, ec);
	c->m_timer.async_wait(std::bind(&http_connection::on_timeout, p, _1));
}

void http_connection::close(bool force)
{
	if (m_abort) return;

	error_code ec;
	if (force)
	{
		m_sock.close(ec);
		m_timer.cancel(ec);
	}
	else
	{
		async_shutdown(m_sock, shared_from_this());
		m_timer.cancel(ec);
	}

	m_limiter_timer.cancel(ec);

	m_hostname.clear();
	m_port = 0;
	m_handler = nullptr;
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
	ADD_OUTSTANDING_ASYNC("http_connection::on_connect");
	TORRENT_ASSERT(!m_connecting);
	m_connecting = true;
	m_sock.async_connect(tcp::endpoint(), std::bind(&http_connection::on_connect
		, shared_from_this(), _1));
}

void http_connection::on_i2p_resolve(error_code const& e, char const* destination)
{
	COMPLETE_ASYNC("http_connection::on_i2p_resolve");
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
	COMPLETE_ASYNC("http_connection::on_resolve");
	if (e)
	{
		callback(e);
		return;
	}
	TORRENT_ASSERT(!addresses.empty());

	for (auto const& addr : addresses)
		m_endpoints.emplace_back(addr, m_port);

	if (m_filter_handler) m_filter_handler(*this, m_endpoints);
	if (m_endpoints.empty())
	{
		close();
		return;
	}

	aux::random_shuffle(m_endpoints);

	// if we have been told to bind to a particular address
	// only connect to addresses of the same family
	if (m_bind_addr)
	{
		auto const new_end = std::remove_if(m_endpoints.begin(), m_endpoints.end()
			, [&](tcp::endpoint const& ep) { return is_v4(ep) != m_bind_addr->is_v4(); });

		m_endpoints.erase(new_end, m_endpoints.end());
		if (m_endpoints.empty())
		{
			callback(error_code(boost::system::errc::address_family_not_supported, generic_category()));
			close();
			return;
		}
	}

	connect();
}

void http_connection::connect()
{
	TORRENT_ASSERT(m_next_ep < int(m_endpoints.size()));

	std::shared_ptr<http_connection> me(shared_from_this());

	if (m_proxy.proxy_hostnames
		&& (m_proxy.type == settings_pack::socks5
			|| m_proxy.type == settings_pack::socks5_pw))
	{
		// test to see if m_hostname really just is an IP (and not a hostname). If it
		// is, ec will be represent "success". If so, don't set it as the socks5
		// hostname, just connect to the IP
		error_code ec;
		address adr = make_address(m_hostname, ec);

		if (ec)
		{
			// we're using a socks proxy and we're resolving
			// hostnames through it
#ifdef TORRENT_USE_OPENSSL
			if (m_ssl)
			{
				TORRENT_ASSERT(m_sock.get<ssl_stream<socks5_stream>>());
				m_sock.get<ssl_stream<socks5_stream>>()->next_layer().set_dst_name(m_hostname);
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

	TORRENT_ASSERT(m_next_ep < int(m_endpoints.size()));
	if (m_next_ep >= int(m_endpoints.size())) return;

	tcp::endpoint target_address = m_endpoints[m_next_ep];
	++m_next_ep;

	ADD_OUTSTANDING_ASYNC("http_connection::on_connect");
	TORRENT_ASSERT(!m_connecting);
	m_connecting = true;
	m_sock.async_connect(target_address, std::bind(&http_connection::on_connect
		, me, _1));
}

void http_connection::on_connect(error_code const& e)
{
	COMPLETE_ASYNC("http_connection::on_connect");
	TORRENT_ASSERT(m_connecting);
	m_connecting = false;

	m_last_receive = clock_type::now();
	m_start_time = m_last_receive;
	if (!e)
	{
		if (m_connect_handler) m_connect_handler(*this);
		ADD_OUTSTANDING_ASYNC("http_connection::on_write");
		async_write(m_sock, boost::asio::buffer(m_sendbuffer)
			, std::bind(&http_connection::on_write, shared_from_this(), _1));
	}
	else if (m_next_ep < int(m_endpoints.size()) && !m_abort)
	{
		// The connection failed. Try the next endpoint in the list.
		error_code ec;
		m_sock.close(ec);
		connect();
	}
	else
	{
		error_code ec;
		m_sock.close(ec);
		callback(e);
	}
}

void http_connection::callback(error_code e, span<char> data)
{
	if (m_bottled && m_called) return;

	std::vector<char> buf;
	if (!data.empty() && m_bottled && m_parser.header_finished())
	{
		data = m_parser.collapse_chunk_headers(data);

		std::string const& encoding = m_parser.header("content-encoding");
		if (encoding == "gzip" || encoding == "x-gzip")
		{
			error_code ec;
			inflate_gzip(data, buf, m_max_bottled_buffer_size, ec);

			if (ec)
			{
				if (m_handler) m_handler(ec, m_parser, data, *this);
				return;
			}
			data = buf;
		}

		// if we completed the whole response, no need
		// to tell the user that the connection was closed by
		// the server or by us. Just clear any error
		if (m_parser.finished()) e.clear();
	}
	m_called = true;
	error_code ec;
	m_timer.cancel(ec);
	if (m_handler) m_handler(e, m_parser, data, *this);
}

void http_connection::on_write(error_code const& e)
{
	COMPLETE_ASYNC("http_connection::on_write");

	if (e == boost::asio::error::operation_aborted) return;

	if (e)
	{
		callback(e);
		return;
	}

	if (m_abort) return;

	std::string().swap(m_sendbuffer);
	m_recvbuffer.resize(4096);

	int amount_to_read = int(m_recvbuffer.size()) - m_read_pos;
	if (m_rate_limit > 0 && amount_to_read > m_download_quota)
	{
		amount_to_read = m_download_quota;
		if (m_download_quota == 0)
		{
			if (!m_limiter_timer_active)
			{
				ADD_OUTSTANDING_ASYNC("http_connection::on_assign_bandwidth");
				on_assign_bandwidth(error_code());
			}
			return;
		}
	}
	ADD_OUTSTANDING_ASYNC("http_connection::on_read");
	m_sock.async_read_some(boost::asio::buffer(m_recvbuffer.data() + m_read_pos
		, std::size_t(amount_to_read))
		, std::bind(&http_connection::on_read
			, shared_from_this(), _1, _2));
}

void http_connection::on_read(error_code const& e
	, std::size_t bytes_transferred)
{
	COMPLETE_ASYNC("http_connection::on_read");

	if (m_rate_limit)
	{
		m_download_quota -= int(bytes_transferred);
		TORRENT_ASSERT(m_download_quota >= 0);
	}

	if (e == boost::asio::error::operation_aborted) return;

	if (m_abort) return;

	// keep ourselves alive even if the callback function
	// deletes this object
	std::shared_ptr<http_connection> me(shared_from_this());

	// when using the asio SSL wrapper, it seems like
	// we get the shut_down error instead of EOF
	if (e == boost::asio::error::eof || e == boost::asio::error::shut_down)
	{
		error_code ec = boost::asio::error::eof;
		TORRENT_ASSERT(bytes_transferred == 0);
		span<char> body;
		if (m_bottled && m_parser.header_finished())
		{
			body = span<char>(m_recvbuffer.data() + m_parser.body_start()
				, m_parser.get_body().size());
		}
		callback(ec, body);
		return;
	}

	if (e)
	{
		TORRENT_ASSERT(bytes_transferred == 0);
		callback(e);
		return;
	}

	m_read_pos += int(bytes_transferred);
	TORRENT_ASSERT(m_read_pos <= int(m_recvbuffer.size()));

	if (m_bottled || !m_parser.header_finished())
	{
		span<char const> rcv_buf(m_recvbuffer);
		bool error = false;
		m_parser.incoming(rcv_buf.first(m_read_pos), error);
		if (error)
		{
			// HTTP parse error
			error_code ec = errors::http_parse_error;
			callback(ec);
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
//				async_shutdown(m_sock, me);
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
			{
				callback(e, span<char>(m_recvbuffer)
					.first(m_read_pos)
					.subspan(m_parser.body_start()));
			}
			m_read_pos = 0;
			m_last_receive = clock_type::now();
		}
		else if (m_bottled && m_parser.finished())
		{
			error_code ec;
			m_timer.cancel(ec);
			callback(e, span<char>(m_recvbuffer)
				.first(m_read_pos)
				.subspan(m_parser.body_start()));
		}
	}
	else
	{
		TORRENT_ASSERT(!m_bottled);
		callback(e, span<char>(m_recvbuffer).first(m_read_pos));
		m_read_pos = 0;
		m_last_receive = clock_type::now();
	}

	// if we've hit the limit, double the buffer size
	if (int(m_recvbuffer.size()) == m_read_pos)
		m_recvbuffer.resize(std::min(m_read_pos * 2, m_max_bottled_buffer_size));

	if (m_read_pos == m_max_bottled_buffer_size)
	{
		// if we've reached the size limit, terminate the connection and
		// report the error
		callback(error_code(boost::system::errc::file_too_large, generic_category()));
		return;
	}
	int amount_to_read = int(m_recvbuffer.size()) - m_read_pos;
	if (m_rate_limit > 0 && amount_to_read > m_download_quota)
	{
		amount_to_read = m_download_quota;
		if (m_download_quota == 0)
		{
			if (!m_limiter_timer_active)
			{
				ADD_OUTSTANDING_ASYNC("http_connection::on_assign_bandwidth");
				on_assign_bandwidth(error_code());
			}
			return;
		}
	}
	ADD_OUTSTANDING_ASYNC("http_connection::on_read");
	m_sock.async_read_some(boost::asio::buffer(m_recvbuffer.data() + m_read_pos
		, std::size_t(amount_to_read))
		, std::bind(&http_connection::on_read
			, me, _1, _2));
}

void http_connection::on_assign_bandwidth(error_code const& e)
{
	COMPLETE_ASYNC("http_connection::on_assign_bandwidth");
	if ((e == boost::asio::error::operation_aborted
		&& m_limiter_timer_active)
		|| !m_sock.is_open())
	{
		callback(boost::asio::error::eof);
		return;
	}
	m_limiter_timer_active = false;
	if (e) return;

	if (m_abort) return;

	if (m_download_quota > 0) return;

	m_download_quota = m_rate_limit / 4;

	int amount_to_read = int(m_recvbuffer.size()) - m_read_pos;
	if (amount_to_read > m_download_quota)
		amount_to_read = m_download_quota;

	if (!m_sock.is_open()) return;

	ADD_OUTSTANDING_ASYNC("http_connection::on_read");
	m_sock.async_read_some(boost::asio::buffer(m_recvbuffer.data() + m_read_pos
		, std::size_t(amount_to_read))
		, std::bind(&http_connection::on_read
			, shared_from_this(), _1, _2));

	error_code ec;
	m_limiter_timer_active = true;
	m_limiter_timer.expires_from_now(milliseconds(250), ec);
	ADD_OUTSTANDING_ASYNC("http_connection::on_assign_bandwidth");
	m_limiter_timer.async_wait(std::bind(&http_connection::on_assign_bandwidth
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
		ADD_OUTSTANDING_ASYNC("http_connection::on_assign_bandwidth");
		m_limiter_timer.async_wait(std::bind(&http_connection::on_assign_bandwidth
			, shared_from_this(), _1));
	}
	m_rate_limit = limit;
}

}
