/*

Copyright (c) 2015-2018, Arvid Norberg
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

#ifndef TORRENT_CLOSE_REASON_HPP
#define TORRENT_CLOSE_REASON_HPP

#include "libtorrent/error_code.hpp"

namespace libtorrent {

	// internal: these are all the reasons to disconnect a peer
	// all reasons caused by the peer sending unexpected data
	// are 256 and up.
	enum class close_reason_t : std::uint16_t
	{
		// no reason specified. Generic close.
		none = 0,

		// we're already connected to
		duplicate_peer_id,

		// this torrent has been removed, paused or stopped from this client.
		torrent_removed,

		// client failed to allocate necessary memory for this peer connection
		no_memory,

		// the source port of this peer is blocked
		port_blocked,

		// the source IP has been blocked
		blocked,

		// both ends of the connection are upload-only. staying connected would
		// be redundant
		upload_to_upload,

		// connection was closed because the other end is upload only and does
		// not have any pieces we're interested in
		not_interested_upload_only,

		// peer connection timed out (generic timeout)
		timeout,

		// the peers have not been interested in each other for a very long time.
		// disconnect
		timed_out_interest,

		// the peer has not sent any message in a long time.
		timed_out_activity,

		// the peer did not complete the handshake in too long
		timed_out_handshake,

		// the peer sent an interested message, but did not send a request
		// after a very long time after being unchoked.
		timed_out_request,

		// the encryption mode is blocked
		protocol_blocked,

		// the peer was disconnected in the hopes of finding a better peer
		// in the swarm
		peer_churn,

		// we have too many peers connected
		too_many_connections,

		// we have too many file-descriptors open
		too_many_files,

		// the encryption handshake failed
		encryption_error = 256,

		// the info hash sent as part of the handshake was not what we expected
		invalid_info_hash,

		self_connection,

		// the metadata received matched the info-hash, but failed to parse.
		// this is either someone finding a SHA1 collision, or the author of
		// the magnet link creating it from an invalid torrent
		invalid_metadata,

		// the advertised metadata size
		metadata_too_big,

		// invalid bittorrent messages
		message_too_big,
		invalid_message_id,
		invalid_message,
		invalid_piece_message,
		invalid_have_message,
		invalid_bitfield_message,
		invalid_choke_message,
		invalid_unchoke_message,
		invalid_interested_message,
		invalid_not_interested_message,
		invalid_request_message,
		invalid_reject_message,
		invalid_allow_fast_message,
		invalid_extended_message,
		invalid_cancel_message,
		invalid_dht_port_message,
		invalid_suggest_message,
		invalid_have_all_message,
		invalid_dont_have_message,
		invalid_have_none_message,
		invalid_pex_message,
		invalid_metadata_request_message,
		invalid_metadata_message,
		invalid_metadata_offset,

		// the peer sent a request while being choked
		request_when_choked,

		// the peer sent corrupt data
		corrupt_pieces,

		pex_message_too_big,
		pex_too_frequent
	};

	close_reason_t error_to_close_reason(error_code const& ec);
}

#endif
