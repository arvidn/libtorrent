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

#include "libtorrent/buffer.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/allocate_resources.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/piece_block_progress.hpp"

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

	class peer_connection: public boost::noncopyable
	{
	friend class invariant_access;
	public:

		// this is the constructor where the we are teh active part.
		// The peer_conenction should handshake and verify that the
		// other end has the correct id
		peer_connection(
			detail::session_impl& ses
			, selector& sel
			, torrent* t
			, boost::shared_ptr<libtorrent::socket> s);

		// with this constructor we have been contacted and we still don't
		// know which torrent the connection belongs to
		peer_connection(
			detail::session_impl& ses
			, selector& sel
			, boost::shared_ptr<libtorrent::socket> s);

		// this function is called once the torrent associated
		// with this peer connection has retrieved the meta-
		// data. If the torrent was spawned with metadata
		// this is called from the constructor.
		void init();

		~peer_connection();

		// this adds an announcement in the announcement queue
		// it will let the peer know that we have the given piece
		void announce_piece(int index);

		// called from the main loop when this connection has any
		// work to do.
		void send_data();
		void receive_data();

		// tells if this connection has data it want to send
		// and has enough upload bandwidth quota left to send it.
		bool can_write() const;
		bool can_read() const;

		bool is_seed() const;

		bool has_timed_out() const;

		// will send a keep-alive message to the peer
		void keep_alive();

		const peer_id& id() const { return m_peer_id; }
		bool has_piece(int i) const;

		const std::deque<piece_block>& download_queue() const;
		const std::deque<piece_block>& request_queue() const;
		const std::deque<peer_request>& upload_queue() const;

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
		void add_stat(size_type downloaded, size_type uploaded);

		// is called once every second by the main loop
		void second_tick();

		boost::shared_ptr<libtorrent::socket> get_socket() const { return m_socket; }

		const peer_id& get_peer_id() const { return m_peer_id; }
		const std::vector<bool>& get_bitfield() const;

		// this will cause this peer_connection to be disconnected.
		// what it does is that it puts a reference to it in
		// m_ses.m_disconnect_peer list, which will be scanned in the
		// mainloop to disconnect peers.
		void disconnect();
		bool is_disconnecting() const { return m_disconnecting; }

		// This is called for every peer right after the upload
		// bandwidth has been distributed among them
		// It will reset the used bandwidth to 0 and
		// possibly add or remove the peer's socket
		// from the socket monitor
		void reset_upload_quota();

		// free upload.
		size_type total_free_upload() const;
		void add_free_upload(size_type free_upload);


		// trust management.
		void received_valid_data();
		void received_invalid_data();
		int trust_points() const;

		size_type share_diff() const;

		bool support_extensions() const { return m_supports_extensions; }

		// a connection is local if it was initiated by us.
		// if it was an incoming connection, it is remote
		bool is_local() const { return m_active; }

		void set_failed() { m_failed = true; }
		bool failed() const { return m_failed; }

#ifdef TORRENT_VERBOSE_LOGGING
		boost::shared_ptr<logger> m_logger;
#endif

		enum extension_index
		{
			extended_chat_message,
			extended_metadata_message,
			extended_peer_exchange_message,
			extended_listen_port_message,
			num_supported_extensions
		};

		bool supports_extension(extension_index ex) const
		{ return m_extension_messages[ex] != -1; }

		bool has_metadata() const;

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
		void on_dht_port(int received);

		void on_extension_list(int received);
		void on_extended(int received);

		void on_chat();
		void on_metadata();
		void on_peer_exchange();
		void on_listen_port();

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
		void send_metadata(std::pair<int, int> req);
		void send_metadata_request(std::pair<int, int> req);

		// how much bandwidth we're using, how much we want,
		// and how much we are allowed to use.
		resource_request m_ul_bandwidth_quota;
		resource_request m_dl_bandwidth_quota;

#ifndef NDEBUG
		void check_invariant() const;
		boost::posix_time::ptime m_last_choke;
#endif

	private:

		void send_block_requests();
		bool dispatch_message(int received);

		// if we don't have all metadata
		// this function will request a part of it
		// from this peer
		void request_metadata();

		// this is called each time this peer generates some
		// data to be sent. It will add this socket to
		// the writibility monitor in the selector.
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
			msg_dht_port,
	// extension protocol message
			msg_extension_list = 20,
			msg_extended,

			num_supported_messages
		};

		static const message_handler m_message_handler[num_supported_messages];

		int m_packet_size;
		int m_recv_pos;
		std::vector<char> m_recv_buffer;

		// this is the buffer where data that is
		// to be sent is stored until it gets
		// consumed by send()
//		std::vector<char> m_send_buffer;
		buffer m_send_buffer;

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
		bool m_writability_monitored;
		bool m_readability_monitored;

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

		// this is set to true if the connection timed
		// out or closed the connection. In that
		// case we will not try to reconnect to
		// this peer
		bool m_failed;

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

		// the blocks we have reserved in the piece
		// picker and will send to this peer.
		std::deque<piece_block> m_request_queue;
		
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

		// for every valid piece we receive where this
		// peer was one of the participants, we increase
		// this value. For every invalid piece we receive
		// where this peer was a participant, we decrease
		// this value. If it sinks below a threshold, its
		// considered a bad peer and will be banned.
		int m_trust_points;

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

		// the time when we last got a part of a
		// piece packet from this peer
		boost::posix_time::ptime m_last_piece;

		// this is true if this connection has been added
		// to the list of connections that will be closed.
		bool m_disconnecting;

		// the time when this peer sent us a not_interested message
		// the last time.
		boost::posix_time::ptime m_became_uninterested;

		// the time when we sent a not_interested message to
		// this peer the last time.
		boost::posix_time::ptime m_became_uninteresting;

		// this is set to the current time each time we get a
		// "I don't have metadata" message.
		boost::posix_time::ptime m_no_metadata;

		// this is set to the time when we last sent
		// a request for metadata to this peer
		boost::posix_time::ptime m_metadata_request;

		// this is set to true when we send a metadata
		// request to this peer, and reset to false when
		// we receive a reply to our request.
		bool m_waiting_metadata_request;

		// if we're waiting for a metadata request
		// this was the request we sent
		std::pair<int, int> m_last_metadata_request;

		// this is true until this socket has received
		// data for the first time. While connecting
		// the timeout will not be triggered. This is
		// because windows XP SP2 may delay connection
		// attempts, which means that the connection
		// may not even have been attempted when the
		// time out is reached.
		bool m_connecting;

		// the number of bytes of metadata we have received
		// so far from this per, only counting the current
		// request. Any previously finished requests
		// that have been forwarded to the torrent object
		// do not count.
		int m_metadata_progress;
	};
}

#endif // TORRENT_PEER_CONNECTION_HPP_INCLUDED

