/*

Copyright (c) 2014-2018, Arvid Norberg, Steven Siloti
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

#ifndef TORRENT_RECEIVE_BUFFER_HPP_INCLUDED
#define TORRENT_RECEIVE_BUFFER_HPP_INCLUDED

#include "libtorrent/buffer.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/sliding_average.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

#include <climits>

namespace libtorrent {

struct TORRENT_EXTRA_EXPORT receive_buffer
{
	friend struct crypto_receive_buffer;

	int packet_size() const { return m_packet_size; }
	int packet_bytes_remaining() const
	{
		TORRENT_ASSERT(m_recv_start == 0);
		TORRENT_ASSERT(m_packet_size > 0);
		return m_packet_size - m_recv_pos;
	}

	int max_receive() const;

	bool packet_finished() const { return m_packet_size <= m_recv_pos; }
	int pos() const { return m_recv_pos; }
	int capacity() const { return aux::numeric_cast<int>(m_recv_buffer.size()); }
	int watermark() const { return aux::numeric_cast<int>(m_watermark.mean()); }

	span<char> reserve(int size);
	void grow(int limit);

	// tell the buffer we just received more bytes at the end of it. This will
	// advance the end cursor
	void received(int bytes_transferred)
	{
		TORRENT_ASSERT(m_packet_size > 0);
		m_recv_end += bytes_transferred;
		TORRENT_ASSERT(m_recv_pos <= int(m_recv_buffer.size()));
	}

	// tell the buffer we consumed some bytes of it. This will advance the read
	// cursor
	int advance_pos(int bytes);

	// has the read cursor reached the end cursor?
	bool pos_at_end() { return m_recv_pos == m_recv_end; }

	// size = the packet size to remove from the receive buffer
	// packet_size = the next packet size to receive in the buffer
	// offset = the offset into the receive buffer where to remove `size` bytes
	void cut(int size, int packet_size, int offset = 0);

	// return the interval between the start of the buffer to the read cursor.
	// This is the "current" packet.
	span<char const> get() const;

#if !defined TORRENT_DISABLE_ENCRYPTION
	// returns the buffer from the current packet start position to the last
	// received byte (possibly part of another packet)
	span<char> mutable_buffer();

	// returns the last 'bytes' from the receive buffer
	span<char> mutable_buffer(int bytes);
#endif

	// the purpose of this function is to free up and cut off all messages
	// in the receive buffer that have been parsed and processed.
	void normalize(int force_shrink = 0);
	bool normalized() const { return m_recv_start == 0; }

	void reset(int packet_size);

#if TORRENT_USE_INVARIANT_CHECKS
	void check_invariant() const
	{
		TORRENT_ASSERT(m_recv_end >= m_recv_start);
		TORRENT_ASSERT(m_recv_end <= int(m_recv_buffer.size()));
		TORRENT_ASSERT(m_recv_start <= int(m_recv_buffer.size()));
		TORRENT_ASSERT(m_recv_start + m_recv_pos <= int(m_recv_buffer.size()));
	}
#endif

private:
	// explicitly disallow assignment, to silence msvc warning
	receive_buffer& operator=(receive_buffer const&);

	// m_recv_buffer.data() (start of actual receive buffer)
	// |
	// |      m_recv_start (start of current packet)
	// |      |
	// |      |    m_recv_pos (number of bytes consumed
	// |      |    |  by upper layer, from logical receive buffer)
	// |      |    |
	// |      x---------x
	// |      |         |        m_recv_buffer.size() (end of actual receive buffer)
	// |      |         |        |
	// v      v         v        v
	// *------==========---------
	//                     ^
	//                     |
	//                     |
	// ------------------->x  m_recv_end (end of received data,
	//                          beyond this point is garbage)
	// m_recv_buffer

	// the start of the logical receive buffer
	int m_recv_start = 0;

	// the number of valid, received bytes in m_recv_buffer
	int m_recv_end = 0;

	// the byte offset in m_recv_buffer that we have
	// are passing on to the upper layer. This is
	// always <= m_recv_end
	int m_recv_pos = 0;

	// the size (in bytes) of the bittorrent message
	// we're currently receiving
	int m_packet_size = 0;

	// keep track of how much of the receive buffer we use, if we're not using
	// enough of it we shrink it
	sliding_average<std::ptrdiff_t, 20> m_watermark;

	buffer m_recv_buffer;
};

#if !defined TORRENT_DISABLE_ENCRYPTION
// Wraps a receive_buffer to provide the ability to inject
// possibly authenticated crypto beneath the bittorrent protocol.
// When authenticated crypto is in use the wrapped receive_buffer
// holds the receive state of the crypto layer while this class
// tracks the state of the bittorrent protocol.
struct crypto_receive_buffer
{
	explicit crypto_receive_buffer(receive_buffer& next)
		: m_connection_buffer(next)
	{}

	span<char> mutable_buffer() { return m_connection_buffer.mutable_buffer(); }

	bool packet_finished() const;

	bool crypto_packet_finished() const
	{
		return m_recv_pos == (std::numeric_limits<int>::max)()
			|| m_connection_buffer.packet_finished();
	}

	int packet_size() const;

	int crypto_packet_size() const
	{
		TORRENT_ASSERT(m_recv_pos != (std::numeric_limits<int>::max)());
		return m_connection_buffer.packet_size() - m_recv_pos;
	}

	int pos() const;

	void cut(int size, int packet_size, int offset = 0);

	void crypto_cut(int size, int packet_size)
	{
		TORRENT_ASSERT(m_recv_pos != (std::numeric_limits<int>::max)());
		m_connection_buffer.cut(size, m_recv_pos + packet_size, m_recv_pos);
	}

	void reset(int packet_size);
	void crypto_reset(int packet_size);

	int advance_pos(int bytes);

	span<char const> get() const;

	span<char> mutable_buffer(int bytes);

private:
	// explicitly disallow assignment, to silence msvc warning
	crypto_receive_buffer& operator=(crypto_receive_buffer const&);

	int m_recv_pos = (std::numeric_limits<int>::max)();
	int m_packet_size = 0;
	receive_buffer& m_connection_buffer;
};
#endif // TORRENT_DISABLE_ENCRYPTION

} // namespace libtorrent

#endif // #ifndef TORRENT_RECEIVE_BUFFER_HPP_INCLUDED
