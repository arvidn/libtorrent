/*

Copyright (c) 2020-2021, Alden Torres
Copyright (c) 2020, Arvid Norberg
Copyright (c) 2020-2021, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_WEBSOCKET_STREAM_HPP_INCLUDED
#define TORRENT_WEBSOCKET_STREAM_HPP_INCLUDED

#include "libtorrent/config.hpp"

#if TORRENT_USE_RTC

#include "libtorrent/assert.hpp"
#include "libtorrent/close_reason.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/aux_/resolver_interface.hpp"
#include "libtorrent/aux_/ssl.hpp"
#include "libtorrent/aux_/ssl_stream.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/debug.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <functional>
#include <vector>
#include <string>
#include <variant>

namespace boost {
namespace beast {
namespace websocket {

// We need to provide an overload of teardown and async_teardown
// to use beast::websocket::stream with lt::ssl::stream

template<class Stream>
void teardown(role_type
	, lt::aux::ssl::stream<Stream>& stream
	, error_code& ec)
{
    stream.shutdown(ec);
}

template<class Stream, class Handler>
void async_teardown(role_type
	, lt::aux::ssl::stream<Stream>& stream
	, Handler&& handler)
{
    stream.async_shutdown(std::forward<Handler>(handler));
}

} // websocket
} // beast
} // boost

namespace lt::aux {

namespace websocket = boost::beast::websocket;

using tcp = boost::asio::ip::tcp;

struct TORRENT_EXTRA_EXPORT websocket_stream
	: std::enable_shared_from_this<websocket_stream>
{
	using connect_handler = std::function<void(error_code const&)>;
	using read_handler = std::function<void(error_code const&, std::size_t)>;
	using write_handler = std::function<void(error_code const&, std::size_t)>;

	websocket_stream(io_context& ios
		, resolver_interface& resolver
		, ssl::context* ssl_ctx
	);

	~websocket_stream() = default;
	websocket_stream& operator=(websocket_stream const&) = delete;
	websocket_stream(websocket_stream const&) = delete;
	websocket_stream& operator=(websocket_stream&&) noexcept = delete;
	websocket_stream(websocket_stream&&) noexcept = delete;

	void close();
	void close(error_code const&) { close(); }
	close_reason_t get_close_reason();

	bool is_open() const { return m_open; }
	bool is_connecting() const { return bool(m_connect_handler); }

	void set_user_agent(std::string user_agent);

	template <class Handler>
	void async_connect(const std::string &url, Handler&& handler)
	{
		if (m_connect_handler)
		{
			post(m_io_service, std::bind(std::forward<Handler>(handler)
						, boost::asio::error::already_started));
			return;
		}

		m_connect_handler = std::forward<Handler>(handler);
		do_connect(url);
	}

	template <class Mutable_Buffer, class Handler>
	void async_read(Mutable_Buffer& buffer, Handler&& handler)
	{
		using namespace std::placeholders;

		if (!m_open)
		{
			post(m_io_service, std::bind(std::forward<Handler>(handler)
						, boost::asio::error::not_connected
						, std::size_t(0)));
			return;
		}

		ADD_OUTSTANDING_ASYNC("websocket_stream::on_read");
		std::visit([&](auto &stream)
		{
			stream.async_read(buffer, std::bind(&websocket_stream::on_read
					, shared_from_this()
					, _1, _2
					, read_handler(std::forward<Handler>(handler))));
		}
		, m_stream);
	}

	template <class Const_Buffer, class Handler>
	void async_write(Const_Buffer const& buffer, Handler&& handler)
	{
		using namespace std::placeholders;

		if (!m_open)
		{
			post(m_io_service, std::bind(std::forward<Handler>(handler)
						, boost::asio::error::not_connected
						, std::size_t(0)));
			return;
		}

		m_keepalive_timer.cancel();

		ADD_OUTSTANDING_ASYNC("websocket_stream::on_write");
		std::visit([&](auto &stream)
		{
			stream.async_write(buffer, std::bind(&websocket_stream::on_write
					, shared_from_this()
					, _1 , _2
					, write_handler(std::forward<Handler>(handler))));
		}
		, m_stream);
	}

private:
	void do_connect(std::string url);
	void do_resolve(std::string hostname, std::uint16_t port);
	void on_resolve(error_code const& ec, std::vector<address> const& addresses);
	void do_tcp_connect(std::vector<tcp::endpoint> endpoints);
	void on_tcp_connect(error_code const& ec);
	void do_ssl_handshake();
	void on_ssl_handshake(error_code const& ec);
	void do_handshake();
	void on_handshake(error_code const& ec);
	void on_read(error_code ec, std::size_t bytes_read, read_handler handler);
	void on_write(error_code ec, std::size_t bytes_written, write_handler handler);
	void on_close(error_code ec);
	void on_keepalive(error_code ec);
	void on_ping(error_code ec);
	void arm_keepalive();

	io_context& m_io_service;
	resolver_interface& m_resolver;
	ssl::context* m_ssl_context;

	using stream_type = websocket::stream<tcp::socket>;
	using ssl_stream_type = websocket::stream<ssl::stream<tcp::socket>>;
	std::variant<stream_type, ssl_stream_type> m_stream;

	std::string m_url;
	std::string m_hostname;
	std::uint16_t m_port;
	std::string m_target;
	std::string m_user_agent;
	std::vector<tcp::endpoint> m_endpoints;

	connect_handler m_connect_handler;

	bool m_open;
	deadline_timer m_keepalive_timer;
};

}

#endif // TORRENT_USE_RTC

#endif // TORRENT_WEBSOCKET_STREAM_HPP_INCLUDED
