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

#include <vector>
#include <iostream>
#include <iomanip>
#include <limits>

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/version.hpp"

using namespace boost::posix_time;

namespace libtorrent
{

	// the names of the extensions to look for in
	// the extensions-message
	const char* peer_connection::extension_names[] =
	{ "chat", "metadata", "peer_exchange", "listen_port" };

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
		:
#ifndef NDEBUG
		m_last_choke(boost::posix_time::second_clock::universal_time()
			- hours(1))
		, 
#endif
		  m_state(read_protocol_length)
		, m_timeout(120)
		, m_packet_size(1)
		, m_recv_pos(0)
		, m_last_receive(second_clock::universal_time())
		, m_last_sent(second_clock::universal_time())
		, m_selector(sel)
		, m_socket(s)
		, m_torrent(t)
		, m_attached_to_torrent(true)
		, m_ses(ses)
		, m_active(true)
		, m_writability_monitored(false)
		, m_readability_monitored(true)
		, m_peer_interested(false)
		, m_peer_choked(true)
		, m_interesting(false)
		, m_choked(true)
		, m_failed(false)
		, m_supports_extensions(false)
		, m_num_pieces(0)
		, m_free_upload(0)
		, m_trust_points(0)
		, m_num_invalid_requests(0)
		, m_last_piece(second_clock::universal_time())
		, m_disconnecting(false)
		, m_became_uninterested(second_clock::universal_time())
		, m_became_uninteresting(second_clock::universal_time())
		, m_no_metadata(
			boost::gregorian::date(1970, boost::date_time::Jan, 1)
			, boost::posix_time::seconds(0))
		, m_metadata_request(
			boost::gregorian::date(1970, boost::date_time::Jan, 1)
			, boost::posix_time::seconds(0))
		, m_waiting_metadata_request(false)
		, m_connecting(false)
		, m_metadata_progress(0)
	{
		INVARIANT_CHECK;

		// these numbers are used the first second of connection.
		// then the given upload limits will be applied by running
		// allocate_resources().

		m_ul_bandwidth_quota.min = 10;
		m_ul_bandwidth_quota.max = resource_request::inf;

		if (m_torrent->m_ul_bandwidth_quota.given == resource_request::inf)
		{
			m_ul_bandwidth_quota.given = resource_request::inf;
		}
		else
		{
			// just enough to get started with the handshake and bitmask
			m_ul_bandwidth_quota.given = 400;
		}

		m_dl_bandwidth_quota.min = 10;
		m_dl_bandwidth_quota.max = resource_request::inf;
	
		if (m_torrent->m_dl_bandwidth_quota.given == resource_request::inf)
		{
			m_dl_bandwidth_quota.given = resource_request::inf;
		}
		else
		{
			// just enough to get started with the handshake and bitmask
			m_dl_bandwidth_quota.given = 400;
		}

		assert(!m_socket->is_blocking());
		assert(m_torrent != 0);

#ifdef TORRENT_VERBOSE_LOGGING
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
		if (m_torrent->valid_metadata())
		{
			init();
			send_bitfield();
		}
	}

	peer_connection::peer_connection(
		detail::session_impl& ses
		, selector& sel
		, boost::shared_ptr<libtorrent::socket> s)
		:
#ifndef NDEBUG
		m_last_choke(boost::posix_time::second_clock::universal_time()
			- hours(1))
		, 
#endif
		  m_state(read_protocol_length)
		, m_timeout(120)
		, m_packet_size(1)
		, m_recv_pos(0)
		, m_last_receive(second_clock::universal_time())
		, m_last_sent(second_clock::universal_time())
		, m_selector(sel)
		, m_socket(s)
		, m_torrent(0)
		, m_attached_to_torrent(0)
		, m_ses(ses)
		, m_active(false)
		, m_writability_monitored(false)
		, m_readability_monitored(true)
		, m_peer_id()
		, m_peer_interested(false)
		, m_peer_choked(true)
		, m_interesting(false)
		, m_choked(true)
		, m_failed(false)
		, m_supports_extensions(false)
		, m_num_pieces(0)
		, m_free_upload(0)
		, m_trust_points(0)
		, m_num_invalid_requests(0)
		, m_last_piece(second_clock::universal_time())
		, m_disconnecting(false)
		, m_became_uninterested(second_clock::universal_time())
		, m_became_uninteresting(second_clock::universal_time())
		, m_no_metadata(
			boost::gregorian::date(1970, boost::date_time::Jan, 1)
			, boost::posix_time::seconds(0))
		, m_metadata_request(
			boost::gregorian::date(1970, boost::date_time::Jan, 1)
			, boost::posix_time::seconds(0))
		, m_waiting_metadata_request(false)
		, m_connecting(true)
		, m_metadata_progress(0)
	{
		INVARIANT_CHECK;

		// upload bandwidth will only be given to connections
		// that are part of a torrent. Since this is an incoming
		// connection, we have to give it some initial bandwidth
		// to send the handshake.
		// after one second, allocate_resources() will be called
		// and the correct bandwidth limits will be set on all
		// connections.

		m_ul_bandwidth_quota.min = 10;
		m_ul_bandwidth_quota.max = resource_request::inf;

		if (m_ses.m_upload_rate == -1)
		{
			m_ul_bandwidth_quota.given = resource_request::inf;
		}
		else
		{
			// just enough to get started with the handshake and bitmask
			m_ul_bandwidth_quota.given = 400;
		}

		m_dl_bandwidth_quota.min = 10;
		m_dl_bandwidth_quota.max = resource_request::inf;
	
		if (m_ses.m_download_rate == -1)
		{
			m_dl_bandwidth_quota.given = resource_request::inf;
		}
		else
		{
			// just enough to get started with the handshake and bitmask
			m_dl_bandwidth_quota.given = 400;
		}

		assert(!m_socket->is_blocking());

		std::fill(m_peer_id.begin(), m_peer_id.end(), 0);

#ifdef TORRENT_VERBOSE_LOGGING
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

	void peer_connection::init()
	{
		assert(m_torrent);
		assert(m_torrent->valid_metadata());

		m_have_piece.resize(m_torrent->torrent_file().num_pieces(), false);

		// now that we have a piece_picker,
		// update it with this peers pieces

		// build a vector of all pieces
		std::vector<int> piece_list;
		for (int i = 0; i < (int)m_have_piece.size(); ++i)
		{
			if (m_have_piece[i])
			{
				++m_num_pieces;
				piece_list.push_back(i);
			}
		}

		// shuffle the piece list
		std::random_shuffle(piece_list.begin(), piece_list.end());

		// let the torrent know which pieces the
		// peer has, in a shuffled order
		bool interesting = false;
		for (std::vector<int>::iterator i = piece_list.begin();
			i != piece_list.end(); ++i)
		{
			int index = *i;
			m_torrent->peer_has(index);
			if (!m_torrent->have_piece(index)
				&& !m_torrent->picker().is_filtered(index))
				interesting = true;
		}

		if (piece_list.size() == m_have_piece.size())
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " *** THIS IS A SEED ***\n";
#endif
			// if we're a seed too, disconnect
			if (m_torrent->is_seed())
			{
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << " we're also a seed, disconnecting\n";
#endif
				throw protocol_error("seed to seed connection redundant, disconnecting");
			}
		}

		if (interesting)
			m_torrent->get_policy().peer_is_interesting(*this);

	}

	peer_connection::~peer_connection()
	{
#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		if (m_logger)
		{
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " *** CONNECTION CLOSED\n";
		}
#endif

		m_selector.remove(m_socket);
		if (m_attached_to_torrent)
		{
			assert(m_torrent != 0);
			m_torrent->remove_peer(this);
		}
	}

	void peer_connection::announce_piece(int index)
	{
		assert(m_torrent);
		assert(m_torrent->valid_metadata());
		assert(index >= 0 && index < m_torrent->torrent_file().num_pieces());
		m_announce_queue.push_back(index);
	}

	bool peer_connection::has_piece(int i) const
	{
		assert(m_torrent);
		assert(m_torrent->valid_metadata());
		assert(i >= 0);
		assert(i < m_torrent->torrent_file().num_pieces());
		return m_have_piece[i];
	}

	const std::deque<piece_block>& peer_connection::download_queue() const
	{
		return m_download_queue;
	}
	
	const std::deque<peer_request>& peer_connection::upload_queue() const
	{
		return m_requests;
	}

	void peer_connection::add_stat(size_type downloaded, size_type uploaded)
	{
		m_statistics.add_stat(downloaded, uploaded);
	}

	const std::vector<bool>& peer_connection::get_bitfield() const
	{
		return m_have_piece;
	}

	void peer_connection::received_valid_data()
	{
		m_trust_points++;
		if (m_trust_points > 20) m_trust_points = 20;
	}

	void peer_connection::received_invalid_data()
	{
		// we decrease more than we increase, to keep the
		// allowed failed/passed ratio low.
		m_trust_points -= 2;
		if (m_trust_points < -7) m_trust_points = -7;
	}
	
	int peer_connection::trust_points() const
	{
		return m_trust_points;
	}

	size_type peer_connection::total_free_upload() const
	{
		return m_free_upload;
	}

	void peer_connection::add_free_upload(size_type free_upload)
	{
		m_free_upload += free_upload;
	}

	void peer_connection::reset_upload_quota()
	{
		m_ul_bandwidth_quota.used = 0;
		m_dl_bandwidth_quota.used = 0;
		if (!m_readability_monitored)
		{
			assert(!m_selector.is_readability_monitored(m_socket));
			m_selector.monitor_readability(m_socket);
			m_readability_monitored = true;
		}
		send_buffer_updated();
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

		if (m_ses.extensions_enabled())
			m_send_buffer[pos+7] = 0x01;
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

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> HANDSHAKE\n";
#endif

		send_buffer_updated();
	}

	// verifies a piece to see if it is valid (is within a valid range)
	// and if it can correspond to a request generated by libtorrent.
	bool peer_connection::verify_piece(const peer_request& p) const
	{
		assert(m_torrent->valid_metadata());

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

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== CHOKE\n";
#endif
		m_peer_choked = true;
		m_torrent->get_policy().choked(*this);

		// remove all pieces from this peers download queue and
		// remove the 'downloading' flag from piece_picker.
		for (std::deque<piece_block>::iterator i = m_download_queue.begin();
			i != m_download_queue.end(); ++i)
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

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== UNCHOKE\n";
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

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== INTERESTED\n";
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

		m_became_uninterested = second_clock::universal_time();

		// clear the request queue if the client isn't interested
		m_requests.clear();
		send_buffer_updated();

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== NOT_INTERESTED\n";
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

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== HAVE    [ piece: " << index << "]\n";
#endif

		if (m_have_piece[index])
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " got redundant HAVE message for index: " << index << "\n";
#endif
		}
		else
		{
			m_have_piece[index] = true;

			// only update the piece_picker if
			// we have the metadata
			if (m_torrent->valid_metadata())
			{
				++m_num_pieces;
				m_torrent->peer_has(index);

				if (!m_torrent->have_piece(index)
					&& !is_interesting()
					&& !m_torrent->picker().is_filtered(index))
					m_torrent->get_policy().peer_is_interesting(*this);
			}

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
		assert(m_torrent);
		// if we don't have the metedata, we cannot
		// verify the bitfield size
		if (m_torrent->valid_metadata()
			&& m_packet_size - 1 != ((int)m_have_piece.size() + 7) / 8)
			throw protocol_error("bitfield with invalid size");

		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== BITFIELD\n";
#endif

		// if we don't have metadata yet
		// just remember the bitmask
		// don't update the piecepicker
		// (since it doesn't exist yet)
		if (!m_torrent->valid_metadata())
		{
			m_have_piece.resize((m_packet_size - 1) * 8, false);
			for (int i = 0; i < (int)m_have_piece.size(); ++i)
				m_have_piece[i] = (m_recv_buffer[1 + (i>>3)] & (1 << (7 - (i&7)))) != 0;
			return;
		}

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
				// this should probably not be allowed
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
			i != piece_list.end(); ++i)
		{
			int index = *i;
			m_torrent->peer_has(index);
			if (!m_torrent->have_piece(index)
				&& !m_torrent->picker().is_filtered(index))
				interesting = true;
		}

		if (piece_list.size() == m_have_piece.size())
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " *** THIS IS A SEED ***\n";
#endif
			// if we're a seed too, disconnect
			if (m_torrent->is_seed())
			{
#ifdef TORRENT_VERBOSE_LOGGING
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

		if (!m_torrent->valid_metadata())
		{
			// if we don't have valid metadata yet,
			// we shouldn't get a request
#ifdef TORRENT_VERBOSE_LOGGING
			using namespace boost::posix_time;
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " <== UNEXPECTED_REQUEST [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " | "
				"i: " << m_peer_interested << " | "
				"t: " << (int)m_torrent->torrent_file().piece_size(r.piece) << " | "
				"n: " << m_torrent->torrent_file().num_pieces() << " ]\n";
#endif
			return;
		}

		if (m_requests.size() > 100)
		{
			// don't allow clients to abuse our
			// memory consumption.
			// ignore requests if the client
			// is making too many of them.
#ifdef TORRENT_VERBOSE_LOGGING
			using namespace boost::posix_time;
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " <== TOO MANY REQUESTS [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " | "
				"i: " << m_peer_interested << " | "
				"t: " << (int)m_torrent->torrent_file().piece_size(r.piece) << " | "
				"n: " << m_torrent->torrent_file().num_pieces() << " ]\n";
#endif
			return;
		}

		// make sure this request
		// is legal and that the peer
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
#ifdef TORRENT_VERBOSE_LOGGING
			using namespace boost::posix_time;
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " <== REQUEST [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif
		}
		else
		{
#ifdef TORRENT_VERBOSE_LOGGING
			using namespace boost::posix_time;
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " <== INVALID_REQUEST [ "
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
					, m_peer_id
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
			m_last_piece = second_clock::universal_time();
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
#ifdef TORRENT_VERBOSE_LOGGING
			using namespace boost::posix_time;
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " <== INVALID_PIECE [ piece: " << p.piece << " | "
				"start: " << p.start << " | "
				"length: " << p.length << " ]\n";
#endif
			throw protocol_error("invalid piece packet");
		}

		using namespace boost::posix_time;

		piece_picker& picker = m_torrent->picker();

		piece_block block_finished(p.piece, p.start / m_torrent->block_size());
		std::deque<piece_block>::iterator b
			= std::find(
				m_download_queue.begin()
				, m_download_queue.end()
				, block_finished);


		std::deque<piece_block>::iterator i;

		if (b != m_download_queue.end())
		{
			for (i = m_download_queue.begin();
				i != b; ++i)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << to_simple_string(second_clock::universal_time())
					<< " *** SKIPPED_PIECE [ piece: " << i->piece_index << " | "
					"b: " << i->block_index << " ] ***\n";
#endif
				// since this piece was skipped, clear it and allow it to
				// be requested from other peers
				picker.abort_download(*i);
			}
		
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " <== PIECE   [ piece: " << p.piece << " | "
				"b: " << p.start / m_torrent->block_size() << " | "
				"s: " << p.start << " | "
				"l: " << p.length << " ]\n";
#endif

			// remove the request that just finished
			// from the download queue plus the
			// skipped blocks.
			m_download_queue.erase(m_download_queue.begin()
				, boost::next(b));
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
							, m_peer_id
							, "got a block that was not requested"));
				}
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << " *** The block we just got was not requested ***\n";
#endif
			}
		}

		// if the block we got is already finished, then ignore it
		if (picker.is_finished(block_finished)) return;

		m_torrent->filesystem().write(&m_recv_buffer[9], p.piece, p.start, p.length);

		bool was_seed = m_torrent->is_seed();
		bool was_finished = picker.num_filtered() + m_torrent->num_pieces()
					== m_torrent->torrent_file().num_pieces();
		
		picker.mark_as_finished(block_finished, m_socket->sender());

		m_torrent->get_policy().block_finished(*this, block_finished);

		// did we just finish the piece?
		if (picker.is_piece_finished(p.piece))
		{
			bool verified = m_torrent->verify_piece(p.piece);
			if (verified)
			{
				m_torrent->announce_piece(p.piece);
				assert(m_torrent->valid_metadata());
				if (!was_finished
					&& picker.num_filtered() + m_torrent->num_pieces()
						== m_torrent->torrent_file().num_pieces())  
				{
					// torrent finished
					// i.e. all the pieces we're interested in have
					// been downloaded. Release the files (they will open
					// in read only mode if needed)
					m_torrent->finished();
				}
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

		if (!can_write() && m_writability_monitored)
		{
			m_writability_monitored = false;
			m_selector.remove_writable(m_socket);
		}

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== CANCEL  [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif
	}

	// -----------------------------
	// ------ EXTENSION LIST -------
	// -----------------------------

	void peer_connection::on_extension_list(int received)
	{
		INVARIANT_CHECK;

		assert(m_torrent);
		assert(received > 0);
		if (m_packet_size > 100 * 1000)
		{
			// too big extension message, abort
			throw protocol_error("'extensions' message size > 100kB");
		}
		m_statistics.received_bytes(0, received);
		if (m_recv_pos < m_packet_size) return;

		try
		{
			entry e = bdecode(m_recv_buffer.begin()+1, m_recv_buffer.end());
#ifdef TORRENT_VERBOSE_LOGGING
			entry::dictionary_type& extensions = e.dict();
			std::stringstream ext;
			e.print(ext);
			(*m_logger) << ext.str();
#endif

			for (int i = 0; i < num_supported_extensions; ++i)
			{
				entry* f = e.find_key(extension_names[i]);
				if (f)
				{
					m_extension_messages[i] = (int)f->integer();
				}
			}
#ifdef TORRENT_VERBOSE_LOGGING
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

		if (extended_id >= 0 && extended_id < num_supported_extensions
			&& !m_ses.m_extension_enabled[extended_id])
			throw protocol_error("'extended' message using disabled extension");

		switch (extended_id)
		{
		case extended_chat_message:
			on_chat(); break;
		case extended_metadata_message:
			on_metadata(); break;
		case extended_peer_exchange_message:
			on_peer_exchange(); break;
		case extended_listen_port_message:
			on_listen_port(); break;
		default:
			throw protocol_error("unknown extended message id: "
				+ boost::lexical_cast<std::string>(extended_id));
		};
	}

	// -----------------------------
	// ----------- CHAT ------------
	// -----------------------------

	void peer_connection::on_chat()
	{
		if (m_packet_size > 2 * 1024)
			throw protocol_error("CHAT message larger than 2 kB");

		if (m_recv_pos < m_packet_size) return;
		try
		{
			entry d = bdecode(m_recv_buffer.begin()+5, m_recv_buffer.end());
			const std::string& str = d["msg"].string();

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
			// TODO: make these non-fatal errors
			// they should just ignore the chat message
			// and report the error via an alert
			throw protocol_error("invalid bencoding in CHAT message");
		}
		catch (type_error&)
		{
			throw protocol_error("invalid types in bencoded CHAT message");
		}
		return;
	}

	// -----------------------------
	// --------- METADATA ----------
	// -----------------------------

	void peer_connection::on_metadata()
	{
		assert(m_torrent);

		if (m_packet_size > 500 * 1024)
			throw protocol_error("metadata message larger than 500 kB");

		if (m_recv_pos < 6) return;

		std::vector<char>::iterator ptr = m_recv_buffer.begin() + 5;
		int type = detail::read_uint8(ptr);

		switch (type)
		{
		case 0: // request
			{
				if (m_recv_pos < m_packet_size) return;

				int start = detail::read_uint8(ptr);
				int size = detail::read_uint8(ptr) + 1;

				if (m_packet_size != 8)
				{
					// invalid metadata request
					throw protocol_error("invalid metadata request");
				}

				send_metadata(std::make_pair(start, size));
			}
			break;
		case 1: // data
			{
				if (m_recv_pos < 14) return;
				int total_size = detail::read_int32(ptr);
				int offset = detail::read_int32(ptr);
				int data_size = m_packet_size - 5 - 9;

				if (total_size > 500 * 1024)
					throw protocol_error("metadata size larger than 500 kB");
				if (offset > total_size)
					throw protocol_error("invalid metadata offset");
				if (offset + data_size > total_size)
					throw protocol_error("invalid metadata message");

				m_torrent->metadata_progress(total_size, m_recv_pos - 14
					- m_metadata_progress);
				m_metadata_progress = m_recv_pos - 14;
				if (m_recv_pos < m_packet_size) return;

#ifdef TORRENT_VERBOSE_LOGGING
				using namespace boost::posix_time;
				(*m_logger) << to_simple_string(second_clock::universal_time())
					<< " <== METADATA [ tot: " << total_size << " offset: "
					<< offset << " size: " << data_size << " ]\n";
#endif

				m_waiting_metadata_request = false;
				m_torrent->received_metadata(&m_recv_buffer[5+9], data_size, offset, total_size);
				m_metadata_progress = 0;
			}
			break;
		case 2: // have no data
			if (m_recv_pos < m_packet_size) return;

			m_no_metadata = second_clock::universal_time();
			if (m_waiting_metadata_request)
				m_torrent->cancel_metadata_request(m_last_metadata_request);
			m_waiting_metadata_request = false;
			break;
		default:
			throw protocol_error("unknown metadata extension message: "
				+ boost::lexical_cast<std::string>(type));
		}
		
	}

	// -----------------------------
	// ------ PEER EXCHANGE --------
	// -----------------------------

	void peer_connection::on_peer_exchange()
	{
		
	}

	// -----------------------------
	// ------- LISTEN PORT ---------
	// -----------------------------

	void peer_connection::on_listen_port()
	{
		using namespace boost::posix_time;
		assert(m_torrent);

		if (m_packet_size != 7)
			throw protocol_error("invalid listen_port message");

		if (is_local())
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< "<== LISTEN_PORT [ UNEXPECTED ]\n";
#endif
			return;
		}

		const char* ptr = &m_recv_buffer[5];
		unsigned short port = detail::read_uint16(ptr);

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << to_simple_string(second_clock::universal_time())
				<< "<== LISTEN_PORT [ port: " << port << " ]\n";
#endif

		address adr = m_socket->sender();
		adr.port = port;
		m_torrent->get_policy().peer_from_tracker(adr, m_peer_id);
	}

	bool peer_connection::has_metadata() const
	{
		using namespace boost::posix_time;
		return second_clock::universal_time() - m_no_metadata > minutes(5);
	}


	void peer_connection::disconnect()
	{
		if (m_disconnecting) return;
		detail::session_impl::connection_map::iterator i
			= m_ses.m_connections.find(m_socket);
		m_disconnecting = true;
		assert(i != m_ses.m_connections.end());
		assert(std::find(m_ses.m_disconnect_peer.begin()
			, m_ses.m_disconnect_peer.end(), i)
			== m_ses.m_disconnect_peer.end());
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

		assert(m_torrent->valid_metadata());

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

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " ==> CANCEL  [ piece: " << block.piece_index << " | s: "
				<< block_offset << " | l: " << block_size << " | " << block.block_index << " ]\n";
#endif

		send_buffer_updated();
	}

	void peer_connection::send_request(piece_block block)
	{
		INVARIANT_CHECK;

		assert(m_torrent->valid_metadata());
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

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> REQUEST [ "
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

	void peer_connection::send_metadata(std::pair<int, int> req)
	{
		assert(req.first >= 0);
		assert(req.second > 0);
		assert(req.second <= 256);
		assert(req.first + req.second <= 256);
		assert(m_torrent);
		INVARIANT_CHECK;

		// abort if the peer doesn't support the metadata extension
		if (!supports_extension(extended_metadata_message)) return;

		std::back_insert_iterator<std::vector<char> > ptr(m_send_buffer);

		if (m_torrent->valid_metadata())
		{
			std::pair<int, int> offset
				= req_to_offset(req, (int)m_torrent->metadata().size());

			// yes, we have metadata, send it
			detail::write_uint32(5 + 9 + offset.second, ptr);
			detail::write_uint8(msg_extended, ptr);
			detail::write_int32(m_extension_messages[extended_metadata_message], ptr);
			// means 'data packet'
			detail::write_uint8(1, ptr);
			detail::write_uint32((int)m_torrent->metadata().size(), ptr);
			detail::write_uint32(offset.first, ptr);
			std::vector<char> const& metadata = m_torrent->metadata();
			std::copy(metadata.begin() + offset.first
				, metadata.begin() + offset.first + offset.second, ptr);
		}
		else
		{
			// we don't have the metadata, reply with
			// don't have-message
			detail::write_uint32(1 + 4 + 1, ptr);
			detail::write_uint8(msg_extended, ptr);
			detail::write_int32(m_extension_messages[extended_metadata_message], ptr);
			// means 'have no data'
			detail::write_uint8(2, ptr);
		}
		send_buffer_updated();
	}

	void peer_connection::send_metadata_request(std::pair<int, int> req)
	{
		assert(req.first >= 0);
		assert(req.second > 0);
		assert(req.first + req.second <= 256);
		assert(m_torrent);
		assert(!m_torrent->valid_metadata());
		INVARIANT_CHECK;

		int start = req.first;
		int size = req.second;

		// abort if the peer doesn't support the metadata extension
		if (!supports_extension(extended_metadata_message)) return;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> METADATA_REQUEST [ start: " << req.first
			<< " size: " << req.second << " ]\n";
#endif

		std::back_insert_iterator<std::vector<char> > ptr(m_send_buffer);

		detail::write_uint32(1 + 4 + 3, ptr);
		detail::write_uint8(msg_extended, ptr);
		detail::write_int32(m_extension_messages[extended_metadata_message], ptr);
		// means 'request data'
		detail::write_uint8(0, ptr);
		detail::write_uint8(start, ptr);
		detail::write_uint8(size - 1, ptr);
		send_buffer_updated();
	}

	void peer_connection::send_chat_message(const std::string& msg)
	{
		INVARIANT_CHECK;

		assert(msg.length() <= 1 * 1024);
		if (!supports_extension(extended_chat_message)) return;

		entry e(entry::dictionary_t);
		e["msg"] = msg;

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

		if (m_torrent->num_pieces() == 0) return;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> BITFIELD ";

		for (int i = 0; i < (int)m_have_piece.size(); ++i)
		{
			if (m_torrent->have_piece(i)) (*m_logger) << "1";
			else (*m_logger) << "0";
		}
		(*m_logger) << "\n";
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

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> EXTENSIONS\n";
#endif
		assert(m_supports_extensions);

		entry extension_list(entry::dictionary_t);

		for (int i = 0; i < num_supported_extensions; ++i)
		{
			// if this specific extension is disabled
			// just don't add it to the supported set
			if (!m_ses.m_extension_enabled[i]) continue;
			extension_list[extension_names[i]] = i;
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

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> CHOKE\n";
#endif
#ifndef NDEBUG
		using namespace boost::posix_time;
		m_last_choke = second_clock::universal_time();
#endif
		m_num_invalid_requests = 0;
		m_requests.clear();
		send_buffer_updated();
	}

	void peer_connection::send_unchoke()
	{
		INVARIANT_CHECK;

#ifndef NDEBUG
		// TODO: once the policy lowers the interval for optimistic
		// unchoke, increase this value that interval
		// this condition cannot be guaranteed since if peers disconnect
		// a new one will be unchoked ignoring when it was last choked
		using namespace boost::posix_time;
		//assert(second_clock::universal_time() - m_last_choke > seconds(9));
#endif

		if (!m_choked) return;
		char msg[] = {0,0,0,1,msg_unchoke};
		m_send_buffer.insert(m_send_buffer.end(), msg, msg+sizeof(msg));
		m_choked = false;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> UNCHOKE\n";
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

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> INTERESTED\n";
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

		m_became_uninteresting = second_clock::universal_time();

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> NOT_INTERESTED\n";
#endif
		send_buffer_updated();
	}

	void peer_connection::send_have(int index)
	{
		assert(m_torrent->valid_metadata());
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

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> HAVE    [ piece: " << index << " ]\n";
#endif
		send_buffer_updated();
	}

	size_type peer_connection::share_diff() const
	{
		float ratio = m_torrent->ratio();

		// if we have an infinite ratio, just say we have downloaded
		// much more than we have uploaded. And we'll keep uploading.
		if (ratio == 0.f)
			return std::numeric_limits<size_type>::max();

		return m_free_upload
			+ static_cast<size_type>(m_statistics.total_payload_download() * ratio)
			- m_statistics.total_payload_upload();
	}

	void peer_connection::second_tick()
	{
		INVARIANT_CHECK;

		// if we don't have any metadata, and this peer
		// supports the request metadata extension
		// and we aren't currently waiting for a request
		// reply. Then, send a request for some metadata.
		if (!m_torrent->valid_metadata()
			&& supports_extension(extended_metadata_message)
			&& !m_waiting_metadata_request
			&& has_metadata())
		{
			assert(m_torrent);
			m_last_metadata_request = m_torrent->metadata_request();
			send_metadata_request(m_last_metadata_request);
			m_waiting_metadata_request = true;
			m_metadata_request = second_clock::universal_time();
		}

		m_statistics.second_tick();
		m_ul_bandwidth_quota.used = std::min(
			(int)ceil(statistics().upload_rate())
			, m_ul_bandwidth_quota.given);

		// If the client sends more data
		// we send it data faster, otherwise, slower.
		// It will also depend on how much data the
		// client has sent us. This is the mean to
		// maintain the share ratio given by m_ratio
		// with all peers.

		if (m_torrent->is_seed() || is_choked() || m_torrent->ratio() == 0.0f)
		{
			// if we have downloaded more than one piece more
			// than we have uploaded OR if we are a seed
			// have an unlimited upload rate
			if(!m_send_buffer.empty() || (!m_requests.empty() && !is_choked()))
				m_ul_bandwidth_quota.max = resource_request::inf;
			else
				m_ul_bandwidth_quota.max = m_ul_bandwidth_quota.min;
		}
		else
		{
			size_type bias = 0x10000+2*m_torrent->block_size() + m_free_upload;

			double break_even_time = 15; // seconds.
			size_type have_uploaded = m_statistics.total_payload_upload();
			size_type have_downloaded = m_statistics.total_payload_download();
			double download_speed = m_statistics.download_rate();

			size_type soon_downloaded =
				have_downloaded + (size_type)(download_speed * break_even_time*1.5);

			if(m_torrent->ratio() != 1.f)
				soon_downloaded = (size_type)(soon_downloaded*(double)m_torrent->ratio());

			double upload_speed_limit = (soon_downloaded - have_uploaded
				                         + bias) / break_even_time;

			upload_speed_limit = std::min(upload_speed_limit,
				(double)std::numeric_limits<int>::max());

			m_ul_bandwidth_quota.max
				= std::max((int)upload_speed_limit, m_ul_bandwidth_quota.min);
		}
		if (m_ul_bandwidth_quota.given > m_ul_bandwidth_quota.max)
			m_ul_bandwidth_quota.given = m_ul_bandwidth_quota.max;

		if (m_ul_bandwidth_quota.used > m_ul_bandwidth_quota.given)
			m_ul_bandwidth_quota.used = m_ul_bandwidth_quota.given;

		send_buffer_updated();

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
			m_ul_bandwidth_quota.wanted = std::numeric_limits<int>::max();
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
			m_ul_bandwidth_quota.wanted = static_cast<int>(m_statistics.download_rate()) + bias;

			// the maximum send_quota given our download rate from this peer
			if (m_ul_bandwidth_quota.wanted < 256) m_ul_bandwidth_quota.wanted = 256;
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
		assert(can_read());
		assert(m_selector.is_readability_monitored(m_socket));

		if (m_disconnecting) return;

		for(;;)
		{
			assert(m_packet_size > 0);
			int max_receive = std::min(
				m_dl_bandwidth_quota.left()
				, m_packet_size - m_recv_pos);
			int received = m_socket->receive(
				&m_recv_buffer[m_recv_pos], max_receive);

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
				m_connecting = false;
				m_last_receive = second_clock::universal_time();

				m_recv_pos += received;
				m_dl_bandwidth_quota.used += received;
				if (!can_read())
				{
					assert(m_readability_monitored);
					assert(m_selector.is_readability_monitored(m_socket));
					m_selector.remove_readable(m_socket);
					m_readability_monitored = false;
				}

				switch(m_state)
				{
				case read_protocol_length:

					m_statistics.received_bytes(0, received);
					if (m_recv_pos < m_packet_size) break;
					assert(m_recv_pos == m_packet_size);

					m_packet_size = reinterpret_cast<unsigned char&>(m_recv_buffer[0]);

#ifdef TORRENT_VERBOSE_LOGGING
					(*m_logger) << " protocol length: " << m_packet_size << "\n";
#endif
					if (m_packet_size > 100 || m_packet_size <= 0)
					{
							std::stringstream s;
							s << "incorrect protocol length ("
								<< m_packet_size
								<< ") should be 19.";
							throw protocol_error(s.str());
					}

					m_state = read_protocol_string;
					m_recv_buffer.resize(m_packet_size);
					m_recv_pos = 0;

					break;


				case read_protocol_string:
					{
						m_statistics.received_bytes(0, received);
						if (m_recv_pos < m_packet_size) break;
						assert(m_recv_pos == m_packet_size);
#ifdef TORRENT_VERBOSE_LOGGING
						(*m_logger) << " protocol: '" << std::string(m_recv_buffer.begin()
							, m_recv_buffer.end()) << "'\n";
#endif
						const char protocol_string[] = "BitTorrent protocol";
						if (!std::equal(m_recv_buffer.begin(), m_recv_buffer.end()
							, protocol_string))
						{
							const char cmd[] = "version";
							if (m_recv_buffer.size() == 7 && std::equal(m_recv_buffer.begin(), m_recv_buffer.end(), cmd))
							{
#ifdef TORRENT_VERBOSE_LOGGING
								(*m_logger) << "sending libtorrent version\n";
#endif
								m_socket->send("libtorrent version " LIBTORRENT_VERSION "\n", 27);
								throw protocol_error("closing");
							}
#ifdef TORRENT_VERBOSE_LOGGING
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

					if ((m_recv_buffer[7] & 0x01) && m_ses.extensions_enabled())
						m_supports_extensions = true;

					if (m_torrent == 0)
					{

						// now, we have to see if there's a torrent with the
						// info_hash we got from the peer
						sha1_hash info_hash;
						std::copy(m_recv_buffer.begin()+8, m_recv_buffer.begin() + 28, (char*)info_hash.begin());
						
						m_torrent = m_ses.find_torrent(info_hash);
						if (m_torrent && m_torrent->is_aborted()) m_torrent = 0;
						if (m_torrent == 0)
						{
							// we couldn't find the torrent!
#ifdef TORRENT_VERBOSE_LOGGING
							(*m_logger) << " couldn't find a torrent with the given info_hash\n";
#endif
							throw protocol_error("got info-hash that is not in our session");
						}

						if (m_torrent->is_paused())
						{
							// paused torrents will not accept
							// incoming connections
#ifdef TORRENT_VERBOSE_LOGGING
							(*m_logger) << " rejected connection to paused torrent\n";
#endif
							throw protocol_error("connection rejected by paused torrent");
						}

						if (m_torrent->valid_metadata()) init();

						// assume the other end has no pieces
						// if we don't have valid metadata yet,
						// leave the vector unallocated
						std::fill(m_have_piece.begin(), m_have_piece.end(), false);

						// yes, we found the torrent
						// reply with our handshake
						send_handshake();
						send_bitfield();
					}
					else
					{
						// verify info hash
						if (!std::equal(m_recv_buffer.begin()+8, m_recv_buffer.begin() + 28, (const char*)m_torrent->torrent_file().info_hash().begin()))
						{
#ifdef TORRENT_VERBOSE_LOGGING
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
#ifdef TORRENT_VERBOSE_LOGGING
					(*m_logger) << " info_hash received\n";
#endif
					break;
				}


				case read_peer_id:
				{
					m_statistics.received_bytes(0, received);
					if (m_recv_pos < m_packet_size) break;
					assert(m_recv_pos == m_packet_size);
					assert(m_packet_size == 20);

#ifdef TORRENT_VERBOSE_LOGGING
					{
						peer_id tmp;
						std::copy(m_recv_buffer.begin(), m_recv_buffer.begin() + 20, (char*)tmp.begin());
						std::stringstream s;
						s << "received peer_id: " << tmp << " client: " << identify_client(tmp) << "\n";
						s << "as ascii: ";
						for (peer_id::iterator i = tmp.begin(); i != tmp.end(); ++i)
						{
							if (std::isprint(*i)) s << *i;
							else s << ".";
						}
						s << "\n";
						(*m_logger) << s.str();
					}
#endif
					std::copy(m_recv_buffer.begin(), m_recv_buffer.begin() + 20, (char*)m_peer_id.begin());

					// disconnect if the peer has the same peer-id as ourself
					// since it most likely is ourself then
					if (m_peer_id == m_ses.get_peer_id())
						throw protocol_error("closing connection to ourself");
					
					if (!m_active)
					{
						// check to make sure we don't have another connection with the same
						// info_hash and peer_id. If we do. close this connection.
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
#ifdef TORRENT_VERBOSE_LOGGING
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

				// if we have used all our download quota,
				// break the receive loop
				if (!can_read()) break;
			}
		}
		assert(m_packet_size > 0);
	}


	bool peer_connection::can_write() const
	{
		// if we have requests or pending data to be sent or announcements to be made
		// we want to send data
		return ((!m_requests.empty() && !m_choked)
			|| !m_send_buffer.empty())
			&& m_ul_bandwidth_quota.left() > 0;
	}

	bool peer_connection::can_read() const
	{
		return m_dl_bandwidth_quota.left() > 0;
	}

	// --------------------------
	// SEND DATA
	// --------------------------

	// throws exception when the client should be disconnected
	void peer_connection::send_data()
	{
		INVARIANT_CHECK;

		assert(!m_disconnecting);
		assert(m_socket->is_writable());
		assert(can_write());

		// only add new piece-chunks if the send buffer is small enough
		// otherwise there will be no end to how large it will be!
		// TODO: the buffer size should probably be dependent on the transfer speed
		while (!m_requests.empty()
			&& ((int)m_send_buffer.size() < m_torrent->block_size() * 6)
			&& !m_choked)
		{
			assert(m_torrent->valid_metadata());
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
#ifdef TORRENT_VERBOSE_LOGGING
			using namespace boost::posix_time;
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " ==> PIECE   [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
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
			assert(m_torrent->valid_metadata());
			for (std::vector<int>::iterator i = m_announce_queue.begin();
				i != m_announce_queue.end();
				++i)
			{
				send_have(*i);
			}
			m_announce_queue.clear();
		}

		assert(m_ul_bandwidth_quota.used <= m_ul_bandwidth_quota.given);

		// send the actual buffer
		if (!m_send_buffer.empty())
		{
			int amount_to_send
				= std::min(m_ul_bandwidth_quota.left(), (int)m_send_buffer.size());

			assert(amount_to_send > 0);

			// we have data that's scheduled for sending
			int sent = m_socket->send(
				&m_send_buffer[0]
				, amount_to_send);

			if (sent > 0)
			{
				m_ul_bandwidth_quota.used += sent;

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

			m_last_sent = second_clock::universal_time();
		}

		assert(m_writability_monitored);
		send_buffer_updated();
	}

#ifndef NDEBUG
	void peer_connection::check_invariant() const
	{
		assert(can_write() == m_selector.is_writability_monitored(m_socket));

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

		// if the socket is still connecting, don't
		// consider it timed out. Because Windows XP SP2
		// may delay connection attempts.
		if (m_connecting) return false;
		
		// if the peer hasn't said a thing for a certain
		// time, it is considered to have timed out
		time_duration d;
		d = second_clock::universal_time() - m_last_receive;
		if (d > seconds(m_timeout)) return true;
/*
		// if the peer hasn't become interested and we haven't
		// become interested in the peer for 60 seconds, it
		// has also timed out.
		time_duration d1;
		time_duration d2;
		d1 = second_clock::universal_time() - m_became_uninterested;
		d2 = second_clock::universal_time() - m_became_uninteresting;
		if (!m_interesting
			&& !m_peer_interested
			&& d1 > seconds(60 * 3)
			&& d2 > seconds(60 * 3))
		{
			return true;
		}
*/
		return false;
	}


	void peer_connection::keep_alive()
	{
		INVARIANT_CHECK;

		boost::posix_time::time_duration d;
		d = second_clock::universal_time() - m_last_sent;
		if (d.total_seconds() < m_timeout / 2) return;

		// we must either send a keep-alive
		// message or something else.
		if (m_announce_queue.empty())
		{
			char noop[] = {0,0,0,0};
			m_send_buffer.insert(m_send_buffer.end(), noop, noop+4);
			m_last_sent = second_clock::universal_time();
#ifdef TORRENT_VERBOSE_LOGGING
			using namespace boost::posix_time;
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " ==> NOP\n";
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
		// if m_num_pieces == 0, we probably doesn't have the
		// metadata yet.
		return m_num_pieces == (int)m_have_piece.size() && m_num_pieces > 0;
	}

	void peer_connection::send_buffer_updated()
	{
		if (!can_write())
		{
			if (m_writability_monitored)
			{
				m_selector.remove_writable(m_socket);
				m_writability_monitored = false;
			}
			assert(!m_selector.is_writability_monitored(m_socket));
			return;
		}

		assert(m_ul_bandwidth_quota.left() > 0);
		assert(can_write());
		if (!m_writability_monitored)
		{
			m_selector.monitor_writability(m_socket);
			m_writability_monitored = true;
		}
		assert(m_writability_monitored);
		assert(m_selector.is_writability_monitored(m_socket));
	}
}

