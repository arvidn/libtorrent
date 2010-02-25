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

namespace libtorrent {

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

	// this is used to keep the minimum difference in
	// timestamps.
	struct delay_history
	{
		void add_sample(boost::uint32_t v);
		void minimum() const;
		void tick();
	};

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

enum
{
	ACK_MASK = 0xffff
};
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

// compare if lhs is less than rhs, taking wrapping
// into account. if lhs is close to UINT_MAX and rhs
// is close to 0, lhs is assumed to have wrapped and
// considered smaller
bool compare_less_wrap(boost::uint32_t lhs, boost::uint32_t rhs, boost::uint32_t mask)
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

// since the uTP socket state may be needed after the
// utp_stream is closed, it's kept in a separate struct
// whose lifetime is not tied to the lifetime of utp_stream

struct utp_socket_impl
{
	utp_socket_impl()
		: m_sm(0)
		, m_send_id(0)
		, m_recv_id(0)
		, m_state(UTP_STATE_NONE)
	{}

	void tick();
	bool incoming_packet(char const* buf, int size);
	bool should_delete() const { return m_state == UTP_STATE_DELETE; }
	udp::endpoint remote_endpoint() const { return udp::endpoint(m_remote_address, m_port); }

	utp_socket_manager* m_sm;

	address m_remote_address;
	boost::uint16_t m_port;

	// the send and receive buffers
	// maps packet sequence numbers
	packet_buffer m_inbuf;
	packet_buffer m_outbuf;

	boost::uint16_t m_send_id;
	boost::uint16_t m_recv_id;
	boost::uint16_t m_ack_nr;
	boost::uint16_t m_seq_nr;
	enum state_t {
		UTP_STATE_NONE,
		UTP_STATE_SYN_SENT,
		UTP_STATE_CONNECTED,
		UTP_STATE_FIN_SENT,
		UTP_STATE_DELETE
	};
	unsigned char m_state;

	libtorrent::buffer m_receive_buffer;
	boost::function<void(error_code const&)> m_connect_handler;
	boost::function<void(error_code const&, std::size_t)> m_read_handler;
	boost::function<void(error_code const&, std::size_t)> m_write_handler;
	std::size_t m_bytes_written;
	std::vector<asio::const_buffer> m_write_buffer;
	std::vector<asio::mutable_buffer> m_read_buffer;

	// the timestamp diff in the last packet received
	// this is what we'll send back
	boost::uint32_t m_reply_micro;
	// the max number of bytes in-flight
	boost::uint32_t m_cwnd;

	// this is the sequence number of the packet that
	// everything has been ACKed up to. Everything we've
	// sent up to this point has been received by the other
	// end.
	boost::uint16_t m_acked_seq_nr;
};

utp_stream::~utp_stream()
{
	if (m_impl) m_impl->destroy();
	m_impl = 0;
}

void utp_stream::assign(utp_socket_impl* s)
{
	TORRENT_ASSERT(m_impl == 0);
	m_impl = s;
}

void utp_stream::set_manager(utp_socket_manager* sm)
{
	TORRENT_ASSERT(m_impl);
	m_impl->m_sm = sm;
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
	h.ack_nr = pf->seq_nr;
	h.timestamp_microseconds = total_microseconds(time_now_hires() - min_time())
	m_sm->send_packet(remote_endpoint(), &h, sizeof(h));
}

bool utp_socket_impl::incoming_packet(char const* buf, int size)
{
	ptime receive_time = time_now_hires();

	utp_header* ph = (utp_header*)buf;

	if (ph->ver != 1) return false;
	if (ph->connection_id != m_recv_id) return false;
	if (ph->type >= NUM_TYPES) return false;

	// this is the difference between their send time and our receive time
	m_reply_micro = total_microseconds(receive_time - min_time()) - ph->timestamp_microseconds;

	if (ph->type == ST_RESET)
	{
		cancel_handlers(asio::error::connection_reset);
		m_state = UTP_STATE_DELETE;
		return true;
	}

	// is this ACK valid? If the other end is ACKing
	// a packet that hasn't been sent yet, respond with
	// a reset
	// since m_seq_nr is the sequence number of the next packet
	// we'll send (and m_seq_nr-1 was the last packet we sent),
	// if the ACK we got is greater than the last packet we sent
	// something is wrong. Reset.
	if (compare_less_wrap((m_seq_nr - 1) & ACK_MASK, ph->ack_nr, ACK_MASK))
	{
		send_reset(ph);
		return true;
	}

	// has this packet already been ACKed?
	// if the ACK we just got is less than the max ACKed
	// sequence number, it doesn't tell us anything.
	// So, only act on it if the ACK is greater than the last acked
	// sequence number
	if (compare_less_wrap(m_acked_seq_nr, ph->ack_nr, ACK_MASK))
	{
#error remove packets from send buffer	
	}

	switch (m_state)
	{
		case UTP_STATE_NONE:
		case UTP_STATE_DELETE::
		default:
		{
			// respond with a reset
			send_reset(ph);
			return true;
		}
		case UTP_STATE_SYN_SENT:
		{
			if (ph->ack != m_seq_nr - 1)
				return true;

			// #error notify client that we're connected!
			m_state = UTP_STATE_CONNECTED;
			return;
		}
		case UTP_STATE_CONNECTED:
		{
			do_ledbat();
			return;
		}
		case UTP_STATE_FIN_SENT:
		{
			// wait for a graceful close of the connection
			if (ph->type == ST_FIN)
				m_state = UTP_STATE_DELETE;
			return true;
		}
	}

	return false;
}

void utp_stream::bind(endpoint_type const& ep, error_code& ec)
{
}

void utp_stream::bind(udp::endpoint const& ep, error_code& ec)
{
}

void utp_socket_impl::tick()
{
}

}
