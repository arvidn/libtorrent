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

#define CCONTROL_TARGET 100

namespace libtorrent
{

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
			char* p = m_storage;
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

enum { ST_DATA = 0, ST_FIN, ST_STATE, ST_RESET, ST_SYN } type;

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

class utp_stream : public proxy_base
{
public:

	explicit utp_stream(io_service& ios, utp_socket_manager& sm, boost::uint16_t id)
		: proxy_base(ios)
		, m_sm(sm)
		, m_send_id(id + 1)
		, m_recv_id(id)
		, m_state(UTP_STATE_NONE)
	{}

	typedef boost::function<void(error_code const&)> handler_type;

	template <class Handler>
	void async_connect(endpoint_type const& endpoint, Handler const& handler)
	{
		TORRENT_ASSERT(m_state == UTP_STATE_NONE);
		// store handler
		async_connect_impl();
	}
	
	void bind(endpoint_type const& ep, error_code& ec);
	void bind(udp::endpoint const& ep, error_code& ec);
	bool incoming_packet(char const* buf, int size);

	~utp_stream();
	
private:
	
	void async_connect_impl();

	utp_socket_manager& m_sm;
	boost::uint16_t m_send_id;
	boost::uint16_t m_recv_id;
	boost::uint16_t m_ack_nr;
	boost::uint16_t m_seq_nr;
	enum state_t {
		UTP_STATE_NONE,
		UTP_STATE_SYN_SENT,
		UTP_STATE_CONNECTED,
		UTP_STATE_FIN_SENT
	};
	unsigned char m_state;
};

}

#endif
