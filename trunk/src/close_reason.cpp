/*

Copyright (c) 2015, Arvid Norberg
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

#include "libtorrent/close_reason.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/assert.hpp"

#include <boost/asio/error.hpp>

namespace libtorrent
{

	close_reason_t error_to_close_reason(error_code const& ec)
	{
		if (ec.category() == get_libtorrent_category())
		{
#define TORRENT_MAP(error, close_reason) \
	case errors:: error : \
		return close_reason;

			switch (ec.value())
			{
				TORRENT_MAP(invalid_swarm_metadata, close_invalid_metadata)
				TORRENT_MAP(session_is_closing, close_torrent_removed)
				TORRENT_MAP(peer_sent_empty_piece, close_invalid_piece_message)
				TORRENT_MAP(mismatching_info_hash, close_invalid_info_hash)
				TORRENT_MAP(port_blocked, close_port_blocked)
				TORRENT_MAP(destructing_torrent, close_torrent_removed)
				TORRENT_MAP(timed_out, close_timeout)
				TORRENT_MAP(upload_upload_connection, close_upload_to_upload)
				TORRENT_MAP(uninteresting_upload_peer, close_not_interested_upload_only)
				TORRENT_MAP(invalid_info_hash, close_invalid_info_hash)
				TORRENT_MAP(torrent_paused, close_torrent_removed)
				TORRENT_MAP(invalid_have, close_invalid_have_message)
				TORRENT_MAP(invalid_bitfield_size, close_invalid_bitfield_message)
				TORRENT_MAP(too_many_requests_when_choked, close_request_when_choked)
				TORRENT_MAP(invalid_piece, close_invalid_piece_message)
				TORRENT_MAP(invalid_piece_size, close_invalid_piece_message)
				TORRENT_MAP(no_memory, close_no_memory)
				TORRENT_MAP(torrent_aborted, close_torrent_removed)
				TORRENT_MAP(self_connection, close_self_connection)
				TORRENT_MAP(timed_out_no_interest, close_timed_out_interest)
				TORRENT_MAP(timed_out_inactivity, close_timed_out_activity)
				TORRENT_MAP(timed_out_no_handshake, close_timed_out_handshake)
				TORRENT_MAP(timed_out_no_request, close_timed_out_request)
				TORRENT_MAP(invalid_choke, close_invalid_choke_message)
				TORRENT_MAP(invalid_unchoke, close_invalid_unchoke_message)
				TORRENT_MAP(invalid_interested, close_invalid_interested_message)
				TORRENT_MAP(invalid_not_interested, close_invalid_not_interested_message)
				TORRENT_MAP(invalid_request, close_invalid_request_message)
				TORRENT_MAP(invalid_hash_list, close_invalid_message)
				TORRENT_MAP(invalid_hash_piece, close_invalid_message)
				TORRENT_MAP(invalid_cancel, close_invalid_cancel_message)
				TORRENT_MAP(invalid_dht_port, close_invalid_dht_port_message)
				TORRENT_MAP(invalid_suggest, close_invalid_suggest_message)
				TORRENT_MAP(invalid_have_all, close_invalid_have_all_message)
				TORRENT_MAP(invalid_have_none, close_invalid_have_none_message)
				TORRENT_MAP(invalid_reject, close_invalid_reject_message)
				TORRENT_MAP(invalid_allow_fast, close_invalid_allow_fast_message)
				TORRENT_MAP(invalid_extended, close_invalid_extended_message)
				TORRENT_MAP(invalid_message, close_invalid_message_id)
				TORRENT_MAP(sync_hash_not_found, close_encryption_error)
				TORRENT_MAP(invalid_encryption_constant, close_encryption_error)
				TORRENT_MAP(no_plaintext_mode, close_protocol_blocked)
				TORRENT_MAP(no_rc4_mode, close_protocol_blocked)
				TORRENT_MAP(unsupported_encryption_mode_selected, close_protocol_blocked)
				TORRENT_MAP(invalid_pad_size, close_encryption_error)
				TORRENT_MAP(invalid_encrypt_handshake, close_encryption_error)
				TORRENT_MAP(no_incoming_encrypted, close_protocol_blocked)
				TORRENT_MAP(no_incoming_regular, close_protocol_blocked)
				TORRENT_MAP(duplicate_peer_id, close_duplicate_peer_id)
				TORRENT_MAP(torrent_removed, close_torrent_removed)
				TORRENT_MAP(packet_too_large, close_message_too_big)
				TORRENT_MAP(torrent_not_ready, close_torrent_removed)
				TORRENT_MAP(session_closing, close_torrent_removed)
				TORRENT_MAP(optimistic_disconnect, close_peer_churn)
				TORRENT_MAP(torrent_finished, close_upload_to_upload)
				TORRENT_MAP(too_many_corrupt_pieces, close_corrupt_pieces)
				TORRENT_MAP(too_many_connections, close_too_many_connections)
				TORRENT_MAP(peer_banned, close_blocked)
				TORRENT_MAP(stopping_torrent, close_torrent_removed)
				TORRENT_MAP(metadata_too_large, close_metadata_too_big)
				TORRENT_MAP(invalid_metadata_size, close_metadata_too_big)
				TORRENT_MAP(invalid_metadata_request, close_invalid_metadata_request_message)
				TORRENT_MAP(invalid_metadata_offset, close_invalid_metadata_offset)
				TORRENT_MAP(invalid_metadata_message, close_invalid_metadata_message)
				TORRENT_MAP(pex_message_too_large, close_pex_message_too_big)
				TORRENT_MAP(invalid_pex_message, close_invalid_pex_message)
				TORRENT_MAP(invalid_lt_tracker_message, close_invalid_message)
				TORRENT_MAP(too_frequent_pex, close_pex_too_frequent)
				TORRENT_MAP(invalid_dont_have, close_invalid_dont_have_message)
				TORRENT_MAP(requires_ssl_connection, close_protocol_blocked)
				TORRENT_MAP(invalid_ssl_cert, close_blocked)
				TORRENT_MAP(not_an_ssl_torrent, close_blocked)
				TORRENT_MAP(banned_by_port_filter, close_port_blocked)

#ifdef TORRENT_USE_ASSERTS
				case errors::redirecting:
					return close_no_reason;
#endif

				default:
					return close_no_reason;
			}
		}
		else if (ec.category() == boost::asio::error::get_misc_category())
		{
			switch (ec.value())
			{
				case boost::asio::error::eof:
					return close_no_reason;
			}
		}
		else if (ec.category() == boost::system::system_category())
		{
			switch (ec.value())
			{
#ifdef TORRENT_USE_ASSERTS
				case boost::system::errc::connection_reset:
				case boost::system::errc::broken_pipe:
					return close_no_reason;
#endif
				case boost::system::errc::timed_out:
					return close_timeout;
				case boost::system::errc::too_many_files_open:
				case boost::system::errc::too_many_files_open_in_system:
					return close_too_many_files;
				case boost::system::errc::not_enough_memory:
				case boost::system::errc::no_buffer_space:
					return close_no_memory;
			}
		}
		else if (ec.category() == get_http_category())
		{
			return close_no_memory;
		}

		return close_no_reason;
	}
}

