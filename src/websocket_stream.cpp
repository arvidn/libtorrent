/*

Copyright (c) 2020, Arvid Norberg
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
#include "libtorrent/aux_/random.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/connect.hpp>
#include <rtc/rtc.hpp> // for overloaded
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <algorithm>
#include <tuple>

namespace lt::aux {

constexpr seconds WEBSOCKET_KEEPALIVE_PERIOD(10);

namespace http = boost::beast::http;
namespace error = boost::asio::error;
using namespace std::placeholders;

websocket_stream::websocket_stream(io_context& ios
		, resolver_interface& resolver
		, ssl::context* ssl_ctx
		)
	: m_io_service(ios)
	, m_resolver(resolver)
	, m_ssl_context(ssl_ctx)
	, m_stream(std::in_place_type_t<stream_type>{}, ios)
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
		std::visit([&](auto& stream)
			{
				stream.async_close(websocket::close_code::none
					, std::bind(&websocket_stream::on_close, shared_from_this(), _1));
			}
			, m_stream);
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

	if (protocol == "ws")
	{
		m_stream.emplace<stream_type>(m_io_service);
	}
	else if (protocol == "wss" && m_ssl_context)
	{
		m_stream.emplace<ssl_stream_type>(m_io_service, *m_ssl_context);
	}
	else {
		if (auto handler = std::exchange(m_connect_handler, nullptr))
			post(m_io_service, std::bind(std::move(handler), error::no_protocol_option));
		return;
	}

	if (port <= 0) port = protocol == "ws" ? 80 : 443;

	if (!target.empty()) m_target = std::move(target);
	else m_target = "/";

	do_resolve(hostname, std::uint16_t(port));
}

void websocket_stream::do_resolve(std::string hostname, std::uint16_t port)
{
	m_hostname = std::move(hostname);
	m_port = std::move(port);

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

	auto* tcp_stream = std::visit(rtc::overloaded
		{
			[&](stream_type &stream) { return &stream.next_layer(); }
			, [&](ssl_stream_type &stream) { return &stream.next_layer().next_layer(); }
		}
		, m_stream);

	ADD_OUTSTANDING_ASYNC("websocket_stream::on_tcp_connect");
	boost::asio::async_connect(*tcp_stream
		, m_endpoints.rbegin()
		, m_endpoints.rend()
		, std::bind(&websocket_stream::on_tcp_connect, shared_from_this(), _1));
}

void websocket_stream::on_tcp_connect(error_code const& ec)
{
	COMPLETE_ASYNC("websocket_stream::on_tcp_connect");
	if (ec)
	{
		if (auto handler = std::exchange(m_connect_handler, nullptr))
			post(m_io_service, std::bind(std::move(handler), ec));
		return;
	}

	if (std::holds_alternative<ssl_stream_type>(m_stream)) do_ssl_handshake();
	else do_handshake();
}

void websocket_stream::do_ssl_handshake()
{
	auto& ssl_stream = std::get<ssl_stream_type>(m_stream).next_layer();

	error_code ec;
	ssl::set_host_name(ssl::get_handle(ssl_stream), m_hostname, ec);
	if (ec)
	{
		if (auto handler = std::exchange(m_connect_handler, nullptr))
			post(m_io_service, std::bind(std::move(handler), ec));
		return;
	}

	ADD_OUTSTANDING_ASYNC("websocket_stream::on_ssl_handshake");
	ssl_stream.async_handshake(ssl::stream_base::client
			, std::bind(&websocket_stream::on_ssl_handshake, shared_from_this(), _1));
}

void websocket_stream::on_ssl_handshake(error_code const& ec)
{
	COMPLETE_ASYNC("websocket_stream::on_ssl_handshake");
	if (ec)
	{
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

	bool is_ssl = std::holds_alternative<ssl_stream_type>(m_stream);
	std::string host = m_hostname;
	if((!is_ssl && m_port != 80) || (is_ssl &&  m_port != 443))
		host += std::string(":") + std::to_string(m_port);

	ADD_OUTSTANDING_ASYNC("websocket_stream::on_handshake");
	std::visit([&](auto& stream)
		{
#if BOOST_VERSION >= 107000
			stream.set_option(websocket::stream_base::decorator(user_agent_handler));
			stream.async_handshake(host
				, m_target
				, std::bind(&websocket_stream::on_handshake, shared_from_this(), _1));
#else
			stream.async_handshake_ex(host
				, m_target
				, user_agent_handler
				, std::bind(&websocket_stream::on_handshake, shared_from_this(), _1));
#endif
	}
	, m_stream);
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
	std::visit([&](auto& stream)
		{
			stream.async_ping({}, std::bind(&websocket_stream::on_ping
				, shared_from_this(), _1));
		}
		, m_stream);

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
