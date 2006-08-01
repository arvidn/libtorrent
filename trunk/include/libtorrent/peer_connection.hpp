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

#include "libtorrent/debug.hpp"

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/smart_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/array.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/optional.hpp>
#include <boost/cstdint.hpp>
#include <boost/detail/atomic_count.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/buffer.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/allocate_resources.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/piece_block_progress.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/session.hpp"

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

	TORRENT_EXPORT void intrusive_ptr_add_ref(peer_connection const*);
	TORRENT_EXPORT void intrusive_ptr_release(peer_connection const*);	

	struct TORRENT_EXPORT protocol_error: std::runtime_error
	{
		protocol_error(const std::string& msg): std::runtime_error(msg) {};
	};

	class TORRENT_EXPORT peer_connection
		: public boost::noncopyable
	{
	friend class invariant_access;
	friend void intrusive_ptr_add_ref(peer_connection const*);
	friend void intrusive_ptr_release(peer_connection const*);
	public:

		// this is the constructor where the we are the active part.
		// The peer_conenction should handshake and verify that the
		// other end has the correct id
		peer_connection(
			detail::session_impl& ses
			, boost::weak_ptr<torrent> t
			, boost::shared_ptr<stream_socket> s
			, tcp::endpoint const& remote);

		// with this constructor we have been contacted and we still don't
		// know which torrent the connection belongs to
		peer_connection(
			detail::session_impl& ses
			, boost::shared_ptr<stream_socket> s);

		// this function is called once the torrent associated
		// with this peer connection has retrieved the meta-
		// data. If the torrent was spawned with metadata
		// this is called from the constructor.
		void init();

		void set_upload_limit(int limit);
		void set_download_limit(int limit);

		virtual ~peer_connection();

		// this adds an announcement in the announcement queue
		// it will let the peer know that we have the given piece
		void announce_piece(int index);

		// tells if this connection has data it want to send
		// and has enough upload bandwidth quota left to send it.
		bool can_write() const;
		bool can_read() const;

		bool is_seed() const;

		bool has_timed_out() const;

		// will send a keep-alive message to the peer
		void keep_alive();

		peer_id const& pid() const { return m_peer_id; }
		void set_pid(const peer_id& pid) { m_peer_id = pid; }
		bool has_piece(int i) const;

		const std::deque<piece_block>& download_queue() const;
		const std::deque<piece_block>& request_queue() const;
		const std::deque<peer_request>& upload_queue() const;

		bool is_interesting() const { return m_interesting; }
		bool is_choked() const { return m_choked; }

		bool is_peer_interested() const { return m_peer_interested; }
		bool has_peer_choked() const { return m_peer_choked; }

		// returns the torrent this connection is a part of
		// may be zero if the connection is an incoming connection
		// and it hasn't received enough information to determine
		// which torrent it should be associated with
		boost::weak_ptr<torrent> associated_torrent() const
		{ return m_torrent; }

		const stat& statistics() const { return m_statistics; }
		void add_stat(size_type downloaded, size_type uploaded);

		// is called once every second by the main loop
		void second_tick(float tick_interval);

		boost::shared_ptr<stream_socket> get_socket() const { return m_socket; }
		tcp::endpoint const& remote() const { return m_remote; }

		std::vector<bool> const& get_bitfield() const;

		// this will cause this peer_connection to be disconnected.
		// what it does is that it puts a reference to it in
		// m_ses.m_disconnect_peer list, which will be scanned in the
		// mainloop to disconnect peers.
		void disconnect();
		bool is_disconnecting() const { return m_disconnecting; }

		// this is called when the connection attempt has succeeded
		// and the peer_connection is supposed to set m_connecting
		// to false, and stop monitor writability
		void on_connection_complete(asio::error const& e);

		// returns true if this connection is still waiting to
		// finish the connection attempt
		bool is_connecting() const { return m_connecting; }

		// returns true if the socket of this peer hasn't been
		// attempted to connect yet (i.e. it's queued for
		// connection attempt).
		bool is_queued() const { return m_queued; }
	
		// called when it's time for this peer_conncetion to actually
		// initiate the tcp connection. This may be postponed until
		// the library isn't using up the limitation of half-open
		// tcp connections.	
		void connect();
		
		// This is called for every peer right after the upload
		// bandwidth has been distributed among them
		// It will reset the used bandwidth to 0.
		void reset_upload_quota();

		// free upload.
		size_type total_free_upload() const;
		void add_free_upload(size_type free_upload);

		// trust management.
		void received_valid_data();
		void received_invalid_data();
		int trust_points() const;

		size_type share_diff() const;

		// a connection is local if it was initiated by us.
		// if it was an incoming connection, it is remote
		bool is_local() const { return m_active; }

		void set_failed() { m_failed = true; }
		bool failed() const { return m_failed; }

		int desired_queue_size() const { return m_desired_queue_size; }

#ifdef TORRENT_VERBOSE_LOGGING
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
		void incoming_keepalive();
		void incoming_choke();
		void incoming_unchoke();
		void incoming_interested();
		void incoming_not_interested();
		void incoming_have(int piece_index);
		void incoming_bitfield(std::vector<bool> const& bitfield);
		void incoming_request(peer_request const& r);
		void incoming_piece(peer_request const& p, char const* data);
		void incoming_piece_fragment();
		void incoming_cancel(peer_request const& r);
		void incoming_dht_port(int listen_port);

		// the following functions appends messages
		// to the send buffer
		void send_choke();
		void send_unchoke();
		void send_interested();
		void send_not_interested();

		// adds a block to the request queue
		void add_request(piece_block const& b);
		void cancel_request(piece_block const& b);
		void send_block_requests();

		// how much bandwidth we're using, how much we want,
		// and how much we are allowed to use.
		resource_request m_ul_bandwidth_quota;
		resource_request m_dl_bandwidth_quota;

#ifndef NDEBUG
		void check_invariant() const;
		boost::posix_time::ptime m_last_choke;
#endif

		virtual void get_peer_info(peer_info& p) const = 0;

		// returns the block currently being
		// downloaded. And the progress of that
		// block. If the peer isn't downloading
		// a piece for the moment, the boost::optional
		// will be invalid.
		virtual boost::optional<piece_block_progress>
		downloading_piece_progress() const
		{
			#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << "downloading_piece_progress() dispatched to the base class!\n";
			#endif
			return boost::optional<piece_block_progress>();
		}

	protected:

		virtual void write_choke() = 0;
		virtual void write_unchoke() = 0;
		virtual void write_interested() = 0;
		virtual void write_not_interested() = 0;
		virtual void write_request(peer_request const& r) = 0;
		virtual void write_cancel(peer_request const& r) = 0;
		virtual void write_have(int index) = 0;
		virtual void write_keepalive() = 0;
		virtual void write_piece(peer_request const& r) = 0;
		
		virtual void on_connected() = 0;
		virtual void on_tick() {}
	
		virtual void on_receive(asio::error const& error
			, std::size_t bytes_transferred) = 0;
		virtual void on_sent(asio::error const& error
			, std::size_t bytes_transferred) = 0;

		void send_buffer(char const* begin, char const* end);
		buffer::interval allocate_send_buffer(int size);
		int send_buffer_size() const
		{
			return (int)m_send_buffer[0].size()
				+ (int)m_send_buffer[1].size()
				- m_write_pos;
		}

		buffer::const_interval receive_buffer() const
		{
			return buffer::const_interval(&m_recv_buffer[0]
				, &m_recv_buffer[0] + m_recv_pos);
		}

		void cut_receive_buffer(int size, int packet_size);

		void reset_recv_buffer(int packet_size);
		int packet_size() const { return m_packet_size; }

		bool packet_finished() const
		{
			assert(m_recv_pos <= m_packet_size);
			return m_packet_size == m_recv_pos;
		}

		void setup_send();
		void setup_receive();

		void attach_to_torrent(sha1_hash const& ih);

		bool verify_piece(peer_request const& p) const;

		// statistics about upload and download speeds
		// and total amount of uploads and downloads for
		// this peer
		stat m_statistics;

		// a back reference to the session
		// the peer belongs to.
		detail::session_impl& m_ses;

		boost::intrusive_ptr<peer_connection> self()
		{ return boost::intrusive_ptr<peer_connection>(this); }
		
		// called from the main loop when this connection has any
		// work to do.
		void on_send_data(asio::error const& error
			, std::size_t bytes_transferred);
		void on_receive_data(asio::error const& error
			, std::size_t bytes_transferred);

		// this is the limit on the number of outstanding requests
		// we have to this peer. This is initialized to the settings
		// in the session_settings structure. But it may be lowered
		// if the peer is known to require a smaller limit (like BitComet).
		// or if the extended handshake sets a limit.
		// web seeds also has a limit on the queue size.
		int m_max_out_request_queue;

		void set_timeout(int s) { m_timeout = s; }

	private:

		void fill_send_buffer();

		// the timeout in seconds
		int m_timeout;

		// the time when we last got a part of a
		// piece packet from this peer
		boost::posix_time::ptime m_last_piece;

		int m_packet_size;
		int m_recv_pos;
		std::vector<char> m_recv_buffer;

		// this is the buffer where data that is
		// to be sent is stored until it gets
		// consumed by send(). Since asio requires
		// the memory buffer that is given to async.
		// operations to remain valid until the operation
		// finishes, there has to be two buffers. While
		// waiting for a async_write operation on one
		// buffer, the other is used to write data to
		// be queued up.
		std::vector<char> m_send_buffer[2];
		// the current send buffer is the one to write to.
		// (m_current_send_buffer + 1) % 2 is the
		// buffer we're currently waiting for.
		int m_current_send_buffer;
		
		// if the sending buffer doesn't finish in one send
		// operation, this is the position within that buffer
		// where the next operation should continue
		int m_write_pos;

		// timeouts
		boost::posix_time::ptime m_last_receive;
		boost::posix_time::ptime m_last_sent;

		boost::shared_ptr<stream_socket> m_socket;
		tcp::endpoint m_remote;
		
		// this is the torrent this connection is
		// associated with. If the connection is an
		// incoming conncetion, this is set to zero
		// until the info_hash is received. Then it's
		// set to the torrent it belongs to.
		boost::weak_ptr<torrent> m_torrent;
		// is true if it was we that connected to the peer
		// and false if we got an incomming connection
		// could be considered: true = local, false = remote
		bool m_active;

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

		// the blocks we have reserved in the piece
		// picker and will send to this peer.
		std::deque<piece_block> m_request_queue;
		
		// the queue of blocks we have requested
		// from this peer
		std::deque<piece_block> m_download_queue;
		
		// the number of request we should queue up
		// at the remote end.
		int m_desired_queue_size;

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

		// if this is true, this peer is assumed to handle all piece
		// requests in fifo order. All skipped blocks are re-requested
		// immediately instead of having a looser requirement
		// where blocks can be sent out of order. The default is to
		// allow non-fifo order.
		bool m_assume_fifo;

		// the number of invalid piece-requests
		// we have got from this peer. If the request
		// queue gets empty, and there have been
		// invalid requests, we can assume the
		// peer is waiting for those pieces.
		// we can then clear its download queue
		// by sending choke, unchoke.
		int m_num_invalid_requests;

		// this is true if this connection has been added
		// to the list of connections that will be closed.
		bool m_disconnecting;

		// the time when this peer sent us a not_interested message
		// the last time.
		boost::posix_time::ptime m_became_uninterested;

		// the time when we sent a not_interested message to
		// this peer the last time.
		boost::posix_time::ptime m_became_uninteresting;

		// this is true until this socket has become
		// writable for the first time (i.e. the
		// connection completed). While connecting
		// the timeout will not be triggered. This is
		// because windows XP SP2 may delay connection
		// attempts, which means that the connection
		// may not even have been attempted when the
		// time out is reached.
		bool m_connecting;

		// This is true until connect is called on the
		// peer_connection's socket. It is false on incoming
		// connections.
		bool m_queued;

		// these are true when there's a asynchronous write
		// or read operation running.
		bool m_writing;
		// this is the number of bytes sent to the socket last
		// time it was invoked. This is compared against the
		// bytes_transferred in the callback function that tells
		// how much actually was sent. Then the quota can be
		// corrected according to the actual number of bytes sent
		int m_last_write_size;
		bool m_reading;
		int m_last_read_size;		
		// reference counter for intrusive_ptr
		mutable boost::detail::atomic_count m_refs;

#ifndef NDEBUG
	public:
		bool m_in_constructor;
#endif
	};
}

#endif // TORRENT_PEER_CONNECTION_HPP_INCLUDED

