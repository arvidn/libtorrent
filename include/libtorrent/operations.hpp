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

namespace libtorrent
{
	// these constants are used to identify the operation that failed, causing a
	// peer to disconnect
	enum operation_t
	{
		// this is used when the bittorrent logic
		// determines to disconnect
		op_bittorrent = 0,

		// a call to iocontrol failed
		op_iocontrol,

		// a call to getpeername failed (querying the remote IP of a
		// connection)
		op_getpeername,

		// a call to getname failed (querying the local IP of a
		// connection)
		op_getname,

		// an attempt to allocate a receive buffer failed
		op_alloc_recvbuf,

		// an attempt to allocate a send buffer failed
		op_alloc_sndbuf,

		// writing to a file failed
		op_file_write,

		// reading from a file failed
		op_file_read,

		// a non-read and non-write file operation failed
		op_file,

		// a socket write operation failed
		op_sock_write,

		// a socket read operation failed
		op_sock_read,

		// a call to open(), to create a socket socket failed
		op_sock_open,

		// a call to bind() on a socket failed
		op_sock_bind,

		// an attempt to query the number of bytes available to read from a socket
		// failed
		op_available,

		// a call related to bittorrent protocol encryption failed
		op_encryption,

		// an attempt to connect a socket failed
		op_connect,

		// establishing an SSL connection failed
		op_ssl_handshake,

		// a connection failed to satisfy the bind interface setting
		op_get_interface
	};

}

#endif // TORRENT_OPERATIONS_HPP_INCLUDED

