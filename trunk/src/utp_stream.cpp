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
#include "libtorrent/error.hpp"
#include "libtorrent/random.hpp"
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
	mutex utp_log_mutex;

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
	mutex::scoped_lock lock(log_file_holder.utp_log_mutex);
	static ptime start = time_now_hires();
	fprintf(log_file_holder.utp_log_file, "[%012"PRId64"] ", total_microseconds(time_now_hires() - start));
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

	// if a packet receives more than this number of
	// duplicate acks, we'll trigger a fast re-send
	dup_ack_limit = 3,

	// the max number of packets to fast-resend per
	// selective ack message
	sack_resend_limit = 3,
};

// compare if lhs is less than rhs, taking wrapping
// into account. if lhs is close to UINT_MAX and rhs
// is close to 0, lhs is assumed to have wrapped and
// considered smaller
TORRENT_EXTRA_EXPORT bool compare_less_wrap(boost::uint32_t lhs, boost::uint32_t rhs, boost::uint32_t mask)
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

	// the size of the buffer 'buf' points to
	boost::uint16_t size;

	// this is the offset to the payload inside the buffer
	// this is also used as a cursor to describe where the
	// next payload that hasn't been consumed yet starts
	boost::uint16_t header_size;
	
	// the number of times this packet has been sent
	boost::uint8_t num_transmissions:6;

	// true if we need to send this packet again. All
	// outstanding packets are marked as needing to be
	// resent on timeouts
	bool need_resend:1;

	// this is set to true for packets that were
	// sent with the DF bit set (Don't Fragment)
	bool mtu_probe:1;

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
		, m_read_handler(0)
		, m_write_handler(0)
		, m_connect_handler(0)
		, m_remote_address()
		, m_read_timeout()
		, m_write_timeout()
		, m_timeout(time_now_hires() + milliseconds(m_sm->connect_timeout()))
		, m_last_cwnd_hit(time_now())
		, m_ack_timer(time_now() + minutes(10))
		, m_last_history_step(time_now_hires())
		, m_cwnd(TORRENT_ETHERNET_MTU << 16)
		, m_buffered_incoming_bytes(0)
		, m_reply_micro(0)
		, m_adv_wnd(TORRENT_ETHERNET_MTU)
		, m_bytes_in_flight(0)
		, m_read(0)
		, m_write_buffer_size(0)
		, m_written(0)
		, m_receive_buffer_size(0)
		, m_read_buffer_size(0)
		, m_in_buf_size(100 * 1024 * 1024)
		, m_in_packets(0)
		, m_out_packets(0)
		, m_send_delay(0)
		, m_recv_delay(0)
		, m_port(0)
		, m_send_id(send_id)
		, m_recv_id(recv_id)
		, m_ack_nr(0)
		, m_seq_nr(0)
		, m_acked_seq_nr(0)
		, m_fast_resend_seq_nr(0)
		, m_eof_seq_nr(0)
		, m_loss_seq_nr(0)
		, m_mtu(TORRENT_ETHERNET_MTU - TORRENT_IPV4_HEADER - TORRENT_UDP_HEADER - 8 - 24 - 36)
		, m_mtu_floor(TORRENT_INET_MIN_MTU - TORRENT_IPV4_HEADER - TORRENT_UDP_HEADER)
		, m_mtu_ceiling(TORRENT_ETHERNET_MTU - TORRENT_IPV4_HEADER - TORRENT_UDP_HEADER)
		, m_mtu_seq(0)
		, m_duplicate_acks(0)
		, m_num_timeouts(0)
		, m_delay_sample_idx(0)
		, m_state(UTP_STATE_NONE)
		, m_eof(false)
		, m_attached(true)
		, m_nagle(true)
		, m_slow_start(false)
		, m_cwnd_full(false)
	{
		TORRENT_ASSERT(m_userdata);
		for (int i = 0; i != num_delay_hist; ++i)
			m_delay_sample_hist[i] = UINT_MAX;
	}

	~utp_socket_impl();

	void tick(ptime const& now);
	void init_mtu(int link_mtu, int utp_mtu);
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
	// returns true if there were handlers cancelled
	// if it returns false, we can detach immediately
	bool destroy();
	void detach();
	void send_syn();
	void send_fin();

	bool send_pkt(bool ack);
	bool resend_packet(packet* p, bool fast_resend = false);
	void send_reset(utp_header* ph);
	void parse_sack(boost::uint16_t packet_ack, char const* ptr, int size, int* acked_bytes
		, ptime const now, boost::uint32_t& min_rtt);
	void write_payload(char* ptr, int size);
	void maybe_inc_acked_seq_nr();
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
	void update_mtu_limits();
	void experienced_loss(int seq_nr);

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
	// it in do_ledbat() are signed.
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

	// the last send delay sample
	boost::int32_t m_send_delay;
	// the last receive delay sample
	boost::int32_t m_recv_delay;

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

	// this is the lowest sequence number that, when lost,
	// will cause the window size to be cut in half
	boost::uint16_t m_loss_seq_nr;

	// the max number of bytes we can send in a packet
	// including the header
	boost::uint16_t m_mtu;

	// the floor is the largest packet that we have
	// been able to get through without fragmentation
	boost::uint16_t m_mtu_floor;

	// the ceiling is the largest packet that we might
	// be able to get through without fragmentation.
	// i.e. ceiling +1 is very likely to not get through
	// or we have in fact experienced a drop or ICMP
	// message indicating that it is
	boost::uint16_t m_mtu_ceiling;

	// the sequence number of the probe in-flight
	// this is 0 if there is no probe in flight
	boost::uint16_t m_mtu_seq;

	// this is a counter of how many times the current m_acked_seq_nr
	// has been ACKed. If it's ACKed more than 3 times, we assume the
	// packet with the next sequence number has been lost, and we trigger
	// a re-send. Ovbiously an ACK only counts as a duplicate as long as
	// we have outstanding packets following it.
	boost::uint8_t m_duplicate_acks;

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

	// this is true if nagle is enabled (which it is by default)
	// TODO: support the option to turn it off
	bool m_nagle:1;

	// this is true while the socket is in slow start mode. It's
	// only in slow-start during the start-up phase
	bool m_slow_start:1;
	
	// this is true as long as we have as many packets in
	// flight as allowed by the congestion window (cwnd)
	bool m_cwnd_full:1;
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

void utp_init_mtu(utp_socket_impl* s, int link_mtu, int utp_mtu)
{
	s->init_mtu(link_mtu, utp_mtu);
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

void utp_socket_impl::update_mtu_limits()
{
	TORRENT_ASSERT(m_mtu_floor <= m_mtu_ceiling);
	m_mtu = (m_mtu_floor + m_mtu_ceiling) / 2;

	// clear the mtu probe sequence number since
	// it was either dropped or acked
	m_mtu_seq = 0;

	if (m_mtu_ceiling - m_mtu_floor < 10)
	{
		// we have narrowed down the mtu within 10
		// bytes. That's good enough, start using
		// floor as the packet size from now on.
		// set the ceiling to the floor as well to
		// disable more probes to be sent
		// we'll never re-probe this connection
		m_mtu = m_mtu_ceiling = m_mtu_floor;
	}
}

int utp_socket_state(utp_socket_impl const* s)
{
	return s->m_state;
}

int utp_stream::send_delay() const
{
	return m_impl ? m_impl->m_send_delay : 0;
}

int utp_stream::recv_delay() const
{
	return m_impl ? m_impl->m_recv_delay : 0;
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
	if (!m_impl->destroy())
	{
		if (!m_impl) return;
		detach_utp_impl(m_impl);
		m_impl = 0;
	}
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
	TORRENT_ASSERT(bytes_transferred > 0 || ec);
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
	TORRENT_ASSERT(bytes_transferred > 0 || ec);
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
	TORRENT_ASSERT(s);

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
	TORRENT_ASSERT(len > 0);
	TORRENT_ASSERT(buf);
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
	TORRENT_ASSERT(len < INT_MAX);
	TORRENT_ASSERT(len > 0);
	TORRENT_ASSERT(buf);

#ifdef TORRENT_DEBUG
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

#ifdef TORRENT_DEBUG
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
	TORRENT_ASSERT(m_impl->m_userdata);
	m_impl->m_read_handler = h;
	if (m_impl->test_socket_state()) return;

	UTP_LOGV("%8p: new read handler. %d bytes in buffer\n"
		, m_impl, m_impl->m_receive_buffer_size);

	TORRENT_ASSERT(m_impl->m_read_buffer_size > 0);

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
		TORRENT_ASSERT(int(target->len) >= to_copy);
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

	TORRENT_ASSERT(m_impl->m_write_buffer_size > 0);

	TORRENT_ASSERT(m_impl->m_userdata);
	m_impl->m_write_handler = h;
	m_impl->m_written = 0;
	if (m_impl->test_socket_state()) return;

	// try to write. send_pkt returns false if there's
	// no more payload to send or if the congestion window
	// is full and we can't send more packets right now
	while (m_impl->send_pkt(false));

	// if there was an error in send_pkt(), m_impl may be
	// 0 at this point
	if (m_impl) m_impl->maybe_trigger_send_callback(time_now_hires());
}

void utp_stream::do_connect(tcp::endpoint const& ep, utp_stream::connect_handler_t handler)
{
	int link_mtu, utp_mtu;
	m_impl->m_sm->mtu_for_dest(ep.address(), link_mtu, utp_mtu);
	m_impl->init_mtu(link_mtu, utp_mtu);
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

	if (m_read > m_read_buffer_size / 2 || now >= m_read_timeout)
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

	if (m_written > m_write_buffer_size * 2 / 3 || now >= m_write_timeout)
	{
		UTP_LOGV("%8p: calling write handler written:%d\n", this, m_written);

		m_write_handler(m_userdata, m_written, m_error, false);
		m_write_handler = 0;
		m_written = 0;
		m_write_buffer_size = 0;
		m_write_buffer.clear();
	}
}

bool utp_socket_impl::destroy()
{
#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: destroy state:%s\n", this, socket_state_names[m_state]);
#endif

	if (m_userdata == 0) return false;

	if (m_state == UTP_STATE_CONNECTED)
		send_fin();

	bool cancelled = cancel_handlers(asio::error::operation_aborted, true);

	m_userdata = 0;

	m_read_buffer.clear();
	m_read_buffer_size = 0;

	m_write_buffer.clear();
	m_write_buffer_size = 0;

	if ((m_state == UTP_STATE_ERROR_WAIT
		|| m_state == UTP_STATE_NONE
		|| m_state == UTP_STATE_SYN_SENT) && cancelled)
	{
		m_state = UTP_STATE_DELETE;
#if TORRENT_UTP_LOG
		UTP_LOGV("%8p: state:%s\n", this, socket_state_names[m_state]);
#endif
	}

	return cancelled;

	// #error our end is closing. Wait for everything to be acked
}

void utp_socket_impl::detach()
{
	UTP_LOGV("%8p: detach()\n", this);
	m_attached = false;
}

void utp_socket_impl::send_syn()
{
	m_seq_nr = random();
	m_acked_seq_nr = (m_seq_nr - 1) & ACK_MASK;
	m_loss_seq_nr = m_acked_seq_nr;
	m_ack_nr = 0;
	m_fast_resend_seq_nr = m_seq_nr;

	packet* p = (packet*)malloc(sizeof(packet) + sizeof(utp_header));
	p->size = sizeof(utp_header);
	p->header_size = sizeof(utp_header);
	p->num_transmissions = 1;
	p->need_resend = false;
	utp_header* h = (utp_header*)p->buf;
	h->type_ver = (ST_SYN << 4) | 1;
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

	error_code ec;
	m_sm->send_packet(udp::endpoint(m_remote_address, m_port), (char const*)h
		, sizeof(utp_header), ec);

	if (ec)
	{
		free(p);
		m_error = ec;
		m_state = UTP_STATE_ERROR_WAIT;
		test_socket_state();
		return;
	}

	TORRENT_ASSERT(!m_outbuf.at(m_seq_nr));
	m_outbuf.insert(m_seq_nr, p);

	m_seq_nr = (m_seq_nr + 1) & ACK_MASK;

	TORRENT_ASSERT(!m_error);
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

	h->type_ver = (ST_FIN << 4) | 1;
	h->extension = 0;
	h->connection_id = m_send_id;
	h->timestamp_difference_microseconds = m_reply_micro;
	h->wnd_size = m_in_buf_size - m_buffered_incoming_bytes - m_receive_buffer_size;
	h->seq_nr = m_seq_nr;
	h->ack_nr = m_ack_nr;

	ptime now = time_now_hires();
	p->send_time = now;
	h->timestamp_microseconds = boost::uint32_t(total_microseconds(now - min_time()));

	error_code ec;
	m_sm->send_packet(udp::endpoint(m_remote_address, m_port)
		, (char const*)h, sizeof(utp_header), ec);

#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: sending FIN seq_nr:%d ack_nr:%d type:%s "
		"id:%d target:%s size:%d error:%s send_buffer_size:%d\n"
		, this, int(h->seq_nr), int(h->ack_nr), packet_type_names[h->get_type()]
		, m_send_id, print_endpoint(udp::endpoint(m_remote_address, m_port)).c_str()
		, int(sizeof(utp_header)), ec.message().c_str(), m_write_buffer_size);
#endif

	if (ec)
	{
		m_error = ec;
		m_state = UTP_STATE_ERROR_WAIT;
		test_socket_state();
		free(p);
		return;
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
	m_fast_resend_seq_nr = m_seq_nr;

	TORRENT_ASSERT(!m_error);
	m_state = UTP_STATE_FIN_SENT;

#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: state:%s\n", this, socket_state_names[m_state]);
#endif
}

void utp_socket_impl::send_reset(utp_header* ph)
{
	utp_header h;
	h.type_ver = (ST_RESET << 4) | 1;
	h.extension = 0;
	h.connection_id = m_send_id;
	h.timestamp_difference_microseconds = m_reply_micro;
	h.wnd_size = 0;
	h.seq_nr = random();
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

	// the number of acked packets past the fast re-send sequence number
	// this is used to determine if we should trigger more fast re-sends
	int dups = 0;

	// the sequence number of the last ACKed packet
	int last_ack = packet_ack;

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
				last_ack = ack_nr;
				if (m_fast_resend_seq_nr == ack_nr)
					m_fast_resend_seq_nr = (m_fast_resend_seq_nr + 1) & ACK_MASK;

				if (compare_less_wrap(m_fast_resend_seq_nr, ack_nr, ACK_MASK)) ++dups;
				// this bit was set, ack_nr was received
				packet* p = (packet*)m_outbuf.remove(ack_nr);
				if (p)
				{
					*acked_bytes += p->size - p->header_size;
					// each ACKed packet counts as a duplicate ack
					UTP_LOGV("%8p: duplicate_acks:%u fast_resend_seq_nr:%u\n"
						, this, m_duplicate_acks, m_fast_resend_seq_nr);
					ack_packet(p, now, min_rtt, ack_nr);
				}
				else
				{
					// this packet might have been acked by a previous
					// selective ack
					maybe_inc_acked_seq_nr();
				}
			}

			mask <<= 1;
			ack_nr = (ack_nr + 1) & ACK_MASK;

			// we haven't sent packets past this point.
			// if there are any more bits set, we have to
			// ignore them anyway
			if (ack_nr == m_seq_nr) break;
		}
		if (ack_nr == m_seq_nr) break;
	}

	TORRENT_ASSERT(m_outbuf.at((m_acked_seq_nr + 1) & ACK_MASK) || ((m_seq_nr - m_acked_seq_nr) & ACK_MASK) <= 1);

	// we received more than dup_ack_limit ACKs in this SACK message.
	// trigger fast re-send
	if (dups >= dup_ack_limit && compare_less_wrap(m_fast_resend_seq_nr, last_ack, ACK_MASK))
	{
		experienced_loss(m_fast_resend_seq_nr);
		int num_resent = 0;
		for (; m_fast_resend_seq_nr != last_ack; m_fast_resend_seq_nr = (m_fast_resend_seq_nr + 1) & ACK_MASK)
		{
			packet* p = (packet*)m_outbuf.at(m_fast_resend_seq_nr);
			if (!p) continue;
			++num_resent;
			if (!resend_packet(p, true)) break;
			m_duplicate_acks = 0;
			if (num_resent >= sack_resend_limit) break;
		}
	}
}

// copies data from the write buffer into the packet
// pointed to by ptr
void utp_socket_impl::write_payload(char* ptr, int size)
{
#ifdef TORRENT_DEBUG
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
		TORRENT_ASSERT(to_copy >= 0);
		TORRENT_ASSERT(to_copy < INT_MAX / 2 && m_written < INT_MAX / 2);
		memcpy(ptr, static_cast<char const*>(i->buf), to_copy);
		size -= to_copy;
		if (m_written == 0)
		{
			m_write_timeout = now + milliseconds(300);
			UTP_LOGV("%8p: setting write timeout to 300 ms from now\n", this);
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

#ifdef TORRENT_DEBUG
	write_buffer_size = 0;
	for (std::vector<iovec_t>::iterator i = m_write_buffer.begin()
		, end(m_write_buffer.end()); i != end; ++i)
	{
		write_buffer_size += i->len;
	}
	TORRENT_ASSERT(m_write_buffer_size == write_buffer_size);
#endif
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
			// resend_packet might have failed
			if (m_state == UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_DELETE) return false;
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
		// the SACK bitfield should ideally fit all
		// the pieces we have successfully received
		sack = (m_inbuf.span() + 7) / 8;
		if (sack > 32) sack = 32;
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
	if (m_bytes_in_flight + payload_size > (std::min)(int(m_cwnd >> 16)
		, int(m_adv_wnd - m_bytes_in_flight)))
	{
		// this means there's not enough room in the send window for
		// another packet. We have to hold off sending this data.
		// we still need to send an ACK though
		payload_size = 0;

		// we're constrained by the window size
		m_last_cwnd_hit = time_now_hires();
		m_cwnd_full = true;

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

	if (((m_seq_nr - m_acked_seq_nr) & ACK_MASK) > 1
		&& payload_size < m_mtu - header_size
		&& !ack
		&& m_nagle)
	{
		// this is nagle. If we don't have a full packet
		// worth of payload to send AND we have at least
		// one outstanding packet, hold off. Once the
		// outstanding packet is acked, we'll send this
		// payload
		UTP_LOGV("%8p: NAGLE not enough payload send_buffer_size:%d cwnd:%d "
			"ret:%d adv_wnd:%d in-flight:%d mtu:%d\n"
			, this, m_write_buffer_size, int(m_cwnd >> 16)
			, ret, m_adv_wnd, m_bytes_in_flight, m_mtu);
		return false;
	}

	int packet_size = header_size + payload_size;

	// MTU DISCOVERY
	bool use_as_probe = false;
	if (m_mtu_seq == 0
		&& packet_size > m_mtu_floor
		&& m_seq_nr != 0)
	{
		use_as_probe = true;
		m_mtu_seq = m_seq_nr;
	}

	packet* p;
	// we only need a heap allocation if we have payload and
	// need to keep the packet around (in the outbuf)
	if (payload_size) p = (packet*)malloc(sizeof(packet) + packet_size);
	else p = (packet*)TORRENT_ALLOCA(char, sizeof(packet) + packet_size);

	p->size = packet_size;
	p->header_size = packet_size - payload_size;
	p->num_transmissions = 1;
	p->need_resend = false;
	p->mtu_probe = use_as_probe;
	char* ptr = p->buf;
	utp_header* h = (utp_header*)ptr;
	ptr += sizeof(utp_header);

	h->type_ver = ((payload_size ? ST_DATA : ST_STATE) << 4) | 1;
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
		"ret:%d adv_wnd:%d in-flight:%d mtu:%d timestamp:%u time_diff:%u "
		"mtu_probe:%d\n"
		, this, int(h->seq_nr), int(h->ack_nr), packet_type_names[h->get_type()]
		, m_send_id, print_endpoint(udp::endpoint(m_remote_address, m_port)).c_str()
		, packet_size, m_error.message().c_str(), m_write_buffer_size, int(m_cwnd >> 16)
		, ret, m_adv_wnd, m_bytes_in_flight, m_mtu, boost::uint32_t(h->timestamp_microseconds)
		, boost::uint32_t(h->timestamp_difference_microseconds), int(p->mtu_probe));
#endif

	TORRENT_ASSERT(!m_error);

	error_code ec;
#ifdef TORRENT_DEBUG
	// simulate 1% packet loss
//	if ((rand() % 100) > 0)
#endif
	m_sm->send_packet(udp::endpoint(m_remote_address, m_port)
		, (char const*)h, packet_size, ec
		, use_as_probe ? utp_socket_manager::dont_fragment : 0);

	++m_out_packets;

	if (ec == error::message_size && use_as_probe)
	{
		m_mtu_ceiling = m_mtu - 1;
		update_mtu_limits();
		// TODO: we might want to do something else here
		// as well, to resend the packet immediately without
		// it being an MTU probe
	}
	else if (ec)
	{
		m_error = ec;
		m_state = UTP_STATE_ERROR_WAIT;
		test_socket_state();
		if (payload_size) free(p);
		return false;
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

bool utp_socket_impl::resend_packet(packet* p, bool fast_resend)
{
	// for fast re-sends the packet hasn't been marked as needing resending
	TORRENT_ASSERT(p->need_resend || fast_resend);

	TORRENT_ASSERT(!m_error);

	if (fast_resend
		&& ((m_acked_seq_nr + 1) & ACK_MASK) == m_mtu_seq
		&& m_mtu_seq != 0)
	{
		// we got multiple acks for the packet before our probe, assume
		// it was dropped because it was too big
		m_mtu_ceiling = m_mtu - 1;
		update_mtu_limits();
	}

	// we can only resend the packet if there's
	// enough space in our congestion window
	int window_size_left = (std::min)(int(m_cwnd >> 16), int(m_adv_wnd)) - m_bytes_in_flight;
	if (!fast_resend && p->size - p->header_size > window_size_left)
	{
		m_last_cwnd_hit = time_now_hires();
		m_cwnd_full = true;
		return false;
	}

	// plus one since we have fast-resend as well, which doesn't
	// necessarily trigger by a timeout
	TORRENT_ASSERT(p->num_transmissions < m_sm->num_resends() + 1);

	TORRENT_ASSERT(p->size - p->header_size >= 0);
	if (p->need_resend) m_bytes_in_flight += p->size - p->header_size;

	++p->num_transmissions;
	p->need_resend = false;
	utp_header* h = (utp_header*)p->buf;
	// update packet header
	h->timestamp_difference_microseconds = m_reply_micro;
	p->send_time = time_now_hires();
	h->timestamp_microseconds = boost::uint32_t(total_microseconds(p->send_time - min_time()));
	if (h->extension == 0)
	{
		// if extension != 0, there might be a SACK in the header
		// and we can't update the ack field (since the SACK bits
		// depend on it). If it's zero however, we can update it.
		h->ack_nr = m_ack_nr;
	}

	error_code ec;
	m_sm->send_packet(udp::endpoint(m_remote_address, m_port)
		, (char const*)p->buf, p->size, ec);
	++m_out_packets;

#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: re-sending packet seq_nr:%d ack_nr:%d type:%s "
		"id:%d target:%s size:%d error:%s send_buffer_size:%d cwnd:%d "
		"adv_wnd:%d in-flight:%d mtu:%d timestamp:%u time_diff:%u\n"
		, this, int(h->seq_nr), int(h->ack_nr), packet_type_names[h->get_type()]
		, m_send_id, print_endpoint(udp::endpoint(m_remote_address, m_port)).c_str()
		, p->size, ec.message().c_str(), m_write_buffer_size, int(m_cwnd >> 16)
		, m_adv_wnd, m_bytes_in_flight, m_mtu, boost::uint32_t(h->timestamp_microseconds)
		, boost::uint32_t(h->timestamp_difference_microseconds));
#endif

	if (ec)
	{
		m_error = ec;
		m_state = UTP_STATE_ERROR_WAIT;
		test_socket_state();
		return false;
	}

	return true;
}

void utp_socket_impl::experienced_loss(int seq_nr)
{
	// since loss often comes in bursts, we only cut the
	// window in half once per RTT. This is implemented
	// by limiting which packets can cause us to cut the
	// window size. The first packet that's lost will
	// update the limit to the last sequence number we sent.
	// i.e. only packet sent after this loss can cause another
	// window size cut
	if (compare_less_wrap(seq_nr, m_loss_seq_nr, ACK_MASK)) return;
	
	// cut window size in 2
	m_cwnd = (std::max)(m_cwnd * m_sm->loss_multiplier() / 100, boost::int64_t(m_mtu << 16));
	m_loss_seq_nr = m_seq_nr;
	UTP_LOGV("%8p: Lost packet %d caused cwnd cut\n", this, seq_nr);

	// the window size could go below one MMS here, if it does,
	// we'll get a timeout in about one second
	
	// if we happen to be in slow-start mode, we need to leave it
	m_slow_start = false;
}

void utp_socket_impl::maybe_inc_acked_seq_nr()
{
	bool incremented = false;
	// don't pass m_seq_nr, since we move into sequence
	// numbers that haven't been sent yet, and aren't
	// supposed to be in m_outbuf
	// if the slot in m_outbuf is 0, it means the
	// packet has been ACKed and removed from the send buffer
	while (((m_acked_seq_nr + 1) & ACK_MASK) != m_seq_nr
		&& m_outbuf.at((m_acked_seq_nr + 1) & ACK_MASK) == 0)
	{
		// increment the fast resend sequence number
		if (m_fast_resend_seq_nr == m_acked_seq_nr)
			m_fast_resend_seq_nr = (m_fast_resend_seq_nr + 1) & ACK_MASK;

		m_acked_seq_nr = (m_acked_seq_nr + 1) & ACK_MASK;
		incremented = true;
	}

	if (!incremented) return;

	// update loss seq number if it's less than the packet
	// that was just acked. If loss seq nr is greater, it suggests
	// that we're still in a window that has experienced loss
	if (compare_less_wrap(m_loss_seq_nr, m_acked_seq_nr, ACK_MASK))
		m_loss_seq_nr = m_acked_seq_nr;
	m_duplicate_acks = 0;
}

void utp_socket_impl::ack_packet(packet* p, ptime const& receive_time
	, boost::uint32_t& min_rtt, boost::uint16_t seq_nr)
{
	TORRENT_ASSERT(p);

	// verify that the packet we're removing was in fact sent
	// with the sequence number we expect
	TORRENT_ASSERT(((utp_header*)p->buf)->seq_nr == seq_nr);

	if (!p->need_resend)
	{
		TORRENT_ASSERT(m_bytes_in_flight >= p->size - p->header_size);
		m_bytes_in_flight -= p->size - p->header_size;
	}

	if (seq_nr == m_mtu_seq && m_mtu_seq != 0)
	{
		TORRENT_ASSERT(p->mtu_probe);
		// our mtu probe was acked!
		m_mtu_floor = m_mtu;
		update_mtu_limits();
	}

	// increment the acked sequence number counter
	maybe_inc_acked_seq_nr();

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
	TORRENT_ASSERT(ec);
	bool ret = m_read_handler || m_write_handler || m_connect_handler;
	
	// calling the callbacks with m_userdata being 0 will just crash
	TORRENT_ASSERT((ret && bool(m_userdata)) || !ret);

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
	if (ph->get_type() != ST_DATA) return false;

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
		if (m_inbuf.at(ph->seq_nr))
		{
			UTP_LOGV("%8p: already received seq_nr: %d\n"
				, this, int(ph->seq_nr));
			return true;
		}

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
	// it might be possible to get here twice, in which we need to
	// cancel any new handlers as well, even though we're already
	// in the delete state
	if (!m_error) return false;
	TORRENT_ASSERT(m_state == UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_DELETE);

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
	return false;
}

void utp_socket_impl::init_mtu(int link_mtu, int utp_mtu)
{
	// if we're in a RAM constrained environment, don't increase
	// the buffer size for interfaces with large MTUs. Just stick
	// to ethernet frame sizes
	if (m_sm->allow_dynamic_sock_buf())
	{
		// Make sure that we have enough socket buffer space
		// for sending and receiving packets of this size
		// add 10% for smaller ACKs and other overhead
		m_sm->set_sock_buf(link_mtu * 11 / 10);
	}
	else if (link_mtu > TORRENT_ETHERNET_MTU)
	{
		// we can't use larger packets than this since we're
		// not allocating any more memory for socket buffers
		int decrease = link_mtu - TORRENT_ETHERNET_MTU;
		utp_mtu -= decrease;
		link_mtu -= decrease;
	}

	m_mtu = utp_mtu;
	m_mtu_ceiling = utp_mtu;
	if (m_mtu_floor > utp_mtu) m_mtu_floor = utp_mtu;

	// if the window size is smaller than one packet size
	// set it to one
	if ((m_cwnd >> 16) < m_mtu) m_cwnd = boost::int64_t(m_mtu) << 16;

	UTP_LOGV("%8p: intializing MTU to: %d [%d, %d]\n"
		, this, m_mtu, m_mtu_floor, m_mtu_ceiling);
}

// return false if this is an invalid packet
bool utp_socket_impl::incoming_packet(char const* buf, int size
	, udp::endpoint const& ep, ptime receive_time)
{
	utp_header* ph = (utp_header*)buf;

	if (ph->get_version() != 1)
	{
		UTP_LOGV("%8p: incoming packet version:%d (ignored)\n"
			, this, int(ph->get_version()));
		return false;
	}

	// SYN packets have special (reverse) connection ids
	if (ph->get_type() != ST_SYN && ph->connection_id != m_recv_id)
	{
		UTP_LOGV("%8p: incoming packet id:%d expected:%d (ignored)\n"
			, this, int(ph->connection_id), int(m_recv_id));
		return false;
	}

	if (ph->get_type() >= NUM_TYPES)
	{
		UTP_LOGV("%8p: incoming packet type:%d (ignored)\n"
			, this, int(ph->get_type()));
		return false;
	}

	if (m_state == UTP_STATE_NONE && ph->get_type() == ST_SYN)
	{
		m_remote_address = ep.address();
		m_port = ep.port();
	}

	if (m_state != UTP_STATE_NONE && ph->get_type() == ST_SYN)
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

		if (prev_base && base_change < 0 && base_change > -10000 && m_delay_hist.initialized())
		{
			// their base delay went down. This is caused by clock drift. To compensate,
			// adjust our base delay upwards
			// don't adjust more than 10 ms. If the change is that big, something is probably wrong
			m_delay_hist.adjust_base(-base_change);
		}

		UTP_LOGV("%8p: incoming packet reply_micro:%u base_change:%d\n"
			, this, m_reply_micro, prev_base ? base_change : 0);
	}

	if (ph->get_type() == ST_RESET)
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
	if (m_state == UTP_STATE_SYN_SENT && ph->get_type() == ST_STATE)
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
		&& ph->get_type() == ST_DATA
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

	TORRENT_ASSERT(m_outbuf.at((m_acked_seq_nr + 1) & ACK_MASK) || ((m_seq_nr - m_acked_seq_nr) & ACK_MASK) <= 1);

	// has this packet already been ACKed?
	// if the ACK we just got is less than the max ACKed
	// sequence number, it doesn't tell us anything.
	// So, only act on it if the ACK is greater than the last acked
	// sequence number
	if (m_state != UTP_STATE_NONE && compare_less_wrap(m_acked_seq_nr, ph->ack_nr, ACK_MASK))
	{
		int const next_ack_nr = ph->ack_nr;

		for (int ack_nr = (m_acked_seq_nr + 1) & ACK_MASK;
			ack_nr != ((next_ack_nr + 1) & ACK_MASK);
			ack_nr = (ack_nr + 1) & ACK_MASK)
		{
			if (m_fast_resend_seq_nr == ack_nr)
				m_fast_resend_seq_nr = (m_fast_resend_seq_nr + 1) & ACK_MASK;
			packet* p = (packet*)m_outbuf.remove(ack_nr);

			if (!p) continue;

			acked_bytes += p->size - p->header_size;
			ack_packet(p, receive_time, min_rtt, ack_nr);
		}

		maybe_inc_acked_seq_nr();
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
		int len = unsigned(*ptr++);
		if (ptr - buf + len > size_t(size))
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
	
	// the send operation in parse_sack() may have set the socket to an error
	// state, in which case we shouldn't continue
	if (m_state == UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_DELETE) return true;

	if (m_duplicate_acks >= dup_ack_limit
		&& ((m_acked_seq_nr + 1) & ACK_MASK) == m_fast_resend_seq_nr)
	{
		// LOSS

		UTP_LOGV("%8p: Packet %d lost.\n", this, m_fast_resend_seq_nr);

		// resend the lost packet
		packet* p = (packet*)m_outbuf.at(m_fast_resend_seq_nr);
		TORRENT_ASSERT(p);
		if (p)
		{
			experienced_loss(m_fast_resend_seq_nr);
			resend_packet(p, true);
			if (m_state == UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_DELETE) return true;
		}
		// don't fast-resend this again
		m_fast_resend_seq_nr = (m_fast_resend_seq_nr + 1) & ACK_MASK;
	}

	// ptr points to the payload of the packet
	// size is the packet size, payload is the
	// number of payload bytes are in this packet
	const int header_size = ptr - buf;
	const int payload_size = size - header_size;

#if TORRENT_UTP_LOG
	UTP_LOGV("%8p: incoming packet seq_nr:%d ack_nr:%d type:%s id:%d size:%d timestampdiff:%u timestamp:%u "
			"our ack_nr:%d our seq_nr:%d our acked_seq_nr:%d our state:%s\n"
		, this, int(ph->seq_nr), int(ph->ack_nr), packet_type_names[ph->get_type()]
		, int(ph->connection_id), payload_size, boost::uint32_t(ph->timestamp_difference_microseconds)
		, boost::uint32_t(ph->timestamp_microseconds), m_ack_nr, m_seq_nr, m_acked_seq_nr, socket_state_names[m_state]);
#endif

	if (ph->get_type() == ST_FIN)
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
				if (m_state == UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_DELETE) return true;
			}
			else
			{
				send_fin();
				if (m_state == UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_DELETE) return true;
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
			if (ph->get_type() == ST_SYN)
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
				m_seq_nr = random();
				m_acked_seq_nr = (m_seq_nr - 1) & ACK_MASK;
				m_loss_seq_nr = m_acked_seq_nr;
				m_fast_resend_seq_nr = m_seq_nr;

				TORRENT_ASSERT(m_send_id == ph->connection_id);
				TORRENT_ASSERT(m_recv_id == ((m_send_id + 1) & 0xffff));

				send_pkt(true);

				return true;
			}
			else
			{
#if TORRENT_UTP_LOG
				UTP_LOGV("%8p: type:%s state:%s (ignored)\n"
					, this, packet_type_names[ph->get_type()], socket_state_names[m_state]);
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

			TORRENT_ASSERT(!m_error);
			m_state = UTP_STATE_CONNECTED;
#if TORRENT_UTP_LOG
			UTP_LOGV("%8p: state:%s\n", this, socket_state_names[m_state]);
#endif

			// only progress our ack_nr on ST_DATA messages
			// since our m_ack_nr is uninitialized at this point
			// we still need to set it to something regardless
			if (ph->get_type() == ST_DATA)
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
			{
				do_ledbat(acked_bytes, delay, prev_bytes_in_flight, receive_time);
				m_send_delay = delay;
			}

			m_recv_delay = (std::min)(their_delay, min_rtt);

			consume_incoming_data(ph, ptr, payload_size, receive_time);

			// the parameter to send_pkt tells it if we're acking data
			// If we are, we'll send an ACK regardless of if we have any
			// space left in our send window or not. If we just got an ACK
			// (i.e. ST_STATE) we're not ACKing anything. If we just
			// received a FIN packet, we need to ack that as well
			bool has_ack = ph->get_type() == ST_DATA || ph->get_type() == ST_FIN || ph->get_type() == ST_SYN;
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
			maybe_trigger_send_callback(receive_time);
			if (m_state == UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_DELETE) return true;

			// Everything up to the FIN has been receieved, respond with a FIN
			// from our side.
			if (m_eof && m_ack_nr == ((m_eof_seq_nr - 1) & ACK_MASK))
			{
				UTP_LOGV("%8p: incoming stream consumed\n", this);

				// This transitions to the UTP_STATE_FIN_SENT state.
				send_fin();
				if (m_state == UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_DELETE) return true;
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
					"max_window:%u "
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
					"cur_window_packets:%u "
					"packet_size:%d "
					"their_delay_base:%s "
					"their_actual_delay:%u "
					"seq_nr:%u "
					"acked_seq_nr:%u "
					"reply_micro:%u "
					"min_rtt:%u "
					"send_buffer:%d "
					"recv_buffer:%d "
					"fast_resend_seq_nr:%d "
					"\n"
					, this
					, sample
					, float(delay / 1000.f)
					, float(their_delay / 1000.f)
					, float(int(m_sm->target_delay() - delay)) / 1000.f
					, boost::uint32_t(m_cwnd >> 16)
					, 0
					, our_delay_base
					, float(delay + their_delay) / 1000.f
					, m_sm->target_delay() / 1000
					, acked_bytes
					, m_bytes_in_flight
					, 0.f // float(scaled_gain)
					, m_rtt.mean()
					, int((m_cwnd * 1000 / (m_rtt.mean()?m_rtt.mean():50)) >> 16)
					, 0
					, m_adv_wnd
					, packet_timeout()
					, int(total_milliseconds(m_timeout - receive_time))
					, int(total_microseconds(receive_time - min_time()))
					, (m_seq_nr - m_acked_seq_nr) & ACK_MASK
					, m_mtu
					, their_delay_base
					, boost::uint32_t(m_reply_micro)
					, m_seq_nr
					, m_acked_seq_nr
					, m_reply_micro
					, min_rtt / 1000
					, m_write_buffer_size
					, m_read_buffer_size
					, m_fast_resend_seq_nr);
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

				if (!m_attached)
				{
					UTP_LOGV("%8p: close initiated here, delete socket\n", this);
					m_error = asio::error::eof;
					m_state = UTP_STATE_DELETE;
					test_socket_state();
				}
				else
				{
					UTP_LOGV("%8p: closing socket\n", this);
					m_error = asio::error::eof;
					m_state = UTP_STATE_ERROR_WAIT;
					test_socket_state();
				}
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
	boost::int64_t scaled_gain;
  
	if (delay >= target_delay / 2)
	{
		UTP_LOGV("%8p: off_target: %d slow_start -> 0\n", this, target_delay - delay);
		m_slow_start = false;
	}

	boost::int64_t linear_gain = (window_factor * delay_factor) >> 16;
	linear_gain *= boost::int64_t(m_sm->gain_factor());

	if (m_slow_start)
	{
		scaled_gain = (std::max)((window_factor * m_cwnd) >> 16, linear_gain);
	}
	else
	{
		scaled_gain = linear_gain;
	}

	if (scaled_gain > 0 && !m_cwnd_full
		&& m_last_cwnd_hit + milliseconds((std::max)(m_rtt.mean(), 500)) < now)
	{
		UTP_LOGV("%8p: last_cwnd_hit:%d full_cwnd:%d scaled_gain -> 0, slow_start -> 0\n", this
			, total_milliseconds(now - m_last_cwnd_hit), int(m_cwnd_full));
		// we haven't bumped into the cwnd limit size in the last second
		// this probably means we have a send rate limit, so we shouldn't make
		// the cwnd size any larger
		scaled_gain = 0;
	}

	UTP_LOGV("%8p: do_ledbat delay:%d off_target: %d window_factor:%f target_factor:%f "
		"scaled_gain:%f cwnd:%d slow_start:%d\n"
		, this, delay, target_delay - delay, window_factor / float(1 << 16)
		, delay_factor / float(1 << 16)
		, scaled_gain / float(1 << 16), int(m_cwnd >> 16)
		, int(m_slow_start));

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


	int window_size_left = (std::min)(int(m_cwnd >> 16), int(m_adv_wnd)) - in_flight + acked_bytes;
	if (window_size_left >= m_mtu)
	{
		UTP_LOGV("%8p: mtu:%d in_flight:%d adv_wnd:%d cwnd:%d acked_bytes:%d cwnd_full -> 0\n"
			, this, m_mtu, in_flight, int(m_adv_wnd), int(m_cwnd >> 16), acked_bytes);
		m_cwnd_full = false;
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

	// avoid overflow by simply capping based on number of timeouts as well
	if (m_num_timeouts >= 7) return 60000;

	int timeout = (std::max)(m_sm->min_timeout(), m_rtt.mean() + m_rtt.avg_deviation() * 2);
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

	TORRENT_ASSERT(m_outbuf.at((m_acked_seq_nr + 1) & ACK_MASK) || ((m_seq_nr - m_acked_seq_nr) & ACK_MASK) <= 1);

	// don't hang on to received data for too long, and don't
	// wait too long telling the client we've sent some data.
	// these functions will trigger time callback if we have
	// a reason to and it's been long enough since we sent or
	// received the data
	maybe_trigger_receive_callback(now);
	maybe_trigger_send_callback(now);

	// if we're already in an error state, we're just waiting for the
	// client to perform an operation so that we can communicate the
	// error. No need to do anything else with this socket
	if (m_state == UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_DELETE) return;

	if (now > m_timeout)
	{
		// TIMEOUT!
		// set cwnd to 1 MSS

		// the window went from less than one MSS to one MSS
		// we can now sent messages again, the send window was opened
		if ((m_cwnd >> 16) < m_mtu) window_opened = true;

		if (m_bytes_in_flight == 0 && (m_cwnd >> 16) >= m_mtu)
		{
			// this is just a timeout because this direction of
			// the stream is idle. Don't reset the cwnd, just decay it
			m_cwnd = m_cwnd * 2 / 3;
		}
		else
		{
			// we timed out because a packet was not ACKed or because
			// the cwnd was made smaller than one packet
			m_cwnd = boost::int64_t(m_mtu) << 16;
		}

		if (m_outbuf.size()) ++m_num_timeouts;

		if (m_num_timeouts > m_sm->num_resends())
		{
			// the connection is dead
			m_error = asio::error::timed_out;
			m_state = UTP_STATE_ERROR_WAIT;
			test_socket_state();
			return;
		}

		m_timeout = now + milliseconds(packet_timeout());
	
		UTP_LOGV("%8p: timeout resetting cwnd:%d\n"
			, this, int(m_cwnd >> 16));

		if (((m_acked_seq_nr + 1) & ACK_MASK) == m_mtu_seq
			&& ((m_seq_nr - 1) & ACK_MASK) == m_mtu_seq
			&& m_mtu_seq != 0)
		{
			// we timed out, and the only outstanding packet
			// we had was the probe. Assume it was dropped
			// because it was too big
			m_mtu_ceiling = m_mtu - 1;
			update_mtu_limits();
		}

		// we dropped all packets, that includes the mtu probe
		m_mtu_seq = 0;

		// since we've already timed out now, don't count
		// loss that we might detect for packets that just
		// timed out
		m_loss_seq_nr = m_seq_nr;

		// we need to go one past m_seq_nr to cover the case
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
			UTP_LOGV("%8p: Packet %d lost.\n", this, i);
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
			if (m_state == UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_DELETE) return;
		}
		else if (m_state < UTP_STATE_FIN_SENT)
		{
			send_pkt(false);
			if (m_state == UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_DELETE) return;
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
		if (m_state == UTP_STATE_ERROR_WAIT || m_state == UTP_STATE_DELETE) return;
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

	TORRENT_ASSERT(int(size) == m_receive_buffer_size);
}

}

