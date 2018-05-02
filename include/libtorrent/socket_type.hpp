/*

Copyright (c) 2009-2018, Arvid Norberg
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

#ifndef TORRENT_SOCKET_TYPE
#define TORRENT_SOCKET_TYPE

#include "libtorrent/config.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/socks5_stream.hpp"
#include "libtorrent/http_stream.hpp"
#include "libtorrent/i2p_stream.hpp"
#include "libtorrent/utp_stream.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/max.hpp"
#include "libtorrent/assert.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/type_traits/aligned_storage.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#ifdef TORRENT_USE_OPENSSL
#include "libtorrent/ssl_stream.hpp"
#endif

#if defined TORRENT_ASIO_DEBUGGING
#include "libtorrent/debug.hpp"
#endif

#if defined TORRENT_OS2 && defined ioc
#undef ioc
#endif

#if TORRENT_USE_I2P

#define TORRENT_SOCKTYPE_I2P_FORWARD(x) \
		case socket_type_int_impl<i2p_stream>::value: \
			get<i2p_stream>()->x; break;

#define TORRENT_SOCKTYPE_I2P_FORWARD_RET(x, def) \
		case socket_type_int_impl<i2p_stream>::value: \
			return get<i2p_stream>()->x;

#else // TORRENT_USE_I2P

#define TORRENT_SOCKTYPE_I2P_FORWARD(x)
#define TORRENT_SOCKTYPE_I2P_FORWARD_RET(x, def)

#endif

#ifdef TORRENT_USE_OPENSSL

#define TORRENT_SOCKTYPE_SSL_FORWARD(x) \
		case socket_type_int_impl<ssl_stream<tcp::socket> >::value: \
			get<ssl_stream<tcp::socket> >()->x; break; \
		case socket_type_int_impl<ssl_stream<socks5_stream> >::value: \
			get<ssl_stream<socks5_stream> >()->x; break; \
		case socket_type_int_impl<ssl_stream<http_stream> >::value: \
			get<ssl_stream<http_stream> >()->x; break; \
		case socket_type_int_impl<ssl_stream<utp_stream> >::value: \
			get<ssl_stream<utp_stream> >()->x; break;

#define TORRENT_SOCKTYPE_SSL_FORWARD_RET(x, def) \
		case socket_type_int_impl<ssl_stream<tcp::socket> >::value: \
			return get<ssl_stream<tcp::socket> >()->x; \
		case socket_type_int_impl<ssl_stream<socks5_stream> >::value: \
			return get<ssl_stream<socks5_stream> >()->x; \
		case socket_type_int_impl<ssl_stream<http_stream> >::value: \
			return get<ssl_stream<http_stream> >()->x; \
		case socket_type_int_impl<ssl_stream<utp_stream> >::value: \
			return get<ssl_stream<utp_stream> >()->x;

#else

#define TORRENT_SOCKTYPE_SSL_FORWARD(x)
#define TORRENT_SOCKTYPE_SSL_FORWARD_RET(x, def)

#endif

#define TORRENT_SOCKTYPE_FORWARD(x) \
	switch (m_type) { \
		case socket_type_int_impl<tcp::socket>::value: \
			get<tcp::socket>()->x; break; \
		case socket_type_int_impl<socks5_stream>::value: \
			get<socks5_stream>()->x; break; \
		case socket_type_int_impl<http_stream>::value: \
			get<http_stream>()->x; break; \
		case socket_type_int_impl<utp_stream>::value: \
			get<utp_stream>()->x; break; \
		TORRENT_SOCKTYPE_I2P_FORWARD(x) \
		TORRENT_SOCKTYPE_SSL_FORWARD(x) \
		default: TORRENT_ASSERT(false); \
	}

#define TORRENT_SOCKTYPE_FORWARD_RET(x, def) \
	switch (m_type) { \
		case socket_type_int_impl<tcp::socket>::value: \
			return get<tcp::socket>()->x; \
		case socket_type_int_impl<socks5_stream>::value: \
			return get<socks5_stream>()->x; \
		case socket_type_int_impl<http_stream>::value: \
			return get<http_stream>()->x; \
		case socket_type_int_impl<utp_stream>::value: \
			return get<utp_stream>()->x; \
		TORRENT_SOCKTYPE_I2P_FORWARD_RET(x, def) \
		TORRENT_SOCKTYPE_SSL_FORWARD_RET(x, def) \
		default: TORRENT_ASSERT(false); return def; \
	}

namespace libtorrent
{

	template <class S>
	struct socket_type_int_impl
	{ enum { value = 0 }; };

	template <>
	struct socket_type_int_impl<tcp::socket>
	{ enum { value = 1 }; };

	template <>
	struct socket_type_int_impl<socks5_stream>
	{ enum { value = 2 }; };

	template <>
	struct socket_type_int_impl<http_stream>
	{ enum { value = 3 }; };

	template <>
	struct socket_type_int_impl<utp_stream>
	{ enum { value = 4 }; };

#if TORRENT_USE_I2P
	template <>
	struct socket_type_int_impl<i2p_stream>
	{ enum { value = 5 }; };
#endif

#ifdef TORRENT_USE_OPENSSL
	template <>
	struct socket_type_int_impl<ssl_stream<tcp::socket> >
	{ enum { value = 6 }; };

	template <>
	struct socket_type_int_impl<ssl_stream<socks5_stream> >
	{ enum { value = 7 }; };

	template <>
	struct socket_type_int_impl<ssl_stream<http_stream> >
	{ enum { value = 8 }; };

	template <>
	struct socket_type_int_impl<ssl_stream<utp_stream> >
	{ enum { value = 9 }; };
#endif

	struct TORRENT_EXTRA_EXPORT socket_type
	{
		typedef tcp::socket::endpoint_type endpoint_type;
		typedef tcp::socket::protocol_type protocol_type;

		typedef tcp::socket::receive_buffer_size receive_buffer_size;
		typedef tcp::socket::send_buffer_size send_buffer_size;

		explicit socket_type(io_service& ios): m_io_service(ios), m_type(0) {}
		~socket_type();

		io_service& get_io_service() const;
		bool is_open() const;

		char const* type_name() const;

#ifndef BOOST_NO_EXCEPTIONS
		void open(protocol_type const& p);
		void close();
		endpoint_type local_endpoint() const;
		endpoint_type remote_endpoint() const;
		void bind(endpoint_type const& endpoint);
		std::size_t available() const;
#endif

		void open(protocol_type const& p, error_code& ec);
		void close(error_code& ec);

		// this is only relevant for uTP connections
		void set_close_reason(boost::uint16_t code);
		boost::uint16_t get_close_reason();

		endpoint_type local_endpoint(error_code& ec) const;
		endpoint_type remote_endpoint(error_code& ec) const;
		void bind(endpoint_type const& endpoint, error_code& ec);
		std::size_t available(error_code& ec) const;
		int type() const;


		template <class Mutable_Buffers>
		std::size_t read_some(Mutable_Buffers const& buffers, error_code& ec)
		{ TORRENT_SOCKTYPE_FORWARD_RET(read_some(buffers, ec), 0) }

		template <class Mutable_Buffers, class Handler>
		void async_read_some(Mutable_Buffers const& buffers, Handler const& handler)
		{ TORRENT_SOCKTYPE_FORWARD(async_read_some(buffers, handler)) }

		template <class Const_Buffers>
		std::size_t write_some(Const_Buffers const& buffers, error_code& ec)
		{ TORRENT_SOCKTYPE_FORWARD_RET(write_some(buffers, ec), 0) }

		template <class Const_Buffers, class Handler>
		void async_write_some(Const_Buffers const& buffers, Handler const& handler)
		{ TORRENT_SOCKTYPE_FORWARD(async_write_some(buffers, handler)) }

		template <class Handler>
		void async_connect(endpoint_type const& endpoint, Handler const& handler)
		{ TORRENT_SOCKTYPE_FORWARD(async_connect(endpoint, handler)) }

#ifndef BOOST_NO_EXCEPTIONS
		template <class IO_Control_Command>
		void io_control(IO_Control_Command& ioc)
		{ TORRENT_SOCKTYPE_FORWARD(io_control(ioc)) }

		template <class Mutable_Buffers>
		std::size_t read_some(Mutable_Buffers const& buffers)
		{ TORRENT_SOCKTYPE_FORWARD_RET(read_some(buffers), 0) }
#endif

		template <class IO_Control_Command>
		void io_control(IO_Control_Command& ioc, error_code& ec)
		{ TORRENT_SOCKTYPE_FORWARD(io_control(ioc, ec)) }

#ifndef BOOST_NO_EXCEPTIONS
		template <class SettableSocketOption>
		void set_option(SettableSocketOption const& opt)
		{ TORRENT_SOCKTYPE_FORWARD(set_option(opt)) }
#endif

		template <class SettableSocketOption>
		error_code set_option(SettableSocketOption const& opt, error_code& ec)
		{ TORRENT_SOCKTYPE_FORWARD_RET(set_option(opt, ec), ec) }

		void non_blocking(bool b, error_code& ec)
		{ TORRENT_SOCKTYPE_FORWARD(non_blocking(b, ec)) }

#ifndef BOOST_NO_EXCEPTIONS
		void non_blocking(bool b)
		{ TORRENT_SOCKTYPE_FORWARD(non_blocking(b)) }
#endif

#ifndef BOOST_NO_EXCEPTIONS
		template <class GettableSocketOption>
		void get_option(GettableSocketOption& opt)
		{ TORRENT_SOCKTYPE_FORWARD(get_option(opt)) }
#endif

		template <class GettableSocketOption>
		error_code get_option(GettableSocketOption& opt, error_code& ec)
		{ TORRENT_SOCKTYPE_FORWARD_RET(get_option(opt, ec), ec) }

		template <class S>
		void instantiate(io_service& ios, void* userdata = 0)
		{
			TORRENT_UNUSED(ios);
			TORRENT_ASSERT(&ios == &m_io_service);
			construct(socket_type_int_impl<S>::value, userdata);
		}

		template <class S> S* get()
		{
			if (m_type != socket_type_int_impl<S>::value) return 0;
			return reinterpret_cast<S*>(&m_data);
		}

		template <class S> S const* get() const
		{
			if (m_type != socket_type_int_impl<S>::value) return 0;
			return reinterpret_cast<S const*>(&m_data);
		}

	private:
		// explicitly disallow assignment, to silence msvc warning
		socket_type& operator=(socket_type const&);

		void destruct();
		void construct(int type, void* userdata);

		io_service& m_io_service;
		int m_type;
		enum { storage_size = max9<
			sizeof(tcp::socket)
			, sizeof(socks5_stream)
			, sizeof(http_stream)
			, sizeof(utp_stream)
#if TORRENT_USE_I2P
			, sizeof(i2p_stream)
#else
			, 0
#endif
#ifdef TORRENT_USE_OPENSSL
			, sizeof(ssl_stream<tcp::socket>)
			, sizeof(ssl_stream<socks5_stream>)
			, sizeof(ssl_stream<http_stream>)
			, sizeof(ssl_stream<utp_stream>)
#else
			, 0, 0, 0, 0
#endif
			>::value
		};

		boost::aligned_storage<storage_size, 8>::type m_data;
	};

	// returns true if this socket is an SSL socket
	bool is_ssl(socket_type const& s);

	// returns true if this is a uTP socket
	bool is_utp(socket_type const& s);

#if TORRENT_USE_I2P
	// returns true if this is an i2p socket
	bool is_i2p(socket_type const& s);
#endif

	// assuming the socket_type s is an ssl socket, make sure it
	// verifies the hostname in its SSL handshake
	void setup_ssl_hostname(socket_type& s, std::string const& hostname, error_code& ec);

	// properly shuts down SSL sockets. holder keeps s alive
	void async_shutdown(socket_type& s, boost::shared_ptr<void> holder);
}

#endif

