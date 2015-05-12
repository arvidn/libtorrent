/*

Copyright (c) 2003-2014, Arvid Norberg
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
#include "libtorrent/size_type.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/bitfield.hpp"

namespace libtorrent
{
	// holds information and statistics about one peer
	// that libtorrent is connected to
	struct TORRENT_EXPORT peer_info
	{
		// flags for the peer_info::flags field. Indicates various states
		// the peer may be in. These flags are not mutually exclusive, but
		// not every combination of them makes sense either.
		enum peer_flags_t
		{
			// **we** are interested in pieces from this peer.
			interesting = 0x1,

			// **we** have choked this peer.
			choked = 0x2,

			// the peer is interested in **us**
			remote_interested = 0x4,

			// the peer has choked **us**.
			remote_choked = 0x8,

			// means that this peer supports the
			// `extension protocol`__.
			// 
			// __ extension_protocol.html
			supports_extensions = 0x10,

			// The connection was initiated by us, the peer has a
			// listen port open, and that port is the same as in the
			// address of this peer. If this flag is not set, this
			// peer connection was opened by this peer connecting to
			// us.
			local_connection = 0x20,

			// The connection is opened, and waiting for the
			// handshake. Until the handshake is done, the peer
			// cannot be identified.
			handshake = 0x40,

			// The connection is in a half-open state (i.e. it is
			// being connected).
			connecting = 0x80,

			// The connection is currently queued for a connection
			// attempt. This may happen if there is a limit set on
			// the number of half-open TCP connections.
			queued = 0x100,

			// The peer has participated in a piece that failed the
			// hash check, and is now "on parole", which means we're
			// only requesting whole pieces from this peer until
			// it either fails that piece or proves that it doesn't
			// send bad data.
			on_parole = 0x200,

			// This peer is a seed (it has all the pieces).
			seed = 0x400,

			// This peer is subject to an optimistic unchoke. It has
			// been unchoked for a while to see if it might unchoke
			// us in return an earn an upload/unchoke slot. If it
			// doesn't within some period of time, it will be choked
			// and another peer will be optimistically unchoked.
			optimistic_unchoke = 0x800,

			// This peer has recently failed to send a block within
			// the request timeout from when the request was sent.
			// We're currently picking one block at a time from this
			// peer.
			snubbed = 0x1000,

			// This peer has either explicitly (with an extension)
			// or implicitly (by becoming a seed) told us that it
			// will not downloading anything more, regardless of
			// which pieces we have.
			upload_only = 0x2000,

			// This means the last time this peer picket a piece,
			// it could not pick as many as it wanted because there
			// were not enough free ones. i.e. all pieces this peer
			// has were already requested from other peers.
			endgame_mode = 0x4000,

			// This flag is set if the peer was in holepunch mode
			// when the connection succeeded. This typically only
			// happens if both peers are behind a NAT and the peers
			// connect via the NAT holepunch mechanism.
			holepunched = 0x8000,

			// indicates that this socket is runnin on top of the
			// I2P transport.
			i2p_socket = 0x10000,

			// indicates that this socket is a uTP socket
			utp_socket = 0x20000,

			// indicates that this socket is running on top of an SSL
			// (TLS) channel
			ssl_socket = 0x40000,

			// this connection is obfuscated with RC4
			rc4_encrypted = 0x100000,
			
			// the handshake of this connection was obfuscated
			// with a diffie-hellman exchange
			plaintext_encrypted = 0x200000
		};

		// tells you in which state the peer is in. It is set to
		// any combination of the peer_flags_t enum.
		boost::uint32_t flags;

		// the flags indicating which sources a peer can
		// have come from. A peer may have been seen from
		// multiple sources
		enum peer_source_flags
		{
			// The peer was received from the tracker.
			tracker = 0x1,

			// The peer was received from the kademlia DHT.
			dht = 0x2,

			// The peer was received from the peer exchange
			// extension.
			pex = 0x4,

			// The peer was received from the local service
			// discovery (The peer is on the local network).
			lsd = 0x8,

			// The peer was added from the fast resume data.
			resume_data = 0x10,
			
			// we received an incoming connection from this peer
			incoming = 0x20
		};

		// a combination of flags describing from which sources this peer
		// was received.
		int source;

		// bits for the read_state and write_state
		enum bw_state
		{
			// The peer is not waiting for any external events to   
			// send or receive data.
			bw_idle = 0,

			// The peer is waiting for the rate limiter.
			bw_limit = 1,

			// The peer has quota and is currently waiting for a
			// network read or write operation to complete. This is
			// the state all peers are in if there are no bandwidth
			// limits.
			bw_network = 2,

			// The peer is waiting for the disk I/O thread to catch
			// up writing buffers to disk before downloading more.
			bw_disk = 4
		};
#ifndef TORRENT_NO_DEPRECATE
		enum bw_state_deprecated { bw_torrent = bw_limit, bw_global = bw_limit };
#endif

		// bitmasks indicating what state this peer is in with regards to sending
		// and receiving data. The states are declared in the bw_state enum.
		char read_state;
		char write_state;
		
		// the IP-address to this peer. The type is an asio endpoint. For
		// more info, see the asio_ documentation.
		//
		// .. _asio: http://asio.sourceforge.net/asio-0.3.8/doc/asio/reference.html
		tcp::endpoint ip;

		// the current upload and download speed we have to and from this peer
		// (including any protocol messages). updated about once per second
		int up_speed;
		int down_speed;

		// The transfer rates of payload data only updated about once per second
		int payload_up_speed;
		int payload_down_speed;

		// the total number of bytes downloaded from and uploaded to this peer.
		// These numbers do not include the protocol chatter, but only the
		// payload data.
		size_type total_download;
		size_type total_upload;

		// the peer's id as used in the bit torrent protocol. This id can be used
		// to extract 'fingerprints' from the peer. Sometimes it can tell you
		// which client the peer is using. See identify_client()_
		peer_id pid;

		// a bitfield, with one bit per piece in the torrent.
		// Each bit tells you if the peer has that piece (if it's set to 1)
		// or if the peer miss that piece (set to 0).
		bitfield pieces;

		// the number of bytes per second we are allowed to send to or receive
		// from this peer. It may be -1 if there's no local limit on the peer.
		// The global limit and the torrent limit may also be enforced.
		int upload_limit;
		int download_limit;

		// the time since we last sent a request
		// to this peer and since any transfer occurred with this peer
		time_duration last_request;
		time_duration last_active;

		// the time until all blocks in the request
		// queue will be d
		time_duration download_queue_time;
		int queue_bytes;

		// the number of seconds until the current front piece request will time
		// out. This timeout can be adjusted through
		// ``session_settings::request_timeout``.
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

		// the number of pieces this peer has participated in
		// sending us that turned out to fail the hash check.
		int num_hashfails;

		// the two letter `ISO 3166 country code`__ for the country the peer is
		// connected from. If the country hasn't been resolved yet, both chars
		// are set to 0. If the resolution failed for some reason, the field is
		// set to "--". If the resolution service returns an invalid country
		// code, it is set to "!!". The ``countries.nerd.dk`` service is used to
		// look up countries. This field will remain set to 0 unless the torrent
		// is set to resolve countries, see `resolve_countries()`_.
		// 
		// __ http://www.iso.org/iso/en/prods-services/iso3166ma/02iso-3166-code-lists/list-en1.html
		char country[2];

		// the name of the AS this peer is located in. This might be
		// an empty string if there is no name in the geo ip database.
		std::string inet_as_name;

		// the AS number the peer is located in.
		int inet_as;

		// this is the number of requests
		// we have sent to this peer
		// that we haven't got a response
		// for yet
		int download_queue_length;
		
		// the number of block requests that have
		// timed out, and are still in the download
		// queue
		int timed_out_requests;

		// the number of busy requests in the download
		// queue. A budy request is a request for a block
		// we've also requested from a different peer
		int busy_requests;

		// the number of requests messages that are currently in the
		// send buffer waiting to be sent.
		int requests_in_buffer;

		// the number of requests that is
		// tried to be maintained (this is
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
		int downloading_piece_index;
		int downloading_block_index;
		int downloading_progress;
		int downloading_total;
	
		// a string describing the software at the other end of the connection.
		// In some cases this information is not available, then it will contain
		// a string that may give away something about which software is running
		// in the other end. In the case of a web seed, the server type and
		// version will be a part of this string.
		std::string client;
		
		// the kind of connection this is. Used for the connection_type field.
		enum connection_type_t
		{
			// Regular bittorrent connection over TCP
			standard_bittorrent = 0,

			// HTTP connection using the `BEP 19`_ protocol
			web_seed = 1,

			// HTTP connection using the `BEP 17`_ protocol
			http_seed = 2
		};

		// the kind of connection this peer uses. See connection_type_t.
		int connection_type;
		
		// an estimate of the rate this peer is downloading at, in
		// bytes per second.
		int remote_dl_rate;

		// the number of bytes this peer has pending in the disk-io thread.
		// Downloaded and waiting to be written to disk. This is what is capped
		// by ``session_settings::max_queued_disk_bytes``.
		int pending_disk_bytes;

		// the number of bytes this peer has been assigned to be allowed to send
		// and receive until it has to request more quota from the bandwidth
		// manager.
		int send_quota;
		int receive_quota;

		// an estimated round trip time to this peer, in milliseconds. It is
		// estimated by timing the the tcp ``connect()``. It may be 0 for
		// incoming connections.
		int rtt;

		// the number of pieces this peer has.
		int num_pieces;

		// the highest download and upload rates seen on this connection. They
		// are given in bytes per second. This number is reset to 0 on reconnect.
		int download_rate_peak;
		int upload_rate_peak;
		
		// the progress of the peer in the range [0, 1]. This is always 0 when
		// floating point operations are diabled, instead use ``progress_ppm``.
		float progress; // [0, 1]

		// indicates the download progress of the peer in the range [0, 1000000]
		// (parts per million).
		int progress_ppm;

		// this is an estimation of the upload rate, to this peer, where it will
		// unchoke us. This is a coarse estimation based on the rate at which
		// we sent right before we were choked. This is primarily used for the
		// bittyrant choking algorithm.
		int estimated_reciprocation_rate;

		// the IP and port pair the socket is bound to locally. i.e. the IP
		// address of the interface it's going out over. This may be useful for
		// multi-homed clients with multiple interfaces to the internet.
		tcp::endpoint local_endpoint;
	};

	// internal
	struct TORRENT_EXPORT peer_list_entry
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
		boost::uint8_t failcount;
		// internal
		boost::uint8_t source;
	};

	// defined in policy.cpp
	int source_rank(int source_bitmask);
}

#endif // TORRENT_PEER_INFO_HPP_INCLUDED
