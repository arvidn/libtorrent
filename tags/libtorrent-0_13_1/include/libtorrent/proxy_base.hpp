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

#ifndef TORRENT_PROXY_BASE_HPP_INCLUDED
#define TORRENT_PROXY_BASE_HPP_INCLUDED

#include "libtorrent/io.hpp"
#include "libtorrent/socket.hpp"
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/function.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>


namespace libtorrent {

class proxy_base : boost::noncopyable
{
public:

	typedef stream_socket::lowest_layer_type lowest_layer_type;
	typedef stream_socket::endpoint_type endpoint_type;
	typedef stream_socket::protocol_type protocol_type;

	explicit proxy_base(asio::io_service& io_service)
		: m_sock(io_service)
		, m_resolver(io_service)
	{}

	void set_proxy(std::string hostname, int port)
	{
		m_hostname = hostname;
		m_port = port;
	}

	template <class Mutable_Buffers, class Handler>
	void async_read_some(Mutable_Buffers const& buffers, Handler const& handler)
	{
		m_sock.async_read_some(buffers, handler);
	}

	template <class Mutable_Buffers>
	std::size_t read_some(Mutable_Buffers const& buffers, asio::error_code& ec)
	{
		return m_sock.read_some(buffers, ec);
	}

	template <class Mutable_Buffers>
	std::size_t read_some(Mutable_Buffers const& buffers)
	{
		return m_sock.read_some(buffers);
	}

	template <class IO_Control_Command>
	void io_control(IO_Control_Command& ioc)
	{
		m_sock.io_control(ioc);
	}

	template <class IO_Control_Command>
	void io_control(IO_Control_Command& ioc, asio::error_code& ec)
	{
		m_sock.io_control(ioc, ec);
	}

	template <class Const_Buffers, class Handler>
	void async_write_some(Const_Buffers const& buffers, Handler const& handler)
	{
		m_sock.async_write_some(buffers, handler);
	}

	void bind(endpoint_type const& endpoint)
	{
		m_sock.bind(endpoint);
	}

	void bind(endpoint_type const& endpoint, asio::error_code& ec)
	{
		m_sock.bind(endpoint, ec);
	}

	void open(protocol_type const& p)
	{
		m_sock.open(p);
	}

	void open(protocol_type const& p, asio::error_code& ec)
	{
		m_sock.open(p, ec);
	}

	void close()
	{
		m_remote_endpoint = endpoint_type();
		m_sock.close();
		m_resolver.cancel();
	}

	void close(asio::error_code& ec)
	{
		m_sock.close(ec);
		m_resolver.cancel();
	}

	endpoint_type remote_endpoint()
	{
		return m_remote_endpoint;
	}

	endpoint_type remote_endpoint(asio::error_code& ec)
	{
		return m_remote_endpoint;
	}

	endpoint_type local_endpoint()
	{
		return m_sock.local_endpoint();
	}

	endpoint_type local_endpoint(asio::error_code& ec)
	{
		return m_sock.local_endpoint(ec);
	}

	asio::io_service& io_service()
	{
		return m_sock.io_service();
	}

	lowest_layer_type& lowest_layer()
	{
		return m_sock.lowest_layer();
	}
	
protected:

	stream_socket m_sock;
	std::string m_hostname;
	int m_port;

	endpoint_type m_remote_endpoint;

	tcp::resolver m_resolver;
};

}

#endif

