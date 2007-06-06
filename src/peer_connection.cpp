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

#include "libtorrent/pch.hpp"

#include <vector>
#include <iostream>
#include <iomanip>
#include <limits>
#include <boost/bind.hpp>

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/policy.hpp"
#include "libtorrent/socket_type.hpp"

using boost::bind;
using boost::shared_ptr;
using libtorrent::aux::session_impl;

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
		if (--c->m_refs == 0)
			delete c;
	}

	// outbound connection
	peer_connection::peer_connection(
		session_impl& ses
		, boost::weak_ptr<torrent> tor
		, shared_ptr<socket_type> s
		, tcp::endpoint const& remote
		, policy::peer* peerinfo)
		:
#ifndef NDEBUG
		m_last_choke(time_now() - hours(1))
		,
#endif
		  m_ses(ses)
		, m_max_out_request_queue(m_ses.settings().max_out_request_queue)
		, m_timeout(m_ses.settings().peer_timeout)
		, m_last_piece(time_now())
		, m_last_request(time_now())
		, m_packet_size(0)
		, m_recv_pos(0)
		, m_current_send_buffer(0)
		, m_write_pos(0)
		, m_last_receive(time_now())
		, m_last_sent(time_now())
		, m_socket(s)
		, m_remote(remote)
		, m_torrent(tor)
		, m_active(true)
		, m_peer_interested(false)
		, m_peer_choked(true)
		, m_interesting(false)
		, m_choked(true)
		, m_failed(false)
		, m_ignore_bandwidth_limits(false)
		, m_num_pieces(0)
		, m_desired_queue_size(2)
		, m_free_upload(0)
		, m_assume_fifo(false)
		, m_num_invalid_requests(0)
		, m_disconnecting(false)
		, m_became_uninterested(time_now())
		, m_became_uninteresting(time_now())
		, m_connecting(true)
		, m_queued(true)
		, m_writing(false)
		, m_reading(false)
		, m_prefer_whole_pieces(false)
		, m_request_large_blocks(false)
		, m_non_prioritized(false)
		, m_refs(0)
		, m_upload_limit(resource_request::inf)
		, m_download_limit(resource_request::inf)
		, m_peer_info(peerinfo)
		, m_speed(slow)
		, m_connection_ticket(-1)
#ifndef NDEBUG
		, m_in_constructor(true)
#endif
	{
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		std::fill(m_country, m_country + 2, 0);
#endif
#ifdef TORRENT_VERBOSE_LOGGING
		m_logger = m_ses.create_log(m_remote.address().to_string() + "_"
			+ boost::lexical_cast<std::string>(m_remote.port()), m_ses.listen_port());
		(*m_logger) << "*** OUTGOING CONNECTION\n";
#endif

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);
		std::fill(m_peer_id.begin(), m_peer_id.end(), 0);

		if (t->ready_for_connections())
			init();
	}

	// incoming connection
	peer_connection::peer_connection(
		session_impl& ses
		, boost::shared_ptr<socket_type> s
		, policy::peer* peerinfo)
		:
#ifndef NDEBUG
		m_last_choke(time_now() - hours(1))
		,
#endif
		  m_ses(ses)
		, m_max_out_request_queue(m_ses.settings().max_out_request_queue)
		, m_timeout(m_ses.settings().peer_timeout)
		, m_last_piece(time_now())
		, m_last_request(time_now())
		, m_packet_size(0)
		, m_recv_pos(0)
		, m_current_send_buffer(0)
		, m_write_pos(0)
		, m_last_receive(time_now())
		, m_last_sent(time_now())
		, m_socket(s)
		, m_active(false)
		, m_peer_interested(false)
		, m_peer_choked(true)
		, m_interesting(false)
		, m_choked(true)
		, m_failed(false)
		, m_ignore_bandwidth_limits(false)
		, m_num_pieces(0)
		, m_desired_queue_size(2)
		, m_free_upload(0)
		, m_assume_fifo(false)
		, m_num_invalid_requests(0)
		, m_disconnecting(false)
		, m_became_uninterested(time_now())
		, m_became_uninteresting(time_now())
		, m_connecting(false)
		, m_queued(false)
		, m_writing(false)
		, m_reading(false)
		, m_prefer_whole_pieces(false)
		, m_request_large_blocks(false)
		, m_non_prioritized(false)
		, m_refs(0)
		, m_upload_limit(resource_request::inf)
		, m_download_limit(resource_request::inf)
		, m_peer_info(peerinfo)
		, m_speed(slow)
#ifndef NDEBUG
		, m_in_constructor(true)
#endif
	{
		tcp::socket::non_blocking_io ioc(true);
		m_socket->io_control(ioc);
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		std::fill(m_country, m_country + 2, 0);
#endif
		m_remote = m_socket->remote_endpoint();

#ifdef TORRENT_VERBOSE_LOGGING
		assert(m_socket->remote_endpoint() == remote());
		m_logger = m_ses.create_log(remote().address().to_string() + "_"
			+ boost::lexical_cast<std::string>(remote().port()), m_ses.listen_port());
		(*m_logger) << "*** INCOMING CONNECTION\n";
#endif
		
		std::fill(m_peer_id.begin(), m_peer_id.end(), 0);
	}

	void peer_connection::update_interest()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		bool interested = false;
		const std::vector<bool>& we_have = t->pieces();
		for (int j = 0; j != (int)we_have.size(); ++j)
		{
			if (!we_have[j]
				&& t->piece_priority(j) > 0
				&& m_have_piece[j])
			{
				interested = true;
				break;
			}
		}
		try
		{
			if (!interested)
				send_not_interested();
			else
				t->get_policy().peer_is_interesting(*this);
		}
		// may throw an asio error if socket has disconnected
		catch (std::exception& e) {}

		assert(is_interesting() == interested);
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void peer_connection::add_extension(boost::shared_ptr<peer_plugin> ext)
	{
		m_extensions.push_back(ext);
	}
#endif

	void peer_connection::init()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);
		assert(t->valid_metadata());
		assert(t->ready_for_connections());

		m_have_piece.resize(t->torrent_file().num_pieces(), false);

		// now that we have a piece_picker,
		// update it with this peers pieces

		int num_pieces = std::count(m_have_piece.begin(), m_have_piece.end(), true);
		if (num_pieces == int(m_have_piece.size()))
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " *** THIS IS A SEED ***\n";
#endif
			// if this is a web seed. we don't have a peer_info struct
			if (m_peer_info) m_peer_info->seed = true;
			// if we're a seed too, disconnect
			if (t->is_seed())
			{
				throw std::runtime_error("seed to seed connection redundant, disconnecting");
			}
			m_num_pieces = num_pieces;
			t->peer_has_all();
			if (!t->is_finished())
				t->get_policy().peer_is_interesting(*this);
			return;
		}

		m_num_pieces = num_pieces;
		// if we're a seed, we don't keep track of piece availability
		if (!t->is_seed())
		{
			bool interesting = false;
			for (int i = 0; i < int(m_have_piece.size()); ++i)
			{
				if (m_have_piece[i])
				{
					t->peer_has(i);
					// if the peer has a piece and we don't, the peer is interesting
					if (!t->have_piece(i)
						&& t->picker().piece_priority(i) != 0)
						interesting = true;
				}
			}
			if (interesting)
				t->get_policy().peer_is_interesting(*this);
		}
	}

	peer_connection::~peer_connection()
	{
//		INVARIANT_CHECK;
		assert(m_disconnecting);

#ifdef TORRENT_VERBOSE_LOGGING
		if (m_logger)
		{
			(*m_logger) << time_now_string()
				<< " *** CONNECTION CLOSED\n";
		}
#endif
#ifndef NDEBUG
		if (m_peer_info)
			assert(m_peer_info->connection == 0);

		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (t) assert(t->connection_for(remote()) != this);
#endif
	}

	void peer_connection::announce_piece(int index)
	{
		// dont announce during handshake
		if (in_handshake()) return;
		
		// optimization, don't send have messages
		// to peers that already have the piece
		if (!m_ses.settings().send_redundant_have
			&& has_piece(index)) return;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
			<< " ==> HAVE    [ piece: " << index << "]\n";
#endif
		write_have(index);
#ifndef NDEBUG
		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);
		assert(t->have_piece(index));
#endif
	}

	bool peer_connection::has_piece(int i) const
	{
		INVARIANT_CHECK;

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
		INVARIANT_CHECK;

		m_statistics.add_stat(downloaded, uploaded);
	}

	std::vector<bool> const& peer_connection::get_bitfield() const
	{
		return m_have_piece;
	}

	void peer_connection::received_valid_data(int index)
	{
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			try { (*i)->on_piece_pass(index); } catch (std::exception&) {}
		}
#endif

		if (peer_info_struct())
		{
			peer_info_struct()->on_parole = false;
			int& trust_points = peer_info_struct()->trust_points;
			trust_points++;
			// TODO: make this limit user settable
			if (trust_points > 20) trust_points = 20;
		}
	}

	void peer_connection::received_invalid_data(int index)
	{
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			try { (*i)->on_piece_failed(index); } catch (std::exception&) {}
		}
#endif

		if (peer_info_struct())
		{
			peer_info_struct()->on_parole = true;
			++peer_info_struct()->hashfails;
			int& trust_points = peer_info_struct()->trust_points;

			// we decrease more than we increase, to keep the
			// allowed failed/passed ratio low.
			// TODO: make this limit user settable
			trust_points -= 2;
			if (trust_points < -7) trust_points = -7;
		}
	}
	
	size_type peer_connection::total_free_upload() const
	{
		return m_free_upload;
	}

	void peer_connection::add_free_upload(size_type free_upload)
	{
		INVARIANT_CHECK;

		m_free_upload += free_upload;
	}

	// verifies a piece to see if it is valid (is within a valid range)
	// and if it can correspond to a request generated by libtorrent.
	bool peer_connection::verify_piece(const peer_request& p) const
	{
		INVARIANT_CHECK;

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
					&& p.start + p.length == t->torrent_file().piece_size(p.piece))
				|| (m_request_large_blocks
					&& p.length <= t->torrent_file().piece_size(p.piece)))
			&& p.start + p.length <= t->torrent_file().piece_size(p.piece)
			&& (p.start % t->block_size() == 0);
	}
	
	struct disconnect_torrent
	{
		disconnect_torrent(boost::weak_ptr<torrent>& t): m_t(&t) {}
		~disconnect_torrent() { if (m_t) m_t->reset(); }
		void cancel() { m_t = 0; }
	private:
		boost::weak_ptr<torrent>* m_t;
	};
	
	void peer_connection::attach_to_torrent(sha1_hash const& ih)
	{
		INVARIANT_CHECK;

		assert(!m_disconnecting);
		m_torrent = m_ses.find_torrent(ih);

		boost::shared_ptr<torrent> t = m_torrent.lock();

		if (t && t->is_aborted())
		{
			m_torrent.reset();
			t.reset();
		}

		if (!t)
		{
			// we couldn't find the torrent!
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " couldn't find a torrent with the given info_hash\n";
#endif
			throw std::runtime_error("got info-hash that is not in our session");
		}

		disconnect_torrent disconnect(m_torrent);
		if (t->is_paused())
		{
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
		assert(m_num_pieces == 0);
		std::fill(m_have_piece.begin(), m_have_piece.end(), false);
		disconnect.cancel();
	}

	// message handlers

	// -----------------------------
	// --------- KEEPALIVE ---------
	// -----------------------------

	void peer_connection::incoming_keepalive()
	{
		INVARIANT_CHECK;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " <== KEEPALIVE\n";
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

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_choke()) return;
		}
#endif

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " <== CHOKE\n";
#endif
		m_peer_choked = true;
		t->get_policy().choked(*this);
		
		if (!t->is_seed())
		{
			piece_picker& p = t->picker();
			// remove all pieces from this peers download queue and
			// remove the 'downloading' flag from piece_picker.
			for (std::deque<piece_block>::iterator i = m_download_queue.begin();
				i != m_download_queue.end(); ++i)
			{
				p.abort_download(*i);
			}
			for (std::deque<piece_block>::const_iterator i = m_request_queue.begin()
				, end(m_request_queue.end()); i != end; ++i)
			{
				// since this piece was skipped, clear it and allow it to
				// be requested from other peers
				p.abort_download(*i);
			}
		}
		
		m_download_queue.clear();
		m_request_queue.clear();
	}

	// -----------------------------
	// ---------- UNCHOKE ----------
	// -----------------------------

	void peer_connection::incoming_unchoke()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_unchoke()) return;
		}
#endif

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " <== UNCHOKE\n";
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

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_interested()) return;
		}
#endif

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " <== INTERESTED\n";
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

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_not_interested()) return;
		}
#endif

		m_became_uninterested = time_now();

		// clear the request queue if the client isn't interested
		m_requests.clear();
//		setup_send();

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " <== NOT_INTERESTED\n";
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

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_have(index)) return;
		}
#endif

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
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
			// we have the metadata and if
			// we're not a seed (in which case
			// we won't have a piece picker)
			if (t->valid_metadata())
			{
				++m_num_pieces;
				t->peer_has(index);

				if (!t->have_piece(index)
					&& !t->is_seed()
					&& !is_interesting()
					&& t->picker().piece_priority(index) != 0)
					t->get_policy().peer_is_interesting(*this);
			}

			if (is_seed())
			{
				assert(m_peer_info);
				m_peer_info->seed = true;
				if (t->is_seed())
				{
					throw protocol_error("seed to seed connection redundant, disconnecting");
				}
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

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_bitfield(bitfield)) return;
		}
#endif

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " <== BITFIELD ";

		for (int i = 0; i < int(bitfield.size()); ++i)
		{
			if (bitfield[i]) (*m_logger) << "1";
			else (*m_logger) << "0";
		}
		(*m_logger) << "\n";
#endif

		// if we don't have the metedata, we cannot
		// verify the bitfield size
		if (t->valid_metadata()
			&& (bitfield.size() / 8) != (m_have_piece.size() / 8))
			throw protocol_error("got bitfield with invalid size: "
				+ boost::lexical_cast<std::string>(bitfield.size() / 8)
				+ "bytes. expected: "
				+ boost::lexical_cast<std::string>(m_have_piece.size() / 8)
				+ "bytes");

		// if we don't have metadata yet
		// just remember the bitmask
		// don't update the piecepicker
		// (since it doesn't exist yet)
		if (!t->ready_for_connections())
		{
			m_have_piece = bitfield;
			m_num_pieces = std::count(bitfield.begin(), bitfield.end(), true);

			if (m_peer_info) m_peer_info->seed = true;
			return;
		}

		int num_pieces = std::count(bitfield.begin(), bitfield.end(), true);
		if (num_pieces == int(m_have_piece.size()))
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " *** THIS IS A SEED ***\n";
#endif
			// if this is a web seed. we don't have a peer_info struct
			if (m_peer_info) m_peer_info->seed = true;
			// if we're a seed too, disconnect
			if (t->is_seed())
			{
				throw protocol_error("seed to seed connection redundant, disconnecting");
			}

			std::fill(m_have_piece.begin(), m_have_piece.end(), true);
			m_num_pieces = num_pieces;
			t->peer_has_all();
			if (!t->is_finished())
				t->get_policy().peer_is_interesting(*this);
			return;
		}

		// let the torrent know which pieces the
		// peer has
		// if we're a seed, we don't keep track of piece availability
		if (!t->is_seed())
		{
			bool interesting = false;
			for (int i = 0; i < (int)m_have_piece.size(); ++i)
			{
				bool have = bitfield[i];
				if (have && !m_have_piece[i])
				{
					m_have_piece[i] = true;
					++m_num_pieces;
					t->peer_has(i);
					if (!t->have_piece(i) && t->picker().piece_priority(i) != 0)
						interesting = true;
				}
				else if (!have && m_have_piece[i])
				{
					// this should probably not be allowed
					m_have_piece[i] = false;
					--m_num_pieces;
					t->peer_lost(i);
				}
			}

			if (interesting) t->get_policy().peer_is_interesting(*this);
		}
		else
		{
			for (int i = 0; i < (int)m_have_piece.size(); ++i)
			{
				bool have = bitfield[i];
				if (have && !m_have_piece[i])
				{
					m_have_piece[i] = true;
					++m_num_pieces;
				}
				else if (!have && m_have_piece[i])
				{
					// this should probably not be allowed
					m_have_piece[i] = false;
					--m_num_pieces;
				}
			}
		}
	}

	// -----------------------------
	// ---------- REQUEST ----------
	// -----------------------------

	void peer_connection::incoming_request(peer_request const& r)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_request(r)) return;
		}
#endif

		if (!t->valid_metadata())
		{
			// if we don't have valid metadata yet,
			// we shouldn't get a request
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
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

		if (int(m_requests.size()) > m_ses.settings().max_allowed_in_request_queue)
		{
			// don't allow clients to abuse our
			// memory consumption.
			// ignore requests if the client
			// is making too many of them.
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
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
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " <== REQUEST [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif
			// if we have choked the client
			// ignore the request
			if (m_choked)
				return;

			m_requests.push_back(r);
			fill_send_buffer();
		}
		else
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " <== INVALID_REQUEST [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " | "
				"i: " << m_peer_interested << " | "
				"t: " << (int)t->torrent_file().piece_size(r.piece) << " | "
				"n: " << t->torrent_file().num_pieces() << " | "
				"h: " << t->have_piece(r.piece) << " ]\n";
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
		m_last_piece = time_now();
	}

#ifndef NDEBUG
	struct check_postcondition
	{
		check_postcondition(boost::shared_ptr<torrent> const& t_
			, bool init_check = true): t(t_) { if (init_check) check(); }
	
		~check_postcondition() { check(); }
		
		void check()
		{
			if (!t->is_seed())
			{
				const int blocks_per_piece = static_cast<int>(
					t->torrent_file().piece_length() / t->block_size());

				std::vector<piece_picker::downloading_piece> const& dl_queue
					= t->picker().get_download_queue();

				for (std::vector<piece_picker::downloading_piece>::const_iterator i =
					dl_queue.begin(); i != dl_queue.end(); ++i)
				{
					assert(i->finished < blocks_per_piece);
				}
			}
		}
		
		shared_ptr<torrent> t;
	};
#endif


	// -----------------------------
	// ----------- PIECE -----------
	// -----------------------------

	void peer_connection::incoming_piece(peer_request const& p, char const* data)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_piece(p, data)) return;
		}
#endif

#ifndef NDEBUG
		check_postcondition post_checker_(t);
		t->check_invariant();
#endif

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
			<< " <== PIECE   [ piece: " << p.piece << " | "
			"s: " << p.start << " | "
			"l: " << p.length << " | "
			"ds: " << statistics().download_rate() << " | "
			"qs: " << m_desired_queue_size << " ]\n";
#endif

		if (!verify_piece(p))
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " <== INVALID_PIECE [ piece: " << p.piece << " | "
				"start: " << p.start << " | "
				"length: " << p.length << " ]\n";
#endif
			throw protocol_error("got invalid piece packet");
		}

		// if we're already seeding, don't bother,
		// just ignore it
		if (t->is_seed())
		{
			t->received_redundant_data(p.length);
			return;
		}

		piece_picker& picker = t->picker();
		piece_manager& fs = t->filesystem();
		policy& pol = t->get_policy();

		std::vector<piece_block> finished_blocks;
		piece_block block_finished(p.piece, p.start / t->block_size());
		assert(p.start % t->block_size() == 0);
		assert(p.length == t->block_size()
			|| p.length == t->torrent_file().total_size() % t->block_size());

		std::deque<piece_block>::iterator b
			= std::find(
				m_download_queue.begin()
				, m_download_queue.end()
				, block_finished);

		// if there's another peer that needs to do another
		// piece request, this will point to it
		peer_connection* request_peer = 0;

		if (b != m_download_queue.end())
		{
			if (m_assume_fifo)
			{
				for (std::deque<piece_block>::iterator i = m_download_queue.begin();
					i != b; ++i)
				{
#ifdef TORRENT_VERBOSE_LOGGING
					(*m_logger) << time_now_string()
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
		}
		else
		{
			// cancel the block from the
			// peer that has taken over it.
			boost::optional<tcp::endpoint> peer
				= t->picker().get_downloader(block_finished);
			if (peer)
			{
				assert(!t->picker().is_finished(block_finished));
				peer_connection* pc = t->connection_for(*peer);
				if (pc && pc != this)
				{
					pc->cancel_request(block_finished);
					request_peer = pc;
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
			t->received_redundant_data(t->block_size());
			pol.block_finished(*this, block_finished);
			send_block_requests();

			if (request_peer && !request_peer->has_peer_choked() && !t->is_seed())
			{
				request_a_block(*t, *request_peer);
				request_peer->send_block_requests();
			}
			return;
		}
		
		fs.write(data, p.piece, p.start, p.length);

		picker.mark_as_finished(block_finished, m_remote);

		try
		{
			pol.block_finished(*this, block_finished);
			send_block_requests();
		}
		catch (std::exception const&) {}

		if (request_peer && !request_peer->has_peer_choked() && !t->is_seed())
		{
			request_a_block(*t, *request_peer);
			request_peer->send_block_requests();
		}

#ifndef NDEBUG
		try
		{
#endif

		bool was_seed = t->is_seed();
		bool was_finished = picker.num_filtered() + t->num_pieces()
			== t->torrent_file().num_pieces();

		// did we just finish the piece?
		if (picker.is_piece_finished(p.piece))
		{
#ifndef NDEBUG
			check_postcondition post_checker2_(t, false);
#endif
			bool verified = t->verify_piece(p.piece);
			if (verified)
			{
				// the following call may cause picker to become invalid
				// in case we just became a seed
				t->announce_piece(p.piece);
				assert(t->valid_metadata());
				// if we just became a seed, picker is now invalid, since it
				// is deallocated by the torrent once it starts seeding
				if (!was_finished
					&& (t->is_seed()
						|| picker.num_filtered() + t->num_pieces()
						== t->torrent_file().num_pieces()))
				{
					// torrent finished
					// i.e. all the pieces we're interested in have
					// been downloaded. Release the files (they will open
					// in read only mode if needed)
					try { t->finished(); }
					catch (std::exception& e)
					{
#ifndef NDEBUG
						std::cerr << e.what() << std::endl;
						assert(false);
#endif
					}
				}
			}
			else
			{
				t->piece_failed(p.piece);
			}

#ifndef NDEBUG
			try
			{
#endif

			pol.piece_finished(p.piece, verified);

#ifndef NDEBUG
			}
			catch (std::exception const& e)
			{
				std::cerr << e.what() << std::endl;
				assert(false);
			}
#endif

#ifndef NDEBUG
			try
			{
#endif

			if (!was_seed && t->is_seed())
			{
				assert(verified);
				t->completed();
			}

#ifndef NDEBUG
			}
			catch (std::exception const& e)
			{
				std::cerr << e.what() << std::endl;
				assert(false);
			}
#endif

		}

#ifndef NDEBUG
		}
		catch (std::exception const& e)
		{
			std::cerr << e.what() << std::endl;
			assert(false);
		}
#endif
	}

	// -----------------------------
	// ---------- CANCEL -----------
	// -----------------------------

	void peer_connection::incoming_cancel(peer_request const& r)
	{
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_cancel(r)) return;
		}
#endif

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
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
			(*m_logger) << time_now_string() << " *** GOT CANCEL NOT IN THE QUEUE\n";
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
		(*m_logger) << time_now_string()
			<< " <== DHT_PORT [ p: " << listen_port << " ]\n";
#endif
#ifndef TORRENT_DISABLE_DHT
		m_ses.add_dht_node(udp::endpoint(
			m_remote.address(), listen_port));
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

		piece_picker::piece_state_t state;
		peer_speed_t speed = peer_speed();
		if (speed == fast) state = piece_picker::fast;
		else if (speed == medium) state = piece_picker::medium;
		else state = piece_picker::slow;

		t->picker().mark_as_downloading(block, m_remote, state);

		m_request_queue.push_back(block);
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
		(*m_logger) << time_now_string()
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
		(*m_logger) << time_now_string() << " ==> CHOKE\n";
#endif
#ifndef NDEBUG
		m_last_choke = time_now();
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
		//assert(time_now() - m_last_choke > seconds(9));
#endif

		if (!m_choked) return;
		write_unchoke();
		m_choked = false;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " ==> UNCHOKE\n";
#endif
	}

	void peer_connection::send_interested()
	{
		INVARIANT_CHECK;

		if (m_interesting) return;
		write_interested();
		m_interesting = true;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " ==> INTERESTED\n";
#endif
	}

	void peer_connection::send_not_interested()
	{
		INVARIANT_CHECK;

		if (!m_interesting) return;
		write_not_interested();
		m_interesting = false;

		m_became_uninteresting = time_now();

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " ==> NOT_INTERESTED\n";
#endif
	}

	void peer_connection::send_block_requests()
	{
		INVARIANT_CHECK;
		
		if (has_peer_choked()) return;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		assert(!has_peer_choked());

		if ((int)m_download_queue.size() >= m_desired_queue_size) return;

		while (!m_request_queue.empty()
			&& (int)m_download_queue.size() < m_desired_queue_size)
		{
			piece_block block = m_request_queue.front();

			int block_offset = block.block_index * t->block_size();
			int block_size = std::min((int)t->torrent_file().piece_size(
				block.piece_index) - block_offset, t->block_size());
			assert(block_size > 0);
			assert(block_size <= t->block_size());

			peer_request r;
			r.piece = block.piece_index;
			r.start = block_offset;
			r.length = block_size;

			m_request_queue.pop_front();
			m_download_queue.push_back(block);
/*
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " *** REQUEST-QUEUE** [ "
				"piece: " << block.piece_index << " | "
				"block: " << block.block_index << " ]\n";
#endif
*/			
			// if we are requesting large blocks, merge the smaller
			// blocks that are in the same piece into larger requests
			if (m_request_large_blocks)
			{
				while (!m_request_queue.empty()
					&& m_request_queue.front().piece_index == r.piece
					&& m_request_queue.front().block_index == block.block_index + 1)
				{
					block = m_request_queue.front();
					m_request_queue.pop_front();
					m_download_queue.push_back(block);
/*
#ifdef TORRENT_VERBOSE_LOGGING
					(*m_logger) << time_now_string()
						<< " *** REQUEST-QUEUE** [ "
						"piece: " << block.piece_index << " | "
						"block: " << block.block_index << " ]\n";
#endif
*/
					block_offset = block.block_index * t->block_size();
					block_size = std::min((int)t->torrent_file().piece_size(
						block.piece_index) - block_offset, t->block_size());
					assert(block_size > 0);
					assert(block_size <= t->block_size());

					r.length += block_size;
				}
			}

			assert(verify_piece(r));
			
#ifndef TORRENT_DISABLE_EXTENSIONS
			bool handled = false;
			for (extension_list_t::iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end; ++i)
			{
				if (handled = (*i)->write_request(r)) break;
			}
			if (!handled)
			{
				write_request(r);
				m_last_request = time_now();
			}
#else
			write_request(r);
			m_last_request = time_now();
#endif

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " ==> REQUEST [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " | "
				"ds: " << statistics().download_rate() << " B/s | "
				"qs: " << m_desired_queue_size << " ]\n";
#endif
		}
		m_last_piece = time_now();
	}


	void close_socket_ignore_error(boost::shared_ptr<socket_type> s)
	{
		try { s->close(); } catch (std::exception& e) {}
	}

	void peer_connection::timed_out()
	{
		if (m_peer_info) ++m_peer_info->failcount;
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_ses.m_logger) << "CONNECTION TIMED OUT: " << m_remote.address().to_string()
			<< "\n";
#endif
		m_ses.connection_failed(m_socket, m_remote, "timed out");
	}

	void peer_connection::disconnect()
	{
		boost::intrusive_ptr<peer_connection> me(this);

		INVARIANT_CHECK;

		if (m_disconnecting) return;
		m_disconnecting = true;
		if (m_connecting)
			m_ses.m_half_open.done(m_connection_ticket);

		m_ses.m_io_service.post(boost::bind(&close_socket_ignore_error, m_socket));

		boost::shared_ptr<torrent> t = m_torrent.lock();

		if (t)
		{
			if (t->has_picker())
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
			}

			t->remove_peer(this);

			m_torrent.reset();
		}

		m_ses.close_connection(me);
	}

	void peer_connection::set_upload_limit(int limit)
	{
		assert(limit >= -1);
		if (limit == -1) limit = resource_request::inf;
		if (limit < 10) limit = 10;
		m_upload_limit = limit;
		m_bandwidth_limit[upload_channel].throttle(m_upload_limit);
	}

	void peer_connection::set_download_limit(int limit)
	{
		assert(limit >= -1);
		if (limit == -1) limit = resource_request::inf;
		if (limit < 10) limit = 10;
		m_download_limit = limit;
		m_bandwidth_limit[download_channel].throttle(m_download_limit);
	}

	size_type peer_connection::share_diff() const
	{
		INVARIANT_CHECK;

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

	// defined in upnp.cpp
	bool is_local(address const& a);

	bool peer_connection::on_local_network() const
	{
		if (libtorrent::is_local(m_remote.address())) return true;
		return false;
	}

	void peer_connection::get_peer_info(peer_info& p) const
	{
		assert(!associated_torrent().expired());

		p.down_speed = statistics().download_rate();
		p.up_speed = statistics().upload_rate();
		p.payload_down_speed = statistics().download_payload_rate();
		p.payload_up_speed = statistics().upload_payload_rate();
		p.pid = pid();
		p.ip = remote();
		
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES	
		p.country[0] = m_country[0];
		p.country[1] = m_country[1];
#endif

		p.total_download = statistics().total_payload_download();
		p.total_upload = statistics().total_payload_upload();

		if (m_bandwidth_limit[upload_channel].throttle() == bandwidth_limit::inf)
			p.upload_limit = -1;
		else
			p.upload_limit = m_bandwidth_limit[upload_channel].throttle();

		if (m_bandwidth_limit[download_channel].throttle() == bandwidth_limit::inf)
			p.download_limit = -1;
		else
			p.download_limit = m_bandwidth_limit[download_channel].throttle();

		p.load_balancing = total_free_upload();

		p.download_queue_length = (int)download_queue().size();
		p.upload_queue_length = (int)upload_queue().size();

		if (boost::optional<piece_block_progress> ret = downloading_piece_progress())
		{
			p.downloading_piece_index = ret->piece_index;
			p.downloading_block_index = ret->block_index;
			p.downloading_progress = ret->bytes_downloaded;
			p.downloading_total = ret->full_block_bytes;
		}
		else
		{
			p.downloading_piece_index = -1;
			p.downloading_block_index = -1;
			p.downloading_progress = 0;
			p.downloading_total = 0;
		}

		p.pieces = get_bitfield();
		ptime now = time_now();
		p.last_request = now - m_last_request;
		p.last_active = now - std::max(m_last_sent, m_last_receive);

		// this will set the flags so that we can update them later
		p.flags = 0;
		get_specific_peer_info(p);

		p.flags |= is_seed() ? peer_info::seed : 0;
		if (peer_info_struct())
		{
			p.source = peer_info_struct()->source;
			p.failcount = peer_info_struct()->failcount;
			p.num_hashfails = peer_info_struct()->hashfails;
			p.flags |= peer_info_struct()->on_parole ? peer_info::on_parole : 0;
		}
		else
		{
			p.source = 0;
			p.failcount = 0;
			p.num_hashfails = 0;
		}

		p.send_buffer_size = send_buffer_size();
	}

	void peer_connection::cut_receive_buffer(int size, int packet_size)
	{
		INVARIANT_CHECK;

		assert(packet_size > 0);
		assert(int(m_recv_buffer.size()) >= size);
		assert(int(m_recv_buffer.size()) >= m_recv_pos);
		assert(m_recv_pos >= size);

		if (size > 0)		
			std::memmove(&m_recv_buffer[0], &m_recv_buffer[0] + size, m_recv_pos - size);

		m_recv_pos -= size;

#ifndef NDEBUG
		std::fill(m_recv_buffer.begin() + m_recv_pos, m_recv_buffer.end(), 0);
#endif

		m_packet_size = packet_size;
		if (m_packet_size >= m_recv_pos) m_recv_buffer.resize(m_packet_size);
	}

	void peer_connection::second_tick(float tick_interval)
	{
		INVARIANT_CHECK;

		ptime now(time_now());

		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		on_tick();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			(*i)->tick();
		}
#endif

		m_ignore_bandwidth_limits = m_ses.settings().ignore_limits_on_local_network
			&& on_local_network();

		m_statistics.second_tick(tick_interval);

		if (!t->valid_metadata()) return;

		// calculate the desired download queue size
		const float queue_time = m_ses.settings().request_queue_time;
		// (if the latency is more than this, the download will stall)
		// so, the queue size is queue_time * down_rate / 16 kiB
		// (16 kB is the size of each request)
		// the minimum number of requests is 2 and the maximum is 48
		// the block size doesn't have to be 16. So we first query the
		// torrent for it
		const int block_size = m_request_large_blocks
			? t->torrent_file().piece_length() : t->block_size();
		assert(block_size > 0);
		
		m_desired_queue_size = static_cast<int>(queue_time
			* statistics().download_rate() / block_size);
		if (m_desired_queue_size > m_max_out_request_queue)
			m_desired_queue_size = m_max_out_request_queue;
		if (m_desired_queue_size < min_request_queue)
			m_desired_queue_size = min_request_queue;

		if (!m_download_queue.empty()
			&& now - m_last_piece > seconds(m_ses.settings().piece_timeout))
		{
			// this peer isn't sending the pieces we've
			// requested (this has been observed by BitComet)
			// in this case we'll clear our download queue and
			// re-request the blocks.
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " *** PIECE_REQUESTS TIMED OUT [ " << (int)m_download_queue.size()
				<< " " << total_seconds(now - m_last_piece) << "] ***\n";
#endif

			if (t->is_seed())
			{
				m_download_queue.clear();
				m_request_queue.clear();
			}
			else
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

				// TODO: If we have a limited number of upload
				// slots, choke this peer
				
				m_assume_fifo = true;

				request_a_block(*t, *this);
				send_block_requests();
			}
		}

		m_statistics.second_tick(tick_interval);

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
			m_bandwidth_limit[upload_channel].throttle(m_upload_limit);
		}
		else
		{
			size_type bias = 0x10000 + 2 * t->block_size() + m_free_upload;

			double break_even_time = 15; // seconds.
			size_type have_uploaded = m_statistics.total_payload_upload();
			size_type have_downloaded = m_statistics.total_payload_download();
			double download_speed = m_statistics.download_rate();

			size_type soon_downloaded =
				have_downloaded + (size_type)(download_speed * break_even_time*1.5);

			if (t->ratio() != 1.f)
				soon_downloaded = (size_type)(soon_downloaded*(double)t->ratio());

			double upload_speed_limit = std::min((soon_downloaded - have_uploaded
				+ bias) / break_even_time, double(m_upload_limit));

			upload_speed_limit = std::min(upload_speed_limit,
				(double)std::numeric_limits<int>::max());

			m_bandwidth_limit[upload_channel].throttle(
				std::min(std::max((int)upload_speed_limit, 20)
				, m_upload_limit));
		}

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
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;

		// only add new piece-chunks if the send buffer is small enough
		// otherwise there will be no end to how large it will be!
		
		int buffer_size_watermark = int(m_statistics.upload_rate()) / 2;
		if (buffer_size_watermark < 1024) buffer_size_watermark = 1024;
		else if (buffer_size_watermark > 80 * 1024) buffer_size_watermark = 80 * 1024;

		while (!m_requests.empty()
			&& (send_buffer_size() < buffer_size_watermark)
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
			(*m_logger) << time_now_string()
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

	void peer_connection::assign_bandwidth(int channel, int amount)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << "bandwidth [ " << channel << " ] + " << amount << "\n";
#endif

		m_bandwidth_limit[channel].assign(amount);
		if (channel == upload_channel)
		{
			m_writing = false;
			setup_send();
		}
		else if (channel == download_channel)
		{
			m_reading = false;
			setup_receive();
		}
	}

	void peer_connection::expire_bandwidth(int channel, int amount)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		m_bandwidth_limit[channel].expire(amount);
		if (channel == upload_channel)
		{
			setup_send();
		}
		else if (channel == download_channel)
		{
			setup_receive();
		}
	}

	void peer_connection::setup_send()
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		if (m_writing) return;
		
		shared_ptr<torrent> t = m_torrent.lock();

		if (m_bandwidth_limit[upload_channel].quota_left() == 0
			&& (!m_send_buffer[m_current_send_buffer].empty()
				|| !m_send_buffer[(m_current_send_buffer + 1) & 1].empty())
			&& !m_connecting
			&& t
			&& !m_ignore_bandwidth_limits)
		{
			// in this case, we have data to send, but no
			// bandwidth. So, we simply request bandwidth
			// from the torrent
			assert(t);
			if (m_bandwidth_limit[upload_channel].max_assignable() > 0)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << "req bandwidth [ " << upload_channel << " ]\n";
#endif

				// peers that we are not interested in are non-prioritized
				t->request_bandwidth(upload_channel, self()
					, !(is_interesting() && !has_peer_choked()));
				m_writing = true;
			}
			return;
		}

		if (!can_write()) return;

		assert(!m_writing);

		int sending_buffer = (m_current_send_buffer + 1) & 1;
		if (m_send_buffer[sending_buffer].empty())
		{
			// this means we have to swap buffer, because there's no
			// previous buffer we're still waiting for.
			std::swap(m_current_send_buffer, sending_buffer);
			m_write_pos = 0;
		}

		// send the actual buffer
		if (!m_send_buffer[sending_buffer].empty())
		{
			int amount_to_send = (int)m_send_buffer[sending_buffer].size() - m_write_pos;
			int quota_left = m_bandwidth_limit[upload_channel].quota_left();
			if (!m_ignore_bandwidth_limits && amount_to_send > quota_left)
				amount_to_send = quota_left;

			assert(amount_to_send > 0);

			assert(m_write_pos < (int)m_send_buffer[sending_buffer].size());
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << "async_write " << amount_to_send << " bytes\n";
#endif
			m_socket->async_write_some(asio::buffer(
				&m_send_buffer[sending_buffer][m_write_pos], amount_to_send)
				, bind(&peer_connection::on_send_data, self(), _1, _2));

			m_writing = true;
		}
	}

	void peer_connection::setup_receive()
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		if (m_reading) return;

		shared_ptr<torrent> t = m_torrent.lock();
		
		if (m_bandwidth_limit[download_channel].quota_left() == 0
			&& !m_connecting
			&& t
			&& !m_ignore_bandwidth_limits)
		{
			if (m_bandwidth_limit[download_channel].max_assignable() > 0)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << "req bandwidth [ " << download_channel << " ]\n";
#endif
				t->request_bandwidth(download_channel, self(), m_non_prioritized);
				m_reading = true;
			}
			return;
		}
		
		if (!can_read()) return;

		assert(m_packet_size > 0);
		int max_receive = m_packet_size - m_recv_pos;
		int quota_left = m_bandwidth_limit[download_channel].quota_left();
		if (!m_ignore_bandwidth_limits && max_receive > quota_left)
			max_receive = quota_left;

		assert(max_receive > 0);

		assert(m_recv_pos >= 0);
		assert(m_packet_size > 0);

		assert(can_read());
#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << "async_read " << max_receive << " bytes\n";
#endif
		m_socket->async_read_some(asio::buffer(&m_recv_buffer[m_recv_pos]
			, max_receive), bind(&peer_connection::on_receive_data, self(), _1, _2));
		m_reading = true;
	}

	void peer_connection::reset_recv_buffer(int packet_size)
	{
		assert(packet_size > 0);
		if (m_recv_pos > m_packet_size)
		{
			cut_receive_buffer(m_packet_size, packet_size);
			return;
		}
		m_recv_pos = 0;
		m_packet_size = packet_size;
		if (int(m_recv_buffer.size()) < m_packet_size)
			m_recv_buffer.resize(m_packet_size);
	}

	void peer_connection::send_buffer(char const* begin, char const* end)
	{
		std::vector<char>& buf = m_send_buffer[m_current_send_buffer];
		buf.insert(buf.end(), begin, end);
		setup_send();
	}

// TODO: change this interface to automatically call setup_send() when the
// return value is destructed
	buffer::interval peer_connection::allocate_send_buffer(int size)
	{
		std::vector<char>& buf = m_send_buffer[m_current_send_buffer];
		buf.resize(buf.size() + size);
		buffer::interval ret(&buf[0] + buf.size() - size, &buf[0] + buf.size());
		return ret;
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
	void peer_connection::on_receive_data(const asio::error_code& error
		, std::size_t bytes_transferred) try
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		assert(m_reading);
		m_reading = false;

		
		if (error)
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << "**ERROR**: " << error.message() << "[in peer_connection::on_receive_data]\n";
#endif
			on_receive(error, bytes_transferred);
			throw std::runtime_error(error.message());
		}

		do
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << "read " << bytes_transferred << " bytes\n";
#endif
			// correct the dl quota usage, if not all of the buffer was actually read
			if (!m_ignore_bandwidth_limits)
				m_bandwidth_limit[download_channel].use_quota(bytes_transferred);

			if (m_disconnecting) return;
	
			assert(m_packet_size > 0);
			assert(bytes_transferred > 0);

			m_last_receive = time_now();
			m_recv_pos += bytes_transferred;
			assert(m_recv_pos <= int(m_recv_buffer.size()));
		
			{
				INVARIANT_CHECK;
				on_receive(error, bytes_transferred);
			}

			assert(m_packet_size > 0);

			if (m_peer_choked
				&& m_recv_pos == 0
				&& (m_recv_buffer.capacity() - m_packet_size) > 128)
			{
				std::vector<char>(m_packet_size).swap(m_recv_buffer);
			}

			int max_receive = m_packet_size - m_recv_pos;
			int quota_left = m_bandwidth_limit[download_channel].quota_left();
			if (!m_ignore_bandwidth_limits && max_receive > quota_left)
				max_receive = quota_left;

			if (max_receive == 0) break;

			asio::error_code ec;	
			bytes_transferred = m_socket->read_some(asio::buffer(&m_recv_buffer[m_recv_pos]
				, max_receive), ec);
			if (ec && ec != asio::error::would_block)
				throw asio::system_error(ec);
		}
		while (bytes_transferred > 0);

		setup_receive();	
	}
	catch (file_error& e)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);
		
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t)
		{
			m_ses.connection_failed(m_socket, remote(), e.what());
			return;
		}
		
		if (t->alerts().should_post(alert::fatal))
		{
			t->alerts().post_alert(
				file_error_alert(t->get_handle()
				, std::string("torrent paused: ") + e.what()));
		}
		t->pause();
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
		m_ses.connection_failed(m_socket, remote(), "connection failed for unknown reason");
	}

	bool peer_connection::can_write() const
	{
		INVARIANT_CHECK;

		// if we have requests or pending data to be sent or announcements to be made
		// we want to send data
		return (!m_send_buffer[m_current_send_buffer].empty()
			|| !m_send_buffer[(m_current_send_buffer + 1) & 1].empty())
			&& (m_bandwidth_limit[upload_channel].quota_left() > 0
				|| m_ignore_bandwidth_limits)
			&& !m_connecting;
	}

	bool peer_connection::can_read() const
	{
		INVARIANT_CHECK;

		return (m_bandwidth_limit[download_channel].quota_left() > 0
				|| m_ignore_bandwidth_limits)
			&& !m_connecting;
	}

	void peer_connection::connect(int ticket)
	{
		INVARIANT_CHECK;

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_ses.m_logger) << "CONNECTING: " << m_remote.address().to_string()
			<< ":" << m_remote.port() << "\n";
#endif

		m_connection_ticket = ticket;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		m_queued = false;
		assert(m_connecting);
		m_socket->open(t->get_interface().protocol());

		// set the socket to non-blocking, so that we can
		// read the entire buffer on each read event we get
		tcp::socket::non_blocking_io ioc(true);
		m_socket->io_control(ioc);
		m_socket->bind(t->get_interface());
		m_socket->async_connect(m_remote
			, bind(&peer_connection::on_connection_complete, self(), _1));

		if (t->alerts().should_post(alert::debug))
		{
			t->alerts().post_alert(peer_error_alert(
				m_remote, m_peer_id, "connecting to peer"));
		}
	}
	
	void peer_connection::on_connection_complete(asio::error_code const& e) try
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		if (m_disconnecting) return;

		m_connecting = false;
		m_ses.m_half_open.done(m_connection_ticket);

		if (e)
		{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
			(*m_ses.m_logger) << "CONNECTION FAILED: " << m_remote.address().to_string()
				<< ": " << e.message() << "\n";
#endif
			m_ses.connection_failed(m_socket, m_remote, e.message().c_str());
			return;
		}

		if (m_disconnecting) return;
		m_last_receive = time_now();

		// this means the connection just succeeded

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		(*m_ses.m_logger) << "COMPLETED: " << m_remote.address().to_string() << "\n";
#endif

		on_connected();
		setup_send();
		setup_receive();
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
	void peer_connection::on_send_data(asio::error_code const& error
		, std::size_t bytes_transferred) try
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		assert(m_writing);
		m_writing = false;

		if (!m_ignore_bandwidth_limits)
			m_bandwidth_limit[upload_channel].use_quota(bytes_transferred);

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << "wrote " << bytes_transferred << " bytes\n";
#endif

		m_write_pos += bytes_transferred;

		
		if (error)
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << "**ERROR**: " << error.message() << " [in peer_connection::on_send_data]\n";
#endif
			throw std::runtime_error(error.message());
		}
		if (m_disconnecting) return;

		assert(!m_connecting);
		assert(bytes_transferred > 0);

		int sending_buffer = (m_current_send_buffer + 1) & 1;

		assert(int(m_send_buffer[sending_buffer].size()) >= m_write_pos);
		if (int(m_send_buffer[sending_buffer].size()) == m_write_pos)
		{
			m_send_buffer[sending_buffer].clear();
			m_write_pos = 0;
		}

		m_last_sent = time_now();

		on_sent(error, bytes_transferred);
		fill_send_buffer();

		if (m_choked)
		{
			for (int i = 0; i < 2; ++i)
			{
				if (int(m_send_buffer[i].size()) < 64
					&& int(m_send_buffer[i].capacity()) > 128)
				{
					std::vector<char> tmp(m_send_buffer[i]);
					tmp.swap(m_send_buffer[i]);
					assert(m_send_buffer[i].capacity() == m_send_buffer[i].size());
				}
			}
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
		m_ses.connection_failed(m_socket, remote(), "connection failed for unknown reason");
	}


#ifndef NDEBUG
	void peer_connection::check_invariant() const
	{
		if (m_peer_info)
			assert(m_peer_info->connection == this
				|| m_peer_info->connection == 0);
	
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t)
		{
			typedef session_impl::torrent_map torrent_map;
			torrent_map& m = m_ses.m_torrents;
			for (torrent_map::iterator i = m.begin(), end(m.end()); i != end; ++i)
			{
				torrent& t = *i->second;
				assert(t.connection_for(m_remote) != this);
			}
			return;
		}

		if (!m_in_constructor && t->connection_for(remote()) != this
			&& !m_ses.settings().allow_multiple_connections_per_ip)
		{
			assert(false);
		}

// expensive when using checked iterators
/*
		if (t->valid_metadata())
		{
			int piece_count = std::count(m_have_piece.begin()
				, m_have_piece.end(), true);
			if (m_num_pieces != piece_count)
			{
				assert(false);
			}
		}
*/
		assert(m_write_pos <= int(m_send_buffer[
			(m_current_send_buffer + 1) & 1].size()));

// extremely expensive invariant check
/*
		if (!t->is_seed())
		{
			piece_picker& p = t->picker();
			const std::vector<piece_picker::downloading_piece>& dlq = p.get_download_queue();
			const int blocks_per_piece = static_cast<int>(
				t->torrent_file().piece_length() / t->block_size());

			for (std::vector<piece_picker::downloading_piece>::const_iterator i =
				dlq.begin(); i != dlq.end(); ++i)
			{
				for (int j = 0; j < blocks_per_piece; ++j)
				{
					if (std::find(m_request_queue.begin(), m_request_queue.end()
						, piece_block(i->index, j)) != m_request_queue.end()
						||
						std::find(m_download_queue.begin(), m_download_queue.end()
						, piece_block(i->index, j)) != m_download_queue.end())
					{
						assert(i->info[j].peer == m_remote);
					}
					else
					{
						assert(i->info[j].peer != m_remote || i->info[j].finished);
					}
				}
			}
		}
*/
	}
#endif

	bool peer_connection::has_timed_out() const
	{
		// TODO: the timeout should be called by an event
		INVARIANT_CHECK;

#ifndef NDEBUG
		// allow step debugging without timing out
		return false;
#endif

		ptime now(time_now());
		
		// if the socket is still connecting, don't
		// consider it timed out. Because Windows XP SP2
		// may delay connection attempts.
		if (m_connecting) return false;
		
		// if the peer hasn't said a thing for a certain
		// time, it is considered to have timed out
		time_duration d;
		d = time_now() - m_last_receive;
		if (d > seconds(m_timeout)) return true;

		// TODO: as long as we have less than 95% of the
		// global (or local) connection limit, connections should
		// never time out for another reason

		// if the peer hasn't become interested and we haven't
		// become interested in the peer for 10 minutes, it
		// has also timed out.
		time_duration d1;
		time_duration d2;
		d1 = now - m_became_uninterested;
		d2 = now - m_became_uninteresting;
		time_duration time_limit = seconds(
			m_ses.settings().inactivity_timeout);

		if (!m_interesting
			&& !m_peer_interested
			&& d1 > time_limit
			&& d2 > time_limit)
		{
			return true;
		}

		return false;
	}

	peer_connection::peer_speed_t peer_connection::peer_speed()
	{
		shared_ptr<torrent> t = m_torrent.lock();
		assert(t);

		int download_rate = int(statistics().download_payload_rate());
		int torrent_download_rate = int(t->statistics().download_payload_rate());

		if (download_rate > 512 && download_rate > torrent_download_rate / 16)
			m_speed = fast;
		else if (download_rate > 4096 && download_rate > torrent_download_rate / 64)
			m_speed = medium;
		else if (download_rate < torrent_download_rate / 15 && m_speed == fast)
			m_speed = medium;
		else if (download_rate < torrent_download_rate / 63 && m_speed == medium)
			m_speed = slow;

		return m_speed;
	}

	void peer_connection::keep_alive()
	{
		INVARIANT_CHECK;

		time_duration d;
		d = time_now() - m_last_sent;
		if (total_seconds(d) < m_timeout / 2) return;
		
		if (m_connecting) return;
		if (in_handshake()) return;

		// if the last send has not completed yet, do not send a keep
		// alive
		if (m_writing) return;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace	boost::posix_time;
		(*m_logger) << time_now_string() << " ==> KEEPALIVE\n";
#endif
		
		write_keepalive();
	}

	bool peer_connection::is_seed() const
	{
		INVARIANT_CHECK;
		// if m_num_pieces == 0, we probably don't have the
		// metadata yet.
		return m_num_pieces == (int)m_have_piece.size() && m_num_pieces > 0;
	}
}

