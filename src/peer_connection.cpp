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
#include <limits>

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/io.hpp"

#if defined(_MSC_VER)
#define for if (false) {} else for
#endif

#define VERBOSE

namespace libtorrent
{

	// the names of the extensions to look for in
	// the extensions-message
	const char* peer_connection::extension_names[] =
	{ "chat" };

	const peer_connection::message_handler peer_connection::m_message_handler[] =
	{
		&peer_connection::on_choke,
		&peer_connection::on_unchoke,
		&peer_connection::on_interested,
		&peer_connection::on_not_interested,
		&peer_connection::on_have,
		&peer_connection::on_bitfield,
		&peer_connection::on_request,
		&peer_connection::on_piece,
		&peer_connection::on_cancel,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		&peer_connection::on_extension_list,
		&peer_connection::on_extended
	};


	peer_connection::peer_connection(
		detail::session_impl& ses
		, selector& sel
		, torrent* t
		, boost::shared_ptr<libtorrent::socket> s)
		: m_state(read_protocol_length)
		, m_timeout(120)
		, m_packet_size(1)
		, m_recv_pos(0)
		, m_last_receive(boost::posix_time::second_clock::local_time())
		, m_last_sent(boost::posix_time::second_clock::local_time())
		, m_selector(sel)
		, m_socket(s)
		, m_torrent(t)
		, m_attached_to_torrent(true)
		, m_ses(ses)
		, m_active(true)
		, m_added_to_selector(false)
		, m_peer_interested(false)
		, m_peer_choked(true)
		, m_interesting(false)
		, m_choked(true)
		, m_supports_extensions(false)
		, m_num_pieces(0)
		, m_free_upload(0)
		, m_send_quota_left(0)
		, m_trust_points(0)
		, m_num_invalid_requests(0)
		, m_last_piece(boost::posix_time::second_clock::local_time())
		, m_disconnecting(false)
		, m_became_uninterested(boost::posix_time::second_clock::local_time())
		, m_became_uninteresting(boost::posix_time::second_clock::local_time())
	{
		INVARIANT_CHECK;

		assert(!m_socket->is_blocking());
		assert(m_torrent != 0);

	#ifndef NDEBUG
		m_logger = m_ses.create_log(s->sender().as_string().c_str());
	#endif

		std::fill(m_peer_id.begin(), m_peer_id.end(), 0);

		// initialize the extension list to zero, since
		// we don't know which extensions the other
		// end supports yet
		std::fill(m_extension_messages, m_extension_messages + num_supported_extensions, -1);

		send_handshake();

		// start in the state where we are trying to read the
		// handshake from the other side
		m_recv_buffer.resize(1);

		// assume the other end has no pieces
		m_have_piece.resize(m_torrent->torrent_file().num_pieces());
		std::fill(m_have_piece.begin(), m_have_piece.end(), false);

		send_bitfield();
	}

	peer_connection::peer_connection(
		detail::session_impl& ses
		, selector& sel
		, boost::shared_ptr<libtorrent::socket> s)
		: m_state(read_protocol_length)
		, m_timeout(120)
		, m_packet_size(1)
		, m_recv_pos(0)
		, m_last_receive(boost::posix_time::second_clock::local_time())
		, m_last_sent(boost::posix_time::second_clock::local_time())
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
		, m_supports_extensions(false)
		, m_num_pieces(0)
		, m_free_upload(0)
		, m_send_quota_left(0)
		, m_trust_points(0)
		, m_num_invalid_requests(0)
		, m_last_piece(boost::posix_time::second_clock::local_time())
		, m_disconnecting(false)
		, m_became_uninterested(boost::posix_time::second_clock::local_time())
		, m_became_uninteresting(boost::posix_time::second_clock::local_time())
	{
		INVARIANT_CHECK;

		assert(!m_socket->is_blocking());

		std::fill(m_peer_id.begin(), m_peer_id.end(), 0);

	#ifndef NDEBUG
		m_logger = m_ses.create_log(s->sender().as_string().c_str());
	#endif

		// initialize the extension list to zero, since
		// we don't know which extensions the other
		// end supports yet
		std::fill(m_extension_messages, m_extension_messages + num_supported_extensions, -1);

		// we are not attached to any torrent yet.
		// we have to wait for the handshake to see
		// which torrent the connector want's to connect to

		// start in the state where we are trying to read the
		// handshake from the other side
		m_recv_buffer.resize(1);
	}

	peer_connection::~peer_connection()
	{
		m_selector.remove(m_socket);
		if (m_attached_to_torrent)
		{
			assert(m_torrent != 0);
			m_torrent->remove_peer(this);
		}
	}

	void peer_connection::send_handshake()
	{
		INVARIANT_CHECK;

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
		// indicate that we support the extension protocol
		// curently disabled
//		m_send_buffer[pos] = 0x80;
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
		(*m_logger) << " ==> HANDSHAKE\n";
	#endif

		send_buffer_updated();
	}

	// verifies a piece to see if it is valid (is within a valid range)
	// and if it can correspond to a request generated by libtorrent.
	bool peer_connection::verify_piece(const peer_request& p) const
	{
		return p.piece >= 0
			&& p.piece < m_torrent->torrent_file().num_pieces()
			&& p.length > 0
			&& p.start >= 0
			&& (p.length == m_torrent->block_size()
				|| (p.length < m_torrent->block_size()
				&& p.piece == m_torrent->torrent_file().num_pieces()-1
				&& p.start + p.length == m_torrent->torrent_file().piece_size(p.piece)))
			&& p.start + p.length <= m_torrent->torrent_file().piece_size(p.piece)
			&& p.start % m_torrent->block_size() == 0;
	}

	boost::optional<piece_block_progress> peer_connection::downloading_piece() const
	{
		// are we currently receiving a 'piece' message?
		if (m_state != read_packet
			|| m_recv_pos < 9
			|| m_recv_buffer[0] != msg_piece)
			return boost::optional<piece_block_progress>();

		const char* ptr = &m_recv_buffer[1];
		peer_request r;
		r.piece = detail::read_int32(ptr);
		r.start = detail::read_int32(ptr);
		r.length = m_packet_size - 9;

		// is any of the piece message header data invalid?
		if (!verify_piece(r))
			return boost::optional<piece_block_progress>();

		piece_block_progress p;

		p.piece_index = r.piece;
		p.block_index = r.start / m_torrent->block_size();
		p.bytes_downloaded = m_recv_pos - 9;
		p.full_block_bytes = r.length;

		return boost::optional<piece_block_progress>(p);
	}


	// message handlers

	// -----------------------------
	// ----------- CHOKE -----------
	// -----------------------------

	void peer_connection::on_choke(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (m_packet_size != 1)
			throw protocol_error("'choke' message size != 1");
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return;

#ifndef NDEBUG
		(*m_logger) << " <== CHOKE\n";
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
	}

	// -----------------------------
	// ---------- UNCHOKE ----------
	// -----------------------------

	void peer_connection::on_unchoke(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (m_packet_size != 1)
			throw protocol_error("'unchoke' message size != 1");
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return;

#ifndef NDEBUG
		(*m_logger) << " <== UNCHOKE\n";
#endif
		m_peer_choked = false;
		m_torrent->get_policy().unchoked(*this);
	}

	// -----------------------------
	// -------- INTERESTED ---------
	// -----------------------------

	void peer_connection::on_interested(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (m_packet_size != 1)
			throw protocol_error("'interested' message size != 1");
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return;

#ifndef NDEBUG
		(*m_logger) << " <== INTERESTED\n";
#endif
		m_peer_interested = true;
		m_torrent->get_policy().interested(*this);
	}

	// -----------------------------
	// ------ NOT INTERESTED -------
	// -----------------------------

	void peer_connection::on_not_interested(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (m_packet_size != 1)
			throw protocol_error("'not interested' message size != 1");
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return;

		m_became_uninterested = boost::posix_time::second_clock::local_time();

		// clear the request queue if the client isn't interested
		m_requests.clear();
		send_buffer_updated();

#ifndef NDEBUG
		(*m_logger) << " <== NOT_INTERESTED\n";
#endif
		m_peer_interested = false;
		m_torrent->get_policy().not_interested(*this);
	}

	// -----------------------------
	// ----------- HAVE ------------
	// -----------------------------

	void peer_connection::on_have(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (m_packet_size != 5)
			throw protocol_error("'have' message size != 5");
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return;

		const char* ptr = &m_recv_buffer[1];
		int index = detail::read_int32(ptr);
		// if we got an invalid message, abort
		if (index >= (int)m_have_piece.size() || index < 0)
			throw protocol_error("have message with higher index than the number of pieces");

#ifndef NDEBUG
		(*m_logger) << " <== HAVE    [ piece: " << index << "]\n";
#endif

		if (m_have_piece[index])
		{
#ifndef NDEBUG
			(*m_logger) << " oops.. we already knew that: " << index << "\n";
#endif
		}
		else
		{
			m_have_piece[index] = true;
			++m_num_pieces;
			m_torrent->peer_has(index);

			if (!m_torrent->have_piece(index) && !is_interesting())
				m_torrent->get_policy().peer_is_interesting(*this);

			if (m_torrent->is_seed() && is_seed())
			{
				throw protocol_error("seed to seed connection redundant, disconnecting");
			}
		}
	}

	// -----------------------------
	// --------- BITFIELD ----------
	// -----------------------------

	void peer_connection::on_bitfield(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (m_packet_size - 1 != ((int)m_have_piece.size() + 7) / 8)
			throw protocol_error("bitfield with invalid size");
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return;

#ifndef NDEBUG
		(*m_logger) << " <== BITFIELD\n";
#endif
		// build a vector of all pieces
		std::vector<int> piece_list;
		for (int i = 0; i < (int)m_have_piece.size(); ++i)
		{
			bool have = (m_recv_buffer[1 + (i>>3)] & (1 << (7 - (i&7)))) != 0;
			if (have && !m_have_piece[i])
			{
				m_have_piece[i] = true;
				++m_num_pieces;
				piece_list.push_back(i);
			}
			else if (!have && m_have_piece[i])
			{
				m_have_piece[i] = false;
				--m_num_pieces;
				m_torrent->peer_lost(i);
			}
		}

		// shuffle the piece list
		std::random_shuffle(piece_list.begin(), piece_list.end());

		// let the torrent know which pieces the
		// peer has, in a shuffled order
		bool interesting = false;
		for (std::vector<int>::iterator i = piece_list.begin();
			i != piece_list.end();
			++i)
		{
			int index = *i;
			m_torrent->peer_has(index);
			if (!m_torrent->have_piece(index))
				interesting = true;
		}

		if (piece_list.size() == m_have_piece.size())
		{
#ifndef NDEBUG
			(*m_logger) << " *** THIS IS A SEED ***\n";
#endif
			// if we're a seed too, disconnect
			if (m_torrent->is_seed())
			{
#ifndef NDEBUG
				(*m_logger) << " we're also a seed, disconnecting\n";
#endif
				throw protocol_error("seed to seed connection redundant, disconnecting");
			}
		}

		if (interesting) m_torrent->get_policy().peer_is_interesting(*this);
	}

	// -----------------------------
	// ---------- REQUEST ----------
	// -----------------------------

	void peer_connection::on_request(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (m_packet_size != 13)
			throw protocol_error("'request' message size != 13");
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return;

		peer_request r;
		const char* ptr = &m_recv_buffer[1];
		r.piece = detail::read_int32(ptr);
		r.start = detail::read_int32(ptr);
		r.length = detail::read_int32(ptr);

		if (m_requests.size() > 40)
		{
			// don't allow clients to abuse our
			// memory consumption.
			// ignore requests if the client
			// is making too many of them.
			return;
		}

		// make sure this request
		// is legal and taht the peer
		// is not choked
		if (r.piece >= 0
			&& r.piece < m_torrent->torrent_file().num_pieces()
			&& m_torrent->have_piece(r.piece)
			&& r.start >= 0
			&& r.start < m_torrent->torrent_file().piece_size(r.piece)
			&& r.length > 0
			&& r.length + r.start <= m_torrent->torrent_file().piece_size(r.piece)
			&& m_peer_interested)
		{
			// if we have choked the client
			// ignore the request
			if (m_choked)
				return;

			m_requests.push_back(r);
			send_buffer_updated();
#ifndef NDEBUG
			(*m_logger) << " <== REQUEST [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif
		}
		else
		{
#ifndef NDEBUG
			(*m_logger) << " <== INVALID_REQUEST [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " | "
				"i: " << m_peer_interested << " | "
				"t: " << (int)m_torrent->torrent_file().piece_size(r.piece) << " | "
				"n: " << m_torrent->torrent_file().num_pieces() << " ]\n";
#endif

			++m_num_invalid_requests;

			if (m_torrent->alerts().should_post(alert::debug))
			{
				m_torrent->alerts().post_alert(invalid_request_alert(
					r
					, m_torrent->get_handle()
					, m_socket->sender()
					, "peer sent an illegal request, ignoring"));
			}
		}
	}

	// -----------------------------
	// ----------- PIECE -----------
	// -----------------------------

	void peer_connection::on_piece(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (m_recv_pos - received <= 9)
		{
			m_last_piece = boost::posix_time::second_clock::local_time();
		}
		// classify the received data as protocol chatter
		// or data payload for the statistics
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

		if (m_recv_pos < m_packet_size) return;

		const char* ptr = &m_recv_buffer[1];
		peer_request p;
		p.piece = detail::read_int32(ptr);
		p.start = detail::read_int32(ptr);
		p.length = m_packet_size - 9;

		if (!verify_piece(p))
		{
#ifndef NDEBUG
			(*m_logger) << " <== INVALID_PIECE [ piece: " << p.piece << " | "
				"start: " << p.start << " | "
				"length: " << p.length << " ]\n";
#endif
			throw protocol_error("invalid piece packet");
		}

#ifndef NDEBUG
		for (std::deque<piece_block>::iterator i = m_download_queue.begin();
			i != m_download_queue.end();
			++i)
		{
			if (i->piece_index == p.piece
				&& i->block_index == p.start / m_torrent->block_size())
				break;

			(*m_logger) << " *** SKIPPED_PIECE [ piece: " << i->piece_index << " | "
				"b: " << i->block_index << " ] ***\n";
		}
		(*m_logger) << " <== PIECE   [ piece: " << p.piece << " | "
			"b: " << p.start / m_torrent->block_size() << " | "
			"s: " << p.start << " | "
			"l: " << p.length << " ]\n";
#endif

		piece_picker& picker = m_torrent->picker();
		piece_block block_finished(p.piece, p.start / m_torrent->block_size());

		// if the block we got is already finished, then ignore it
		if (picker.is_finished(block_finished)) return;

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
			// cancel the block from the
			// peer that has taken over it.
			boost::optional<address> peer = m_torrent->picker().get_downloader(block_finished);
			if (peer)
			{
				peer_connection* pc = m_torrent->connection_for(*peer);
				if (pc && pc != this)
				{
					pc->send_cancel(block_finished);
				}
			}
			else
			{
				if (m_torrent->alerts().should_post(alert::debug))
				{
					m_torrent->alerts().post_alert(
						peer_error_alert(
						m_socket->sender()
						, "got a block that was not requested"));
				}
#ifndef NDEBUG
				(*m_logger) << " *** The block we just got was not requested ***\n";
#endif
			}
		}

		m_torrent->filesystem().write(&m_recv_buffer[9], p.piece, p.start, p.length);

		bool was_seed = m_torrent->is_seed();

		picker.mark_as_finished(block_finished, m_socket->sender());

		m_torrent->get_policy().block_finished(*this, block_finished);

		// did we just finish the piece?
		if (picker.is_piece_finished(p.piece))
		{
			bool verified = m_torrent->verify_piece(p.piece);
			if (verified)
			{
				m_torrent->announce_piece(p.piece);
			}
			else
			{
				m_torrent->piece_failed(p.piece);
			}
			m_torrent->get_policy().piece_finished(p.piece, verified);

			if (!was_seed && m_torrent->is_seed())
			{
				assert(verified);
				m_torrent->completed();
			}

		}
	}

	// -----------------------------
	// ---------- CANCEL -----------
	// -----------------------------

	void peer_connection::on_cancel(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (m_packet_size != 13)
			throw protocol_error("'cancel' message size != 13");
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return;

		peer_request r;
		const char* ptr = &m_recv_buffer[1];
		r.piece = detail::read_int32(ptr);
		r.start = detail::read_int32(ptr);
		r.length = detail::read_int32(ptr);

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
		(*m_logger) << " <== CANCEL  [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif
	}

	// -----------------------------
	// ------ EXTENSION LIST -------
	// -----------------------------

	void peer_connection::on_extension_list(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (m_packet_size > 100 * 1024)
		{
			// too big extension message, abort
			throw protocol_error("'extensions' message size > 100kB");
		}
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return;

		try
		{
			entry e = bdecode(m_recv_buffer.begin()+1, m_recv_buffer.end());
			entry::dictionary_type& extensions = e.dict();

			for (int i = 0; i < num_supported_extensions; ++i)
			{
				entry::dictionary_type::iterator f =
					extensions.find(extension_names[i]);
				if (f != extensions.end())
				{
					m_extension_messages[i] = (int)f->second.integer();
				}
			}
#ifndef NDEBUG
			(*m_logger) << "supported extensions:\n";
			for (entry::dictionary_type::const_iterator i = extensions.begin();
				i != extensions.end();
				++i)
			{
				(*m_logger) << i->first << "\n";
			}
#endif
		}
		catch(invalid_encoding&)
		{
			throw protocol_error("'extensions' packet contains invalid bencoding");
		}
		catch(type_error&)
		{
			throw protocol_error("'extensions' packet contains incorrect types");
		}
	}

	// -----------------------------
	// --------- EXTENDED ----------
	// -----------------------------

	void peer_connection::on_extended(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		m_statistics.received_bytes(0, received);
		if (m_packet_size < 5)
			throw protocol_error("'extended' message smaller than 5 bytes");

		if (m_torrent == 0)
			throw protocol_error("'extended' message sent before proper handshake");


		if (m_recv_pos < 5) return;

		const char* ptr = &m_recv_buffer[1];

		int extended_id = detail::read_int32(ptr);

		switch (extended_id)
		{
		case extended_chat_message:
			{
				if (m_packet_size > 2 * 1024)
					throw protocol_error("CHAT message larger than 2 kB");
				if (m_recv_pos < m_packet_size) return;
				try
				{
					entry d = bdecode(m_recv_buffer.begin()+5, m_recv_buffer.end());
					entry::dictionary_type::const_iterator i = d.dict().find("msg");
					if (i == d.dict().end())
						throw protocol_error("CHAT message did not contain any 'msg'");

					const std::string& str = i->second.string();

					if (m_torrent->alerts().should_post(alert::critical))
					{
						m_torrent->alerts().post_alert(
							chat_message_alert(
								m_torrent->get_handle()
								, m_socket->sender(), str));
					}

				}
				catch (invalid_encoding&)
				{
					throw protocol_error("invalid bencoding in CHAT message");
				}
				catch (type_error&)
				{
					throw protocol_error("invalid types in bencoded CHAT message");
				}
				return;
			}
		default:
			throw protocol_error("unknown extended message id");

		};
	}









	void peer_connection::disconnect()
	{
		assert(m_disconnecting == false);
		detail::session_impl::connection_map::iterator i = m_ses.m_connections.find(m_socket);
		m_disconnecting = true;
		assert(i != m_ses.m_connections.end());
		assert(std::find(m_ses.m_disconnect_peer.begin(), m_ses.m_disconnect_peer.end(), i) == m_ses.m_disconnect_peer.end());
		m_ses.m_disconnect_peer.push_back(i);
	}

	bool peer_connection::dispatch_message(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		assert(m_recv_pos >= received);
		assert(m_recv_pos > 0);
		assert(m_torrent != 0);

		int packet_type = m_recv_buffer[0];
		if (packet_type < 0
			|| packet_type >= num_supported_messages
			|| m_message_handler[packet_type] == 0)
		{
			throw protocol_error("unknown message id");
		}

		assert(m_message_handler[packet_type] != 0);

		// call the correct handler for this packet type
		(this->*m_message_handler[packet_type])(received);

		if (m_recv_pos < m_packet_size) return false;

		assert(m_recv_pos == m_packet_size);
		return true;
	}

	void peer_connection::send_cancel(piece_block block)
	{
		INVARIANT_CHECK;

		assert(block.piece_index >= 0);
		assert(block.piece_index < m_torrent->torrent_file().num_pieces());
		assert(block.block_index >= 0);
		assert(block.block_index < m_torrent->torrent_file().piece_size(block.piece_index));
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

		char* ptr = &m_send_buffer[start_offset];

		// index
		detail::write_int32(block.piece_index, ptr);
		// begin
		detail::write_int32(block_offset, ptr);
		// length
		detail::write_int32(block_size, ptr);

	#ifndef NDEBUG
		(*m_logger) << " ==> CANCEL  [ piece: " << block.piece_index << " | s: " << block_offset << " | l: " << block_size << " | " << block.block_index << " ]\n";
	#endif

		send_buffer_updated();
	}

	void peer_connection::send_request(piece_block block)
	{
		INVARIANT_CHECK;

		assert(block.piece_index >= 0);
		assert(block.piece_index < m_torrent->torrent_file().num_pieces());
		assert(block.block_index >= 0);
		assert(block.block_index < m_torrent->torrent_file().piece_size(block.piece_index));
		assert(!m_torrent->picker().is_downloading(block));

		m_torrent->picker().mark_as_downloading(block, m_socket->sender());

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

		char* ptr = &m_send_buffer[start_offset+5];
		// index
		detail::write_int32(block.piece_index, ptr);

		// begin
		detail::write_int32(block_offset, ptr);

		// length
		detail::write_int32(block_size, ptr);

	#ifndef NDEBUG
		(*m_logger) << " ==> REQUEST [ "
			"piece: " << block.piece_index << " | "
			"b: " << block.block_index << " | "
			"s: " << block_offset << " | "
			"l: " << block_size << " ]\n";

		peer_request r;
		r.piece = block.piece_index;
		r.start = block_offset;
		r.length = block_size;
		assert(verify_piece(r));
	#endif

		send_buffer_updated();
	}

	void peer_connection::send_chat_message(const std::string& msg)
	{
		INVARIANT_CHECK;

		assert(msg.length() <= 1 * 1024);
		if (m_extension_messages[extended_chat_message] == -1) return;

		entry e(entry::dictionary_t);
		e.dict()["msg"] = msg;

		std::vector<char> message;
		bencode(std::back_inserter(message), e);
		std::back_insert_iterator<std::vector<char> > ptr(m_send_buffer);

		detail::write_uint32(1 + 4 + (int)message.size(), ptr);
		detail::write_uint8(msg_extended, ptr);
		detail::write_int32(m_extension_messages[extended_chat_message], ptr);
		std::copy(message.begin(), message.end(), ptr);
		send_buffer_updated();
	}

	void peer_connection::send_bitfield()
	{
		INVARIANT_CHECK;

	#ifndef NDEBUG
		(*m_logger) << " ==> BITFIELD\n";
	#endif
		const int packet_size = ((int)m_have_piece.size() + 7) / 8 + 5;
		const int old_size = (int)m_send_buffer.size();
		m_send_buffer.resize(old_size + packet_size);

		char* ptr = &m_send_buffer[old_size];
		detail::write_int32(packet_size - 4, ptr);
		detail::write_uint8(msg_bitfield, ptr);

		std::fill(m_send_buffer.begin() + old_size + 5, m_send_buffer.end(), 0);
		for (int i = 0; i < (int)m_have_piece.size(); ++i)
		{
			if (m_torrent->have_piece(i))
				m_send_buffer[old_size + 5 + (i>>3)] |= 1 << (7 - (i&7));
		}
		send_buffer_updated();
	}

	void peer_connection::send_extensions()
	{
		INVARIANT_CHECK;

	#ifndef NDEBUG
		(*m_logger) << " ==> EXTENSIONS\n";
	#endif
		assert(m_supports_extensions);

		entry extension_list(entry::dictionary_t);

		for (int i = 0; i < num_supported_extensions; ++i)
		{
			extension_list.dict()[extension_names[i]] = i;
		}

		// make room for message size
		const int msg_size_pos = (int)m_send_buffer.size();
		m_send_buffer.resize(msg_size_pos + 4);

		m_send_buffer.push_back(msg_extension_list);

		bencode(std::back_inserter(m_send_buffer), extension_list);

		// write the length of the message
		char* ptr = &m_send_buffer[msg_size_pos];
		detail::write_int32((int)m_send_buffer.size() - msg_size_pos - 4, ptr);

		send_buffer_updated();
	}

	void peer_connection::send_choke()
	{
		INVARIANT_CHECK;

		if (m_choked) return;
		char msg[] = {0,0,0,1,msg_choke};
		m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
		m_choked = true;
	#ifndef NDEBUG
		(*m_logger) << " ==> CHOKE\n";
	#endif
		m_num_invalid_requests = 0;
		m_requests.clear();
		send_buffer_updated();
	}

	void peer_connection::send_unchoke()
	{
		INVARIANT_CHECK;

		if (!m_choked) return;
		char msg[] = {0,0,0,1,msg_unchoke};
		m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
		m_choked = false;
	#ifndef NDEBUG
		(*m_logger) << " ==> UNCHOKE\n";
	#endif
		send_buffer_updated();
	}

	void peer_connection::send_interested()
	{
		INVARIANT_CHECK;

		if (m_interesting) return;
		char msg[] = {0,0,0,1,msg_interested};
		m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
		m_interesting = true;
	#ifndef NDEBUG
		(*m_logger) << " ==> INTERESTED\n";
	#endif
		send_buffer_updated();
	}

	void peer_connection::send_not_interested()
	{
		INVARIANT_CHECK;

		if (!m_interesting) return;
		char msg[] = {0,0,0,1,msg_not_interested};
		m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
		m_interesting = false;

		m_became_uninteresting = boost::posix_time::second_clock::local_time();

	#ifndef NDEBUG
		(*m_logger) << " ==> NOT_INTERESTED\n";
	#endif
		send_buffer_updated();
	}

	void peer_connection::send_have(int index)
	{
		assert(index >= 0);
		assert(index < m_torrent->torrent_file().num_pieces());
		INVARIANT_CHECK;

		// optimization, don't send have messages
		// to peers that already have the piece
		if (m_have_piece[index]) return;

		const int packet_size = 9;
		char msg[packet_size] = {0,0,0,5,msg_have};
		char* ptr = msg+5;
		detail::write_int32(index, ptr);
		m_send_buffer.insert(m_send_buffer.end(), msg, msg + packet_size);
	#ifndef NDEBUG
		(*m_logger) << " ==> HAVE    [ piece: " << index << " ]\n";
	#endif
		send_buffer_updated();
	}

	size_type peer_connection::share_diff() const
	{
		float ratio = m_torrent->ratio();

		// if we have an infinite ratio, just say we have downloaded
		// much more than we have uploaded. And we'll keep uploading.
		if (ratio == 0.f) return std::numeric_limits<int>::max();

		return m_free_upload
			+ static_cast<size_type>(m_statistics.total_payload_download() * ratio)
			- m_statistics.total_payload_upload();
	}

	void peer_connection::second_tick()
	{
		INVARIANT_CHECK;

		m_statistics.second_tick();

		update_send_quota_left();
		
		// If the client sends more data
		// we send it data faster, otherwise, slower.
		// It will also depend on how much data the
		// client has sent us. This is the mean to
		// maintain the share ratio given by m_ratio
		// with all peers.

		if (m_torrent->is_seed() || is_choked() || m_torrent->ratio()==0.0f)
		{
			// if we have downloaded more than one piece more
			// than we have uploaded OR if we are a seed
			// have an unlimited upload rate
			upload_bandwidth.wanted = std::numeric_limits<int>::max();
		}
		else
		{
			double bias = 0x20000 + m_free_upload;

			double break_even_time = 10;
			double have_uploaded = (double)m_statistics.total_payload_upload();
			double have_downloaded = (double)m_statistics.total_payload_download();
			double download_speed = m_statistics.download_rate();

			double soon_downloaded =
				have_downloaded+download_speed * 2 * break_even_time;

			double upload_speed_limit = (soon_downloaded*m_torrent->ratio()
				                   - have_uploaded + bias) / break_even_time;
			upload_speed_limit=std::max(upload_speed_limit,1.0);
			upload_speed_limit=std::min(upload_speed_limit,
				                        (double)std::numeric_limits<int>::max());

			upload_bandwidth.wanted = (int) upload_speed_limit;
		}

/*
		size_type diff = share_diff();

		enum { block_limit = 2 }; // how many blocks difference is considered unfair

		// if the peer has been choked, send the current piece
		// as fast as possible
		if (diff > block_limit*m_torrent->block_size() || m_torrent->is_seed() || is_choked())
		{
			// if we have downloaded more than one piece more
			// than we have uploaded OR if we are a seed
			// have an unlimited upload rate
			upload_bandwidth.wanted = std::numeric_limits<int>::max();
		}
		else
		{
			float ratio = m_torrent->ratio();
			// if we have downloaded too much, response with an
			// upload rate of 10 kB/s more than we dowlload
			// if we have uploaded too much, send with a rate of
			// 10 kB/s less than we receive
			int bias = 0;
			if (diff > -block_limit*m_torrent->block_size())
			{
				bias = static_cast<int>(m_statistics.download_rate() * ratio) / 2;
				if (bias < 10*1024) bias = 10*1024;
			}
			else
			{
				bias = -static_cast<int>(m_statistics.download_rate() * ratio) / 2;
			}
			upload_bandwidth.wanted = static_cast<int>(m_statistics.download_rate()) + bias;

			// the maximum send_quota given our download rate from this peer
			if (upload_bandwidth.wanted < 256) upload_bandwidth.wanted = 256;
		}
*/
	}

	// --------------------------
	// RECEIVE DATA
	// --------------------------

	// throws exception when the client should be disconnected
	void peer_connection::receive_data()
	{
		INVARIANT_CHECK;

		assert(!m_socket->is_blocking());
		assert(m_packet_size > 0);
		assert(m_socket->is_readable());
		for(;;)
		{
			assert(m_packet_size > 0);
			int received = m_socket->receive(&m_recv_buffer[m_recv_pos], m_packet_size - m_recv_pos);

			// connection closed
			if (received == 0)
			{
				throw protocol_error("connection closed by remote host");
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
					(*m_logger) << " protocol length: " << m_packet_size << "\n";
	#endif
					m_state = read_protocol_string;
					m_recv_buffer.resize(m_packet_size);
					m_recv_pos = 0;

					if (m_packet_size != 19)
					{
	#ifndef NDEBUG
							(*m_logger) << "incorrect protocol length\n";
	#endif
							std::stringstream s;
							s << "received incorrect protocol length ("
								<< m_packet_size
								<< ") should be 19.";
							throw protocol_error(s.str());
					}
					break;


				case read_protocol_string:
					{
						m_statistics.received_bytes(0, received);
						if (m_recv_pos < m_packet_size) break;
						assert(m_recv_pos == m_packet_size);
	#ifndef NDEBUG
						(*m_logger) << " protocol: '" << std::string(m_recv_buffer.begin(), m_recv_buffer.end()) << "'\n";
	#endif
						const char protocol_string[] = "BitTorrent protocol";
						if (!std::equal(m_recv_buffer.begin(), m_recv_buffer.end(), protocol_string))
						{
	#ifndef NDEBUG
							(*m_logger) << "incorrect protocol name\n";
	#endif
							std::stringstream s;
							s << "got invalid protocol name: '"
								<< std::string(m_recv_buffer.begin(), m_recv_buffer.end())
								<< "'";
							throw protocol_error(s.str());
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

					// TODO: if the protocol is to be extended
					// these 8 bytes would be used to describe the
					// extensions available on the other side
					// currently disabled
//					if (m_recv_buffer[0] & 0x80)
//					{
//						m_supports_extensions = true;
//					}

					if (m_torrent == 0)
					{

						// now, we have to see if there's a torrent with the
						// info_hash we got from the peer
						sha1_hash info_hash;
						std::copy(m_recv_buffer.begin()+8, m_recv_buffer.begin() + 28, (char*)info_hash.begin());
						
						m_torrent = m_ses.find_torrent(info_hash);
						if (m_torrent == 0)
						{
							// we couldn't find the torrent!
	#ifndef NDEBUG
							(*m_logger) << " couldn't find a torrent with the given info_hash\n";
	#endif
							throw protocol_error("got info-hash that is not in our session");
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
							(*m_logger) << " received invalid info_hash\n";
	#endif
							throw protocol_error("invalid info-hash in handshake");
						}
					}

					if (m_supports_extensions) send_extensions();

					m_state = read_peer_id;
					m_packet_size = 20;
					m_recv_pos = 0;
					m_recv_buffer.resize(20);
	#ifndef NDEBUG
					(*m_logger) << " info_hash received\n";
	#endif
					break;
				}


				case read_peer_id:
				{
					m_statistics.received_bytes(0, received);
					if (m_recv_pos < m_packet_size) break;
					assert(m_recv_pos == m_packet_size);

	#ifndef NDEBUG
					{
						peer_id tmp;
						std::copy(m_recv_buffer.begin(), m_recv_buffer.begin() + 20, (char*)tmp.begin());
						std::stringstream s;
						s << "received peer_id: " << tmp << " client: " << identify_client(tmp) << "\n";
						(*m_logger) << s.str();
					}
	#endif

					if (!m_active)
					{
						// check to make sure we don't have another connection with the same
						// info_hash and peer_id. If we do. close this connection.
						std::copy(m_recv_buffer.begin(), m_recv_buffer.begin() + 20, (char*)m_peer_id.begin());

						m_attached_to_torrent = true;
						m_torrent->attach_peer(this);
						assert(m_torrent->get_policy().has_connection(this));
					}

					m_state = read_packet_size;
					m_packet_size = 4;
					m_recv_pos = 0;
					m_recv_buffer.resize(4);

					break;
				}


				case read_packet_size:
				{
					m_statistics.received_bytes(0, received);
					if (m_recv_pos < m_packet_size) break;
					assert(m_recv_pos == m_packet_size);

					// convert from big endian to native byte order
					const char* ptr = &m_recv_buffer[0];
					m_packet_size = detail::read_int32(ptr);
					// don't accept packets larger than 1 MB
					if (m_packet_size > 1024*1024 || m_packet_size < 0)
					{
	#ifndef NDEBUG
						(*m_logger) << " packet too large (packet_size > 1 Megabyte), abort\n";
	#endif
						// packet too large
						throw protocol_error("packet > 1 MB");
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
				}
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


	bool peer_connection::has_data() const
	{
		// if we have requests or pending data to be sent or announcements to be made
		// we want to send data
		return ((!m_requests.empty() && !m_choked)
			|| !m_send_buffer.empty())
			&& m_send_quota_left != 0;
	}

	// --------------------------
	// SEND DATA
	// --------------------------

	// throws exception when the client should be disconnected
	void peer_connection::send_data()
	{
		INVARIANT_CHECK;

		assert(m_socket->is_writable());
		assert(has_data());

		// only add new piece-chunks if the send buffer is small enough
		// otherwise there will be no end to how large it will be!
		while (!m_requests.empty()
			&& ((int)m_send_buffer.size() < m_torrent->block_size())
			&& !m_choked)
		{
			peer_request& r = m_requests.front();
			
			assert(r.piece >= 0);
			assert(r.piece < (int)m_have_piece.size());
			assert(m_torrent != 0);
			assert(m_torrent->have_piece(r.piece));
			assert(r.start + r.length <= m_torrent->torrent_file().piece_size(r.piece));
			assert(r.length > 0 && r.start >= 0);

#ifndef NDEBUG
//			assert(m_torrent->verify_piece(r.piece) && "internal error");
#endif
			const int send_buffer_offset = (int)m_send_buffer.size();
			const int packet_size = 4 + 5 + 4 + r.length;
			m_send_buffer.resize(send_buffer_offset + packet_size);
			char* ptr = &m_send_buffer[send_buffer_offset];
			detail::write_int32(packet_size-4, ptr);
			*ptr = msg_piece; ++ptr;
			detail::write_int32(r.piece, ptr);
			detail::write_int32(r.start, ptr);

			m_torrent->filesystem().read(
				&m_send_buffer[send_buffer_offset+13]
				, r.piece
				, r.start
				, r.length);
#ifndef NDEBUG
			(*m_logger) << " ==> PIECE   [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif

			m_payloads.push_back(range(send_buffer_offset+13, r.length));
			m_requests.erase(m_requests.begin());

			if (m_requests.empty()
				&& m_num_invalid_requests > 0
				&& is_peer_interested()
				&& !is_seed())
			{
				// this will make the peer clear
				// its download queue and re-request
				// pieces. Hopefully it will not
				// send invalid requests then
				send_choke();
				send_unchoke();
			}
		}

		if (!m_announce_queue.empty())
		{
			for (std::vector<int>::iterator i = m_announce_queue.begin();
				i != m_announce_queue.end();
				++i)
			{
				send_have(*i);
			}
			m_announce_queue.clear();
		}

		assert(m_send_quota_left != 0);

		// send the actual buffer
		if (!m_send_buffer.empty())
		{

			int amount_to_send = (int)m_send_buffer.size();
			assert(m_send_quota_left != 0);
			if (m_send_quota_left > 0)
				amount_to_send = std::min(m_send_quota_left, amount_to_send);


			// we have data that's scheduled for sending
			int sent = m_socket->send(
				&m_send_buffer[0]
				, amount_to_send);

			if (sent > 0)
			{
				if (m_send_quota_left != -1)
				{
					assert(m_send_quota_left >= sent);
					m_send_quota_left -= sent;
				}

				// manage the payload markers
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
				if (sent == (int)m_send_buffer.size())
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
	}

#ifndef NDEBUG
	void peer_connection::check_invariant() const
	{
		assert(has_data() == m_selector.is_writability_monitored(m_socket));
/*
		assert(m_num_pieces == std::count(
			m_have_piece.begin()
			, m_have_piece.end()
			, true));
*/	}
#endif

	bool peer_connection::has_timed_out() const
	{
		using namespace boost::posix_time;

		// if the peer hasn't said a thing for a certain
		// time, it is considered to have timed out
		time_duration d;
		d = second_clock::local_time() - m_last_receive;
		if (d > seconds(m_timeout)) return true;

		// if the peer hasn't become interested and we haven't
		// become interested in the peer for 60 seconds, it
		// has also timed out.
		time_duration d1;
		time_duration d2;
		d1 = second_clock::local_time() - m_became_uninterested;
		d2 = second_clock::local_time() - m_became_uninteresting;
		if (!m_interesting
			&& !m_peer_interested
			&& d1 > seconds(60)
			&& d2 > seconds(60))
		{
			return true;
		}
		return false;
	}


	void peer_connection::keep_alive()
	{
		INVARIANT_CHECK;

		boost::posix_time::time_duration d;
		d = boost::posix_time::second_clock::local_time() - m_last_sent;
		if (d.seconds() < m_timeout / 2) return;

		// we must either send a keep-alive
		// message or something else.
		if (m_announce_queue.empty())
		{
			char noop[] = {0,0,0,0};
			m_send_buffer.insert(m_send_buffer.end(), noop, noop+4);
			m_last_sent = boost::posix_time::second_clock::local_time();
	#ifndef NDEBUG
			(*m_logger) << " ==> NOP\n";
	#endif
		}
		else
		{
			for (std::vector<int>::iterator i = m_announce_queue.begin();
				i != m_announce_queue.end();
				++i)
			{
				send_have(*i);
			}
			m_announce_queue.clear();
		}
		send_buffer_updated();
	}

	bool peer_connection::is_seed() const
	{
		return m_num_pieces == (int)m_have_piece.size();
	}
}
