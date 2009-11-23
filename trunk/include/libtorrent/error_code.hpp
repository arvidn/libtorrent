/*

Copyright (c) 2008, Arvid Norberg
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

#ifndef TORRENT_ERROR_CODE_HPP_INCLUDED
#define TORRENT_ERROR_CODE_HPP_INCLUDED

#include <boost/version.hpp>

#if BOOST_VERSION < 103500
#include <asio/error_code.hpp>
#else
#include <boost/system/error_code.hpp>
#endif

#include "libtorrent/config.hpp"

namespace libtorrent
{

	namespace errors
	{
		enum error_code_enum
		{
			no_error = 0,
			file_collision,
			failed_hash_check,
			torrent_is_no_dict,
			torrent_missing_info,
			torrent_info_no_dict,
			torrent_missing_piece_length,
			torrent_missing_name,
			torrent_invalid_name,
			torrent_invalid_length,
			torrent_file_parse_failed,
			torrent_missing_pieces,
			torrent_invalid_hashes,
			too_many_pieces_in_torrent,
			invalid_swarm_metadata,
			invalid_bencoding,
			no_files_in_torrent,
			invalid_escaped_string,
			session_is_closing,
			duplicate_torrent,
			invalid_torrent_handle,
			invalid_entry_type,
			missing_info_hash_in_uri,
			file_too_short,
			unsupported_url_protocol,
			url_parse_error,
			peer_sent_empty_piece,
			parse_failed,
			invalid_file_tag,
			missing_info_hash,
			mismatching_info_hash,
			invalid_hostname,
			invalid_port,
			port_blocked,
			expected_close_bracket_in_address,
			destructing_torrent,
			timed_out,
			upload_upload_connection,
			uninteresting_upload_peer,
			invalid_info_hash,
			torrent_paused,
			invalid_have,
			invalid_bitfield_size,
			too_many_requests_when_choked,
			invalid_piece,
			no_memory,
			torrent_aborted,
			self_connection,
			invalid_piece_size,
			timed_out_no_interest,
			timed_out_inactivity,
			timed_out_no_handshake,
			timed_out_no_request,
			invalid_choke,
			invalid_unchoke,
			invalid_interested,
			invalid_not_interested,
			invalid_request,
			invalid_hash_list,
			invalid_hash_piece,
			invalid_cancel,
			invalid_dht_port,
			invalid_suggest,
			invalid_have_all,
			invalid_have_none,
			invalid_reject,
			invalid_allow_fast,
			invalid_extended,
			invalid_message,
			sync_hash_not_found,
			invalid_encryption_constant,
			no_plaintext_mode,
			no_rc4_mode,
			unsupported_encryption_mode,
			unsupported_encryption_mode_selected,
			invalid_pad_size,
			invalid_encrypt_handshake,
			no_incoming_encrypted,
			no_incoming_regular,
			duplicate_peer_id,
			torrent_removed,
			packet_too_large,
			http_parse_error,
			http_error,
			missing_location,
			invalid_redirection,
			redirecting,
			invalid_range,
			no_content_length,
			banned_by_ip_filter,
			too_many_connections,
			peer_banned,
			stopping_torrent,
			too_many_corrupt_pieces,
			torrent_not_ready,
			peer_not_constructed,
			session_closing,
			optimistic_disconnect,
			torrent_finished,
			no_router,
			metadata_too_large,
			invalid_metadata_request,
			invalid_metadata_size,
			invalid_metadata_offset,
			invalid_metadata_message,
			pex_message_too_large,
			invalid_pex_message,
			invalid_lt_tracker_message,

// natpmp errors
			unsupported_protocol_version,
			natpmp_not_authorized,
			network_failure,
			no_resources,
			unsupported_opcode,

// fastresume errors
			missing_file_sizes,
			no_files_in_resume_data,
			missing_pieces,
			mismatching_number_of_files,
			mismatching_file_size,
			mismatching_file_timestamp,
			not_a_dictionary,
			invalid_blocks_per_piece,
			missing_slots,
			too_many_slots,
			invalid_slot_list,
			invalid_piece_index,
			pieces_need_reorder,
			reserved1,
			reserved2,
			reserved3,
			reserved4,
			reserved5,
			reserved6,
			reserved7,
			reserved8,

			no_i2p_router,

			error_code_max
		};
	}

#if BOOST_VERSION < 103500
	typedef asio::error_code error_code;
	inline asio::error::error_category get_posix_category() { return asio::error::system_category; }
	inline asio::error::error_category get_system_category() { return asio::error::system_category; }

	extern TORRENT_EXPORT asio::error::error_category libtorrent_category;
#else

	struct TORRENT_EXPORT libtorrent_error_category : boost::system::error_category
	{
		virtual const char* name() const;
		virtual std::string message(int ev) const;
		virtual boost::system::error_condition default_error_condition(int ev) const
		{ return boost::system::error_condition(ev, *this); }
	};

	extern TORRENT_EXPORT libtorrent_error_category libtorrent_category;

	using boost::system::error_code;
	inline boost::system::error_category const& get_system_category()
	{ return boost::system::get_system_category(); }
	inline boost::system::error_category const& get_posix_category()
#if BOOST_VERSION < 103600
	{ return boost::system::get_posix_category(); }
#else
	{ return boost::system::get_generic_category(); }
#endif
#endif

#ifndef BOOST_NO_EXCEPTIONS
	struct TORRENT_EXPORT libtorrent_exception: std::exception
	{
		libtorrent_exception(error_code const& s): m_error(s), m_msg(0) {}
		virtual const char* what() const throw()
		{
			if (!m_msg)
			{
				std::string msg = m_error.message();
				m_msg = strdup(msg.c_str());
			}

			return m_msg;
		}
		virtual ~libtorrent_exception() throw() { free(m_msg); }
		error_code error() const { return m_error; }
	private:
		error_code m_error;
		mutable char* m_msg;
	};
#endif
}

#endif

