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

#include <boost/smart_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/array.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/optional.hpp>

#include "libtorrent/socket.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/torrent_handle.hpp"

// TODO: each time a block is 'taken over'
// from another peer. That peer must be given
// a chance to request another block instead.
// Where it also could become not-interested.

// TODO: maybe there should be some kind of 
// per-torrent free-upload counter. All free
// download we get is put in there and increases
// the amount of free upload we give. The free upload
// could be distributed to the interest peers
// depending on amount we have downloaded from
// the peer and depending on the share ratio.
// there's no point in giving free upload to
// peers we can trade with. Maybe the free upload
// only should be given to those we are not interested
// in?

namespace libtorrent
{
	class torrent;

	namespace detail
	{
		struct session_impl;

		// reads an integer from a byte stream
		// in big endian byte order and converts
		// it to native endianess
		template <class InIt>
		unsigned int read_uint(InIt& start)
		{
			unsigned int val = 0;
			val |= static_cast<unsigned char>(*start) << 24; ++start;
			val |= static_cast<unsigned char>(*start) << 16; ++start;
			val |= static_cast<unsigned char>(*start) << 8; ++start;
			val |= static_cast<unsigned char>(*start); ++start;
			return val;
		}

		template <class InIt>
		inline int read_int(InIt& start)
		{
			return static_cast<int>(read_uint(start));
		}

		template <class InIt>
		inline unsigned char read_uchar(InIt& start)
		{
			unsigned char ret = static_cast<unsigned char>(*start);
			++start;
			return ret;
		}

		// reads an integer to a byte stream
		// and converts it from native endianess
		template <class OutIt>
		void write_uint(unsigned int val, OutIt& start)
		{
			*start = static_cast<unsigned char>((val >> 24) & 0xff); ++start;
			*start = static_cast<unsigned char>((val >> 16) & 0xff); ++start;
			*start = static_cast<unsigned char>((val >> 8) & 0xff); ++start;
			*start = static_cast<unsigned char>((val) & 0xff); ++start;
		}

		template <class OutIt>
		inline void write_int(int val, OutIt& start)
		{
			write_uint(static_cast<unsigned int>(val), start);
		}

		template <class OutIt>
		inline void write_uchar(unsigned char val, OutIt& start)
		{
			*start = static_cast<char>(val);
			++start;
		}

	}

	struct protocol_error: std::runtime_error
	{
		protocol_error(const std::string& msg): std::runtime_error(msg) {};
	};

	struct chat_message_alert: alert
	{
		chat_message_alert(const torrent_handle& h
			, const peer_id& send
			, const std::string& msg)
			: alert(alert::critical, msg)
			, handle(h)
			, sender(send)
			{}

		virtual std::auto_ptr<alert> clone() const
		{ return std::auto_ptr<alert>(new chat_message_alert(*this)); }

		torrent_handle handle;
		peer_id sender;
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
	public:

		// this is the constructor where the we are teh active part. The peer_conenction
		// should handshake and verify that the other end has the correct id
		peer_connection(
			detail::session_impl& ses
			, selector& sel
			, torrent* t
			, boost::shared_ptr<libtorrent::socket> s
			, const peer_id& p);

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

		const std::deque<piece_block>& download_queue() const throw()
		{ return m_download_queue; }

		void choke();
		void unchoke();
		void interested();
		void not_interested();
		void request_block(piece_block block);
		void cancel_block(piece_block block);

		// returns the block currently being
		// downloaded. And the progress of that
		// block. If the peer isn't downloading
		// a piece for the moment, the boost::optional
		// will be invalid.
		boost::optional<piece_block_progress> downloading_piece() const;

		bool is_interesting() const throw() { return m_interesting; }
		bool is_choked() const throw() { return m_choked; }

		bool is_peer_interested() const throw() { return m_peer_interested; }
		bool has_peer_choked() const throw() { return m_peer_choked; }

		// returns the torrent this connection is a part of
		// may be zero if the connection is an incoming connection
		// and it hasn't received enough information to determine
		// which torrent it should be associated with
		torrent* associated_torrent() const throw() { return m_attached_to_torrent?m_torrent:0; }

		const stat& statistics() const { return m_statistics; }

		// is called once every second by the main loop
		void second_tick();

		boost::shared_ptr<libtorrent::socket> get_socket() const { return m_socket; }

		const peer_id& get_peer_id() const { return m_peer_id; }
		const std::vector<bool>& get_bitfield() const { return m_have_piece; }

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

		int total_free_upload() const
		{ return m_free_upload; }

		void add_free_upload(int free_upload)
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

		int share_diff() const
		{
			return m_free_upload
				+ m_statistics.total_payload_download()
				- m_statistics.total_payload_upload();
		}

		bool support_extensions() const
		{ return m_supports_extensions; }

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

	private:

		bool dispatch_message(int received);
		void send_buffer_updated();

		void send_bitfield();
		void send_have(int index);
		void send_handshake();
		void send_extensions();
		void send_chat_message(const std::string& msg);

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

		std::size_t m_packet_size;
		std::size_t m_recv_pos;
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
			range(int s, int l): start(s), length(l) {}
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
		int m_free_upload;

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
		unsigned char m_extension_messages[num_supported_extensions];
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

