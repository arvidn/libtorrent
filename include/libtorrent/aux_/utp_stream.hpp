/*

Copyright (c) 2010-2020, Arvid Norberg
Copyright (c) 2015-2018, Alden Torres
Copyright (c) 2017, Steven Siloti
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

#ifndef TORRENT_UTP_STREAM_HPP_INCLUDED
#define TORRENT_UTP_STREAM_HPP_INCLUDED

#include "libtorrent/udp_socket.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/aux_/packet_buffer.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/close_reason.hpp"
#include "libtorrent/aux_/timestamp_history.hpp"
#include "libtorrent/sliding_average.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/aux_/storage_utils.hpp" // for iovec_t

#include <functional>

#ifndef BOOST_NO_EXCEPTIONS
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/system/system_error.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

namespace libtorrent {
namespace aux {

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
		big_endian_int& operator=(T v) &
		{
			char* p = m_storage;
			aux::write_impl<T>(v, p);
			return *this;
		}
		operator T() const
		{
			const char* p = m_storage;
			return aux::read_impl(p, aux::type<T>());
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
enum utp_socket_state_t : std::uint8_t
{ ST_DATA, ST_FIN, ST_STATE, ST_RESET, ST_SYN, NUM_TYPES };

// internal: extension headers. 2 is skipped because there is a deprecated
// extension with that number in the wild
enum utp_extensions_t : std::uint8_t
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
struct utp_stream;

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

	using executor_type = tcp::socket::executor_type;
	executor_type get_executor() { return m_io_service.get_executor(); }

	explicit utp_stream(io_context& io_context);
	~utp_stream();
	utp_stream& operator=(utp_stream const&) = delete;
	utp_stream(utp_stream const&) = delete;
	utp_stream& operator=(utp_stream&&) noexcept = delete;
	utp_stream(utp_stream&&) noexcept;

	lowest_layer_type& lowest_layer() { return *this; }
	lowest_layer_type const& lowest_layer() const { return *this; }

	// used for incoming connections
	void set_impl(utp_socket_impl*);
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

	void non_blocking(bool, error_code&) {}

#ifndef BOOST_NO_EXCEPTIONS
	void bind(endpoint_type const& /*endpoint*/) {}
#endif

	void bind(endpoint_type const&, error_code&);

#ifndef BOOST_NO_EXCEPTIONS
	template <class SettableSocketOption>
	void set_option(SettableSocketOption const&) {}
#endif

	template <class SettableSocketOption>
	void set_option(SettableSocketOption const&, error_code&) { }

#ifndef BOOST_NO_EXCEPTIONS
	template <class GettableSocketOption>
	void get_option(GettableSocketOption&) {}
#endif

	template <class GettableSocketOption>
	void get_option(GettableSocketOption&, error_code&) {}

	void cancel(error_code&)
	{
		cancel_handlers(boost::asio::error::operation_aborted);
	}

	void close();
	void close(error_code const&) { close(); }

	void set_close_reason(close_reason_t code);
	close_reason_t get_close_reason() const;

	bool is_open() const { return m_open; }

	int read_buffer_size() const;
	static void on_read(utp_stream* self, std::size_t bytes_transferred
		, error_code const& ec, bool shutdown);
	static void on_write(utp_stream* self, std::size_t bytes_transferred
		, error_code const& ec, bool shutdown);
	static void on_connect(utp_stream* self, error_code const& ec, bool shutdown);
	static void on_close_reason(utp_stream* self, close_reason_t reason);

	void add_read_buffer(void* buf, int len);
	void issue_read();
	void add_write_buffer(void const* buf, int len);
	void issue_write();
	std::size_t read_some(bool clear_buffers);
	std::size_t write_some(bool clear_buffers);

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

	template <class Handler>
	void async_connect(endpoint_type const& endpoint, Handler handler)
	{
		if (m_impl == nullptr)
		{
			post(m_io_service, std::bind<void>(std::move(handler), boost::asio::error::not_connected));
			return;
		}

		m_connect_handler = std::move(handler);
		do_connect(endpoint);
	}

	template <class Mutable_Buffers, class Handler>
	void async_read_some(Mutable_Buffers const& buffers, Handler handler)
	{
		if (m_impl == nullptr)
		{
			post(m_io_service, std::bind<void>(std::move(handler), boost::asio::error::not_connected, std::size_t(0)));
			return;
		}

		TORRENT_ASSERT(!m_read_handler);
		if (m_read_handler)
		{
			post(m_io_service, std::bind<void>(std::move(handler), boost::asio::error::operation_not_supported, std::size_t(0)));
			return;
		}
		std::size_t bytes_added = 0;
		for (auto i = buffer_sequence_begin(buffers)
			, end(buffer_sequence_end(buffers)); i != end; ++i)
		{
			if (i->size() == 0) continue;
			add_read_buffer(i->data(), int(i->size()));
			bytes_added += i->size();
		}
		if (bytes_added == 0)
		{
			// if we're reading 0 bytes, post handler immediately
			// asio's SSL layer depends on this behavior
			post(m_io_service, std::bind<void>(std::move(handler), error_code(), std::size_t(0)));
			return;
		}

		m_read_handler = std::move(handler);
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

		for (auto i = buffer_sequence_begin(buffers)
			, end(buffer_sequence_end(buffers)); i != end; ++i)
		{
			add_read_buffer(i->data(), int(i->size()));
#if TORRENT_USE_ASSERTS
			buf_size += i->size();
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
		TORRENT_ASSERT(!m_write_handler);
		if (m_impl == nullptr)
		{
			ec = boost::asio::error::not_connected;
			return 0;
		}

#if TORRENT_USE_ASSERTS
		size_t buf_size = 0;
#endif

		for (auto i = buffer_sequence_begin(buffers)
			, end(buffer_sequence_end(buffers)); i != end; ++i)
		{
			add_write_buffer(i->data(), int(i->size()));
#if TORRENT_USE_ASSERTS
			buf_size += i->size();
#endif
		}
		std::size_t ret = write_some(true);
		TORRENT_ASSERT(ret <= buf_size);
		if(ret == 0)
		{
			ec = boost::asio::error::would_block;
			return 0;
		}
		return ret;
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
	void async_write_some(Const_Buffers const& buffers, Handler handler)
	{
		if (m_impl == nullptr)
		{
			post(m_io_service, std::bind<void>(std::move(handler)
				, boost::asio::error::not_connected, std::size_t(0)));
			return;
		}

		TORRENT_ASSERT(!m_write_handler);
		if (m_write_handler)
		{
			post(m_io_service, std::bind<void>(std::move(handler)
				, boost::asio::error::operation_not_supported, std::size_t(0)));
			return;
		}

		std::size_t bytes_added = 0;
		for (auto i = buffer_sequence_begin(buffers)
			, end(buffer_sequence_end(buffers)); i != end; ++i)
		{
			if (i->size() == 0) continue;
			add_write_buffer(i->data(), int(i->size()));
			bytes_added += i->size();
		}
		if (bytes_added == 0)
		{
			// if we're writing 0 bytes, post handler immediately
			// asio's SSL layer depends on this behavior
			post(m_io_service, std::bind<void>(std::move(handler), error_code(), std::size_t(0)));
			return;
		}
		m_write_handler = std::move(handler);
		issue_write();
	}

#if BOOST_VERSION >= 106600
	// Compatiblity with the async_wait method introduced in boost 1.66

	enum wait_type { wait_read, wait_write, wait_error };

	template <class Handler>
	void async_wait(wait_type type, Handler handler) {
		switch(type)
		{
		case wait_read:
			async_read_some(boost::asio::null_buffers()
					, [handler](error_code ec, size_t) { handler(std::move(ec)); });
			break;

		case wait_write:
			async_write_some(boost::asio::null_buffers()
					, [handler](error_code ec, size_t) { handler(std::move(ec)); });
			break;

		case wait_error:
			post(m_io_service, std::bind<void>(std::move(handler)
					, boost::asio::error::operation_not_supported));
            break;
		}
	}
#endif

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

// since the uTP socket state may be needed after the
// utp_stream is closed, it's kept in a separate struct
// whose lifetime is not tied to the lifetime of utp_stream

// the utp socket is closely modelled after the asio async
// operations and handler model. For writing to the socket,
// the client provides a list of buffers (for gather/writev
// style of I/O) and whenever the socket can write another
// packet to the stream, it picks up data from these buffers.
// When all of the data has been written, or enough time has
// passed since we first started writing, the write handler
// is called and the write buffer is reset. This means that
// we're not writing anything at all while waiting for the
// client to re-issue a write request.

// reading is a little bit more complicated, since we must
// be able to receive data even when the user doesn't have
// an outstanding read operation on the socket. When the user
// does however, we want to receive data directly into the
// user's buffer instead of first copying it into our receive
// buffer. This is why the receive case is more complicated.
// There are two receive buffers. One provided by the user,
// which when present is always used. The other one is used
// when the user doesn't have an outstanding read request,
// and hence hasn't provided any buffer space to receive into.

// the user provided read buffer is called "m_read_buffer" and
// its size is "m_read_buffer_size". The buffer we spill over
// into when the user provided buffer is full or when there
// is none, is "m_receive_buffer" and "m_receive_buffer_size"
// respectively.

// in order to know when to trigger the read and write handlers
// there are two counters, m_read and m_written, which count
// the number of bytes we've stuffed into the user provided
// read buffer or written to the stream from the write buffer.
// These are used to trigger the handlers if we're written a
// large number of bytes. It's also triggered if we're filled
// the whole read buffer, or written the entire write buffer.
// The last way the handlers can be triggered is if we're read
// or written some, and enough time has elapsed since then.

// when we receive data into m_receive_buffer (i.e. the buffer
// used when there's no user provided one) is stored as a
// number of heap allocated packets. This is just because it's
// simple to reuse the data structured and it provides all the
// functionality needed for this buffer.

struct utp_socket_impl
{
#if TORRENT_USE_INVARIANT_CHECKS
	friend struct ::libtorrent::invariant_access;
#endif

	utp_socket_impl(std::uint16_t recv_id, std::uint16_t send_id
		, utp_stream* userdata, utp_socket_manager& sm);

	~utp_socket_impl();

	void tick(time_point now);
	void init_mtu(int mtu);
	bool incoming_packet(span<char const> buf
		, udp::endpoint const& ep, time_point receive_time);
	void writable();

	bool should_delete() const;
	tcp::endpoint remote_endpoint(error_code& ec) const;
	std::size_t available() const;
	// returns true if there were handlers cancelled
	// if it returns false, we can detach immediately
	bool destroy();
	void set_close_reason(close_reason_t code);
	void detach();
	void send_syn();
	void send_fin();

	void subscribe_drained();
	void defer_ack();
	void remove_sack_header(packet* p);

	enum packet_flags_t { pkt_ack = 1, pkt_fin = 2 };
	bool send_pkt(int flags = 0);
	bool resend_packet(packet* p, bool fast_resend = false);
	void send_reset(utp_header const* ph);
	std::pair<std::uint32_t, int> parse_sack(std::uint16_t packet_ack, std::uint8_t const* ptr
		, int size, time_point now);
	void parse_close_reason(std::uint8_t const* ptr, int size);
	void write_payload(std::uint8_t* ptr, int size);
	void maybe_inc_acked_seq_nr();
	std::uint32_t ack_packet(packet_ptr p, time_point receive_time
		, std::uint16_t seq_nr);
	void write_sack(std::uint8_t* buf, int size) const;
	void incoming(std::uint8_t const* buf, int size, packet_ptr p, time_point now);
	void do_ledbat(int acked_bytes, int delay, int in_flight);
	int packet_timeout() const;
	bool test_socket_state();
	void maybe_trigger_receive_callback();
	void maybe_trigger_send_callback();
	bool cancel_handlers(error_code const& ec, bool shutdown);
	bool consume_incoming_data(
		utp_header const* ph, std::uint8_t const* ptr, int payload_size, time_point now);
	void update_mtu_limits();
	void experienced_loss(std::uint32_t seq_nr, time_point now);

	void send_ack();
	void socket_drained();

	void set_userdata(utp_stream* s) { m_userdata = s; }
	void abort();
	udp::endpoint remote_endpoint() const;

	std::uint16_t receive_id() const { return m_recv_id; }
	bool match(udp::endpoint const& ep, std::uint16_t id) const;

	// non-copyable
	utp_socket_impl(utp_socket_impl const&) = delete;
	utp_socket_impl const& operator=(utp_socket_impl const&) = delete;

	// The underlying UDP socket this uTP socket is bound to
	// TODO: it would be nice to make this private
	std::weak_ptr<utp_socket_interface> m_sock;

	void add_write_buffer(void const* buf, int len);
	void add_read_buffer(void* buf, int len);

	int send_delay() const { return m_send_delay; }
	int recv_delay() const { return m_recv_delay; }

	void issue_read();
	void issue_write();

	void do_connect(tcp::endpoint const& ep);

	std::size_t read_some(bool const clear_buffers);
	std::size_t write_some(bool const clear_buffers); // Warning: non-blocking
	int receive_buffer_size() const { return m_receive_buffer_size; }

	bool null_buffers() const { return m_null_buffers; }

private:

	// it's important that these match the enums in performance_counters for
	// num_utp_idle etc.
	enum class state_t {
		// not yet connected
		none,
		// sent a syn packet, not received any acks
		syn_sent,
		// syn-ack received and in normal operation
		// of sending and receiving data
		connected,
		// fin sent, but all packets up to the fin packet
		// have not yet been acked. We might still be waiting
		// for a FIN from the other end
		fin_sent,

		// ====== states beyond this point =====
		// === are considered closing states ===
		// === and will cause the socket to ====
		// ============ be deleted =============

		// the socket has been gracefully disconnected
		// and is waiting for the client to make a
		// socket call so that we can communicate this
		// fact and actually delete all the state, or
		// there is an error on this socket and we're
		// waiting to communicate this to the client in
		// a callback. The error in either case is stored
		// in m_error. If the socket has gracefully shut
		// down, the error is error::eof.
		error_wait,

		// there are no more references to this socket
		// and we can delete it
		deleting
	};

	packet_ptr acquire_packet(int const allocate);
	void release_packet(packet_ptr p);

	void set_state(state_t s);
	state_t state() const { return static_cast<state_t>(m_state); }

#if TORRENT_USE_INVARIANT_CHECKS
	void check_receive_buffers() const;
	void check_invariant() const;
#endif

	utp_socket_manager& m_sm;

	// userdata pointer passed along
	// with any callback. This is initialized to nullptr
	// then set to point to the utp_stream when
	// hooked up, and then reset to 0 once the utp_stream
	// detaches. This is used to know whether or not
	// the socket impl is still attached to a utp_stream
	// object. When it isn't, we'll never be able to
	// signal anything back to the client, and in case
	// of errors, we just have to delete ourselves
	// i.e. transition to the state_t::deleting state
	utp_stream* m_userdata;

	// if there's currently an async read or write
	// operation in progress, these buffers are initialized
	// and used, otherwise any bytes received are stuck in
	// m_receive_buffer until another read is made
	// as we flush from the write buffer, individual iovecs
	// are updated to only refer to unflushed portions of the
	// buffers. Buffers that empty are erased from the vector.
	std::vector<span<char const>> m_write_buffer;

	// if this is non nullptr, it's a packet. This packet was held off because
	// of NAGLE. We couldn't send it immediately. It's left
	// here to accrue more bytes before we send it.
	packet_ptr m_nagle_packet;

	// the user provided read buffer. If this has a size greater
	// than 0, we'll always prefer using it over putting received
	// data in the m_receive_buffer. As data is stored in the
	// read buffer, the iovec_t elements are adjusted to only
	// refer to the unwritten portions of the buffers, and the
	// ones that fill up are erased from the vector
	std::vector<iovec_t> m_read_buffer;

	// packets we've received without a read operation
	// active. Store them here until the client triggers
	// an async_read_some
	std::vector<packet_ptr> m_receive_buffer;

	// this is the error on this socket. If m_state is
	// set to state_t::error_wait, this error should be
	// forwarded to the client as soon as we have a new
	// async operation initiated
	error_code m_error;

	// these indicate whether or not there is an outstanding read/write or
	// connect operation. i.e. is there upper layer subscribed to these events.
	bool m_read_handler = false;
	bool m_write_handler = false;
	bool m_connect_handler = false;

	// the address of the remote endpoint
	address m_remote_address;

	// the send and receive buffers
	// maps packet sequence numbers
	packet_buffer m_inbuf;
	packet_buffer m_outbuf;

	// the time when the last packet we sent times out. Including re-sends.
	// if we ever end up not having sent anything in one second (
	// or one mean rtt + 2 average deviations, whichever is greater)
	// we set our cwnd to 1 MSS. This condition can happen either because
	// a packet has timed out and needs to be resent or because our
	// cwnd is set to less than one MSS during congestion control.
	// it can also happen if the other end sends an advertised window
	// size less than one MSS.
	time_point m_timeout;

	// the last time we stepped the timestamp history
	time_point m_last_history_step = clock_type::now();

	// the next time we allow a lost packet to halve cwnd. We only do this once every
	// 100 ms
	time_point m_next_loss;

	// the max number of bytes in-flight. This is a fixed point
	// value, to get the true number of bytes, shift right 16 bits
	// the value is always >= 0, but the calculations performed on
	// it in do_ledbat() are signed.
	std::int64_t m_cwnd = TORRENT_ETHERNET_MTU << 16;

	timestamp_history m_delay_hist;
	timestamp_history m_their_delay_hist;

	// the slow-start threshold. This is the congestion window size (m_cwnd)
	// in bytes the last time we left slow-start mode. This is used as a
	// threshold to leave slow-start earlier next time, to avoid packet-loss
	std::int32_t m_ssthres = 0;

	// the number of bytes we have buffered in m_inbuf
	std::int32_t m_buffered_incoming_bytes = 0;

	// the timestamp diff in the last packet received
	// this is what we'll send back
	std::uint32_t m_reply_micro = 0;

	// this is the advertised receive window the other end sent
	// we'll never have more un-acked bytes in flight
	// if this ever gets set to zero, we'll try one packet every
	// second until the window opens up again
	std::uint32_t m_adv_wnd = TORRENT_ETHERNET_MTU;

	// the number of un-acked bytes we have sent
	std::int32_t m_bytes_in_flight = 0;

	// the number of bytes read into the user provided
	// buffer. If this grows too big, we'll trigger the
	// read handler.
	std::int32_t m_read = 0;

	// the sum of the lengths of all iovec in m_write_buffer
	std::int32_t m_write_buffer_size = 0;

	// the number of bytes already written to packets
	// from m_write_buffer
	std::int32_t m_written = 0;

	// the sum of all packets stored in m_receive_buffer
	std::int32_t m_receive_buffer_size = 0;

	// the sum of all buffers in m_read_buffer
	std::int32_t m_read_buffer_size = 0;

	// max number of bytes to allocate for receive buffer
	std::int32_t m_receive_buffer_capacity = 1024 * 1024;

	// this holds the 3 last delay measurements,
	// these are the actual corrected delay measurements.
	// the lowest of the 3 last ones is used in the congestion
	// controller. This is to not completely close the cwnd
	// by a single outlier.
	std::array<std::uint32_t, 3> m_delay_sample_hist;

	// counters
	std::uint32_t m_in_packets = 0;
	std::uint32_t m_out_packets = 0;

	// the last send delay sample
	std::int32_t m_send_delay = 0;
	// the last receive delay sample
	std::int32_t m_recv_delay = 0;

	// average RTT
	sliding_average<int, 16> m_rtt;

	// if this is != 0, it means the upper layer provided a reason for why
	// the connection is being closed. The reason is indicated by this
	// non-zero value which is included in a packet header extension
	close_reason_t m_close_reason = close_reason_t::none;

	// port of destination endpoint
	std::uint16_t m_port = 0;

	std::uint16_t m_send_id;
	std::uint16_t m_recv_id;

	// this is the ack we're sending back. We have
	// received all packets up to this sequence number
	std::uint16_t m_ack_nr = 0;

	// the sequence number of the next packet
	// we'll send
	std::uint16_t m_seq_nr = 0;

	// this is the sequence number of the packet that
	// everything has been ACKed up to. Everything we've
	// sent up to this point has been received by the other
	// end.
	std::uint16_t m_acked_seq_nr = 0;

	// each packet gets one chance of "fast resend". i.e.
	// if we have multiple duplicate acks, we may send a
	// packet immediately, if m_fast_resend_seq_nr is set
	// to that packet's sequence number
	std::uint16_t m_fast_resend_seq_nr = 0;

	// this is the sequence number of the FIN packet
	// we've received. This sequence number is only
	// valid if m_eof is true. We should not accept
	// any packets beyond this sequence number from the
	// other end
	std::uint16_t m_eof_seq_nr = 0;

	// this is the lowest sequence number that, when lost,
	// will cause the window size to be cut in half
	std::uint16_t m_loss_seq_nr = 0;

	// the max number of bytes we can send in a packet
	// including the header
	std::uint16_t m_mtu = TORRENT_ETHERNET_MTU - TORRENT_IPV4_HEADER - TORRENT_UDP_HEADER - 8 - 24 - 36;

	// the floor is the largest packet that we have
	// been able to get through without fragmentation
	std::uint16_t m_mtu_floor = TORRENT_INET_MIN_MTU - TORRENT_IPV4_HEADER - TORRENT_UDP_HEADER;

	// the ceiling is the largest packet that we might
	// be able to get through without fragmentation.
	// i.e. ceiling +1 is very likely to not get through
	// or we have in fact experienced a drop or ICMP
	// message indicating that it is
	std::uint16_t m_mtu_ceiling = TORRENT_ETHERNET_MTU - TORRENT_IPV4_HEADER - TORRENT_UDP_HEADER;

	// the sequence number of the probe in-flight
	// this is 0 if there is no probe in flight
	std::uint16_t m_mtu_seq = 0;

	// this is a counter of how many times the current m_acked_seq_nr
	// has been ACKed. If it's ACKed more than 3 times, we assume the
	// packet with the next sequence number has been lost, and we trigger
	// a re-send. Obviously an ACK only counts as a duplicate as long as
	// we have outstanding packets following it.
	std::uint8_t m_duplicate_acks = 0;

	// the number of packet timeouts we've seen in a row
	// this affects the packet timeout time
	std::uint8_t m_num_timeouts = 0;

	// this is the cursor into m_delay_sample_hist
	std::uint8_t m_delay_sample_idx:2;

	// the state the socket is in
	std::uint8_t m_state:3;

	// this is set to true when we receive a fin
	bool m_eof:1;

	// is this socket state attached to a user space socket?
	bool m_attached:1;

	// this is true if nagle is enabled (which it is by default)
	bool m_nagle:1;

	// this is true while the socket is in slow start mode. It's
	// only in slow-start during the start-up phase. Slow start
	// (contrary to what its name suggest) means that we're growing
	// the congestion window (cwnd) exponentially rather than linearly.
	// this is done at startup of a socket in order to find its
	// link capacity faster. This behaves similar to TCP slow start
	bool m_slow_start:1;

	// this is true as long as we have as many packets in
	// flight as allowed by the congestion window (cwnd)
	bool m_cwnd_full:1;

	// this is set to one if the current read operation
	// has a null_buffer. i.e. we're not reading into a user-provided
	// buffer, we're just signalling when there's something
	// to read from our internal receive buffer
	bool m_null_buffers:1;

	// this is set to true when this socket has added itself to
	// the utp socket manager's list of deferred acks. Once the
	// burst of incoming UDP packets is all drained, the utp socket
	// manager will send acks for all sockets on this list.
	bool m_deferred_ack:1;

	// this is true if this socket has subscribed to be notified
	// when this receive round is done
	bool m_subscribe_drained:1;

	// if this socket tries to send a packet via the utp socket
	// manager, and it fails with EWOULDBLOCK, the socket
	// is stalled and this is set. It's also added to a list
	// of sockets in the utp_socket_manager to be notified of
	// the socket being writable again
	bool m_stalled:1;

	// this is false by default and set to true once we've received a non-SYN
	// packet for this connection with a correct ack_nr, confirming that the
	// other end is not spoofing its source IP
	bool m_confirmed:1;
};

}
}

#endif
