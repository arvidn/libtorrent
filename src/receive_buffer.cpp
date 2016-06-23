/*

Copyright (c) 2014-2016, Arvid Norberg, Steven Siloti
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

#include <libtorrent/receive_buffer.hpp>

namespace libtorrent {

	namespace {
		int round_up8(int v)
		{
			return ((v & 7) == 0) ? v : v + (8 - (v & 7));
		}
	}

int receive_buffer::max_receive()
{
	int max = packet_bytes_remaining();
	if (m_recv_pos >= m_soft_packet_size) m_soft_packet_size = 0;
	if (m_soft_packet_size && max > m_soft_packet_size - m_recv_pos)
		max = m_soft_packet_size - m_recv_pos;
	return max;
}

boost::asio::mutable_buffer receive_buffer::reserve(int size)
{
	TORRENT_ASSERT(size > 0);
	TORRENT_ASSERT(m_recv_pos >= 0);
	// this is unintuitive, but we used to use m_recv_pos in this function when
	// we should have used m_recv_end. perhaps they always happen to be equal
	TORRENT_ASSERT(m_recv_pos == m_recv_end);

	// normalize() must be called before receiving more data
	TORRENT_ASSERT(m_recv_start == 0);

	m_recv_buffer.resize(m_recv_end + size);
	return boost::asio::buffer(&m_recv_buffer[0] + m_recv_end, size);
}

int receive_buffer::advance_pos(int bytes)
{
	int const packet_size = m_soft_packet_size ? m_soft_packet_size : m_packet_size;
	int const limit = packet_size > m_recv_pos ? packet_size - m_recv_pos : packet_size;
	int const sub_transferred = (std::min)(bytes, limit);
	m_recv_pos += sub_transferred;
	if (m_recv_pos >= m_soft_packet_size) m_soft_packet_size = 0;
	return sub_transferred;
}

void receive_buffer::clamp_size()
{
	if (m_recv_pos == 0
		&& (m_recv_buffer.capacity() - m_packet_size) > 128)
	{
		// round up to an even 8 bytes since that's the RC4 blocksize
		buffer(round_up8(m_packet_size)).swap(m_recv_buffer);
	}
}

// size = the packet size to remove from the receive buffer
// packet_size = the next packet size to receive in the buffer
// offset = the offset into the receive buffer where to remove `size` bytes
void receive_buffer::cut(int size, int packet_size, int offset)
{
	TORRENT_ASSERT(packet_size > 0);
	TORRENT_ASSERT(int(m_recv_buffer.size()) >= size);
	TORRENT_ASSERT(int(m_recv_buffer.size()) >= m_recv_pos);
	TORRENT_ASSERT(m_recv_pos >= size + offset);
	TORRENT_ASSERT(offset >= 0);
	TORRENT_ASSERT(int(m_recv_buffer.size()) >= m_recv_end);
	TORRENT_ASSERT(m_recv_start <= m_recv_end);
	TORRENT_ASSERT(size >= 0);

	if (offset > 0)
	{
		TORRENT_ASSERT(m_recv_start - size <= m_recv_end);

		if (size > 0)
			std::memmove(&m_recv_buffer[0] + m_recv_start + offset
				, &m_recv_buffer[0] + m_recv_start + offset + size
				, m_recv_end - m_recv_start - size - offset);

		m_recv_pos -= size;
		m_recv_end -= size;

#ifdef TORRENT_DEBUG
		std::fill(m_recv_buffer.begin() + m_recv_end, m_recv_buffer.end(), 0xcc);
#endif
	}
	else
	{
		TORRENT_ASSERT(m_recv_start + size <= m_recv_end);
		m_recv_start += size;
		m_recv_pos -= size;
	}

	m_packet_size = packet_size;
}

buffer::const_interval receive_buffer::get() const
{
	if (m_recv_buffer.empty())
	{
		TORRENT_ASSERT(m_recv_pos == 0);
		return buffer::const_interval(0,0);
	}

	int rcv_pos = (std::min)(m_recv_pos, int(m_recv_buffer.size()) - m_recv_start);
	return buffer::const_interval(&m_recv_buffer[0] + m_recv_start
		, &m_recv_buffer[0] + m_recv_start + rcv_pos);
}

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
boost::asio::mutable_buffer receive_buffer::mutable_buffer()
{
	namespace asio = boost::asio;

	if (m_recv_buffer.empty())
	{
		TORRENT_ASSERT(m_recv_pos == 0);
		return asio::mutable_buffer();
	}
	int const rcv_pos = (std::min)(m_recv_pos, int(m_recv_buffer.size()));
	return asio::mutable_buffer(&m_recv_buffer[0] + m_recv_start, rcv_pos);
}

boost::asio::mutable_buffer receive_buffer::mutable_buffer(int const bytes)
{
	namespace asio = boost::asio;

	// bytes is the number of bytes we just received, and m_recv_pos has
	// already been adjusted for these bytes. The receive pos immediately
	// before we received these bytes was (m_recv_pos - bytes)
	int const last_recv_pos = m_recv_pos - bytes;
	TORRENT_ASSERT(bytes <= m_recv_pos);

	return asio::mutable_buffer(&m_recv_buffer[0] + m_recv_start
			+ last_recv_pos, bytes);
}
#endif

// the purpose of this function is to free up and cut off all messages
// in the receive buffer that have been parsed and processed.
void receive_buffer::normalize()
{
	TORRENT_ASSERT(m_recv_end >= m_recv_start);
	if (m_recv_start == 0) return;

	if (m_recv_end > m_recv_start)
		std::memmove(&m_recv_buffer[0], &m_recv_buffer[0] + m_recv_start, m_recv_end - m_recv_start);

	m_recv_end -= m_recv_start;
	m_recv_start = 0;

#ifdef TORRENT_DEBUG
	std::fill(m_recv_buffer.begin() + m_recv_end, m_recv_buffer.end(), 0xcc);
#endif
}

void receive_buffer::reset(int packet_size)
{
	TORRENT_ASSERT(m_recv_buffer.size() >= m_recv_end);
	TORRENT_ASSERT(packet_size > 0);
	if (m_recv_end > m_packet_size)
	{
		cut(m_packet_size, packet_size);
		return;
	}

	m_recv_pos = 0;
	m_recv_start = 0;
	m_recv_end = 0;
	m_packet_size = packet_size;
}

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
bool crypto_receive_buffer::packet_finished() const
{
	if (m_recv_pos == INT_MAX)
		return m_connection_buffer.packet_finished();
	else
		return m_packet_size <= m_recv_pos;
}

int crypto_receive_buffer::packet_size() const
{
	if (m_recv_pos == INT_MAX)
		return m_connection_buffer.packet_size();
	else
		return m_packet_size;
}

int crypto_receive_buffer::pos() const
{
	if (m_recv_pos == INT_MAX)
		return m_connection_buffer.pos();
	else
		return m_recv_pos;
}

void crypto_receive_buffer::cut(int size, int packet_size, int offset)
{
	if (m_recv_pos != INT_MAX)
	{
		TORRENT_ASSERT(size <= m_recv_pos);
		m_packet_size = packet_size;
		packet_size = m_connection_buffer.packet_size() - size;
		m_recv_pos -= size;
	}
	m_connection_buffer.cut(size, packet_size, offset);
}

void crypto_receive_buffer::reset(int packet_size)
{
	if (m_recv_pos != INT_MAX)
	{
		if (m_connection_buffer.m_recv_end > m_packet_size)
		{
			cut(m_packet_size, packet_size);
			return;
		}
		m_packet_size = packet_size;
		packet_size = m_connection_buffer.packet_size() - m_recv_pos;
		m_recv_pos = 0;
	}
	m_connection_buffer.reset(packet_size);
}

void crypto_receive_buffer::crypto_reset(int packet_size)
{
	TORRENT_ASSERT(packet_finished());
	TORRENT_ASSERT(crypto_packet_finished());
	TORRENT_ASSERT(m_recv_pos == INT_MAX || m_recv_pos == m_connection_buffer.pos());
	TORRENT_ASSERT(m_recv_pos == INT_MAX || m_connection_buffer.pos_at_end());

	if (packet_size == 0)
	{
		if (m_recv_pos != INT_MAX)
			m_connection_buffer.cut(0, m_packet_size);
		m_recv_pos = INT_MAX;
	}
	else
	{
		if (m_recv_pos == INT_MAX)
			m_packet_size = m_connection_buffer.packet_size();
		m_recv_pos = m_connection_buffer.pos();
		TORRENT_ASSERT(m_recv_pos >= 0);
		m_connection_buffer.cut(0, m_recv_pos + packet_size);
	}
}

void crypto_receive_buffer::set_soft_packet_size(int size)
{
	if (m_recv_pos == INT_MAX)
		m_connection_buffer.set_soft_packet_size(size);
	else
		m_soft_packet_size = size;
}

int crypto_receive_buffer::advance_pos(int bytes)
{
	if (m_recv_pos == INT_MAX) return bytes;

	int packet_size = m_soft_packet_size ? m_soft_packet_size : m_packet_size;
	int limit = packet_size > m_recv_pos ? packet_size - m_recv_pos : packet_size;
	int sub_transferred = (std::min)(bytes, limit);
	m_recv_pos += sub_transferred;
	m_connection_buffer.cut(0, m_connection_buffer.packet_size() + sub_transferred);
	if (m_recv_pos >= m_soft_packet_size) m_soft_packet_size = 0;
	return sub_transferred;
}

buffer::const_interval crypto_receive_buffer::get() const
{
	buffer::const_interval recv_buffer = m_connection_buffer.get();
	if (m_recv_pos < m_connection_buffer.pos())
		recv_buffer.end = recv_buffer.begin + m_recv_pos;
	return recv_buffer;
}

boost::asio::mutable_buffer crypto_receive_buffer::mutable_buffer(
	std::size_t const bytes)
{
	int const pending_decryption = (m_recv_pos != INT_MAX)
		? m_connection_buffer.packet_size() - m_recv_pos
		: int(bytes);
	return m_connection_buffer.mutable_buffer(pending_decryption);
}
#endif // TORRENT_DISABLE_ENCRYPTION

} // namespace libtorrent
