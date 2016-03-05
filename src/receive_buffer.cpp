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
	TORRENT_ASSERT(!m_disk_recv_buffer);
	// this is unintuitive, but we used to use m_recv_pos in this function when
	// we should have used m_recv_end. perhaps they always happen to be equal
	TORRENT_ASSERT(m_recv_pos == m_recv_end);

	m_recv_buffer.resize(m_recv_end + size);
	return boost::asio::buffer(&m_recv_buffer[0] + m_recv_end, size);
}

int receive_buffer::reserve(boost::array<boost::asio::mutable_buffer, 2>& vec, int size)
{
	TORRENT_ASSERT(size > 0);
	TORRENT_ASSERT(m_recv_pos >= 0);
	TORRENT_ASSERT(m_packet_size > 0);

	// normalize() must be called before receiving more data
	TORRENT_ASSERT(m_recv_start == 0);

	// this is unintuitive, but we used to use m_recv_pos in this function when
	// we should have used m_recv_end. perhaps they always happen to be equal
	TORRENT_ASSERT(m_recv_pos == m_recv_end);

	int num_bufs;
	int const regular_buf_size = regular_buffer_size();

	if (int(m_recv_buffer.size()) < regular_buf_size)
		m_recv_buffer.resize(round_up8(regular_buf_size));

	if (!m_disk_recv_buffer || regular_buf_size >= m_recv_end + size)
	{
		// only receive into regular buffer
		TORRENT_ASSERT(m_recv_end + size <= int(m_recv_buffer.size()));
		vec[0] = boost::asio::buffer(&m_recv_buffer[0] + m_recv_end, size);
		TORRENT_ASSERT(boost::asio::buffer_size(vec[0]) > 0);
		num_bufs = 1;
	}
	else if (m_recv_end >= regular_buf_size)
	{
		// only receive into disk buffer
		TORRENT_ASSERT(m_recv_end - regular_buf_size >= 0);
		TORRENT_ASSERT(m_recv_end - regular_buf_size + size <= m_disk_recv_buffer_size);
		vec[0] = boost::asio::buffer(m_disk_recv_buffer.get() + m_recv_end - regular_buf_size, size);
		TORRENT_ASSERT(boost::asio::buffer_size(vec[0]) > 0);
		num_bufs = 1;
	}
	else
	{
		// receive into both regular and disk buffer
		TORRENT_ASSERT(size + m_recv_end > regular_buf_size);
		TORRENT_ASSERT(m_recv_end < regular_buf_size);
		TORRENT_ASSERT(size - regular_buf_size
			+ m_recv_end <= m_disk_recv_buffer_size);

		vec[0] = boost::asio::buffer(&m_recv_buffer[0] + m_recv_end
			, regular_buf_size - m_recv_end);
		vec[1] = boost::asio::buffer(m_disk_recv_buffer.get()
			, size - regular_buf_size + m_recv_end);
		TORRENT_ASSERT(boost::asio::buffer_size(vec[0])
			+ boost::asio::buffer_size(vec[1])> 0);
		num_bufs = 2;
	}

	return num_bufs;
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
		return buffer::interval(0,0);
	}

	int rcv_pos = (std::min)(m_recv_pos, int(m_recv_buffer.size()) - m_recv_start);
	return buffer::const_interval(&m_recv_buffer[0] + m_recv_start
		, &m_recv_buffer[0] + m_recv_start + rcv_pos);
}

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
buffer::interval receive_buffer::mutable_buffer()
{
	if (m_recv_buffer.empty())
	{
		TORRENT_ASSERT(m_recv_pos == 0);
		return buffer::interval(0,0);
	}
	TORRENT_ASSERT(!m_disk_recv_buffer);
	TORRENT_ASSERT(m_disk_recv_buffer_size == 0);
	int rcv_pos = (std::min)(m_recv_pos, int(m_recv_buffer.size()));
	return buffer::interval(&m_recv_buffer[0] + m_recv_start
		, &m_recv_buffer[0] + m_recv_start + rcv_pos);
}

// TODO: 2 should this take a boost::array<..., 2> instead? it could return the
// number of buffers added, just like reserve.
void receive_buffer::mutable_buffers(std::vector<boost::asio::mutable_buffer>& vec, int const bytes)
{
	namespace asio = boost::asio;

	// bytes is the number of bytes we just received, and m_recv_pos has
	// already been adjusted for these bytes. The receive pos immediately
	// before we received these bytes was (m_recv_pos - bytes)

	int const last_recv_pos = m_recv_pos - bytes;
	TORRENT_ASSERT(bytes <= m_recv_pos);

	// the number of bytes in the current packet that are being received into a
	// regular receive buffer (as opposed to a disk cache buffer)
	int const regular_buf_size = regular_buffer_size();

	TORRENT_ASSERT(regular_buf_size >= 0);
	if (!m_disk_recv_buffer || regular_buf_size >= m_recv_pos)
	{
		// we just received into a regular disk buffer
		vec.push_back(asio::mutable_buffer(&m_recv_buffer[0] + m_recv_start
			+ last_recv_pos, bytes));
	}
	else if (last_recv_pos >= regular_buf_size)
	{
		// we only received into a disk buffer
		vec.push_back(asio::mutable_buffer(m_disk_recv_buffer.get()
			+ last_recv_pos - regular_buf_size, bytes));
	}
	else
	{
		// we received into a regular and a disk buffer
		TORRENT_ASSERT(last_recv_pos < regular_buf_size);
		TORRENT_ASSERT(m_recv_pos > regular_buf_size);
		vec.push_back(asio::mutable_buffer(&m_recv_buffer[0] + m_recv_start + last_recv_pos
			, regular_buf_size - last_recv_pos));
		vec.push_back(asio::mutable_buffer(m_disk_recv_buffer.get()
			, m_recv_pos - regular_buf_size));
	}

#if TORRENT_USE_ASSERTS
	int vec_bytes = 0;
	for (std::vector<asio::mutable_buffer>::iterator i = vec.begin();
		i != vec.end(); ++i)
		vec_bytes += boost::asio::buffer_size(*i);
	TORRENT_ASSERT(vec_bytes == bytes);
#endif
}
#endif

void receive_buffer::assign_disk_buffer(char* buffer, int size)
{
	TORRENT_ASSERT(m_packet_size > 0);
	assert_no_disk_buffer();
	m_disk_recv_buffer.reset(buffer);
	if (m_disk_recv_buffer)
		m_disk_recv_buffer_size = size;
}

char* receive_buffer::release_disk_buffer()
{
	if (!m_disk_recv_buffer) return 0;

	TORRENT_ASSERT(m_disk_recv_buffer_size <= m_recv_end);
	TORRENT_ASSERT(m_recv_start <= m_recv_end - m_disk_recv_buffer_size);
	m_recv_end -= m_disk_recv_buffer_size;
	m_disk_recv_buffer_size = 0;
	return m_disk_recv_buffer.release();
}

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
	TORRENT_ASSERT(!m_connection_buffer.has_disk_buffer());

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

void crypto_receive_buffer::mutable_buffers(
	std::vector<boost::asio::mutable_buffer>& vec
	, std::size_t bytes_transfered)
{
	int pending_decryption = bytes_transfered;
	if (m_recv_pos != INT_MAX)
	{
		pending_decryption = m_connection_buffer.packet_size() - m_recv_pos;
	}
	m_connection_buffer.mutable_buffers(vec, pending_decryption);
}
#endif // TORRENT_DISABLE_ENCRYPTION

} // namespace libtorrent
