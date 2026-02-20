/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/curl_boost_socket.hpp"

#if TORRENT_USE_CURL
#include "libtorrent/aux_/curl_pool.hpp"
#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent::aux {

void curl_boost_socket::subscribe_read()
{
	ADD_OUTSTANDING_ASYNC("curl_boost_socket::wait_read");
	m_socket.async_wait(socket_t::wait_read, [socket = this](const error_code& ec) {
		COMPLETE_ASYNC("curl_boost_socket::wait_read");
		// note: canceled async-handlers should not do anything, e.g. "socket" pointer can be dangling
		if (ec == error::operation_aborted)
			return;

		// curl does not specify how to send curl_cselect_t::err, e.g. (in | err) or just (err) by itself.
		// It does not matter as the parameter is completely ignored by curl internally.
		const bool alive = socket->m_pool.socket_event(*socket, ec ? curl_cselect_t::err : curl_cselect_t::in);
		if (!alive)
			return;
		// The alive check is a bit ugly, but it allows delaying subscribe_read() until the new poll mode is known.
		// Without knowing whether the socket is alive, it would be required to call subscribe_read() before socket_event().

		// curl doesn't handle all errors for sockets, we must assume it only waits for a timeout (e.g. the error could
		// be part of boost and not related to the socket at all). If the socket keeps subscribing on a bad
		// file descriptor, asio will keep executing the async completion-handler immediately to return the error. This
		// will loop excessively until the timeout.
		// When there is an unrecoverable error, stop subscribing. Subscribing to a file descriptor does not have
		// any real transient errors that warrant retrying with the risk of an infinite loop.
		if (!ec)
		{
			// Curl expects the socket to stay in the same polling mode until it calls set_poll_mode() to change it.
			// note that socket_event() may have changed the polling mode
			if (socket->m_poll_mode.test(curl_poll_t::in))
				socket->subscribe_read();
		}
	});
}

void curl_boost_socket::subscribe_write()
{
	ADD_OUTSTANDING_ASYNC("curl_boost_socket::wait_write");
	m_socket.async_wait(socket_t::wait_write, [socket = this](const error_code& ec) {
		COMPLETE_ASYNC("curl_boost_socket::wait_write");
		if (ec == error::operation_aborted)
			return;

		const bool alive = socket->m_pool.socket_event(*socket, ec ? curl_cselect_t::err : curl_cselect_t::out);
		if (!alive)
			return;

		if (!ec)
		{
			if (socket->m_poll_mode.test(curl_poll_t::out))
				socket->subscribe_write();
		}
	});
}

void curl_boost_socket::set_poll_mode(bitmask<curl_poll_t> new_poll_mode)
{
	if (new_poll_mode == m_poll_mode)
		return;

	// if poll mode has bits set for wait-operations that are no longer required, reset all async operations
	const auto unwanted_bits = ~new_poll_mode;
	if (m_poll_mode.test(unwanted_bits))
	{
		m_socket.cancel();
		m_poll_mode = curl_poll_t::none;
	}

	const bool need_read_event = new_poll_mode.test(curl_poll_t::in);
	const bool have_read_event = m_poll_mode.test(curl_poll_t::in);
	if (need_read_event && !have_read_event)
		subscribe_read();

	const bool need_write_event = new_poll_mode.test(curl_poll_t::out);
	const bool have_write_event = m_poll_mode.test(curl_poll_t::out);
	if (need_write_event && !have_write_event)
		subscribe_write();

	m_poll_mode = new_poll_mode;
}

std::unique_ptr<curl_boost_socket> curl_boost_socket::wrap(curl_pool& pool, curl_socket_t native_socket, error_code& ec)
{
	auto asio_socket = socket_t(pool.get_executor());
	asio_socket.assign(native_socket, ec);
	// return object regardless of (ec), let caller handle it
	return std::make_unique<curl_boost_socket>(pool, std::move(asio_socket));
}
}
#endif //TORRENT_USE_CURL
