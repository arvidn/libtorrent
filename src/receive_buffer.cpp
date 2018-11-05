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

#include "libtorrent/receive_buffer.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/span.hpp"

namespace libtorrent {

int receive_buffer::max_receive() const
{
	return int(m_recv_buffer.size()) - m_recv_end;
}

span<char> receive_buffer::reserve(int const size)
{
	INVARIANT_CHECK;
	TORRENT_ASSERT(size > 0);
	TORRENT_ASSERT(m_recv_pos >= 0);

	// normalize() must be called before receiving more data
	TORRENT_ASSERT(m_recv_start == 0);

	if (int(m_recv_buffer.size()) < m_recv_end + size)
	{
		int const new_size = std::max(m_recv_end + size, m_packet_size);
		buffer new_buffer(new_size, {m_recv_buffer.data(), m_recv_end});
		m_recv_buffer = std::move(new_buffer);

		// since we just increased the size of the buffer, reset the watermark to
		// start at our new size (avoid flapping the buffer size)
		m_watermark = {};
	}

	return span<char>(m_recv_buffer).subspan(m_recv_end, size);
}

void receive_buffer::grow(int const limit)
{
	INVARIANT_CHECK;
	int const current_size = int(m_recv_buffer.size());
	TORRENT_ASSERT(current_size < std::numeric_limits<int>::max() / 3);

	// first grow to one piece message, then grow by 50% each time
	int const new_size = (current_size < m_packet_size)
		? m_packet_size : std::min(current_size * 3 / 2, limit);

	// re-allocate the buffer and copy over the part of it that's used
	buffer new_buffer(new_size, {m_recv_buffer.data(), m_recv_end});
	m_recv_buffer = std::move(new_buffer);

	// since we just increased the size of the buffer, reset the watermark to
	// start at our new size (avoid flapping the buffer size)
	m_watermark = {};
}

int receive_buffer::advance_pos(int const bytes)
{
	INVARIANT_CHECK;
	int const limit = m_packet_size > m_recv_pos ? m_packet_size - m_recv_pos : m_packet_size;
	int const sub_transferred = std::min(bytes, limit);
	m_recv_pos += sub_transferred;
	return sub_transferred;
}

// size = the packet size to remove from the receive buffer
// packet_size = the next packet size to receive in the buffer
// offset = the offset into the receive buffer where to remove `size` bytes
void receive_buffer::cut(int const size, int const packet_size, int const offset)
{
	INVARIANT_CHECK;
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
		{
			std::memmove(&m_recv_buffer[0] + m_recv_start + offset
				, &m_recv_buffer[0] + m_recv_start + offset + size
				, aux::numeric_cast<std::size_t>(m_recv_end - m_recv_start - size - offset));
		}

		m_recv_pos -= size;
		m_recv_end -= size;

#if TORRENT_USE_ASSERTS
		std::fill(m_recv_buffer.begin() + m_recv_end, m_recv_buffer.end(), std::uint8_t{0xcc});
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

span<char const> receive_buffer::get() const
{
	if (m_recv_buffer.empty())
	{
		TORRENT_ASSERT(m_recv_pos == 0);
		return {};
	}

	TORRENT_ASSERT(m_recv_start + m_recv_pos <= int(m_recv_buffer.size()));
	return span<char const>(m_recv_buffer).subspan(m_recv_start, m_recv_pos);
}

#if !defined TORRENT_DISABLE_ENCRYPTION
span<char> receive_buffer::mutable_buffer()
{
	INVARIANT_CHECK;
	return span<char>(m_recv_buffer).subspan(m_recv_start, m_recv_pos);
}

span<char> receive_buffer::mutable_buffer(int const bytes)
{
	INVARIANT_CHECK;
	// bytes is the number of bytes we just received, and m_recv_pos has
	// already been adjusted for these bytes. The receive pos immediately
	// before we received these bytes was (m_recv_pos - bytes)
	return span<char>(m_recv_buffer).subspan(m_recv_start + m_recv_pos - bytes, bytes);
}
#endif

// the purpose of this function is to free up and cut off all messages
// in the receive buffer that have been parsed and processed.
// it may also shrink the size of the buffer allocation if we haven't been using
// enough of it lately.
void receive_buffer::normalize(int const force_shrink)
{
	INVARIANT_CHECK;
	TORRENT_ASSERT(m_recv_end >= m_recv_start);

	m_watermark.add_sample(std::max(m_recv_end, m_packet_size));

	// if the running average drops below half of the current buffer size,
	// reallocate a smaller one.
	bool const shrink_buffer = std::int64_t(m_recv_buffer.size()) / 2 > m_watermark.mean()
		&& m_watermark.mean() > (m_recv_end - m_recv_start);

	span<char const> bytes_to_shift(m_recv_buffer.data() + m_recv_start
		, m_recv_end - m_recv_start);

	if (force_shrink)
	{
		int const target_size = std::max(std::max(force_shrink
			, int(bytes_to_shift.size())), m_packet_size);
		buffer new_buffer(target_size, bytes_to_shift);
		m_recv_buffer = std::move(new_buffer);
	}
	else if (shrink_buffer)
	{
		buffer new_buffer(m_watermark.mean(), bytes_to_shift);
		m_recv_buffer = std::move(new_buffer);
	}
	else if (m_recv_end > m_recv_start
		&& m_recv_start > 0)
	{
		std::memmove(m_recv_buffer.data(), bytes_to_shift.data()
			, std::size_t(bytes_to_shift.size()));
	}

	m_recv_end -= m_recv_start;
	m_recv_start = 0;

#if TORRENT_USE_ASSERTS
	std::fill(m_recv_buffer.begin() + m_recv_end, m_recv_buffer.end(), std::uint8_t{0xcc});
#endif
}

void receive_buffer::reset(int const packet_size)
{
	INVARIANT_CHECK;
	TORRENT_ASSERT(int(m_recv_buffer.size()) >= m_recv_end);
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

#if !defined TORRENT_DISABLE_ENCRYPTION
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

int crypto_receive_buffer::advance_pos(int bytes)
{
	if (m_recv_pos == INT_MAX) return bytes;

	int const limit = m_packet_size > m_recv_pos ? m_packet_size - m_recv_pos : m_packet_size;
	int const sub_transferred = std::min(bytes, limit);
	m_recv_pos += sub_transferred;
	m_connection_buffer.cut(0, m_connection_buffer.packet_size() + sub_transferred);
	return sub_transferred;
}

span<char const> crypto_receive_buffer::get() const
{
	span<char const> recv_buffer = m_connection_buffer.get();
	if (m_recv_pos < m_connection_buffer.pos())
		recv_buffer = recv_buffer.first(m_recv_pos);
	return recv_buffer;
}

span<char> crypto_receive_buffer::mutable_buffer(
	int const bytes)
{
	int const pending_decryption = (m_recv_pos != INT_MAX)
		? m_connection_buffer.packet_size() - m_recv_pos
		: bytes;
	return m_connection_buffer.mutable_buffer(pending_decryption);
}
#endif // TORRENT_DISABLE_ENCRYPTION

} // namespace libtorrent
