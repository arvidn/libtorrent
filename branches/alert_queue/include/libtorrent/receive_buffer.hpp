/*

Copyright (c) 2014, Arvid Norberg, Steven Siloti
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

#include <libtorrent/buffer.hpp>
#include <libtorrent/disk_buffer_holder.hpp>
#include <boost/asio/buffer.hpp>
#include <vector>

namespace libtorrent {

struct receive_buffer
{
	friend struct crypto_receive_buffer;

	receive_buffer(buffer_allocator_interface& allocator)
		: m_recv_start(0)
		, m_recv_end(0)
		, m_recv_pos(0)
		, m_packet_size(0)
		, m_soft_packet_size(0)
		, m_disk_recv_buffer_size(0)
		, m_disk_recv_buffer(allocator, 0)
	{}

	int packet_size() const { return m_packet_size; }
	int packet_bytes_remaining() const
	{
		TORRENT_ASSERT(m_recv_start == 0);
		return m_packet_size - m_recv_pos;
	}

	int max_receive();

	bool packet_finished() const { return m_packet_size <= m_recv_pos; }
	int pos() const { return m_recv_pos; }
	int capacity() const { return m_recv_buffer.capacity() + m_disk_recv_buffer_size; }

	int regular_buffer_size() const { return m_packet_size - m_disk_recv_buffer_size; }

	// regular buffer only
	boost::asio::mutable_buffer reserve(int size);
	// with possible disk buffer usage
	int reserve(boost::array<boost::asio::mutable_buffer, 2>& vec, int size);

	void received(int bytes_transferred)
	{
		TORRENT_ASSERT(m_packet_size > 0);
		m_recv_end += bytes_transferred;
		TORRENT_ASSERT(m_recv_pos <= int(m_recv_buffer.size()
			+ m_disk_recv_buffer_size));
	}

	int advance_pos(int bytes);
	bool pos_at_end() { return m_recv_pos == m_recv_end; }

	void clamp_size();

	void set_soft_packet_size(int size) { m_soft_packet_size = size; }

	// size = the packet size to remove from the receive buffer
	// packet_size = the next packet size to receive in the buffer
	// offset = the offset into the receive buffer where to remove `size` bytes
	void cut(int size, int packet_size, int offset = 0);

	buffer::const_interval get() const;

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
	// returns the entire regular buffer
	// should only be used during the handshake
	buffer::interval mutable_buffer();
	// returns the last 'bytes' from the receive buffer
	void mutable_buffers(std::vector<boost::asio::mutable_buffer>& vec, int bytes);
#endif

	void free_disk_buffer()
	{
		m_disk_recv_buffer.reset();
		m_disk_recv_buffer_size = 0;
	}

	bool has_disk_buffer() const { return m_disk_recv_buffer; }
	void assert_no_disk_buffer() const
	{
		TORRENT_ASSERT(!m_disk_recv_buffer);
		TORRENT_ASSERT(m_disk_recv_buffer_size == 0);
	}

	void assign_disk_buffer(char* buffer, int size);
	char* release_disk_buffer();

	// the purpose of this function is to free up and cut off all messages
	// in the receive buffer that have been parsed and processed.
	void normalize();
	bool normalized() const { return m_recv_start == 0; }

	void reset(int packet_size);

	bool can_recv_contiguous(int /*size*/) const { return true; }

#if TORRENT_USE_INVARIANT_CHECKS
	void check_invariant() const
	{
		TORRENT_ASSERT(m_recv_end >= m_recv_start);
		TORRENT_ASSERT(bool(m_disk_recv_buffer) == (m_disk_recv_buffer_size > 0));
	}
#endif

private:
	// recv_buf.begin (start of actual receive buffer)
	// |
	// |      m_recv_start (logical start of current
	// |      |  receive buffer, as perceived by upper layers)
	// |      |
	// |      |    m_recv_pos (number of bytes consumed
	// |      |    |  by upper layer, from logical receive buffer)
	// |      |    |
	// |      x---------x
	// |      |         |        recv_buf.end (end of actual receive buffer)
	// |      |         |        |
	// v      v         v        v
	// *------==========---------
	//                     ^
	//                     |
	//                     |
	// ------------------->x  m_recv_end (end of received data,
	//                          beyond this point is garbage)
	// m_recv_buffer

	// when not using contiguous receive buffers, there
	// may be a disk_recv_buffer in the mix as well. Whenever
	// m_disk_recv_buffer_size > 0 (and presumably also
	// m_disk_recv_buffer != NULL) the disk buffer is imagined
	// to be appended to the receive buffer right after m_recv_end.

	// the start of the logical receive buffer
	int m_recv_start;

	// the number of valid, received bytes in m_recv_buffer
	int m_recv_end;

	// the byte offset in m_recv_buffer that we have
	// are passing on to the upper layer. This is
	// always <= m_recv_end
	int m_recv_pos;

	// the size (in bytes) of the bittorrent message
	// we're currently receiving
	int m_packet_size;

	// the number of bytes that the other
	// end has to send us in order to respond
	// to all outstanding piece requests we
	// have sent to it
	int m_soft_packet_size;

	int m_disk_recv_buffer_size;

	buffer m_recv_buffer;

	// if this peer is receiving a piece, this
	// points to a disk buffer that the data is
	// read into. This eliminates a memcopy from
	// the receive buffer into the disk buffer
	disk_buffer_holder m_disk_recv_buffer;
};

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
// Wraps a receive_buffer to provide the ability to inject
// possibly authenticated crypto beneath the bittorrent protocol.
// When authenticated crypto is in use the wrapped receive_buffer
// holds the receive state of the crpyto layer while this class
// tracks the state of the bittorrent protocol.
struct crypto_receive_buffer
{
	crypto_receive_buffer(receive_buffer& next)
	: m_recv_pos(INT_MAX)
	, m_packet_size(0)
	, m_soft_packet_size(0)
	, m_connection_buffer(next)
	{}

	buffer::interval mutable_buffer() { return m_connection_buffer.mutable_buffer(); }
	char* release_disk_buffer() { return m_connection_buffer.release_disk_buffer(); }
	bool has_disk_buffer() const { return m_connection_buffer.has_disk_buffer(); }
	void assert_no_disk_buffer() const { m_connection_buffer.assert_no_disk_buffer(); }

	bool packet_finished() const;

	bool crypto_packet_finished() const
	{
		return m_recv_pos == INT_MAX || m_connection_buffer.packet_finished();
	}

	int packet_size() const;

	int crypto_packet_size() const
	{
		TORRENT_ASSERT(m_recv_pos != INT_MAX);
		return m_connection_buffer.packet_size() - m_recv_pos;
	}

	int pos() const;

	void cut(int size, int packet_size, int offset = 0);

	void crypto_cut(int size, int packet_size)
	{
		TORRENT_ASSERT(m_recv_pos != INT_MAX);
		m_connection_buffer.cut(size, m_recv_pos + packet_size, m_recv_pos);
	}

	void reset(int packet_size);
	void crypto_reset(int packet_size);

	void set_soft_packet_size(int size);

	int advance_pos(int bytes);

	buffer::const_interval get() const;

	bool can_recv_contiguous(int /*size*/) const
	{
		// TODO: Detect when the start of the next crpyto packet is aligned
		// with the start of piece data and the crpyto packet is at least
		// as large as the piece data. With a little extra work
		// we could receive directly into a disk buffer in that case.
		return m_recv_pos == INT_MAX;
	}

	void mutable_buffers(std::vector<boost::asio::mutable_buffer>& vec
		, std::size_t bytes_transfered);

private:
	int m_recv_pos;
	int m_packet_size;
	int m_soft_packet_size;
	receive_buffer& m_connection_buffer;
};
#endif // TORRENT_DISABLE_ENCRYPTION

} // namespace libtorrent

#endif // #ifndef TORRENT_RECEIVE_BUFFER_HPP_INCLUDED
