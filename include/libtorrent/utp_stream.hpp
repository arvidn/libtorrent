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

#ifndef TORRENT_UTP_STREAM_HPP_INCLUDED
#define TORRENT_UTP_STREAM_HPP_INCLUDED

#include "libtorrent/proxy_base.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/packet_buffer.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/close_reason.hpp"

#include <functional>

#ifndef BOOST_NO_EXCEPTIONS
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/system/system_error.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

namespace libtorrent {

#ifndef TORRENT_UTP_LOG_ENABLE
	#define TORRENT_UTP_LOG 0
	#define TORRENT_VERBOSE_UTP_LOG 0
#else
	#define TORRENT_UTP_LOG 1
	#define TORRENT_VERBOSE_UTP_LOG 1
#endif

#if TORRENT_UTP_LOG
	TORRENT_FORMAT(1, 2)
	void utp_log(char const* fmt, ...);
	TORRENT_EXPORT bool is_utp_stream_logging();

	// This function should be used at the very beginning and very end of your program.
	TORRENT_EXPORT void set_utp_stream_logging(bool enable);
#endif

	TORRENT_EXTRA_EXPORT bool compare_less_wrap(std::uint32_t lhs
		, std::uint32_t rhs, std::uint32_t mask);

	struct utp_socket_manager;

	// internal: the point of the bif_endian_int is two-fold
	// one purpose is to not have any alignment requirements
	// so that any buffer received from the network can be cast
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
			detail::write_impl<T>(v, p);
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

	using be_uint64 = big_endian_int<std::uint64_t>;
	using be_uint32 = big_endian_int<std::uint32_t>;
	using be_uint16 = big_endian_int<std::uint16_t>;
	using be_int64 = big_endian_int<std::int64_t>;
	using be_int32 = big_endian_int<std::int32_t>;
	using be_int16 = big_endian_int<std::int16_t>;

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

// internal: extension headers. 2 is skipped because there is a deprecated
// extension with that number in the wild
enum utp_extensions_t
{ utp_no_extension = 0, utp_sack = 1, utp_close_reason = 3 };

struct utp_header
{
	std::uint8_t type_ver;
	std::uint8_t extension;
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
struct utp_socket_interface;

utp_socket_impl* construct_utp_impl(std::uint16_t recv_id
	, std::uint16_t send_id, void* userdata
	, utp_socket_manager& sm);
void detach_utp_impl(utp_socket_impl* s);
void delete_utp_impl(utp_socket_impl* s);
void utp_abort(utp_socket_impl* s);
bool should_delete(utp_socket_impl* s);
bool bound_to_udp_socket(utp_socket_impl* s, std::weak_ptr<utp_socket_interface> sock);
void tick_utp_impl(utp_socket_impl* s, time_point now);
void utp_init_mtu(utp_socket_impl* s, int link_mtu, int utp_mtu);
void utp_init_socket(utp_socket_impl* s, std::weak_ptr<utp_socket_interface> sock);
bool utp_incoming_packet(utp_socket_impl* s, span<char const> p
	, udp::endpoint const& ep, time_point receive_time);
bool utp_match(utp_socket_impl* s, udp::endpoint const& ep, std::uint16_t id);
udp::endpoint utp_remote_endpoint(utp_socket_impl* s);
std::uint16_t utp_receive_id(utp_socket_impl* s);
int utp_socket_state(utp_socket_impl const* s);
void utp_send_ack(utp_socket_impl* s);
void utp_socket_drained(utp_socket_impl* s);
void utp_writable(utp_socket_impl* s);

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
struct TORRENT_EXTRA_EXPORT utp_stream
{
	using lowest_layer_type = utp_stream ;
	using endpoint_type = tcp::socket::endpoint_type;
	using protocol_type = tcp::socket::protocol_type;

#if BOOST_VERSION >= 106600
	using executor_type = tcp::socket::executor_type;
	executor_type get_executor() { return m_io_service.get_executor(); }
#endif

	explicit utp_stream(io_context& io_context);
	~utp_stream();
	utp_stream& operator=(utp_stream const&) = delete;
	utp_stream(utp_stream const&) = delete;
	utp_stream& operator=(utp_stream&&) noexcept = delete;
	utp_stream(utp_stream&&) noexcept = delete;

	lowest_layer_type& lowest_layer() { return *this; }

	// used for incoming connections
	void set_impl(utp_socket_impl* s);
	utp_socket_impl* get_impl();

#ifndef BOOST_NO_EXCEPTIONS
	template <class IO_Control_Command>
	void io_control(IO_Control_Command&) {}
#endif

	template <class IO_Control_Command>
	void io_control(IO_Control_Command&, error_code&) {}

#ifndef BOOST_NO_EXCEPTIONS
	void non_blocking(bool) {}
#endif

	error_code non_blocking(bool, error_code&) { return error_code(); }

#ifndef BOOST_NO_EXCEPTIONS
	void bind(endpoint_type const& /*endpoint*/) {}
#endif

	void bind(endpoint_type const&, error_code&);

#ifndef BOOST_NO_EXCEPTIONS
	template <class SettableSocketOption>
	void set_option(SettableSocketOption const&) {}
#endif

	template <class SettableSocketOption>
	error_code set_option(SettableSocketOption const&, error_code& ec) { return ec; }

#ifndef BOOST_NO_EXCEPTIONS
	template <class GettableSocketOption>
	void get_option(GettableSocketOption&) {}
#endif

	template <class GettableSocketOption>
	error_code get_option(GettableSocketOption&, error_code& ec)
	{ return ec; }

	error_code cancel(error_code&)
	{
		cancel_handlers(boost::asio::error::operation_aborted);
		return error_code();
	}

	void close();
	void close(error_code const&) { close(); }

	void set_close_reason(close_reason_t code);
	close_reason_t get_close_reason();

	bool is_open() const { return m_open; }

	int read_buffer_size() const;
	static void on_read(void* self, std::size_t bytes_transferred
		, error_code const& ec, bool kill);
	static void on_write(void* self, std::size_t bytes_transferred
		, error_code const& ec, bool kill);
	static void on_connect(void* self, error_code const& ec, bool kill);
	static void on_close_reason(void* self, close_reason_t reason);

	void add_read_buffer(void* buf, std::size_t len);
	void issue_read();
	void add_write_buffer(void const* buf, std::size_t len);
	void issue_write();
	std::size_t read_some(bool clear_buffers);

	int send_delay() const;
	int recv_delay() const;

	void do_connect(tcp::endpoint const& ep);

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

	io_context& get_io_service() { return m_io_service; }

	template <class Handler>
	void async_connect(endpoint_type const& endpoint, Handler const& handler)
	{
		if (m_impl == nullptr)
		{
			post(m_io_service, std::bind<void>(handler, boost::asio::error::not_connected));
			return;
		}

		m_connect_handler = handler;
		do_connect(endpoint);
	}

	template <class Mutable_Buffers, class Handler>
	void async_read_some(Mutable_Buffers const& buffers, Handler const& handler)
	{
		if (m_impl == nullptr)
		{
			post(m_io_service, std::bind<void>(handler, boost::asio::error::not_connected, std::size_t(0)));
			return;
		}

		TORRENT_ASSERT(!m_read_handler);
		if (m_read_handler)
		{
			post(m_io_service, std::bind<void>(handler, boost::asio::error::operation_not_supported, std::size_t(0)));
			return;
		}
		std::size_t bytes_added = 0;
#if BOOST_VERSION >= 106600
		for (auto i = buffer_sequence_begin(buffers)
			, end(buffer_sequence_end(buffers)); i != end; ++i)
#else
		for (typename Mutable_Buffers::const_iterator i = buffers.begin()
			, end(buffers.end()); i != end; ++i)
#endif
		{
			if (buffer_size(*i) == 0) continue;
			using boost::asio::buffer_cast;
			using boost::asio::buffer_size;
			add_read_buffer(buffer_cast<void*>(*i), buffer_size(*i));
			bytes_added += buffer_size(*i);
		}
		if (bytes_added == 0)
		{
			// if we're reading 0 bytes, post handler immediately
			// asio's SSL layer depends on this behavior
			post(m_io_service, std::bind<void>(handler, error_code(), std::size_t(0)));
			return;
		}

		m_read_handler = handler;
		issue_read();
	}

	template <class Protocol>
	void open(Protocol const&, error_code&)
	{ m_open = true; }

	template <class Protocol>
	void open(Protocol const&)
	{ m_open = true; }

	template <class Mutable_Buffers>
	std::size_t read_some(Mutable_Buffers const& buffers, error_code& ec)
	{
		TORRENT_ASSERT(!m_read_handler);
		if (m_impl == nullptr)
		{
			ec = boost::asio::error::not_connected;
			return 0;
		}

		if (read_buffer_size() == 0)
		{
			ec = boost::asio::error::would_block;
			return 0;
		}
#if TORRENT_USE_ASSERTS
		size_t buf_size = 0;
#endif

#if BOOST_VERSION >= 106600
		for (auto i = buffer_sequence_begin(buffers)
			, end(buffer_sequence_end(buffers)); i != end; ++i)
#else
		for (typename Mutable_Buffers::const_iterator i = buffers.begin()
			, end(buffers.end()); i != end; ++i)
#endif
		{
			using boost::asio::buffer_cast;
			using boost::asio::buffer_size;
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
	std::size_t write_some(Const_Buffers const& /* buffers */, error_code& /* ec */)
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

	template <class Const_Buffers, class Handler>
	void async_write_some(Const_Buffers const& buffers, Handler const& handler)
	{
		if (m_impl == nullptr)
		{
			post(m_io_service, std::bind<void>(handler
				, boost::asio::error::not_connected, std::size_t(0)));
			return;
		}

		TORRENT_ASSERT(!m_write_handler);
		if (m_write_handler)
		{
			post(m_io_service, std::bind<void>(handler
				, boost::asio::error::operation_not_supported, std::size_t(0)));
			return;
		}

		std::size_t bytes_added = 0;
#if BOOST_VERSION >= 106600
		for (auto i = buffer_sequence_begin(buffers)
			, end(buffer_sequence_end(buffers)); i != end; ++i)
#else
		for (typename Const_Buffers::const_iterator i = buffers.begin()
			, end(buffers.end()); i != end; ++i)
#endif
		{
			if (buffer_size(*i) == 0) continue;
			using boost::asio::buffer_cast;
			using boost::asio::buffer_size;
			add_write_buffer(buffer_cast<void const*>(*i), buffer_size(*i));
			bytes_added += buffer_size(*i);
		}
		if (bytes_added == 0)
		{
			// if we're writing 0 bytes, post handler immediately
			// asio's SSL layer depends on this behavior
			post(m_io_service, std::bind<void>(handler, error_code(), std::size_t(0)));
			return;
		}
		m_write_handler = handler;
		issue_write();
	}

private:

	void cancel_handlers(error_code const&);

	std::function<void(error_code const&)> m_connect_handler;
	std::function<void(error_code const&, std::size_t)> m_read_handler;
	std::function<void(error_code const&, std::size_t)> m_write_handler;

	io_context& m_io_service;
	utp_socket_impl* m_impl;

	close_reason_t m_incoming_close_reason = close_reason_t::none;

	// this field requires another 8 bytes (including padding)
	bool m_open;
};

}

#endif
