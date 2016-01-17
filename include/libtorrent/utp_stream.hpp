/*

Copyright (c) 2009-2016, Arvid Norberg
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

#ifndef BOOST_NO_EXCEPTIONS
#include <boost/system/system_error.hpp>
#endif

#define CCONTROL_TARGET 100

namespace libtorrent
{
	struct utp_socket_manager;

	// internal: some MTU and protocol header sizes constants
	enum
	{
		TORRENT_IPV4_HEADER = 20,
		TORRENT_IPV6_HEADER = 40,
		TORRENT_UDP_HEADER = 8,
		TORRENT_SOCKS5_HEADER = 6, // plus the size of the destination address

		TORRENT_ETHERNET_MTU = 1500,
		TORRENT_TEREDO_MTU = 1280,
		TORRENT_INET_MIN_MTU = 576,
		TORRENT_INET_MAX_MTU = 0xffff
	};

	// internal: the point of the bif_endian_int is two-fold
	// one purpuse is to not have any alignment requirements
	// so that any byffer received from the network can be cast
	// to it and read as an integer of various sizes without
	// triggering a bus error. The other purpose is to convert
	// from network byte order to host byte order when read and
	// written, to offer a convenient interface to both interpreting
	// and writing network packets
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
	| type  | ver   | extension     | connection_id                 |
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

// internal: the different kinds of uTP packets
enum utp_socket_state_t
{ ST_DATA, ST_FIN, ST_STATE, ST_RESET, ST_SYN, NUM_TYPES };

struct utp_header
{
	unsigned char type_ver;
	unsigned char extension;
	be_uint16 connection_id;
	be_uint32 timestamp_microseconds;
	be_uint32 timestamp_difference_microseconds;
	be_uint32 wnd_size;
	be_uint16 seq_nr;
	be_uint16 ack_nr;

	int get_type() const { return type_ver >> 4; }
	int get_version() const { return type_ver & 0xf; }
};

struct utp_socket_impl;

utp_socket_impl* construct_utp_impl(boost::uint16_t recv_id
	, boost::uint16_t send_id, void* userdata
	, utp_socket_manager* sm);
void detach_utp_impl(utp_socket_impl* s);
void delete_utp_impl(utp_socket_impl* s);
bool should_delete(utp_socket_impl* s);
void tick_utp_impl(utp_socket_impl* s, ptime const& now);
void utp_init_mtu(utp_socket_impl* s, int link_mtu, int utp_mtu);
bool utp_incoming_packet(utp_socket_impl* s, char const* p
	, int size, udp::endpoint const& ep, ptime receive_time);
bool utp_match(utp_socket_impl* s, udp::endpoint const& ep, boost::uint16_t id);
udp::endpoint utp_remote_endpoint(utp_socket_impl* s);
boost::uint16_t utp_receive_id(utp_socket_impl* s);
int utp_socket_state(utp_socket_impl const* s);
void utp_send_ack(utp_socket_impl* s);
void utp_socket_drained(utp_socket_impl* s);
void utp_writable(utp_socket_impl* s);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
int socket_impl_size();
#endif

// this is the user-level stream interface to utp sockets.
// the reason why it's split up in a utp_stream class and
// an implementation class is because the socket state has
// to be able to out-live the user level socket. For instance
// when sending data on a stream and then closing it, the
// state holding the send buffer has to be kept around until
// it has been flushed, which may be longer than the client
// will keep the utp_stream object around for.
// for more details, see utp_socket_impl, which is analogous
// to the kernel state for a socket. It's defined in utp_stream.cpp
class TORRENT_EXTRA_EXPORT utp_stream
{
public:

	typedef utp_stream lowest_layer_type;
	typedef stream_socket::endpoint_type endpoint_type;
	typedef stream_socket::protocol_type protocol_type;

	explicit utp_stream(asio::io_service& io_service);
	~utp_stream();

	lowest_layer_type& lowest_layer() { return *this; }

	// used for incoming connections
	void set_impl(utp_socket_impl* s);
	utp_socket_impl* get_impl();

#ifndef BOOST_NO_EXCEPTIONS
	template <class IO_Control_Command>
	void io_control(IO_Control_Command& ioc) {}
#endif

	template <class IO_Control_Command>
	void io_control(IO_Control_Command& ioc, error_code& ec) {}

#ifndef BOOST_NO_EXCEPTIONS
	void bind(endpoint_type const& /*endpoint*/) {}
#endif

	void bind(endpoint_type const& endpoint, error_code& ec);

#ifndef BOOST_NO_EXCEPTIONS
	template <class SettableSocketOption>
	void set_option(SettableSocketOption const& opt) {}
#endif

	template <class SettableSocketOption>
	error_code set_option(SettableSocketOption const& opt, error_code& ec) { return ec; }

#ifndef BOOST_NO_EXCEPTIONS
	template <class GettableSocketOption>
	void get_option(GettableSocketOption& opt) {}
#endif

	template <class GettableSocketOption>
	error_code get_option(GettableSocketOption& opt, error_code& ec) { return ec; }


	void close();
	void close(error_code const& /*ec*/) { close(); }
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
	size_t read_some(bool clear_buffers);
	
	int send_delay() const;
	int recv_delay() const;

	void do_connect(tcp::endpoint const& ep, connect_handler_t h);

	endpoint_type local_endpoint() const
	{
		error_code ec;
		return local_endpoint(ec);
	}

	endpoint_type local_endpoint(error_code& ec) const;

	endpoint_type remote_endpoint() const
	{
		error_code ec;
		return remote_endpoint(ec);
	}

	endpoint_type remote_endpoint(error_code& ec) const;

	std::size_t available() const;
	std::size_t available(error_code& /*ec*/) const { return available(); }

	asio::io_service& get_io_service() { return m_io_service; }

	template <class Handler>
	void async_connect(endpoint_type const& endpoint, Handler const& handler)
	{
		if (!endpoint.address().is_v4())
		{
			m_io_service.post(boost::bind<void>(handler, asio::error::operation_not_supported, 0));
			return;
		}

		if (m_impl == 0)
		{
			m_io_service.post(boost::bind<void>(handler, asio::error::not_connected, 0));
			return;
		}

		m_connect_handler = handler;
		do_connect(endpoint, &utp_stream::on_connect);
	}
	
	template <class Handler>
	void async_read_some(boost::asio::null_buffers const& buffers, Handler const& handler)
	{
		TORRENT_ASSERT(false);
	}

	template <class Mutable_Buffers, class Handler>
	void async_read_some(Mutable_Buffers const& buffers, Handler const& handler)
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
		int bytes_added = 0;
		for (typename Mutable_Buffers::const_iterator i = buffers.begin()
			, end(buffers.end()); i != end; ++i)
		{
			if (buffer_size(*i) == 0) continue;
			using asio::buffer_cast;
			using asio::buffer_size;
			add_read_buffer(buffer_cast<void*>(*i), buffer_size(*i));
			bytes_added += buffer_size(*i);
		}
		if (bytes_added == 0)
		{
			// if we're reading 0 bytes, post handler immediately
			// asio's SSL layer depends on this behavior
			m_io_service.post(boost::bind<void>(handler, error_code(), 0));
			return;
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
		TORRENT_ASSERT(!m_read_handler);
		if (m_impl == 0)
		{
			ec = asio::error::not_connected;
			return 0;
		}

		if (read_buffer_size() == 0)
		{
			ec = asio::error::would_block;
			return 0;
		}
#if TORRENT_USE_ASSERTS
		size_t buf_size = 0;
#endif

		for (typename Mutable_Buffers::const_iterator i = buffers.begin()
			, end(buffers.end()); i != end; ++i)
		{
			using asio::buffer_cast;
			using asio::buffer_size;
			add_read_buffer(buffer_cast<void*>(*i), buffer_size(*i));
#if TORRENT_USE_ASSERTS
			buf_size += buffer_size(*i);
#endif
		}
		std::size_t ret = read_some(true);
		TORRENT_ASSERT(ret <= buf_size);
		TORRENT_ASSERT(ret > 0);
		return ret;
	}

	template <class Const_Buffers>
	std::size_t write_some(Const_Buffers const& buffers, error_code& ec)
	{
		TORRENT_ASSERT(false && "not implemented!");
		// TODO: implement blocking write. Low priority since it's not used (yet)
		return 0;
	}

#ifndef BOOST_NO_EXCEPTIONS
	template <class Mutable_Buffers>
	std::size_t read_some(Mutable_Buffers const& buffers)
	{
		error_code ec;
		std::size_t ret = read_some(buffers, ec);
		if (ec)
			boost::throw_exception(boost::system::system_error(ec));
		return ret;
	}

	template <class Const_Buffers>
	std::size_t write_some(Const_Buffers const& buffers)
	{
		error_code ec;
		std::size_t ret = write_some(buffers, ec);
		if (ec)
			boost::throw_exception(boost::system::system_error(ec));
		return ret;
	}
#endif

	template <class Handler>
	void async_write_some(boost::asio::null_buffers const& buffers, Handler const& handler)
	{
		TORRENT_ASSERT(false);
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

		int bytes_added = 0;
		for (typename Const_Buffers::const_iterator i = buffers.begin()
			, end(buffers.end()); i != end; ++i)
		{
			if (buffer_size(*i) == 0) continue;
			using asio::buffer_cast;
			using asio::buffer_size;
			add_write_buffer((void*)buffer_cast<void const*>(*i), buffer_size(*i));
			bytes_added += buffer_size(*i);
		}
		if (bytes_added == 0)
		{
			// if we're reading 0 bytes, post handler immediately
			// asio's SSL layer depends on this behavior
			m_io_service.post(boost::bind<void>(handler, error_code(), 0));
			return;
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

	// this field requires another 8 bytes (including padding)
	bool m_open;
};

}

#endif
