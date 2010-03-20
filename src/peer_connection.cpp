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
#include "libtorrent/assert.hpp"
#include "libtorrent/broadcast_socket.hpp"

//#define TORRENT_CORRUPT_DATA

using boost::bind;
using boost::shared_ptr;
using libtorrent::aux::session_impl;

namespace libtorrent
{
	// outbound connection
	peer_connection::peer_connection(
		session_impl& ses
		, boost::weak_ptr<torrent> tor
		, shared_ptr<socket_type> s
		, tcp::endpoint const& endp
		, policy::peer* peerinfo)
		:
#ifdef TORRENT_DEBUG
		m_last_choke(time_now() - hours(1))
		,
#endif
		  m_ses(ses)
		, m_max_out_request_queue(m_ses.settings().max_out_request_queue)
		, m_work(ses.m_io_service)
		, m_last_piece(time_now())
		, m_last_request(time_now())
		, m_last_incoming_request(min_time())
		, m_last_unchoke(min_time())
		, m_last_receive(time_now())
		, m_last_sent(time_now())
		, m_requested(min_time())
		, m_timeout_extend(0)
		, m_remote_dl_update(time_now())
		, m_connect(time_now())
		, m_became_uninterested(time_now())
		, m_became_uninteresting(time_now())
		, m_free_upload(0)
		, m_downloaded_at_last_unchoke(0)
		, m_disk_recv_buffer(ses, 0)
		, m_socket(s)
		, m_remote(endp)
		, m_torrent(tor)
		, m_num_pieces(0)
		, m_timeout(m_ses.settings().peer_timeout)
		, m_packet_size(0)
		, m_recv_pos(0)
		, m_disk_recv_buffer_size(0)
		, m_reading_bytes(0)
		, m_num_invalid_requests(0)
		, m_priority(1)
		, m_upload_limit(bandwidth_limit::inf)
		, m_download_limit(bandwidth_limit::inf)
		, m_peer_info(peerinfo)
		, m_speed(slow)
		, m_connection_ticket(-1)
		, m_remote_bytes_dled(0)
		, m_remote_dl_rate(0)
		, m_outstanding_writing_bytes(0)
		, m_download_rate_peak(0)
		, m_upload_rate_peak(0)
		, m_rtt(0)
		, m_prefer_whole_pieces(0)
		, m_desired_queue_size(2)
		, m_fast_reconnect(false)
		, m_active(true)
		, m_peer_interested(false)
		, m_peer_choked(true)
		, m_interesting(false)
		, m_choked(true)
		, m_failed(false)
		, m_ignore_bandwidth_limits(false)
		, m_have_all(false)
		, m_disconnecting(false)
		, m_connecting(true)
		, m_queued(true)
		, m_request_large_blocks(false)
		, m_upload_only(false)
		, m_snubbed(false)
		, m_bitfield_received(false)
#ifdef TORRENT_DEBUG
		, m_in_constructor(true)
		, m_disconnect_started(false)
		, m_initialized(false)
#endif
	{
		m_channel_state[upload_channel] = peer_info::bw_idle;
		m_channel_state[download_channel] = peer_info::bw_idle;

		TORRENT_ASSERT(peerinfo == 0 || peerinfo->banned == false);
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		std::fill(m_country, m_country + 2, 0);
#ifndef TORRENT_DISABLE_GEO_IP
		if (m_ses.has_country_db())
		{
			char const *country = m_ses.country_for_ip(m_remote.address());
			if (country != 0)
			{
				m_country[0] = country[0];
				m_country[1] = country[1];
			}
		}
#endif
#endif
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
		error_code ec;
		m_logger = m_ses.create_log(m_remote.address().to_string(ec) + "_"
			+ to_string(m_remote.port()).elems, m_ses.listen_port());
		(*m_logger) << "*** OUTGOING CONNECTION\n";
#endif
#ifdef TORRENT_DEBUG
		piece_failed = false;
#endif
#ifndef TORRENT_DISABLE_GEO_IP
		m_inet_as_name = m_ses.as_name_for_ip(m_remote.address());
#endif

		std::fill(m_peer_id.begin(), m_peer_id.end(), 0);
	}

	// incoming connection
	peer_connection::peer_connection(
		session_impl& ses
		, shared_ptr<socket_type> s
		, tcp::endpoint const& endp
		, policy::peer* peerinfo)
		:
#ifdef TORRENT_DEBUG
		m_last_choke(time_now() - hours(1))
		,
#endif
		  m_ses(ses)
		, m_max_out_request_queue(m_ses.settings().max_out_request_queue)
		, m_work(ses.m_io_service)
		, m_last_piece(time_now())
		, m_last_request(time_now())
		, m_last_incoming_request(min_time())
		, m_last_unchoke(min_time())
		, m_last_receive(time_now())
		, m_last_sent(time_now())
		, m_requested(min_time())
		, m_timeout_extend(0)
		, m_remote_dl_update(time_now())
		, m_connect(time_now())
		, m_became_uninterested(time_now())
		, m_became_uninteresting(time_now())
		, m_free_upload(0)
		, m_downloaded_at_last_unchoke(0)
		, m_disk_recv_buffer(ses, 0)
		, m_socket(s)
		, m_remote(endp)
		, m_num_pieces(0)
		, m_timeout(m_ses.settings().peer_timeout)
		, m_packet_size(0)
		, m_recv_pos(0)
		, m_disk_recv_buffer_size(0)
		, m_reading_bytes(0)
		, m_num_invalid_requests(0)
		, m_priority(1)
		, m_upload_limit(bandwidth_limit::inf)
		, m_download_limit(bandwidth_limit::inf)
		, m_peer_info(peerinfo)
		, m_speed(slow)
		, m_connection_ticket(-1)
		, m_remote_bytes_dled(0)
		, m_remote_dl_rate(0)
		, m_outstanding_writing_bytes(0)
		, m_download_rate_peak(0)
		, m_upload_rate_peak(0)
		, m_rtt(0)
		, m_prefer_whole_pieces(0)
		, m_desired_queue_size(2)
		, m_fast_reconnect(false)
		, m_active(false)
		, m_peer_interested(false)
		, m_peer_choked(true)
		, m_interesting(false)
		, m_choked(true)
		, m_failed(false)
		, m_ignore_bandwidth_limits(false)
		, m_have_all(false)
		, m_disconnecting(false)
		, m_connecting(false)
		, m_queued(false)
		, m_request_large_blocks(false)
		, m_upload_only(false)
		, m_snubbed(false)
		, m_bitfield_received(false)
#ifdef TORRENT_DEBUG
		, m_in_constructor(true)
		, m_disconnect_started(false)
		, m_initialized(false)
#endif
	{
		m_channel_state[upload_channel] = peer_info::bw_idle;
		m_channel_state[download_channel] = peer_info::bw_idle;

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		std::fill(m_country, m_country + 2, 0);
#ifndef TORRENT_DISABLE_GEO_IP
		if (m_ses.has_country_db())
		{
			char const *country = m_ses.country_for_ip(m_remote.address());
			if (country != 0)
			{
				m_country[0] = country[0];
				m_country[1] = country[1];
			}
		}
#endif
#endif

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
		error_code ec;
		TORRENT_ASSERT(m_socket->remote_endpoint(ec) == m_remote || ec);
		m_logger = m_ses.create_log(remote().address().to_string(ec) + "_"
			+ to_string(remote().port()).elems, m_ses.listen_port());
		(*m_logger) << "*** INCOMING CONNECTION\n";
#endif
		
#ifndef TORRENT_DISABLE_GEO_IP
		m_inet_as_name = m_ses.as_name_for_ip(m_remote.address());
#endif
#ifdef TORRENT_DEBUG
		piece_failed = false;
#endif
		std::fill(m_peer_id.begin(), m_peer_id.end(), 0);
	}

	bool peer_connection::unchoke_compare(boost::intrusive_ptr<peer_connection const> const& p) const
	{
		TORRENT_ASSERT(p);
		peer_connection const& rhs = *p;

		size_type c1;
		size_type c2;

		// first compare how many bytes they've sent us
		c1 = m_statistics.total_payload_download() - m_downloaded_at_last_unchoke;
		c2 = rhs.m_statistics.total_payload_download() - rhs.m_downloaded_at_last_unchoke;
		if (c1 > c2) return true;
		if (c1 < c2) return false;

		// if they are equal, compare how much we have uploaded
		if (m_peer_info) c1 = m_peer_info->total_upload();
		else c1 = m_statistics.total_payload_upload();
		if (rhs.m_peer_info) c2 = rhs.m_peer_info->total_upload();
		else c2 = rhs.m_statistics.total_payload_upload();

		// in order to not switch back and forth too often,
		// unchoked peers must be at least one piece ahead
		// of a choked peer to be sorted at a lower unchoke-priority
		boost::shared_ptr<torrent> t1 = m_torrent.lock();
		TORRENT_ASSERT(t1);
		boost::shared_ptr<torrent> t2 = rhs.associated_torrent().lock();
		TORRENT_ASSERT(t2);
		if (!is_choked()) c1 -= (std::max)(t1->torrent_file().piece_length(), 256 * 1024);
		if (!rhs.is_choked()) c2 -= (std::max)(t2->torrent_file().piece_length(), 256 * 1024);
		
		return c1 < c2;
	}

	void peer_connection::reset_choke_counters()
	{
		m_downloaded_at_last_unchoke = m_statistics.total_payload_download();
	}

	void peer_connection::start()
	{
		TORRENT_ASSERT(m_peer_info == 0 || m_peer_info->connection == this);
		boost::shared_ptr<torrent> t = m_torrent.lock();

		if (!t)
		{
			tcp::socket::non_blocking_io ioc(true);
			error_code ec;
			m_socket->io_control(ioc, ec);
			if (ec)
			{
				disconnect(ec.message().c_str());
				return;
			}
			m_remote = m_socket->remote_endpoint(ec);
			if (ec)
			{
				disconnect(ec.message().c_str());
				return;
			}
			if (m_remote.address().is_v4())
				m_socket->set_option(type_of_service(m_ses.settings().peer_tos), ec);
		}
		else if (t->ready_for_connections())
		{
			init();
		}
	}

	void peer_connection::update_interest()
	{
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		// if m_have_piece is 0, it means the connections
		// have not been initialized yet. The interested
		// flag will be updated once they are.
		if (m_have_piece.size() == 0) return;
		if (!t->ready_for_connections()) return;

		bool interested = false;
		if (!t->is_finished())
		{
			piece_picker const& p = t->picker();
			int num_pieces = p.num_pieces();
			for (int j = 0; j != num_pieces; ++j)
			{
				if (!p.have_piece(j)
					&& t->piece_priority(j) > 0
					&& m_have_piece[j])
				{
					interested = true;
					break;
				}
			}
		}
		try
		{
			if (!interested) send_not_interested();
			else t->get_policy().peer_is_interesting(*this);
		}
		// may throw an asio error if socket has disconnected
		catch (std::exception&) {}

		TORRENT_ASSERT(in_handshake() || is_interesting() == interested);
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void peer_connection::add_extension(boost::shared_ptr<peer_plugin> ext)
	{
		m_extensions.push_back(ext);
	}
#endif

	void peer_connection::send_allowed_set()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		int num_allowed_pieces = m_ses.settings().allowed_fast_set_size;
		if (num_allowed_pieces == 0) return;

		int num_pieces = t->torrent_file().num_pieces();

		if (num_allowed_pieces >= num_pieces)
		{
			for (int i = 0; i < num_pieces; ++i)
			{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " ==> ALLOWED_FAST [ " << i << " ]\n";
#endif
				write_allow_fast(i);
				m_accept_fast.insert(i);
			}
			return;
		}

		std::string x;
		address const& addr = m_remote.address();
		if (addr.is_v4())
		{
			address_v4::bytes_type bytes = addr.to_v4().to_bytes();
			x.assign((char*)&bytes[0], bytes.size());
		}
		else
		{
			address_v6::bytes_type bytes = addr.to_v6().to_bytes();
			x.assign((char*)&bytes[0], bytes.size());
		}
		x.append((char*)&t->torrent_file().info_hash()[0], 20);

		sha1_hash hash = hasher(&x[0], x.size()).final();
		for (;;)
		{
			char* p = (char*)&hash[0];
			for (int i = 0; i < 5; ++i)
			{
				int piece = detail::read_uint32(p) % num_pieces;
				if (m_accept_fast.find(piece) == m_accept_fast.end())
				{
#ifdef TORRENT_VERBOSE_LOGGING
					(*m_logger) << time_now_string()
						<< " ==> ALLOWED_FAST [ " << piece << " ]\n";
#endif
					write_allow_fast(piece);
					m_accept_fast.insert(piece);
					if (int(m_accept_fast.size()) >= num_allowed_pieces
						|| int(m_accept_fast.size()) == num_pieces) return;
				}
			}
			hash = hasher((char*)&hash[0], 20).final();
		}
	}

	void peer_connection::on_metadata_impl()
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		m_have_piece.resize(t->torrent_file().num_pieces(), m_have_all);
		m_num_pieces = m_have_piece.count();

		// now that we know how many pieces there are
		// remove any invalid allowed_fast and suggest pieces
		// now that we know what the number of pieces are
		for (std::vector<int>::iterator i = m_allowed_fast.begin();
			i != m_allowed_fast.end();)
		{
			if (*i < m_num_pieces)
			{
				++i;
				continue;
			}
			i = m_allowed_fast.erase(i);
		}

		for (std::vector<int>::iterator i = m_suggested_pieces.begin();
			i != m_suggested_pieces.end();)
		{
			if (*i < m_num_pieces)
			{
				++i;
				continue;
			}
			i = m_suggested_pieces.erase(i);
		}
		
		if (m_num_pieces == int(m_have_piece.size()))
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " *** on_metadata(): THIS IS A SEED ***\n";
#endif
			// if this is a web seed. we don't have a peer_info struct
			t->get_policy().set_seed(m_peer_info, true);
			m_upload_only = true;

			t->peer_has_all();
			disconnect_if_redundant();
			if (m_disconnecting) return;

			on_metadata();
			if (m_disconnecting) return;

			if (!t->is_finished())
				t->get_policy().peer_is_interesting(*this);

			return;
		}
		TORRENT_ASSERT(!m_have_all);

		on_metadata();
		if (m_disconnecting) return;

		// let the torrent know which pieces the
		// peer has
		// if we're a seed, we don't keep track of piece availability
		bool interesting = false;
		if (!t->is_seed())
		{
			t->peer_has(m_have_piece);

			for (int i = 0; i < (int)m_have_piece.size(); ++i)
			{
				if (m_have_piece[i])
				{
					if (!t->have_piece(i) && t->picker().piece_priority(i) != 0)
						interesting = true;
				}
			}
		}

		if (interesting) t->get_policy().peer_is_interesting(*this);
		else if (upload_only()) disconnect("upload to upload connections");
	}

	void peer_connection::init()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		TORRENT_ASSERT(t->valid_metadata());
		TORRENT_ASSERT(t->ready_for_connections());

		m_have_piece.resize(t->torrent_file().num_pieces(), m_have_all);

		if (m_have_all) m_num_pieces = t->torrent_file().num_pieces();
#ifdef TORRENT_DEBUG
		m_initialized = true;
#endif
		// now that we have a piece_picker,
		// update it with this peer's pieces

		TORRENT_ASSERT(m_num_pieces == m_have_piece.count());

		if (m_num_pieces == int(m_have_piece.size()))
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " *** THIS IS A SEED ***\n";
#endif
			// if this is a web seed. we don't have a peer_info struct
			t->get_policy().set_seed(m_peer_info, true);
			m_upload_only = true;

			t->peer_has_all();
			if (t->is_finished()) send_not_interested();
			else t->get_policy().peer_is_interesting(*this);
			return;
		}

		// if we're a seed, we don't keep track of piece availability
		if (!t->is_seed())
		{
			t->peer_has(m_have_piece);
			bool interesting = false;
			for (int i = 0; i < int(m_have_piece.size()); ++i)
			{
				if (m_have_piece[i])
				{
					// if the peer has a piece and we don't, the peer is interesting
					if (!t->have_piece(i)
						&& t->picker().piece_priority(i) != 0)
						interesting = true;
				}
			}
			if (interesting) t->get_policy().peer_is_interesting(*this);
			else send_not_interested();
		}
		else
		{
			update_interest();
		}
	}

	peer_connection::~peer_connection()
	{
//		INVARIANT_CHECK;
		TORRENT_ASSERT(!m_in_constructor);
		TORRENT_ASSERT(m_disconnecting);
		TORRENT_ASSERT(m_disconnect_started);

		m_disk_recv_buffer_size = 0;

#ifndef TORRENT_DISABLE_EXTENSIONS
		m_extensions.clear();
#endif

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
		if (m_logger)
		{
			(*m_logger) << time_now_string()
				<< " *** CONNECTION CLOSED\n";
		}
#endif
		TORRENT_ASSERT(!m_ses.has_peer(this));
#ifdef TORRENT_DEBUG
		for (aux::session_impl::torrent_map::const_iterator i = m_ses.m_torrents.begin()
			, end(m_ses.m_torrents.end()); i != end; ++i)
			TORRENT_ASSERT(!i->second->has_peer(this));
		if (m_peer_info)
			TORRENT_ASSERT(m_peer_info->connection == 0);

		boost::shared_ptr<torrent> t = m_torrent.lock();
#endif
	}

	int peer_connection::picker_options() const
	{
		int ret = 0; 
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		if (!t) return 0;

		if (t->is_sequential_download())
		{
			ret |= piece_picker::sequential;
		}
		else if (t->num_have() < t->settings().initial_picker_threshold)
		{
			// if we have fewer pieces than a certain threshols
			// don't pick rare pieces, just pick random ones,
			// and prioritize finishing them
			ret |= piece_picker::prioritize_partials;
		}
		else
		{
			ret |= piece_picker::rarest_first;
		}

		if (m_snubbed)
		{
			// snubbed peers should request
			// the common pieces first, just to make
			// it more likely for all snubbed peers to
			// request blocks from the same piece
			ret |= piece_picker::reverse;
		}

		if (t->settings().prioritize_partial_pieces)
			ret |= piece_picker::prioritize_partials;

		if (on_parole()) ret |= piece_picker::on_parole
			| piece_picker::prioritize_partials;

		// only one of rarest_first, common_first and sequential can be set.
		TORRENT_ASSERT(bool(ret & piece_picker::rarest_first)
			+ bool(ret & piece_picker::sequential) <= 1);
		return ret;
	}

	void peer_connection::fast_reconnect(bool r)
	{
		if (!peer_info_struct() || peer_info_struct()->fast_reconnects > 1)
			return;
		m_fast_reconnect = r;
		peer_info_struct()->connected = time_now()
			- seconds(m_ses.settings().min_reconnect_time
			* m_ses.settings().max_failcount);
		++peer_info_struct()->fast_reconnects;
	}

	void peer_connection::announce_piece(int index)
	{
		// dont announce during handshake
		if (in_handshake()) return;

		// remove suggested pieces that we have		
		std::vector<int>::iterator i = std::find(
			m_suggested_pieces.begin(), m_suggested_pieces.end(), index);
		if (i != m_suggested_pieces.end()) m_suggested_pieces.erase(i);

		if (has_piece(index))
		{
			// if we got a piece that this peer has
			// it might have been the last interesting
			// piece this peer had. We might not be
			// interested anymore
			update_interest();
			if (is_disconnecting()) return;

			// optimization, don't send have messages
			// to peers that already have the piece
			if (!m_ses.settings().send_redundant_have)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << time_now_string()
					<< " ==> HAVE    [ piece: " << index << " ] SUPRESSED\n";
#endif
				return;
			}
		}

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
			<< " ==> HAVE    [ piece: " << index << "]\n";
#endif
		write_have(index);
#ifdef TORRENT_DEBUG
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		TORRENT_ASSERT(t->have_piece(index));
#endif
	}

	bool peer_connection::has_piece(int i) const
	{
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		TORRENT_ASSERT(t->valid_metadata());
		TORRENT_ASSERT(i >= 0);
		TORRENT_ASSERT(i < t->torrent_file().num_pieces());
		return m_have_piece[i];
	}

	std::deque<piece_block> const& peer_connection::request_queue() const
	{
		return m_request_queue;
	}
	
	std::deque<pending_block> const& peer_connection::download_queue() const
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

	bitfield const& peer_connection::get_bitfield() const
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
#ifdef BOOST_NO_EXCEPTIONS
			(*i)->on_piece_pass(index);
#else
			try { (*i)->on_piece_pass(index); } catch (std::exception&) {}
#endif
		}
#endif
	}

	void peer_connection::received_invalid_data(int index)
	{
		INVARIANT_CHECK;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
#ifdef BOOST_NO_EXCEPTIONS
			(*i)->on_piece_failed(index);
#else
			try { (*i)->on_piece_failed(index); } catch (std::exception&) {}
#endif
		}
#endif
		if (is_disconnecting()) return;

		if (peer_info_struct())
		{
			if (m_ses.settings().use_parole_mode)
				peer_info_struct()->on_parole = true;

			++peer_info_struct()->hashfails;
			boost::int8_t& trust_points = peer_info_struct()->trust_points;

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
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		TORRENT_ASSERT(t->valid_metadata());
		torrent_info const& ti = t->torrent_file();

		return p.piece >= 0
			&& p.piece < ti.num_pieces()
			&& p.start >= 0
			&& p.start < ti.piece_length()
			&& t->to_req(piece_block(p.piece, p.start / t->block_size())) == p;
	}

	void peer_connection::attach_to_torrent(sha1_hash const& ih)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_disconnecting);
		TORRENT_ASSERT(m_torrent.expired());
		boost::weak_ptr<torrent> wpt = m_ses.find_torrent(ih);
		boost::shared_ptr<torrent> t = wpt.lock();

		if (t && t->is_aborted())
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << " *** the torrent has been aborted\n";
#endif
			t.reset();
		}

		if (!t)
		{
			// we couldn't find the torrent!
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << " *** couldn't find a torrent with the given info_hash: " << ih << "\n";
			(*m_logger) << " torrents:\n";
			session_impl::torrent_map const& torrents = m_ses.m_torrents;
			for (session_impl::torrent_map::const_iterator i = torrents.begin()
				, end(torrents.end()); i != end; ++i)
			{
				(*m_logger) << "   " << i->second->torrent_file().info_hash() << "\n";
			}
#endif
			disconnect("got invalid info-hash", 2);
			return;
		}

		if (t->is_paused())
		{
			// paused torrents will not accept
			// incoming connections
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << " rejected connection to paused torrent\n";
#endif
			disconnect("connection rejected bacause torrent is paused");
			return;
		}

		TORRENT_ASSERT(m_torrent.expired());
		// check to make sure we don't have another connection with the same
		// info_hash and peer_id. If we do. close this connection.
#ifdef TORRENT_DEBUG
		try
		{
#endif
		t->attach_peer(this);
#ifdef TORRENT_DEBUG
		}
		catch (std::exception& e)
		{
			std::cout << e.what() << std::endl;
			TORRENT_ASSERT(false);
		}
#endif
		if (m_disconnecting) return;
		m_torrent = wpt;

		TORRENT_ASSERT(!m_torrent.expired());

		// if the torrent isn't ready to accept
		// connections yet, we'll have to wait with
		// our initialization
		if (t->ready_for_connections()) init();

		TORRENT_ASSERT(!m_torrent.expired());

		// assume the other end has no pieces
		// if we don't have valid metadata yet,
		// leave the vector unallocated
		TORRENT_ASSERT(m_num_pieces == 0);
		m_have_piece.clear_all();
		TORRENT_ASSERT(!m_torrent.expired());
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
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_choke()) return;
		}
#endif
		if (is_disconnecting()) return;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " <== CHOKE\n";
#endif
		m_peer_choked = true;
		
		if (peer_info_struct() == 0 || !peer_info_struct()->on_parole)
		{
			// if the peer is not in parole mode, clear the queued
			// up block requests
			if (!t->is_seed())
			{
				piece_picker& p = t->picker();
				for (std::deque<piece_block>::const_iterator i = m_request_queue.begin()
					, end(m_request_queue.end()); i != end; ++i)
				{
					// since this piece was skipped, clear it and allow it to
					// be requested from other peers
					p.abort_download(*i);
				}
			}
			m_request_queue.clear();
		}
	}

	bool match_request(peer_request const& r, piece_block const& b, int block_size)
	{
		if (b.piece_index != r.piece) return false;
		if (b.block_index != r.start / block_size) return false;
		if (r.start % block_size != 0) return false;
		return true;
	}

	// -----------------------------
	// -------- REJECT PIECE -------
	// -----------------------------

	void peer_connection::incoming_reject_request(peer_request const& r)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_reject(r)) return;
		}
#endif

		if (is_disconnecting()) return;

		std::deque<pending_block>::iterator i = std::find_if(
			m_download_queue.begin(), m_download_queue.end()
			, bind(match_request, boost::cref(r), bind(&pending_block::block, _1)
			, t->block_size()));
	
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " <== REJECT_PIECE [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif

		piece_block b(-1, 0);
		if (i != m_download_queue.end())
		{
	  		b = i->block;
			m_download_queue.erase(i);
			
			// if the peer is in parole mode, keep the request
			if (peer_info_struct() && peer_info_struct()->on_parole)
			{
				m_request_queue.push_front(b);
			}
			else if (!t->is_seed())
			{
				piece_picker& p = t->picker();
				p.abort_download(b);
			}
		}
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
		else
		{
			(*m_logger) << time_now_string()
				<< " *** PIECE NOT IN REQUEST QUEUE\n";
		}
#endif
		if (has_peer_choked())
		{
			// if we're choked and we got a rejection of
			// a piece in the allowed fast set, remove it
			// from the allow fast set.
			std::vector<int>::iterator i = std::find(
				m_allowed_fast.begin(), m_allowed_fast.end(), r.piece);
			if (i != m_allowed_fast.end()) m_allowed_fast.erase(i);
		}
		else
		{
			std::vector<int>::iterator i = std::find(m_suggested_pieces.begin()
				, m_suggested_pieces.end(), r.piece);
			if (i != m_suggested_pieces.end())
				m_suggested_pieces.erase(i);
		}

		if (m_request_queue.empty() && m_download_queue.size() < 2)
		{
			request_a_block(*t, *this);
			send_block_requests();
		}
	}
	
	// -----------------------------
	// ------- SUGGEST PIECE -------
	// -----------------------------

	void peer_connection::incoming_suggest(int index)
	{
		INVARIANT_CHECK;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
			<< " <== SUGGEST_PIECE [ piece: " << index << " ]\n";
#endif
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_suggest(index)) return;
		}
#endif

		if (is_disconnecting()) return;
		if (index < 0)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string() << " <== INVALID_SUGGEST_PIECE [ " << index << " ]\n";
#endif
			return;
		}
		
		if (t->valid_metadata())
		{
			if (index >= int(m_have_piece.size()))
			{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
				(*m_logger) << time_now_string() << " <== INVALID_ALLOWED_FAST [ " << index << " | s: "
					<< int(m_have_piece.size()) << " ]\n";
#endif
				return;
			}

			// if we already have the piece, we can
			// ignore this message
			if (t->have_piece(index))
				return;
		}

		if (m_suggested_pieces.size() > 9)
			m_suggested_pieces.erase(m_suggested_pieces.begin());

		m_suggested_pieces.push_back(index);

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
			<< " ** SUGGEST_PIECE [ piece: " << index << " added to set: " << m_suggested_pieces.size() << " ]\n";
#endif
	}

	// -----------------------------
	// ---------- UNCHOKE ----------
	// -----------------------------

	void peer_connection::incoming_unchoke()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

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
		if (is_disconnecting()) return;

		t->get_policy().unchoked(*this);
	}

	// -----------------------------
	// -------- INTERESTED ---------
	// -----------------------------

	void peer_connection::incoming_interested()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

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
		if (is_disconnecting()) return;
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

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " <== NOT_INTERESTED\n";
#endif
		m_peer_interested = false;
		if (is_disconnecting()) return;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		if (!is_choked())
		{
			if (m_peer_info && m_peer_info->optimistically_unchoked)
			{
				m_peer_info->optimistically_unchoked = false;
				m_ses.m_optimistic_unchoke_time_scaler = 0;
			}
			t->choke_peer(*this);
			--m_ses.m_num_unchoked;
			m_ses.m_unchoke_time_scaler = 0;
		}

		t->get_policy().not_interested(*this);
	}

	// -----------------------------
	// ----------- HAVE ------------
	// -----------------------------

	void peer_connection::incoming_have(int index)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_have(index)) return;
		}
#endif

		if (is_disconnecting()) return;

		// if we haven't received a bitfield, it was
		// probably omitted, which is the same as 'have_none'
		if (!m_bitfield_received) incoming_have_none();

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
			<< " <== HAVE    [ piece: " << index << "]\n";
#endif

		if (is_disconnecting()) return;

		if (!t->valid_metadata() && index > int(m_have_piece.size()))
		{
			if (index < 65536)
			{
				// if we don't have metadata
				// and we might not have received a bitfield
				// extend the bitmask to fit the new
				// have message
				m_have_piece.resize(index + 1, false);
			}
			else
			{
				// unless the index > 64k, in which case
				// we just ignore it
				return;
			}
		}

		// if we got an invalid message, abort
		if (index >= int(m_have_piece.size()) || index < 0)
		{
			disconnect("got 'have'-message with higher index than the number of pieces", 2);
			return;
		}

		if (m_have_piece[index])
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << "   got redundant HAVE message for index: " << index << "\n";
#endif
			return;
		}

		m_have_piece.set_bit(index);
		++m_num_pieces;

		// only update the piece_picker if
		// we have the metadata and if
		// we're not a seed (in which case
		// we won't have a piece picker)
		if (!t->valid_metadata()) return;

		t->peer_has(index);

		// it is important that we don't disconnect before the
		// torrent has accounted for this piece, otherwise
		// we will have an inconsistent count for it in the
		// piece picker since disconnecting will decrement the
		// count
		if (is_seed())
		{
			t->get_policy().set_seed(m_peer_info, true);
			m_upload_only = true;
			disconnect_if_redundant();
			if (is_disconnecting()) return;
		}

		if (!t->have_piece(index)
			&& !t->is_seed()
			&& !is_interesting()
			&& t->picker().piece_priority(index) != 0)
			t->get_policy().peer_is_interesting(*this);

		// this will disregard all have messages we get within
		// the first two seconds. Since some clients implements
		// lazy bitfields, these will not be reliable to use
		// for an estimated peer download rate.
		if (!peer_info_struct() || time_now() - peer_info_struct()->connected > seconds(2))
		{
			// update bytes downloaded since last timer
			m_remote_bytes_dled += t->torrent_file().piece_size(index);
		}
	}

	// -----------------------------
	// --------- BITFIELD ----------
	// -----------------------------

	void peer_connection::incoming_bitfield(bitfield const& bits)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_bitfield(bits)) return;
		}
#endif

		if (is_disconnecting()) return;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " <== BITFIELD ";

		for (int i = 0; i < int(bits.size()); ++i)
		{
			if (bits[i]) (*m_logger) << "1";
			else (*m_logger) << "0";
		}
		(*m_logger) << "\n";
#endif

		// if we don't have the metedata, we cannot
		// verify the bitfield size
		if (t->valid_metadata()
			&& (bits.size() + 7) / 8 != (m_have_piece.size() + 7) / 8)
		{
			std::stringstream msg;
			msg << "got bitfield with invalid size: " << ((bits.size() + 7) / 8)
				<< "bytes. expected: " << ((m_have_piece.size() + 7) / 8)
				<< " bytes";
			disconnect(msg.str().c_str(), 2);
			return;
		}

		m_bitfield_received = true;

		// if we don't have metadata yet
		// just remember the bitmask
		// don't update the piecepicker
		// (since it doesn't exist yet)
		if (!t->ready_for_connections())
		{
			m_have_piece = bits;
			m_num_pieces = bits.count();
			t->get_policy().set_seed(m_peer_info, m_num_pieces == int(bits.size()));
			return;
		}

		TORRENT_ASSERT(t->valid_metadata());
		
		int num_pieces = bits.count();
		if (num_pieces == int(m_have_piece.size()))
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " *** THIS IS A SEED ***\n";
#endif
			// if this is a web seed. we don't have a peer_info struct
			t->get_policy().set_seed(m_peer_info, true);
			m_upload_only = true;

			m_have_piece.set_all();
			m_num_pieces = num_pieces;
			t->peer_has_all();
			if (!t->is_finished())
				t->get_policy().peer_is_interesting(*this);

			disconnect_if_redundant();

			return;
		}

		// let the torrent know which pieces the
		// peer has
		// if we're a seed, we don't keep track of piece availability
		bool interesting = false;
		if (!t->is_seed())
		{
			t->peer_has(bits);

			for (int i = 0; i < (int)m_have_piece.size(); ++i)
			{
				bool have = bits[i];
				if (have && !m_have_piece[i])
				{
					if (!t->have_piece(i) && t->picker().piece_priority(i) != 0)
						interesting = true;
				}
				else if (!have && m_have_piece[i])
				{
					// this should probably not be allowed
					t->peer_lost(i);
				}
			}
		}

		m_have_piece = bits;
		m_num_pieces = num_pieces;

		if (interesting) t->get_policy().peer_is_interesting(*this);
		else if (upload_only()) disconnect("upload to upload connections");
	}

	void peer_connection::disconnect_if_redundant()
	{
		// we cannot disconnect in a constructor
		TORRENT_ASSERT(m_in_constructor == false);
		if (!m_ses.settings().close_redundant_connections) return;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);
		if (m_upload_only && t->is_finished())
		{
			disconnect("seed to seed");
			return;
		}

		if (m_upload_only
			&& !m_interesting
			&& m_bitfield_received
			&& t->are_files_checked())
		{
			disconnect("uninteresting upload-only peer");
			return;
		}
	}

	// -----------------------------
	// ---------- REQUEST ----------
	// -----------------------------

	void peer_connection::incoming_request(peer_request const& r)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		// if we haven't received a bitfield, it was
		// probably omitted, which is the same as 'have_none'
		if (!m_bitfield_received) incoming_have_none();
		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_request(r)) return;
		}
#endif
		if (is_disconnecting()) return;

		if (!t->valid_metadata())
		{
			// if we don't have valid metadata yet,
			// we shouldn't get a request
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string()
				<< " <== UNEXPECTED_REQUEST [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " | "
				"i: " << m_peer_interested << " | "
				"t: " << t->torrent_file().piece_size(r.piece) << " | "
				"n: " << t->torrent_file().num_pieces() << " ]\n";

			(*m_logger) << time_now_string()
				<< " ==> REJECT_PIECE [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " ]\n";
#endif
			write_reject_request(r);
			return;
		}

		if (int(m_requests.size()) > m_ses.settings().max_allowed_in_request_queue)
		{
			// don't allow clients to abuse our
			// memory consumption.
			// ignore requests if the client
			// is making too many of them.
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string()
				<< " <== TOO MANY REQUESTS [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " | "
				"i: " << m_peer_interested << " | "
				"t: " << t->torrent_file().piece_size(r.piece) << " | "
				"n: " << t->torrent_file().num_pieces() << " ]\n";

			(*m_logger) << time_now_string()
				<< " ==> REJECT_PIECE [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " ]\n";
#endif
			write_reject_request(r);
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
			&& m_peer_interested
			&& r.length <= t->block_size())
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " <== REQUEST [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif
			// if we have choked the client
			// ignore the request
			if (m_choked && m_accept_fast.find(r.piece) == m_accept_fast.end())
			{
				write_reject_request(r);
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
				(*m_logger) << time_now_string()
					<< " *** REJECTING REQUEST [ peer choked and piece not in allowed fast set ]\n";
				(*m_logger) << time_now_string()
					<< " ==> REJECT_PIECE [ "
					"piece: " << r.piece << " | "
					"s: " << r.start << " | "
					"l: " << r.length << " ]\n";
#endif
			}
			else
			{
				m_requests.push_back(r);
				m_last_incoming_request = time_now();
				fill_send_buffer();
			}
		}
		else
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string()
				<< " <== INVALID_REQUEST [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " | "
				"i: " << m_peer_interested << " | "
				"t: " << t->torrent_file().piece_size(r.piece) << " | "
				"n: " << t->torrent_file().num_pieces() << " | "
				"h: " << t->have_piece(r.piece) << " | "
				"block_limit: " << t->block_size() << " ]\n";

			(*m_logger) << time_now_string()
				<< " ==> REJECT_PIECE [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " ]\n";
#endif

			write_reject_request(r);
			++m_num_invalid_requests;

			if (t->alerts().should_post<invalid_request_alert>())
			{
				t->alerts().post_alert(invalid_request_alert(
					t->get_handle(), m_remote, m_peer_id, r));
			}
		}
	}

	void peer_connection::incoming_piece_fragment()
	{
		m_last_piece = time_now();
	}

#ifdef TORRENT_DEBUG
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
					(t->torrent_file().piece_length() + t->block_size() - 1) / t->block_size());

				std::vector<piece_picker::downloading_piece> const& dl_queue
					= t->picker().get_download_queue();

				for (std::vector<piece_picker::downloading_piece>::const_iterator i =
					dl_queue.begin(); i != dl_queue.end(); ++i)
				{
					TORRENT_ASSERT(i->finished <= blocks_per_piece);
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
		char* buffer = m_ses.allocate_disk_buffer();
		if (buffer == 0)
		{
			disconnect("out of memory");
			return;
		}
		disk_buffer_holder holder(m_ses, buffer);
		std::memcpy(buffer, data, p.length);
		incoming_piece(p, holder);
	}

	void peer_connection::incoming_piece(peer_request const& p, disk_buffer_holder& data)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		TORRENT_ASSERT(!m_disk_recv_buffer);
		TORRENT_ASSERT(m_disk_recv_buffer_size == 0);

#ifdef TORRENT_CORRUPT_DATA
		// corrupt all pieces from certain peers
		if (m_remote.address().is_v4()
			&& (m_remote.address().to_v4().to_ulong() & 0xf) == 0)
		{
			data.get()[0] = ~data.get()[0];
		}
#endif

		// if we haven't received a bitfield, it was
		// probably omitted, which is the same as 'have_none'
		if (!m_bitfield_received) incoming_have_none();
		if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_piece(p, data)) return;
		}
#endif
		if (is_disconnecting()) return;

#ifdef TORRENT_DEBUG
		check_postcondition post_checker_(t);
#if !defined TORRENT_DISABLE_INVARIANT_CHECKS && defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		t->check_invariant();
#endif
#endif

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
			<< " <== PIECE   [ piece: " << p.piece << " | "
			"s: " << p.start << " | "
			"l: " << p.length << " | "
			"ds: " << statistics().download_rate() << " | "
			"qs: " << int(m_desired_queue_size) << " ]\n";
#endif

		if (p.length == 0)
		{
			if (t->alerts().should_post<peer_error_alert>())
			{
				t->alerts().post_alert(peer_error_alert(t->get_handle(), m_remote
					, m_peer_id, "peer sent 0 length piece"));
			}
			return;
		}

		if (!verify_piece(p))
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string()
				<< " <== INVALID_PIECE [ piece: " << p.piece << " | "
				"start: " << p.start << " | "
				"length: " << p.length << " ]\n";
#endif
			disconnect("got invalid piece packet", 2);
			return;
		}

		// if we're already seeding, don't bother,
		// just ignore it
		if (t->is_seed())
		{
			t->add_redundant_bytes(p.length);
			return;
		}

		ptime now = time_now();

		piece_picker& picker = t->picker();
		piece_manager& fs = t->filesystem();

		std::vector<piece_block> finished_blocks;
		piece_block block_finished(p.piece, p.start / t->block_size());
		TORRENT_ASSERT(verify_piece(p));

		std::deque<pending_block>::iterator b
			= std::find_if(
				m_download_queue.begin()
				, m_download_queue.end()
				, has_block(block_finished));

		if (b == m_download_queue.end())
		{
			if (t->alerts().should_post<unwanted_block_alert>())
			{
				t->alerts().post_alert(unwanted_block_alert(t->get_handle(), m_remote
					, m_peer_id, block_finished.block_index, block_finished.piece_index));
			}
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << " *** The block we just got was not in the "
				"request queue ***\n";
#endif
			t->add_redundant_bytes(p.length);
			request_a_block(*t, *this);
			send_block_requests();
			return;
		}
#ifdef TORRENT_DEBUG
		pending_block pending_b = *b;
#endif

		int block_index = b - m_download_queue.begin();
		TORRENT_ASSERT(m_download_queue[block_index] == pending_b);
		for (int i = 0; i < block_index; ++i)
		{
			pending_block& qe = m_download_queue[i];
			TORRENT_ASSERT(m_download_queue[block_index] == pending_b);
			TORRENT_ASSERT(i < block_index);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string()
				<< " *** SKIPPED_PIECE [ piece: " << qe.block.piece_index << " | "
				"b: " << qe.block.block_index << " | skip: " << qe.skipped << " | "
				"dqs: " << int(m_desired_queue_size) << "] ***\n";
#endif

			++qe.skipped;
			// if the number of times a block is skipped by out of order
			// blocks exceeds the size of the outstanding queue, assume that
			// the other end dropped the request.
			// never use this feature in the 0.14 branch. It's optional in 0.15+
			if (false
				&& qe.skipped > m_desired_queue_size)
			{
				if (m_ses.m_alerts.should_post<request_dropped_alert>())
					m_ses.m_alerts.post_alert(request_dropped_alert(t->get_handle()
						, remote(), pid(), qe.block.block_index, qe.block.piece_index));

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
				(*m_logger) << time_now_string()
					<< " *** DROPPED_PIECE [ piece: " << qe.block.piece_index << " | "
					"b: " << qe.block.block_index << " | skip: " << qe.skipped << " | "
					"dqs: " << int(m_desired_queue_size) << "] ***\n";
#endif
				picker.abort_download(qe.block);
				TORRENT_ASSERT(m_download_queue[block_index] == pending_b);
				m_download_queue.erase(m_download_queue.begin() + i);
				--i;
				--block_index;
				TORRENT_ASSERT(m_download_queue[block_index] == pending_b);
			}
		}
		TORRENT_ASSERT(int(m_download_queue.size()) > block_index);
		b = m_download_queue.begin() + block_index;
		TORRENT_ASSERT(*b == pending_b);
		
		// if the block we got is already finished, then ignore it
		if (picker.is_downloaded(block_finished))
		{
			t->add_redundant_bytes(p.length);

			m_download_queue.erase(b);
			m_timeout_extend = 0;

			if (!m_download_queue.empty())
				m_requested = now;

			request_a_block(*t, *this);
			send_block_requests();
			return;
		}
		
		if (total_seconds(now - m_requested)
			< m_ses.settings().request_timeout
			&& m_snubbed)
		{
			m_snubbed = false;
			if (m_ses.m_alerts.should_post<peer_unsnubbed_alert>())
			{
				m_ses.m_alerts.post_alert(peer_unsnubbed_alert(t->get_handle()
					, m_remote, m_peer_id));
			}
		}

		fs.async_write(p, data, bind(&peer_connection::on_disk_write_complete
			, self(), _1, _2, p, t));
		m_outstanding_writing_bytes += p.length;
		TORRENT_ASSERT(m_channel_state[download_channel] == peer_info::bw_idle);
		m_download_queue.erase(b);

		if (m_outstanding_writing_bytes >= m_ses.settings().max_outstanding_disk_bytes_per_connection
			&& t->alerts().should_post<performance_alert>())
		{
			t->alerts().post_alert(performance_alert(t->get_handle()
				, performance_alert::outstanding_disk_buffer_limit_reached));
		}

		if (!m_download_queue.empty())
		{
			m_timeout_extend = (std::max)(m_timeout_extend
				- m_ses.settings().request_timeout, 0);
			m_requested += seconds(m_ses.settings().request_timeout);
			if (m_requested > now) m_requested = now;
		}
		else
		{
			m_timeout_extend = 0;
		}

		// did we request this block from any other peers?
		bool multi = picker.num_peers(block_finished) > 1;
		picker.mark_as_writing(block_finished, peer_info_struct());

		TORRENT_ASSERT(picker.num_peers(block_finished) == 0);
		// if we requested this block from other peers, cancel it now
		if (multi) t->cancel_block(block_finished);

		TORRENT_ASSERT(picker.num_peers(block_finished) == 0);

#if defined TORRENT_DEBUG && !defined TORRENT_DISABLE_INVARIANT_CHECKS \
	&& defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		t->check_invariant();
#endif
		request_a_block(*t, *this);
		send_block_requests();
	}

	void peer_connection::on_disk_write_complete(int ret, disk_io_job const& j
		, peer_request p, boost::shared_ptr<torrent> t)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		m_outstanding_writing_bytes -= p.length;
		TORRENT_ASSERT(m_outstanding_writing_bytes >= 0);

#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
//		(*m_ses.m_logger) << time_now_string() << " *** DISK_WRITE_COMPLETE [ p: "
//			<< p.piece << " o: " << p.start << " ]\n";
#endif
		// in case the outstanding bytes just dropped down
		// to allow to receive more data
		setup_receive();

		piece_block block_finished(p.piece, p.start / t->block_size());

		if (ret == -1 || !t)
		{
			if (t->has_picker()) t->picker().write_failed(block_finished);

			if (!t)
			{
				disconnect(j.str.c_str());
				return;
			}
		
			if (t->alerts().should_post<file_error_alert>())
				t->alerts().post_alert(file_error_alert(j.error_file, t->get_handle(), j.str));
			t->set_error(j.str);
			t->pause();
			return;
		}

		if (t->is_seed()) return;

		piece_picker& picker = t->picker();

		TORRENT_ASSERT(p.piece == j.piece);
		TORRENT_ASSERT(p.start == j.offset);
		TORRENT_ASSERT(picker.num_peers(block_finished) == 0);
		picker.mark_as_finished(block_finished, peer_info_struct());
		if (t->alerts().should_post<block_finished_alert>())
		{
			t->alerts().post_alert(block_finished_alert(t->get_handle(), 
				remote(), pid(), block_finished.block_index, block_finished.piece_index));
		}

		if (t->is_aborted()) return;

		// did we just finish the piece?
		if (picker.is_piece_finished(p.piece))
		{
#ifdef TORRENT_DEBUG
			check_postcondition post_checker2_(t, false);
#endif
			t->async_verify_piece(p.piece, bind(&torrent::piece_finished, t
				, p.piece, _1));
		}

		if (!t->is_seed() && !m_torrent.expired())
		{
			// this is a free function defined in policy.cpp
			request_a_block(*t, *this);
			send_block_requests();
		}

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
		if (is_disconnecting()) return;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
			<< " <== CANCEL  [ piece: " << r.piece << " | s: " << r.start << " | l: " << r.length << " ]\n";
#endif

		std::deque<peer_request>::iterator i
			= std::find(m_requests.begin(), m_requests.end(), r);

		if (i != m_requests.end())
		{
			m_requests.erase(i);
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " ==> REJECT_PIECE [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " ]\n";
#endif
			write_reject_request(r);
		}
		else
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
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

	// -----------------------------
	// --------- HAVE ALL ----------
	// -----------------------------

	void peer_connection::incoming_have_all()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		// we cannot disconnect in a constructor, and
		// this function may end up doing that
		TORRENT_ASSERT(m_in_constructor == false);

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " <== HAVE_ALL\n";
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_have_all()) return;
		}
#endif
		if (is_disconnecting()) return;

		m_have_all = true;

		t->get_policy().set_seed(m_peer_info, true);
		m_upload_only = true;
		m_bitfield_received = true;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << " *** THIS IS A SEED ***\n";
#endif

		// if we don't have metadata yet
		// just remember the bitmask
		// don't update the piecepicker
		// (since it doesn't exist yet)
		if (!t->ready_for_connections())
		{
			// assume seeds are interesting when we
			// don't even have the metadata
			t->get_policy().peer_is_interesting(*this);

			disconnect_if_redundant();
			// TODO: this might need something more
			// so that once we have the metadata
			// we can construct a full bitfield
			return;
		}

		TORRENT_ASSERT(!m_have_piece.empty());
		m_have_piece.set_all();
		m_num_pieces = m_have_piece.size();
		
		t->peer_has_all();

		// if we're finished, we're not interested
		if (t->is_finished()) send_not_interested();
		else t->get_policy().peer_is_interesting(*this);

		disconnect_if_redundant();
	}
	
	// -----------------------------
	// --------- HAVE NONE ---------
	// -----------------------------

	void peer_connection::incoming_have_none()
	{
		INVARIANT_CHECK;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " <== HAVE_NONE\n";
#endif

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_have_none()) return;
		}
#endif
		if (is_disconnecting()) return;
		t->get_policy().set_seed(m_peer_info, false);
		m_bitfield_received = true;

		// we're never interested in a peer that doesn't have anything
		send_not_interested();

		TORRENT_ASSERT(!m_have_piece.empty() || !t->ready_for_connections());
		disconnect_if_redundant();
	}

	// -----------------------------
	// ------- ALLOWED FAST --------
	// -----------------------------

	void peer_connection::incoming_allowed_fast(int index)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " <== ALLOWED_FAST [ " << index << " ]\n";
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_allowed_fast(index)) return;
		}
#endif
		if (is_disconnecting()) return;
		if (index < 0)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string() << " <== INVALID_ALLOWED_FAST [ " << " ]\n";
#endif
			return;
		}

		if (t->valid_metadata())
		{
			if (index >= int(m_have_piece.size()))
			{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
				(*m_logger) << time_now_string() << " <== INVALID_ALLOWED_FAST [ " << index << " | s: "
					<< int(m_have_piece.size()) << " ]\n";
#endif
				return;
			}

			// if we already have the piece, we can
			// ignore this message
			if (t->have_piece(index))
				return;
		}

		// if we don't have the metadata, we'll verify
		// this piece index later
		m_allowed_fast.push_back(index);

		// if the peer has the piece and we want
		// to download it, request it
		if (int(m_have_piece.size()) > index
			&& m_have_piece[index]
			&& t->valid_metadata()
			&& t->has_picker()
			&& t->picker().piece_priority(index) > 0)
		{
			t->get_policy().peer_is_interesting(*this);
		}
	}

	std::vector<int> const& peer_connection::allowed_fast()
	{
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		m_allowed_fast.erase(std::remove_if(m_allowed_fast.begin()
			, m_allowed_fast.end(), bind(&torrent::have_piece, t, _1))
			, m_allowed_fast.end());

		// TODO: sort the allowed fast set in priority order
		return m_allowed_fast;
	}

	void peer_connection::add_request(piece_block const& block)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		TORRENT_ASSERT(t->valid_metadata());
		TORRENT_ASSERT(block.piece_index >= 0);
		TORRENT_ASSERT(block.piece_index < t->torrent_file().num_pieces());
		TORRENT_ASSERT(block.block_index >= 0);
		TORRENT_ASSERT(block.block_index < t->torrent_file().piece_size(block.piece_index));
		TORRENT_ASSERT(!t->picker().is_requested(block) || (t->picker().num_peers(block) > 0));
		TORRENT_ASSERT(!t->have_piece(block.piece_index));
		TORRENT_ASSERT(std::find_if(m_download_queue.begin(), m_download_queue.end()
			, has_block(block)) == m_download_queue.end());
		TORRENT_ASSERT(std::find(m_request_queue.begin(), m_request_queue.end()
			, block) == m_request_queue.end());

		piece_picker::piece_state_t state;
		peer_speed_t speed = peer_speed();
		char const* speedmsg = 0;
		if (speed == fast)
		{
			speedmsg = "fast";
			state = piece_picker::fast;
		}
		else if (speed == medium)
		{
			speedmsg = "medium";
			state = piece_picker::medium;
		}
		else
		{
			speedmsg = "slow";
			state = piece_picker::slow;
		}

		if (!t->picker().mark_as_downloading(block, peer_info_struct(), state))
			return;

		if (t->alerts().should_post<block_downloading_alert>())
		{
			t->alerts().post_alert(block_downloading_alert(t->get_handle(), 
				remote(), pid(), speedmsg, block.block_index, block.piece_index));
		}

		m_request_queue.push_back(block);
	}

	void peer_connection::cancel_request(piece_block const& block)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		// this peer might be disconnecting
		if (!t) return;

		TORRENT_ASSERT(t->valid_metadata());

		TORRENT_ASSERT(block.piece_index >= 0);
		TORRENT_ASSERT(block.piece_index < t->torrent_file().num_pieces());
		TORRENT_ASSERT(block.block_index >= 0);
		TORRENT_ASSERT(block.block_index < t->torrent_file().piece_size(block.piece_index));

		// if all the peers that requested this block has been
		// cancelled, then just ignore the cancel.
		if (!t->picker().is_requested(block)) return;

		std::deque<pending_block>::iterator it
			= std::find_if(m_download_queue.begin(), m_download_queue.end(), has_block(block));
		if (it == m_download_queue.end())
		{
			std::deque<piece_block>::iterator rit = std::find(m_request_queue.begin()
				, m_request_queue.end(), block);

			// when a multi block is received, it is cancelled
			// from all peers, so if this one hasn't requested
			// the block, just ignore to cancel it.
			if (rit == m_request_queue.end()) return;

			t->picker().abort_download(block);
			m_request_queue.erase(rit);
			// since we found it in the request queue, it means it hasn't been
			// sent yet, so we don't have to send a cancel.
			return;
		}

		int block_offset = block.block_index * t->block_size();
		int block_size
			= (std::min)(t->torrent_file().piece_size(block.piece_index)-block_offset,
			t->block_size());
		TORRENT_ASSERT(block_size > 0);
		TORRENT_ASSERT(block_size <= t->block_size());

		peer_request r;
		r.piece = block.piece_index;
		r.start = block_offset;
		r.length = block_size;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
				<< " ==> CANCEL  [ piece: " << block.piece_index << " | s: "
				<< block_offset << " | l: " << block_size << " | " << block.block_index << " ]\n";
#endif
		write_cancel(r);
	}

	void peer_connection::send_choke()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_peer_info || !m_peer_info->optimistically_unchoked);

		if (m_choked) return;
		write_choke();
		m_choked = true;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " ==> CHOKE\n";
#endif
#ifdef TORRENT_DEBUG
		m_last_choke = time_now();
#endif
		m_num_invalid_requests = 0;

		// reject the requests we have in the queue
		// except the allowed fast pieces
		for (std::deque<peer_request>::iterator i = m_requests.begin();
			i != m_requests.end();)
		{
			if (m_accept_fast.count(i->piece))
			{
				++i;
				continue;
			}

			peer_request const& r = *i;
			write_reject_request(r);

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " ==> REJECT_PIECE [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " ]\n";
#endif
			i = m_requests.erase(i);
		}
	}

	bool peer_connection::send_unchoke()
	{
		INVARIANT_CHECK;

		if (!m_choked) return false;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t->ready_for_connections()) return false;
		m_last_unchoke = time_now();
		write_unchoke();
		m_choked = false;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " ==> UNCHOKE\n";
#endif
		return true;
	}

	void peer_connection::send_interested()
	{
		if (m_interesting) return;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t->ready_for_connections()) return;
		m_interesting = true;
		write_interested();

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " ==> INTERESTED\n";
#endif
	}

	void peer_connection::send_not_interested()
	{
		// we cannot disconnect in a constructor, and
		// this function may end up doing that
		TORRENT_ASSERT(m_in_constructor == false);

		if (!m_interesting) return;
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t->ready_for_connections()) return;
		m_interesting = false;
		write_not_interested();

		m_became_uninteresting = time_now();

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " ==> NOT_INTERESTED\n";
#endif
		disconnect_if_redundant();
	}

	void peer_connection::send_block_requests()
	{
		INVARIANT_CHECK;
		
		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		if ((int)m_download_queue.size() >= m_desired_queue_size) return;

		bool empty_download_queue = m_download_queue.empty();

		while (!m_request_queue.empty()
			&& (int)m_download_queue.size() < m_desired_queue_size)
		{
			piece_block block = m_request_queue.front();

			int block_offset = block.block_index * t->block_size();
			int block_size = (std::min)(t->torrent_file().piece_size(
				block.piece_index) - block_offset, t->block_size());
			TORRENT_ASSERT(block_size > 0);
			TORRENT_ASSERT(block_size <= t->block_size());

			peer_request r;
			r.piece = block.piece_index;
			r.start = block_offset;
			r.length = block_size;

			m_request_queue.pop_front();
			if (t->is_seed()) continue;
			// this can happen if a block times out, is re-requested and
			// then arrives "unexpectedly"
			if (t->picker().is_finished(block) || t->picker().is_downloaded(block))
				continue;

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
				int blocks_per_piece = t->torrent_file().piece_length() / t->block_size();

				while (!m_request_queue.empty())
				{
					// check to see if this block is connected to the previous one
					// if it is, merge them, otherwise, break this merge loop
					piece_block const& front = m_request_queue.front();
					if (front.piece_index * blocks_per_piece + front.block_index
						!= block.piece_index * blocks_per_piece + block.block_index + 1)
						break;
					block = m_request_queue.front();
					m_request_queue.pop_front();
					m_download_queue.push_back(block);

#ifdef TORRENT_VERBOSE_LOGGING
					(*m_logger) << time_now_string()
						<< " *** MERGING REQUEST ** [ "
						"piece: " << block.piece_index << " | "
						"block: " << block.block_index << " ]\n";
#endif

					block_offset = block.block_index * t->block_size();
					block_size = (std::min)(t->torrent_file().piece_size(
						block.piece_index) - block_offset, t->block_size());
					TORRENT_ASSERT(block_size > 0);
					TORRENT_ASSERT(block_size <= t->block_size());

					r.length += block_size;
				}
			}

			// the verification will fail for coalesced blocks
			TORRENT_ASSERT(verify_piece(r) || m_request_large_blocks);
			
#ifndef TORRENT_DISABLE_EXTENSIONS
			bool handled = false;
			for (extension_list_t::iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end; ++i)
			{
				if ((handled = (*i)->write_request(r))) break;
			}
			if (is_disconnecting()) return;
			if (!handled)
#endif
			{
				write_request(r);
				m_last_request = time_now();
			}

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string()
				<< " ==> REQUEST [ "
				"piece: " << r.piece << " | "
				"s: " << r.start << " | "
				"l: " << r.length << " | "
				"ds: " << statistics().download_rate() << " B/s | "
				"dqs: " << int(m_desired_queue_size) << " "
				"rqs: " << int(m_download_queue.size()) << " "
				"blk: " << (m_request_large_blocks?"large":"single") << " ]\n";
#endif
		}
		m_last_piece = time_now();

		if (!m_download_queue.empty()
			&& empty_download_queue)
		{
			// This means we just added a request to this connection
			m_requested = time_now();
		}
	}

	void peer_connection::timed_out()
	{
		TORRENT_ASSERT(m_connecting);
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		error_code ec;
		(*m_ses.m_logger) << time_now_string() << " CONNECTION TIMED OUT: " << m_remote.address().to_string(ec)
			<< "\n";
#endif
		disconnect("timed out: connect", 1);
	}

	// the error argument defaults to 0, which means deliberate disconnect
	// 1 means unexpected disconnect/error
	// 2 protocol error (client sent something invalid)
	void peer_connection::disconnect(char const* message, int error)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

#ifdef TORRENT_DEBUG
		m_disconnect_started = true;
#endif

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
		switch (error)
		{
		case 0:
			(*m_logger) << "*** CONNECTION CLOSED " << message << "\n";
			break;
		case 1:
			(*m_logger) << "*** CONNECTION FAILED " << message << "\n";
			break;
		case 2:
			(*m_logger) << "*** PEER ERROR " << message << "\n";
			break;
		}
#endif
		// we cannot do this in a constructor
		TORRENT_ASSERT(m_in_constructor == false);
		if (error > 0) m_failed = true;
		if (m_disconnecting) return;
		boost::intrusive_ptr<peer_connection> me(this);

		INVARIANT_CHECK;

		if (m_connecting && m_connection_ticket >= 0)
		{
			m_ses.m_half_open.done(m_connection_ticket);
			m_connection_ticket = -1;
		}

		boost::shared_ptr<torrent> t = m_torrent.lock();
		torrent_handle handle;
		if (t) handle = t->get_handle();

		if (message)
		{
			if (error > 1 && m_ses.m_alerts.should_post<peer_error_alert>())
			{
				m_ses.m_alerts.post_alert(
					peer_error_alert(handle, remote(), pid(), message));
			}
			else if (error <= 1 && m_ses.m_alerts.should_post<peer_disconnected_alert>())
			{
				m_ses.m_alerts.post_alert(
					peer_disconnected_alert(handle, remote(), pid(), message));
			}
		}

		if (t)
		{
			// make sure we keep all the stats!
			calc_ip_overhead();
			t->add_stats(statistics());

			if (t->has_picker())
			{
				piece_picker& picker = t->picker();

				while (!m_download_queue.empty())
				{
					picker.abort_download(m_download_queue.back().block);
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

#if defined TORRENT_DEBUG && defined TORRENT_EXPENSIVE_INVARIANT_CHECKS
		// since this connection doesn't have a torrent reference
		// no torrent should have a reference to this connection either
		for (aux::session_impl::torrent_map::const_iterator i = m_ses.m_torrents.begin()
			, end(m_ses.m_torrents.end()); i != end; ++i)
			TORRENT_ASSERT(!i->second->has_peer(this));
#endif

		m_disconnecting = true;
		error_code ec;
		m_socket->close(ec);
		m_ses.close_connection(this, message);

		// we should only disconnect while we still have
		// at least one reference left to the connection
		TORRENT_ASSERT(refcount() > 0);
	}

	void peer_connection::set_upload_limit(int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		if (limit == -1) limit = (std::numeric_limits<int>::max)();
		if (limit < 10) limit = 10;
		m_upload_limit = limit;
		m_bandwidth_limit[upload_channel].throttle(m_upload_limit);
	}

	void peer_connection::set_download_limit(int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		if (limit == -1) limit = (std::numeric_limits<int>::max)();
		if (limit < 10) limit = 10;
		m_download_limit = limit;
		m_bandwidth_limit[download_channel].throttle(m_download_limit);
	}

	size_type peer_connection::share_diff() const
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		float ratio = t->ratio();

		// if we have an infinite ratio, just say we have downloaded
		// much more than we have uploaded. And we'll keep uploading.
		if (ratio == 0.f)
			return (std::numeric_limits<size_type>::max)();

		return m_free_upload
			+ static_cast<size_type>(m_statistics.total_payload_download() * ratio)
			- m_statistics.total_payload_upload();
	}

	// defined in upnp.cpp
	bool is_local(address const& a);

	bool peer_connection::on_local_network() const
	{
		if (libtorrent::is_local(m_remote.address())
			|| is_loopback(m_remote.address())) return true;
		return false;
	}

	void peer_connection::get_peer_info(peer_info& p) const
	{
		TORRENT_ASSERT(!associated_torrent().expired());

		ptime now = time_now();

		p.download_rate_peak = m_download_rate_peak;
		p.upload_rate_peak = m_upload_rate_peak;
		p.rtt = m_rtt;
		p.down_speed = statistics().download_rate();
		p.up_speed = statistics().upload_rate();
		p.payload_down_speed = statistics().download_payload_rate();
		p.payload_up_speed = statistics().upload_payload_rate();
		p.pid = pid();
		p.ip = remote();
		p.pending_disk_bytes = m_outstanding_writing_bytes;
		p.send_quota = m_bandwidth_limit[upload_channel].quota_left();
		p.receive_quota = m_bandwidth_limit[download_channel].quota_left();
		if (m_download_queue.empty()) p.request_timeout = -1;
		else p.request_timeout = total_seconds(m_requested - now) + m_ses.settings().request_timeout
			+ m_timeout_extend;
#ifndef TORRENT_DISABLE_GEO_IP
		p.inet_as_name = m_inet_as_name;
#endif
		
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

		p.download_queue_length = int(download_queue().size() + m_request_queue.size());
		p.requests_in_buffer = int(m_requests_in_buffer.size());
		p.target_dl_queue_length = int(desired_queue_size());
		p.upload_queue_length = int(upload_queue().size());

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
		p.last_request = now - m_last_request;
		p.last_active = now - (std::max)(m_last_sent, m_last_receive);

		// this will set the flags so that we can update them later
		p.flags = 0;
		get_specific_peer_info(p);

		p.flags |= is_seed() ? peer_info::seed : 0;
		p.flags |= m_snubbed ? peer_info::snubbed : 0;
		p.flags |= m_upload_only ? peer_info::upload_only : 0;
		if (peer_info_struct())
		{
			policy::peer* pi = peer_info_struct();
			p.source = pi->source;
			p.failcount = pi->failcount;
			p.num_hashfails = pi->hashfails;
			p.flags |= pi->on_parole ? peer_info::on_parole : 0;
			p.flags |= pi->optimistically_unchoked ? peer_info::optimistic_unchoke : 0;
#ifndef TORRENT_DISABLE_GEO_IP
			p.inet_as = pi->inet_as->first;
#endif
		}
		else
		{
			p.source = 0;
			p.failcount = 0;
			p.num_hashfails = 0;
			p.remote_dl_rate = 0;
#ifndef TORRENT_DISABLE_GEO_IP
			p.inet_as = 0xffff;
#endif
		}

		p.remote_dl_rate = m_remote_dl_rate;
		p.send_buffer_size = m_send_buffer.capacity();
		p.used_send_buffer = m_send_buffer.size();
		p.receive_buffer_size = m_recv_buffer.capacity() + m_disk_recv_buffer_size;
		p.used_receive_buffer = m_recv_pos;
		p.write_state = m_channel_state[upload_channel];
		p.read_state = m_channel_state[download_channel];
		
		// pieces may be empty if we don't have metadata yet
		if (p.pieces.size() == 0) p.progress = 0.f;
		else p.progress = (float)p.pieces.count() / (float)p.pieces.size();
	}

	// allocates a disk buffer of size 'disk_buffer_size' and replaces the
	// end of the current receive buffer with it. i.e. the receive pos
	// must be <= packet_size - disk_buffer_size
	// the disk buffer can be accessed through release_disk_receive_buffer()
	// when it is queried, the responsibility to free it is transferred
	// to the caller
	bool peer_connection::allocate_disk_receive_buffer(int disk_buffer_size)
	{
		INVARIANT_CHECK;
		
		TORRENT_ASSERT(m_packet_size > 0);
		TORRENT_ASSERT(m_recv_pos <= m_packet_size - disk_buffer_size);
		TORRENT_ASSERT(!m_disk_recv_buffer);
		TORRENT_ASSERT(disk_buffer_size <= 16 * 1024);

		if (disk_buffer_size == 0) return true;

		if (disk_buffer_size > 16 * 1024)
		{
			disconnect("invalid piece size", 2);
			return false;
		}

		m_disk_recv_buffer.reset(m_ses.allocate_disk_buffer());
		if (!m_disk_recv_buffer)
		{
			disconnect("out of memory");
			return false;
		}
		m_disk_recv_buffer_size = disk_buffer_size;
		return true;
	}

	char* peer_connection::release_disk_receive_buffer()
	{
		m_disk_recv_buffer_size = 0;
		return m_disk_recv_buffer.release();
	}
	
	// size = the packet size to remove from the receive buffer
	// packet_size = the next packet size to receive in the buffer
	void peer_connection::cut_receive_buffer(int size, int packet_size)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(packet_size > 0);
		TORRENT_ASSERT(int(m_recv_buffer.size()) >= size);
		TORRENT_ASSERT(int(m_recv_buffer.size()) >= m_recv_pos);
		TORRENT_ASSERT(m_recv_pos >= size);

		if (size > 0)		
			std::memmove(&m_recv_buffer[0], &m_recv_buffer[0] + size, m_recv_pos - size);

		m_recv_pos -= size;

#ifdef TORRENT_DEBUG
		std::fill(m_recv_buffer.begin() + m_recv_pos, m_recv_buffer.end(), 0);
#endif

		m_packet_size = packet_size;
	}

	void peer_connection::calc_ip_overhead()
	{
		m_statistics.calc_ip_overhead();
	}

	void peer_connection::second_tick(float tick_interval)
	{
		ptime now(time_now());
		boost::intrusive_ptr<peer_connection> me(self());

		// the invariant check must be run before me is destructed
		// in case the peer got disconnected
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t || m_disconnecting)
		{
			m_ses.m_half_open.done(m_connection_ticket);
			m_connecting = false;
			disconnect("torrent aborted");
			return;
		}

		on_tick();

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			(*i)->tick();
		}
		if (is_disconnecting()) return;
#endif

		// if the peer hasn't said a thing for a certain
		// time, it is considered to have timed out
		time_duration d;
		d = now - m_last_receive;
		if (d > seconds(m_timeout) && !m_connecting)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string() << " *** LAST ACTIVITY [ "
				<< total_seconds(d) << " seconds ago ] ***\n";
#endif
			disconnect("timed out: inactivity");
			return;
		}

		// do not stall waiting for a handshake
		if (!m_connecting
			&& in_handshake()
			&& d > seconds(m_ses.settings().handshake_timeout))
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string() << " *** NO HANDSHAKE [ waited "
				<< total_seconds(d) << " seconds ] ***\n";
#endif
			disconnect("timed out: no handshake");
			return;
		}

		// disconnect peers that we unchoked, but
		// they didn't send a request within 20 seconds.
		// but only if we're a seed
		d = now - (std::max)(m_last_unchoke, m_last_incoming_request);
		if (!m_connecting
			&& m_requests.empty()
			&& !m_choked
			&& m_peer_interested
			&& t && t->is_finished()
			&& d > seconds(20))
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string() << " *** NO REQUEST [ t: "
				<< total_seconds(d) << " ] ***\n";
#endif
			disconnect("timed out: no request when unchoked");
			return;
		}

		// if the peer hasn't become interested and we haven't
		// become interested in the peer for 10 minutes, it
		// has also timed out.
		time_duration d1;
		time_duration d2;
		d1 = now - m_became_uninterested;
		d2 = now - m_became_uninteresting;
		time_duration time_limit = seconds(
			m_ses.settings().inactivity_timeout);

		// don't bother disconnect peers we haven't been interested
		// in (and that hasn't been interested in us) for a while
		// unless we have used up all our connection slots
		if (!m_interesting
			&& !m_peer_interested
			&& d1 > time_limit
			&& d2 > time_limit
			&& (m_ses.num_connections() >= m_ses.max_connections()
			|| (t && t->num_peers() >= t->max_connections())))
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string() << " *** MUTUAL NO INTEREST [ "
				"t1: " << total_seconds(d1) << " | "
				"t2: " << total_seconds(d2) << " ] ***\n";
#endif
			disconnect("timed out: no interest");
			return;
		}

		if (!m_download_queue.empty()
			&& now > m_requested + seconds(m_ses.settings().request_timeout
			+ m_timeout_extend))
		{
			snub_peer();
		}

		// if we haven't sent something in too long, send a keep-alive
		keep_alive();

		m_ignore_bandwidth_limits = m_ses.settings().ignore_limits_on_local_network
			&& on_local_network();

		m_statistics.second_tick(tick_interval);

		if (m_statistics.upload_payload_rate() > m_upload_rate_peak)
		{
			m_upload_rate_peak = m_statistics.upload_payload_rate();
		}
		if (m_statistics.download_payload_rate() > m_download_rate_peak)
		{
			m_download_rate_peak = m_statistics.download_payload_rate();
#ifndef TORRENT_DISABLE_GEO_IP
			if (peer_info_struct())
			{
				std::pair<const int, int>* as_stats = peer_info_struct()->inet_as;
				if (as_stats && as_stats->second < m_download_rate_peak)
					as_stats->second = m_download_rate_peak;
			}
#endif
		}
		if (is_disconnecting()) return;

		if (!t->ready_for_connections()) return;

		// calculate the desired download queue size
		const float queue_time = m_ses.settings().request_queue_time;
		// (if the latency is more than this, the download will stall)
		// so, the queue size is queue_time * down_rate / 16 kiB
		// (16 kB is the size of each request)
		// the minimum number of requests is 2 and the maximum is 48
		// the block size doesn't have to be 16. So we first query the
		// torrent for it
		const int block_size = t->block_size();
		TORRENT_ASSERT(block_size > 0);
		
		if (m_snubbed)
		{
			m_desired_queue_size = 1;
		}
		else
		{
			m_desired_queue_size = static_cast<int>(queue_time
				* statistics().download_rate() / block_size);
			if (m_desired_queue_size > m_max_out_request_queue)
				m_desired_queue_size = m_max_out_request_queue;
			if (m_desired_queue_size < min_request_queue)
				m_desired_queue_size = min_request_queue;

			if (m_desired_queue_size == m_max_out_request_queue 
				&& t->alerts().should_post<performance_alert>())
			{
				t->alerts().post_alert(performance_alert(t->get_handle()
					, performance_alert::outstanding_request_limit_reached));
			}
		}

		if (!m_download_queue.empty()
			&& now - m_last_piece > seconds(m_ses.settings().piece_timeout
				+ m_timeout_extend))
		{
			// this peer isn't sending the pieces we've
			// requested (this has been observed by BitComet)
			// in this case we'll clear our download queue and
			// re-request the blocks.
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string()
				<< " *** PIECE_REQUESTS TIMED OUT [ " << (int)m_download_queue.size()
				<< " " << total_seconds(now - m_last_piece) << "] ***\n";
#endif

			snub_peer();
		}

		// If the client sends more data
		// we send it data faster, otherwise, slower.
		// It will also depend on how much data the
		// client has sent us. This is the mean to
		// maintain the share ratio given by m_ratio
		// with all peers.

		if (t->is_finished() || is_choked() || t->ratio() == 0.0f)
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

			double upload_speed_limit = (std::min)((soon_downloaded - have_uploaded
				+ bias) / break_even_time, double(m_upload_limit));

			upload_speed_limit = (std::min)(upload_speed_limit,
				(double)(std::numeric_limits<int>::max)());

			m_bandwidth_limit[upload_channel].throttle(
				(std::min)((std::max)((int)upload_speed_limit, 20)
				, m_upload_limit));
		}

		// update once every minute
		if (now - m_remote_dl_update >= seconds(60))
		{
			float factor = 0.6666666666667f;
			
			if (m_remote_dl_rate == 0) factor = 0.0f;

			m_remote_dl_rate = int((m_remote_dl_rate * factor) + 
				((m_remote_bytes_dled * (1.0f-factor)) / 60.f));
			
			m_remote_bytes_dled = 0;
			m_remote_dl_update = now;
		}

		fill_send_buffer();
	}

	void peer_connection::snub_peer()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		if (!m_snubbed)
		{
			m_snubbed = true;
			if (m_ses.m_alerts.should_post<peer_snubbed_alert>())
			{
				m_ses.m_alerts.post_alert(peer_snubbed_alert(t->get_handle()
					, m_remote, m_peer_id));
			}
		}
		m_desired_queue_size = 1;

		if (on_parole())
		{
			m_timeout_extend += m_ses.settings().request_timeout;
			return;
		}
		if (!t->has_picker()) return;
		piece_picker& picker = t->picker();

		int prev_request_queue = m_request_queue.size();

		// request a new block before removing the previous
		// one, in order to prevent it from
		// picking the same block again, stalling the
		// same piece indefinitely.
		m_desired_queue_size = 2;
		request_a_block(*t, *this);
		m_desired_queue_size = 1;

		piece_block r(-1, -1);
		// time out the last request in the queue
		if (prev_request_queue > 0)
		{
			std::deque<piece_block>::iterator i
				= m_request_queue.begin() + (prev_request_queue - 1);
			r = *i;
			m_request_queue.erase(i);
		}
		else
		{
			TORRENT_ASSERT(!m_download_queue.empty());
			r = m_download_queue.back().block;

			// only time out a request if it blocks the piece
			// from being completed (i.e. no free blocks to
			// request from it)
			piece_picker::downloading_piece p;
			picker.piece_info(r.piece_index, p);
			int free_blocks = picker.blocks_in_piece(r.piece_index)
				- p.finished - p.writing - p.requested;
			if (free_blocks > 0)
			{
				m_timeout_extend += m_ses.settings().request_timeout;
				return;
			}

			if (m_ses.m_alerts.should_post<block_timeout_alert>())
			{
				m_ses.m_alerts.post_alert(block_timeout_alert(t->get_handle()
					, remote(), pid(), r.block_index, r.piece_index));
			}
			m_download_queue.pop_back();
		}
		if (!m_download_queue.empty() || !m_request_queue.empty())
			m_timeout_extend += m_ses.settings().request_timeout;

		if (r != piece_block(-1, -1))
			picker.abort_download(r);

		send_block_requests();
	}

	void peer_connection::fill_send_buffer()
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) return;

		// only add new piece-chunks if the send buffer is small enough
		// otherwise there will be no end to how large it will be!
		
		int buffer_size_watermark = int(m_statistics.upload_rate()) / 2;
		if (buffer_size_watermark < 512) buffer_size_watermark = 512;
		else if (buffer_size_watermark > m_ses.settings().send_buffer_watermark)
			buffer_size_watermark = m_ses.settings().send_buffer_watermark;

		while (!m_requests.empty()
			&& (send_buffer_size() + m_reading_bytes < buffer_size_watermark))
		{
			TORRENT_ASSERT(t->ready_for_connections());
			peer_request& r = m_requests.front();
			
			TORRENT_ASSERT(r.piece >= 0);
			TORRENT_ASSERT(r.piece < (int)m_have_piece.size());
			TORRENT_ASSERT(t->have_piece(r.piece));
			TORRENT_ASSERT(r.start + r.length <= t->torrent_file().piece_size(r.piece));
			TORRENT_ASSERT(r.length > 0 && r.start >= 0);

			t->filesystem().async_read(r, bind(&peer_connection::on_disk_read_complete
				, self(), _1, _2, r));
			m_reading_bytes += r.length;

			m_requests.erase(m_requests.begin());
		}
	}

	void peer_connection::on_disk_read_complete(int ret, disk_io_job const& j, peer_request r)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		m_reading_bytes -= r.length;

		disk_buffer_holder buffer(m_ses, j.buffer);

		if (ret != r.length || m_torrent.expired())
		{
			boost::shared_ptr<torrent> t = m_torrent.lock();
			if (!t)
			{
				disconnect(j.str.c_str());
				return;
			}
		
			if (t->alerts().should_post<file_error_alert>())
				t->alerts().post_alert(file_error_alert(j.error_file, t->get_handle(), j.str));
			t->set_error(j.str);
			t->pause();
			return;
		}

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
			<< " ==> PIECE   [ piece: " << r.piece << " | s: " << r.start
			<< " | l: " << r.length << " ]\n";
#endif

		write_piece(r, buffer);
		setup_send();
	}

	void peer_connection::assign_bandwidth(int channel, int amount)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << "bandwidth [ " << channel << " ] + " << amount << "\n";
#endif

		m_bandwidth_limit[channel].assign(amount);
		TORRENT_ASSERT(m_channel_state[channel] == peer_info::bw_global);
		m_channel_state[channel] = peer_info::bw_idle;
		if (channel == upload_channel)
		{
			setup_send();
		}
		else if (channel == download_channel)
		{
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

		if (m_channel_state[upload_channel] != peer_info::bw_idle) return;
		
		shared_ptr<torrent> t = m_torrent.lock();

		if (m_bandwidth_limit[upload_channel].quota_left() == 0
			&& !m_send_buffer.empty()
			&& !m_connecting
			&& t
			&& !m_ignore_bandwidth_limits)
		{
			// in this case, we have data to send, but no
			// bandwidth. So, we simply request bandwidth
			// from the torrent
			TORRENT_ASSERT(t);
			if (m_bandwidth_limit[upload_channel].max_assignable() > 0)
			{
				int priority = is_interesting() * 2 + m_requests_in_buffer.size();
				// peers that we are not interested in are non-prioritized
				m_channel_state[upload_channel] = peer_info::bw_torrent;
				t->request_bandwidth(upload_channel, self()
					, m_send_buffer.size(), priority);
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << time_now_string() << " *** REQUEST_BANDWIDTH [ upload prio: "
					<< priority << "]\n";
#endif

			}
			return;
		}

		if (!can_write())
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string() << " *** CANNOT WRITE ["
				" quota: " << m_bandwidth_limit[upload_channel].quota_left() <<
				" ignore: " << (m_ignore_bandwidth_limits?"yes":"no") <<
				" buf: " << m_send_buffer.size() <<
				" connecting: " << (m_connecting?"yes":"no") <<
				" ]\n";
#endif
			return;
		}

		// send the actual buffer
		if (!m_send_buffer.empty())
		{
			int amount_to_send = m_send_buffer.size();
			int quota_left = m_bandwidth_limit[upload_channel].quota_left();
			if (!m_ignore_bandwidth_limits && amount_to_send > quota_left)
				amount_to_send = quota_left;

			TORRENT_ASSERT(amount_to_send > 0);

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string() << " *** ASYNC_WRITE [ bytes: " << amount_to_send << " ]\n";
#endif
			std::list<asio::const_buffer> const& vec = m_send_buffer.build_iovec(amount_to_send);
			m_socket->async_write_some(vec, bind(&peer_connection::on_send_data, self(), _1, _2));

			m_channel_state[upload_channel] = peer_info::bw_network;
		}
	}

	void peer_connection::setup_receive()
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		if (m_channel_state[download_channel] != peer_info::bw_idle) return;

		shared_ptr<torrent> t = m_torrent.lock();
		
		if (m_bandwidth_limit[download_channel].quota_left() == 0
			&& !m_connecting
			&& t
			&& !m_ignore_bandwidth_limits)
		{
			if (m_bandwidth_limit[download_channel].max_assignable() > 0)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << time_now_string() << " *** REQUEST_BANDWIDTH [ download ]\n";
#endif
				TORRENT_ASSERT(m_channel_state[download_channel] == peer_info::bw_idle);
				m_channel_state[download_channel] = peer_info::bw_torrent;
				t->request_bandwidth(download_channel, self()
					, m_download_queue.size() * 16 * 1024 + 30, m_priority);
			}
			return;
		}
		
		if (!can_read())
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string() << " *** CANNOT READ ["
				" quota: " << m_bandwidth_limit[download_channel].quota_left() <<
				" ignore: " << (m_ignore_bandwidth_limits?"yes":"no") <<
				" outstanding: " << m_outstanding_writing_bytes <<
				" outstanding-limit: " << m_ses.settings().max_outstanding_disk_bytes_per_connection <<
				" ]\n";
#endif
			return;
		}

		TORRENT_ASSERT(m_packet_size > 0);
		int max_receive = m_packet_size - m_recv_pos;
		int quota_left = m_bandwidth_limit[download_channel].quota_left();
		if (!m_ignore_bandwidth_limits && max_receive > quota_left)
			max_receive = quota_left;

		if (max_receive == 0) return;

		TORRENT_ASSERT(m_recv_pos >= 0);
		TORRENT_ASSERT(m_packet_size > 0);
		TORRENT_ASSERT(can_read());
#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " *** ASYNC_READ [ max: " << max_receive << " bytes ]\n";
#endif

		int regular_buffer_size = m_packet_size - m_disk_recv_buffer_size;

		if (int(m_recv_buffer.size()) < regular_buffer_size)
			m_recv_buffer.resize(regular_buffer_size);

		if (!m_disk_recv_buffer || regular_buffer_size >= m_recv_pos + max_receive)
		{
			// only receive into regular buffer
			TORRENT_ASSERT(m_recv_pos + max_receive <= int(m_recv_buffer.size()));
			m_socket->async_read_some(asio::buffer(&m_recv_buffer[m_recv_pos]
				, max_receive), bind(&peer_connection::on_receive_data, self(), _1, _2));
		}
		else if (m_recv_pos >= regular_buffer_size)
		{
			// only receive into disk buffer
			TORRENT_ASSERT(m_recv_pos - regular_buffer_size >= 0);
			TORRENT_ASSERT(m_recv_pos - regular_buffer_size + max_receive <= m_disk_recv_buffer_size);
			m_socket->async_read_some(asio::buffer(m_disk_recv_buffer.get() + m_recv_pos - regular_buffer_size
				, max_receive)
				, bind(&peer_connection::on_receive_data, self(), _1, _2));
		}
		else
		{
			// receive into both regular and disk buffer
			TORRENT_ASSERT(max_receive + m_recv_pos > regular_buffer_size);
			TORRENT_ASSERT(m_recv_pos < regular_buffer_size);
			TORRENT_ASSERT(max_receive - regular_buffer_size
				+ m_recv_pos <= m_disk_recv_buffer_size);

			boost::array<asio::mutable_buffer, 2> vec;
			vec[0] = asio::buffer(&m_recv_buffer[m_recv_pos]
				, regular_buffer_size - m_recv_pos);
			vec[1] = asio::buffer(m_disk_recv_buffer.get()
				, max_receive - regular_buffer_size + m_recv_pos);
			m_socket->async_read_some(vec, bind(&peer_connection::on_receive_data
				, self(), _1, _2));
		}
		m_channel_state[download_channel] = peer_info::bw_network;
	}

#ifndef TORRENT_DISABLE_ENCRYPTION

	// returns the last 'bytes' from the receive buffer
	std::pair<buffer::interval, buffer::interval> peer_connection::wr_recv_buffers(int bytes)
	{
		TORRENT_ASSERT(bytes <= m_recv_pos);

		std::pair<buffer::interval, buffer::interval> vec;
		int regular_buffer_size = m_packet_size - m_disk_recv_buffer_size;
		TORRENT_ASSERT(regular_buffer_size >= 0);
		if (!m_disk_recv_buffer || regular_buffer_size >= m_recv_pos)
		{
			vec.first = buffer::interval(&m_recv_buffer[0]
				+ m_recv_pos - bytes, &m_recv_buffer[0] + m_recv_pos);
			vec.second = buffer::interval(0,0);
		}
		else if (m_recv_pos - bytes >= regular_buffer_size)
		{
			vec.first = buffer::interval(m_disk_recv_buffer.get() + m_recv_pos
				- regular_buffer_size - bytes, m_disk_recv_buffer.get() + m_recv_pos
				- regular_buffer_size);
			vec.second = buffer::interval(0,0);
		}
		else
		{
			TORRENT_ASSERT(m_recv_pos - bytes < regular_buffer_size);
			TORRENT_ASSERT(m_recv_pos > regular_buffer_size);
			vec.first = buffer::interval(&m_recv_buffer[0] + m_recv_pos - bytes
				, &m_recv_buffer[0] + regular_buffer_size);
			vec.second = buffer::interval(m_disk_recv_buffer.get()
				, m_disk_recv_buffer.get() + m_recv_pos - regular_buffer_size);
		}
		TORRENT_ASSERT(vec.first.left() + vec.second.left() == bytes);
		return vec;
	}
#endif

	void peer_connection::reset_recv_buffer(int packet_size)
	{
		TORRENT_ASSERT(packet_size > 0);
		if (m_recv_pos > m_packet_size)
		{
			cut_receive_buffer(m_packet_size, packet_size);
			return;
		}
		m_recv_pos = 0;
		m_packet_size = packet_size;
	}

	void peer_connection::send_buffer(char const* buf, int size, int flags)
	{
		if (flags == message_type_request)
			m_requests_in_buffer.push_back(m_send_buffer.size() + size);

		int free_space = m_send_buffer.space_in_last_buffer();
		if (free_space > size) free_space = size;
		if (free_space > 0)
		{
			m_send_buffer.append(buf, free_space);
			size -= free_space;
			buf += free_space;
#ifdef TORRENT_STATS
			m_ses.m_buffer_usage_logger << log_time() << " send_buffer: "
				<< free_space << std::endl;
			m_ses.log_buffer_usage();
#endif
		}
		if (size <= 0) return;

		std::pair<char*, int> buffer = m_ses.allocate_buffer(size);
		if (buffer.first == 0)
		{
			disconnect("out of memory");
			return;
		}
		TORRENT_ASSERT(buffer.second >= size);
		std::memcpy(buffer.first, buf, size);
		m_send_buffer.append_buffer(buffer.first, buffer.second, size
			, bind(&session_impl::free_buffer, boost::ref(m_ses), _1, buffer.second));
#ifdef TORRENT_STATS
		m_ses.m_buffer_usage_logger << log_time() << " send_buffer_alloc: " << size << std::endl;
		m_ses.log_buffer_usage();
#endif
		setup_send();
	}

// TODO: change this interface to automatically call setup_send() when the
// return value is destructed
	buffer::interval peer_connection::allocate_send_buffer(int size)
	{
		TORRENT_ASSERT(size > 0);
		char* insert = m_send_buffer.allocate_appendix(size);
		if (insert == 0)
		{
			std::pair<char*, int> buffer = m_ses.allocate_buffer(size);
			if (buffer.first == 0)
			{
				disconnect("out of memory");
				return buffer::interval(0, 0);
			}
			TORRENT_ASSERT(buffer.second >= size);
			m_send_buffer.append_buffer(buffer.first, buffer.second, size
				, bind(&session_impl::free_buffer, boost::ref(m_ses), _1, buffer.second));
			buffer::interval ret(buffer.first, buffer.first + size);
#ifdef TORRENT_STATS
			m_ses.m_buffer_usage_logger << log_time() << " allocate_buffer_alloc: " << size << std::endl;
			m_ses.log_buffer_usage();
#endif
			return ret;
		}
		else
		{
#ifdef TORRENT_STATS
			m_ses.m_buffer_usage_logger << log_time() << " allocate_buffer: " << size << std::endl;
			m_ses.log_buffer_usage();
#endif
			buffer::interval ret(insert, insert + size);
			return ret;
		}
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
	void peer_connection::on_receive_data(const error_code& error
		, std::size_t bytes_transferred)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		// keep ourselves alive in until this function exits in
		// case we disconnect
		boost::intrusive_ptr<peer_connection> me(self());

		TORRENT_ASSERT(m_channel_state[download_channel] == peer_info::bw_network);
		m_channel_state[download_channel] = peer_info::bw_idle;

		if (error)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << time_now_string() << " **ERROR**: "
				<< error.message() << "[in peer_connection::on_receive_data]\n";
#endif
			on_receive(error, bytes_transferred);
			disconnect(error.message().c_str());
			return;
		}

		int max_receive = 0;
		do
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << "read " << bytes_transferred << " bytes\n";
#endif
			// correct the dl quota usage, if not all of the buffer was actually read
			if (!m_ignore_bandwidth_limits)
				m_bandwidth_limit[download_channel].use_quota(bytes_transferred);

			if (m_disconnecting) return;
	
			TORRENT_ASSERT(m_packet_size > 0);
			TORRENT_ASSERT(bytes_transferred > 0);

			m_last_receive = time_now();
			m_recv_pos += bytes_transferred;
			TORRENT_ASSERT(m_recv_pos <= int(m_recv_buffer.size()
				+ m_disk_recv_buffer_size));

			on_receive(error, bytes_transferred);

			TORRENT_ASSERT(m_packet_size > 0);

			if (m_peer_choked
				&& m_recv_pos == 0
				&& (m_recv_buffer.capacity() - m_packet_size) > 128)
			{
				buffer(m_packet_size).swap(m_recv_buffer);
			}

			max_receive = m_packet_size - m_recv_pos;
			int quota_left = m_bandwidth_limit[download_channel].quota_left();
			if (!m_ignore_bandwidth_limits && max_receive > quota_left)
				max_receive = quota_left;

			if (max_receive == 0) break;

			int regular_buffer_size = m_packet_size - m_disk_recv_buffer_size;

			if (int(m_recv_buffer.size()) < regular_buffer_size)
				m_recv_buffer.resize(regular_buffer_size);

			error_code ec;	
			if (!m_disk_recv_buffer || regular_buffer_size >= m_recv_pos + max_receive)
			{
				// only receive into regular buffer
				TORRENT_ASSERT(m_recv_pos + max_receive <= int(m_recv_buffer.size()));
				bytes_transferred = m_socket->read_some(asio::buffer(&m_recv_buffer[m_recv_pos]
					, max_receive), ec);
			}
			else if (m_recv_pos >= regular_buffer_size)
			{
				// only receive into disk buffer
				TORRENT_ASSERT(m_recv_pos - regular_buffer_size >= 0);
				TORRENT_ASSERT(m_recv_pos - regular_buffer_size + max_receive <= m_disk_recv_buffer_size);
				bytes_transferred = m_socket->read_some(asio::buffer(m_disk_recv_buffer.get()
					+ m_recv_pos - regular_buffer_size, (std::min)(m_packet_size
					- m_recv_pos, max_receive)), ec);
			}
			else
			{
				// receive into both regular and disk buffer
				TORRENT_ASSERT(max_receive + m_recv_pos > regular_buffer_size);
				TORRENT_ASSERT(m_recv_pos < regular_buffer_size);
				TORRENT_ASSERT(max_receive - regular_buffer_size
					+ m_recv_pos <= m_disk_recv_buffer_size);

				boost::array<asio::mutable_buffer, 2> vec;
				vec[0] = asio::buffer(&m_recv_buffer[m_recv_pos]
					, regular_buffer_size - m_recv_pos);
				vec[1] = asio::buffer(m_disk_recv_buffer.get()
					, (std::min)(m_disk_recv_buffer_size
					, max_receive - regular_buffer_size + m_recv_pos));
				bytes_transferred = m_socket->read_some(vec, ec);
			}
			if (ec && ec != asio::error::would_block)
			{
				disconnect(ec.message().c_str());
				return;
			}
			if (ec == asio::error::would_block) break;
		}
		while (bytes_transferred > 0);

		setup_receive();	
	}

	bool peer_connection::can_write() const
	{
		// if we have requests or pending data to be sent or announcements to be made
		// we want to send data
		return !m_send_buffer.empty()
			&& (m_bandwidth_limit[upload_channel].quota_left() > 0
				|| m_ignore_bandwidth_limits)
			&& !m_connecting;
	}

	bool peer_connection::can_read() const
	{
		bool ret = (m_bandwidth_limit[download_channel].quota_left() > 0
				|| m_ignore_bandwidth_limits)
			&& !m_connecting
			&& m_outstanding_writing_bytes <
				m_ses.settings().max_outstanding_disk_bytes_per_connection;
		
		return ret;
	}

	void peer_connection::connect(int ticket)
	{
#ifdef TORRENT_DEBUG
		// in case we disconnect here, we need to
		// keep the connection alive until the
		// exit invariant check is run
		boost::intrusive_ptr<peer_connection> me(self());
#endif
		INVARIANT_CHECK;

		error_code ec;
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		(*m_ses.m_logger) << time_now_string() << " CONNECTING: " << m_remote.address().to_string(ec)
			<< ":" << m_remote.port() << "\n";
#endif

		m_connection_ticket = ticket;
		boost::shared_ptr<torrent> t = m_torrent.lock();

		m_queued = false;
		TORRENT_ASSERT(m_connecting);

		if (!t)
		{
			disconnect("torrent aborted");
			return;
		}

		m_socket->open(m_remote.protocol(), ec);
		if (ec)
		{
			disconnect(ec.message().c_str());
			return;
		}

		// set the socket to non-blocking, so that we can
		// read the entire buffer on each read event we get
		tcp::socket::non_blocking_io ioc(true);
		m_socket->io_control(ioc, ec);
		if (ec)
		{
			disconnect(ec.message().c_str());
			return;
		}

		tcp::endpoint bind_interface = t->get_interface();
	
		std::pair<int, int> const& out_ports = m_ses.settings().outgoing_ports;
		if (out_ports.first > 0 && out_ports.second >= out_ports.first)
		{
			m_socket->set_option(socket_acceptor::reuse_address(true), ec);
			if (ec)
			{
				disconnect(ec.message().c_str());
				return;
			}
			bind_interface.port(m_ses.next_port());
		}

		// if we're not binding to a specific interface, bind
		// to the same protocol family as the target endpoint
		if (is_any(bind_interface.address()))
		{
			if (m_remote.address().is_v4())
				bind_interface.address(address_v4::any());
			else
				bind_interface.address(address_v6::any());
		}

		m_socket->bind(bind_interface, ec);
		if (ec)
		{
			disconnect(ec.message().c_str());
			return;
		}
		m_socket->async_connect(m_remote
			, bind(&peer_connection::on_connection_complete, self(), _1));
		m_connect = time_now();

		if (t->alerts().should_post<peer_connect_alert>())
		{
			t->alerts().post_alert(peer_connect_alert(
				t->get_handle(), remote(), pid()));
		}
	}
	
	void peer_connection::on_connection_complete(error_code const& e)
	{
		ptime completed = time_now();

		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		m_rtt = total_milliseconds(completed - m_connect);
		
		if (m_disconnecting) return;

		m_connecting = false;
		m_ses.m_half_open.done(m_connection_ticket);

		error_code ec;
		if (e)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_ses.m_logger) << time_now_string() << " CONNECTION FAILED: " << m_remote.address().to_string(ec)
				<< ": " << e.message() << "\n";
#endif
			disconnect(e.message().c_str(), 1);
			return;
		}

		if (m_disconnecting) return;
		m_last_receive = time_now();

		// this means the connection just succeeded

		TORRENT_ASSERT(m_socket);
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING
		(*m_ses.m_logger) << time_now_string() << " COMPLETED: " << m_remote.address().to_string(ec)
			<< " rtt = " << m_rtt << "\n";
#endif

		if (m_remote == m_socket->local_endpoint(ec))
		{
			// if the remote endpoint is the same as the local endpoint, we're connected
			// to ourselves
			disconnect("connected to ourselves", 1);
			return;
		}

		if (m_remote.address().is_v4())
		{
			error_code ec;
			m_socket->set_option(type_of_service(m_ses.settings().peer_tos), ec);
		}

		on_connected();
		setup_send();
		setup_receive();
	}
	
	// --------------------------
	// SEND DATA
	// --------------------------

	// throws exception when the client should be disconnected
	void peer_connection::on_send_data(error_code const& error
		, std::size_t bytes_transferred)
	{
		session_impl::mutex_t::scoped_lock l(m_ses.m_mutex);

		INVARIANT_CHECK;

		// keep ourselves alive in until this function exits in
		// case we disconnect
		boost::intrusive_ptr<peer_connection> me(self());

		TORRENT_ASSERT(m_channel_state[upload_channel] == peer_info::bw_network);

		m_send_buffer.pop_front(bytes_transferred);

		for (std::vector<int>::iterator i = m_requests_in_buffer.begin()
			, end(m_requests_in_buffer.end()); i != end; ++i)
			*i -= bytes_transferred;

		while (!m_requests_in_buffer.empty()
			&& m_requests_in_buffer.front() <= 0)
			m_requests_in_buffer.erase(m_requests_in_buffer.begin());
		
		m_channel_state[upload_channel] = peer_info::bw_idle;

		if (!m_ignore_bandwidth_limits)
			m_bandwidth_limit[upload_channel].use_quota(bytes_transferred);

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << "wrote " << bytes_transferred << " bytes\n";
#endif

		if (error)
		{
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING
			(*m_logger) << "**ERROR**: " << error.message() << " [in peer_connection::on_send_data]\n";
#endif
			disconnect(error.message().c_str());
			return;
		}
		if (m_disconnecting) return;

		TORRENT_ASSERT(!m_connecting);
		TORRENT_ASSERT(bytes_transferred > 0);

		m_last_sent = time_now();

		on_sent(error, bytes_transferred);
		fill_send_buffer();

		setup_send();
	}

#ifdef TORRENT_DEBUG
	void peer_connection::check_invariant() const
	{
		TORRENT_ASSERT(bool(m_disk_recv_buffer) == (m_disk_recv_buffer_size > 0));

		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (m_disconnecting)
		{
			TORRENT_ASSERT(!t);
			TORRENT_ASSERT(m_disconnect_started);
		}
		else if (!m_in_constructor)
		{
			TORRENT_ASSERT(m_ses.has_peer((peer_connection*)this));
		}

/*
		// this assertion correct most of the time, but sometimes right when the
		// limit is changed it might break
		for (int i = 0; i < 2; ++i)
		{
			// this peer is in the bandwidth history iff max_assignable < limit
			TORRENT_ASSERT((m_bandwidth_limit[i].max_assignable() < m_bandwidth_limit[i].throttle())
				== m_ses.m_bandwidth_manager[i]->is_in_history(this)
				|| m_bandwidth_limit[i].throttle() == bandwidth_limit::inf);
		}
*/

		if (m_channel_state[download_channel] == peer_info::bw_torrent
			|| m_channel_state[download_channel] == peer_info::bw_global)
			TORRENT_ASSERT(m_bandwidth_limit[download_channel].quota_left() == 0);
		if (m_channel_state[upload_channel] == peer_info::bw_torrent
			|| m_channel_state[upload_channel] == peer_info::bw_global)
			TORRENT_ASSERT(m_bandwidth_limit[upload_channel].quota_left() == 0);

		std::set<piece_block> unique;
		std::transform(m_download_queue.begin(), m_download_queue.end()
			, std::inserter(unique, unique.begin()), boost::bind(&pending_block::block, _1));
		std::copy(m_request_queue.begin(), m_request_queue.end(), std::inserter(unique, unique.begin()));
		TORRENT_ASSERT(unique.size() == m_download_queue.size() + m_request_queue.size());
		if (m_peer_info)
		{
			TORRENT_ASSERT(m_peer_info->prev_amount_upload == 0);
			TORRENT_ASSERT(m_peer_info->prev_amount_download == 0);
			TORRENT_ASSERT(m_peer_info->connection == this
				|| m_peer_info->connection == 0);

			if (m_peer_info->optimistically_unchoked)
				TORRENT_ASSERT(!is_choked());
		}

		TORRENT_ASSERT(m_have_piece.count() == m_num_pieces);

		if (!t)
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			// since this connection doesn't have a torrent reference
			// no torrent should have a reference to this connection either
			for (aux::session_impl::torrent_map::const_iterator i = m_ses.m_torrents.begin()
				, end(m_ses.m_torrents.end()); i != end; ++i)
				TORRENT_ASSERT(!i->second->has_peer((peer_connection*)this));
#endif
			return;
		}

		if (t->ready_for_connections() && m_initialized)
			TORRENT_ASSERT(t->torrent_file().num_pieces() == int(m_have_piece.size()));

		if (m_ses.settings().close_redundant_connections)
		{
			// make sure upload only peers are disconnected
			if (t->is_finished() && m_upload_only)
				TORRENT_ASSERT(m_disconnect_started);
			if (m_upload_only
				&& !m_interesting
				&& m_bitfield_received
				&& t->are_files_checked())
				TORRENT_ASSERT(m_disconnect_started);
		}

		if (!m_disconnect_started && m_initialized)
		{
			// none of this matters if we're disconnecting anyway
			if (t->is_finished())
				TORRENT_ASSERT(!m_interesting);
			if (is_seed())
				TORRENT_ASSERT(m_upload_only);
		}

		if (t->has_picker())
		{
			std::map<piece_block, int> num_requests;
			for (torrent::const_peer_iterator i = t->begin(); i != t->end(); ++i)
			{
				// make sure this peer is not a dangling pointer
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
				TORRENT_ASSERT(m_ses.has_peer(*i));
#endif
				peer_connection const& p = *(*i);
				for (std::deque<piece_block>::const_iterator i = p.request_queue().begin()
					, end(p.request_queue().end()); i != end; ++i)
					++num_requests[*i];
				for (std::deque<pending_block>::const_iterator i = p.download_queue().begin()
					, end(p.download_queue().end()); i != end; ++i)
					++num_requests[i->block];
			}
			for (std::map<piece_block, int>::iterator i = num_requests.begin()
				, end(num_requests.end()); i != end; ++i)
			{
				if (!t->picker().is_downloaded(i->first))
					TORRENT_ASSERT(t->picker().num_peers(i->first) == i->second);
			}
		}
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		if (m_peer_info)
		{
			policy::const_iterator i = t->get_policy().begin_peer();
			policy::const_iterator end = t->get_policy().end_peer();
			for (; i != end; ++i)
			{
				if (&i->second == m_peer_info) break;
			}
			TORRENT_ASSERT(i != end);
		}
#endif
		if (t->has_picker() && !t->is_aborted())
		{
			// make sure that pieces that have completed the download
			// of all their blocks are in the disk io thread's queue
			// to be checked.
			const std::vector<piece_picker::downloading_piece>& dl_queue
				= t->picker().get_download_queue();
			for (std::vector<piece_picker::downloading_piece>::const_iterator i =
				dl_queue.begin(); i != dl_queue.end(); ++i)
			{
				const int blocks_per_piece = t->picker().blocks_in_piece(i->index);

				bool complete = true;
				for (int j = 0; j < blocks_per_piece; ++j)
				{
					if (i->info[j].state == piece_picker::block_info::state_finished)
						continue;
					complete = false;
					break;
				}
/*
// this invariant is not valid anymore since the completion event
// might be queued in the io service
				if (complete && !piece_failed)
				{
					disk_io_job ret = m_ses.m_disk_thread.find_job(
						&t->filesystem(), -1, i->index);
					TORRENT_ASSERT(ret.action == disk_io_job::hash || ret.action == disk_io_job::write);
					TORRENT_ASSERT(ret.piece == i->index);
				}
*/
			}
		}

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
						TORRENT_ASSERT(i->info[j].peer == m_remote);
					}
					else
					{
						TORRENT_ASSERT(i->info[j].peer != m_remote || i->info[j].finished);
					}
				}
			}
		}
*/
	}
#endif

	peer_connection::peer_speed_t peer_connection::peer_speed()
	{
		shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT(t);

		int download_rate = int(statistics().download_payload_rate());
		int torrent_download_rate = int(t->statistics().download_payload_rate());

		if (download_rate > 512 && download_rate > torrent_download_rate / 16)
			m_speed = fast;
		else if (download_rate > 4096 && download_rate > torrent_download_rate / 64)
			m_speed = medium;
		else if (download_rate < torrent_download_rate / 15 && m_speed == fast)
			m_speed = medium;
		else
			m_speed = slow;

		return m_speed;
	}

	void peer_connection::keep_alive()
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

		time_duration d;
		d = time_now() - m_last_sent;
		if (total_seconds(d) < m_timeout / 2) return;
		
		if (m_connecting) return;
		if (in_handshake()) return;

		// if the last send has not completed yet, do not send a keep
		// alive
		if (m_channel_state[upload_channel] != peer_info::bw_idle) return;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " ==> KEEPALIVE\n";
#endif

		m_last_sent = time_now();
		write_keepalive();
	}

	bool peer_connection::is_seed() const
	{
		// if m_num_pieces == 0, we probably don't have the
		// metadata yet.
		boost::shared_ptr<torrent> t = m_torrent.lock();
		return m_num_pieces == (int)m_have_piece.size() && m_num_pieces > 0 && t && t->valid_metadata();
	}
}

