/*

Copyright (c) 2003-2018, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/piece_block_progress.hpp"
#include "libtorrent/bandwidth_limit.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/chained_buffer.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/bandwidth_socket.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/sliding_average.hpp"
#include "libtorrent/peer_class.hpp"
#include "libtorrent/peer_class_set.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/disk_observer.hpp"
#include "libtorrent/peer_connection_interface.hpp"
#include "libtorrent/socket.hpp" // for tcp::endpoint
#include "libtorrent/io_service_fwd.hpp"
#include "libtorrent/receive_buffer.hpp"
#include "libtorrent/aux_/allocating_handler.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/piece_block.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/piece_picker.hpp" // for picker_options_t
#include "libtorrent/units.hpp"

#include <ctime>
#include <algorithm>
#include <vector>
#include <string>
#include <utility> // for std::forward
#include <tuple> // for make_tuple
#include <array>
#include <cstdint>

namespace libtorrent {

	class torrent;
	struct torrent_peer;
	struct disk_interface;

#ifndef TORRENT_DISABLE_EXTENSIONS
	struct peer_plugin;
#endif

namespace aux {

	struct socket_type;
	struct session_interface;

}

	struct pending_block
	{
		pending_block(piece_block const& b) // NOLINT
			: block(b), send_buffer_offset(not_in_buffer), not_wanted(false)
			, timed_out(false), busy(false)
		{}

		piece_block block;

		static constexpr std::uint32_t not_in_buffer = 0x1fffffff;

		// the number of bytes into the send buffer this request is. Every time
		// some portion of the send buffer is transmitted, this offset is
		// decremented by the number of bytes sent. once this drops below 0, the
		// request_time field is set to the current time.
		// if the request has not been written to the send buffer, this field
		// remains not_in_buffer.
		std::uint32_t send_buffer_offset:29;

		// if any of these are set to true, this block
		// is not allocated
		// in the piece picker anymore, and open for
		// other peers to pick. This may be caused by
		// it either timing out or being received
		// unexpectedly from the peer
		std::uint32_t not_wanted:1;
		std::uint32_t timed_out:1;

		// the busy flag is set if the block was
		// requested from another peer when this
		// request was queued. We only allow a single
		// busy request at a time in each peer's queue
		std::uint32_t busy:1;

		bool operator==(pending_block const& b) const
		{
			return b.block == block
				&& b.not_wanted == not_wanted
				&& b.timed_out == timed_out;
		}
	};

	// argument pack passed to peer_connection constructor
	struct peer_connection_args
	{
		aux::session_interface* ses;
		aux::session_settings const* sett;
		counters* stats_counters;
		disk_interface* disk_thread;
		io_service* ios;
		std::weak_ptr<torrent> tor;
		std::shared_ptr<aux::socket_type> s;
		tcp::endpoint endp;
		torrent_peer* peerinfo;
		peer_id our_peer_id;
	};

	struct TORRENT_EXTRA_EXPORT peer_connection_hot_members
	{
		// if tor is set, this is an outgoing connection
		peer_connection_hot_members(
			std::weak_ptr<torrent> t
			, aux::session_interface& ses
			, aux::session_settings const& sett)
			: m_torrent(std::move(t))
			, m_ses(ses)
			, m_settings(sett)
			, m_disconnecting(false)
			, m_connecting(!m_torrent.expired())
			, m_endgame_mode(false)
			, m_snubbed(false)
			, m_interesting(false)
			, m_choked(true)
			, m_ignore_stats(false)
		{}

		// explicitly disallow assignment, to silence msvc warning
		peer_connection_hot_members& operator=(peer_connection_hot_members const&) = delete;

	protected:

		// the pieces the other end have
		typed_bitfield<piece_index_t> m_have_piece;

		// this is the torrent this connection is
		// associated with. If the connection is an
		// incoming connection, this is set to zero
		// until the info_hash is received. Then it's
		// set to the torrent it belongs to.

		// TODO: make this a raw pointer (to save size in
		// the first cache line) and make the constructor
		// take a raw pointer. torrent objects should always
		// outlive their peers
		std::weak_ptr<torrent> m_torrent;

	public:

		// a back reference to the session
		// the peer belongs to.
		aux::session_interface& m_ses;

		// settings that apply to this peer
		aux::session_settings const& m_settings;

	protected:

		// this is true if this connection has been added
		// to the list of connections that will be closed.
		bool m_disconnecting:1;

		// this is true until this socket has become
		// writable for the first time (i.e. the
		// connection completed). While connecting
		// the timeout will not be triggered. This is
		// because windows XP SP2 may delay connection
		// attempts, which means that the connection
		// may not even have been attempted when the
		// time out is reached.
		bool m_connecting:1;

		// this is set to true if the last time we tried to
		// pick a piece to download, we could only find
		// blocks that were already requested from other
		// peers. In this case, we should not try to pick
		// another piece until the last one we requested is done
		bool m_endgame_mode:1;

		// set to true when a piece request times out. The
		// result is that the desired pending queue size
		// is set to 1
		bool m_snubbed:1;

		// the peer has pieces we are interested in
		bool m_interesting:1;

		// we have choked the upload to the peer
		bool m_choked:1;

		// when this is set, the transfer stats for this connection
		// is not included in the torrent or session stats
		bool m_ignore_stats:1;
	};

	enum class connection_type : std::uint8_t
	{
		bittorrent,
		url_seed,
		http_seed
	};

	using request_flags_t = flags::bitfield_flag<std::uint8_t, struct request_flags_tag>;

	class TORRENT_EXTRA_EXPORT peer_connection
		: public peer_connection_hot_members
		, public bandwidth_socket
		, public peer_class_set
		, public disk_observer
		, public peer_connection_interface
		, public std::enable_shared_from_this<peer_connection>
		, public aux::error_handler_interface
	{
	friend class invariant_access;
	friend class torrent;
	friend struct cork;
	public:

		void on_exception(std::exception const& e) override;
		void on_error(error_code const& ec) override;

		virtual connection_type type() const = 0;

		enum channels
		{
			upload_channel,
			download_channel,
			num_channels
		};

		explicit peer_connection(peer_connection_args const& pack);

		// this function is called after it has been constructed and properly
		// reference counted. It is safe to call self() in this function
		// and schedule events with references to itself (that is not safe to
		// do in the constructor).
		virtual void start();

		~peer_connection() override;

		void set_peer_info(torrent_peer* pi) override
		{
			TORRENT_ASSERT(m_peer_info == nullptr || pi == nullptr );
			TORRENT_ASSERT(pi != nullptr || m_disconnect_started);
			m_peer_info = pi;
		}

		torrent_peer* peer_info_struct() const override
		{ return m_peer_info; }

		// this is called when the peer object is created, in case
		// it was let in by the connections limit slack. This means
		// the peer needs to, as soon as the handshake is done, either
		// disconnect itself or another peer.
		void peer_exceeds_limit()
		{ m_exceeded_limit = true; }

		// this is called if this peer causes another peer
		// to be disconnected, in which case it has fulfilled
		// its requirement.
		void peer_disconnected_other()
		{ m_exceeded_limit = false; }

		void send_allowed_set();

#ifndef TORRENT_DISABLE_EXTENSIONS
		void add_extension(std::shared_ptr<peer_plugin>);
		peer_plugin const* find_plugin(string_view type);
#endif

		// this function is called once the torrent associated
		// with this peer connection has retrieved the meta-
		// data. If the torrent was spawned with metadata
		// this is called from the constructor.
		void init();

		// this is called when the metadata is retrieved
		// and the files has been checked
		virtual void on_metadata() {}

		void on_metadata_impl();

		void picker_options(picker_options_t o) { m_picker_options = o; }

		int prefer_contiguous_blocks() const
		{
			if (on_parole()) return 1;
			return m_prefer_contiguous_blocks;
		}

		bool on_parole() const;

		picker_options_t picker_options() const;

		void prefer_contiguous_blocks(int num)
		{ m_prefer_contiguous_blocks = num; }

		bool request_large_blocks() const
		{ return m_request_large_blocks; }

		void request_large_blocks(bool b)
		{ m_request_large_blocks = b; }

		void set_endgame(bool b);
		bool endgame() const { return m_endgame_mode; }

		bool no_download() const { return m_no_download; }
		void no_download(bool b) { m_no_download = b; }

		bool ignore_stats() const { return m_ignore_stats; }
		void ignore_stats(bool b) { m_ignore_stats = b; }

		std::uint32_t peer_rank() const;

		void fast_reconnect(bool r);
		bool fast_reconnect() const override { return m_fast_reconnect; }

		// this is called when we receive a new piece
		// (and it has passed the hash check)
		void received_piece(piece_index_t index);

		// this adds an announcement in the announcement queue
		// it will let the peer know that we have the given piece
		void announce_piece(piece_index_t index);

#ifndef TORRENT_DISABLE_SUPERSEEDING
		// this will tell the peer to announce the given piece
		// and only allow it to request that piece
		void superseed_piece(piece_index_t replace_piece, piece_index_t new_piece);
		bool super_seeded_piece(piece_index_t index) const
		{
			return m_superseed_piece[0] == index
				|| m_superseed_piece[1] == index;
		}
#endif

		// tells if this connection has data it want to send
		// and has enough upload bandwidth quota left to send it.
		bool can_write() const;
		bool can_read();

		bool is_seed() const;
		int num_have_pieces() const { return m_num_pieces; }

#ifndef TORRENT_DISABLE_SHARE_MODE
		void set_share_mode(bool m);
		bool share_mode() const { return m_share_mode; }
#endif

		void set_upload_only(bool u);
		bool upload_only() const { return m_upload_only; }

		void set_holepunch_mode() override;

		// will send a keep-alive message to the peer
		void keep_alive();

		peer_id const& pid() const override { return m_peer_id; }
		void set_pid(peer_id const& peer_id) { m_peer_id = peer_id; }
		bool has_piece(piece_index_t i) const;

		std::vector<pending_block> const& download_queue() const;
		std::vector<pending_block> const& request_queue() const;
		std::vector<peer_request> const& upload_queue() const;

		void clear_request_queue();
		void clear_download_queue();

		// estimate of how long it will take until we have
		// received all piece requests that we have sent
		// if extra_bytes is specified, it will include those
		// bytes as if they've been requested
		time_duration download_queue_time(int extra_bytes = 0) const;

		bool is_interesting() const { return m_interesting; }
		bool is_choked() const override { return m_choked; }

		bool is_peer_interested() const { return m_peer_interested; }
		bool has_peer_choked() const { return m_peer_choked; }

		void choke_this_peer();
		void maybe_unchoke_this_peer();

		void update_interest();

		void get_peer_info(peer_info& p) const override;

		// returns the torrent this connection is a part of
		// may be zero if the connection is an incoming connection
		// and it hasn't received enough information to determine
		// which torrent it should be associated with
		std::weak_ptr<torrent> associated_torrent() const
		{ return m_torrent; }

		stat const& statistics() const override { return m_statistics; }
		void add_stat(std::int64_t downloaded, std::int64_t uploaded) override;
		void sent_bytes(int bytes_payload, int bytes_protocol);
		void received_bytes(int bytes_payload, int bytes_protocol);
		void trancieve_ip_packet(int bytes, bool ipv6);
		void sent_syn(bool ipv6);
		void received_synack(bool ipv6);

		// is called once every second by the main loop
		void second_tick(int tick_interval_ms);

		std::shared_ptr<aux::socket_type> get_socket() const { return m_socket; }
		tcp::endpoint const& remote() const override { return m_remote; }
		tcp::endpoint local_endpoint() const override { return m_local; }

		typed_bitfield<piece_index_t> const& get_bitfield() const;
		std::vector<piece_index_t> const& allowed_fast();
		std::vector<piece_index_t> const& suggested_pieces() const { return m_suggested_pieces; }

		time_point connected_time() const { return m_connect; }
		time_point last_received() const { return m_last_receive; }

		// this will cause this peer_connection to be disconnected.
		void disconnect(error_code const& ec
			, operation_t op, disconnect_severity_t = peer_connection_interface::normal) override;

		// called when a connect attempt fails (not when an
		// established connection fails)
		void connect_failed(error_code const& e);
		bool is_disconnecting() const override { return m_disconnecting; }

		// this is called when the connection attempt has succeeded
		// and the peer_connection is supposed to set m_connecting
		// to false, and stop monitor writability
		void on_connection_complete(error_code const& e);

		// returns true if this connection is still waiting to
		// finish the connection attempt
		bool is_connecting() const { return m_connecting; }

		// trust management.
		virtual void received_valid_data(piece_index_t index);
		// returns false if the peer should not be
		// disconnected
		virtual bool received_invalid_data(piece_index_t index, bool single_peer);

		// a connection is local if it was initiated by us.
		// if it was an incoming connection, it is remote
		bool is_outgoing() const final { return m_outgoing; }

		bool received_listen_port() const { return m_received_listen_port; }
		void received_listen_port()
		{ m_received_listen_port = true; }

		bool on_local_network() const;
		bool ignore_unchoke_slots() const;

		bool failed() const override { return m_failed; }

		int desired_queue_size() const
		{
			// this peer is in end-game mode we only want
			// one outstanding request
			return (m_endgame_mode || m_snubbed) ? 1 : m_desired_queue_size;
		}

		// compares this connection against the given connection
		// for which one is more eligible for an unchoke.
		// returns true if this is more eligible

		int download_payload_rate() const { return m_statistics.download_payload_rate(); }

		// resets the byte counters that are used to measure
		// the number of bytes transferred within unchoke cycles
		void reset_choke_counters();

		// if this peer connection is useless (neither party is
		// interested in the other), disconnect it
		// returns true if the connection was disconnected
		bool disconnect_if_redundant();

#if TORRENT_ABI_VERSION == 1
		void increase_est_reciprocation_rate();
		void decrease_est_reciprocation_rate();
		int est_reciprocation_rate() const { return m_est_reciprocation_rate; }
#endif

#ifndef TORRENT_DISABLE_LOGGING
		bool should_log(peer_log_alert::direction_t direction) const final;
		void peer_log(peer_log_alert::direction_t direction
			, char const* event, char const* fmt, ...) const noexcept final TORRENT_FORMAT(4,5);
		void peer_log(peer_log_alert::direction_t direction
			, char const* event) const noexcept;
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
		void incoming_have(piece_index_t piece_index);
		void incoming_dont_have(piece_index_t piece_index);
		void incoming_bitfield(typed_bitfield<piece_index_t> const& bits);
		void incoming_request(peer_request const& r);
		void incoming_piece(peer_request const& p, char const* data);
		void incoming_piece_fragment(int bytes);
		void start_receive_piece(peer_request const& r);
		void incoming_cancel(peer_request const& r);

		bool can_disconnect(error_code const& ec) const;
		void incoming_dht_port(int listen_port);

		void incoming_reject_request(peer_request const& r);
		void incoming_have_all();
		void incoming_have_none();
		void incoming_allowed_fast(piece_index_t index);
		void incoming_suggest(piece_index_t index);

		void set_has_metadata(bool m) { m_has_metadata = m; }
		bool has_metadata() const { return m_has_metadata; }

		// the following functions appends messages
		// to the send buffer
		bool send_choke();
		bool send_unchoke();
		void send_interested();
		void send_not_interested();
		void send_suggest(piece_index_t piece);
		void send_upload_only(bool enabled);

		void snub_peer();
		// reject any request in the request
		// queue from this piece
		void reject_piece(piece_index_t index);

		bool can_request_time_critical() const;

		// returns true if the specified block was actually made time-critical.
		// if the block was already time-critical, it returns false.
		bool make_time_critical(piece_block const& block);

		static constexpr request_flags_t time_critical = 0_bit;
		static constexpr request_flags_t busy = 1_bit;

		// adds a block to the request queue
		// returns true if successful, false otherwise
		bool add_request(piece_block const& b, request_flags_t flags = {});

		// clears the request queue and sends cancels for all messages
		// in the download queue
		void cancel_all_requests();

		// removes a block from the request queue or download queue
		// sends a cancel message if appropriate
		// refills the request queue, and possibly ignoring pieces requested
		// by peers in the ignore list (to avoid recursion)
		// if force is true, the blocks is also freed from the piece
		// picker, allowing another peer to request it immediately
		void cancel_request(piece_block const& b, bool force = false);
		void send_block_requests();

		void assign_bandwidth(int channel, int amount) override;

#if TORRENT_USE_INVARIANT_CHECKS
		void check_invariant() const;
#endif

		// is true until we can be sure that the other end
		// speaks our protocol (be it bittorrent or http).
		virtual bool in_handshake() const = 0;

		// returns the block currently being
		// downloaded. And the progress of that
		// block. If the peer isn't downloading
		// a piece for the moment, implementors
		// must return an object with the piece_index
		// value invalid (the default constructor).
		virtual piece_block_progress downloading_piece_progress() const;

		void send_buffer(span<char const> buf);
		void setup_send();

		template <typename Holder>
		void append_send_buffer(Holder buffer, int size)
		{
			TORRENT_ASSERT(is_single_thread());
			m_send_buffer.append_buffer(std::move(buffer), size);
		}

		int outstanding_bytes() const { return m_outstanding_bytes; }

		int send_buffer_size() const
		{ return m_send_buffer.size(); }

		int send_buffer_capacity() const
		{ return m_send_buffer.capacity(); }

		void max_out_request_queue(int s);
		int max_out_request_queue() const;

#if TORRENT_USE_ASSERTS
		bool piece_failed;
#endif

		std::time_t last_seen_complete() const { return m_last_seen_complete; }
		void set_last_seen_complete(int ago) { m_last_seen_complete = ::time(nullptr) - ago; }

		std::int64_t uploaded_in_last_round() const
		{ return m_statistics.total_payload_upload() - m_uploaded_at_last_round; }

		std::int64_t downloaded_in_last_round() const
		{ return m_statistics.total_payload_download() - m_downloaded_at_last_round; }

		std::int64_t uploaded_since_unchoked() const
		{ return m_statistics.total_payload_upload() - m_uploaded_at_last_unchoke; }

		// the time we last unchoked this peer
		time_point time_of_last_unchoke() const
		{ return m_last_unchoke; }

		// called when the disk write buffer is drained again, and we can
		// start downloading payload again
		void on_disk() override;

		int num_reading_bytes() const { return m_reading_bytes; }

		void setup_receive();

		std::shared_ptr<peer_connection> self()
		{
			TORRENT_ASSERT(!m_destructed);
			TORRENT_ASSERT(m_in_use == 1337);
			TORRENT_ASSERT(!m_in_constructor);
			return shared_from_this();
		}

		counters& stats_counters() const { return m_counters; }

		int get_priority(int channel) const;

	protected:

		virtual void get_specific_peer_info(peer_info& p) const = 0;

		virtual void write_choke() = 0;
		virtual void write_unchoke() = 0;
		virtual void write_interested() = 0;
		virtual void write_not_interested() = 0;
		virtual void write_request(peer_request const& r) = 0;
		virtual void write_cancel(peer_request const& r) = 0;
		virtual void write_have(piece_index_t index) = 0;
		virtual void write_dont_have(piece_index_t index) = 0;
		virtual void write_keepalive() = 0;
		virtual void write_piece(peer_request const& r, disk_buffer_holder buffer) = 0;
		virtual void write_suggest(piece_index_t piece) = 0;
		virtual void write_bitfield() = 0;

		virtual void write_reject_request(peer_request const& r) = 0;
		virtual void write_allow_fast(piece_index_t piece) = 0;
		virtual void write_upload_only(bool enabled) = 0;

		virtual void on_connected() = 0;
		virtual void on_tick() {}

		// implemented by concrete connection classes
		virtual void on_receive(error_code const& error
			, std::size_t bytes_transferred) = 0;
		virtual void on_sent(error_code const& error
			, std::size_t bytes_transferred) = 0;

		void send_piece_suggestions(int num);

		virtual
		std::tuple<int, span<span<char const>>>
		hit_send_barrier(span<span<char>> /* iovec */)
		{
			return std::make_tuple(INT_MAX
				, span<span<char const>>());
		}

		void attach_to_torrent(sha1_hash const& ih);

		bool verify_piece(peer_request const& p) const;

		void update_desired_queue_size();

		void set_send_barrier(int bytes)
		{
			TORRENT_ASSERT(bytes == INT_MAX || bytes <= send_buffer_size());
			m_send_barrier = bytes;
		}

		int get_send_barrier() const { return m_send_barrier; }

		virtual int timeout() const;

		io_service& get_io_service() { return m_ios; }

	private:

		// callbacks for data being sent or received
		void on_send_data(error_code const& error
			, std::size_t bytes_transferred);
		void on_receive_data(error_code const& error
			, std::size_t bytes_transferred);

		void account_received_bytes(int bytes_transferred);

		// explicitly disallow assignment, to silence msvc warning
		peer_connection& operator=(peer_connection const&);

		void do_update_interest();
		void fill_send_buffer();
		void on_disk_read_complete(disk_buffer_holder disk_block, disk_job_flags_t flags
			, storage_error const& error, peer_request const& r, time_point issue_time);
		void on_disk_write_complete(storage_error const& error
			, peer_request const &r, std::shared_ptr<torrent> t);
		void on_seed_mode_hashed(piece_index_t piece
			, sha1_hash const& piece_hash, storage_error const& error);
		int request_timeout() const;
		void check_graceful_pause();

		int wanted_transfer(int channel);
		int request_bandwidth(int channel, int bytes = 0);

		std::shared_ptr<aux::socket_type> m_socket;

		// the queue of blocks we have requested
		// from this peer
		aux::vector<pending_block> m_download_queue;

		// the queue of requests we have got
		// from this peer that haven't been issued
		// to the disk thread yet
		aux::vector<peer_request> m_requests;

		// this peer's peer info struct. This may
		// be 0, in case the connection is incoming
		// and hasn't been added to a torrent yet.
		torrent_peer* m_peer_info;

		// stats counters
		counters& m_counters;

		// the number of pieces this peer
		// has. Must be the same as
		// std::count(m_have_piece.begin(),
		// m_have_piece.end(), true)
		int m_num_pieces;


	public:
		// upload and download channel state
		// enum from peer_info::bw_state
		bandwidth_state_flags_t m_channel_state[2];

	protected:
		receive_buffer m_recv_buffer;

		// number of bytes this peer can send and receive
		int m_quota[2];

		// the blocks we have reserved in the piece
		// picker and will request from this peer.
		std::vector<pending_block> m_request_queue;

		// this is the limit on the number of outstanding requests
		// we have to this peer. This is initialized to the settings
		// in the settings_pack. But it may be lowered
		// if the peer is known to require a smaller limit (like BitComet).
		// or if the extended handshake sets a limit.
		// web seeds also has a limit on the queue size.
		int m_max_out_request_queue;

		// this is the peer we're actually talking to
		// it may not necessarily be the peer we're
		// connected to, in case we use a proxy
		tcp::endpoint m_remote;

	public:
		chained_buffer m_send_buffer;
	private:

		// the disk thread to use to issue disk jobs to
		disk_interface& m_disk_thread;

		// io service
		io_service& m_ios;

	protected:
#ifndef TORRENT_DISABLE_EXTENSIONS
		std::list<std::shared_ptr<peer_plugin>> m_extensions;
#endif
	private:

		// the average time between incoming pieces. Or, if there is no
		// outstanding request, the time since the piece was requested. It
		// is essentially an estimate of the time it will take to completely
		// receive a payload message after it has been requested.
		sliding_average<int, 20> m_request_time;

		// keep the io_service running as long as we
		// have peer connections
		io_service::work m_work;

		// the time when we last got a part of a
		// piece packet from this peer
		time_point m_last_piece = aux::time_now();

		// the time we sent a request to
		// this peer the last time
		time_point m_last_request = aux::time_now();
		// the time we received the last
		// piece request from the peer
		time_point m_last_incoming_request = min_time();

		// the time when we unchoked this peer
		time_point m_last_unchoke = aux::time_now();

		// if we're unchoked by this peer, this
		// was the time
		time_point m_last_unchoked = aux::time_now();

		// the time we last choked this peer. min_time() in
		// case we never unchoked it
		time_point m_last_choke = min_time();

		// timeouts
		time_point m_last_receive = aux::time_now();
		time_point m_last_sent = aux::time_now();

		// the last time we filled our send buffer with payload
		// this is used for timeouts
		time_point m_last_sent_payload = aux::time_now();

		// the time when the first entry in the request queue was requested. Used
		// for request timeout. it doesn't necessarily represent the time when a
		// specific request was made. Since requests can be handled out-of-order,
		// it represents whichever request the other end decided to respond to.
		// Once we get that response, we set it to the current time.
		// for more information, see the blog post at:
		// http://blog.libtorrent.org/2011/11/block-request-time-outs/
		time_point m_requested = aux::time_now();

		// the time when async_connect was called
		// or when the incoming connection was established
		time_point m_connect = aux::time_now();

		// the time when this peer sent us a not_interested message
		// the last time.
		time_point m_became_uninterested = aux::time_now();

		// the time when we sent a not_interested message to
		// this peer the last time.
		time_point m_became_uninteresting = aux::time_now();

		// the total payload download bytes
		// at the last unchoke round. This is used to
		// measure the number of bytes transferred during
		// an unchoke cycle, to unchoke peers the more bytes
		// they sent us
		std::int64_t m_downloaded_at_last_round = 0;
		std::int64_t m_uploaded_at_last_round = 0;

		// this is the number of bytes we had uploaded the
		// last time this peer was unchoked. This does not
		// reset each unchoke interval/round. This is used to
		// track upload across rounds, for the full duration of
		// the peer being unchoked. Specifically, it's used
		// for the round-robin unchoke algorithm.
		std::int64_t m_uploaded_at_last_unchoke = 0;

		// the number of payload bytes downloaded last second tick
		std::int32_t m_downloaded_last_second = 0;

		// the number of payload bytes uploaded last second tick
		std::int32_t m_uploaded_last_second = 0;

		// the number of bytes that the other
		// end has to send us in order to respond
		// to all outstanding piece requests we
		// have sent to it
		int m_outstanding_bytes = 0;

		aux::handler_storage<TORRENT_READ_HANDLER_MAX_SIZE> m_read_handler_storage;
		aux::handler_storage<TORRENT_WRITE_HANDLER_MAX_SIZE> m_write_handler_storage;

		// these are pieces we have recently sent suggests for to this peer.
		// it just serves as a queue to remember what we've sent, to avoid
		// re-sending suggests for the same piece
		// i.e. outgoing suggest pieces
		aux::vector<piece_index_t> m_suggest_pieces;

		// the pieces we will send to the peer
		// if requested (regardless of choke state)
		std::vector<piece_index_t> m_accept_fast;

		// a sent-piece counter for the allowed fast set
		// to avoid exploitation. Each slot is a counter
		// for one of the pieces from the allowed-fast set
		aux::vector<std::uint16_t> m_accept_fast_piece_cnt;

		// the pieces the peer will send us if
		// requested (regardless of choke state)
		std::vector<piece_index_t> m_allowed_fast;

		// pieces that has been suggested to be downloaded from this peer
		// i.e. incoming suggestions
		// TODO: 2 this should really be a circular buffer
		aux::vector<piece_index_t> m_suggested_pieces;

		// the time when this peer last saw a complete copy
		// of this torrent
		time_t m_last_seen_complete = 0;

		// the block we're currently receiving. Or
		// (-1, -1) if we're not receiving one
		piece_block m_receiving_block = piece_block::invalid;

		// the local endpoint for this peer, i.e. our address
		// and our port. If this is set for outgoing connections
		// before the connection completes, it means we want to
		// force the connection to be bound to the specified interface.
		// if it ends up being bound to a different local IP, the connection
		// is closed.
		tcp::endpoint m_local;

		// remote peer's id
		peer_id m_peer_id;

	protected:

		template <typename Fun, typename... Args>
		void wrap(Fun f, Args&&... a);

		// statistics about upload and download speeds
		// and total amount of uploads and downloads for
		// this peer
		// TODO: factor this out into its own class with a virtual interface
		// torrent and session should implement this interface
		stat m_statistics;

		// the number of outstanding bytes expected
		// to be received by extensions
		int m_extension_outstanding_bytes = 0;

		// the number of time critical requests
		// queued up in the m_request_queue that
		// soon will be committed to the download
		// queue. This is included in download_queue_time()
		// so that it can be used while adding more
		// requests and take the previous requests
		// into account without submitting it all
		// immediately
		int m_queued_time_critical = 0;

		// the number of bytes we are currently reading
		// from disk, that will be added to the send
		// buffer as soon as they complete
		int m_reading_bytes = 0;

		// options used for the piece picker. These flags will
		// be augmented with flags controlled by other settings
		// like sequential download etc. These are here to
		// let plugins control flags that should always be set
		picker_options_t m_picker_options{};

		// the number of invalid piece-requests
		// we have got from this peer. If the request
		// queue gets empty, and there have been
		// invalid requests, we can assume the
		// peer is waiting for those pieces.
		// we can then clear its download queue
		// by sending choke, unchoke.
		int m_num_invalid_requests = 0;

#ifndef TORRENT_DISABLE_SUPERSEEDING
		// if [0] is -1, super-seeding is not active. If it is >= 0
		// this is the piece that is available to this peer. Only
		// these two pieces can be downloaded from us by this peer.
		// This will remain the current piece for this peer until
		// another peer sends us a have message for this piece
		std::array<piece_index_t, 2> m_superseed_piece = {{piece_index_t(-1), piece_index_t(-1)}};
#endif

		// the number of bytes send to the disk-io
		// thread that hasn't yet been completely written.
		int m_outstanding_writing_bytes = 0;

		// max transfer rates seen on this peer
		int m_download_rate_peak = 0;
		int m_upload_rate_peak = 0;

#if TORRENT_ABI_VERSION == 1
		// when using the BitTyrant choker, this is our
		// estimated reciprocation rate. i.e. the rate
		// we need to send to this peer for it to unchoke
		// us
		int m_est_reciprocation_rate;
#endif

		// stop sending data after this many bytes, INT_MAX = inf
		int m_send_barrier = INT_MAX;

		// the number of request we should queue up
		// at the remote end.
		// TODO: 2 rename this target queue size
		std::uint16_t m_desired_queue_size = 4;

		// if set to non-zero, this peer will always prefer
		// to request entire n pieces, rather than blocks.
		// where n is the value of this variable.
		// if it is 0, the download rate limit setting
		// will be used to determine if whole pieces
		// are preferred.
		int m_prefer_contiguous_blocks = 0;

		// this is the number of times this peer has had
		// a request rejected because of a disk I/O failure.
		// once this reaches a certain threshold, the
		// peer is disconnected in order to avoid infinite
		// loops of consistent failures
		std::uint8_t m_disk_read_failures = 0;

		// this is used in seed mode whenever we trigger a hash check
		// for a piece, before we read it. It's used to throttle
		// the hash checks to just a few per peer at a time.
		std::uint8_t m_outstanding_piece_verification:3;

		// is true if it was we that connected to the peer
		// and false if we got an incoming connection
		// could be considered: true = local, false = remote
		bool m_outgoing:1;

		// is true if we learn the incoming connections listening
		// during the extended handshake
		bool m_received_listen_port:1;

		// if this is true, the disconnection
		// timestamp is not updated when the connection
		// is closed. This means the time until we can
		// reconnect to this peer is shorter, and likely
		// immediate.
		bool m_fast_reconnect:1;

		// this is set to true if the connection timed
		// out or closed the connection. In that
		// case we will not try to reconnect to
		// this peer
		bool m_failed:1;

		// this is set to true if the connection attempt
		// succeeded. i.e. the TCP 3-way handshake
		bool m_connected:1;

		// if this is true, the blocks picked by the piece
		// picker will be merged before passed to the
		// request function. i.e. subsequent blocks are
		// merged into larger blocks. This is used by
		// the http-downloader, to request whole pieces
		// at a time.
		bool m_request_large_blocks:1;

#ifndef TORRENT_DISABLE_SHARE_MODE
		// set to true if this peer is in share mode
		bool m_share_mode:1;
#endif

		// set to true when this peer is only uploading
		bool m_upload_only:1;

		// this is set to true once the bitfield is received
		bool m_bitfield_received:1;

		// if this is set to true, the client will not
		// pick any pieces from this peer
		bool m_no_download:1;

		// 1 bit

		// set to true while we're trying to holepunch
		bool m_holepunch_mode:1;

		// the other side has told us that it won't send anymore
		// data to us for a while
		bool m_peer_choked:1;

		// this is set to true when a have_all
		// message is received. This information
		// is used to fill the bitmask in init()
		bool m_have_all:1;

		// other side says that it's interested in downloading
		// from us.
		bool m_peer_interested:1;

		// set to true when we should recalculate interest
		// for this peer. Since this is a fairly expensive
		// operation, it's delayed until the second_tick is
		// fired, so that multiple events that wants to recalc
		// interest are coalesced into only triggering it once
		// the actual computation is done in do_update_interest().
		bool m_need_interest_update:1;

		// set to true if this peer has metadata, and false
		// otherwise.
		bool m_has_metadata:1;

		// this is set to true if this peer was accepted exceeding
		// the connection limit. It means it has to disconnect
		// itself, or some other peer, as soon as it's completed
		// the handshake. We need to wait for the handshake in
		// order to know which torrent it belongs to, to know which
		// other peers to compare it to.
		bool m_exceeded_limit:1;

		// this is slow-start at the bittorrent layer. It affects how we increase
		// desired queue size (i.e. the number of outstanding requests we keep).
		// While the underlying transport protocol is in slow-start, the number of
		// outstanding requests need to increase at the same pace to keep up.
		bool m_slow_start:1;

#if TORRENT_USE_ASSERTS
	public:
		bool m_in_constructor = true;
		bool m_disconnect_started = false;
		bool m_initialized = false;
		int m_in_use = 1337;
		int m_received_in_piece = 0;
		bool m_destructed = false;
		// this is true while there is an outstanding
		// async write job on the socket
		bool m_socket_is_writing = false;
		bool is_single_thread() const;
#endif
	};

	struct cork
	{
		explicit cork(peer_connection& p): m_pc(p)
		{
			if (m_pc.m_channel_state[peer_connection::upload_channel] & peer_info::bw_network)
				return;

			// pretend that there's an outstanding send operation already, to
			// prevent future calls to setup_send() from actually causing an
			// async_send() to be issued.
			m_pc.m_channel_state[peer_connection::upload_channel] |= peer_info::bw_network;
			m_need_uncork = true;
		}
		cork(cork const&) = delete;
		cork& operator=(cork const&) = delete;

		~cork()
		{
			if (!m_need_uncork) return;
			try {
				m_pc.m_channel_state[peer_connection::upload_channel] &= ~peer_info::bw_network;
				m_pc.setup_send();
			}
			catch (std::bad_alloc const&) {
				m_pc.disconnect(make_error_code(boost::system::errc::not_enough_memory)
					, operation_t::sock_write);
			}
			catch (boost::system::system_error const& err) {
				m_pc.disconnect(err.code(), operation_t::sock_write);
			}
			catch (...) {
				m_pc.disconnect(make_error_code(boost::system::errc::not_enough_memory)
					, operation_t::sock_write);
			}
		}
	private:
		peer_connection& m_pc;
		bool m_need_uncork = false;
	};

}

#endif // TORRENT_PEER_CONNECTION_HPP_INCLUDED
