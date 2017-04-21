/*

Copyright (c) 2017, Steven Siloti
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

#ifndef TORRENT_SESSION_LISTEN_SOCKET_HPP_INCLUDED
#define TORRENT_SESSION_LISTEN_SOCKET_HPP_INCLUDED

#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp" // for tcp::endpoint

namespace libtorrent { namespace aux {

	// abstract interface for a listen socket owned by session_impl
	// pointers to this type serve as a handle for the listen socket
	// use a separate abstract type to prohibit outside access to private fields of listen_socket_t
	// and because some users of these handles should not be coupled to session_impl
	struct TORRENT_EXTRA_EXPORT session_listen_socket
	{
		virtual address get_external_address() = 0;
		virtual tcp::endpoint get_local_endpoint() = 0;

		session_listen_socket() = default;

	protected:
		session_listen_socket(session_listen_socket const&) = default;
		session_listen_socket& operator=(session_listen_socket const&) = default;
		~session_listen_socket() = default;
	};

} }

#endif
