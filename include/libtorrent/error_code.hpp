/*

Copyright (c) 2008-2018, Arvid Norberg
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
#include "libtorrent/config.hpp"
#include "libtorrent/string_util.hpp" // for allocate_string_copy
#include <stdlib.h> // free

#include "libtorrent/aux_/disable_warnings_push.hpp"

#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
// asio assumes that the windows error codes are defined already
#include <winsock2.h>
#endif

#include <boost/system/error_code.hpp>
#include <boost/asio/error.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#ifndef BOOST_SYSTEM_NOEXCEPT
#define BOOST_SYSTEM_NOEXCEPT TORRENT_EXCEPTION_THROW_SPECIFIER
#endif

namespace libtorrent
{

	namespace errors
	{
		// libtorrent uses boost.system's ``error_code`` class to represent
		// errors. libtorrent has its own error category
		// libtorrent_category() with the error codes defined by
		// error_code_enum.
		enum error_code_enum
		{
			// Not an error
			no_error = 0,
			// Two torrents has files which end up overwriting each other
			file_collision,
			// A piece did not match its piece hash
			failed_hash_check,
			// The .torrent file does not contain a bencoded dictionary at
			// its top level
			torrent_is_no_dict,
			// The .torrent file does not have an ``info`` dictionary
			torrent_missing_info,
			// The .torrent file's ``info`` entry is not a dictionary
			torrent_info_no_dict,
			// The .torrent file does not have a ``piece length`` entry
			torrent_missing_piece_length,
			// The .torrent file does not have a ``name`` entry
			torrent_missing_name,
			// The .torrent file's name entry is invalid
			torrent_invalid_name,
			// The length of a file, or of the whole .torrent file is invalid.
			// Either negative or not an integer
			torrent_invalid_length,
			// Failed to parse a file entry in the .torrent
			torrent_file_parse_failed,
			// The ``pieces`` field is missing or invalid in the .torrent file
			torrent_missing_pieces,
			// The ``pieces`` string has incorrect length
			torrent_invalid_hashes,
			// The .torrent file has more pieces than is supported by libtorrent
			too_many_pieces_in_torrent,
			// The metadata (.torrent file) that was received from the swarm
			// matched the info-hash, but failed to be parsed
			invalid_swarm_metadata,
			// The file or buffer is not correctly bencoded
			invalid_bencoding,
			// The .torrent file does not contain any files
			no_files_in_torrent,
			// The string was not properly url-encoded as expected
			invalid_escaped_string,
			// Operation is not permitted since the session is shutting down
			session_is_closing,
			// There's already a torrent with that info-hash added to the
			// session
			duplicate_torrent,
			// The supplied torrent_handle is not referring to a valid torrent
			invalid_torrent_handle,
			// The type requested from the entry did not match its type
			invalid_entry_type,
			// The specified URI does not contain a valid info-hash
			missing_info_hash_in_uri,
			// One of the files in the torrent was unexpectedly small. This
			// might be caused by files being changed by an external process
			file_too_short,
			// The URL used an unknown protocol. Currently ``http`` and
			// ``https`` (if built with openssl support) are recognized. For
			// trackers ``udp`` is recognized as well.
			unsupported_url_protocol,
			// The URL did not conform to URL syntax and failed to be parsed
			url_parse_error,
			// The peer sent a 'piece' message of length 0
			peer_sent_empty_piece,
			// A bencoded structure was corrupt and failed to be parsed
			parse_failed,
			// The fast resume file was missing or had an invalid file version
			// tag
			invalid_file_tag,
			// The fast resume file was missing or had an invalid info-hash
			missing_info_hash,
			// The info-hash did not match the torrent
			mismatching_info_hash,
			// The URL contained an invalid hostname
			invalid_hostname,
			// The URL had an invalid port
			invalid_port,
			// The port is blocked by the port-filter, and prevented the
			// connection
			port_blocked,
			// The IPv6 address was expected to end with ']'
			expected_close_bracket_in_address,
			// The torrent is being destructed, preventing the operation to
			// succeed
			destructing_torrent,
			// The connection timed out
			timed_out,
			// The peer is upload only, and we are upload only. There's no point
			// in keeping the connection
			upload_upload_connection,
			// The peer is upload only, and we're not interested in it. There's
			// no point in keeping the connection
			uninteresting_upload_peer,
			// The peer sent an unknown info-hash
			invalid_info_hash,
			// The torrent is paused, preventing the operation from succeeding
			torrent_paused,
			// The peer sent an invalid have message, either wrong size or
			// referring to a piece that doesn't exist in the torrent
			invalid_have,
			// The bitfield message had the incorrect size
			invalid_bitfield_size,
			// The peer kept requesting pieces after it was choked, possible
			// abuse attempt.
			too_many_requests_when_choked,
			// The peer sent a piece message that does not correspond to a
			// piece request sent by the client
			invalid_piece,
			// memory allocation failed
			no_memory,
			// The torrent is aborted, preventing the operation to succeed
			torrent_aborted,
			// The peer is a connection to ourself, no point in keeping it
			self_connection,
			// The peer sent a piece message with invalid size, either negative
			// or greater than one block
			invalid_piece_size,
			// The peer has not been interesting or interested in us for too
			// long, no point in keeping it around
			timed_out_no_interest,
			// The peer has not said anything in a long time, possibly dead
			timed_out_inactivity,
			// The peer did not send a handshake within a reasonable amount of
			// time, it might not be a bittorrent peer
			timed_out_no_handshake,
			// The peer has been unchoked for too long without requesting any
			// data. It might be lying about its interest in us
			timed_out_no_request,
			// The peer sent an invalid choke message
			invalid_choke,
			// The peer send an invalid unchoke message
			invalid_unchoke,
			// The peer sent an invalid interested message
			invalid_interested,
			// The peer sent an invalid not-interested message
			invalid_not_interested,
			// The peer sent an invalid piece request message
			invalid_request,
			// The peer sent an invalid hash-list message (this is part of the
			// merkle-torrent extension)
			invalid_hash_list,
			// The peer sent an invalid hash-piece message (this is part of the
			// merkle-torrent extension)
			invalid_hash_piece,
			// The peer sent an invalid cancel message
			invalid_cancel,
			// The peer sent an invalid DHT port-message
			invalid_dht_port,
			// The peer sent an invalid suggest piece-message
			invalid_suggest,
			// The peer sent an invalid have all-message
			invalid_have_all,
			// The peer sent an invalid have none-message
			invalid_have_none,
			// The peer sent an invalid reject message
			invalid_reject,
			// The peer sent an invalid allow fast-message
			invalid_allow_fast,
			// The peer sent an invalid extension message ID
			invalid_extended,
			// The peer sent an invalid message ID
			invalid_message,
			// The synchronization hash was not found in the encrypted handshake
			sync_hash_not_found,
			// The encryption constant in the handshake is invalid
			invalid_encryption_constant,
			// The peer does not support plaintext, which is the selected mode
			no_plaintext_mode,
			// The peer does not support rc4, which is the selected mode
			no_rc4_mode,
			// The peer does not support any of the encryption modes that the
			// client supports
			unsupported_encryption_mode,
			// The peer selected an encryption mode that the client did not
			// advertise and does not support
			unsupported_encryption_mode_selected,
			// The pad size used in the encryption handshake is of invalid size
			invalid_pad_size,
			// The encryption handshake is invalid
			invalid_encrypt_handshake,
			// The client is set to not support incoming encrypted connections
			// and this is an encrypted connection
			no_incoming_encrypted,
			// The client is set to not support incoming regular bittorrent
			// connections, and this is a regular connection
			no_incoming_regular,
			// The client is already connected to this peer-ID
			duplicate_peer_id,
			// Torrent was removed
			torrent_removed,
			// The packet size exceeded the upper sanity check-limit
			packet_too_large,

			reserved,

			// The web server responded with an error
			http_error,
			// The web server response is missing a location header
			missing_location,
			// The web seed redirected to a path that no longer matches the
			// .torrent directory structure
			invalid_redirection,
			// The connection was closed because it redirected to a different
			// URL
			redirecting,
			// The HTTP range header is invalid
			invalid_range,
			// The HTTP response did not have a content length
			no_content_length,
			// The IP is blocked by the IP filter
			banned_by_ip_filter,
			// At the connection limit
			too_many_connections,
			// The peer is marked as banned
			peer_banned,
			// The torrent is stopping, causing the operation to fail
			stopping_torrent,
			// The peer has sent too many corrupt pieces and is banned
			too_many_corrupt_pieces,
			// The torrent is not ready to receive peers
			torrent_not_ready,
			// The peer is not completely constructed yet
			peer_not_constructed,
			// The session is closing, causing the operation to fail
			session_closing,
			// The peer was disconnected in order to leave room for a
			// potentially better peer
			optimistic_disconnect,
			// The torrent is finished
			torrent_finished,
			// No UPnP router found
			no_router,
			// The metadata message says the metadata exceeds the limit
			metadata_too_large,
			// The peer sent an invalid metadata request message
			invalid_metadata_request,
			// The peer advertised an invalid metadata size
			invalid_metadata_size,
			// The peer sent a message with an invalid metadata offset
			invalid_metadata_offset,
			// The peer sent an invalid metadata message
			invalid_metadata_message,
			// The peer sent a peer exchange message that was too large
			pex_message_too_large,
			// The peer sent an invalid peer exchange message
			invalid_pex_message,
			// The peer sent an invalid tracker exchange message
			invalid_lt_tracker_message,
			// The peer sent an pex messages too often. This is a possible
			// attempt of and attack
			too_frequent_pex,
			// The operation failed because it requires the torrent to have
			// the metadata (.torrent file) and it doesn't have it yet.
			// This happens for magnet links before they have downloaded the
			// metadata, and also torrents added by URL.
			no_metadata,
			// The peer sent an invalid ``dont_have`` message. The don't have
			// message is an extension to allow peers to advertise that the
			// no longer has a piece they previously had.
			invalid_dont_have,
			// The peer tried to connect to an SSL torrent without connecting
			// over SSL.
			requires_ssl_connection,
			// The peer tried to connect to a torrent with a certificate
			// for a different torrent.
			invalid_ssl_cert,
			// the torrent is not an SSL torrent, and the operation requires
			// an SSL torrent
			not_an_ssl_torrent,
			// peer was banned because its listen port is within a banned port
			// range, as specified by the port_filter.
			banned_by_port_filter,


			// The NAT-PMP router responded with an unsupported protocol version
			unsupported_protocol_version = 120,
			// You are not authorized to map ports on this NAT-PMP router
			natpmp_not_authorized,
			// The NAT-PMP router failed because of a network failure
			network_failure,
			// The NAT-PMP router failed because of lack of resources
			no_resources,
			// The NAT-PMP router failed because an unsupported opcode was sent
			unsupported_opcode,



			// The resume data file is missing the 'file sizes' entry
			missing_file_sizes = 130,
			// The resume data file 'file sizes' entry is empty
			no_files_in_resume_data,
			// The resume data file is missing the 'pieces' and 'slots' entry
			missing_pieces,
			// The number of files in the resume data does not match the number
			// of files in the torrent
			mismatching_number_of_files,
			// One of the files on disk has a different size than in the fast
			// resume file
			mismatching_file_size,
			// One of the files on disk has a different timestamp than in the
			// fast resume file
			mismatching_file_timestamp,
			// The resume data file is not a dictionary
			not_a_dictionary,
			// The 'blocks per piece' entry is invalid in the resume data file
			invalid_blocks_per_piece,
			// The resume file is missing the 'slots' entry, which is required
			// for torrents with compact allocation. *DEPRECATED*
			missing_slots,
			// The resume file contains more slots than the torrent
			too_many_slots,
			// The 'slot' entry is invalid in the resume data
			invalid_slot_list,
			// One index in the 'slot' list is invalid
			invalid_piece_index,
			// The pieces on disk needs to be re-ordered for the specified
			// allocation mode. This happens if you specify sparse allocation
			// and the files on disk are using compact storage. The pieces needs
			// to be moved to their right position. *DEPRECATED*
			pieces_need_reorder,
			// this error is returned when asking to save resume data and
			// specifying the flag to only save when there's anything new to save
			// (torrent_handle::only_if_modified) and there wasn't anything changed.
			resume_data_not_modified,



			// The HTTP header was not correctly formatted
			http_parse_error = 150,
			// The HTTP response was in the 300-399 range but lacked a location
			// header
			http_missing_location,
			// The HTTP response was encoded with gzip or deflate but
			// decompressing it failed
			http_failed_decompress,



			// The URL specified an i2p address, but no i2p router is configured
			no_i2p_router = 160,
			// i2p acceptor is not available yet, can't announce without endpoint
			no_i2p_endpoint = 161,



			// The tracker URL doesn't support transforming it into a scrape
			// URL. i.e. it doesn't contain "announce.
			scrape_not_available = 170,
			// invalid tracker response
			invalid_tracker_response,
			// invalid peer dictionary entry. Not a dictionary
			invalid_peer_dict,
			// tracker sent a failure message
			tracker_failure,
			// missing or invalid 'files' entry
			invalid_files_entry,
			// missing or invalid 'hash' entry
			invalid_hash_entry,
			// missing or invalid 'peers' and 'peers6' entry
			invalid_peers_entry,
			// udp tracker response packet has invalid size
			invalid_tracker_response_length,
			// invalid transaction id in udp tracker response
			invalid_tracker_transaction_id,
			// invalid action field in udp tracker response
			invalid_tracker_action,

#ifndef TORRENT_NO_DEPRECATE
			// expected string in bencoded string
			expected_string = 190,
			// expected colon in bencoded string
			expected_colon,
			// unexpected end of file in bencoded string
			unexpected_eof,
			// expected value (list, dict, int or string) in bencoded string
			expected_value,
			// bencoded recursion depth limit exceeded
			depth_exceeded,
			// bencoded item count limit exceeded
			limit_exceeded,
			// integer overflow
			overflow,
#endif

			// the number of error codes
			error_code_max
		};

		// HTTP errors are reported in the libtorrent::http_category, with error code enums in
		// the ``libtorrent::errors`` namespace.
		enum http_errors
		{
			cont = 100,
			ok = 200,
			created = 201,
			accepted = 202,
			no_content = 204,
			multiple_choices = 300,
			moved_permanently = 301,
			moved_temporarily = 302,
			not_modified = 304,
			bad_request = 400,
			unauthorized = 401,
			forbidden = 403,
			not_found = 404,
			internal_server_error = 500,
			not_implemented = 501,
			bad_gateway = 502,
			service_unavailable = 503
		};

		// hidden
		TORRENT_EXPORT boost::system::error_code make_error_code(error_code_enum e);

	} // namespace errors

	// return the instance of the libtorrent_error_category which
	// maps libtorrent error codes to human readable error messages.
	TORRENT_EXPORT boost::system::error_category& libtorrent_category();

	// returns the error_category for HTTP errors
	TORRENT_EXPORT boost::system::error_category& http_category();

	using boost::system::error_code;
	using boost::system::error_condition;

	// internal
	using boost::system::generic_category;
	using boost::system::system_category;

#ifndef TORRENT_NO_DEPRECATE
	TORRENT_DEPRECATED TORRENT_EXPORT
	boost::system::error_category& get_libtorrent_category();
	TORRENT_DEPRECATED TORRENT_EXPORT
	boost::system::error_category& get_http_category();
#endif

#ifndef BOOST_NO_EXCEPTIONS
	struct TORRENT_EXPORT libtorrent_exception : std::exception
	{
		libtorrent_exception(error_code const& s): m_error(s), m_msg(0) {}
		virtual const char* what() const TORRENT_EXCEPTION_THROW_SPECIFIER;
		virtual ~libtorrent_exception() TORRENT_EXCEPTION_THROW_SPECIFIER;
#if __cplusplus >= 201103L
		libtorrent_exception(libtorrent_exception const&) = default;
		libtorrent_exception& operator=(libtorrent_exception const&) = default;
#endif
		error_code error() const { return m_error; }
	private:
		error_code m_error;
		mutable char* m_msg;
	};
#endif

	// used by storage to return errors
	// also includes which underlying file the
	// error happened on
	struct TORRENT_EXPORT storage_error
	{
		storage_error(): file(-1), operation(0) {}
		storage_error(error_code e): ec(e), file(-1), operation(0) {}

		operator bool() const { return ec.value() != 0; }

		// the error that occurred
		error_code ec;

		// the file the error occurred on
		boost::int32_t file:24;

		// A code from file_operation_t enum, indicating what
		// kind of operation failed.
		boost::uint32_t operation:8;

		enum file_operation_t {
			none,
			stat,
			mkdir,
			open,
			rename,
			remove,
			copy,
			read,
			write,
			fallocate,
			alloc_cache_piece,
			partfile_move,
			partfile_read,
			partfile_write,
			check_resume,
			hard_link
		};

		// Returns a string literal representing the file operation
		// that failed. If there were no failure, it returns
		// an empty string.
		char const* operation_str() const
		{
			static char const* ops[] =
			{
				"", "stat", "mkdir", "open", "rename", "remove", "copy"
				, "read", "write", "fallocate", "allocate cache piece"
				, "partfile move", "partfile read", "partfile write"
				, "check resume", "hard_link"
			};
			return ops[operation];
		}
	};

}

namespace boost { namespace system {

	template<> struct is_error_code_enum<libtorrent::errors::error_code_enum>
	{ static const bool value = true; };

	template<> struct is_error_code_enum<libtorrent::errors::http_errors>
	{ static const bool value = true; };
} }

#endif

