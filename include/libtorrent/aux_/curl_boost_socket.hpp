/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_CURL_BOOST_SOCKET_HPP
#define LIBTORRENT_CURL_BOOST_SOCKET_HPP
#include "libtorrent/config.hpp"

#if TORRENT_USE_CURL
#include "libtorrent/aux_/bitmask.hpp"
#include "libtorrent/aux_/curl.hpp"
#include "libtorrent/aux_/intrusive_list.hpp"
#include "libtorrent/aux_/memory.hpp"
#include "libtorrent/error_code.hpp"

#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
#include <boost/asio/windows/stream_handle.hpp>
#else
#include <boost/asio/posix/stream_descriptor.hpp>
#endif

namespace libtorrent::aux {
class curl_pool;

class curl_boost_socket : public unique_ptr_intrusive_list_base<curl_boost_socket> {
public:
#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
	using socket_t = boost::asio::windows::stream_handle;
#else
	using socket_t = boost::asio::posix::stream_descriptor;
#endif

	curl_boost_socket(curl_pool& pool, socket_t&& socket)
		: m_pool(pool)
		, m_socket(std::move(socket))
	{
	}

	void set_poll_mode(bitmask<curl_poll_t> new_poll_mode);

	// releases the native_handle without closing it
	void release_handle() { m_socket.release(); }
	[[nodiscard]] curl_socket_t native_handle() { return m_socket.native_handle(); }

	[[nodiscard]] static std::unique_ptr<curl_boost_socket> wrap(curl_pool& pool, curl_socket_t native_socket, error_code& ec);
private:
	void subscribe_read();
	void subscribe_write();

	// allows async completion-handlers to notify pool through the "this" pointer
	curl_pool& m_pool;

	socket_t m_socket;
	bitmask<curl_poll_t> m_poll_mode = curl_poll_t::none;
};
}
#endif //TORRENT_USE_CURL
#endif //LIBTORRENT_CURL_BOOST_SOCKET_HPP
