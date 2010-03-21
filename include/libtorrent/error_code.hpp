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
#include <boost/shared_ptr.hpp>

#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
// asio assumes that the windows error codes are defined already
#include <winsock2.h>
#endif

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
			reserved,
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
			reserved108,
			reserved109,
			reserved110,
			reserved111,
			reserved112,
			reserved113,
			reserved114,
			reserved115,
			reserved116,
			reserved117,
			reserved118,
			reserved119,

// natpmp errors
			unsupported_protocol_version, // 120
			natpmp_not_authorized,
			network_failure,
			no_resources,
			unsupported_opcode,
			reserved125,
			reserved126,
			reserved127,
			reserved128,
			reserved129,

// fastresume errors
			missing_file_sizes, // 130
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
			reserved143,
			reserved144,
			reserved145,
			reserved146,
			reserved147,
			reserved148,
			reserved149,
// HTTP errors
			http_parse_error, // 150
			http_missing_location,
			http_failed_decompress,
			reserved153,
			reserved154,
			reserved155,
			reserved156,
			reserved157,
			reserved158,
			reserved159,

			error_code_max
		};
	}
}

#if BOOST_VERSION >= 103500

namespace boost { namespace system {

	template<> struct is_error_code_enum<libtorrent::errors::error_code_enum>
	{ static const bool value = true; };

	template<> struct is_error_condition_enum<libtorrent::errors::error_code_enum>
	{ static const bool value = true; };
} }

#endif

namespace libtorrent
{

#if BOOST_VERSION < 103500
	typedef asio::error_code error_code;
	inline asio::error::error_category get_posix_category() { return asio::error::system_category; }
	inline asio::error::error_category get_system_category() { return asio::error::system_category; }

	boost::system::error_category const& get_libtorrent_category()
	{
		static ::asio::error::error_category libtorrent_category(20);
		return libtorrent_category;
	}

#else

	struct TORRENT_EXPORT libtorrent_error_category : boost::system::error_category
	{
		virtual const char* name() const;
		virtual std::string message(int ev) const;
		virtual boost::system::error_condition default_error_condition(int ev) const
		{ return boost::system::error_condition(ev, *this); }
	};

	inline boost::system::error_category& get_libtorrent_category()
	{
		static libtorrent_error_category libtorrent_category;
		return libtorrent_category;
	}

	namespace errors
	{
		inline boost::system::error_code make_error_code(error_code_enum e)
		{
			return boost::system::error_code(e, get_libtorrent_category());
		}
	}

	using boost::system::error_code;
	inline boost::system::error_category const& get_system_category()
	{ return boost::system::get_system_category(); }
	inline boost::system::error_category const& get_posix_category()
#if BOOST_VERSION < 103600
	{ return boost::system::get_posix_category(); }
#else
	{ return boost::system::get_generic_category(); }
#endif // BOOST_VERSION < 103600
#endif // BOOST_VERSION < 103500

#ifndef BOOST_NO_EXCEPTIONS
	struct TORRENT_EXPORT libtorrent_exception: std::exception
	{
		libtorrent_exception(error_code const& s): m_error(s) {}
		virtual const char* what() const throw()
		{
			if (!m_msg) m_msg.reset(new std::string(m_error.message()));
			return m_msg->c_str();
		}
		virtual ~libtorrent_exception() throw() {}
		error_code error() const { return m_error; }
	private:
		error_code m_error;
		mutable boost::shared_ptr<std::string> m_msg;
	};
#endif
}

#endif

