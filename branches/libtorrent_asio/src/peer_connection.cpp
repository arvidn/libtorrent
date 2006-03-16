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
#include <boost/bind.hpp>

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
using boost::bind;
using boost::shared_ptr;
using libtorrent::detail::session_impl;

namespace libtorrent
{

	void intrusive_ptr_add_ref(peer_connection const* c)
	{
		assert(c->m_refs >= 0);
		assert(c != 0);
		++c->m_refs;
	}

	void intrusive_ptr_release(peer_connection const* c)
	{
		assert(c->m_refs > 0);
		assert(c != 0);
		--c->m_refs;
		if (c->m_refs == 0)
			delete c;
	}

	peer_connection::peer_connection(
		detail::session_impl& ses
		, boost::weak_ptr<torrent> tor
		, shared_ptr<stream_socket> s
		, tcp::endpoint const& remote)
		:
#ifndef NDEBUG
		m_last_choke(boost::posix_time::second_clock::universal_time()
			- hours(1))
		,
#endif
		  m_ses(ses)
		, m_timeout(120)
		, m_last_piece(second_clock::universal_time())
		, m_packet_size(0)
		, m_recv_pos(0)
		, m_current_send_buffer(0)
		, m_last_receive(second_clock::universal_time())
		, m_last_sent(second_clock::universal_time())
		, m_socket(s)
		, m_remote(remote)
		, m_torrent(tor)
		, m_active(true)
		, m_peer_interested(false)
		, m_peer_choked(true)
		, m_interesting(false)
		, m_choked(true)
		, m_failed(false)
		, m_num_pieces(0)
		, m_desired_queue_size(2)
		, m_free_upload(0)
		, m_trust_points(0)
		, m_assume_fifo(false)
		, m_disconnecting(false)
		, m_became_uninterested(second_clock::universal_time())
		, m_became_uninteresting(second_clock::universal_time())
		, m_connecting(true)
		, m_queued(true)
		, m_writing(false)
		, m_last_write_size(0)
		, m_reading(false)
		, m_last_read_size(0)
		, m_refs(0)
	{
		INVARIANT_CHECK;

#ifdef TORRENT_VERBOSE_LOGGING
		m_logger = m_ses.create_log(m_remote.address().to_string() + "_"
			+ boost::lexical_cast<std::string>(m_remote.port()));
		(*m_logger) << "*** OUTGOING CONNECTION\n";
#endif

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);
		// these numbers are used the first second of connection.
		// then the given upload limits will be applied by running
		// allocate_resources().
		m_ul_bandwidth_quota.min = 10;
		m_ul_bandwidth_quota.max = resource_request::inf;

		if (t->m_ul_bandwidth_quota.given == resource_request::inf)
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
	
		if (t->m_dl_bandwidth_quota.given == resource_request::inf)
		{
			m_dl_bandwidth_quota.given = resource_request::inf;
		}
		else
		{
			// just enough to get started with the handshake and bitmask
			m_dl_bandwidth_quota.given = 400;
		}

		std::fill(m_peer_id.begin(), m_peer_id.end(), 0);

		if (t->ready_for_connections())
			init();
	}

	peer_connection::peer_connection(
		detail::session_impl& ses
		, boost::shared_ptr<stream_socket> s)
		:
#ifndef NDEBUG
		m_last_choke(boost::posix_time::second_clock::universal_time()
			- hours(1))
		,
#endif
		  m_ses(ses)
		, m_timeout(120)
		, m_last_piece(second_clock::universal_time())
		, m_packet_size(0)
		, m_recv_pos(0)
		, m_current_send_buffer(0)
		, m_last_receive(second_clock::universal_time())
		, m_last_sent(second_clock::universal_time())
		, m_socket(s)
		, m_active(false)
		, m_peer_id()
		, m_peer_interested(false)
		, m_peer_choked(true)
		, m_interesting(false)
		, m_choked(true)
		, m_failed(false)
		, m_num_pieces(0)
		, m_desired_queue_size(2)
		, m_free_upload(0)
		, m_trust_points(0)
		, m_assume_fifo(false)
		, m_disconnecting(false)
		, m_became_uninterested(second_clock::universal_time())
		, m_became_uninteresting(second_clock::universal_time())
		, m_connecting(false)
		, m_queued(false)
		, m_writing(false)
		, m_last_write_size(0)
		, m_reading(false)
		, m_last_read_size(0)
		, m_refs(0)
	{
		INVARIANT_CHECK;

		m_remote = m_socket->remote_endpoint();

#ifdef TORRENT_VERBOSE_LOGGING
		assert(m_socket->remote_endpoint() == remote());
		m_logger = m_ses.create_log(remote().address().to_string() + "_"
			+ boost::lexical_cast<std::string>(remote().port()));
		(*m_logger) << "*** INCOMING CONNECTION\n";
#endif


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

		std::fill(m_peer_id.begin(), m_peer_id.end(), 0);
	}

	void peer_connection::init()
	{
		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);
		assert(t->valid_metadata());
		assert(t->ready_for_connections());

		m_have_piece.resize(t->torrent_file().num_pieces(), false);

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
			t->peer_has(index);
			if (!t->have_piece(index)
				&& !t->picker().is_filtered(index))
				interesting = true;
		}

		if (piece_list.size() == m_have_piece.size())
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " *** THIS IS A SEED ***\n";
#endif
			// if we're a seed too, disconnect
			if (t->is_seed())
			{
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << " we're also a seed, disconnecting\n";
#endif
				throw std::runtime_error("seed to seed connection redundant, disconnecting");
			}
		}

		if (interesting)
			t->get_policy().peer_is_interesting(*this);
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
#ifndef NDEBUG
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (t) assert(t->connection_for(remote()) != this);
#endif
	}

	void peer_connection::announce_piece(int index)
	{
		// optimization, don't send have messages
		// to peers that already have the piece
		if (has_piece(index)) return;
		write_have(index);
	}

	bool peer_connection::has_piece(int i) const
	{
		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);
		assert(t->valid_metadata());
		assert(i >= 0);
		assert(i < t->torrent_file().num_pieces());
		return m_have_piece[i];
	}

	std::deque<piece_block> const& peer_connection::request_queue() const
	{
		return m_request_queue;
	}
	
	std::deque<piece_block> const& peer_connection::download_queue() const
	{
		return m_download_queue;
	}
	
	std::deque<peer_request> const& peer_connection::upload_queue() const
	{
		return m_requests;
	}

	void peer_connection::add_stat(size_type downloaded, size_type uploaded)
	{
		m_statistics.add_stat(downloaded, uploaded);
	}

	std::vector<bool> const& peer_connection::get_bitfield() const
	{
		return m_have_piece;
	}

	void peer_connection::received_valid_data()
	{
		m_trust_points++;
		// TODO: make this limit user settable
		if (m_trust_points > 20) m_trust_points = 20;
	}

	void peer_connection::received_invalid_data()
	{
		// we decrease more than we increase, to keep the
		// allowed failed/passed ratio low.
		// TODO: make this limit user settable
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
		assert(m_ul_bandwidth_quota.left() >= 0);
		assert(m_dl_bandwidth_quota.left() >= 0);
		setup_send();
		setup_receive();
	}

	// verifies a piece to see if it is valid (is within a valid range)
	// and if it can correspond to a request generated by libtorrent.
	bool peer_connection::verify_piece(const peer_request& p) const
	{
		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		assert(t->valid_metadata());

		return p.piece >= 0
			&& p.piece < t->torrent_file().num_pieces()
			&& p.length > 0
			&& p.start >= 0
			&& (p.length == t->block_size()
				|| (p.length < t->block_size()
				&& p.piece == t->torrent_file().num_pieces()-1
				&& p.start + p.length == t->torrent_file().piece_size(p.piece)))
			&& p.start + p.length <= t->torrent_file().piece_size(p.piece)
			&& p.start % t->block_size() == 0;
	}
	
	void peer_connection::attach_to_torrent(sha1_hash const& ih)
	{
		m_torrent = m_ses.find_torrent(ih);

		boost::shared_ptr<torrent> t = m_torrent.lock();

		if (t && t->is_aborted()) m_torrent.reset();
		if (!t)
		{
			// we couldn't find the torrent!
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " couldn't find a torrent with the given info_hash\n";
#endif
			throw std::runtime_error("got info-hash that is not in our session");
		}

		if (t->is_paused())
		{
			m_torrent.reset();
			// paused torrents will not accept
			// incoming connections
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " rejected connection to paused torrent\n";
#endif
			throw std::runtime_error("connection rejected by paused torrent");
		}

		// check to make sure we don't have another connection with the same
		// info_hash and peer_id. If we do. close this connection.
		t->attach_peer(this);

		// if the torrent isn't ready to accept
		// connections yet, we'll have to wait with
		// our initialization
		if (t->ready_for_connections()) init();

		// assume the other end has no pieces
		// if we don't have valid metadata yet,
		// leave the vector unallocated
		std::fill(m_have_piece.begin(), m_have_piece.end(), false);
	}

	// message handlers

	// -----------------------------
	// --------- KEEPALIVE ---------
	// -----------------------------

	void peer_connection::incoming_keepalive()
	{
		INVARIANT_CHECK;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== KEEPALIVE\n";
#endif
	}

	// -----------------------------
	// ----------- CHOKE -----------
	// -----------------------------

	void peer_connection::incoming_choke()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== CHOKE\n";
#endif
		m_peer_choked = true;
		t->get_policy().choked(*this);

		// remove all pieces from this peers download queue and
		// remove the 'downloading' flag from piece_picker.
		for (std::deque<piece_block>::iterator i = m_download_queue.begin();
			i != m_download_queue.end(); ++i)
		{
			t->picker().abort_download(*i);
		}
		for (std::deque<piece_block>::const_iterator i = m_request_queue.begin()
			, end(m_request_queue.end()); i != end; ++i)
		{
			// since this piece was skipped, clear it and allow it to
			// be requested from other peers
			t->picker().abort_download(*i);
		}
		m_download_queue.clear();
		m_request_queue.clear();

#ifndef NDEBUG
//		t->picker().integrity_check(m_torrent);
#endif
	}

	// -----------------------------
	// ---------- UNCHOKE ----------
	// -----------------------------

	void peer_connection::incoming_unchoke()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== UNCHOKE\n";
#endif
		m_peer_choked = false;
		t->get_policy().unchoked(*this);
	}

	// -----------------------------
	// -------- INTERESTED ---------
	// -----------------------------

	void peer_connection::incoming_interested()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== INTERESTED\n";
#endif
		m_peer_interested = true;
		t->get_policy().interested(*this);
	}

	// -----------------------------
	// ------ NOT INTERESTED -------
	// -----------------------------

	void peer_connection::incoming_not_interested()
	{
		INVARIANT_CHECK;

		m_became_uninterested = second_clock::universal_time();

		// clear the request queue if the client isn't interested
		m_requests.clear();
		setup_send();

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== NOT_INTERESTED\n";
#endif

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		m_peer_interested = false;
		t->get_policy().not_interested(*this);
	}

	// -----------------------------
	// ----------- HAVE ------------
	// -----------------------------

	void peer_connection::incoming_have(int index)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== HAVE    [ piece: " << index << "]\n";
#endif

		// if we got an invalid message, abort
		if (index >= (int)m_have_piece.size() || index < 0)
			throw protocol_error("got 'have'-message with higher index "
				"than the number of pieces");

		if (m_have_piece[index])
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << "   got redundant HAVE message for index: " << index << "\n";
#endif
		}
		else
		{
			m_have_piece[index] = true;

			// only update the piece_picker if
			// we have the metadata
			if (t->valid_metadata())
			{
				++m_num_pieces;
				t->peer_has(index);

				if (!t->have_piece(index)
					&& !is_interesting()
					&& !t->picker().is_filtered(index))
					t->get_policy().peer_is_interesting(*this);
			}

			if (t->is_seed() && is_seed())
			{
				throw protocol_error("seed to seed connection redundant, disconnecting");
			}
		}
	}

	// -----------------------------
	// --------- BITFIELD ----------
	// -----------------------------

	void peer_connection::incoming_bitfield(std::vector<bool> const& bitfield)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== BITFIELD\n";
#endif

		// if we don't have the metedata, we cannot
		// verify the bitfield size
		if (t->valid_metadata()
			&& (bitfield.size() / 8) != (m_have_piece.size() / 8))
			throw protocol_error("got bitfield with invalid size");

		// if we don't have metadata yet
		// just remember the bitmask
		// don't update the piecepicker
		// (since it doesn't exist yet)
		if (!t->valid_metadata())
		{
			m_have_piece = bitfield;
			return;
		}

		// build a vector of all pieces
		std::vector<int> piece_list;
		for (int i = 0; i < (int)m_have_piece.size(); ++i)
		{
			bool have = bitfield[i];
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
				t->peer_lost(i);
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
			t->peer_has(index);
			if (!t->have_piece(index)
				&& !t->picker().is_filtered(index))
				interesting = true;
		}

		if (piece_list.size() == m_have_piece.size())
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " *** THIS IS A SEED ***\n";
#endif
			// if we're a seed too, disconnect
			if (t->is_seed())
			{
				throw protocol_error("seed to seed connection redundant, disconnecting");
			}
		}

		if (interesting) t->get_policy().peer_is_interesting(*this);
	}

	// -----------------------------
	// ---------- REQUEST ----------
	// -----------------------------

	void peer_connection::incoming_request(peer_request const& r)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		if (!t->valid_metadata())
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
				"t: " << (int)t->torrent_file().piece_size(r.piece) << " | "
				"n: " << t->torrent_file().num_pieces() << " ]\n";
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
				"t: " << (int)t->torrent_file().piece_size(r.piece) << " | "
				"n: " << t->torrent_file().num_pieces() << " ]\n";
#endif
			return;
		}

		// make sure this request
		// is legal and that the peer
		// is not choked
		if (r.piece >= 0
			&& r.piece < t->torrent_file().num_pieces()
			&& t->have_piece(r.piece)
			&& r.start >= 0
			&& r.start < t->torrent_file().piece_size(r.piece)
			&& r.length > 0
			&& r.length + r.start <= t->torrent_file().piece_size(r.piece)
			&& m_peer_interested)
		{
			// if we have choked the client
			// ignore the request
			if (m_choked)
				return;

			m_requests.push_back(r);
			setup_send();
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
				"t: " << (int)t->torrent_file().piece_size(r.piece) << " | "
				"n: " << t->torrent_file().num_pieces() << " ]\n";
#endif

			++m_num_invalid_requests;

			if (t->alerts().should_post(alert::debug))
			{
				t->alerts().post_alert(invalid_request_alert(
					r
					, t->get_handle()
					, m_remote
					, m_peer_id
					, "peer sent an illegal piece request, ignoring"));
			}
		}
	}

	void peer_connection::incoming_piece_fragment()
	{
		m_last_piece = second_clock::universal_time();
	}
	
	// -----------------------------
	// ----------- PIECE -----------
	// -----------------------------

	void peer_connection::incoming_piece(peer_request const& p, char const* data)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== PIECE   [ piece: " << p.piece << " | "
			"b: " << p.start / t->block_size() << " | "
			"s: " << p.start << " | "
			"l: " << p.length << " | "
			"ds: " << statistics().download_rate() << " | "
			"qs: " << m_desired_queue_size << " ]\n";
#endif

		if (!verify_piece(p))
		{
#ifdef TORRENT_VERBOSE_LOGGING
			using namespace boost::posix_time;
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " <== INVALID_PIECE [ piece: " << p.piece << " | "
				"start: " << p.start << " | "
				"length: " << p.length << " ]\n";
#endif
			throw protocol_error("got invalid piece packet");
		}

		using namespace boost::posix_time;

		piece_picker& picker = t->picker();

		piece_block block_finished(p.piece, p.start / t->block_size());
		std::deque<piece_block>::iterator b
			= std::find(
				m_download_queue.begin()
				, m_download_queue.end()
				, block_finished);

		std::deque<piece_block>::iterator i;

		if (b != m_download_queue.end())
		{
			if (m_assume_fifo)
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
					// TODO: send cancel?
					picker.abort_download(*i);
				}
			
				// remove the request that just finished
				// from the download queue plus the
				// skipped blocks.
				m_download_queue.erase(m_download_queue.begin()
					, boost::next(b));
			}
			else
			{
				m_download_queue.erase(b);
			}
			send_block_requests();
		}
		else
		{
			// cancel the block from the
			// peer that has taken over it.
			boost::optional<tcp::endpoint> peer
				= t->picker().get_downloader(block_finished);
			if (peer)
			{
				peer_connection* pc = t->connection_for(*peer);
				if (pc && pc != this)
				{
					pc->cancel_request(block_finished);
				}
			}
			else
			{
				if (t->alerts().should_post(alert::debug))
				{
					t->alerts().post_alert(
						peer_error_alert(
							m_remote
							, m_peer_id
							, "got a block that was not requested"));
				}
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << " *** The block we just got was not in the "
					"request queue ***\n";
#endif
			}
		}

		// if the block we got is already finished, then ignore it
		if (picker.is_finished(block_finished))
		{
			t->received_redundant_data(p.length);
			return;
		}

		t->filesystem().write(data, p.piece, p.start, p.length);

		bool was_seed = t->is_seed();
		bool was_finished = picker.num_filtered() + t->num_pieces()
			== t->torrent_file().num_pieces();
		
		picker.mark_as_finished(block_finished, m_remote);

		t->get_policy().block_finished(*this, block_finished);

		// if the piece failed, this connection may be closed, and
		// detached from the torrent. In that case m_torrent will
		// be set to 0. So, we need to temporarily save it in this function

		// did we just finish the piece?
		if (picker.is_piece_finished(p.piece))
		{
			bool verified = t->verify_piece(p.piece);
			if (verified)
			{
				t->announce_piece(p.piece);
				assert(t->valid_metadata());
				if (!was_finished
					&& picker.num_filtered() + t->num_pieces()
						== t->torrent_file().num_pieces())  
				{
					// torrent finished
					// i.e. all the pieces we're interested in have
					// been downloaded. Release the files (they will open
					// in read only mode if needed)
					t->finished();
				}
			}
			else
			{
				t->piece_failed(p.piece);
			}
			t->get_policy().piece_finished(p.piece, verified);

			if (!was_seed && t->is_seed())
			{
				assert(verified);
				t->completed();
			}
		}
	}

	// -----------------------------
	// ---------- CANCEL -----------
	// -----------------------------

	void peer_connection::incoming_cancel(peer_request const& r)
	{
		INVARIANT_CHECK;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== CANCEL  [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif

		std::deque<peer_request>::iterator i
			= std::find(m_requests.begin(), m_requests.end(), r);

		if (i != m_requests.end())
		{
			m_requests.erase(i);
		}
		else
		{
#ifdef TORRENT_VERBOSE_LOGGING
			using namespace boost::posix_time;
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " *** GOT CANCEL NOT IN THE QUEUE\n";
#endif
		}
	}

	// -----------------------------
	// --------- DHT PORT ----------
	// -----------------------------

	void peer_connection::incoming_dht_port(int listen_port)
	{
		INVARIANT_CHECK;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== DHT_PORT [ p: " << listen_port << " ]\n";
#endif
	}

	void peer_connection::add_request(piece_block const& block)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		assert(t->valid_metadata());
		assert(block.piece_index >= 0);
		assert(block.piece_index < t->torrent_file().num_pieces());
		assert(block.block_index >= 0);
		assert(block.block_index < t->torrent_file().piece_size(block.piece_index));
		assert(!t->picker().is_downloading(block));

		t->picker().mark_as_downloading(block, m_remote);
		m_request_queue.push_back(block);
		send_block_requests();
	}

	void peer_connection::cancel_request(piece_block const& block)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		assert(t->valid_metadata());

		assert(block.piece_index >= 0);
		assert(block.piece_index < t->torrent_file().num_pieces());
		assert(block.block_index >= 0);
		assert(block.block_index < t->torrent_file().piece_size(block.piece_index));
		assert(t->picker().is_downloading(block));

		t->picker().abort_download(block);

		std::deque<piece_block>::iterator it
			= std::find(m_download_queue.begin(), m_download_queue.end(), block);
		if (it == m_download_queue.end())
		{
			it = std::find(m_request_queue.begin(), m_request_queue.end(), block);
			assert(it != m_request_queue.end());
			if (it == m_request_queue.end()) return;
			m_request_queue.erase(it);
			// since we found it in the request queue, it means it hasn't been
			// sent yet, so we don't have to send a cancel.
			return;
		}
		else
		{	
			m_download_queue.erase(it);
		}

		send_block_requests();

		int block_offset = block.block_index * t->block_size();
		int block_size
			= std::min((int)t->torrent_file().piece_size(block.piece_index)-block_offset,
			t->block_size());
		assert(block_size > 0);
		assert(block_size <= t->block_size());

		peer_request r;
		r.piece = block.piece_index;
		r.start = block_offset;
		r.length = block_size;

		write_cancel(r);

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " ==> CANCEL  [ piece: " << block.piece_index << " | s: "
				<< block_offset << " | l: " << block_size << " | " << block.block_index << " ]\n";
#endif
	}

	void peer_connection::send_choke()
	{
		INVARIANT_CHECK;

		if (m_choked) return;
		write_choke();
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
		write_unchoke();
		m_choked = false;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> UNCHOKE\n";
#endif
	}

	void peer_connection::send_interested()
	{
		INVARIANT_CHECK;

		if (m_interesting) return;
		write_interested();
		m_interesting = true;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> INTERESTED\n";
#endif
	}

	void peer_connection::send_not_interested()
	{
		INVARIANT_CHECK;

		if (!m_interesting) return;
		write_not_interested();
		m_interesting = false;

		m_became_uninteresting = second_clock::universal_time();

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> NOT_INTERESTED\n";
#endif
	}

	void peer_connection::send_block_requests()
	{
		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);
		
		assert(!has_peer_choked());

		if ((int)m_download_queue.size() >= m_desired_queue_size) return;

		while (!m_request_queue.empty()
			&& (int)m_download_queue.size() < m_desired_queue_size)
		{
			piece_block block = m_request_queue.front();
			m_request_queue.pop_front();
			m_download_queue.push_back(block);

			int block_offset = block.block_index * t->block_size();
			int block_size	= std::min((int)t->torrent_file().piece_size(
				block.piece_index) - block_offset, t->block_size());
			assert(block_size > 0);
			assert(block_size <= t->block_size());

			peer_request r;
			r.piece = block.piece_index;
			r.start = block_offset;
			r.length = block_size;

			assert(verify_piece(r));
			write_request(r);
			
			using namespace boost::posix_time;

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << to_simple_string(second_clock::universal_time())
				<< " ==> REQUEST [ "
				"piece: " << block.piece_index << " | "
				"b: " << block.block_index << " | "
				"s: " << block_offset << " | "
				"l: " << block_size << " | "
				"ds: " << statistics().download_rate() << " | "
				"qs: " << m_desired_queue_size << " ]\n";
#endif
		}
		m_last_piece = second_clock::universal_time();
	}


	void close_socket_ignore_error(boost::shared_ptr<stream_socket> s)
	{
		s->close(asio::ignore_error());
	}

	void peer_connection::disconnect()
	{
		if (m_disconnecting) return;
		m_disconnecting = true;
		m_ses.m_selector.post(boost::bind(&close_socket_ignore_error, m_socket));

		boost::shared_ptr<torrent> t = m_torrent.lock();

		if (t)
		{
			piece_picker& picker = t->picker();
			
			while (!m_download_queue.empty())
			{
				picker.abort_download(m_download_queue.back());
				m_download_queue.pop_back();
			}
			while (!m_request_queue.empty())
			{
				picker.abort_download(m_request_queue.back());
				m_request_queue.pop_back();
			}

			t->remove_peer(this);
		}

		m_ses.close_connection(self());
	}

	size_type peer_connection::share_diff() const
	{
		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		float ratio = t->ratio();

		// if we have an infinite ratio, just say we have downloaded
		// much more than we have uploaded. And we'll keep uploading.
		if (ratio == 0.f)
			return std::numeric_limits<size_type>::max();

		return m_free_upload
			+ static_cast<size_type>(m_statistics.total_payload_download() * ratio)
			- m_statistics.total_payload_upload();
	}

	void peer_connection::cut_receive_buffer(int size, int packet_size)
	{
		assert((int)m_recv_buffer.size() >= size);

		std::copy(m_recv_buffer.begin() + size, m_recv_buffer.begin() + m_recv_pos, m_recv_buffer.begin());

		assert(m_recv_pos >= size);
		m_recv_pos -= size;

#ifndef NDEBUG
		std::fill(m_recv_buffer.begin() + m_recv_pos, m_recv_buffer.end(), 0);
#endif

		m_packet_size = packet_size;
		m_recv_buffer.resize(m_packet_size);
	}

	void peer_connection::second_tick()
	{
		INVARIANT_CHECK;

		ptime now(second_clock::universal_time());

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);
		
		// calculate the desired download queue size
		const float queue_time = m_ses.m_settings.request_queue_time;
		// (if the latency is more than this, the download will stall)
		// so, the queue size is queue_time * down_rate / 16 kiB
		// (16 kB is the size of each request)
		// the minimum number of requests is 2 and the maximum is 48
		// the block size doesn't have to be 16. So we first query the
		// torrent for it
		const int block_size = t->block_size();
		assert(block_size > 0);
		
		int m_desired_queue_size = static_cast<int>(queue_time
			* statistics().download_rate() / block_size);
		if (m_desired_queue_size > max_request_queue) m_desired_queue_size
			= max_request_queue;
		if (m_desired_queue_size < min_request_queue) m_desired_queue_size
			= min_request_queue;

		
		// TODO: the timeout should be user-settable
		if (!m_download_queue.empty()
			&& now - m_last_piece > seconds(m_ses.m_settings.piece_timeout))
		{
			// this peer isn't sending the pieces we've
			// requested (this has been observed by BitComet)
			// in this case we'll clear our download queue and
			// re-request the blocks.
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << to_simple_string(now)
				<< " *** PIECE_REQUESTS TIMED OUT [ " << (int)m_download_queue.size()
				<< " " << to_simple_string(now - m_last_piece) << "] ***\n";
#endif

			piece_picker& picker = t->picker();
			for (std::deque<piece_block>::const_iterator i = m_download_queue.begin()
				, end(m_download_queue.end()); i != end; ++i)
			{
				// since this piece was skipped, clear it and allow it to
				// be requested from other peers
				picker.abort_download(*i);
			}
			for (std::deque<piece_block>::const_iterator i = m_request_queue.begin()
				, end(m_request_queue.end()); i != end; ++i)
			{
				// since this piece was skipped, clear it and allow it to
				// be requested from other peers
				picker.abort_download(*i);
			}

			m_download_queue.clear();
			m_request_queue.clear();
			
//			m_assume_fifo = true;

			// this will trigger new picking of pieces
			t->get_policy().unchoked(*this);
		}
	
		on_tick();
		
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

		if (t->is_seed() || is_choked() || t->ratio() == 0.0f)
		{
			// if we have downloaded more than one piece more
			// than we have uploaded OR if we are a seed
			// have an unlimited upload rate
			if(!m_send_buffer[m_current_send_buffer].empty()
				|| (!m_requests.empty() && !is_choked()))
				m_ul_bandwidth_quota.max = resource_request::inf;
			else
				m_ul_bandwidth_quota.max = m_ul_bandwidth_quota.min;
		}
		else
		{
			size_type bias = 0x10000+2*t->block_size() + m_free_upload;

			double break_even_time = 15; // seconds.
			size_type have_uploaded = m_statistics.total_payload_upload();
			size_type have_downloaded = m_statistics.total_payload_download();
			double download_speed = m_statistics.download_rate();

			size_type soon_downloaded =
				have_downloaded + (size_type)(download_speed * break_even_time*1.5);

			if(t->ratio() != 1.f)
				soon_downloaded = (size_type)(soon_downloaded*(double)t->ratio());

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

		fill_send_buffer();
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

	void peer_connection::fill_send_buffer()
	{
		if (!can_write()) return;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		// only add new piece-chunks if the send buffer is small enough
		// otherwise there will be no end to how large it will be!
		// TODO: the buffer size should probably be dependent on the transfer speed
		while (!m_requests.empty()
			&& (send_buffer_size() < t->block_size() * 6)
			&& !m_choked)
		{
			assert(t->valid_metadata());
			peer_request& r = m_requests.front();
			
			assert(r.piece >= 0);
			assert(r.piece < (int)m_have_piece.size());
			assert(t->have_piece(r.piece));
			assert(r.start + r.length <= t->torrent_file().piece_size(r.piece));
			assert(r.length > 0 && r.start >= 0);

			write_piece(r);

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> PIECE   [ piece: " << r.piece << " | s: " << r.start
			<< " | l: " << r.length << " ]\n";
#endif

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
	}

	void peer_connection::setup_send()
	{
		if (m_writing) return;
		if (!can_write()) return;

		assert(!m_writing);

		// send the actual buffer
		if (!m_send_buffer[m_current_send_buffer].empty())
		{
			int amount_to_send
				= std::min(m_ul_bandwidth_quota.left()
				, (int)m_send_buffer[m_current_send_buffer].size());

			assert(amount_to_send > 0);

			buffer::interval_type send_buffer
				= m_send_buffer[m_current_send_buffer].data();
			// swap the send buffer for the double buffered effect
			m_current_send_buffer = (m_current_send_buffer + 1) & 1;

			// we have data that's scheduled for sending
			int to_send = std::min(int(send_buffer.first.end - send_buffer.first.begin)
				, amount_to_send);

			boost::array<asio::const_buffer, 2> bufs;
			assert(to_send >= 0);
			bufs[0] = asio::buffer(send_buffer.first.begin, to_send);

			to_send = std::min(int(send_buffer.second.end - send_buffer.second.begin)
				, amount_to_send - to_send);
			assert(to_send >= 0);
			bufs[1] = asio::buffer(send_buffer.second.begin, to_send);

			assert(m_ul_bandwidth_quota.left() >= int(buffer_size(bufs[0]) + buffer_size(bufs[1])));
			m_socket->async_write_some(bufs, bind(&peer_connection::on_send_data
				, self(), _1, _2));
			m_writing = true;
			m_last_write_size = amount_to_send;
			m_ul_bandwidth_quota.used += m_last_write_size;
		}
	}

	void peer_connection::setup_receive()
	{
		if (m_reading) return;
		if (!can_read()) return;

		assert(m_packet_size > 0);
		int max_receive = std::min(
			m_dl_bandwidth_quota.left()
			, m_packet_size - m_recv_pos);

		assert(m_recv_pos >= 0);
		assert(m_packet_size > 0);
		assert(m_dl_bandwidth_quota.left() > 0);
		assert(max_receive > 0);

		assert(can_read());
		m_socket->async_read_some(asio::buffer(&m_recv_buffer[m_recv_pos]
			, max_receive), bind(&peer_connection::on_receive_data, self(), _1, _2));
		m_reading = true;
		m_last_read_size = max_receive;
		m_dl_bandwidth_quota.used += max_receive;
		assert(m_dl_bandwidth_quota.used <= m_dl_bandwidth_quota.given);
	}

	void peer_connection::reset_recv_buffer(int packet_size)
	{
		assert(packet_size > 0);
		m_recv_pos = 0;
		m_packet_size = packet_size;
		m_recv_buffer.resize(m_packet_size);
	}
	
	void peer_connection::send_buffer(char const* begin, char const* end)
	{
		m_send_buffer[m_current_send_buffer].insert(begin, end);
		setup_send();
	}

// TODO: change this interface to automatically call setup_send() when the
// return value is destructed
	buffer::interval peer_connection::allocate_send_buffer(int size)
	{
		return m_send_buffer[m_current_send_buffer].allocate(size);
	}

	template<class T>
	struct set_to_zero
	{
		set_to_zero(T& v, bool cond): m_val(v), m_cond(cond) {}
		void fire() { if (!m_cond) return; m_cond = false; m_val = 0; }
		~set_to_zero() { if (m_cond) m_val = 0; }
	private:
		T& m_val;
		bool m_cond;
	};


	// --------------------------
	// RECEIVE DATA
	// --------------------------

	// throws exception when the client should be disconnected
	void peer_connection::on_receive_data(const asio::error& error
		, std::size_t bytes_transferred) try
	{
		INVARIANT_CHECK;

		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		assert(m_reading);
		assert(m_last_read_size > 0);
		m_reading = false;
		// correct the dl quota usage, if not all of the buffer was actually read
		m_dl_bandwidth_quota.used -= m_last_read_size - bytes_transferred;
		m_last_read_size = 0;

		if (error)
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << "**ERROR**: " << error.what() << "\n";
#endif
			on_receive(error, bytes_transferred);
			throw std::runtime_error(error.what());
		}

		if (m_disconnecting) return;
	
		assert(m_packet_size > 0);
		assert(bytes_transferred > 0);

		m_last_receive = second_clock::universal_time();
		m_recv_pos += bytes_transferred;

		// this will reset the m_recv_pos to 0 if the
		// entire packet was received
		// it is important that this is done before
		// setup_receive() is called. Therefore, fire() is
		// called before setup_receive().
		assert(m_recv_pos <= m_packet_size);
		set_to_zero<int> reset(m_recv_pos, m_recv_pos == m_packet_size);
		
		on_receive(error, bytes_transferred);

		assert(m_packet_size > 0);

		// do the reset immediately
		reset.fire();

		setup_receive();	
	}
	catch (std::exception& e)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		m_ses.connection_failed(m_socket, remote(), e.what());
//		disconnect();
	}
	catch (...)
	{
		// all exceptions should derive from std::exception
		assert(false);
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		m_ses.connection_failed(m_socket, remote(), "connection failed for unkown reason");
//		disconnect();
	}

	bool peer_connection::can_write() const
	{
		// if we have requests or pending data to be sent or announcements to be made
		// we want to send data
		return ((!m_requests.empty() && !m_choked)
			|| !m_send_buffer[m_current_send_buffer].empty())
			&& m_ul_bandwidth_quota.left() > 0
			&& !m_connecting;
	}

	bool peer_connection::can_read() const
	{
		return m_dl_bandwidth_quota.left() > 0 && !m_connecting;
	}

	void peer_connection::connect()
	{
		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_ses.m_logger) << "CONNECTING: " << m_remote.address().to_string() << "\n";
#endif

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		m_queued = false;
		assert(m_connecting);
		m_socket->open(asio::ipv4::tcp());
		m_socket->bind(t->get_interface());
		m_socket->async_connect(m_remote
			, bind(&peer_connection::on_connection_complete, self(), _1));

		if (t->alerts().should_post(alert::debug))
		{
			t->alerts().post_alert(peer_error_alert(
				m_remote, m_peer_id, "connecting to peer"));
		}
	}
	
	void peer_connection::on_connection_complete(asio::error const& e) try
	{
		INVARIANT_CHECK;

		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		if (e)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_ses.m_logger) << "CONNECTION FAILED: " << m_remote.address().to_string() << "\n";
#endif
			m_ses.connection_failed(m_socket, m_remote, e.what());
//			disconnect();
			return;
		}

		// this means the connection just succeeded

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_ses.m_logger) << "COMPLETED: " << m_remote.address().to_string() << "\n";
#endif

		m_connecting = false;
		m_ses.connection_completed(self());
		on_connected();
	}
	catch (std::exception& ex)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		m_ses.connection_failed(m_socket, remote(), ex.what());
	}
	catch (...)
	{
		// all exceptions should derive from std::exception
		assert(false);
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		m_ses.connection_failed(m_socket, remote(), "connection failed for unkown reason");
	}
	
	// --------------------------
	// SEND DATA
	// --------------------------

	// throws exception when the client should be disconnected
	void peer_connection::on_send_data(asio::error const& error
		, std::size_t bytes_transferred) try
	{
		INVARIANT_CHECK;

		assert(m_writing);
		assert(m_last_write_size > 0);
		m_writing = false;
		// correct the ul quota usage, if not all of the buffer was sent
		m_ul_bandwidth_quota.used -= m_last_write_size - bytes_transferred;
		m_last_write_size = 0;

		if (error)
			throw std::runtime_error(error.what());
		if (m_disconnecting) return;

		assert(!m_connecting);
//		assert(bytes_transferred > 0);

		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		int sending_buffer = (m_current_send_buffer + 1) & 1;
		m_send_buffer[sending_buffer].erase(bytes_transferred);

		m_last_sent = second_clock::universal_time();

		on_sent(error, bytes_transferred);
		fill_send_buffer();
		if (!m_send_buffer[sending_buffer].empty())
		{
			// if the send operation didn't send all of the data in the buffer.
			// send it again.
			m_current_send_buffer = sending_buffer;
		}
		setup_send();
	}
	catch (std::exception& e)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		m_ses.connection_failed(m_socket, remote(), e.what());
	}
	catch (...)
	{
		// all exceptions should derive from std::exception
		assert(false);
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		m_ses.connection_failed(m_socket, remote(), "connection failed for unkown reason");
	}


#ifndef NDEBUG
	void peer_connection::check_invariant() const
	{
		assert(m_num_pieces == std::count(
			m_have_piece.begin()
			, m_have_piece.end()
			, true));
	}
#endif

	bool peer_connection::has_timed_out() const
	{
		// TODO: the timeout should be set by an event rather

		using namespace boost::posix_time;

		ptime now(second_clock::universal_time());
		
		// if the socket is still connecting, don't
		// consider it timed out. Because Windows XP SP2
		// may delay connection attempts.
		if (m_connecting) return false;
		
		// if the peer hasn't said a thing for a certain
		// time, it is considered to have timed out
		time_duration d;
		d = second_clock::universal_time() - m_last_receive;
		if (d > seconds(m_timeout)) return true;

		// if the peer hasn't become interested and we haven't
		// become interested in the peer for 10 minutes, it
		// has also timed out.
		time_duration d1;
		time_duration d2;
		d1 = now - m_became_uninterested;
		d2 = now - m_became_uninteresting;
		// TODO: these timeouts should be user settable
		if (!m_interesting
			&& !m_peer_interested
			&& d1 > minutes(10)
			&& d2 > minutes(10))
		{
			return true;
		}

		return false;
	}


	void peer_connection::keep_alive()
	{
		INVARIANT_CHECK;

		boost::posix_time::time_duration d;
		d = second_clock::universal_time() - m_last_sent;
		if (d.total_seconds() < m_timeout / 2) return;

		if (m_connecting) return;

		write_keepalive();
	}

	bool peer_connection::is_seed() const
	{
		// if m_num_pieces == 0, we probably doesn't have the
		// metadata yet.
		return m_num_pieces == (int)m_have_piece.size() && m_num_pieces > 0;
	}
}

