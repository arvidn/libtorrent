/*

Copyright (c) 2020-2021, Arvid Norberg
Copyright (c) 2020-2021, Paul-Louis Ageneau
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp" // for TORRENT_USE_RTC

#if TORRENT_USE_RTC

#include "libtorrent/aux_/websocket_stream.hpp"
#include "libtorrent/aux_/debug.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/parse_url.hpp"
#include "libtorrent/aux_/instantiate_connection.hpp"
#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/aux_/ip_helpers.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/aux_/string_util.hpp" // for is_i2p_url
#include "libtorrent/error_code.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <algorithm>
#include <tuple>
#include <type_traits>

namespace libtorrent::aux {

constexpr seconds WEBSOCKET_KEEPALIVE_PERIOD(10);

namespace http = boost::beast::http;
namespace error = boost::asio::error;
using namespace std::placeholders;

namespace {

std::optional<websocket_socket_type> make_websocket_socket(socket_type& socket)
{
	return std::visit(
		[](auto& stream) -> std::optional<websocket_socket_type> {
			using stream_type = std::decay_t<decltype(stream)>;
			if constexpr (std::is_same_v<stream_type, tcp::socket>
				|| std::is_same_v<stream_type, socks5_stream>
				|| std::is_same_v<stream_type, http_stream>
#if TORRENT_USE_SSL
				|| std::is_same_v<stream_type, ssl_stream<tcp::socket>>
				|| std::is_same_v<stream_type, ssl_stream<socks5_stream>>
				|| std::is_same_v<stream_type, ssl_stream<http_stream>>
#endif
			)
				return websocket_socket_type(std::move(stream));
			else
				return std::nullopt;
		},
		socket.var());
}

void setup_websocket_ssl_hostname(
	websocket_socket_type& socket, std::string const& hostname, error_code& ec)
{
#if TORRENT_USE_SSL
	std::visit(
		[&](auto& stream) {
			using stream_type = std::decay_t<decltype(stream)>;
			if constexpr (std::is_same_v<stream_type, ssl_stream<tcp::socket>>
				|| std::is_same_v<stream_type, ssl_stream<socks5_stream>>
				|| std::is_same_v<stream_type, ssl_stream<http_stream>>)
			{
				stream.set_host_name(hostname, ec);
				if (!ec)
					stream.set_verify_callback(ssl::host_name_verification(hostname), ec);
			}
		},
		socket.var());
#else
	// Plain WS has no TLS hostname to configure. WSS is rejected before
	// this helper is called when SSL support is unavailable.
	TORRENT_UNUSED(socket);
	TORRENT_UNUSED(hostname);
	TORRENT_UNUSED(ec);
#endif
}

} // anonymous namespace

websocket_stream::websocket_stream(
	io_context& ios, resolver_interface& resolver, ssl::context* ssl_ctx)
	: websocket_stream(ios, resolver, ssl_ctx, proxy_settings{})
{}

websocket_stream::websocket_stream(
	io_context& ios, resolver_interface& resolver, ssl::context* ssl_ctx, proxy_settings proxy)
	: m_io_service(ios)
	, m_resolver(resolver)
	, m_ssl_context(ssl_ctx)
	, m_proxy(std::move(proxy))
	, m_open(false)
	, m_keepalive_timer(ios)
{

}

void websocket_stream::close()
{
	if (auto handler = std::exchange(m_connect_handler, nullptr))
		post(m_io_service, std::bind(std::move(handler), error::operation_aborted));

	m_keepalive_timer.cancel();

	if (m_open)
	{
		m_open = false;

		ADD_OUTSTANDING_ASYNC("websocket_stream::on_close");
		m_stream->async_close(websocket::close_code::none,
			std::bind(&websocket_stream::on_close, shared_from_this(), _1));
	}
}

close_reason_t websocket_stream::get_close_reason()
{
	return close_reason_t::none;
}

void websocket_stream::set_user_agent(std::string user_agent)
{
	m_user_agent = std::move(user_agent);
}

void websocket_stream::do_connect(std::string url)
{
	if (m_open)
	{
		if (auto handler = std::exchange(m_connect_handler, nullptr))
			post(m_io_service, std::bind(std::move(handler), error::already_connected));
		return;
	}

	m_url = std::move(url);

	error_code ec;
	auto [protocol, _ignored, hostname, port, target] = parse_url_components(m_url, ec);
	TORRENT_UNUSED(_ignored);
	if (ec)
	{
		if (auto handler = std::exchange(m_connect_handler, nullptr))
			post(m_io_service, std::bind(std::move(handler), ec));
		return;
	}

#if TORRENT_USE_I2P
	// i2p destinations must not be looked up via DNS
	if (is_i2p_url(m_url))
	{
		if (auto handler = std::exchange(m_connect_handler, nullptr))
			post(m_io_service,
				std::bind(std::move(handler), error_code(errors::unsupported_url_protocol)));
		return;
	}
#endif

	if (protocol != "ws" && protocol != "wss")
	{
		if (auto handler = std::exchange(m_connect_handler, nullptr))
			post(m_io_service, std::bind(std::move(handler), error::no_protocol_option));
		return;
	}

	if (protocol == "wss" && !m_ssl_context)
	{
		if (auto handler = std::exchange(m_connect_handler, nullptr))
			post(m_io_service, std::bind(std::move(handler), error::no_protocol_option));
		return;
	}

	if (port <= 0) port = protocol == "ws" ? 80 : 443;

	if (!target.empty()) m_target = std::move(target);
	else m_target = "/";

	m_hostname = std::move(hostname);
	m_port = std::uint16_t(port);

	bool const use_proxy = m_proxy.type != settings_pack::none && m_proxy.proxy_tracker_connections;
	m_remote_dns = use_proxy && m_proxy.proxy_hostnames && !is_ip_address(m_hostname)
		&& (m_proxy.type == settings_pack::socks5 || m_proxy.type == settings_pack::socks5_pw
			|| m_proxy.type == settings_pack::http || m_proxy.type == settings_pack::http_pw);

	if (m_remote_dns)
		do_tcp_connect({tcp::endpoint(address(), m_port)});
	else
		do_resolve(m_hostname, m_port);
}

void websocket_stream::do_resolve(std::string hostname, std::uint16_t port)
{
	m_hostname = std::move(hostname);
	m_port = port;

	ADD_OUTSTANDING_ASYNC("websocket_stream::on_resolve");
	m_resolver.async_resolve(m_hostname
		, resolver_interface::abort_on_shutdown
		, std::bind(&websocket_stream::on_resolve, shared_from_this(), _1, _2));
}

void websocket_stream::on_resolve(error_code const& ec, std::vector<address> const& addresses)
{
	COMPLETE_ASYNC("websocket_stream::on_resolve");
	if (ec)
	{
		if (auto handler = std::exchange(m_connect_handler, nullptr))
			post(m_io_service, std::bind(std::move(handler), ec));
		return;
	}

	TORRENT_ASSERT(!addresses.empty());

	std::vector<tcp::endpoint> endpoints;
	for (auto const& addr : addresses)
		endpoints.emplace_back(addr, m_port);

	do_tcp_connect(std::move(endpoints));
}

void websocket_stream::do_tcp_connect(std::vector<tcp::endpoint> endpoints)
{
	m_endpoints = std::move(endpoints);
	m_endpoint_index = 0;
	connect_endpoint();
}

void websocket_stream::connect_endpoint()
{
	if (!m_connect_handler)
		return;
	TORRENT_ASSERT(m_endpoint_index < m_endpoints.size());

	bool const use_proxy = m_proxy.type != settings_pack::none && m_proxy.proxy_tracker_connections;
	proxy_settings proxy = m_proxy;
	if (!use_proxy)
		proxy.type = settings_pack::none;


	void* const ssl_context =
		m_url.compare(0, 4, "wss:") == 0 ? static_cast<void*>(m_ssl_context) : nullptr;
	auto socket = instantiate_connection(m_io_service, proxy, ssl_context, nullptr, false, true);

	// Each endpoint attempt gets a fresh transport. Proxy streams retain
	// connection state (and HTTP CONNECT state) after some failures, so they
	// cannot safely be reused for the next resolved endpoint.
	auto websocket_socket = make_websocket_socket(socket);
	if (!websocket_socket)
	{
		if (auto handler = std::exchange(m_connect_handler, nullptr))
			post(m_io_service, std::bind(std::move(handler), error::operation_not_supported));
		return;
	}
	m_stream.reset();
	m_stream.emplace(std::move(*websocket_socket));

	// Configure proxy destination details on the final transport. This mirrors
	// http_connection::connect(), and ensures SOCKS5 receives the destination
	// hostname rather than the placeholder endpoint used for remote DNS.
	auto& transport = m_stream->next_layer();
	if (use_proxy && (m_proxy.type == settings_pack::http || m_proxy.type == settings_pack::http_pw)
		&& (m_remote_dns || m_proxy.send_host_in_connect))
	{
		if (auto* http_socket = std::get_if<http_stream>(&transport.var()))
			http_socket->set_host(m_hostname);
#if TORRENT_USE_SSL
		else if (auto* ssl_http_socket = std::get_if<ssl_stream<http_stream>>(&transport.var()))
			ssl_http_socket->next_layer().set_host(m_hostname);
#endif
	}

	if (m_remote_dns
		&& (m_proxy.type == settings_pack::socks5 || m_proxy.type == settings_pack::socks5_pw))
	{
		if (auto* socks_socket = std::get_if<socks5_stream>(&transport.var()))
			socks_socket->set_dst_name(m_hostname);
#if TORRENT_USE_SSL
		else if (auto* ssl_socks_socket = std::get_if<ssl_stream<socks5_stream>>(&transport.var()))
			ssl_socks_socket->next_layer().set_dst_name(m_hostname);
#endif
	}

	// Set the TLS hostname before connecting, preserving SNI and certificate validation.
	error_code ssl_ec;
	setup_websocket_ssl_hostname(m_stream->next_layer(), m_hostname, ssl_ec);
	if (ssl_ec)
	{
		if (auto handler = std::exchange(m_connect_handler, nullptr))
			post(m_io_service, std::bind(std::move(handler), ssl_ec));
		return;
	}

	ADD_OUTSTANDING_ASYNC("websocket_stream::on_tcp_connect");
	m_stream->next_layer().async_connect(m_endpoints[m_endpoint_index],
		std::bind(&websocket_stream::on_tcp_connect, shared_from_this(), _1));
}

void websocket_stream::terminate_socket()
{
	error_code ignore;
	m_stream->next_layer().close(ignore);
}

void websocket_stream::on_tcp_connect(error_code const& ec)
{
	COMPLETE_ASYNC("websocket_stream::on_tcp_connect");

	// close() clears m_connect_handler. Don't retry or start the WebSocket
	// handshake after the caller has cancelled connection establishment.
	if (!m_connect_handler)
	{
		auto const self = shared_from_this();
		post(m_io_service, [self] {
			if (!self->m_connect_handler && self->m_stream)
				self->terminate_socket();
		});
		return;
	}

	if (ec)
	{
		if (++m_endpoint_index < m_endpoints.size())
		{
			// The proxy stream may still be unwinding its completion callback
			// (e.g. after a rejected HTTP CONNECT). Replace it only after that
			// callback has returned.
			auto const self = shared_from_this();
			post(m_io_service, [self] {
				if (self->m_connect_handler)
					self->connect_endpoint();
			});
			return;
		}
		if (auto handler = std::exchange(m_connect_handler, nullptr))
			post(m_io_service, std::bind(std::move(handler), ec));
		return;
	}

	do_handshake();
}

void websocket_stream::do_handshake()
{
	auto user_agent_handler = [user_agent = m_user_agent](websocket::request_type& req)
		{
			if (!user_agent.empty()) req.set(http::field::user_agent, user_agent);
		};

	bool const is_ssl = m_url.compare(0, 4, "wss:") == 0;
	std::string host = m_hostname;
	if ((!is_ssl && m_port != 80) || (is_ssl && m_port != 443))
		host += std::string(":") + std::to_string(m_port);

	ADD_OUTSTANDING_ASYNC("websocket_stream::on_handshake");
#if BOOST_VERSION >= 107000
	m_stream->set_option(websocket::stream_base::decorator(user_agent_handler));
	m_stream->async_handshake(
		host, m_target, std::bind(&websocket_stream::on_handshake, shared_from_this(), _1));
#else
	m_stream->async_handshake_ex(host,
		m_target,
		user_agent_handler,
		std::bind(&websocket_stream::on_handshake, shared_from_this(), _1));
#endif
}

void websocket_stream::on_handshake(error_code const& ec)
{
	COMPLETE_ASYNC("websocket_stream::on_handshake");
	auto handler = std::exchange(m_connect_handler, nullptr);
	if (ec)
	{
		if (handler) post(m_io_service, std::bind(std::move(handler), ec));
		return;
	}

	m_open = true;
	arm_keepalive();

	if (handler) post(m_io_service, std::bind(std::move(handler), ec));
	else close();
}

void websocket_stream::on_read(error_code ec, std::size_t bytes_read, read_handler handler)
{
	COMPLETE_ASYNC("websocket_stream::on_read");

	if (ec) m_open = false;

	post(m_io_service, std::bind(std::move(handler), ec, bytes_read));
}

void websocket_stream::on_write(error_code ec, std::size_t bytes_written, write_handler handler)
{
	COMPLETE_ASYNC("websocket_stream::on_write");

	if (!ec) arm_keepalive();

	post(m_io_service, std::bind(std::move(handler), ec, bytes_written));
}

void websocket_stream::on_close(error_code)
{
	COMPLETE_ASYNC("websocket_stream::on_close");
}

void websocket_stream::on_keepalive(error_code ec)
{
	if (ec || !m_open) return;

	ADD_OUTSTANDING_ASYNC("websocket_stream::on_ping");
	m_stream->async_ping({}, std::bind(&websocket_stream::on_ping, shared_from_this(), _1));
}

void websocket_stream::on_ping(error_code ec)
{
	COMPLETE_ASYNC("websocket_stream::on_ping");

	if (ec) return;

	arm_keepalive();
}

void websocket_stream::arm_keepalive()
{
	m_keepalive_timer.expires_after(WEBSOCKET_KEEPALIVE_PERIOD);
	m_keepalive_timer.async_wait(std::bind(&websocket_stream::on_keepalive, shared_from_this(), _1));
}

}

#endif // TORRENT_USE_RTC
