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

#include "libtorrent/aux_/listen_socket_handle.hpp"
#include "libtorrent/aux_/session_impl.hpp"

namespace libtorrent { namespace aux {

	address listen_socket_handle::get_external_address() const
	{
		auto s = m_sock.lock();
		TORRENT_ASSERT(s);
		if (!s) throw_ex<std::bad_weak_ptr>();
		return s->external_address.external_address();
	}

	tcp::endpoint listen_socket_handle::get_local_endpoint() const
	{
		auto s = m_sock.lock();
		TORRENT_ASSERT(s);
		if (!s) throw_ex<std::bad_weak_ptr>();
		return s->local_endpoint;
	}

	bool listen_socket_handle::is_ssl() const
	{
		auto s = m_sock.lock();
		TORRENT_ASSERT(s);
		if (!s) throw_ex<std::bad_weak_ptr>();
		return s->ssl == transport::ssl;
	}

	listen_socket_t* listen_socket_handle::get() const
	{
		return m_sock.lock().get();
	}

	bool listen_socket_handle::can_route(address const& a) const
	{
		auto s = m_sock.lock();
		if (!s) return false;
		return s->can_route(a);
	}

} }
