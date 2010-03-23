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

#ifndef TORRENT_UTP_STREAM_HPP_INCLUDED
#define TORRENT_UTP_STREAM_HPP_INCLUDED

#include "libtorrent/connection_queue.hpp"
#include "libtorrent/proxy_base.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/packet_buffer.hpp"
#include "libtorrent/error_code.hpp"

#include <boost/bind.hpp>
#include <boost/function/function1.hpp>
#include <boost/function/function2.hpp>

#define CCONTROL_TARGET 100

namespace libtorrent
{
	struct utp_socket_manager;

	template <class T> struct big_endian_int
	{
		big_endian_int& operator=(T v)
		{
			char* p = m_storage;
			detail::write_impl(v, p);
			return *this;
		}
		operator T() const
		{
			const char* p = m_storage;
			return detail::read_impl(p, detail::type<T>());
		}
	private:
		char m_storage[sizeof(T)];
	};

	typedef big_endian_int<boost::uint64_t> be_uint64;
	typedef big_endian_int<boost::uint32_t> be_uint32;
	typedef big_endian_int<boost::uint16_t> be_uint16;
	typedef big_endian_int<boost::int64_t> be_int64;
	typedef big_endian_int<boost::int32_t> be_int32;
	typedef big_endian_int<boost::int16_t> be_int16;

/*
	uTP header from BEP 29

	0       4       8               16              24              32
	+-------+-------+---------------+---------------+---------------+
	| ver   | type  | extension     | connection_id                 |
	+-------+-------+---------------+---------------+---------------+
	| timestamp_microseconds                                        |
	+---------------+---------------+---------------+---------------+
	| timestamp_difference_microseconds                             |
	+---------------+---------------+---------------+---------------+
	| wnd_size                                                      |
	+---------------+---------------+---------------+---------------+
	| seq_nr                        | ack_nr                        |
	+---------------+---------------+---------------+---------------+

*/

	enum type { ST_DATA = 0, ST_FIN, ST_STATE, ST_RESET, ST_SYN, NUM_TYPES };

	struct utp_header
	{
		unsigned char ver:4;
		unsigned char type:4;
		unsigned char extension;
		be_uint16 connection_id;
		be_uint32 timestamp_microseconds;
		be_uint32 timestamp_difference_microseconds;
		be_uint32 wnd_size;
		be_uint16 seq_nr;
		be_uint16 ack_nr;
	};

	namespace aux {

	template <class Mutable_Buffers>
	inline std::size_t copy_to_buffers(char const* linear, std::size_t size
		, Mutable_Buffers const& buffers)
	{
		std::size_t copied = 0;
		for (typename Mutable_Buffers::const_iterator i = buffers.begin();
			i != buffers.end(); ++i)
		{
			using asio::buffer_cast;
			using asio::buffer_size;
			int to_copy = (std::min)(buffer_size(*i), size - copied);
			if (to_copy == 0) break;
			std::memcpy(buffer_cast<char*>(*i), linear + copied, to_copy);
			copied += to_copy;
		}
		return copied;
	}

	template <class Const_Buffers>
	inline std::size_t copy_from_buffers(char* linear, std::size_t size
		, Const_Buffers const& buffers, std::size_t skip = 0)
	{
		std::size_t copied = 0;
		for (typename Const_Buffers::const_iterator i = buffers.begin();
			i != buffers.end(); ++i)
		{
			using asio::buffer_cast;
			using asio::buffer_size;
			if (skip > 0 && skip >= buffer_size(*i))
			{
				skip -= buffer_size(*i);
				continue;
			}
			int to_copy = (std::min)(buffer_size(*i) - skip, size - copied);
			if (to_copy == 0) break;
			std::memcpy(linear + copied, buffer_cast<char const*>(*i) + skip, to_copy);
			copied += to_copy;
			skip = 0;
		}
		return copied;
	}

	template <class Const_Buffers>
	inline std::size_t buffers_size(Const_Buffers const& buffers)
	{
		std::size_t ret = 0;
		for (typename Const_Buffers::const_iterator i = buffers.begin();
			i != buffers.end(); ++i)
			ret += buffer_size(*i);
		return ret;
	}

} // namespace aux

struct utp_socket_impl;

utp_socket_impl* construct_utp_impl(void* userdata, boost::uint16_t id);
void delete_utp_impl(utp_socket_impl* s);
bool should_delete(utp_socket_impl* s);
void tick_utp_impl(utp_socket_impl* s, ptime const& now);
bool utp_incoming_packet(utp_socket_impl* s, char const* p, int size);
udp::endpoint utp_remote_endpoint(utp_socket_impl* s);

class utp_stream
{
public:

	typedef stream_socket::lowest_layer_type lowest_layer_type;
	typedef stream_socket::endpoint_type endpoint_type;
	typedef stream_socket::protocol_type protocol_type;

	explicit utp_stream(asio::io_service& io_service);
	~utp_stream();

	// used for incoming connections
	void assign(utp_socket_impl* s);
	void set_manager(utp_socket_manager* sm);

#ifndef BOOST_NO_EXCEPTIONS
	template <class IO_Control_Command>
	void io_control(IO_Control_Command& ioc) {}
#endif

	template <class IO_Control_Command>
	void io_control(IO_Control_Command& ioc, error_code& ec) {}

#ifndef BOOST_NO_EXCEPTIONS
	void bind(endpoint_type const& endpoint) {}
#endif

	void bind(endpoint_type const& endpoint, error_code& ec);

#ifndef BOOST_NO_EXCEPTIONS
	template <class SettableSocketOption>
	void set_option(SettableSocketOption const& opt) {}
#endif

	template <class SettableSocketOption>
	error_code set_option(SettableSocketOption const& opt, error_code& ec) { return ec; }

	void close();
	void close(error_code const& ec) { close(); }
	bool is_open() const { return m_open; }

	int read_buffer_size() const;
	static void on_read(void* self, size_t bytes_transferred, error_code const& ec, bool kill);
	static void on_write(void* self, size_t bytes_transferred, error_code const& ec, bool kill);
	static void on_connect(void* self, error_code const& ec, bool kill);

	typedef void(*handler_t)(void*, size_t, error_code const&, bool);
	typedef void(*connect_handler_t)(void*, error_code const&, bool);

	void add_read_buffer(void* buf, size_t len);
	void set_read_handler(handler_t h);
	void add_write_buffer(void const* buf, size_t len);
	void set_write_handler(handler_t h);
	
	void do_connect(tcp::endpoint const& ep, connect_handler_t h);

	endpoint_type local_endpoint() const;

	endpoint_type local_endpoint(error_code const& ec) const
	{ return local_endpoint(); }

	endpoint_type remote_endpoint() const;

	endpoint_type remote_endpoint(error_code const& ec) const
	{ return remote_endpoint(); }

	asio::io_service& io_service()
	{ return m_io_service; }

	template <class Handler>
	void async_connect(endpoint_type const& endpoint, Handler const& handler)
	{
		if (!endpoint.address().is_v4())
		{
			error_code ec = asio::error::operation_not_supported;
			m_io_service.post(boost::bind<void>(handler, asio::error::operation_not_supported, 0));
			handler(ec);
			return;
		}

		m_connect_handler = handler;
		do_connect(endpoint, &utp_stream::on_connect);
	}
	
	template <class Mutable_Buffers, class Handler>
	void async_read_some(Mutable_Buffers const& buffer, Handler const& handler)
	{
		if (m_impl == 0)
		{
			m_io_service.post(boost::bind<void>(handler, asio::error::not_connected, 0));
			return;
		}

		TORRENT_ASSERT(!m_read_handler);
		if (m_read_handler)
		{
			m_io_service.post(boost::bind<void>(handler, asio::error::operation_not_supported, 0));
			return;
		}
		for (typename Mutable_Buffers::const_iterator i = buffer.begin()
			, end(buffer.end()); i != end; ++i)
		{
			using asio::buffer_cast;
			using asio::buffer_size;
			add_read_buffer(buffer_cast<void*>(*i), buffer_size(*i));
		}
		m_read_handler = handler;
		set_read_handler(&utp_stream::on_read);
	}

	void do_async_connect(endpoint_type const& ep
		, boost::function<void(error_code const&)> const& handler);

	template <class Protocol>
	void open(Protocol const& p, error_code& ec)
	{ m_open = true; }

	template <class Protocol>
	void open(Protocol const& p)
	{ m_open = true; }

	template <class Mutable_Buffers>
	std::size_t read_some(Mutable_Buffers const& buffers, error_code& ec)
	{
		if (read_buffer_size() == 0)
		{
			ec = asio::error::would_block;
			return -1;
		}
//#error implement
		ec = asio::error::would_block;
		return -1;
	}

	template <class Const_Buffers, class Handler>
	void async_write_some(Const_Buffers const& buffers, Handler const& handler)
	{
		if (m_impl == 0)
		{
			m_io_service.post(boost::bind<void>(handler, asio::error::not_connected, 0));
			return;
		}

		TORRENT_ASSERT(!m_write_handler);
		if (m_write_handler)
		{
			m_io_service.post(boost::bind<void>(handler, asio::error::operation_not_supported, 0));
			return;
		}
		for (typename Const_Buffers::const_iterator i = buffers.begin()
			, end(buffers.end()); i != end; ++i)
		{
			using asio::buffer_cast;
			using asio::buffer_size;
			add_write_buffer((void*)buffer_cast<void const*>(*i), buffer_size(*i));
		}
		m_write_handler = handler;
		set_write_handler(&utp_stream::on_write);
	}

//private:

	void cancel_handlers(error_code const&);

	boost::function1<void, error_code const&> m_connect_handler;
	boost::function2<void, error_code const&, std::size_t> m_read_handler;
	boost::function2<void, error_code const&, std::size_t> m_write_handler;

	asio::io_service& m_io_service;
	utp_socket_impl* m_impl;
	bool m_open;
};

}

#endif
