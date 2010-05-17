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

#if TORRENT_USE_I2P

#define TORRENT_SOCKTYPE_FORWARD(x) \
	switch (m_type) { \
		case socket_type_int_impl<stream_socket>::value: \
			get<stream_socket>()->x; break; \
		case socket_type_int_impl<socks5_stream>::value: \
			get<socks5_stream>()->x; break; \
		case socket_type_int_impl<http_stream>::value: \
			get<http_stream>()->x; break; \
		case socket_type_int_impl<utp_stream>::value: \
			get<utp_stream>()->x; break; \
		case socket_type_int_impl<i2p_stream>::value: \
			get<i2p_stream>()->x; break; \
		default: TORRENT_ASSERT(false); \
	}

#define TORRENT_SOCKTYPE_FORWARD_RET(x, def) \
	switch (m_type) { \
		case socket_type_int_impl<stream_socket>::value: \
			return get<stream_socket>()->x; \
		case socket_type_int_impl<socks5_stream>::value: \
			return get<socks5_stream>()->x; \
		case socket_type_int_impl<http_stream>::value: \
			return get<http_stream>()->x; \
		case socket_type_int_impl<utp_stream>::value: \
			return get<utp_stream>()->x; \
		case socket_type_int_impl<i2p_stream>::value: \
			return get<i2p_stream>()->x; \
		default: TORRENT_ASSERT(false); return def; \
	}

#else // TORRENT_USE_I2P

#define TORRENT_SOCKTYPE_FORWARD(x) \
	switch (m_type) { \
		case socket_type_int_impl<stream_socket>::value: \
			get<stream_socket>()->x; break; \
		case socket_type_int_impl<socks5_stream>::value: \
			get<socks5_stream>()->x; break; \
		case socket_type_int_impl<http_stream>::value: \
			get<http_stream>()->x; break; \
		case socket_type_int_impl<utp_stream>::value: \
			get<utp_stream>()->x; break; \
		default: TORRENT_ASSERT(false); \
	}

#define TORRENT_SOCKTYPE_FORWARD_RET(x, def) \
	switch (m_type) { \
		case socket_type_int_impl<stream_socket>::value: \
			return get<stream_socket>()->x; \
		case socket_type_int_impl<socks5_stream>::value: \
			return get<socks5_stream>()->x; \
		case socket_type_int_impl<http_stream>::value: \
			return get<http_stream>()->x; \
		case socket_type_int_impl<utp_stream>::value: \
			return get<utp_stream>()->x; \
		default: TORRENT_ASSERT(false); return def; \
	}

#endif // TORRENT_USE_I2P

namespace libtorrent
{

	template <class S>
	struct socket_type_int_impl
	{ enum { value = 0 }; };

	template <>
	struct socket_type_int_impl<stream_socket>
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

	struct TORRENT_EXPORT socket_type
	{
		typedef stream_socket::endpoint_type endpoint_type;
		typedef stream_socket::protocol_type protocol_type;
		typedef socket_type lowest_layer_type;
	
		explicit socket_type(io_service& ios): m_io_service(ios), m_type(0) {}
		~socket_type();

		io_service& get_io_service() const;
		bool is_open() const;

		lowest_layer_type& lowest_layer() { return *this; }


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
		endpoint_type local_endpoint(error_code& ec) const;
		endpoint_type remote_endpoint(error_code& ec) const;
		void bind(endpoint_type const& endpoint, error_code& ec);
		std::size_t available(error_code& ec) const;


		template <class Mutable_Buffers>
		std::size_t read_some(Mutable_Buffers const& buffers, error_code& ec)
		{ TORRENT_SOCKTYPE_FORWARD_RET(read_some(buffers, ec), 0) }

		template <class Mutable_Buffers, class Handler>
		void async_read_some(Mutable_Buffers const& buffers, Handler const& handler)
		{ TORRENT_SOCKTYPE_FORWARD(async_read_some(buffers, handler)) }

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

		template <class S>
		void instantiate(io_service& ios)
		{
			TORRENT_ASSERT(&ios == &m_io_service);
			construct(socket_type_int_impl<S>::value);
		}

		template <class S> S* get()
		{
			if (m_type !=  socket_type_int_impl<S>::value) return 0;
			return (S*)m_data;
		}

		template <class S> S const* get() const 
		{
			if (m_type !=  socket_type_int_impl<S>::value) return 0;
			return (S const*)m_data;
		}

	private:

		void destruct();
		void construct(int type);

		io_service& m_io_service;
		int m_type;
#if TORRENT_USE_I2P
		enum { storage_size = max5<sizeof(stream_socket)
			, sizeof(socks5_stream), sizeof(http_stream)
			, sizeof(i2p_stream), sizeof(utp_stream)>::value };
#else
		enum { storage_size = max4<sizeof(stream_socket)
			, sizeof(socks5_stream), sizeof(http_stream)
			, sizeof(utp_stream)>::value };
#endif
		size_type m_data[(storage_size + sizeof(size_type) - 1) / sizeof(size_type)];
	};
}

#endif

