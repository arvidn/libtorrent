/*

Copyright (c) 2007-2011, 2013-2015, 2017-2020, Arvid Norberg
Copyright (c) 2016, 2019, Alden Torres
Copyright (c) 2017, Jan Berkel
Copyright (c) 2020, Paul-Louis Ageneau
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

#ifndef TORRENT_PROXY_BASE_HPP_INCLUDED
#define TORRENT_PROXY_BASE_HPP_INCLUDED

#include "libtorrent/io.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/noexcept_movable.hpp"

namespace libtorrent {

struct proxy_base
{
	using next_layer_type = tcp::socket;
	using lowest_layer_type = tcp::socket::lowest_layer_type;
	using endpoint_type = tcp::socket::endpoint_type;
	using protocol_type = tcp::socket::protocol_type;

	explicit proxy_base(io_context& io_context);
	~proxy_base();
	proxy_base(proxy_base&&) noexcept = default;
	proxy_base& operator=(proxy_base&&) = default;
	proxy_base(proxy_base const&) = delete;
	proxy_base& operator=(proxy_base const&) = delete;

	void set_proxy(std::string hostname, int port)
	{
		m_hostname = std::move(hostname);
		m_port = port;
	}

	using executor_type = tcp::socket::executor_type;
	executor_type get_executor() { return m_sock.get_executor(); }

	template <class Mutable_Buffers, class Handler>
	void async_read_some(Mutable_Buffers const& buffers, Handler handler)
	{
		m_sock.async_read_some(buffers, std::move(handler));
	}

	template <class Mutable_Buffers>
	std::size_t read_some(Mutable_Buffers const& buffers, error_code& ec)
	{
		return m_sock.read_some(buffers, ec);
	}

	template <class Const_Buffers>
	std::size_t write_some(Const_Buffers const& buffers, error_code& ec)
	{
		return m_sock.write_some(buffers, ec);
	}

	std::size_t available(error_code& ec) const
	{ return m_sock.available(ec); }

#ifndef BOOST_NO_EXCEPTIONS
	std::size_t available() const
	{ return m_sock.available(); }

	template <class Mutable_Buffers>
	std::size_t read_some(Mutable_Buffers const& buffers)
	{
		return m_sock.read_some(buffers);
	}

	template <class Const_Buffers>
	std::size_t write_some(Const_Buffers const& buffers)
	{
		return m_sock.write_some(buffers);
	}

	template <class IO_Control_Command>
	void io_control(IO_Control_Command& ioc)
	{
		m_sock.io_control(ioc);
	}
#endif

	template <class IO_Control_Command>
	void io_control(IO_Control_Command& ioc, error_code& ec)
	{
		m_sock.io_control(ioc, ec);
	}

	template <class Const_Buffers, class Handler>
	void async_write_some(Const_Buffers const& buffers, Handler handler)
	{
		m_sock.async_write_some(buffers, std::move(handler));
	}

#if BOOST_VERSION >= 106600 && !defined TORRENT_BUILD_SIMULATOR
	// Compatiblity with the async_wait method introduced in boost 1.66

	static constexpr auto wait_read = tcp::socket::wait_read;
	static constexpr auto wait_write = tcp::socket::wait_write;
	static constexpr auto wait_error = tcp::socket::wait_error;

	template <class Handler>
	void async_wait(tcp::socket::wait_type type, Handler handler)
	{
		m_sock.async_wait(type, std::move(handler));
	}
#endif

#ifndef BOOST_NO_EXCEPTIONS
	void non_blocking(bool b)
	{
		m_sock.non_blocking(b);
	}
#endif

	void non_blocking(bool b, error_code& ec)
	{
		m_sock.non_blocking(b, ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	template <class SettableSocketOption>
	void set_option(SettableSocketOption const& opt)
	{
		m_sock.set_option(opt);
	}
#endif

	template <class SettableSocketOption>
	void set_option(SettableSocketOption const& opt, error_code& ec)
	{
		m_sock.set_option(opt, ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	template <class GettableSocketOption>
	void get_option(GettableSocketOption& opt)
	{
		m_sock.get_option(opt);
	}
#endif

	template <class GettableSocketOption>
	void get_option(GettableSocketOption& opt, error_code& ec)
	{
		m_sock.get_option(opt, ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	void bind(endpoint_type const& /* endpoint */)
	{
//		m_sock.bind(endpoint);
	}
#endif

	void cancel()
	{
		m_sock.cancel();
	}

	void cancel(error_code& ec)
	{
		m_sock.cancel(ec);
	}

	void bind(endpoint_type const& /* endpoint */, error_code& /* ec */)
	{
		// the reason why we ignore binds here is because we don't
		// (necessarily) yet know what address family the proxy
		// will resolve to, and binding to the wrong one would
		// break our connection attempt later. The caller here
		// doesn't necessarily know that we're proxying, so this
		// bind address is based on the final endpoint, not the
		// proxy.
		// TODO: it would be nice to remember the bind port and bind once we know where the proxy is
//		m_sock.bind(endpoint, ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	void open(protocol_type const&)
	{
//		m_sock.open(p);
	}
#endif

	void open(protocol_type const&, error_code&)
	{
		// we need to ignore this for the same reason as stated
		// for ignoring bind()
//		m_sock.open(p, ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	void close()
	{
		m_remote_endpoint = endpoint_type();
		m_sock.close();
		m_resolver.cancel();
	}
#endif

	void close(error_code& ec)
	{
		m_remote_endpoint = endpoint_type();
		m_sock.close(ec);
		m_resolver.cancel();
	}

#ifndef BOOST_NO_EXCEPTIONS
	endpoint_type remote_endpoint() const
	{
		return m_remote_endpoint;
	}
#endif

	endpoint_type remote_endpoint(error_code& ec) const
	{
		if (!m_sock.is_open()) ec = boost::asio::error::not_connected;
		return m_remote_endpoint;
	}

#ifndef BOOST_NO_EXCEPTIONS
	endpoint_type local_endpoint() const
	{
		return m_sock.local_endpoint();
	}
#endif

	endpoint_type local_endpoint(error_code& ec) const
	{
		return m_sock.local_endpoint(ec);
	}

	lowest_layer_type& lowest_layer()
	{
		return m_sock.lowest_layer();
	}

	next_layer_type& next_layer()
	{
		return m_sock;
	}

	bool is_open() const { return m_sock.is_open(); }

protected:

	// The handler must be taken as lvalue reference here since we may not call
	// it. But if we do, we want the call operator to own the function object.
	template <typename Handler>
	bool handle_error(error_code const& e, Handler&& h)
	{
		if (!e) return false;
		std::forward<Handler>(h)(e);
		error_code ec;
		close(ec);
		return true;
	}

	aux::noexcept_movable<tcp::socket> m_sock;
	std::string m_hostname; // proxy host
	int m_port;             // proxy port

	aux::noexcept_movable<endpoint_type> m_remote_endpoint;

	// TODO: 2 use the resolver interface that has a built-in cache
	aux::noexcept_move_only<tcp::resolver> m_resolver;
};

template <typename Handler, typename UnderlyingHandler>
struct wrap_allocator_t
{
	wrap_allocator_t(Handler h, UnderlyingHandler uh)
		: m_handler(std::move(h))
		, m_underlying_handler(std::move(uh))
	{}

	wrap_allocator_t(wrap_allocator_t const&) = default;
	wrap_allocator_t(wrap_allocator_t&&) = default;

	template <class... A>
	void operator()(A&&... a)
	{
		m_handler(std::forward<A>(a)..., std::move(m_underlying_handler));
	}

	using allocator_type = typename boost::asio::associated_allocator<UnderlyingHandler>::type;
	using executor_type = typename boost::asio::associated_executor<UnderlyingHandler>::type;

	allocator_type get_allocator() const noexcept
	{ return boost::asio::get_associated_allocator(m_underlying_handler); }

	executor_type get_executor() const noexcept
	{
		return boost::asio::get_associated_executor(m_underlying_handler);
	}

private:
	Handler m_handler;
	UnderlyingHandler m_underlying_handler;
};

template <typename Handler, typename UnderlyingHandler>
wrap_allocator_t<Handler, UnderlyingHandler> wrap_allocator(Handler h, UnderlyingHandler u)
{
	return wrap_allocator_t<Handler, UnderlyingHandler>{std::move(h), std::move(u)};
}


}

#endif
