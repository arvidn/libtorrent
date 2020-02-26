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

#ifndef TORRENT_OPERATIONS_HPP_INCLUDED
#define TORRENT_OPERATIONS_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include <cstdint>

namespace libtorrent {

	// these constants are used to identify the operation that failed, causing a
	// peer to disconnect
	enum class operation_t : std::uint8_t
	{
		// the error was unexpected and it is unknown which operation caused it
		unknown,

		// this is used when the bittorrent logic
		// determines to disconnect
		bittorrent,

		// a call to iocontrol failed
		iocontrol,

		// a call to ``getpeername()`` failed (querying the remote IP of a
		// connection)
		getpeername,

		// a call to getname failed (querying the local IP of a
		// connection)
		getname,

		// an attempt to allocate a receive buffer failed
		alloc_recvbuf,

		// an attempt to allocate a send buffer failed
		alloc_sndbuf,

		// writing to a file failed
		file_write,

		// reading from a file failed
		file_read,

		// a non-read and non-write file operation failed
		file,

		// a socket write operation failed
		sock_write,

		// a socket read operation failed
		sock_read,

		// a call to open(), to create a socket socket failed
		sock_open,

		// a call to bind() on a socket failed
		sock_bind,

		// an attempt to query the number of bytes available to read from a socket
		// failed
		available,

		// a call related to bittorrent protocol encryption failed
		encryption,

		// an attempt to connect a socket failed
		connect,

		// establishing an SSL connection failed
		ssl_handshake,

		// a connection failed to satisfy the bind interface setting
		get_interface,

		// a call to listen() on a socket
		sock_listen,

		// a call to the ioctl to bind a socket to a specific network device or
		// adapter
		sock_bind_to_device,

		// a call to accept() on a socket
		sock_accept,

		// convert a string into a valid network address
		parse_address,

		// enumeration network devices or adapters
		enum_if,

		// invoking stat() on a file
		file_stat,

		// copying a file
		file_copy,

		// allocating storage for a file
		file_fallocate,

		// creating a hard link
		file_hard_link,

		// removing a file
		file_remove,

		// renaming a file
		file_rename,

		// opening a file
		file_open,

		// creating a directory
		mkdir,

		// check fast resume data against files on disk
		check_resume,

		// an unknown exception
		exception,

		// allocate space for a piece in the cache
		alloc_cache_piece,

		// move a part-file
		partfile_move,

		// read from a part file
		partfile_read,

		// write to a part-file
		partfile_write,

		// a hostname lookup
		hostname_lookup,

		// create or read a symlink
		symlink,

		// handshake with a peer or server
		handshake,

		// set socket option
		sock_option,

		// enumeration network routes
		enum_route,
	};

	// maps an operation id (from peer_error_alert and peer_disconnected_alert)
	// to its name. See operation_t for the constants
	TORRENT_EXPORT char const* operation_name(operation_t op);

#if TORRENT_ABI_VERSION == 1
	enum deprecated_operation_t : std::uint8_t
	{
		// the error was unexpected and it is unknown which operation caused it
		op_unknown TORRENT_DEPRECATED_ENUM,

		// this is used when the bittorrent logic
		// determines to disconnect
		op_bittorrent TORRENT_DEPRECATED_ENUM ,

		// a call to ``iocontrol()`` failed
		op_iocontrol TORRENT_DEPRECATED_ENUM,

		// a call to ``getpeername()`` failed (querying the remote IP of a
		// connection)
		op_getpeername TORRENT_DEPRECATED_ENUM,

		// a call to ``getsockname()`` failed (querying the local IP of a
		// connection)
		op_getname TORRENT_DEPRECATED_ENUM,

		// an attempt to allocate a receive buffer failed
		op_alloc_recvbuf TORRENT_DEPRECATED_ENUM,

		// an attempt to allocate a send buffer failed
		op_alloc_sndbuf TORRENT_DEPRECATED_ENUM,

		// writing to a file failed
		op_file_write TORRENT_DEPRECATED_ENUM,

		// reading from a file failed
		op_file_read TORRENT_DEPRECATED_ENUM,

		// a non-read and non-write file operation failed
		op_file TORRENT_DEPRECATED_ENUM,

		// a socket write operation failed
		op_sock_write TORRENT_DEPRECATED_ENUM,

		// a socket read operation failed
		op_sock_read TORRENT_DEPRECATED_ENUM,

		// a call to open(), to create a socket socket failed
		op_sock_open TORRENT_DEPRECATED_ENUM,

		// a call to bind() on a socket failed
		op_sock_bind TORRENT_DEPRECATED_ENUM,

		// an attempt to query the number of bytes available to read from a socket
		// failed
		op_available TORRENT_DEPRECATED_ENUM,

		// a call related to bittorrent protocol encryption failed
		op_encryption TORRENT_DEPRECATED_ENUM,

		// an attempt to connect a socket failed
		op_connect TORRENT_DEPRECATED_ENUM,

		// establishing an SSL connection failed
		op_ssl_handshake TORRENT_DEPRECATED_ENUM,

		// a connection failed to satisfy the bind interface setting
		op_get_interface TORRENT_DEPRECATED_ENUM,
	};
#endif

}

#endif // TORRENT_OPERATIONS_HPP_INCLUDED
