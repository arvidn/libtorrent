/*

Copyright (c) 2003, Arvid Norberg
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

#include <iostream>
#include <iomanip>
#include <vector>

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/session.hpp"

#if defined(_MSC_VER)
#define for if (false) {} else for
#endif

#define VERBOSE

using namespace libtorrent;

namespace
{
	// reads an integer from a byte stream
	// in big endian byte order and converts
	// it to native endianess
	unsigned int read_int(const char* buf)
	{
		unsigned int val = 0;
		val |= static_cast<unsigned char>(buf[0]) << 24;
		val |= static_cast<unsigned char>(buf[1]) << 16;
		val |= static_cast<unsigned char>(buf[2]) << 8;
		val |= static_cast<unsigned char>(buf[3]);
		return val;
	}

	void write_int(unsigned int val, char* buf)
	{
		buf[0] = static_cast<unsigned char>(val >> 24);
		buf[1] = static_cast<unsigned char>(val >> 16);
		buf[2] = static_cast<unsigned char>(val >> 8);
		buf[3] = static_cast<unsigned char>(val);
	}


}

libtorrent::peer_connection::peer_connection(
	detail::session_impl& ses
	, selector& sel
	, torrent* t
	, boost::shared_ptr<libtorrent::socket> s
	, const peer_id& p)
	: m_state(read_protocol_length)
	, m_timeout(120)
	, m_packet_size(1)
	, m_recv_pos(0)
	, m_last_receive(boost::gregorian::date(std::time(0)))
	, m_last_sent(boost::gregorian::date(std::time(0)))
	, m_selector(sel)
	, m_socket(s)
	, m_torrent(t)
	, m_attached_to_torrent(true)
	, m_ses(ses)
	, m_active(true)
	, m_added_to_selector(false)
	, m_peer_id(p)
	, m_peer_interested(false)
	, m_peer_choked(true)
	, m_interesting(false)
	, m_choked(true)
	, m_free_upload(0)
	, m_send_quota(100)
	, m_send_quota_left(100)
	, m_send_quota_limit(100)
	, m_trust_points(0)
{
	assert(!m_socket->is_blocking());
	assert(m_torrent != 0);

#ifndef NDEBUG
	m_logger = m_ses.create_log(s->sender().as_string().c_str());
#endif

	send_handshake();

	// start in the state where we are trying to read the
	// handshake from the other side
	m_recv_buffer.resize(1);

	// assume the other end has no pieces
	m_have_piece.resize(m_torrent->torrent_file().num_pieces());
	std::fill(m_have_piece.begin(), m_have_piece.end(), false);

	send_bitfield();
}

libtorrent::peer_connection::peer_connection(
	detail::session_impl& ses
	, selector& sel
	, boost::shared_ptr<libtorrent::socket> s)
	: m_state(read_protocol_length)
	, m_timeout(120)
	, m_packet_size(1)
	, m_recv_pos(0)
	, m_last_receive(boost::gregorian::date(std::time(0)))
	, m_last_sent(boost::gregorian::date(std::time(0)))
	, m_selector(sel)
	, m_socket(s)
	, m_torrent(0)
	, m_attached_to_torrent(0)
	, m_ses(ses)
	, m_active(false)
	, m_added_to_selector(false)
	, m_peer_id()
	, m_peer_interested(false)
	, m_peer_choked(true)
	, m_interesting(false)
	, m_choked(true)
	, m_free_upload(0)
	, m_send_quota(100)
	, m_send_quota_left(100)
	, m_send_quota_limit(100)
	, m_trust_points(0)
{
	assert(!m_socket->is_blocking());

#ifndef NDEBUG
	m_logger = m_ses.create_log(s->sender().as_string().c_str());
#endif

	// we are not attached to any torrent yet.
	// we have to wait for the handshake to see
	// which torrent the connector want's to connect to

	// start in the state where we are trying to read the
	// handshake from the other side
	m_recv_buffer.resize(1);
}

libtorrent::peer_connection::~peer_connection()
{
	m_selector.remove(m_socket);
	if (m_attached_to_torrent)
	{
		assert(m_torrent != 0);
		m_torrent->remove_peer(this);
	}
}

void libtorrent::peer_connection::set_send_quota(int num_bytes)
{
	assert(num_bytes <= m_send_quota_limit || m_send_quota_limit == -1);
	if (num_bytes > m_send_quota_limit && m_send_quota_limit!=-1) num_bytes = m_send_quota_limit;

	m_send_quota = num_bytes;
	m_send_quota_left = num_bytes;
	send_buffer_updated();
}

void libtorrent::peer_connection::send_handshake()
{
	assert(m_send_buffer.size() == 0);

	// add handshake to the send buffer
	const char version_string[] = "BitTorrent protocol";
	const int string_len = sizeof(version_string)-1;
	int pos = 1;

	m_send_buffer.resize(1 + string_len + 8 + 20 + 20);

	// length of version string
	m_send_buffer[0] = string_len;

	// version string itself
	std::copy(
		version_string
		, version_string+string_len
		, m_send_buffer.begin()+pos);
	pos += string_len;

	// 8 zeroes
	std::fill(
		m_send_buffer.begin() + pos
		, m_send_buffer.begin() + pos + 8
		, 0);
	pos += 8;

	// info hash
	std::copy(
		m_torrent->torrent_file().info_hash().begin()
		, m_torrent->torrent_file().info_hash().end()
		, m_send_buffer.begin() + pos);
	pos += 20;

	// peer id
	std::copy(
		m_ses.get_peer_id().begin()
		, m_ses.get_peer_id().end()
		, m_send_buffer.begin() + pos);

#ifndef NDEBUG
	(*m_logger) << m_socket->sender().as_string() << " ==> HANDSHAKE\n";
#endif

	send_buffer_updated();
}

boost::optional<piece_block_progress> libtorrent::peer_connection::downloading_piece() const
{
	// are we currently receiving a 'piece' message?
	if (m_state != read_packet
		|| m_recv_pos < 9
		|| m_recv_buffer[0] != msg_piece)
		return boost::optional<piece_block_progress>();

	int piece_index = read_int(&m_recv_buffer[1]);
	int offset = read_int(&m_recv_buffer[5]);
	int len = m_packet_size - 9;

	// is any of the piece message header data invalid?
	// TODO: make sure that len is == block_size or less only
	// if its's the last block.
	if (piece_index < 0
		|| piece_index >= m_torrent->torrent_file().num_pieces()
		|| offset < 0
		|| offset + len > m_torrent->torrent_file().piece_size(piece_index)
		|| offset % m_torrent->block_size() != 0)
		return boost::optional<piece_block_progress>();

	piece_block_progress p;

	p.piece_index = piece_index;
	p.block_index = offset / m_torrent->block_size();
	p.bytes_downloaded = m_recv_pos - 9;
	p.full_block_bytes = len;

	return boost::optional<piece_block_progress>(p);
}

bool libtorrent::peer_connection::dispatch_message(int received)
{
	assert(m_recv_pos >= received);
	assert(m_recv_pos > 0);

	int packet_type = m_recv_buffer[0];
	if (packet_type > msg_cancel || packet_type < msg_choke)
		throw protocol_error("unknown message id");

	switch (packet_type)
	{

		// *************** CHOKE ***************
	case msg_choke:
		if (m_packet_size != 1)
			throw protocol_error("'choke' message size != 1");
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return false;

#ifndef NDEBUG
		(*m_logger) << m_socket->sender().as_string() << " <== CHOKE\n";
#endif
		m_peer_choked = true;
		m_torrent->get_policy().choked(*this);

		// remove all pieces from this peers download queue and
		// remove the 'downloading' flag from piece_picker.
		for (std::deque<piece_block>::iterator i = m_download_queue.begin();
			i != m_download_queue.end();
			++i)
		{
			m_torrent->picker().abort_download(*i);
		}
		m_download_queue.clear();
#ifndef NDEBUG
//		m_torrent->picker().integrity_check(m_torrent);
#endif
		break;



		// *************** UNCHOKE ***************
	case msg_unchoke:
		if (m_packet_size != 1)
			throw protocol_error("'unchoke' message size != 1");
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return false;

#ifndef NDEBUG
		(*m_logger) << m_socket->sender().as_string() << " <== UNCHOKE\n";
#endif
		m_peer_choked = false;
		m_torrent->get_policy().unchoked(*this);
		break;


		// *************** INTERESTED ***************
	case msg_interested:
		if (m_packet_size != 1)
			throw protocol_error("'interested' message size != 1");
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return false;

#ifndef NDEBUG
		(*m_logger) << m_socket->sender().as_string() << " <== INTERESTED\n";
#endif
		m_peer_interested = true;
		m_torrent->get_policy().interested(*this);
		break;


		// *************** NOT INTERESTED ***************
	case msg_not_interested:
		if (m_packet_size != 1)
			throw protocol_error("'not interested' message size != 1");
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return false;

#ifndef NDEBUG
		(*m_logger) << m_socket->sender().as_string() << " <== NOT_INTERESTED\n";
#endif
		m_peer_interested = false;
		m_torrent->get_policy().not_interested(*this);
		break;



		// *************** HAVE ***************
	case msg_have:
		{
			if (m_packet_size != 5)
				throw protocol_error("'have' message size != 5");
			m_statistics.received_bytes(0, received);
			if (m_recv_pos < m_packet_size) return false;

			std::size_t index = read_int(&m_recv_buffer[1]);
			// if we got an invalid message, abort
			if (index >= m_have_piece.size())
				throw protocol_error("have message with higher index than the number of pieces");

#ifndef NDEBUG
			(*m_logger) << m_socket->sender().as_string() << " <== HAVE [ piece: " << index << "]\n";
#endif

			if (m_have_piece[index])
			{
#ifndef NDEBUG
				(*m_logger) << m_socket->sender().as_string() << " oops.. we already knew that: " << index << "\n";
#endif
			}
			else
			{
				m_have_piece[index] = true;
				if (!m_torrent->peer_has(index) && !is_interesting())
					m_torrent->get_policy().peer_is_interesting(*this);
			}
			break;
		}




		// *************** BITFIELD ***************
	case msg_bitfield:
		{
			if (m_packet_size - 1 != (m_have_piece.size() + 7) / 8)
				throw protocol_error("bitfield with invalid size");
			m_statistics.received_bytes(0, received);
			if (m_recv_pos < m_packet_size) return false;

#ifndef NDEBUG
			(*m_logger) << m_socket->sender().as_string() << " <== BITFIELD\n";
#endif
			bool interesting = false;
			bool is_seed = true;
			for (std::size_t i = 0; i < m_have_piece.size(); ++i)
			{
				bool have = m_recv_buffer[1 + (i>>3)] & (1 << (7 - (i&7)));
				if (have && !m_have_piece[i])
				{
					m_have_piece[i] = true;
					if (m_torrent->peer_has(i)) interesting = true;
				}
				else if (!have && m_have_piece[i])
				{
					m_have_piece[i] = false;
					m_torrent->peer_lost(i);
				}
				if (!have) is_seed = false;
			}
			if (is_seed)
			{
#ifndef NDEBUG
				(*m_logger) << m_socket->sender().as_string() << " *** THIS IS A SEED ***\n";
#endif
			}

			if (interesting) m_torrent->get_policy().peer_is_interesting(*this);

			break;
		}


		// *************** REQUEST ***************
	case msg_request:
		{
			if (m_packet_size != 13)
				throw protocol_error("'request' message size != 13");
			m_statistics.received_bytes(0, received);
			if (m_recv_pos < m_packet_size) return false;

			peer_request r;
			r.piece = read_int(&m_recv_buffer[1]);
			r.start = read_int(&m_recv_buffer[5]);
			r.length = read_int(&m_recv_buffer[9]);

			// make sure this request
			// is legal and taht the peer
			// is not choked
			if (r.piece >= 0
				&& r.piece < m_torrent->torrent_file().num_pieces()
				&& r.start >= 0
				&& r.start < m_torrent->torrent_file().piece_size(r.piece)
				&& r.length > 0
				&& r.length + r.start < m_torrent->torrent_file().piece_size(r.piece)
				&& !m_choked)
			{
				m_requests.push_back(r);
				send_buffer_updated();
#ifndef NDEBUG
				(*m_logger) << m_socket->sender().as_string() << " <== REQUEST [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif
			}
			else
			{
				// TODO: log this illegal request
				// if the only error is that the
				// peer is choked, it may not be a
				// mistake
			}

			break;
		}



		// *************** PIECE ***************
	case msg_piece:
		{
			if (m_recv_pos <= 9)
				// only received protocol data
				m_statistics.received_bytes(0, received);
			else if (m_recv_pos - received >= 9)
				// only received payload data
				m_statistics.received_bytes(received, 0);
			else
			{
				// received a bit of both
				assert(m_recv_pos - received < 9);
				assert(m_recv_pos > 9);
				assert(9 - (m_recv_pos - received) <= 9);
				m_statistics.received_bytes(
					m_recv_pos - 9
					, 9 - (m_recv_pos - received));
			}

			if (m_recv_pos < m_packet_size) return false;

			std::size_t index = read_int(&m_recv_buffer[1]);
			if (index < 0 || index >= m_torrent->torrent_file().num_pieces())
			{
#ifndef NDEBUG
				(*m_logger) << m_socket->sender().as_string() << " piece index invalid\n";
#endif
				throw protocol_error("invalid piece index in piece message");
			}
			int offset = read_int(&m_recv_buffer[5]);
			int len = m_packet_size - 9;

			if (offset < 0)
			{
#ifndef NDEBUG
				(*m_logger) << m_socket->sender().as_string() << " offset < 0\n";
#endif
				throw protocol_error("offset < 0 in piece message");
			}

			if (offset + len > m_torrent->torrent_file().piece_size(index))
			{
#ifndef NDEBUG
				(*m_logger) << m_socket->sender().as_string() << " piece packet contains more data than the piece size\n";
#endif
				throw protocol_error("piece message contains more data than the piece size");
			}
			// TODO: make sure that len is == block_size or less only
			// if its's the last block.

			if (offset % m_torrent->block_size() != 0)
			{
#ifndef NDEBUG
				(*m_logger) << m_socket->sender().as_string() << " piece packet contains unaligned offset\n";
#endif
				throw protocol_error("piece message contains unaligned offset");
			}
/*
			piece_block req = m_download_queue.front();
			if (req.piece_index != index)
			{
#ifndef NDEBUG
				(*m_logger) << m_socket->sender().as_string() << " piece packet contains unrequested index\n";
#endif
				return false;
			}

			if (req.block_index != offset / m_torrent->block_size())
			{
#ifndef NDEBUG
				(*m_logger) << m_socket->sender().as_string() << " piece packet contains unrequested offset\n";
#endif
				return false;
			}
*/
#ifndef NDEBUG
			(*m_logger) << m_socket->sender().as_string() << " <== PIECE [ piece: " << index << " | s: " << offset << " | l: " << len << " ]\n";
#endif

			piece_picker& picker = m_torrent->picker();
			piece_block block_finished(index, offset / m_torrent->block_size());

			std::deque<piece_block>::iterator b
				= std::find(
					m_download_queue.begin()
					, m_download_queue.end()
					, block_finished);

			if (b != m_download_queue.end())
			{
				// pop the request that just finished
				// from the download queue
				m_download_queue.erase(b);
			}
			else
			{
				// TODO: cancel the block from the
				// peer that has taken over it.
			}

			if (picker.is_finished(block_finished)) break;

			m_torrent->filesystem().write(&m_recv_buffer[9], index, offset, len);

			picker.mark_as_finished(block_finished, m_peer_id);

			m_torrent->get_policy().block_finished(*this, block_finished);

			// did we just finish the piece?
			if (picker.is_piece_finished(index))
			{
				bool verified = m_torrent->verify_piece(index);
				if (verified)
				{
					m_torrent->announce_piece(index);
				}
				else
				{
					m_torrent->piece_failed(index);
				}
				m_torrent->get_policy().piece_finished(index, verified);
			}
			break;
		}


		// *************** CANCEL ***************
	case msg_cancel:
		{
			if (m_packet_size != 13)
				throw protocol_error("'cancel' message size != 13");
			m_statistics.received_bytes(0, received);
			if (m_recv_pos < m_packet_size) return false;

			peer_request r;
			r.piece = read_int(&m_recv_buffer[1]);
			r.start = read_int(&m_recv_buffer[5]);
			r.length = read_int(&m_recv_buffer[9]);

			std::deque<peer_request>::iterator i
				= std::find(m_requests.begin(), m_requests.end(), r);
			if (i != m_requests.end())
			{
				m_requests.erase(i);
			}

			if (!has_data() && m_added_to_selector)
			{
				m_added_to_selector = false;
				m_selector.remove_writable(m_socket);
			}

#ifndef NDEBUG
			(*m_logger) << m_socket->sender().as_string() << " <== CANCEL [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif
			break;
		}
	}
	assert(m_recv_pos == m_packet_size);
	return true;
}

void libtorrent::peer_connection::cancel_block(piece_block block)
{
	assert(block.piece_index >= 0);
	assert(block.piece_index < m_torrent->torrent_file().num_pieces());
	assert(m_torrent->picker().is_downloading(block));

	m_torrent->picker().abort_download(block);

	std::deque<piece_block>::iterator i
		= std::find(m_download_queue.begin(), m_download_queue.end(), block);
	assert(i != m_download_queue.end());

	m_download_queue.erase(i);


	int block_offset = block.block_index * m_torrent->block_size();
	int block_size
		= std::min((int)m_torrent->torrent_file().piece_size(block.piece_index)-block_offset,
		m_torrent->block_size());
	assert(block_size > 0);
	assert(block_size <= m_torrent->block_size());

	char buf[] = {0,0,0,13, msg_cancel};

	std::size_t start_offset = m_send_buffer.size();
	m_send_buffer.resize(start_offset + 17);

	std::copy(buf, buf + 5, m_send_buffer.begin()+start_offset);
	start_offset += 5;

	// index
	write_int(block.piece_index, &m_send_buffer[start_offset]);
	start_offset += 4;

	// begin
	write_int(block_offset, &m_send_buffer[start_offset]);
	start_offset += 4;

	// length
	write_int(block_size, &m_send_buffer[start_offset]);
	start_offset += 4;
#ifndef NDEBUG
	(*m_logger) << m_socket->sender().as_string() << " ==> CANCEL [ piece: " << block.piece_index << " | s: " << block_offset << " | l: " << block_size << " | " << block.block_index << " ]\n";
#endif
	assert(start_offset == m_send_buffer.size());

	send_buffer_updated();
}

void libtorrent::peer_connection::request_block(piece_block block)
{
	assert(block.piece_index >= 0);
	assert(block.piece_index < m_torrent->torrent_file().num_pieces());
	assert(!m_torrent->picker().is_downloading(block));

	m_torrent->picker().mark_as_downloading(block, m_peer_id);

	m_download_queue.push_back(block);

	int block_offset = block.block_index * m_torrent->block_size();
	int block_size
		= std::min((int)m_torrent->torrent_file().piece_size(block.piece_index)-block_offset,
		m_torrent->block_size());
	assert(block_size > 0);
	assert(block_size <= m_torrent->block_size());

	char buf[] = {0,0,0,13, msg_request};

	std::size_t start_offset = m_send_buffer.size();
	m_send_buffer.resize(start_offset + 17);

	std::copy(buf, buf + 5, m_send_buffer.begin()+start_offset);
	start_offset +=5;

	// index
	write_int(block.piece_index, &m_send_buffer[start_offset]);
	start_offset += 4;

	// begin
	write_int(block_offset, &m_send_buffer[start_offset]);
	start_offset += 4;

	// length
	write_int(block_size, &m_send_buffer[start_offset]);
	start_offset += 4;
#ifndef NDEBUG
	(*m_logger) << m_socket->sender().as_string() << " ==> REQUEST [ piece: " << block.piece_index << " | s: " << block_offset << " | l: " << block_size << " | " << block.block_index << " ]\n";
#endif
	assert(start_offset == m_send_buffer.size());

	send_buffer_updated();
}

void libtorrent::peer_connection::send_bitfield()
{
#ifndef NDEBUG
	(*m_logger) << m_socket->sender().as_string() << " ==> BITFIELD\n";
#endif
	const int packet_size = (m_have_piece.size() + 7) / 8 + 5;
	const int old_size = m_send_buffer.size();
	m_send_buffer.resize(old_size + packet_size);
	write_int(packet_size - 4, &m_send_buffer[old_size]);
	m_send_buffer[old_size+4] = msg_bitfield;
	std::fill(m_send_buffer.begin()+old_size+5, m_send_buffer.end(), 0);
	for (std::size_t i = 0; i < m_have_piece.size(); ++i)
	{
		if (m_torrent->have_piece(i))
			m_send_buffer[old_size + 5 + (i>>3)] |= 1 << (7 - (i&7));
	}
	send_buffer_updated();
}

void libtorrent::peer_connection::choke()
{
	if (m_choked) return;
	char msg[] = {0,0,0,1,msg_choke};
	m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
	m_choked = true;
#ifndef NDEBUG
	(*m_logger) << m_socket->sender().as_string() << " ==> CHOKE\n";
#endif
	m_requests.clear();
	send_buffer_updated();
}

void libtorrent::peer_connection::unchoke()
{
	if (!m_choked) return;
	char msg[] = {0,0,0,1,msg_unchoke};
	m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
	m_choked = false;
#ifndef NDEBUG
	(*m_logger) << m_socket->sender().as_string() << " ==> UNCHOKE\n";
#endif
	send_buffer_updated();
}

void libtorrent::peer_connection::interested()
{
	if (m_interesting) return;
	char msg[] = {0,0,0,1,msg_interested};
	m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
	m_interesting = true;
#ifndef NDEBUG
	(*m_logger) << m_socket->sender().as_string() << " ==> INTERESTED\n";
#endif
	send_buffer_updated();
}

void libtorrent::peer_connection::not_interested()
{
	if (!m_interesting) return;
	char msg[] = {0,0,0,1,msg_not_interested};
	m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
	m_interesting = false;
#ifndef NDEBUG
	(*m_logger) << m_socket->sender().as_string() << " ==> NOT_INTERESTED\n";
#endif
	send_buffer_updated();
}

void libtorrent::peer_connection::send_have(int index)
{
	const int packet_size = 9;
	char msg[packet_size] = {0,0,0,5,msg_have};
	write_int(index, msg+5);
	m_send_buffer.insert(m_send_buffer.end(), msg, msg + packet_size);
#ifndef NDEBUG
	(*m_logger) << m_socket->sender().as_string() << " ==> HAVE [ piece: " << index << " ]\n";
#endif
	send_buffer_updated();
}

void libtorrent::peer_connection::second_tick()
{
	m_statistics.second_tick();
	m_send_quota_left = m_send_quota;
	if (m_send_quota > 0) send_buffer_updated();

	// If the client sends more data
	// we send it data faster, otherwise, slower.
	// It will also depend on how much data the
	// client has sent us. This is the mean to
	// maintain a 1:1 share ratio with all peers.

	int diff = share_diff();

	if (diff > 2*m_torrent->block_size())
	{
		// if we have downloaded more than one piece more
		// than we have uploaded, have an unlimited
		// upload rate
		m_send_quota_limit = -1;
	}
	else
	{
		// if we have downloaded too much, response with an
		// upload rate of 10 kB/s more than we dowlload
		// if we have uploaded too much, send with a rate of
		// 10 kB/s less than we receive
		int bias = 0;
		if (diff > -2*m_torrent->block_size())
		{
			bias = m_statistics.download_rate() * .5;
			if (bias < 10*1024) bias = 10*1024;
		}
		else
		{
			bias = -m_statistics.download_rate() * .5;
		}
		m_send_quota_limit = m_statistics.download_rate() + bias;
		// the maximum send_quota given our download rate from this peer
		if (m_send_quota_limit < 256) m_send_quota_limit = 256;
	}
}

// --------------------------
// RECEIVE DATA
// --------------------------

// throws exception when the client should be disconnected
void libtorrent::peer_connection::receive_data()
{
	assert(!m_socket->is_blocking());
	assert(m_packet_size > 0);
	for(;;)
	{
		assert(m_packet_size > 0);
		int received = m_socket->receive(&m_recv_buffer[m_recv_pos], m_packet_size - m_recv_pos);

		// connection closed
		if (received == 0)
		{
			throw network_error(0);
		}

		// an error
		if (received < 0)
		{
			// would_block means that no data was ready to be received
			// returns to exit the loop
			if (m_socket->last_error() == socket::would_block)
				return;

			// the connection was closed
			throw network_error(m_socket->last_error());
		}

		if (received > 0)
		{
			m_last_receive = boost::posix_time::second_clock::local_time();

			m_recv_pos += received;

			switch(m_state)
			{
			case read_protocol_length:

				m_statistics.received_bytes(0, received);
				if (m_recv_pos < m_packet_size) break;
				assert(m_recv_pos == m_packet_size);

				m_packet_size = reinterpret_cast<unsigned char&>(m_recv_buffer[0]);
#ifndef NDEBUG
				(*m_logger) << m_socket->sender().as_string() << " protocol length: " << m_packet_size << "\n";
#endif
				m_state = read_protocol_string;
				m_recv_buffer.resize(m_packet_size);
				m_recv_pos = 0;

				if (m_packet_size == 0)
				{
#ifndef NDEBUG
						(*m_logger) << "incorrect protocol length\n";
#endif
						throw network_error(0);
				}
				break;


			case read_protocol_string:
				{
					m_statistics.received_bytes(0, received);
					if (m_recv_pos < m_packet_size) break;
					assert(m_recv_pos == m_packet_size);
#ifndef NDEBUG
					(*m_logger) << m_socket->sender().as_string() << " protocol: '" << std::string(m_recv_buffer.begin(), m_recv_buffer.end()) << "'\n";
#endif
					const char protocol_string[] = "BitTorrent protocol";
					const int protocol_len = sizeof(protocol_string) - 1;
					if (!std::equal(m_recv_buffer.begin(), m_recv_buffer.end(), protocol_string))
					{
#ifndef NDEBUG
						(*m_logger) << "incorrect protocol name\n";
#endif
						throw network_error(0);
					}

					m_state = read_info_hash;
					m_packet_size = 28;
					m_recv_pos = 0;
					m_recv_buffer.resize(28);
				}
				break;


			case read_info_hash:
			{
				m_statistics.received_bytes(0, received);
				if (m_recv_pos < m_packet_size) break;
				assert(m_recv_pos == m_packet_size);
				// ok, now we have got enough of the handshake. Is this connection
				// attached to a torrent?

				if (m_torrent == 0)
				{
					// TODO: if the protocol is to be extended
					// these 8 bytes would be used to describe the
					// extensions available on the other side

					// now, we have to see if there's a torrent with the
					// info_hash we got from the peer
					sha1_hash info_hash;
					std::copy(m_recv_buffer.begin()+8, m_recv_buffer.begin() + 28, (char*)info_hash.begin());
					
					m_torrent = m_ses.find_torrent(info_hash);
					if (m_torrent == 0)
					{
						// we couldn't find the torrent!
#ifndef NDEBUG
						(*m_logger) << m_socket->sender().as_string() << " couldn't find a torrent with the given info_hash\n";
#endif
						throw network_error(0);
					}

					// assume the other end has no pieces
					m_have_piece.resize(m_torrent->torrent_file().num_pieces());
					std::fill(m_have_piece.begin(), m_have_piece.end(), false);

					// yes, we found the torrent
					// reply with our handshake
					std::copy(m_recv_buffer.begin()+28, m_recv_buffer.begin() + 48, (char*)m_peer_id.begin());
					send_handshake();
					send_bitfield();
				}
				else
				{
					// verify info hash
					if (!std::equal(m_recv_buffer.begin()+8, m_recv_buffer.begin() + 28, (const char*)m_torrent->torrent_file().info_hash().begin()))
					{
#ifndef NDEBUG
						(*m_logger) << m_socket->sender().as_string() << " received invalid info_hash\n";
#endif
						throw network_error(0);
					}
				}

				m_state = read_peer_id;
				m_packet_size = 20;
				m_recv_pos = 0;
				m_recv_buffer.resize(20);
#ifndef NDEBUG
				(*m_logger) << m_socket->sender().as_string() << " info_hash received\n";
#endif
				break;
			}


			case read_peer_id:
			{
				m_statistics.received_bytes(0, received);
				if (m_recv_pos < m_packet_size) break;
				assert(m_recv_pos == m_packet_size);

				if (m_active)
				{
					// verify peer_id
					// TODO: It seems like the original client ignores to check the peer id
					// can that be correct?
					if (!std::equal(m_recv_buffer.begin(), m_recv_buffer.begin() + 20, (const char*)m_peer_id.begin()))
					{
#ifndef NDEBUG
						(*m_logger) << m_socket->sender().as_string() << " invalid peer_id (it doesn't equal the one from the tracker)\n";
#endif
						throw network_error(0);
					}
				}
				else
				{
					// check to make sure we don't have another connection with the same
					// info_hash and peer_id. If we do. close this connection.
					std::copy(m_recv_buffer.begin(), m_recv_buffer.begin() + 20, (char*)m_peer_id.begin());

					if (m_torrent->has_peer(m_peer_id))
					{
#ifndef NDEBUG
						(*m_logger) << m_socket->sender().as_string() << " duplicate connection, closing\n";
#endif
						throw network_error(0);
					}

					m_attached_to_torrent = true;
					m_torrent->attach_peer(this);
					assert(m_torrent->get_policy().has_connection(this));
				}

				m_state = read_packet_size;
				m_packet_size = 4;
				m_recv_pos = 0;
				m_recv_buffer.resize(4);
#ifndef NDEBUG
				(*m_logger) << m_socket->sender().as_string() << " received peer_id\n";
#endif
				break;
			}


			case read_packet_size:
				m_statistics.received_bytes(0, received);
				if (m_recv_pos < m_packet_size) break;
				assert(m_recv_pos == m_packet_size);

				// convert from big endian to native byte order
				m_packet_size = read_int(&m_recv_buffer[0]);
				// don't accept packets larger than 1 MB
				if (m_packet_size > 1024*1024 || m_packet_size < 0)
				{
#ifndef NDEBUG
					(*m_logger) << m_socket->sender().as_string() << " packet too large (packet_size > 1 Megabyte), abort\n";
#endif
					// packet too large
					throw network_error(0);
				}
				
				if (m_packet_size == 0)
				{
					// keepalive message
					m_state = read_packet_size;
					m_packet_size = 4;
				}
				else
				{
					m_state = read_packet;
					m_recv_buffer.resize(m_packet_size);
				}
				m_recv_pos = 0;
				assert(m_packet_size > 0);
				break;

			case read_packet:

				if (dispatch_message(received))
				{
					m_state = read_packet_size;
					m_packet_size = 4;
					m_recv_buffer.resize(4);
					m_recv_pos = 0;
					assert(m_packet_size > 0);
				}
				break;
			}
		}
	}
	assert(m_packet_size > 0);
}


bool libtorrent::peer_connection::has_data() const throw()
{
	// if we have requests or pending data to be sent or announcements to be made
	// we want to send data
	return ((!m_requests.empty() && !m_choked)
		|| !m_send_buffer.empty()
		|| !m_announce_queue.empty())
		&& m_send_quota_left != 0;
}

// --------------------------
// SEND DATA
// --------------------------

// throws exception when the client should be disconnected
void libtorrent::peer_connection::send_data()
{
	assert(m_socket->is_writable());
	assert(has_data());

	// only add new piece-chunks if the send buffer is small enough
	// otherwise there will be no end to how large it will be!
	// TODO: make this a bit better. Don't always read the entire
	// requested block. Have a limit of how much of the requested
	// block is actually read at a time.
	while (!m_requests.empty()
		&& (m_send_buffer.size() < m_torrent->block_size())
		&& !m_choked)
	{
		peer_request& r = m_requests.front();
		
		if (r.piece >= 0 && r.piece < m_have_piece.size() && m_torrent && m_torrent->have_piece(r.piece))
		{
			// make sure the request is ok
			if (r.start + r.length > m_torrent->torrent_file().piece_size(r.piece))
			{
				// NOT OK! disconnect
				throw network_error(0);
			}

			if (r.length <= 0 || r.start < 0)
			{
				// NOT OK! disconnect
				throw network_error(0);
			}

#ifndef NDEBUG
			assert(m_torrent->verify_piece(r.piece) && "internal error");
#endif
			const int send_buffer_offset = m_send_buffer.size();
			const int packet_size = 4 + 5 + 4 + r.length;
			m_send_buffer.resize(send_buffer_offset + packet_size);
			write_int(packet_size-4, &m_send_buffer[send_buffer_offset]);
			m_send_buffer[send_buffer_offset+4] = msg_piece;
			write_int(r.piece, &m_send_buffer[send_buffer_offset+5]);
			write_int(r.start, &m_send_buffer[send_buffer_offset+9]);

			m_torrent->filesystem().read(
				&m_send_buffer[send_buffer_offset+13]
				, r.piece
				, r.start
				, r.length);
#ifndef NDEBUG
			(*m_logger) << m_socket->sender().as_string() << " ==> PIECE [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif
			m_payloads.push_back(range(send_buffer_offset+13, r.length));
		}
		else
		{
#ifndef NDEBUG
			(*m_logger) << m_socket->sender().as_string()
				<< " *** WARNING [ illegal piece request idx: " << r.piece
				<< " | s: " << r.start
				<< " | l: " << r.length
				<< " | max_piece: " << m_have_piece.size()
				<< " | torrent: " << (m_torrent != 0)
				<< " | have: " << m_torrent->have_piece(r.piece)
				<< " ]\n";
#endif
		}
		m_requests.erase(m_requests.begin());
	}

	if (!m_announce_queue.empty())
	{
		for (std::vector<int>::iterator i = m_announce_queue.begin();
			i != m_announce_queue.end();
			++i)
		{
//			(*m_logger) << "have piece: " << *i << " sent to: " << m_socket->sender().as_string() << "\n";
			send_have(*i);
		}
		m_announce_queue.clear();
	}

	assert(m_send_quota_left != 0);

	// send the actual buffer
	if (!m_send_buffer.empty())
	{

		int amount_to_send = m_send_buffer.size();
		assert(m_send_quota_left != 0);
		if (m_send_quota_left > 0)
			amount_to_send = std::min(m_send_quota_left, amount_to_send);
		// we have data that's scheduled for sending
		int sent = m_socket->send(
			&m_send_buffer[0]
			, amount_to_send);

	#ifndef NDEBUG
		(*m_logger) << m_socket->sender().as_string() << " ==> SENT [ length: " << sent << " ]\n";
	#endif

		if (sent > 0)
		{
			if (m_send_quota_left != -1)
			{
				assert(m_send_quota_left >= sent);
				m_send_quota_left -= sent;
			}

			int amount_payload = 0;
			if (!m_payloads.empty())
			{
				for (std::deque<range>::iterator i = m_payloads.begin();
					i != m_payloads.end();
					++i)
				{
					i->start -= sent;
					if (i->start < 0)
					{
						if (i->start + i->length <= 0)
						{
							amount_payload += i->length;
						}
						else
						{
							amount_payload += -i->start;
							i->length -= -i->start;
							i->start = 0;
						}
					}
				}
			}
			// remove all payload ranges that has been sent
			m_payloads.erase(
				std::remove_if(m_payloads.begin(), m_payloads.end(), range_below_zero)
				, m_payloads.end());

			assert(amount_payload <= sent);
			m_statistics.sent_bytes(amount_payload, sent - amount_payload);

			// empty the entire buffer at once or if
			// only a part of the buffer could be sent
			// remove the part that was sent from the buffer
			if (sent == m_send_buffer.size())
			{
				m_send_buffer.clear();
			}
			else
			{
				m_send_buffer.erase(
					m_send_buffer.begin()
					, m_send_buffer.begin() + sent);
			}
		}
		else
		{
			assert(sent == -1);
			throw network_error(m_socket->last_error());
		}

		m_last_sent = boost::posix_time::second_clock::local_time();
	}

	assert(m_added_to_selector);
	send_buffer_updated();
#ifndef NDEBUG
	if (has_data())
	{
		if (m_socket->is_writable())
		{
			std::cout << "ERROR\n";
		}
	}
#endif
}


void libtorrent::peer_connection::keep_alive()
{
	boost::posix_time::time_duration d;
	d = boost::posix_time::second_clock::local_time() - m_last_sent;
	if (d.seconds() > m_timeout / 2)
	{
		char noop[] = {0,0,0,0};
		m_send_buffer.insert(m_send_buffer.end(), noop, noop+4);
		m_last_sent = boost::posix_time::second_clock::local_time();
#ifndef NDEBUG
		(*m_logger) << m_socket->sender().as_string() << " ==> NOP\n";
#endif
		send_buffer_updated();
	}
}
