/*

Copyright (c) 2017, Steven Siloti
Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/listen_socket_handle.hpp"
#include "libtorrent/aux_/session_impl.hpp"

namespace lt::aux {

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
}
