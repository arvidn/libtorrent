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

#ifndef TORRENT_LISTEN_SOCKET_HANDLE_HPP_INCLUDED
#define TORRENT_LISTEN_SOCKET_HANDLE_HPP_INCLUDED

#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp" // for tcp::endpoint
#include <memory>

namespace libtorrent { namespace aux {

	struct listen_socket_t;

	struct TORRENT_EXTRA_EXPORT listen_socket_handle
	{
		friend struct session_impl;

		listen_socket_handle() = default;

		listen_socket_handle(std::shared_ptr<listen_socket_t> s) // NOLINT
			: m_sock(s)
		{}

		listen_socket_handle(listen_socket_handle const& o) = default;
		listen_socket_handle& operator=(listen_socket_handle const& o) = default;

		explicit operator bool() const { return !m_sock.expired(); }

		address get_external_address() const;
		tcp::endpoint get_local_endpoint() const;
		bool can_route(address const&) const;

		bool is_ssl() const;

		bool operator==(listen_socket_handle const& o) const
		{
			return !m_sock.owner_before(o.m_sock) && !o.m_sock.owner_before(m_sock);
		}

		bool operator!=(listen_socket_handle const& o) const
		{
			return !(*this == o);
		}

		bool operator<(listen_socket_handle const& o) const
		{ return m_sock.owner_before(o.m_sock); }

		listen_socket_t* get() const;

		std::weak_ptr<listen_socket_t> get_ptr() const { return m_sock; }

	private:
		std::weak_ptr<listen_socket_t> m_sock;
	};

} }

#endif
