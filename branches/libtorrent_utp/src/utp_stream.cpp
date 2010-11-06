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

#include "libtorrent/utp_stream.hpp"
#include "libtorrent/sliding_average.hpp"
#include "libtorrent/utp_socket_manager.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/timestamp_history.hpp"
#include <boost/cstdint.hpp>

#define TORRENT_UTP_LOG 0
#define TORRENT_VERBOSE_UTP_LOG 0
#define TORRENT_UT_SEQ 1

#if TORRENT_UTP_LOG
#include <stdarg.h>
#include "libtorrent/socket_io.hpp"
#endif

namespace libtorrent {

#if TORRENT_UTP_LOG

char const* packet_type_names[] = { "ST_DATA", "ST_FIN", "ST_STATE", "ST_RESET", "ST_SYN" };
char const* socket_state_names[] = { "NONE", "SYN_SENT", "CONNECTED", "FIN_SENT", "ERROR", "DELETE" };

static struct utp_logger
{
	FILE* utp_log_file;

	utp_logger() : utp_log_file(0)
	{
		utp_log_file = fopen("utp.log", "w+");
	}
	~utp_logger()
	{
		if (utp_log_file) fclose(utp_log_file);
	}
} log_file_holder;

void utp_log(char const* fmt, ...)
{
	fprintf(log_file_holder.utp_log_file, "[%012"PRId64"] ", total_microseconds(time_now_hires() - min_time()));
	va_list l;
	va_start(l, fmt);
	vfprintf(log_file_holder.utp_log_file, fmt, l);
	va_end(l);
}

#define UTP_LOG utp_log
#if TORRENT_VERBOSE_UTP_LOG
#define UTP_LOGV utp_log
#else
#define UTP_LOGV if (false) printf
#endif

#else

#define UTP_LOG if (false) printf
#define UTP_LOGV if (false) printf

#endif

enum
{
	ACK_MASK = 0xffff,

	// the number of packets that'll fit in the reorder buffer
	max_packets_reorder = 512,
};

// compare if lhs is less than rhs, taking wrapping
// into account. if lhs is close to UINT_MAX and rhs
// is close to 0, lhs is assumed to have wrapped and
// considered smaller
TORRENT_EXPORT bool compare_less_wrap(boost::uint32_t lhs, boost::uint32_t rhs, boost::uint32_t mask)
{
	// distance walking from lhs to rhs, downwards
	boost::uint32_t dist_down = (lhs - rhs) & mask;
	// distance walking from lhs to rhs, upwards
	boost::uint32_t dist_up = (rhs - lhs) & mask;

	// if the distance walking up is shorter, lhs
	// is less than rhs. If the distance walking down
	// is shorter, then rhs is less than lhs
	return dist_up < dist_down;
}

// used for out-of-order incoming packets
// as well as sent packets that are waiting to be ACKed
struct packet
{
	// the last time this packet was sent
	ptime send_time;

	// the size of the buffer 'buf' pointst to
	boost::uint16_t size;

	// this is the offset to the payload inside the buffer
	// this is also used as a cursor to describe where the
	// next payload that hasn't been consumed yet starts
	boost::uint16_t header_size;
	
	// the number of times this packet has been sent
	boost::uint8_t num_transmissions:7;

	// true if we need to send this packet again. All
	// outstanding packets are marked as needing to be
	// resent on timeouts
	bool need_resend:1;

	// the actual packet buffer
	char buf[];
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
	utp_socket_impl(boost::uint16_t recv_id, boost::uint16_t send_id
		, void* userdata, utp_socket_manager* sm)
		: m_sm(sm)
		, m_userdata(userdata)
		, m_remote_address()
		, m_read_handler(0)
		, m_write_handler(0)
		, m_connect_handler(0)
		, m_read_timeout()
		, m_write_timeout()
		, m_timeout(time_now_hires() + milliseconds(m_sm->connect_timeout()))
		, m_last_cwnd_hit(min_time())
		, m_ack_timer(time_now() + minutes(10))
		, m_last_history_step(time_now_hires())
		, m_cwnd(1500 << 16)
		, m_buffered_incoming_bytes(0)
		, m_reply_micro(0)
		, m_adv_wnd(1500)
		, m_bytes_in_flight(0)
		, m_read(0)
		, m_write_buffer_size(0)
		, m_written(0)
		, m_receive_buffer_size(0)
		, m_read_buffer_size(0)
		, m_in_buf_size(100 * 1024 * 1024)
		, m_in_packets(0)
		, m_out_packets(0)
		, m_port(0)
		, m_send_id(send_id)
		, m_recv_id(recv_id)
		, m_ack_nr(0)
		, m_seq_nr(0)
		, m_acked_seq_nr(0)
		, m_fast_resend_seq_nr(0)
		, m_mtu(1500 - 20 - 8 - 8 - 24 - 36)
		, m_duplicate_acks(0)
		, m_num_timeouts(0)
		, m_eof_seq_nr(0)
		, m_delay_sample_idx(0)
		, m_state(UTP_STATE_NONE)
		, m_eof(false)
		, m_attached(true)
	{
		for (int i = 0; i != num_delay_hist; ++i)
			m_delay_sample_hist[i] = UINT_MAX;
	}

	~utp_socket_impl();

	void init(udp::endpoint const& ep, boost::uint16_t id, void* userdata
		, utp_socket_manager* sm)
	{
		m_remote_address = ep.address();
		m_port = ep.port();
		m_send_id = id + 1;
		m_recv_id = id;
		m_userdata = userdata;
		m_sm = sm;
	}

	void tick(ptime const& now);
	bool incoming_packet(char const* buf, int size
		, udp::endpoint const& ep, ptime receive_time);
	bool should_delete() const;
	tcp::endpoint remote_endpoint(error_code& ec) const
	{
		if (m_state == UTP_STATE_NONE)
			ec = asio::error::not_connected;
		else
			TORRENT_ASSERT(m_remote_address != address_v4::any());
		return tcp::endpoint(m_remote_address, m_port);
	}
	std::size_t available() const;
	void destroy();
	void detach();
	void send_syn();
	void send_fin();

	bool send_pkt(bool ack);
	bool resend_packet(packet* p);
	void send_reset(utp_header* ph);
	void parse_sack(boost::uint16_t packet_ack, char const* ptr, int size, int* acked_bytes
		, ptime const now, boost::uint32_t& min_rtt);
	void write_payload(char* ptr, int size);
	void ack_packet(packet* p, ptime const& receive_time
		, boost::uint32_t& min_rtt, boost::uint16_t seq_nr);
	void write_sack(char* buf, int size) const;
	void incoming(char const* buf, int size, packet* p, ptime now);
	void do_ledbat(int acked_bytes, int delay, int in_flight, ptime const now);
	int packet_timeout() const;
	bool test_socket_state();
	void maybe_trigger_receive_callback(ptime now);
	void maybe_trigger_send_callback(ptime now);
	bool cancel_handlers(error_code const& ec, bool kill);
	bool consume_incoming_data(
		utp_header const* ph, char const* ptr, int payload_size, ptime now);

	void check_receive_buffers() const;

	utp_socket_manager* m_sm;

	// userdata pointer passed along
	// with any callback. This is initialized to 0
	// then set to point to the utp_stream when
	// hooked up, and then reset to 0 once the utp_stream
	// detaches. This is used to know whether or not
	// the socket impl is still attached to a utp_stream
	// object. When it isn't, we'll never be able to
	// signal anything back to the client, and in case
	// of errors, we just have to delete ourselves
	// i.e. transition to the UTP_STATE_DELETED state
	void* m_userdata;

	// This is a platform-independent replacement
	// for the regular iovec type in posix. Since
	// it's not used in any system call, we might as
	// well define our own type instead of wrapping
	// the system's type.
	struct iovec_t
	{
		iovec_t(void* b, size_t l): buf(b), len(l) {}
		void* buf;
		size_t len;
	};

	// if there's currently an async read or write
	// operation in progress, these buffers are initialized
	// and used, otherwise any bytes received are stuck in
	// m_receive_buffer until another read is made
	// as we flush from the write buffer, individual iovecs
	// are updated to only refer to unflushed portions of the
	// buffers. Buffers that empty are erased from the vector.
	std::vector<iovec_t> m_write_buffer;

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
	std::vector<packet*> m_receive_buffer;

	// this is the error on this socket. If m_state is
	// set to UTP_STATE_ERROR_WAIT, this error should be
	// forwarded to the client as soon as we have a new
	// async operation initiated
	error_code m_error;

	// these are the callbacks made into the utp_stream object
	// on read/write/connect events
	utp_stream::handler_t m_read_handler;
	utp_stream::handler_t m_write_handler;
	utp_stream::connect_handler_t m_connect_handler;

	// the address of the remote endpoint
	address m_remote_address;

	// the send and receive buffers
	// maps packet sequence numbers
	packet_buffer m_inbuf;
	packet_buffer m_outbuf;

	// timers when we should trigger the read and
	// write callbacks (unless the buffers fill up
	// before)
	ptime m_read_timeout;
	ptime m_write_timeout;

	// the time when the last packet we sent times out. Including re-sends.
	// if we ever end up not having sent anything in one second (
	// or one mean rtt + 2 average deviations, whichever is greater)
	// we set our cwnd to 1 MSS. This condition can happen either because
	// a packet has timed out and needs to be resent or because our
	// cwnd is set to less than one MSS during congestion control.
	// it can also happen if the other end sends an advertized window
	// size less than one MSS.
	ptime m_timeout;
	
	// the last time we wanted to send more data, but couldn't because
	// it would bring the number of outstanding bytes above the cwnd.
	// this is used to restrict increasing the cwnd size when we're
	// not sending fast enough to need it bigger
	ptime m_last_cwnd_hit;

	// the next time we need to send an ACK the latest
	// updated every time we send an ACK and every time we
	// put off sending an ACK for a received packet
	ptime m_ack_timer;

	// the last time we stepped the timestamp history
	ptime m_last_history_step;

	// the max number of bytes in-flight. This is a fixed point
	// value, to get the true number of bytes, shift right 16 bits
	// the value is always >= 0, but the calculations performed on
	// it in do_ledbat() is signed.
	boost::int64_t m_cwnd;

	timestamp_history m_delay_hist;
	timestamp_history m_their_delay_hist;

	// the number of bytes we have buffered in m_inbuf
	boost::int32_t m_buffered_incoming_bytes;

	// the timestamp diff in the last packet received
	// this is what we'll send back
	boost::uint32_t m_reply_micro;

	// this is the advertized receive window the other end sent
	// we'll never have more un-acked bytes in flight
	// if this ever gets set to zero, we'll try one packet every
	// second until the window opens up again
	boost::uint32_t m_adv_wnd;

	// the number of un-acked bytes we have sent
	boost::int32_t m_bytes_in_flight;

	// the number of bytes read into the user provided
	// buffer. If this grows too big, we'll trigger the
	// read handler.
	boost::int32_t m_read;

	// the sum of the lengths of all iovec in m_write_buffer
	boost::int32_t m_write_buffer_size;

	// the number of bytes already written to packets
	// from m_write_buffer
	boost::int32_t m_written;

	// the sum of all packets stored in m_receive_buffer
	boost::int32_t m_receive_buffer_size;
	
	// the sum of all buffers in m_read_buffer
	boost::int32_t m_read_buffer_size;

	// max number of bytes to allocate for receive buffer
	boost::int32_t m_in_buf_size;

	// this holds the 3 last delay measurements,
	// these are the actual corrected delay measurements.
	// the lowest of the 3 last ones is used in the congestion
	// controller. This is to not completely close the cwnd
	// by a single outlier.
	enum { num_delay_hist = 3 };
	boost::uint32_t m_delay_sample_hist[num_delay_hist];

	// counters
	boost::uint32_t m_in_packets;
	boost::uint32_t m_out_packets;

	// average RTT
	sliding_average<16> m_rtt;

	// port of destination endpoint
	boost::uint16_t m_port;

	boost::uint16_t m_send_id;
	boost::uint16_t m_recv_id;

	// this is the ack we're sending back. We have
	// received all packets up to this sequence number
	boost::uint16_t m_ack_nr;

	// the sequence number of the next packet
	// we'll send
	boost::uint16_t m_seq_nr;

	// this is the sequence number of the packet that
	// everything has been ACKed up to. Everything we've
	// sent up to this point has been received by the other
	// end.
	boost::uint16_t m_acked_seq_nr;

	// each packet gets one chance of "fast resend". i.e.
	// if we have multiple duplicate acks, we may send a
	// packet immediately, if m_fast_resend_seq_nr is set
	// to that packet's sequence number
	boost::uint16_t m_fast_resend_seq_nr;

	// this is the sequence number of the FIN packet
	// we've received. This sequence number is only
	// valid if m_eof is true. We should not accept
	// any packets beyond this sequence number from the
	// other end
	boost::uint16_t m_eof_seq_nr;

	// the max number of bytes we can send in a packet
	// including the header
	boost::uint16_t m_mtu;

	// this is a counter of how many times the current m_acked_seq_nr
	// has been ACKed. If it's ACKed more than 3 times, we assume the
	// packet with the next sequence number has been lost, and we trigger
	// a re-send. Ovbiously an ACK only counts as a duplicate as long as
	// we have outstanding packets following it.
	boost::uint8_t m_duplicate_acks;
	// #error implement nagle

	// the number of packet timeouts we've seen in a row
	// this affects the packet timeout time
	boost::uint8_t m_num_timeouts;

	enum state_t {
		// not yet connected
		UTP_STATE_NONE,
		// sent a syn packet, not received any acks
		UTP_STATE_SYN_SENT,
		// syn-ack received and in normal operation
		// of sending and receiving data
		UTP_STATE_CONNECTED,
		// fin sent, but all packets up to the fin packet
		// have not yet been acked. We might still be waiting
		// for a FIN from the other end
		UTP_STATE_FIN_SENT,

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
		UTP_STATE_ERROR_WAIT,

		// there are no more references to this socket
		// and we can delete it
		UTP_STATE_DELETE
	};

	// this is the cursor into m_delay_sample_hist
	boost::uint8_t m_delay_sample_idx:2;

	// the state the socket is in
	boost::uint8_t m_state:3;

	// this is set to true when we receive a fin
	bool m_eof:1;

	// is this socket state attached to a user space socket?
	bool m_attached:1;

};

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
int socket_impl_size() { return sizeof(utp_socket_impl); }
#endif

utp_socket_impl* construct_utp_impl(boost::uint16_t recv_id
	, boost::uint16_t send_id, void* userdata
	, utp_socket_manager* sm)
{
	return new utp_socket_impl(recv_id, send_id, userdata, sm);
}

void detach_utp_impl(utp_socket_impl* s)
{
	s->detach();
}

void delete_utp_impl(utp_socket_impl* s)
{
	delete s;
}

bool should_delete(utp_socket_impl* s)
{
	return s->should_delete();
}

void tick_utp_impl(utp_socket_impl* s, ptime const& now)
{
	s->tick(now);
}

bool utp_incoming_packet(utp_socket_impl* s, char const* p
	, int size, udp::endpoint const& ep, ptime receive_time)
{
	return s->incoming_packet(p, size, ep, receive_time);
}

bool utp_match(utp_socket_impl* s, udp::endpoint const& ep, boost::uint16_t id)
{
	return s->m_remote_address == ep.address()
		&& s->m_port == ep.port()
		&& s->m_recv_id == id;
}

udp::endpoint utp_remote_endpoint(utp_socket_impl* s)
{
	return udp::endpoint(s->m_remote_address, s->m_port);
}

boost::uint16_t utp_receive_id(utp_socket_impl* s)
{
	return s->m_recv_id;
}

int utp_socket_state(utp_socket_impl const* s)
{
	return s->m_state;
}

utp_stream::utp_stream(asio::io_service& io_service)
	: m_io_service(io_service)
	, m_impl(0)
	, m_open(false)
{
}

utp_socket_impl* utp_stream::get_impl()
{
	return m_impl;
}

void utp_stream::close()
{
	if (!m_impl) return;
	m_impl->destroy();
}

std::size_t utp_stream::available() const
{
	return m_impl->available();
}

utp_stream::endpoint_type utp_stream::remote_endpoint(error_code& ec) const
{
	if (!m_impl)
	{
		ec = asio::error::not_connected;
		return endpoint_type();
	}
	return m_impl->remote_endpoint(ec);
}

utp_stream::endpoint_type utp_stream::local_endpoint(error_code& ec) const
{
	if (m_impl == 0 || m_impl->m_sm == 0)
	{
		ec = asio::error::not_connected;
		return endpoint_type();
	}
	return m_impl->m_sm->local_endpoint(ec);
}

utp_stream::~utp_stream()
{
	if (m_impl)
	{
		UTP_LOGV("%8p: utp_stream destructed\n", m_impl);
		m_impl->destroy();
		detach_utp_impl(m_impl);
	}

	m_impl = 0;
}

void utp_stream::set_impl(utp_socket_impl* impl)
{
	TORRENT_ASSERT(m_impl == 0);
	TORRENT_ASSERT(!m_open);
	m_impl = impl;
	m_open = true;
}

int utp_stream::read_buffer_size() const
{
	TORRENT_ASSERT(m_impl);
	return m_impl->m_receive_buffer_size;
}

void utp_stream::on_read(void* self, size_t bytes_transferred, error_code const& ec, bool kill)
{
	utp_stream* s = (utp_stream*)self;

	UTP_LOGV("%8p: calling read handler read:%d ec:%s kill:%d\n", s->m_impl
		, int(bytes_transferred), ec.message().c_str(), kill);

	TORRENT_ASSERT(s->m_read_handler);
	s->m_io_service.post(boost::bind<void>(s->m_read_handler, ec, bytes_transferred));
	s->m_read_handler.clear();
	if (kill && s->m_impl)
	{
		detach_utp_impl(s->m_impl);
		s->m_impl = 0;
	}
}

void utp_stream::on_write(void* self, size_t bytes_transferred, error_code const& ec, bool kill)
{
	utp_stream* s = (utp_stream*)self;

	UTP_LOGV("%8p: calling write handler written:%d ec:%s kill:%d\n", s->m_impl
		, int(bytes_transferred), ec.message().c_str(), kill);

	TORRENT_ASSERT(s->m_write_handler);
	s->m_io_service.post(boost::bind<void>(s->m_write_handler, ec, bytes_transferred));
	s->m_write_handler.clear();
	if (kill && s->m_impl)
	{
		detach_utp_impl(s->m_impl);
		s->m_impl = 0;
	}
}

void utp_stream::on_connect(void* self, error_code const& ec, bool kill)
{
	utp_stream* s = (utp_stream*)self;

	UTP_LOGV("%8p: calling connect handler ec:%s kill:%d\n"
		, s->m_impl, ec.message().c_str(), kill);

	TORRENT_ASSERT(s->m_connect_handler);
	s->m_io_service.post(boost::bind<void>(s->m_connect_handler, ec));
	s->m_connect_handler.clear();
	if (kill && s->m_impl)
	{
		detach_utp_impl(s->m_impl);
		s->m_impl = 0;
	}
}

void utp_stream::add_read_buffer(void* buf, size_t len)
{
	TORRENT_ASSERT(m_impl);
	TORRENT_ASSERT(len < INT_MAX);
	m_impl->m_read_buffer.push_back(utp_socket_impl::iovec_t(buf, len));
	m_impl->m_read_buffer_size += len;

	UTP_LOGV("%8p: add_read_buffer %d bytes\n", m_impl, int(len));
}

// this is the wrapper to add a user provided write buffer to the
// utp_socket_impl. It makes sure the m_write_buffer_size is kept
// up to date
void utp_stream::add_write_buffer(void const* buf, size_t len)
{
	TORRENT_ASSERT(m_impl);

#ifndef NDEBUG
	int write_buffer_size = 0;
	for (std::vector<utp_socket_impl::iovec_t>::iterator i = m_impl->m_write_buffer.begin()
		, end(m_impl->m_write_buffer.end()); i != end; ++i)
	{
		write_buffer_size += i->len;
	}
	TORRENT_ASSERT(m_impl->m_write_buffer_size == write_buffer_size);
#endif

	m_impl->m_write_buffer.push_back(utp_socket_impl::iovec_t((void*)buf, len));
	m_impl->m_write_buffer_size += len;

#ifndef NDEBUG
	write_buffer_size = 0;
	for (std::vector<utp_socket_impl::iovec_t>::iterator i = m_impl->m_write_buffer.begin()
		, end(m_impl->m_write_buffer.end()); i != end; ++i)
	{
		write_buffer_size += i->len;
	}
	TORRENT_ASSERT(m_impl->m_write_buffer_size == write_buffer_size);
#endif

	UTP_LOGV("%8p: add_write_buffer %d bytes\n", m_impl, int(len));
}

// this is called when all user provided read buffers have been added
// and it's time to execute the async operation. The first thing we
// do is to copy any data stored in m_receive_buffer into the user
// provided buffer. This might be enough to in turn trigger the read
// handler immediately.
void utp_stream::set_read_handler(handler_t h)
{
	m_impl->m_read_handler = h;
	if (m_impl->test_socket_state()) return;

	UTP_LOGV("%8p: new read handler. %d bytes in buffer\n"
		, m_impl, m_impl->m_receive_buffer_size);

	// so, the client wants to read. If we already
	// have some data in the read buffer, move it into the
	// client's buffer right away

	m_impl->m_read += read_some(false);
	m_impl->maybe_trigger_receive_callback(time_now_hires());
}

size_t utp_stream::read_some(bool clear_buffers)
{
	if (m_impl->m_receive_buffer_size == 0)
	{
		if (clear_buffers)
		{
			m_impl->m_read_buffer_size = 0;
			m_impl->m_read_buffer.clear();
		}
		return 0;
	}

	std::vector<utp_socket_impl::iovec_t>::iterator target = m_impl->m_read_buffer.begin();

	size_t ret = 0;

	int pop_packets = 0;
	for (std::vector<packet*>::iterator i = m_impl->m_receive_buffer.begin()
		, end(m_impl->m_receive_buffer.end()); i != end;)
	{
		if (target == m_impl->m_read_buffer.end()) 
		{
			UTP_LOGV("  No more target buffers: %d bytes left in buffer\n"
				, m_impl->m_receive_buffer_size);
			TORRENT_ASSERT(m_impl->m_read_buffer.empty());
			break;
		}

		m_impl->check_receive_buffers();

		packet* p = *i;
		int to_copy = (std::min)(p->size - p->header_size, int(target->len));
		TORRENT_ASSERT(to_copy >= 0);
		memcpy(target->buf, p->buf + p->header_size, to_copy);
		ret += to_copy;
		target->buf = ((char*)target->buf) + to_copy;
		TORRENT_ASSERT(target->len >= to_copy);
		target->len -= to_copy;
		m_impl->m_receive_buffer_size -= to_copy;
		TORRENT_ASSERT(m_impl->m_read_buffer_size >= to_copy);
		m_impl->m_read_buffer_size -= to_copy;
		p->header_size += to_copy;
		if (target->len == 0) target = m_impl->m_read_buffer.erase(target);

		m_impl->check_receive_buffers();

		TORRENT_ASSERT(m_impl->m_receive_buffer_size >= 0);

		// Consumed entire packet
		if (p->header_size == p->size)
		{
			free(p);
			++pop_packets;
			*i = 0;
			++i;
		}

		if (m_impl->m_receive_buffer_size == 0)
		{
			UTP_LOGV("  Didn't fill entire target: %d bytes left in buffer\n"
				, m_impl->m_receive_buffer_size);
			break;
		}
	}
	// remove the packets from the receive_buffer that we already copied over
	// and freed
	m_impl->m_receive_buffer.erase(m_impl->m_receive_buffer.begin()
		, m_impl->m_receive_buffer.begin() + pop_packets);
	// we exited either because we ran out of bytes to copy
	// or because we ran out of space to copy the bytes to
	TORRENT_ASSERT(m_impl->m_receive_buffer_size == 0
		|| m_impl->m_read_buffer.empty());

	UTP_LOGV("%8p: %d packets moved from buffer to user space\n"
		, m_impl, pop_packets);

	if (clear_buffers)
	{
		 m_impl->m_read_buffer_size = 0;
		 m_impl->m_read_buffer.clear();
	}
	TORRENT_ASSERT(ret > 0);
	return ret;
}

// this is called when all user provided write buffers have been
// added. Start trying to send packets with the payload immediately.
void utp_stream::set_write_handler(handler_t h)
{
	UTP_LOGV("%8p: new write handler. %d bytes to write\n"
		, m_impl, m_impl->m_write_buffer_size);

	m_impl->m_write_handler = h;
	m_impl->m_written = 0;
	if (m_impl->test_socket_state()) return;
	// try to write. send_pkt returns false if there's
	// no more payload to send or if the congestion window
	// is full and we can't send more packets right now
	while (m_impl->send_pkt(false));
}

void utp_stream::do_connect(tcp::endpoint const& ep, utp_stream::connect_handler_t handler)
{
	TORRENT_ASSERT(m_impl->m_connect_handler == 0);
	m_impl->m_remote_address = ep.address();
	m_impl->m_port = ep.port();
	m_impl->m_connect_handler = handler;

	if (m_impl->test_socket_state()) return;
	m_impl->send_syn();
}

// =========== utp_socket_impl ============

utp_socket_impl::~utp_socket_impl()
{
	TORRENT_ASSERT(!m_attached);

	UTP_LOGV("%8p: destroying utp socket state\n", this);

	// free any buffers we're holding
	for (boost::uint16_t i = m_inbuf.cursor(), end((m_inbuf.cursor()
		+ m_inbuf.capacity()) & ACK_MASK);
		i != end; i = (i + 1) & ACK_MASK)
	{
		void* p = m_inbuf.remove(i);
		free(p);
	}
	for (boost::uint16_t i = m_outbuf.cursor(), end((m_outbuf.cursor()
		+ m_outbuf.capacity()) & ACK_MASK);
		i != end; i = (i + 1) & ACK_MASK)
	{
		void* p = m_outbuf.remove(i);
		free(p);
	}

	for (std::vector<packet*>::iterator i = m_receive_buffer.begin()
		, end = m_receive_buffer.end(); i != end; ++i)
	{
		free(*i);
	}
}

bool utp_socket_impl::should_delete() const
{
	// if the socket state is not attached anymore we're free
	// to delete it from the client's point of view. The other
	// endpoint however might still need to be told that we're
	// closing the socket. Only delete the state if we're not
	// attached and we're in a state where the other end doesn't
	// expect the socket to still be alive
	bool ret = (m_state >= UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_NONE)
		&& !m_attached;

	if (ret)
	{
		UTP_LOGV("%8p: should_delete() = true\n", this);
	}

	return ret;
}

void utp_socket_impl::maybe_trigger_receive_callback(ptime now)
{
	// nothing has been read or there's no outstanding read operation
	if (m_read == 0 || m_read_handler == 0) return;

	if (m_read > 10000 || m_read_buffer_size == 0 || now >= m_read_timeout)
	{
		UTP_LOGV("%8p: calling read handler read:%d\n", this, m_read);
		m_read_handler(m_userdata, m_read, m_error, false);
		m_read_handler = 0;
		m_read = 0;
		m_read_buffer_size = 0;
		m_read_buffer.clear();
	}
}

void utp_socket_impl::maybe_trigger_send_callback(ptime now)
{
	// nothing has been written or there's no outstanding write operation
	if (m_written == 0 || m_write_handler == 0) return;

	if (m_written > 10000 || m_write_buffer_size == 0 || now >= m_write_timeout)
	{
		UTP_LOGV("%8p: calling write handler written:%d\n", this, m_written);

		m_write_handler(m_userdata, m_written, m_error, false);
		m_write_handler = 0;
		m_written = 0;
		m_write_buffer_size = 0;
		m_write_buffer.clear();
	}
}

void utp_socket_impl::destroy()
{
#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: destroy state:%s\n", this, socket_state_names[m_state]);
#endif

	if (m_userdata == 0) return;

	m_error = asio::error::operation_aborted;
	cancel_handlers(m_error, true);

	m_userdata = 0;
	m_read_buffer.clear();
	m_read_buffer_size = 0;

	m_write_buffer.clear();
	m_write_buffer_size = 0;

	if (m_state == UTP_STATE_ERROR_WAIT
		|| m_state == UTP_STATE_NONE
		|| m_state == UTP_STATE_SYN_SENT)
	{
		m_state = UTP_STATE_DELETE;
#if TORRENT_UTP_LOG
		UTP_LOGV("%8p: state:%s\n", this, socket_state_names[m_state]);
#endif
		return;
	}

	// you should never close a socket with an outstanding write!
	TORRENT_ASSERT(!m_write_handler);

	if (m_state == UTP_STATE_CONNECTED)
		send_fin();

	// #error our end is closing. Wait for everything to be acked
}

void utp_socket_impl::detach()
{
	UTP_LOGV("%8p: detach()\n", this);
	m_attached = false;
}

void utp_socket_impl::send_syn()
{
	m_seq_nr = rand();
	m_acked_seq_nr = (m_seq_nr - 1) & ACK_MASK;
	m_ack_nr = 0;

	packet* p = (packet*)malloc(sizeof(packet) + sizeof(utp_header));
	p->size = sizeof(utp_header);
	p->header_size = sizeof(utp_header);
	p->num_transmissions = 1;
	p->need_resend = false;
	utp_header* h = (utp_header*)p->buf;
	h->ver = 1;
	h->type = ST_SYN;
	h->extension = 0;
	// using recv_id here is intentional! This is an odd
	// thing in uTP. The syn packet is sent with the connection
	// ID that it expects to receive the syn ack on. All
	// subsequent connection IDs will be this plus one.
	h->connection_id = m_recv_id;
	h->timestamp_difference_microseconds = m_reply_micro;
	h->wnd_size = 0;
	h->seq_nr = m_seq_nr;
	h->ack_nr = 0;

	ptime now = time_now_hires();
	p->send_time = now;
	h->timestamp_microseconds = boost::uint32_t(total_microseconds(now - min_time()));

#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: send_syn seq_nr:%d id:%d target:%s\n"
		, this, int(m_seq_nr), int(m_recv_id)
		, print_endpoint(udp::endpoint(m_remote_address, m_port)).c_str());
#endif

	TORRENT_ASSERT(!m_error);
	m_sm->send_packet(udp::endpoint(m_remote_address, m_port), (char const*)h
		, sizeof(utp_header), m_error);
	if (m_error)
	{
		free(p);
		m_state = UTP_STATE_ERROR_WAIT;
		test_socket_state();
		return;
	}

	TORRENT_ASSERT(!m_outbuf.at(m_seq_nr));
	m_outbuf.insert(m_seq_nr, p);

	m_seq_nr = (m_seq_nr + 1) & ACK_MASK;

	m_state = UTP_STATE_SYN_SENT;
#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: state:%s\n", this, socket_state_names[m_state]);
#endif
}

void utp_socket_impl::send_fin()
{
	TORRENT_ASSERT(m_state != UTP_STATE_FIN_SENT);

	// we need a heap allocated packet in order to stick it
	// in the send buffer, so that we can resend it
	packet* p = (packet*)malloc(sizeof(packet) + sizeof(utp_header));

	p->size = sizeof(utp_header);
	p->header_size = sizeof(utp_header);
	p->num_transmissions = 1;
	p->need_resend = false;
	utp_header* h = (utp_header*)p->buf;

	h->ver = 1;
	h->type = ST_FIN;
	h->extension = 0;
	h->connection_id = m_send_id;
	h->timestamp_difference_microseconds = m_reply_micro;
	h->wnd_size = m_in_buf_size - m_buffered_incoming_bytes - m_receive_buffer_size;
	h->seq_nr = m_seq_nr;
	h->ack_nr = m_ack_nr;

	ptime now = time_now_hires();
	p->send_time = now;
	h->timestamp_microseconds = boost::uint32_t(total_microseconds(now - min_time()));

	m_sm->send_packet(udp::endpoint(m_remote_address, m_port)
		, (char const*)h, sizeof(utp_header), m_error);

#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: sending FIN seq_nr:%d ack_nr:%d type:%s "
		"id:%d target:%s size:%d error:%s send_buffer_size:%d\n"
		, this, int(h->seq_nr), int(h->ack_nr), packet_type_names[h->type]
		, m_send_id, print_endpoint(udp::endpoint(m_remote_address, m_port)).c_str()
		, int(sizeof(utp_header)), m_error.message().c_str(), m_write_buffer_size);
#endif

	if (m_error)
	{
		m_state = UTP_STATE_ERROR_WAIT;
		test_socket_state();
	}

#if !TORRENT_UT_SEQ
	// if the other end closed the connection immediately
	// our FIN packet will end up having the same sequence
	// number as the SYN, so this assert is invalid
	TORRENT_ASSERT(!m_outbuf.at(m_seq_nr));
#endif

	packet* old = (packet*)m_outbuf.insert(m_seq_nr, p);
	if (old)
	{
		if (!old->need_resend) m_bytes_in_flight -= old->size - old->header_size;
		free(old);
	}
	m_seq_nr = (m_seq_nr + 1) & ACK_MASK;

	m_state = UTP_STATE_FIN_SENT;

#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: state:%s\n", this, socket_state_names[m_state]);
#endif
}

void utp_socket_impl::send_reset(utp_header* ph)
{
	utp_header h;
	h.ver = 1;
	h.type = ST_RESET;
	h.extension = 0;
	h.connection_id = m_send_id;
	h.timestamp_difference_microseconds = m_reply_micro;
	h.wnd_size = 0;
	h.seq_nr = rand();
	h.ack_nr = ph->seq_nr;
	ptime now = time_now_hires();
	h.timestamp_microseconds = boost::uint32_t(total_microseconds(now - min_time()));

	UTP_LOGV("%8p: send_reset seq_nr:%d id:%d ack_nr:%d\n"
		, this, int(h.seq_nr), int(m_send_id), int(ph->seq_nr));

	// ignore errors here
	error_code ec;
	m_sm->send_packet(udp::endpoint(m_remote_address, m_port), (char const*)&h, sizeof(h), ec);
}

std::size_t utp_socket_impl::available() const
{
	return m_receive_buffer_size;
}

void utp_socket_impl::parse_sack(boost::uint16_t packet_ack, char const* ptr
	, int size, int* acked_bytes, ptime const now, boost::uint32_t& min_rtt)
{
	if (size == 0) return;

	// this is the sequence number the current bit represents
	int ack_nr = (packet_ack + 2) & ACK_MASK;

#if TORRENT_UTP_LOG
	std::string bitmask;
	for (char const* b = ptr, *end = ptr + size; b != end; ++b)
	{
		unsigned char bitfield = unsigned(*b);
		unsigned char mask = 1;
		// for each bit
		for (int i = 0; i < 8; ++i)
		{
			bitmask += (mask & bitfield) ? "1" : "0";
			mask <<= 1;
		}
	}
	UTP_LOGV("%8p: got SACK first:%d %s our_seq_nr:%u\n"
		, this, ack_nr, bitmask.c_str(), m_seq_nr);
#endif


	// for each byte
	for (char const* end = ptr + size; ptr != end; ++ptr)
	{
		unsigned char bitfield = unsigned(*ptr);
		unsigned char mask = 1;
		// for each bit
		for (int i = 0; i < 8; ++i)
		{
			if (mask & bitfield)
			{
				// this bit was set, ack_nr was received
				packet* p = (packet*)m_outbuf.remove(ack_nr);
				if (p)
				{
					acked_bytes += p->size - p->header_size;
					// each ACKed packet counts as a duplicate ack
					++m_duplicate_acks;
					ack_packet(p, now, min_rtt, ack_nr);
				}
			}

			mask <<= 1;
			ack_nr = (ack_nr + 1) & ACK_MASK;

			// we haven't sent packets past this point.
			// if there are any more bits set, we have to
			// ignore them anyway
			if (ack_nr == m_seq_nr) return;
		}
	}
}

// copies data from the write buffer into the packet
// pointed to by ptr
void utp_socket_impl::write_payload(char* ptr, int size)
{
#ifndef NDEBUG
	int write_buffer_size = 0;
	for (std::vector<iovec_t>::iterator i = m_write_buffer.begin()
		, end(m_write_buffer.end()); i != end; ++i)
	{
		write_buffer_size += i->len;
	}
	TORRENT_ASSERT(m_write_buffer_size == write_buffer_size);
#endif
	TORRENT_ASSERT(!m_write_buffer.empty() || size == 0);
	TORRENT_ASSERT(m_write_buffer_size >= size);
	std::vector<iovec_t>::iterator i = m_write_buffer.begin();

	if (size == 0) return;

	ptime now = time_now_hires();

	int buffers_to_clear = 0;
	while (size > 0)
	{
		// i points to the iovec we'll start copying from
		int to_copy = (std::min)(size, int(i->len));
		memcpy(ptr, static_cast<char const*>(i->buf), to_copy);
		size -= to_copy;
		if (m_written == 0)
		{
			m_write_timeout = now + milliseconds(100);
			UTP_LOGV("%8p: setting write timeout to 100 ms from now\n", this);
		}
		m_written += to_copy;
		ptr += to_copy;
		i->len -= to_copy;
		TORRENT_ASSERT(m_write_buffer_size >= to_copy);
		m_write_buffer_size -= to_copy;
		((char const*&)i->buf) += to_copy;
		if (i->len == 0) ++buffers_to_clear;
		++i;
	}

	if (buffers_to_clear)
		m_write_buffer.erase(m_write_buffer.begin()
			, m_write_buffer.begin() + buffers_to_clear);

#ifndef NDEBUG
	write_buffer_size = 0;
	for (std::vector<iovec_t>::iterator i = m_write_buffer.begin()
		, end(m_write_buffer.end()); i != end; ++i)
	{
		write_buffer_size += i->len;
	}
	TORRENT_ASSERT(m_write_buffer_size == write_buffer_size);
#endif
	maybe_trigger_send_callback(now);
}

// sends a packet, pulls data from the write buffer (if there's any)
// if ack is true, we need to send a packet regardless of if there's
// any data. Returns true if we could send more data (i.e. call
// send_pkt() again)
bool utp_socket_impl::send_pkt(bool ack)
{
	// This assert is bad because we call this function to ack
	// received FIN when we're in UTP_STATE_FIN_SENT.
	//
	// TORRENT_ASSERT(m_state != UTP_STATE_FIN_SENT);

	// first see if we need to resend any packets

	for (int i = (m_acked_seq_nr + 1) & ACK_MASK; i != m_seq_nr; i = (i + 1) & ACK_MASK)
	{
		packet* p = (packet*)m_outbuf.at(i);
		if (!p) continue;
		if (!p->need_resend) continue;
		if (!resend_packet(p))
		{
			// we couldn't resend the packet. It probably doesn't
			// fit in our cwnd. If ack is set, we need to continue
			// to send our ack anyway, if we don't have to send an
			// ack, we might as well return
			if (!ack) return false;
			break;
		}

		// don't fast-resend this packet
		if (m_fast_resend_seq_nr == i)
			m_fast_resend_seq_nr = (m_fast_resend_seq_nr + 1) & ACK_MASK;
	}

	bool ret = false;

	int sack = 0;
	if (m_inbuf.size())
	{
		// #error find out how big the SACK bitfield needs to be
		sack = 4;
	}

	int header_size = sizeof(utp_header) + (sack ? sack + 2 : 0);
	int payload_size = m_write_buffer_size;
	if (m_mtu - header_size < payload_size)
	{
		payload_size = m_mtu - header_size;
		ret = true; // there's more data to send
	}

	// if we have one MSS worth of data, make sure it fits in our
	// congestion window and the advertized receive window from
	// the other end.
	if (m_bytes_in_flight + payload_size > (std::min)(int(m_cwnd >> 16), int(m_adv_wnd) - m_bytes_in_flight))
	{
		// this means there's not enough room in the send window for
		// another packet. We have to hold off sending this data.
		// we still need to send an ACK though
		payload_size = 0;

		// we're restrained by the window size
		m_last_cwnd_hit = time_now_hires();

		// there's no more space in the cwnd, no need to
		// try to send more right now
		ret = false;

		UTP_LOGV("%8p: no space in window send_buffer_size:%d cwnd:%d "
			"ret:%d adv_wnd:%d in-flight:%d mtu:%d\n"
			, this, m_write_buffer_size, int(m_cwnd >> 16)
			, ret, m_adv_wnd, m_bytes_in_flight, m_mtu);
	}

	// if we don't have any data to send, or can't send any data
	// and we don't have any data to ack, don't send a packet
	if (payload_size == 0 && !ack)
	{
#if TORRENT_UTP_LOG
		UTP_LOGV("%8p: skipping send seq_nr:%d ack_nr:%d "
			"id:%d target:%s header_size:%d error:%s send_buffer_size:%d cwnd:%d "
			"ret:%d adv_wnd:%d in-flight:%d mtu:%d\n"
			, this, int(m_seq_nr), int(m_ack_nr)
			, m_send_id, print_endpoint(udp::endpoint(m_remote_address, m_port)).c_str()
			, header_size, m_error.message().c_str(), m_write_buffer_size, int(m_cwnd >> 16)
			, int(ret), m_adv_wnd, m_bytes_in_flight, m_mtu);
#endif
		return false;
	}

	int packet_size = header_size + payload_size;

	packet* p;
	// we only need a heap allocation if we have payload and
	// need to keep the packet around (in the outbuf)
	if (payload_size) p = (packet*)malloc(sizeof(packet) + packet_size);
	else p = (packet*)TORRENT_ALLOCA(char, sizeof(packet) + packet_size);

	p->size = packet_size;
	p->header_size = packet_size - payload_size;
	p->num_transmissions = 1;
	p->need_resend = false;
	char* ptr = p->buf;
	utp_header* h = (utp_header*)ptr;
	ptr += sizeof(utp_header);

	h->ver = 1;
	h->type = payload_size ? ST_DATA : ST_STATE;
	h->extension = sack ? 1 : 0;
	h->connection_id = m_send_id;
	h->timestamp_difference_microseconds = m_reply_micro;
	h->wnd_size = m_in_buf_size - m_buffered_incoming_bytes - m_receive_buffer_size;
	// seq_nr is ignored for ST_STATE packets, so it doesn't
	// matter that we say this is a sequence number we haven't
	// actually sent yet
	h->seq_nr = m_seq_nr;
	h->ack_nr = m_ack_nr;

	if (sack)
	{
		*ptr++ = 0; // end of extension chain
		*ptr++ = sack; // bytes for SACK bitfield
		write_sack(ptr, sack);
		ptr += sack;
	}

	write_payload(ptr, payload_size);

	// fill in the timestamp as late as possible
	ptime now = time_now_hires();
	p->send_time = now;
	h->timestamp_microseconds = boost::uint32_t(total_microseconds(now - min_time()));

#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: sending packet seq_nr:%d ack_nr:%d type:%s "
		"id:%d target:%s size:%d error:%s send_buffer_size:%d cwnd:%d "
		"ret:%d adv_wnd:%d in-flight:%d mtu:%d timestamp:%u time_diff:%u\n"
		, this, int(h->seq_nr), int(h->ack_nr), packet_type_names[h->type]
		, m_send_id, print_endpoint(udp::endpoint(m_remote_address, m_port)).c_str()
		, packet_size, m_error.message().c_str(), m_write_buffer_size, int(m_cwnd >> 16)
		, ret, m_adv_wnd, m_bytes_in_flight, m_mtu, boost::uint32_t(h->timestamp_microseconds)
		, boost::uint32_t(h->timestamp_difference_microseconds));
#endif

	m_sm->send_packet(udp::endpoint(m_remote_address, m_port)
		, (char const*)h, packet_size, m_error);

	++m_out_packets;

	if (m_error)
	{
		m_state = UTP_STATE_ERROR_WAIT;
		test_socket_state();
	}

	// we just sent a packet. this means we just ACKed the last received
	// packet as well. So, we can now reset the delayed ack timer to
	// not trigger for a long time
	m_ack_timer = now + minutes(10);

	// if we have payload, we need to save the packet until it's acked
	// and progress m_seq_nr
	if (payload_size)
	{
#if !TORRENT_UT_SEQ
		// if the other end closed the connection immediately
		// our FIN packet will end up having the same sequence
		// number as the SYN, so this assert is invalid
		TORRENT_ASSERT(!m_outbuf.at(m_seq_nr));
#endif
		packet* old = (packet*)m_outbuf.insert(m_seq_nr, p);
		if (old)
		{
			if (!old->need_resend) m_bytes_in_flight -= old->size - old->header_size;
			free(old);
		}
		m_seq_nr = (m_seq_nr + 1) & ACK_MASK;
		TORRENT_ASSERT(payload_size >= 0);
		m_bytes_in_flight += payload_size;
	}

	return ret;
}

// size is in bytes
void utp_socket_impl::write_sack(char* buf, int size) const
{
	TORRENT_ASSERT(m_inbuf.size());
	int ack_nr = (m_ack_nr + 2) & ACK_MASK;
	char* end = buf + size;

	for (; buf != end; ++buf)
	{
		*buf = 0;
		int mask = 1;
		for (int i = 0; i < 8; ++i)
		{
			if (m_inbuf.at(ack_nr)) *buf |= mask;
			mask <<= 1;
			ack_nr = (ack_nr + 1) & ACK_MASK;
		}
	}
}

bool utp_socket_impl::resend_packet(packet* p)
{
	TORRENT_ASSERT(p->need_resend);

	// we can only resend the packet if there's
	// enough space in our congestion window
	int window_size_left = (std::min)(int(m_cwnd >> 16), int(m_adv_wnd)) - m_bytes_in_flight;
	if (p->size - p->header_size > window_size_left)
	{
		m_last_cwnd_hit = time_now_hires();
		return false;
	}

	TORRENT_ASSERT(p->num_transmissions < m_sm->num_resends());

	TORRENT_ASSERT(p->size - p->header_size >= 0);
	if (p->need_resend) m_bytes_in_flight += p->size - p->header_size;

	++p->num_transmissions;
	p->need_resend = false;
	utp_header* h = (utp_header*)p->buf;
	// update packet header
	h->timestamp_difference_microseconds = m_reply_micro;
	p->send_time = time_now_hires();
	h->timestamp_microseconds = boost::uint32_t(total_microseconds(p->send_time - min_time()));

#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: re-sending packet seq_nr:%d ack_nr:%d type:%s "
		"id:%d target:%s size:%d error:%s send_buffer_size:%d cwnd:%d "
		"adv_wnd:%d in-flight:%d mtu:%d timestamp:%u time_diff:%u\n"
		, this, int(h->seq_nr), int(h->ack_nr), packet_type_names[h->type]
		, m_send_id, print_endpoint(udp::endpoint(m_remote_address, m_port)).c_str()
		, p->size, m_error.message().c_str(), m_write_buffer_size, int(m_cwnd >> 16)
		, m_adv_wnd, m_bytes_in_flight, m_mtu, boost::uint32_t(h->timestamp_microseconds)
		, boost::uint32_t(h->timestamp_difference_microseconds));
#endif

	m_sm->send_packet(udp::endpoint(m_remote_address, m_port)
		, (char const*)p->buf, p->size, m_error);

	if (m_error)
	{
		m_state = UTP_STATE_ERROR_WAIT;
		test_socket_state();
		return false;
	}

	return true;
}

void utp_socket_impl::ack_packet(packet* p, ptime const& receive_time
	, boost::uint32_t& min_rtt, boost::uint16_t seq_nr)
{
	TORRENT_ASSERT(p);
	if (!p->need_resend)
	{
		TORRENT_ASSERT(m_bytes_in_flight >= p->size - p->header_size);
		m_bytes_in_flight -= p->size - p->header_size;
	}

	// increment the acked sequence number counter
	if (m_acked_seq_nr == seq_nr)
	{
		m_acked_seq_nr = (m_acked_seq_nr + 1) & ACK_MASK;
		m_duplicate_acks = 0;
	}
	// increment the fast resend sequence number
	if (m_fast_resend_seq_nr == seq_nr)
		m_fast_resend_seq_nr = (m_fast_resend_seq_nr + 1) & ACK_MASK;

	boost::uint32_t rtt = boost::uint32_t(total_microseconds(receive_time - p->send_time));
	if (receive_time < p->send_time)
	{
		// this means our clock is not monotonic. Just assume the RTT was 100 ms
		rtt = 100000;

		// the clock for this plaform is not monotonic!
		TORRENT_ASSERT(false);
	}

	UTP_LOGV("%8p: acked packet %d (%d bytes) (rtt:%u)\n"
		, this, seq_nr, p->size - p->header_size, rtt / 1000);

	m_rtt.add_sample(rtt / 1000);
	if (rtt < min_rtt) min_rtt = rtt;
	free(p);
}

void utp_socket_impl::incoming(char const* buf, int size, packet* p, ptime now)
{
	while (!m_read_buffer.empty())
	{
		if (p)
		{
			buf = p->buf + p->header_size;
			TORRENT_ASSERT(p->size - p->header_size >= size);
		}
		iovec_t* target = &m_read_buffer.front();

		int to_copy = (std::min)(size, int(target->len));
		memcpy(target->buf, buf, to_copy);
		if (m_read == 0)
		{
			m_read_timeout = now + milliseconds(100);
			UTP_LOGV("%8p: setting read timeout to 100 ms from now\n", this);
		}
		m_read += to_copy;
		target->buf = ((char*)target->buf) + to_copy;
		target->len -= to_copy;
		buf += to_copy;
		TORRENT_ASSERT(m_read_buffer_size >= to_copy);
		m_read_buffer_size -= to_copy;
		size -= to_copy;
		if (target->len == 0) m_read_buffer.erase(m_read_buffer.begin());
		if (p)
		{
			p->header_size += to_copy;
			TORRENT_ASSERT(p->header_size <= p->size);
		}

		if (size == 0)
		{
			TORRENT_ASSERT(p == 0 || p->header_size == p->size);
			free(p);
			maybe_trigger_receive_callback(now);
			return;
		}
	}

	TORRENT_ASSERT(m_read_buffer_size == 0);

	if (!p)
	{
		TORRENT_ASSERT(buf);
		p = (packet*)malloc(sizeof(packet) + size);
		p->size = size;
		p->header_size = 0;
		memcpy(p->buf, buf, size);
	}
	if (m_receive_buffer_size == 0) m_read_timeout = now + milliseconds(100);
	// save this packet until the client issues another read
	m_receive_buffer.push_back(p);
	m_receive_buffer_size += p->size - p->header_size;

	check_receive_buffers();
}

bool utp_socket_impl::cancel_handlers(error_code const& ec, bool kill)
{
	bool ret = m_read_handler || m_write_handler || m_connect_handler;
	if (m_read_handler) m_read_handler(m_userdata, 0, ec, kill);
	m_read_handler = 0;
	if (m_write_handler) m_write_handler(m_userdata, 0, ec, kill);
	m_write_handler = 0;
	if (m_connect_handler) m_connect_handler(m_userdata, ec, kill);
	m_connect_handler = 0;
	return ret;
}

bool utp_socket_impl::consume_incoming_data(
	utp_header const* ph, char const* ptr, int payload_size
	, ptime now)
{
	if (ph->type != ST_DATA) return false;

	if (m_eof && m_ack_nr == m_eof_seq_nr)
	{
		// What?! We've already received a FIN and everything up
		// to it has been acked. Ignore this packet
		return true;
	}

	if (ph->seq_nr == ((m_ack_nr + 1) & ACK_MASK))
	{
		TORRENT_ASSERT(m_inbuf.at(m_ack_nr) == 0);

		// we received a packet in order
		incoming(ptr, payload_size, 0, now);
		m_ack_nr = (m_ack_nr + 1) & ACK_MASK;

		// If this packet was previously in the reorder buffer
		// it would have been acked when m_ack_nr-1 was acked.
		TORRENT_ASSERT(m_inbuf.at(m_ack_nr) == 0);

		UTP_LOGV("%8p: remove inbuf: %d (%d)\n"
			, this, m_ack_nr, int(m_inbuf.size()));

		for (;;)
		{
			int const next_ack_nr = (m_ack_nr + 1) & ACK_MASK;

			packet* p = (packet*)m_inbuf.remove(next_ack_nr);

			if (!p)
				break;

			m_buffered_incoming_bytes -= p->size - p->header_size;
			incoming(0, p->size - p->header_size, p, now);

			m_ack_nr = next_ack_nr;

			UTP_LOGV("%8p: reordered remove inbuf: %d (%d)\n"
				, this, m_ack_nr, int(m_inbuf.size()));
		}

		// should we trigger the read handler?
		maybe_trigger_receive_callback(now);
	}
	else
	{
		// this packet was received out of order. Stick it in the
		// reorder buffer until it can be delivered in order

		// have we already received this packet and passed it on
		// to the client?
		if (!compare_less_wrap(m_ack_nr, ph->seq_nr, ACK_MASK))
		{
			UTP_LOGV("%8p: already received seq_nr: %d\n"
				, this, int(ph->seq_nr));
			return true;
		}

		// do we already have this packet? If so, just ignore it
		if (m_inbuf.at(ph->seq_nr)) return true;

		// we don't need to save the packet header, just the payload
		packet* p = (packet*)malloc(sizeof(packet) + payload_size);
		p->size = payload_size;
		p->header_size = 0;
		p->num_transmissions = 0;
		p->need_resend = false;
		memcpy(p->buf, ptr, payload_size);
		m_inbuf.insert(ph->seq_nr, p);
		m_buffered_incoming_bytes += p->size;

		UTP_LOGV("%8p: out of order. insert inbuf: %d (%d) m_ack_nr: %d\n"
			, this, int(ph->seq_nr), int(m_inbuf.size()), m_ack_nr);
	}

	return false;
}

// returns true of the socket was closed
bool utp_socket_impl::test_socket_state()
{
	// if the socket is in a state where it's dead, just waiting to
	// tell the client that it's closed. Do that and transition into
	// the deleted state, where it will be deleted
	if (m_state == UTP_STATE_ERROR_WAIT)
	{
#if TORRENT_UTP_LOG
		UTP_LOGV("%8p: state:%s error:%s\n"
			, this, socket_state_names[m_state], m_error.message().c_str());
#endif

		if (cancel_handlers(m_error, true))
		{
			m_state = UTP_STATE_DELETE;
#if TORRENT_UTP_LOG
			UTP_LOGV("%8p: state:%s\n", this, socket_state_names[m_state]);
#endif
			return true;
		}
	}
	return false;
}

// return false if this is an invalid packet
bool utp_socket_impl::incoming_packet(char const* buf, int size
	, udp::endpoint const& ep, ptime receive_time)
{
	utp_header* ph = (utp_header*)buf;

	if (ph->ver != 1)
	{
		UTP_LOGV("%8p: incoming packet ver:%d (ignored)\n"
			, this, int(ph->ver));
		return false;
	}

	// SYN packets have special (reverse) connection ids
	if (ph->type != ST_SYN && ph->connection_id != m_recv_id)
	{
		UTP_LOGV("%8p: incoming packet id:%d expected:%d (ignored)\n"
			, this, int(ph->connection_id), int(m_recv_id));
		return false;
	}

	if (ph->type >= NUM_TYPES)
	{
		UTP_LOGV("%8p: incoming packet type:%d (ignored)\n"
			, this, int(ph->type));
		return false;
	}

	if (m_state == UTP_STATE_NONE && ph->type == ST_SYN)
	{
		m_remote_address = ep.address();
		m_port = ep.port();
	}

	if (m_state != UTP_STATE_NONE && ph->type == ST_SYN)
	{
		UTP_LOGV("%8p: incoming packet type:ST_SYN (ignored)\n", this);
		return true;
	}

	bool step = false;
	if (receive_time - m_last_history_step > minutes(1))
	{
		step = true;
		m_last_history_step = receive_time;
	}

	// this is the difference between their send time and our receive time
	// 0 means no sample yet
	boost::uint32_t their_delay = 0;
	if (ph->timestamp_microseconds != 0)
	{
		m_reply_micro = boost::uint32_t(total_microseconds(receive_time - min_time()))
			- ph->timestamp_microseconds;
		boost::uint32_t prev_base = m_their_delay_hist.initialized() ? m_their_delay_hist.base() : 0;
		their_delay = m_their_delay_hist.add_sample(m_reply_micro, step);
		int base_change = m_their_delay_hist.base() - prev_base;
		UTP_LOGV("%8p: their_delay::add_sample:%u prev_base:%u new_base:%u\n"
			, this, m_reply_micro, prev_base, m_their_delay_hist.base());

		if (prev_base && base_change < 0 && base_change > -10000)
		{
			// their base delay went down. This is caused by clock drift. To compensate,
			// adjust our base delay upwards
			// don't adjust more than 10 ms. If the change is that big, something is probably wrong
			m_delay_hist.adjust_base(-base_change);
		}

		UTP_LOGV("%8p: incoming packet reply_micro:%u base_change:%d\n"
			, this, m_reply_micro, prev_base ? base_change : 0);
	}

	if (ph->type == ST_RESET)
	{
		UTP_LOGV("%8p: incoming packet type:RESET\n", this);
		m_error = asio::error::connection_reset;
		m_state = UTP_STATE_ERROR_WAIT;
		test_socket_state();
		return true;
	}

	// is this ACK valid? If the other end is ACKing
	// a packet that hasn't been sent yet
	// just ignore it. A 3rd party could easily inject a packet
	// like this in a stream, don't sever it because of it.
	// since m_seq_nr is the sequence number of the next packet
	// we'll send (and m_seq_nr-1 was the last packet we sent),
	// if the ACK we got is greater than the last packet we sent
	// something is wrong.
	// If our state is state_none, this packet must be a syn packet
	// and the ack_nr should be ignored
	boost::uint16_t cmp_seq_nr = (m_seq_nr - 1) & ACK_MASK;
#if TORRENT_UT_SEQ
	if (m_state == UTP_STATE_SYN_SENT && ph->type == ST_STATE)
		cmp_seq_nr = m_seq_nr;
#endif
	if (m_state != UTP_STATE_NONE
		&& compare_less_wrap(cmp_seq_nr, ph->ack_nr, ACK_MASK))
	{
		UTP_LOGV("%8p: incoming packet ack_nr:%d our seq_nr:%d (ignored)\n"
			, this, int(ph->ack_nr), m_seq_nr);
		return true;
	}

	// check to make sure the sequence number of this packet
	// is reasonable. If it's a data packet and we've already
	// received it, ignore it. This is either a stray old packet
	// that finally made it here (after having been re-sent) or
	// an attempt to interfere with the connection from a 3rd party
	// in both cases, we can safely ignore the timestamp and ACK
	// information in this packet
/*
	// even if we've already received this packet, we need to
	// send another ack to it, since it may be a resend caused by
	// our ack getting dropped
	if (m_state != UTP_STATE_SYN_SENT
		&& ph->type == ST_DATA
		&& !compare_less_wrap(m_ack_nr, ph->seq_nr, ACK_MASK))
	{
		// we've already received this packet
		UTP_LOGV("%8p: incoming packet seq_nr:%d our ack_nr:%d (ignored)\n"
			, this, int(ph->seq_nr), m_ack_nr);
		return true;
	}
*/

	// if the socket is closing, always ignore any packet
	// with a higher sequence number than the FIN sequence number
	if (m_eof && compare_less_wrap(m_eof_seq_nr, ph->seq_nr, ACK_MASK))
	{
		UTP_LOGV("%8p: incoming packet seq_nr:%d eof_seq_nr:%d (ignored)\n"
			, this, int(ph->seq_nr), m_eof_seq_nr);
	
	}

	if (m_state != UTP_STATE_NONE
		&& m_state != UTP_STATE_SYN_SENT
		&& compare_less_wrap((m_ack_nr + max_packets_reorder) & ACK_MASK, ph->seq_nr, ACK_MASK))
	{
		// this is too far out to fit in our reorder buffer. Drop it
		// This is either an attack to try to break the connection
		// or a seariously damaged connection that lost a lot of
		// packets. Neither is very likely, and it should be OK
		// to drop the timestamp information.
		UTP_LOGV("%8p: incoming packet seq_nr:%d our ack_nr:%d (ignored)\n"
			, this, int(ph->seq_nr), m_ack_nr);
		return true;
	}

	++m_in_packets;

	// this is a valid incoming packet, update the timeout timer
	m_num_timeouts = 0;
	m_timeout = receive_time + milliseconds(packet_timeout());
	UTP_LOGV("%8p: updating timeout to: now + %d\n"
		, this, packet_timeout());

	// the test for INT_MAX here is a work-around for a bug in uTorrent where
	// it's sometimes sent as INT_MAX when it is in fact uninitialized
	const boost::uint32_t sample = ph->timestamp_difference_microseconds == INT_MAX
		? 0 : ph->timestamp_difference_microseconds;

	boost::uint32_t delay = 0;
	if (sample != 0)
	{
		delay = m_delay_hist.add_sample(sample, step);
		m_delay_sample_hist[m_delay_sample_idx++] = delay;
		if (m_delay_sample_idx >= num_delay_hist) m_delay_sample_idx = 0;
	}

	int acked_bytes = 0;

	TORRENT_ASSERT(m_bytes_in_flight >= 0);
	int prev_bytes_in_flight = m_bytes_in_flight;

	m_adv_wnd = ph->wnd_size;

	// if we get an ack for the same sequence number as
	// was last ACKed, and we have outstanding packets,
	// it counts as a duplicate ack
	if (ph->ack_nr == m_acked_seq_nr && m_outbuf.size())
	{
		++m_duplicate_acks;
	}

	boost::uint32_t min_rtt = UINT_MAX;

	// has this packet already been ACKed?
	// if the ACK we just got is less than the max ACKed
	// sequence number, it doesn't tell us anything.
	// So, only act on it if the ACK is greater than the last acked
	// sequence number
	if (compare_less_wrap(m_acked_seq_nr, ph->ack_nr, ACK_MASK))
	{
		int const next_ack_nr = ph->ack_nr;

		for (int ack_nr = (m_acked_seq_nr + 1) & ACK_MASK;
			ack_nr != ((next_ack_nr + 1) & ACK_MASK);
			ack_nr = (ack_nr + 1) & ACK_MASK)
		{
			packet* p = (packet*)m_outbuf.remove(ack_nr);
			if (!p) continue;
			acked_bytes += p->size - p->header_size;
			ack_packet(p, receive_time, min_rtt, ack_nr);
		}

		m_acked_seq_nr = next_ack_nr;

		m_duplicate_acks = 0;
		if (compare_less_wrap(m_fast_resend_seq_nr, (m_acked_seq_nr + 1) & ACK_MASK, ACK_MASK))
			m_fast_resend_seq_nr = (m_acked_seq_nr + 1) & ACK_MASK;
	}

	// look for extended headers
	char const* ptr = buf;
	ptr += sizeof(utp_header);

	unsigned int extension = ph->extension;
	while (extension)
	{
		// invalid packet. It says it has an extension header
		// but the packet is too short
		if (ptr - buf + 2 > size)
		{
			UTP_LOGV("%8p: invalid extension header\n", this);
			return true;
		}
		int next_extension = unsigned(*ptr++);
		unsigned int len = unsigned(*ptr++);
		if (ptr - buf + len > size)
		{
			UTP_LOGV("%8p: invalid extension header size:%d packet:%d\n"
				, this, len, int(ptr - buf));
			return true;
		}
		switch(extension)
		{
			case 1: // selective ACKs
				parse_sack(ph->ack_nr, ptr, len, &acked_bytes, receive_time, min_rtt);
				break;
		}
		ptr += len;
		extension = next_extension;
	}

	if (m_duplicate_acks > 3
		&& ((m_acked_seq_nr + 1) & ACK_MASK) == m_fast_resend_seq_nr)
	{
		// LOSS

		UTP_LOGV("%8p: Packet %d lost.\n", this, m_fast_resend_seq_nr);

		// resend the lost packet
		packet* p = (packet*)m_outbuf.at(m_fast_resend_seq_nr);
		TORRENT_ASSERT(p);
		// don't fast-resend this again
		m_fast_resend_seq_nr = (m_fast_resend_seq_nr + 1) & ACK_MASK;
		if (p)
		{
			TORRENT_ASSERT(p->num_transmissions <= m_sm->num_resends());
			++p->num_transmissions;
			if (p->need_resend) m_bytes_in_flight += p->size - p->header_size;
			p->need_resend = false;
			utp_header* h = (utp_header*)p->buf;
			h->timestamp_difference_microseconds = m_reply_micro;
			p->send_time = time_now_hires();
			// update packet header
			h->timestamp_microseconds = total_microseconds(p->send_time - min_time());

#if TORRENT_UTP_LOG
			UTP_LOGV("%8p: fast re-sending packet seq_nr:%d ack_nr:%d type:%s "
				"id:%d target:%s size:%d error:%s send_buffer_size:%d cwnd:%d "
				"adv_wnd:%d in-flight:%d mtu:%d timestamp:%u time_diff:%u\n"
				, this, int(h->seq_nr), int(h->ack_nr), packet_type_names[h->type]
				, m_send_id, print_endpoint(udp::endpoint(m_remote_address, m_port)).c_str()
				, p->size, m_error.message().c_str(), m_write_buffer_size, int(m_cwnd >> 16)
				, m_adv_wnd, m_bytes_in_flight, m_mtu, boost::uint32_t(h->timestamp_microseconds)
				, boost::uint32_t(h->timestamp_difference_microseconds));
#endif

			m_sm->send_packet(udp::endpoint(m_remote_address, m_port)
				, p->buf, p->size, m_error);
			++m_out_packets;
			if (m_error)
			{
				m_state = UTP_STATE_ERROR_WAIT;
				test_socket_state();
				return true;
			}
		}
		// cut window size in 2
		m_cwnd = (std::max)(m_cwnd / 2, boost::int64_t(m_mtu << 16));

		// the window size could go below one MMS here, if it does,
		// we'll get a timeout in about one second
	}

	// ptr points to the payload of the packet
	// size is the packet size, payload is the
	// number of payload bytes are in this packet
	const int header_size = ptr - buf;
	const int payload_size = size - header_size;

#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: incoming packet seq_nr:%d ack_nr:%d type:%s id:%d size:%d timestampdiff:%u timestamp:%u "
			"our ack_nr:%d our seq_nr:%d our acked_seq_nr:%d our state:%s\n"
		, this, int(ph->seq_nr), int(ph->ack_nr), packet_type_names[ph->type]
		, int(ph->connection_id), payload_size, boost::uint32_t(ph->timestamp_difference_microseconds)
		, boost::uint32_t(ph->timestamp_microseconds), m_ack_nr, m_seq_nr, m_acked_seq_nr, socket_state_names[m_state]);
#endif

	if (ph->type == ST_FIN)
	{
		// We ignore duplicate FIN packets, but we still need to ACK them.
		if (ph->seq_nr == ((m_ack_nr + 1) & ACK_MASK)
			|| ph->seq_nr == m_ack_nr)
		{
			UTP_LOGV("%8p: FIN received in order\n", this);

			// The FIN arrived in order, nothing else is in the
			// reorder buffer.

//			TORRENT_ASSERT(m_inbuf.size() == 0);
			m_ack_nr = ph->seq_nr;

			// Transition to UTP_STATE_FIN_SENT. The sent FIN is also an ack
			// to the FIN we received. Once we're in UTP_STATE_FIN_SENT we
			// just need to wait for our FIN to be acked.

			if (m_state == UTP_STATE_FIN_SENT)
			{
				send_pkt(true);
			}
			else
			{
				send_fin();
			}
		}

		if (m_eof)
		{
			UTP_LOGV("%8p: duplicate FIN packet (ignoring)\n", this);
			return true;
		}
		m_eof = true;
		m_eof_seq_nr = ph->seq_nr;

		// we will respond with a fin once we have received everything up to m_eof_seq_nr
	}

	switch (m_state)
	{
		case UTP_STATE_NONE:
		{
			if (ph->type == ST_SYN)
			{
				// if we're in state_none, the only thing
				// we accept are SYN packets.
				m_state = UTP_STATE_CONNECTED;

				m_remote_address = ep.address();
				m_port = ep.port();

#if TORRENT_UTP_LOG
				UTP_LOGV("%8p: state:%s\n"
					, this, socket_state_names[m_state]);
#endif
				m_ack_nr = ph->seq_nr;
				m_seq_nr = rand();
				m_acked_seq_nr = (m_seq_nr - 1) & ACK_MASK;

				TORRENT_ASSERT(m_send_id == ph->connection_id);
				TORRENT_ASSERT(m_recv_id == ((m_send_id + 1) & 0xffff));

				send_pkt(true);

				return true;
			}
			else
			{
#if TORRENT_UTP_LOG
				UTP_LOGV("%8p: type:%s state:%s (ignored)\n"
					, this, packet_type_names[ph->type], socket_state_names[m_state]);
#endif
				return true;
			}
			break;
		}
		case UTP_STATE_SYN_SENT:
		{
			// just wait for an ack to our SYN, ignore everything else
			if (ph->ack_nr != ((m_seq_nr - 1) & ACK_MASK))
			{
#if TORRENT_UTP_LOG
				UTP_LOGV("%8p: incorrect ack_nr (%d) waiting for %d\n"
					, this, int(ph->ack_nr), (m_seq_nr - 1) & ACK_MASK);
#endif
				return true;
			}

			m_state = UTP_STATE_CONNECTED;
#if TORRENT_UTP_LOG
			UTP_LOGV("%8p: state:%s\n", this, socket_state_names[m_state]);
#endif

			// only progress our ack_nr on ST_DATA messages
			// since our m_ack_nr is uninitialized at this point
			// we still need to set it to something regardless
			if (ph->type == ST_DATA)
				m_ack_nr = ph->seq_nr;
			else
				m_ack_nr = (ph->seq_nr - 1) & ACK_MASK;

			// notify the client that the socket connected
			if (m_connect_handler)
			{
				UTP_LOGV("%8p: calling connect handler\n", this);
				m_connect_handler(m_userdata, m_error, false);
			}
			m_connect_handler = 0;
			// fall through
		}
		case UTP_STATE_CONNECTED:
		{
			// the lowest seen RTT can be used to clamp the delay
			// within reasonable bounds. The one-way delay is never
			// higher than the round-trip time.

			// it's impossible for delay to be more than the RTT, so make
			// sure to clamp it as a sanity check
			if (delay > min_rtt) delay = min_rtt;

			// only use the minimum from the last 3 delay measurements
			delay = *std::min_element(m_delay_sample_hist, m_delay_sample_hist + num_delay_hist);

			if (sample && acked_bytes && prev_bytes_in_flight)
				do_ledbat(acked_bytes, delay, prev_bytes_in_flight, receive_time);

			consume_incoming_data(ph, ptr, payload_size, receive_time);

			// the parameter to send_pkt tells it if we're acking data
			// If we are, we'll send an ACK regardless of if we have any
			// space left in our send window or not. If we just got an ACK
			// (i.e. ST_STATE) we're not ACKing anything. If we just
			// received a FIN packet, we need to ack that as well
			bool has_ack = ph->type == ST_DATA || ph->type == ST_FIN || ph->type == ST_SYN;
			int delayed_ack = m_sm->delayed_ack();
			if (has_ack && delayed_ack && m_ack_timer > receive_time)
			{
				// we have data to ACK, and delayed ACKs are enabled.
				// update the ACK timer and clear the flag, to pretend
				// like we don't have anything to ACK
				m_ack_timer = (std::min)(m_ack_timer, receive_time + milliseconds(delayed_ack));
				has_ack = false;
				UTP_LOGV("%8p: delaying ack. timer triggers in %d milliseconds\n"
					, this, int(total_milliseconds(m_ack_timer - time_now_hires())));
			}

			if (send_pkt(has_ack))
			{
				// try to send more data as long as we can
				while (send_pkt(false));
			}

			// Everything up to the FIN has been receieved, respond with a FIN
			// from our side.
			if (m_eof && m_ack_nr == ((m_eof_seq_nr - 1) & ACK_MASK))
			{
				UTP_LOGV("%8p: incoming stream consumed\n", this);

				// This transitions to the UTP_STATE_FIN_SENT state.
				send_fin();
			}

#if TORRENT_UTP_LOG
			if (sample && acked_bytes && prev_bytes_in_flight)
			{
				char their_delay_base[20];
				if (m_their_delay_hist.initialized())
					snprintf(their_delay_base, sizeof(their_delay_base), "%u", m_their_delay_hist.base());
				else
					strcpy(their_delay_base, "-");

				char our_delay_base[20];
				if (m_delay_hist.initialized())
					snprintf(our_delay_base, sizeof(our_delay_base), "%u", m_delay_hist.base());
				else
					strcpy(our_delay_base, "-");

				UTP_LOG("%8p: "
					"actual_delay:%u "
					"our_delay:%f "
					"their_delay:%f "
					"off_target:%f "
					"max_window:%d "
					"upload_rate:%d "
					"delay_base:%s "
					"delay_sum:%f "
					"target_delay:%d "
					"acked_bytes:%d "
					"cur_window:%d "
					"scaled_gain:%f "
					"rtt:%u "
					"rate:%d "
					"quota:%d "
					"wnduser:%u "
					"rto:%d "
					"timeout:%d "
					"get_microseconds:%u "
					"cur_window_packets:%d "
					"packet_size:%d "
					"their_delay_base:%s "
					"their_actual_delay:%u "
					"seq_nr:%u "
					"acked_seq_nr:%u "
					"reply_micro:%u "
					"min_rtt:%u "
					"send_buffer:%d "
					"recv_buffer:%d "
					"\n"
					, this
					, sample
					, float(delay / 1000.f)
					, float(their_delay / 1000.f)
					, float(int(m_sm->target_delay() - delay)) / 1000.f
					, int(m_cwnd >> 16)
					, 0
					, our_delay_base
					, float(delay + their_delay) / 1000.f
					, m_sm->target_delay() / 1000
					, acked_bytes
					, m_bytes_in_flight
					, 0.f // float(scaled_gain)
					, m_rtt.mean()
					, int(m_cwnd * 1000 / (m_rtt.mean()?m_rtt.mean():50)) >> 16
					, 0
					, m_adv_wnd
					, packet_timeout()
					, int(total_milliseconds(m_timeout - receive_time))
					, int(total_microseconds(receive_time - min_time()))
					, m_seq_nr - m_acked_seq_nr
					, m_mtu
					, their_delay_base
					, boost::uint32_t(m_reply_micro)
					, m_seq_nr
					, m_acked_seq_nr
					, m_reply_micro
					, min_rtt / 1000
					, m_write_buffer_size
					, m_read_buffer_size);
			}
#endif

			return true;
		}
		case UTP_STATE_FIN_SENT:
		{
			// There are two ways we can end up in this state:
			//
			// 1. If the socket has been explicitly closed on our
			//    side, in which case m_eof is false.
			//
			// 2. If we received a FIN from the remote side, in which
			//    case m_eof is true. If this is the case, we don't
			//    come here until everything up to the FIN has been
			//    received.
			//
			//
			//

			// At this point m_seq_nr - 1 is the FIN sequence number.

			// We can receive both ST_DATA and ST_STATE here, because after
			// we have closed our end of the socket, the remote end might
			// have data in the pipeline. We don't really care about the
			// data, but we do have to ack it. Or rather, we have to ack
			// the FIN that will come after the data.

			// Case 1:
			// ---------------------------------------------------------------
			//
			// If we are here because the local endpoint was closed, we need
			// to first wait for all of our messages to be acked:
			//
			//   if (m_acked_seq_nr == ((m_seq_nr - 1) & ACK_MASK))
			//
			// `m_seq_nr - 1` is the ST_FIN message that we sent.
			//
			//                     ----------------------
			//
			// After that has happened we need to wait for the remote side
			// to send its ST_FIN message. When we receive that we send an
			// ST_STATE back to ack, and wait for a sufficient period.
			// During this wait we keep acking incoming ST_FIN's. This is
			// all handled at the top of this function.
			//
			// Note that the user handlers are all cancelled when the initial
			// close() call happens, so nothing will happen on the user side
			// after that.

			// Case 2:
			// ---------------------------------------------------------------
			//
			// If we are here because we received a ST_FIN message, and then
			// sent our own ST_FIN to ack that, we need to wait for our ST_FIN
			// to be acked:
			//
			//   if (m_acked_seq_nr == ((m_seq_nr - 1) & ACK_MASK))
			//
			// `m_seq_nr - 1` is the ST_FIN message that we sent.
			//
			// After that has happened we know the remote side has all our
			// data, and we can gracefully shut down.

			if (consume_incoming_data(ph, ptr, payload_size, receive_time))
				return true;

			if (m_acked_seq_nr == ((m_seq_nr - 1) & ACK_MASK))
			{
				// When this happens we know that the remote side has
				// received all of our packets.

				UTP_LOGV("%8p: FIN acked\n", this);
/*
				if (!m_userdata)
				{
					UTP_LOGV("%8p: close initiated here, "
					"wait for remote FIN\n", this);
				}
				else
				{
*/
				UTP_LOGV("%8p: closing socket\n", this);
				m_error = asio::error::eof;
				m_state = UTP_STATE_ERROR_WAIT;
				test_socket_state();
//				}
			}

			return true;
		}
		case UTP_STATE_DELETE:
		default:
		{
			// respond with a reset
			send_reset(ph);
			return true;
		}
	}

	return false;
}

void utp_socket_impl::do_ledbat(int acked_bytes, int delay, int in_flight, ptime const now)
{
	// the portion of the in-flight bytes that were acked. This is used to make
	// the gain factor be scaled by the rtt. The formula is applied once per
	// rtt, or on every ACK skaled by the number of ACKs per rtt
	TORRENT_ASSERT(in_flight > 0);
	TORRENT_ASSERT(acked_bytes > 0);

	int target_delay = m_sm->target_delay();

	// all of these are fixed points with 16 bits fraction portion
	boost::int64_t window_factor = (boost::int64_t(acked_bytes) << 16) / in_flight;
	boost::int64_t delay_factor = (boost::int64_t(target_delay - delay) << 16) / target_delay;
	boost::int64_t scaled_gain = (window_factor * delay_factor) >> 16;
	scaled_gain *= boost::int64_t(m_sm->gain_factor());

	if (scaled_gain > 0 && m_last_cwnd_hit + seconds(1) < now)
	{
		// we haven't bumped into the cwnd limit size in the last second
		// this probably means we have a send rate limit, so we shouldn't make
		// the cwnd size any larger
		scaled_gain = 0;
	}

	UTP_LOGV("%8p: do_ledbat delay:%d off_target: %d window_factor:%f target_factor:%f "
		"scaled_gain:%f cwnd:%d\n"
		, this, delay, target_delay - delay, window_factor / float(1 << 16)
		, delay_factor / float(1 << 16)
		, scaled_gain / float(1 << 16), int(m_cwnd >> 16));

	// if scaled_gain + m_cwnd <= 0, set m_cwnd to 0
	if (-scaled_gain >= m_cwnd)
	{
		m_cwnd = 0;
	}
	else
	{
		m_cwnd += scaled_gain;
		TORRENT_ASSERT(m_cwnd > 0);
	}
}

void utp_stream::bind(endpoint_type const& ep, error_code& ec) { }

// returns the number of milliseconds a packet would have before
// it would time-out if it was sent right now. Takes the RTT estimate
// into account
int utp_socket_impl::packet_timeout() const
{
	// SYN packets have a bit longer timeout, since we don't
	// have an RTT estimate yet, make a conservative guess
	if (m_state == UTP_STATE_NONE) return 3000;
	int timeout = (std::max)(1000, m_rtt.mean() + m_rtt.avg_deviation() * 2);
	if (m_num_timeouts > 0) timeout += (1 << (int(m_num_timeouts) - 1)) * 1000;
	return timeout;
}

void utp_socket_impl::tick(ptime const& now)
{
#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: tick:%s r: %d (%s) w: %d (%s)\n"
		, this, socket_state_names[m_state], m_read, m_read_handler ? "handler" : "no handler"
		, m_written, m_write_handler ? "handler" : "no handler");
#endif
	bool window_opened = false;

	// don't hang on to received data for too long, and don't
	// wait too long telling the client we've sent some data.
	// these functions will trigger time callback if we have
	// a reason to and it's been long enough since we sent or
	// received the data
	maybe_trigger_receive_callback(now);
	maybe_trigger_send_callback(now);

	if (now > m_timeout)
	{
		// TIMEOUT!
		// set cwnd to 1 MSS

		// the window went from less than one MSS to one MSS
		// we can now sent messages again, the send window was opened
		if ((m_cwnd >> 16) < m_mtu) window_opened = true;

		m_cwnd = m_mtu << 16;
		if (m_outbuf.size()) ++m_num_timeouts;
		m_timeout = now + milliseconds(packet_timeout());
	
		UTP_LOGV("%8p: timeout resetting cwnd:%d\n"
			, this, int(m_cwnd >> 16));

		// we need to go one passed m_seq_nr to cover the case
		// where we just sent a SYN packet and then adjusted for
		// the uTorrent sequence number reuse
		for (int i = m_acked_seq_nr & ACK_MASK;
			i != ((m_seq_nr + 1) & ACK_MASK);
			i = (i + 1) & ACK_MASK)
		{
			packet* p = (packet*)m_outbuf.at(i);
			if (!p) continue;
			if (p->need_resend) continue;
			p->need_resend = true;
			TORRENT_ASSERT(m_bytes_in_flight >= p->size - p->header_size);
			m_bytes_in_flight -= p->size - p->header_size;
		}

		TORRENT_ASSERT(m_bytes_in_flight == 0);

		// if we have a packet that needs re-sending, resend it
		packet* p = (packet*)m_outbuf.at((m_acked_seq_nr + 1) & ACK_MASK);
		if (p)
		{
			if (p->num_transmissions >= m_sm->num_resends()
				|| (m_state == UTP_STATE_SYN_SENT && p->num_transmissions >= m_sm->syn_resends())
				|| (m_state == UTP_STATE_FIN_SENT && p->num_transmissions >= m_sm->fin_resends()))
			{
#if TORRENT_UTP_LOG
				UTP_LOGV("%8p: %d failed sends in a row. Socket timed out. state:%s\n"
					, this, p->num_transmissions, socket_state_names[m_state]);
#endif

				// the connection is dead
				m_error = asio::error::timed_out;
				m_state = UTP_STATE_ERROR_WAIT;
				test_socket_state();
				return;
			}
	
			// don't fast-resend this packet
			if (m_fast_resend_seq_nr == ((m_acked_seq_nr + 1) & ACK_MASK))
				m_fast_resend_seq_nr = (m_fast_resend_seq_nr + 1) & ACK_MASK;

			// the packet timed out, resend it
			resend_packet(p);
			if (m_error)
			{
				m_state = UTP_STATE_ERROR_WAIT;
				test_socket_state();
				return;
			}
		}
		else if (m_state < UTP_STATE_FIN_SENT)
		{
			send_pkt(false);
		}
		else if (m_state == UTP_STATE_FIN_SENT)
		{
			// the connection is dead
			m_error = asio::error::eof;
			m_state = UTP_STATE_ERROR_WAIT;
			test_socket_state();
			return;
		}
	}

	if (now > m_ack_timer)
	{
		UTP_LOGV("%8p: ack timer expired, sending ACK\n", this);
		// we need to send an ACK now!
		send_pkt(true);
	}

	switch (m_state)
	{
		case UTP_STATE_NONE:
		case UTP_STATE_DELETE:
			return;
//		case UTP_STATE_SYN_SENT:
//
//			break;
	}
}

void utp_socket_impl::check_receive_buffers() const
{
	std::size_t size = 0;

	for (std::vector<packet*>::const_iterator i = m_receive_buffer.begin()
		, end(m_receive_buffer.end()); i != end; ++i)
	{
		if (packet const* p = *i)
			size += p->size - p->header_size;
	}

	TORRENT_ASSERT(size == m_receive_buffer_size);
}

}

