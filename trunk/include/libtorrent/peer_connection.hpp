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
#include <deque>
#include <string>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/smart_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/array.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/optional.hpp>
#include <boost/cstdint.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/socket.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent.hpp"

// TODO: each time a block is 'taken over'
// from another peer. That peer must be given
// a chance to become not-interested.

namespace libtorrent
{
	class torrent;

	namespace detail
	{
		struct session_impl;
	}

	struct protocol_error: std::runtime_error
	{
		protocol_error(const std::string& msg): std::runtime_error(msg) {};
	};

	struct peer_request
	{
		int piece;
		int start;
		int length;
		bool operator==(const peer_request& r)
		{ return piece == r.piece && start == r.start && length	== r.length; }
	};

	struct piece_block_progress
	{
		// the piece and block index
		// determines exactly which
		// part of the torrent that
		// is currently being downloaded
		int piece_index;
		int block_index;
		// the number of bytes we have received
		// of this block
		int bytes_downloaded;
		// the number of bytes in the block
		int full_block_bytes;
	};

	class peer_connection: public boost::noncopyable
	{
	friend class invariant_access;
	public:

		// this is the constructor where the we are teh active part. The peer_conenction
		// should handshake and verify that the other end has the correct id
		peer_connection(
			detail::session_impl& ses
			, selector& sel
			, torrent* t
			, boost::shared_ptr<libtorrent::socket> s);

		// with this constructor we have been contacted and we still don't know which torrent the
		// connection belongs to
		peer_connection(
			detail::session_impl& ses
			, selector& sel
			, boost::shared_ptr<libtorrent::socket> s);

		~peer_connection();

		// this adds an announcement in the announcement queue
		// it will let the peer know that we have the given piece
		void announce_piece(int index)
		{
			assert(index >= 0 && index < m_torrent->torrent_file().num_pieces());
			m_announce_queue.push_back(index);
		}

		// called from the main loop when this connection has any
		// work to do.
		void send_data();
		void receive_data();

		// tells if this connection has data it want to send
		bool has_data() const;

		bool is_seed() const;

		bool has_timed_out() const;

		// will send a keep-alive message to the peer
		void keep_alive();

		const peer_id& id() const { return m_peer_id; }
		bool has_piece(int i) const
		{
			assert(i >= 0);
			assert(i < m_torrent->torrent_file().num_pieces());
			return m_have_piece[i];
		}

		const std::deque<piece_block>& download_queue() const
		{ return m_download_queue; }
		const std::deque<peer_request>& upload_queue() const
		{ return m_requests; }

		// returns the block currently being
		// downloaded. And the progress of that
		// block. If the peer isn't downloading
		// a piece for the moment, the boost::optional
		// will be invalid.
		boost::optional<piece_block_progress> downloading_piece() const;

		bool is_interesting() const { return m_interesting; }
		bool is_choked() const { return m_choked; }

		bool is_peer_interested() const { return m_peer_interested; }
		bool has_peer_choked() const { return m_peer_choked; }

		// returns the torrent this connection is a part of
		// may be zero if the connection is an incoming connection
		// and it hasn't received enough information to determine
		// which torrent it should be associated with
		torrent* associated_torrent() const { return m_attached_to_torrent?m_torrent:0; }

		bool verify_piece(const peer_request& p) const;

		const stat& statistics() const { return m_statistics; }

		// is called once every second by the main loop
		void second_tick();

		boost::shared_ptr<libtorrent::socket> get_socket() const { return m_socket; }

		const peer_id& get_peer_id() const { return m_peer_id; }
		const std::vector<bool>& get_bitfield() const
		{ return m_have_piece; }

		// this will cause this peer_connection to be disconnected.
		// what it does is that it puts a reference to it in
		// m_ses.m_disconnect_peer list, which will be scanned in the
		// mainloop to disconnect peers.
		void disconnect();

		bool is_disconnecting() const
		{ return m_disconnecting; }

		// sets the number of bytes this peer
		// is allowed to send until it should
		// stop sending. When it stops sending
		// it will simply wait for another call
		// to second_tick() where it will get
		// more send quota.
		void set_send_quota(int num_bytes);

		// returns the send quota this peer has
		// left until will stop sending.
		// if the send_quota is -1, it means the
		// quota is unlimited.
		int send_quota_left() const { return m_send_quota_left; }

		size_type total_free_upload() const
		{ return m_free_upload; }

		void add_free_upload(size_type free_upload)
		{ m_free_upload += free_upload; }

		// returns the send quota assigned to this
		// peer.
		int send_quota() const { return m_send_quota; }

		void received_valid_data()
		{
			m_trust_points++;
			if (m_trust_points > 20) m_trust_points = 20;
		}

		void received_invalid_data()
		{
			m_trust_points--;
			if (m_trust_points < -5) m_trust_points = -5;
		}

		int trust_points() const
		{ return m_trust_points; }

		int send_quota_limit() const
		{ return m_send_quota_limit; }

		size_type share_diff() const;

		bool support_extensions() const
		{ return m_supports_extensions; }

		const boost::posix_time::time_duration& last_piece_time() const
		{ return m_last_piece_time; }

		// a connection is local if it was initiated by us.
		// if it was an incoming connection, it is remote
		bool is_local() const
		{ return m_active; }

#ifndef NDEBUG
		boost::shared_ptr<logger> m_logger;
#endif

		// the message handlers are called
		// each time a recv() returns some new
		// data, the last time it will be called
		// is when the entire packet has been
		// received, then it will no longer
		// be called. i.e. most handlers need
		// to check how much of the packet they
		// have received before any processing
		void on_choke(int received);
		void on_unchoke(int received);
		void on_interested(int received);
		void on_not_interested(int received);
		void on_have(int received);
		void on_bitfield(int received);
		void on_request(int received);
		void on_piece(int received);
		void on_cancel(int received);
		void on_extension_list(int received);
		void on_extended(int received);

		typedef void (peer_connection::*message_handler)(int received);

		// the following functions appends messages
		// to the send buffer
		void send_choke();
		void send_unchoke();
		void send_interested();
		void send_not_interested();
		void send_request(piece_block block);
		void send_cancel(piece_block block);
		void send_bitfield();
		void send_have(int index);
		void send_handshake();
		void send_extensions();
		void send_chat_message(const std::string& msg);


	private:

		void check_invariant() const;

		bool dispatch_message(int received);
		void send_buffer_updated();

		// is used during handshake
		enum state
		{
			read_protocol_length = 0,
			read_protocol_string,
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
	// standard messages
			msg_choke = 0,
			msg_unchoke,
			msg_interested,
			msg_not_interested,
			msg_have,
			msg_bitfield,
			msg_request,
			msg_piece,
			msg_cancel,
	// extension protocol message
			msg_extension_list = 20,
			msg_extended,

			num_supported_messages
		};

		const static message_handler m_message_handler[num_supported_messages];

		int m_packet_size;
		int m_recv_pos;
		std::vector<char> m_recv_buffer;

		// this is the buffer where data that is
		// to be sent is stored until it gets
		// consumed by send()
		std::vector<char> m_send_buffer;

		// this is a queue of ranges that describes
		// where in the send buffer actual payload
		// data is located. This is currently
		// only used to be able to gather statistics
		// seperately on payload and protocol data.
		struct range
		{
			range(int s, int l)
				: start(s)
				, length(l)
			{
				assert(s >= 0);
				assert(l > 0);
			}
			int start;
			int length;
		};
		static bool range_below_zero(const range& r)
		{ return r.start < 0; }
		std::deque<range> m_payloads;

		// timeouts
		boost::posix_time::ptime m_last_receive;
		boost::posix_time::ptime m_last_sent;

		// the selector is used to add and remove this
		// peer's socket from the writability monitor list.
		selector& m_selector;
		boost::shared_ptr<libtorrent::socket> m_socket;

		// this is the torrent this connection is
		// associated with. If the connection is an
		// incoming conncetion, this is set to zero
		// until the info_hash is received. Then it's
		// set to the torrent it belongs to.
		torrent* m_torrent;

		// this is set to false until the peer_id
		// is received from the other end. Or it is
		// true from the start if the conenction
		// was actively opened from our side.
		bool m_attached_to_torrent;

		// a back reference to the session
		// the peer belongs to.
		detail::session_impl& m_ses;

		// is true if it was we that connected to the peer
		// and false if we got an incomming connection
		// could be considered: true = local, false = remote
		bool m_active;

		// this is true as long as this peer's
		// socket is added to the selector to
		// monitor writability. Each time we do
		// something that generates data to be 
		// sent to this peer, we check this and
		// if it's not added to the selector we
		// add it. (this is done in send_buffer_updated())
		bool m_added_to_selector;

		// remote peer's id
		peer_id m_peer_id;

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

		// this is set to true if the handshake from
		// the peer indicated that it supports the
		// extension protocol
		bool m_supports_extensions;

		// the pieces the other end have
		std::vector<bool> m_have_piece;

		// the number of pieces this peer
		// has. Must be the same as
		// std::count(m_have_piece.begin(),
		// m_have_piece.end(), true)
		int m_num_pieces;

		// the queue of requests we have got
		// from this peer
		std::deque<peer_request> m_requests;

		// a list of pieces that have become available
		// and should be announced as available to
		// the peer
		std::vector<int> m_announce_queue;

		// the queue of blocks we have requested
		// from this peer
		std::deque<piece_block> m_download_queue;

		// statistics about upload and download speeds
		// and total amount of uploads and downloads for
		// this peer
		stat m_statistics;

		// the amount of data this peer has been given
		// as free upload. This is distributed from
		// peers from which we get free download
		// this will be negative on a peer from which
		// we get free download, and positive on peers
		// that we give the free upload, to keep the balance.
		size_type m_free_upload;

		// this is used to limit upload bandwidth.
		// it is reset to the allowed number of
		// bytes to send frequently. Every time
		// thie peer send some data,
		// m_send_quota_left variable will be decreased
		// so it can limit the number of bytes next
		// time it sends data. when it reaches zero
		// the client will stop send data and await
		// more quota. if it is set to -1, the peer
		// will ignore the qouta and send at maximum
		// speed
		int m_send_quota;
		int m_send_quota_left;

		// this is the maximum send quota we should give
		// this peer given the current download rate
		// and the current share ratio with this peer.
		// this limit will maintain a 1:1 share ratio.
		// -1 means no limit
		int m_send_quota_limit;

		// for every valid piece we receive where this
		// peer was one of the participants, we increase
		// this value. For every invalid piece we receive
		// where this peer was a participant, we decrease
		// this value. If it sinks below a threshold, its
		// considered a bad peer and will be banned.
		int m_trust_points;

		enum extension_index
		{
			extended_chat_message,
			num_supported_extensions
		};
		static const char* extension_names[num_supported_extensions];
		int m_extension_messages[num_supported_extensions];

		// the number of invalid piece-requests
		// we have got from this peer. If the request
		// queue gets empty, and there have been
		// invalid requests, we can assume the
		// peer is waiting for those pieces.
		// we can then clear its download queue
		// by sending choke, unchoke.
		int m_num_invalid_requests;

		// the time at which we started to get the last piece
		// message from this peer
		boost::posix_time::ptime m_last_piece;

		// the time it took for the peer to send the piece
		// message
		boost::posix_time::time_duration m_last_piece_time;

		// this is true if this connection has been added
		// to the list of connections that will be closed.
		bool m_disconnecting;

		// the time when this peer sent us a not_interested message
		// the last time.
		boost::posix_time::ptime m_became_uninterested;

		// the time when we sent a not_interested message to
		// this peer the last time.
		boost::posix_time::ptime m_became_uninteresting;
	};

	// this is called each time this peer generates some
	// data to be sent. It will add this socket to
	// the writibility monitor in the selector.
	inline void peer_connection::send_buffer_updated()
	{
		if (!has_data())
		{
			if (m_added_to_selector)
			{
				m_selector.remove_writable(m_socket);
				m_added_to_selector = false;
			}
			assert(!m_selector.is_writability_monitored(m_socket));
			return;
		}

		assert(m_send_quota_left > 0 || m_send_quota_left == -1);
		assert(has_data());
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

