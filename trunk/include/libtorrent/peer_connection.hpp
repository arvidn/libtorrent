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

#ifndef TORRENT_PEER_CONNECTION_HPP_INCLUDED
#define TORRENT_PEER_CONNECTION_HPP_INCLUDED

#include <ctime>
#include <algorithm>
#include <vector>

#include <boost/smart_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/array.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

#include "libtorrent/socket.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/debug.hpp"

/*
 * This file declares the following functions:
 *
 *----------------------------------
 *
 *
 *
 */

namespace libtorrent
{
	class torrent;

	namespace detail
	{
		struct session_impl;
	}

	struct peer_request
	{
		int piece;
		int start;
		int length;
		bool operator==(const peer_request& r)
		{ return piece == r.piece && start == r.start && length	== r.length; }
	};

	class peer_connection: public boost::noncopyable
	{
	public:

		// this is the constructor where the we are teh active part. The peer_conenction
		// should handshake and verify that the other end has the correct id
		peer_connection(
			detail::session_impl* ses
			, selector& sel
			, torrent* t
			, boost::shared_ptr<libtorrent::socket> s
			, const peer_id& p);

		// with this constructor we have been contacted and we still don't know which torrent the
		// connection belongs to
		peer_connection(
			detail::session_impl* ses
			, selector& sel
			, boost::shared_ptr<libtorrent::socket> s);

		~peer_connection();

		// this adds an announcement in the announcement queue
		// it will let the peer know that we have the given piece
		void announce_piece(int index)
		{
			m_announce_queue.push_back(index);
			send_buffer_updated();
		}

		// called from the main loop when this connection has any
		// work to do.
		void send_data();
		void receive_data();

		// tells if this connection has data it want to send
		bool has_data() const throw();

		bool has_timed_out()
		{
			boost::posix_time::time_duration d;
			d = boost::posix_time::second_clock::local_time() - m_last_receive;
			return d.seconds() > m_timeout;
		}

		// will send a keep-alive message to the peer
		void keep_alive();

		const peer_id& id() const throw() { return m_peer_id; }
		bool has_piece(int i) const throw() { return m_have_piece[i]; }

		const std::vector<piece_block>& download_queue() const throw()
		{ return m_download_queue; }

		void choke();
		void unchoke();
		void interested();
		void not_interested();
		void request_block(piece_block block);
		void cancel_block(piece_block block);

		bool is_interesting() const throw() { return m_interesting; }
		bool has_choked() const throw() { return m_choked; }

		bool is_peer_interested() const throw() { return m_peer_interested; }
		bool has_peer_choked() const throw() { return m_peer_choked; }

		// returns the torrent this connection is a part of
		// may be zero if the connection is an incoming connection
		// and it hasn't received enough information to determine
		// which torrent it should be associated with
		torrent* associated_torrent() const throw() { return m_torrent; }

		const stat& statistics() const { return m_statistics; }

		// is called once every second by the main loop
		void second_tick() { m_statistics.second_tick(); }

		boost::shared_ptr<libtorrent::socket> get_socket() const { return m_socket; }

		const peer_id& get_peer_id() const { return m_peer_id; }
		const std::vector<bool>& get_bitfield() const { return m_have_piece; }

#if defined(TORRENT_VERBOSE_LOGGING)
		boost::shared_ptr<logger> m_logger;
#endif

	private:

		bool dispatch_message();
		void send_buffer_updated();

		void send_bitfield();
		void send_have(int index);
		void send_handshake();


		// is used during handshake
		enum state
		{
			read_protocol_length = 0,
			read_protocol_version,
			read_info_hash,
			read_peer_id,

			read_packet_size,
			read_packet
		};

		state m_state;

		// the timeout in seconds
		int m_timeout;

		enum message_type
		{
			msg_choke = 0,
			msg_unchoke,
			msg_interested,
			msg_not_interested,
			msg_have,
			msg_bitfield,
			msg_request,
			msg_piece,
			msg_cancel
		};

		std::size_t m_packet_size;
		std::size_t m_recv_pos;
		std::vector<char> m_recv_buffer;

		// this is the buffer where data that is
		// to be sent is stored until
		std::vector<char> m_send_buffer;

		// timeouts
		boost::posix_time::ptime m_last_receive;
		boost::posix_time::ptime m_last_sent;

		selector& m_selector;
		boost::shared_ptr<libtorrent::socket> m_socket;
		torrent* m_torrent;
		detail::session_impl* m_ses;
		// is true if it was we that connected to the peer
		// and false if we got an incomming connection
		bool m_active;

		// this is true as long as this peer's
		// socket is added to the selector to
		// monitor writability. Each time we do
		// something that generates data to be 
		// sent to this peer, we check this and
		// if it's not added to the selector we
		// add it.
		bool m_added_to_selector;

		// remote peer's id
		peer_id m_peer_id;

		// the pieces that we are sending and receiving
		piece_file m_sending_piece;
		piece_file m_receiving_piece;

		// other side says that it's interested in downloading
		// from us.
		bool m_peer_interested;

		// the other side has told us that it won't send anymore
		// data to us for a while
		bool m_peer_choked;

		// the peer has pieces we are interested in
		bool m_interesting;

		// we have choked the upload to the peer
		bool m_choked;

		// the pieces the other end have
		std::vector<bool> m_have_piece;

		std::vector<peer_request> m_requests;

		// a list of pieces that have become available
		// and should be announced as available to
		// the peer
		std::vector<int> m_announce_queue;
		std::vector<piece_block> m_download_queue;

		stat m_statistics;
	};

	// this is called each time this peer generates some
	// data to be sent. It will add this socket to
	// the writibility monitor in the selector.
	inline void peer_connection::send_buffer_updated()
	{
		if (!has_data()) return;

		if (!m_added_to_selector)
		{
			m_selector.monitor_writability(m_socket);
			m_added_to_selector = true;
		}
		assert(m_added_to_selector);
		assert(m_selector.is_writability_monitored(m_socket));
	}
}

#endif // TORRENT_PEER_CONNECTION_HPP_INCLUDED

