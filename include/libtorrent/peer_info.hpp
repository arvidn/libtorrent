/*

Copyright (c) 2003-2011, 2013-2020, Arvid Norberg
Copyright (c) 2004, Magnus Jonsson
Copyright (c) 2016, Alden Torres
Copyright (c) 2019, Amir Abrams
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

#ifndef TORRENT_PEER_INFO_HPP_INCLUDED
#define TORRENT_PEER_INFO_HPP_INCLUDED

#include "libtorrent/socket.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/aux_/deprecated.hpp"

namespace libtorrent {

	// flags for the peer_info::flags field. Indicates various states
	// the peer may be in. These flags are not mutually exclusive, but
	// not every combination of them makes sense either.
	using peer_flags_t = flags::bitfield_flag<std::uint32_t, struct peer_flags_tag>;

	// the flags indicating which sources a peer can
	// have come from. A peer may have been seen from
	// multiple sources
	using peer_source_flags_t = flags::bitfield_flag<std::uint8_t, struct peer_source_flags_tag>;

	// flags indicating what is blocking network transfers in up- and down
	// direction
	using bandwidth_state_flags_t = flags::bitfield_flag<std::uint8_t, struct bandwidth_state_flags_tag>;

	using connection_type_t = flags::bitfield_flag<std::uint8_t, struct connection_type_tag>;

TORRENT_VERSION_NAMESPACE_2

	// holds information and statistics about one peer
	// that libtorrent is connected to
	struct TORRENT_EXPORT peer_info
	{
		// hidden
		peer_info();
		~peer_info();
		peer_info(peer_info const&);
		peer_info(peer_info&&);
		peer_info& operator=(peer_info const&);

		// a string describing the software at the other end of the connection.
		// In some cases this information is not available, then it will contain
		// a string that may give away something about which software is running
		// in the other end. In the case of a web seed, the server type and
		// version will be a part of this string.
		std::string client;

		// a bitfield, with one bit per piece in the torrent. Each bit tells you
		// if the peer has that piece (if it's set to 1) or if the peer miss that
		// piece (set to 0).
		typed_bitfield<piece_index_t> pieces;

		// the total number of bytes downloaded from and uploaded to this peer.
		// These numbers do not include the protocol chatter, but only the
		// payload data.
		std::int64_t total_download;
		std::int64_t total_upload;

		// the time since we last sent a request to this peer and since any
		// transfer occurred with this peer
		time_duration last_request;
		time_duration last_active;

		// the time until all blocks in the request queue will be downloaded
		time_duration download_queue_time;

#if TORRENT_ABI_VERSION == 1
		using peer_flags_t = libtorrent::peer_flags_t;
		using peer_source_flags = libtorrent::peer_source_flags_t;
#endif

		// **we** are interested in pieces from this peer.
		static constexpr peer_flags_t interesting = 0_bit;

		// **we** have choked this peer.
		static constexpr peer_flags_t choked = 1_bit;

		// the peer is interested in **us**
		static constexpr peer_flags_t remote_interested = 2_bit;

		// the peer has choked **us**.
		static constexpr peer_flags_t remote_choked = 3_bit;

		// means that this peer supports the
		// `extension protocol`__.
		//
		// __ extension_protocol.html
		static constexpr peer_flags_t supports_extensions = 4_bit;

		// The connection was initiated by us, the peer has a
		// listen port open, and that port is the same as in the
		// address of this peer. If this flag is not set, this
		// peer connection was opened by this peer connecting to
		// us.
		static constexpr peer_flags_t outgoing_connection = 5_bit;

		// deprecated synonym for outgoing_connection
		static constexpr peer_flags_t local_connection = 5_bit;

		// The connection is opened, and waiting for the
		// handshake. Until the handshake is done, the peer
		// cannot be identified.
		static constexpr peer_flags_t handshake = 6_bit;

		// The connection is in a half-open state (i.e. it is
		// being connected).
		static constexpr peer_flags_t connecting = 7_bit;

#if TORRENT_ABI_VERSION == 1
		// The connection is currently queued for a connection
		// attempt. This may happen if there is a limit set on
		// the number of half-open TCP connections.
		TORRENT_DEPRECATED static constexpr peer_flags_t queued = 8_bit;
#endif

		// The peer has participated in a piece that failed the
		// hash check, and is now "on parole", which means we're
		// only requesting whole pieces from this peer until
		// it either fails that piece or proves that it doesn't
		// send bad data.
		static constexpr peer_flags_t on_parole = 9_bit;

		// This peer is a seed (it has all the pieces).
		static constexpr peer_flags_t seed = 10_bit;

		// This peer is subject to an optimistic unchoke. It has
		// been unchoked for a while to see if it might unchoke
		// us in return an earn an upload/unchoke slot. If it
		// doesn't within some period of time, it will be choked
		// and another peer will be optimistically unchoked.
		static constexpr peer_flags_t optimistic_unchoke = 11_bit;

		// This peer has recently failed to send a block within
		// the request timeout from when the request was sent.
		// We're currently picking one block at a time from this
		// peer.
		static constexpr peer_flags_t snubbed = 12_bit;

		// This peer has either explicitly (with an extension)
		// or implicitly (by becoming a seed) told us that it
		// will not downloading anything more, regardless of
		// which pieces we have.
		static constexpr peer_flags_t upload_only = 13_bit;

		// This means the last time this peer picket a piece,
		// it could not pick as many as it wanted because there
		// were not enough free ones. i.e. all pieces this peer
		// has were already requested from other peers.
		static constexpr peer_flags_t endgame_mode = 14_bit;

		// This flag is set if the peer was in holepunch mode
		// when the connection succeeded. This typically only
		// happens if both peers are behind a NAT and the peers
		// connect via the NAT holepunch mechanism.
		static constexpr peer_flags_t holepunched = 15_bit;

		// indicates that this socket is running on top of the
		// I2P transport.
		static constexpr peer_flags_t i2p_socket = 16_bit;

		// indicates that this socket is a uTP socket
		static constexpr peer_flags_t utp_socket = 17_bit;

		// indicates that this socket is running on top of an SSL
		// (TLS) channel
		static constexpr peer_flags_t ssl_socket = 18_bit;

		// this connection is obfuscated with RC4
		static constexpr peer_flags_t rc4_encrypted = 19_bit;

		// the handshake of this connection was obfuscated
		// with a Diffie-Hellman exchange
		static constexpr peer_flags_t plaintext_encrypted = 20_bit;

		// tells you in which state the peer is in. It is set to
		// any combination of the peer_flags_t flags above.
		peer_flags_t flags;

		// The peer was received from the tracker.
		static constexpr peer_source_flags_t tracker = 0_bit;

		// The peer was received from the kademlia DHT.
		static constexpr peer_source_flags_t dht = 1_bit;

		// The peer was received from the peer exchange
		// extension.
		static constexpr peer_source_flags_t pex = 2_bit;

		// The peer was received from the local service
		// discovery (The peer is on the local network).
		static constexpr peer_source_flags_t lsd = 3_bit;

		// The peer was added from the fast resume data.
		static constexpr peer_source_flags_t resume_data = 4_bit;

		// we received an incoming connection from this peer
		static constexpr peer_source_flags_t incoming = 5_bit;

		// a combination of flags describing from which sources this peer
		// was received. A combination of the peer_source_flags_t above.
		peer_source_flags_t source;

		// the current upload and download speed we have to and from this peer
		// (including any protocol messages). updated about once per second
		int up_speed;
		int down_speed;

		// The transfer rates of payload data only updated about once per second
		int payload_up_speed;
		int payload_down_speed;

		// the peer's id as used in the bit torrent protocol. This id can be used
		// to extract 'fingerprints' from the peer. Sometimes it can tell you
		// which client the peer is using. See identify_client()_
		peer_id pid;

		// the number of bytes we have requested from this peer, but not yet
		// received.
		int queue_bytes;

		// the number of seconds until the current front piece request will time
		// out. This timeout can be adjusted through
		// ``settings_pack::request_timeout``.
		// -1 means that there is not outstanding request.
		int request_timeout;

		// the number of bytes allocated
		// and used for the peer's send buffer, respectively.
		int send_buffer_size;
		int used_send_buffer;

		// the number of bytes
		// allocated and used as receive buffer, respectively.
		int receive_buffer_size;
		int used_receive_buffer;
		int receive_buffer_watermark;

		// the number of pieces this peer has participated in sending us that
		// turned out to fail the hash check.
		int num_hashfails;

		// this is the number of requests we have sent to this peer that we
		// haven't got a response for yet
		int download_queue_length;

		// the number of block requests that have timed out, and are still in the
		// download queue
		int timed_out_requests;

		// the number of busy requests in the download queue. A busy request is a
		// request for a block we've also requested from a different peer
		int busy_requests;

		// the number of requests messages that are currently in the send buffer
		// waiting to be sent.
		int requests_in_buffer;

		// the number of requests that is tried to be maintained (this is
		// typically a function of download speed)
		int target_dl_queue_length;

		// the number of piece-requests we have received from this peer
		// that we haven't answered with a piece yet.
		int upload_queue_length;

		// the number of times this peer has "failed". i.e. failed to connect or
		// disconnected us. The failcount is decremented when we see this peer in
		// a tracker response or peer exchange message.
		int failcount;

		// You can know which piece, and which part of that piece, that is
		// currently being downloaded from a specific peer by looking at these
		// four members. ``downloading_piece_index`` is the index of the piece
		// that is currently being downloaded. This may be set to -1 if there's
		// currently no piece downloading from this peer. If it is >= 0, the
		// other three members are valid. ``downloading_block_index`` is the
		// index of the block (or sub-piece) that is being downloaded.
		// ``downloading_progress`` is the number of bytes of this block we have
		// received from the peer, and ``downloading_total`` is the total number
		// of bytes in this block.
		piece_index_t downloading_piece_index;
		int downloading_block_index;
		int downloading_progress;
		int downloading_total;

#if TORRENT_ABI_VERSION <= 2
		using connection_type_t = libtorrent::connection_type_t;
#endif
		// Regular bittorrent connection
		static constexpr connection_type_t standard_bittorrent = 0_bit;

			// HTTP connection using the `BEP 19`_ protocol
		static constexpr connection_type_t web_seed = 1_bit;

			// HTTP connection using the `BEP 17`_ protocol
		static constexpr connection_type_t http_seed = 2_bit;

		// the kind of connection this peer uses. See connection_type_t.
		connection_type_t connection_type;

#if TORRENT_ABI_VERSION == 1
		// an estimate of the rate this peer is downloading at, in
		// bytes per second.
		TORRENT_DEPRECATED int remote_dl_rate;
#endif

		// the number of bytes this peer has pending in the disk-io thread.
		// Downloaded and waiting to be written to disk. This is what is capped
		// by ``settings_pack::max_queued_disk_bytes``.
		int pending_disk_bytes;

		// number of outstanding bytes to read
		// from disk
		int pending_disk_read_bytes;

		// the number of bytes this peer has been assigned to be allowed to send
		// and receive until it has to request more quota from the bandwidth
		// manager.
		int send_quota;
		int receive_quota;

		// an estimated round trip time to this peer, in milliseconds. It is
		// estimated by timing the TCP ``connect()``. It may be 0 for
		// incoming connections.
		int rtt;

		// the number of pieces this peer has.
		int num_pieces;

		// the highest download and upload rates seen on this connection. They
		// are given in bytes per second. This number is reset to 0 on reconnect.
		int download_rate_peak;
		int upload_rate_peak;

		// the progress of the peer in the range [0, 1]. This is always 0 when
		// floating point operations are disabled, instead use ``progress_ppm``.
		float progress; // [0, 1]

		// indicates the download progress of the peer in the range [0, 1000000]
		// (parts per million).
		int progress_ppm;

#if TORRENT_ABI_VERSION == 1
		// this is an estimation of the upload rate, to this peer, where it will
		// unchoke us. This is a coarse estimation based on the rate at which
		// we sent right before we were choked. This is primarily used for the
		// bittyrant choking algorithm.
		TORRENT_DEPRECATED int estimated_reciprocation_rate;
#endif

		// the IP-address to this peer. The type is an asio endpoint. For
		// more info, see the asio_ documentation.
		//
		// .. _asio: http://asio.sourceforge.net/asio-0.3.8/doc/asio/reference.html
		tcp::endpoint ip;

		// the IP and port pair the socket is bound to locally. i.e. the IP
		// address of the interface it's going out over. This may be useful for
		// multi-homed clients with multiple interfaces to the internet.
		tcp::endpoint local_endpoint;

		// The peer is not waiting for any external events to
		// send or receive data.
		static constexpr bandwidth_state_flags_t bw_idle = 0_bit;

		// The peer is waiting for the rate limiter.
		static constexpr bandwidth_state_flags_t bw_limit = 1_bit;

		// The peer has quota and is currently waiting for a
		// network read or write operation to complete. This is
		// the state all peers are in if there are no bandwidth
		// limits.
		static constexpr bandwidth_state_flags_t bw_network = 2_bit;

		// The peer is waiting for the disk I/O thread to catch
		// up writing buffers to disk before downloading more.
		static constexpr bandwidth_state_flags_t bw_disk = 4_bit;

		// bitmasks indicating what state this peer
		// is in with regards to sending and receiving data. The states are
		// defined as independent flags of type bandwidth_state_flags_t, in this
		// class.
		bandwidth_state_flags_t read_state;
		bandwidth_state_flags_t write_state;

#if TORRENT_ABI_VERSION == 1
		TORRENT_DEPRECATED static constexpr bandwidth_state_flags_t bw_torrent = bw_limit;
		TORRENT_DEPRECATED static constexpr bandwidth_state_flags_t bw_global = bw_limit;

		// the number of bytes per second we are allowed to send to or receive
		// from this peer. It may be -1 if there's no local limit on the peer.
		// The global limit and the torrent limit may also be enforced.
		TORRENT_DEPRECATED int upload_limit;
		TORRENT_DEPRECATED int download_limit;

		// a measurement of the balancing of free download (that we get) and free
		// upload that we give. Every peer gets a certain amount of free upload,
		// but this member says how much *extra* free upload this peer has got.
		// If it is a negative number it means that this was a peer from which we
		// have got this amount of free download.
		TORRENT_DEPRECATED std::int64_t load_balancing;
#endif
	};

TORRENT_VERSION_NAMESPACE_2_END

#if TORRENT_ABI_VERSION == 1
	// internal
	struct TORRENT_EXTRA_EXPORT peer_list_entry
	{
		// internal
		enum flags_t
		{
			banned = 1
		};

		// internal
		tcp::endpoint ip;
		// internal
		int flags;
		// internal
		std::uint8_t failcount;
		// internal
		std::uint8_t source;
	};
#endif

}

#endif // TORRENT_PEER_INFO_HPP_INCLUDED
