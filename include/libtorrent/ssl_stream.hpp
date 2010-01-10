/*

Copyright (c) 2008, Arvid Norberg
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

#ifndef TORRENT_SSL_STREAM_HPP_INCLUDED
#define TORRENT_SSL_STREAM_HPP_INCLUDED

#include "libtorrent/socket.hpp"
#if BOOST_VERSION < 103500
#include <asio/ssl.hpp>
#else
#include <boost/asio/ssl.hpp>
#endif
// openssl seems to believe it owns
// this name in every single scope
#undef set_key

namespace libtorrent {

template <class Stream>
class ssl_stream
{
public:

	explicit ssl_stream(io_service& io_service)
		: m_context(io_service, asio::ssl::context::sslv23_client)
		, m_sock(io_service, m_context)
	{
		error_code ec;
		m_context.set_verify_mode(asio::ssl::context::verify_none, ec);
	}

	typedef Stream next_layer_type;
	typedef typename Stream::lowest_layer_type lowest_layer_type;
	typedef typename Stream::endpoint_type endpoint_type;
	typedef typename Stream::protocol_type protocol_type;
	typedef typename asio::ssl::stream<Stream> sock_type;

	typedef boost::function<void(error_code const&)> handler_type;

	template <class Handler>
	void async_connect(endpoint_type const& endpoint, Handler const& handler)
	{
		// the connect is split up in the following steps:
		// 1. connect to peer
		// 2. perform SSL client handshake

		// to avoid unnecessary copying of the handler,
		// store it in a shared_ptr
		boost::shared_ptr<handler_type> h(new handler_type(handler));

		m_sock.next_layer().async_connect(endpoint
			, boost::bind(&ssl_stream::connected, this, _1, h));
	}

	template <class Mutable_Buffers, class Handler>
	void async_read_some(Mutable_Buffers const& buffers, Handler const& handler)
	{
		m_sock.async_read_some(buffers, handler);
	}

	template <class Mutable_Buffers>
	std::size_t read_some(Mutable_Buffers const& buffers, error_code& ec)
	{
		return m_sock.read_some(buffers, ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	template <class Mutable_Buffers>
	std::size_t read_some(Mutable_Buffers const& buffers)
	{
		return m_sock.read_some(buffers);
	}

	template <class IO_Control_Command>
	void io_control(IO_Control_Command& ioc)
	{
		m_sock.next_layer().io_control(ioc);
	}
#endif

	template <class IO_Control_Command>
	void io_control(IO_Control_Command& ioc, error_code& ec)
	{
		m_sock.next_layer().io_control(ioc, ec);
	}

	template <class Const_Buffers, class Handler>
	void async_write_some(Const_Buffers const& buffers, Handler const& handler)
	{
		m_sock.async_write_some(buffers, handler);
	}

#ifndef BOOST_NO_EXCEPTIONS
	void bind(endpoint_type const& endpoint)
	{
		m_sock.next_layer().bind(endpoint);
	}
#endif

	void bind(endpoint_type const& endpoint, error_code& ec)
	{
		m_sock.next_layer().bind(endpoint, ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	void open(protocol_type const& p)
	{
		m_sock.next_layer().open(p);
	}
#endif

	void open(protocol_type const& p, error_code& ec)
	{
		m_sock.next_layer().open(p, ec);
	}

	bool is_open() const
	{
		return const_cast<sock_type&>(m_sock).next_layer().is_open();
	}

#ifndef BOOST_NO_EXCEPTIONS
	void close()
	{
		m_sock.next_layer().close();
	}
#endif

	void close(error_code& ec)
	{
		m_sock.next_layer().close(ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	endpoint_type remote_endpoint() const
	{
		return const_cast<sock_type&>(m_sock).next_layer().remote_endpoint();
	}
#endif

	endpoint_type remote_endpoint(error_code& ec) const
	{
		return const_cast<sock_type&>(m_sock).next_layer().remote_endpoint(ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	endpoint_type local_endpoint() const
	{
		return const_cast<sock_type&>(m_sock).next_layer().local_endpoint();
	}
#endif

	endpoint_type local_endpoint(error_code& ec) const
	{
		return const_cast<sock_type&>(m_sock).next_layer().local_endpoint(ec);
	}

	io_service& get_io_service()
	{
		return m_sock.get_io_service();
	}

	lowest_layer_type& lowest_layer()
	{
		return m_sock.lowest_layer();
	}
	
	next_layer_type& next_layer()
	{
		return m_sock.next_layer();
	}

private:

	void connected(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			return;
		}

		m_sock.async_handshake(asio::ssl::stream_base::client
			, boost::bind(&ssl_stream::handshake, this, _1, h));
	}

	void handshake(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		(*h)(e);
	}

	asio::ssl::context m_context;
	asio::ssl::stream<Stream> m_sock;
};

}

#endif

