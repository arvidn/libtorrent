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

#if defined(_MSC_VER) && _MSC_VER < 1300
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

libtorrent::peer_connection::peer_connection(detail::session_impl* ses, torrent* t, boost::shared_ptr<libtorrent::socket> s, const peer_id& p)
	: m_state(read_protocol_length)
	, m_timeout(120)
	, m_packet_size(1)
	, m_recv_pos(0)
	, m_last_receive(std::time(0))
	, m_last_sent(std::time(0))
	, m_socket(s)
	, m_torrent(t)
	, m_ses(ses)
	, m_active(true)
	, m_peer_id(p)
	, m_peer_interested(false)
	, m_peer_choked(true)
	, m_interesting(false)
	, m_choked(true)
{
	assert(m_torrent != 0);

#if defined(TORRENT_VERBOSE_LOGGING)
	m_logger = m_ses->create_log(s->sender().as_string().c_str());
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

libtorrent::peer_connection::peer_connection(detail::session_impl* ses, boost::shared_ptr<libtorrent::socket> s)
	: m_state(read_protocol_length)
	, m_timeout(120)
	, m_packet_size(1)
	, m_recv_pos(0)
	, m_last_receive(std::time(0))
	, m_last_sent(std::time(0))
	, m_socket(s)
	, m_torrent(0)
	, m_ses(ses)
	, m_active(false)
	, m_peer_id()
	, m_peer_interested(false)
	, m_peer_choked(true)
	, m_interesting(false)
	, m_choked(true)
{

#if defined(TORRENT_VERBOSE_LOGGING)
	m_logger = m_ses->create_log(s->sender().as_string().c_str());
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
	m_receiving_piece.close();
	if (m_torrent) m_torrent->remove_peer(this);
}

void libtorrent::peer_connection::send_handshake()
{
	assert(m_send_buffer.size() == 0);

	// add handshake to the send buffer
	m_send_buffer.resize(68);
	const char* version_string = "BitTorrent protocol";
	m_send_buffer[0] = 19;
	std::copy(version_string, version_string+19, m_send_buffer.begin()+1);
	std::fill(m_send_buffer.begin() + 20, m_send_buffer.begin() + 28, 0);
	std::copy(m_torrent->torrent_file().info_hash().begin(), m_torrent->torrent_file().info_hash().end(), m_send_buffer.begin() + 28);
	std::copy(m_ses->get_peer_id().begin(), m_ses->get_peer_id().end(), m_send_buffer.begin() + 48);

#if defined(TORRENT_VERBOSE_LOGGING)
	(*m_logger) << m_socket->sender().as_string() << " ==> HANDSHAKE\n";
#endif

}

bool libtorrent::peer_connection::dispatch_message()
{
	int packet_type = m_recv_buffer[0];
	if (packet_type > 8 || packet_type < 0)
		return false;

	switch (packet_type)
	{

		// *************** CHOKE ***************
	case msg_choke:
#if defined(TORRENT_VERBOSE_LOGGING)
		(*m_logger) << m_socket->sender().as_string() << " <== CHOKE\n";
#endif
		m_peer_choked = true;
		m_torrent->get_policy().choked(*this);

		// remove all pieces from this peers download queue and
		// remove the 'downloading' flag from piece_picker.
		for (std::vector<piece_block>::iterator i = m_download_queue.begin();
			i != m_download_queue.end();
			++i)
		{
			m_torrent->picker().abort_download(*i);
		}
		m_download_queue.clear();
#ifndef NDEBUG
		m_torrent->picker().integrity_check(m_torrent);
#endif
		break;



		// *************** UNCHOKE ***************
	case msg_unchoke:
#if defined(TORRENT_VERBOSE_LOGGING)
		(*m_logger) << m_socket->sender().as_string() << " <== UNCHOKE\n";
#endif
		m_peer_choked = false;
		m_torrent->get_policy().unchoked(*this);
		break;


		// *************** INTERESTED ***************
	case msg_interested:
#if defined(TORRENT_VERBOSE_LOGGING)
		(*m_logger) << m_socket->sender().as_string() << " <== INTERESTED\n";
#endif
		m_peer_interested = true;
		m_torrent->get_policy().interested(*this);
		break;


		// *************** NOT INTERESTED ***************
	case msg_not_interested:
#if defined(TORRENT_VERBOSE_LOGGING)
		(*m_logger) << m_socket->sender().as_string() << " <== NOT_INTERESTED\n";
#endif
		m_peer_interested = false;
		m_torrent->get_policy().not_interested(*this);
		break;



		// *************** HAVE ***************
	case msg_have:
		{
			std::size_t index = read_int(&m_recv_buffer[1]);
			// if we got an invalid message, abort
			if (index >= m_have_piece.size())
				return false;

#if defined(TORRENT_VERBOSE_LOGGING)
			(*m_logger) << m_socket->sender().as_string() << " <== HAVE [ piece: " << index << "]\n";
#endif

			if (m_have_piece[index])
			{
#if defined(TORRENT_VERBOSE_LOGGING)
				(*m_logger) << m_socket->sender().as_string() << " oops.. we already knew that: " << index << "\n";
#endif
			}
			else
			{
				m_have_piece[index] = true;
				if (m_torrent->peer_has(index))
					m_torrent->get_policy().peer_is_interesting(*this);
			}
			break;
		}




		// *************** BITFIELD ***************
	case msg_bitfield:
		{
			if (m_packet_size - 1 != (m_have_piece.size() + 7) / 8)
				return false;

#if defined(TORRENT_VERBOSE_LOGGING)
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
#if defined(TORRENT_VERBOSE_LOGGING)
				(*m_logger) << m_socket->sender().as_string() << " *** THIS IS A SEED ***\n";
#endif
			}

			if (interesting) m_torrent->get_policy().peer_is_interesting(*this);

			break;
		}


		// *************** REQUEST ***************
	case msg_request:
		{
			peer_request r;
			r.piece = read_int(&m_recv_buffer[1]);
			r.start = read_int(&m_recv_buffer[5]);
			r.length = read_int(&m_recv_buffer[9]);
			m_requests.push_back(r);

#if defined(TORRENT_VERBOSE_LOGGING)
			(*m_logger) << m_socket->sender().as_string() << " <== REQUEST [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif

			break;
		}



		// *************** PIECE ***************
	case msg_piece:
		{
			std::size_t index = read_int(&m_recv_buffer[1]);
			if (index < 0 || index >= m_torrent->torrent_file().num_pieces())
			{
#if defined(TORRENT_VERBOSE_LOGGING)
				(*m_logger) << m_socket->sender().as_string() << " piece index invalid\n";
#endif
				return false;
			}
			int offset = read_int(&m_recv_buffer[5]);
			int len = m_packet_size - 9;

			if (offset < 0)
			{
#if defined(TORRENT_VERBOSE_LOGGING)
				(*m_logger) << m_socket->sender().as_string() << " offset < 0\n";
#endif
				return false;
			}

			if (offset + len > m_torrent->torrent_file().piece_size(index))
			{
#if defined(TORRENT_VERBOSE_LOGGING)
				(*m_logger) << m_socket->sender().as_string() << " piece packet contains more data than the piece size\n";
#endif
				return false;
			}

			if (offset % m_torrent->block_size() != 0)
			{
#if defined(TORRENT_VERBOSE_LOGGING)
				(*m_logger) << m_socket->sender().as_string() << " piece packet contains unaligned offset\n";
#endif
				return false;
			}

			piece_block req = m_download_queue.front();
			if (req.piece_index != index)
			{
#if defined(TORRENT_VERBOSE_LOGGING)
				(*m_logger) << m_socket->sender().as_string() << " piece packet contains unrequested index\n";
#endif
				return false;
			}

			if (req.block_index != offset / m_torrent->block_size())
			{
#if defined(TORRENT_VERBOSE_LOGGING)
				(*m_logger) << m_socket->sender().as_string() << " piece packet contains unrequested offset\n";
#endif
				return false;
			}

			m_receiving_piece.open(m_torrent->filesystem(), index, piece_file::out, offset);

#if defined(TORRENT_VERBOSE_LOGGING)
			(*m_logger) << m_socket->sender().as_string() << " <== PIECE [ piece: " << index << " | s: " << offset << " | l: " << len << " ]\n";
#endif

			m_receiving_piece.write(&m_recv_buffer[9], len);
			m_torrent->downloaded_bytes(len);

			piece_picker& picker = m_torrent->picker();
			piece_block block_finished(index, offset / m_torrent->block_size());
			picker.mark_as_finished(block_finished);

			// pop the request that just finished
			// from the download queue
			m_download_queue.erase(m_download_queue.begin());
			m_torrent->m_unverified_blocks++;

			// did we just finish the piece?
			if (picker.is_piece_finished(index))
			{
				m_torrent->m_unverified_blocks -= picker.blocks_in_piece(index);

				bool verified = m_torrent->filesystem()->verify_piece(m_receiving_piece);
				if (verified)
				{
					m_torrent->announce_piece(index);
				}
				else
				{
					// we have to let the piece_picker know that
					// this piece failed the check as it can restore it
					// and mark it as being interesting for download
					// TODO: do this more intelligently! and keep track
					// of how much crap (data that failed hash-check) and
					// how much redundant data we have downloaded
					picker.restore_piece(index);
				}
				m_torrent->get_policy().piece_finished(*this, index, verified);
			}
			m_torrent->get_policy().block_finished(*this, block_finished);
			break;
		}


		// *************** CANCEL ***************
	case msg_cancel:
		{
			peer_request r;
			r.piece = read_int(&m_recv_buffer[1]);
			r.start = read_int(&m_recv_buffer[5]);
			r.length = read_int(&m_recv_buffer[9]);

			std::vector<peer_request>::iterator i
				= std::find(m_requests.begin(), m_requests.end(), r);
			if (i != m_requests.end())
			{
				m_requests.erase(i);
			}

#if defined(TORRENT_VERBOSE_LOGGING)
			(*m_logger) << m_socket->sender().as_string() << " <== CANCEL [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif
			m_requests.clear();
			break;
		}
	}

	return true;
}

void libtorrent::peer_connection::request_block(piece_block block)
{
	assert(block.piece_index >= 0);
	assert(block.piece_index < m_torrent->torrent_file().num_pieces());
	assert(!m_torrent->picker().is_downloading(block));

	m_torrent->picker().mark_as_downloading(block);

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

	// TODO: add a timeout to disconnect peer if we don't get any piece messages when
	// we have requested.

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
#if defined(TORRENT_VERBOSE_LOGGING)
	(*m_logger) << m_socket->sender().as_string() << " ==> REQUEST [ piece: " << block.piece_index << " | s: " << block_offset << " | l: " << block_size << " | " << block.block_index << " ]\n";
#endif
	assert(start_offset == m_send_buffer.size());

}

void libtorrent::peer_connection::send_bitfield()
{
#if defined(TORRENT_VERBOSE_LOGGING)
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
}

void libtorrent::peer_connection::choke()
{
	if (m_choked) return;
	char msg[] = {0,0,0,1,msg_choke};
	m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
	m_choked = true;
#if defined(TORRENT_VERBOSE_LOGGING)
	(*m_logger) << m_socket->sender().as_string() << " ==> CHOKE\n";
#endif
}

void libtorrent::peer_connection::unchoke()
{
	if (!m_choked) return;
	char msg[] = {0,0,0,1,msg_unchoke};
	m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
	m_choked = false;
#if defined(TORRENT_VERBOSE_LOGGING)
	(*m_logger) << m_socket->sender().as_string() << " ==> UNCHOKE\n";
#endif
}

void libtorrent::peer_connection::interested()
{
	if (m_interesting) return;
	char msg[] = {0,0,0,1,msg_interested};
	m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
	m_interesting = true;
#if defined(TORRENT_VERBOSE_LOGGING)
	(*m_logger) << m_socket->sender().as_string() << " ==> INTERESTED\n";
#endif
}

void libtorrent::peer_connection::not_interested()
{
	if (!m_interesting) return;
	char msg[] = {0,0,0,1,msg_not_interested};
	m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
	m_interesting = false;
#if defined(TORRENT_VERBOSE_LOGGING)
	(*m_logger) << m_socket->sender().as_string() << " ==> NOT_INTERESTED\n";
#endif
}

void libtorrent::peer_connection::send_have(int index)
{
	char msg[9] = {0,0,0,5,msg_have};
	write_int(index, msg+5);
	m_send_buffer.insert(m_send_buffer.end(), msg, msg+9);
#if defined(TORRENT_VERBOSE_LOGGING)
	(*m_logger) << m_socket->sender().as_string() << " ==> HAVE [ piece: " << index << " ]\n";
#endif
}


// --------------------------
// RECEIVE DATA
// --------------------------

// throws exception when the client should be disconnected
void libtorrent::peer_connection::receive_data()
{
	for(;;)
	{
		int received = m_socket->receive(&m_recv_buffer[m_recv_pos], m_packet_size - m_recv_pos);

		// connection closed
		if (received == 0) throw network_error(0);

		// an error
		if (received < 0)
		{
			// would block means that no data was ready to be received
			if (m_socket->last_error() == socket::would_block) return;

			// the connection was closed
			throw network_error(0);
		}

		if (received > 0)
		{
			m_statistics.received_bytes(received);
			m_last_receive = boost::posix_time::second_clock::local_time();

			m_recv_pos += received;

			if (m_recv_pos == m_packet_size)
			{
				switch(m_state)
				{
				case read_protocol_length:
					m_packet_size = reinterpret_cast<unsigned char&>(m_recv_buffer[0]);
	#if defined(TORRENT_VERBOSE_LOGGING)
					(*m_logger) << m_socket->sender().as_string() << " protocol length: " << m_packet_size << "\n";
	#endif
					m_state = read_protocol_version;
					if (m_packet_size != 19)
						throw network_error(0);
					m_recv_buffer.resize(m_packet_size);
					m_recv_pos = 0;
					break;


				case read_protocol_version:
					{
						const char* protocol_version = "BitTorrent protocol";
	#if defined(TORRENT_VERBOSE_LOGGING)
						(*m_logger) << m_socket->sender().as_string() << " protocol name: " << std::string(m_recv_buffer.begin(), m_recv_buffer.end()) << "\n";
	#endif
						if (!std::equal(m_recv_buffer.begin(), m_recv_buffer.end(), protocol_version))
						{
							// unknown protocol, close connection
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
					// ok, now we have got enough of the handshake. Is this connection
					// attached to a torrent?

					if (m_torrent == 0)
					{
						// no, we have to see if there's a torrent with the
						// info_hash we got from the peer
						sha1_hash info_hash;
						std::copy(m_recv_buffer.begin()+8, m_recv_buffer.begin() + 28, (char*)info_hash.begin());
						
						m_torrent = m_ses->find_active_torrent(info_hash);
						if (m_torrent == 0)
						{
							// we couldn't find the torrent!
	#if defined(TORRENT_VERBOSE_LOGGING)
							(*m_logger) << m_socket->sender().as_string() << " couldn't find a torrent with the given info_hash\n";
	#endif
							throw network_error(0);
						}
						m_torrent->attach_peer(this);

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
	#if defined(TORRENT_VERBOSE_LOGGING)
							(*m_logger) << m_socket->sender().as_string() << " received invalid info_hash\n";
	#endif
							throw network_error(0);
						}
					}

					m_state = read_peer_id;
					m_packet_size = 20;
					m_recv_pos = 0;
					m_recv_buffer.resize(20);
	#if defined(TORRENT_VERBOSE_LOGGING)
					(*m_logger) << m_socket->sender().as_string() << " info_hash received\n";
	#endif
					break;
				}


				case read_peer_id:
				{
					if (m_active)
					{
						// verify peer_id
						// TODO: It seems like the original client ignores to check the peer id
						// can this be correct?
						if (!std::equal(m_recv_buffer.begin(), m_recv_buffer.begin() + 20, (const char*)m_peer_id.begin()))
						{
	#if defined(TORRENT_VERBOSE_LOGGING)
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
						if (m_torrent->num_connections(m_peer_id) > 1)
						{
	#if defined(TORRENT_VERBOSE_LOGGING)
							(*m_logger) << m_socket->sender().as_string() << " duplicate connection, closing\n";
	#endif
							throw network_error(0);
						}
					}

					m_state = read_packet_size;
					m_packet_size = 4;
					m_recv_pos = 0;
					m_recv_buffer.resize(4);
	#if defined(TORRENT_VERBOSE_LOGGING)
					(*m_logger) << m_socket->sender().as_string() << " received peer_id\n";
	#endif
					break;
				}


				case read_packet_size:
					// convert from big endian to native byte order
					m_packet_size = read_int(&m_recv_buffer[0]);
					// don't accept packets larger than 1 MB
					if (m_packet_size > 1024*1024 || m_packet_size < 0)
					{
	#if defined(TORRENT_VERBOSE_LOGGING)
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
					break;

				case read_packet:
					if (!dispatch_message())
					{
	#if defined(TORRENT_VERBOSE_LOGGING)
						(*m_logger) << m_socket->sender().as_string() << " received invalid packet\n";
	#endif
						// invalid message
						throw network_error(0);
					}

					m_state = read_packet_size;
					m_packet_size = 4;
					m_recv_buffer.resize(4);
					m_recv_pos = 0;
					break;
				}
			}
		}
	}
}


bool libtorrent::peer_connection::has_data() const throw()
{
	// if we have requests or pending data to be sent or announcements to be made
	// we want to send data
	return !m_requests.empty() || !m_send_buffer.empty() || !m_announce_queue.empty();
}

// --------------------------
// SEND DATA
// --------------------------

// throws exception when the client should be disconnected
void libtorrent::peer_connection::send_data()
{
	// only add new piece-chunks if the send buffer is empty
	// otherwise there will be no end to how large it will be!
	if (!m_requests.empty() && m_send_buffer.empty() && m_peer_interested && !m_choked)
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

			if (m_sending_piece.index() != r.piece)
			{
				m_sending_piece.open(m_torrent->filesystem(), r.piece, piece_file::in);
#ifndef NDEBUG
				assert(m_torrent->filesystem()->verify_piece(m_sending_piece) && "internal error");
				m_sending_piece.open(m_torrent->filesystem(), r.piece, piece_file::in);
#endif
			}
			const int packet_size = 4 + 5 + 4 + r.length;
			m_send_buffer.resize(packet_size);
			write_int(packet_size-4, &m_send_buffer[0]);
			m_send_buffer[4] = msg_piece;
			write_int(r.piece, &m_send_buffer[5]);
			write_int(r.start, &m_send_buffer[9]);

			if (r.start > m_sending_piece.tell())
			{
				m_sending_piece.seek_forward(r.start - m_sending_piece.tell());
			}
			else if (r.start < m_sending_piece.tell())
			{
				int index = m_sending_piece.index();
				m_sending_piece.close();
				m_sending_piece.open(m_torrent->filesystem(), index, piece_file::in);
				m_sending_piece.seek_forward(r.start);
			}

			m_sending_piece.read(&m_send_buffer[13], r.length);
#if defined(TORRENT_VERBOSE_LOGGING)
			(*m_logger) << m_socket->sender().as_string() << " ==> PIECE [ idx: " << r.piece << " | s: " << r.start << " | l: " << r.length << " | dest: " << m_socket->sender().as_string() << " ]\n";
#endif
			// let the torrent keep track of how much we have uploaded
			m_torrent->uploaded_bytes(r.length);
			m_requests.erase(m_requests.begin());
		}
		else
		{
#if defined(TORRENT_VERBOSE_LOGGING)
			(*m_logger) << m_socket->sender().as_string() << " *** WARNING [ illegal piece request ]\n";
#endif
		}
	}

	if (!m_announce_queue.empty())
	{
		for (std::vector<int>::iterator i = m_announce_queue.begin(); i != m_announce_queue.end(); ++i)
		{
//			(*m_logger) << "have piece: " << *i << " sent to: " << m_socket->sender().as_string() << "\n";
			send_have(*i);
		}
		m_announce_queue.clear();
	}

	// send the actual buffer
	if (!m_send_buffer.empty())
	{
		// we have data that's scheduled for sending
		std::size_t sent = m_socket->send(&m_send_buffer[0], m_send_buffer.size());

#if defined(TORRENT_VERBOSE_LOGGING)
		(*m_logger) << m_socket->sender().as_string() << " ==> SENT [ length: " << sent << " ]\n";
#endif

		if (sent > 0)
		{
			m_statistics.sent_bytes(sent);

			// empty the entire buffer at once or if
			// only a part of the buffer could be sent
			// remove the part that was sent from the buffer
			if (sent == m_send_buffer.size())
				m_send_buffer.clear();
			else
				m_send_buffer.erase(m_send_buffer.begin(), m_send_buffer.begin() + sent);
		}

		m_last_sent = boost::posix_time::second_clock::local_time();
	}

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
#if defined(TORRENT_VERBOSE_LOGGING)
		(*m_logger) << m_socket->sender().as_string() << " ==> NOP\n";
#endif

	}
}
