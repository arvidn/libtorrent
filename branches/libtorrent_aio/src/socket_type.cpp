/*

Copyright (c) 2009, Arvid Norberg
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

#include "libtorrent/socket_type.hpp"

#ifdef TORRENT_USE_OPENSSL
#include <boost/asio/ssl/context.hpp>
#endif

namespace libtorrent
{

	bool is_ssl(socket_type const& s)
	{
#ifdef TORRENT_USE_OPENSSL
#define CASE(t) case socket_type_int_impl<ssl_stream<t> >::value:
		switch (s.type())
		{
			CASE(stream_socket)
			CASE(socks5_stream)
			CASE(http_stream)
			CASE(utp_stream)
				return true;
			default: return false;
		};
#undef CASE
#else
		return false;
#endif
	}

	void socket_type::destruct()
	{
		switch (m_type)
		{
			case 0: break;
			case socket_type_int_impl<stream_socket>::value:
				get<stream_socket>()->~stream_socket();
				break;
			case socket_type_int_impl<socks5_stream>::value:
				get<socks5_stream>()->~socks5_stream();
				break;
			case socket_type_int_impl<http_stream>::value:
				get<http_stream>()->~http_stream();
				break;
			case socket_type_int_impl<utp_stream>::value:
					  get<utp_stream>()->~utp_stream();
					  break;
#if TORRENT_USE_I2P
			case socket_type_int_impl<i2p_stream>::value:
				get<i2p_stream>()->~i2p_stream();
				break;
#endif
#ifdef TORRENT_USE_OPENSSL
			case socket_type_int_impl<ssl_stream<stream_socket> >::value:
				get<ssl_stream<stream_socket> >()->~ssl_stream();
				break;
			case socket_type_int_impl<ssl_stream<socks5_stream> >::value:
				get<ssl_stream<socks5_stream> >()->~ssl_stream();
				break;
			case socket_type_int_impl<ssl_stream<http_stream> >::value:
				get<ssl_stream<http_stream> >()->~ssl_stream();
				break;
			case socket_type_int_impl<ssl_stream<utp_stream> >::value:
				get<ssl_stream<utp_stream> >()->~ssl_stream();
				break;
#endif
			default: TORRENT_ASSERT(false);
		}
		m_type = 0;
	}

	void socket_type::construct(int type, void* userdata)
	{
		destruct();
		switch (type)
		{
			case 0: break;
			case socket_type_int_impl<stream_socket>::value:
				new ((stream_socket*)m_data) stream_socket(m_io_service);
				break;
			case socket_type_int_impl<socks5_stream>::value:
				new ((socks5_stream*)m_data) socks5_stream(m_io_service);
				break;
			case socket_type_int_impl<http_stream>::value:
				new ((http_stream*)m_data) http_stream(m_io_service);
				break;
			case socket_type_int_impl<utp_stream>::value:
				new ((utp_stream*)m_data) utp_stream(m_io_service);
				break;
#if TORRENT_USE_I2P
			case socket_type_int_impl<i2p_stream>::value:
				new ((i2p_stream*)m_data) i2p_stream(m_io_service);
				break;
#endif
#ifdef TORRENT_USE_OPENSSL
			case socket_type_int_impl<ssl_stream<stream_socket> >::value:
				TORRENT_ASSERT(userdata);
				new ((ssl_stream<stream_socket>*)m_data) ssl_stream<stream_socket>(m_io_service
					, *((boost::asio::ssl::context*)userdata));
				break;
			case socket_type_int_impl<ssl_stream<socks5_stream> >::value:
				TORRENT_ASSERT(userdata);
				new ((ssl_stream<socks5_stream>*)m_data) ssl_stream<socks5_stream>(m_io_service
					, *((boost::asio::ssl::context*)userdata));
				break;
			case socket_type_int_impl<ssl_stream<http_stream> >::value:
				TORRENT_ASSERT(userdata);
				new ((ssl_stream<http_stream>*)m_data) ssl_stream<http_stream>(m_io_service
					, *((boost::asio::ssl::context*)userdata));
				break;
			case socket_type_int_impl<ssl_stream<utp_stream> >::value:
				TORRENT_ASSERT(userdata);
				new ((ssl_stream<utp_stream>*)m_data) ssl_stream<utp_stream>(m_io_service
					, *((boost::asio::ssl::context*)userdata));
				break;
#endif
			default: TORRENT_ASSERT(false);
		}

		m_type = type;
	}

	char const* socket_type::type_name() const
	{
		static char const* const names[] =
		{
			"uninitialized",
			"TCP",
			"Socks5",
			"HTTP",
			"uTP",
#if TORRENT_USE_I2P
			"I2P",
#else
			"",
#endif
#ifdef TORRENT_USE_OPENSSL
			"SSL/TCP",
			"SSL/Socks5",
			"SSL/HTTP",
			"SSL/uTP"
#else
			"","","",""
#endif
		};
		return names[m_type];
	}

	io_service& socket_type::get_io_service() const
	{ return m_io_service; }

	socket_type::~socket_type()
	{ destruct(); }

	bool socket_type::is_open() const
	{
		if (m_type == 0) return false;
		TORRENT_SOCKTYPE_FORWARD_RET(is_open(), false)
	}

	void socket_type::open(protocol_type const& p, error_code& ec)
	{ TORRENT_SOCKTYPE_FORWARD(open(p, ec)) }

	void socket_type::close(error_code& ec)
	{
		if (m_type == 0) return;
		TORRENT_SOCKTYPE_FORWARD(close(ec))
	}

	socket_type::endpoint_type socket_type::local_endpoint(error_code& ec) const
	{ TORRENT_SOCKTYPE_FORWARD_RET(local_endpoint(ec), socket_type::endpoint_type()) }

	socket_type::endpoint_type socket_type::remote_endpoint(error_code& ec) const
	{ TORRENT_SOCKTYPE_FORWARD_RET(remote_endpoint(ec), socket_type::endpoint_type()) }

	void socket_type::bind(endpoint_type const& endpoint, error_code& ec)
	{ TORRENT_SOCKTYPE_FORWARD(bind(endpoint, ec)) }

	std::size_t socket_type::available(error_code& ec) const
	{ TORRENT_SOCKTYPE_FORWARD_RET(available(ec), 0) }

	int socket_type::type() const { return m_type; }

#ifndef BOOST_NO_EXCEPTIONS
	void socket_type::open(protocol_type const& p)
	{ TORRENT_SOCKTYPE_FORWARD(open(p)) }

	void socket_type::close()
	{
		if (m_type == 0) return;
		TORRENT_SOCKTYPE_FORWARD(close())
	}

	socket_type::endpoint_type socket_type::local_endpoint() const
	{ TORRENT_SOCKTYPE_FORWARD_RET(local_endpoint(), socket_type::endpoint_type()) }

	socket_type::endpoint_type socket_type::remote_endpoint() const
	{ TORRENT_SOCKTYPE_FORWARD_RET(remote_endpoint(), socket_type::endpoint_type()) }

	void socket_type::bind(endpoint_type const& endpoint)
	{ TORRENT_SOCKTYPE_FORWARD(bind(endpoint)) }

	std::size_t socket_type::available() const
	{ TORRENT_SOCKTYPE_FORWARD_RET(available(), 0) }
#endif

}

