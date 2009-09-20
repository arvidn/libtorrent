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
#if BOOST_VERSION < 103500
#include <asio/read.hpp>
#include <asio/write.hpp>
#else
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#endif

namespace libtorrent {

class proxy_base : boost::noncopyable
{
public:

	typedef stream_socket::lowest_layer_type lowest_layer_type;
	typedef stream_socket::endpoint_type endpoint_type;
	typedef stream_socket::protocol_type protocol_type;

	explicit proxy_base(io_service& io_service)
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
		m_sock.io_control(ioc);
	}
#endif

	template <class IO_Control_Command>
	void io_control(IO_Control_Command& ioc, error_code& ec)
	{
		m_sock.io_control(ioc, ec);
	}

	template <class Const_Buffers, class Handler>
	void async_write_some(Const_Buffers const& buffers, Handler const& handler)
	{
		m_sock.async_write_some(buffers, handler);
	}

#ifndef BOOST_NO_EXCEPTIONS
	template <class SettableSocketOption>
	void set_option(SettableSocketOption const& opt)
	{
		m_sock.set_option(opt);
	}
#endif

	template <class SettableSocketOption>
	error_code set_option(SettableSocketOption const& opt, error_code& ec)
	{
		return m_sock.set_option(opt, ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	void bind(endpoint_type const& endpoint)
	{
		m_sock.bind(endpoint);
	}
#endif

	void bind(endpoint_type const& endpoint, error_code& ec)
	{
		m_sock.bind(endpoint, ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	void open(protocol_type const& p)
	{
		m_sock.open(p);
	}
#endif

	void open(protocol_type const& p, error_code& ec)
	{
		m_sock.open(p, ec);
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

	io_service& get_io_service()
	{
		return m_sock.get_io_service();
	}

	lowest_layer_type& lowest_layer()
	{
		return m_sock.lowest_layer();
	}

	bool is_open() const { return m_sock.is_open(); }
	
protected:

	stream_socket m_sock;
	std::string m_hostname;
	int m_port;

	endpoint_type m_remote_endpoint;

	tcp::resolver m_resolver;
};

}

#endif

