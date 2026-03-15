/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_CURL_POOL_HPP_INCLUDED
#define TORRENT_CURL_POOL_HPP_INCLUDED
#include "libtorrent/config.hpp"

#if TORRENT_USE_CURL
#include <functional>
#include <string>
#include "libtorrent/aux_/curl.hpp"
#include "libtorrent/aux_/curl_boost_socket.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/aux_/intrusive_list.hpp"
#include "libtorrent/io_context.hpp"

namespace libtorrent::aux {
class curl_request;

class TORRENT_EXTRA_EXPORT curl_pool {
public:
	using executor_type = boost::asio::any_io_executor;

	curl_pool(const executor_type& executor);
	~curl_pool();

	// Triggers curl processing of socket event.
	// May call destructor of the "socket" parameter and other sockets before it returns.
	// May call completion handler for easy handles before it returns (which may delete or remove them).
	void socket_event(curl_boost_socket& socket, curl_cselect_t event);

	// Default argument CURL_SOCKET_TIMEOUT is used for timeouts. It can also be used to kickstart curl processing but
	// curl should already create timeouts of zero seconds when it wants to process something.
	// May call destructor of sockets before it returns
	// May call completion handler for easy handles before it returns (which may delete or remove them)
	void process_socket_action(curl_socket_t native_socket = CURL_SOCKET_TIMEOUT, curl_cselect_t event = curl_cselect_t::none);

	void add_request(curl_request&);
	void remove_request(curl_request&);

	void set_max_connections(int max_connections);
	void set_max_host_connections(long value);

	using completion_handler_t = std::function<void(curl_request&, CURLcode)>;
	// request is removed from the pool before completion handler is called, callee should not attempt to remove it.
	void set_completion_callback(completion_handler_t cb) { m_completion_handler = std::move(cb); }

	void on_request_timeout(curl_request& crequest);
	[[nodiscard]] executor_type get_executor() const noexcept { return m_executor; }
private:
	template<CURLMoption option, typename T>
	void setopt(T value);

	int set_timeout(long timeout_ms) noexcept;

	static int update_socket_shim(CURL* /* easy_handle */,
							curl_socket_t native_socket,
							int what,
							void* clientp,
							void* socketp);

	static int update_socket(curl_socket_t native_socket,
							bitmask<curl_poll_t> poll_mode,
							curl_pool* pool,
							curl_boost_socket* socket);

	[[nodiscard]] CURLM* handle() const noexcept { return m_curl_handle; }

	// May call completion handler for easy handles before it returns
	void process_completed_requests();

	curl_boost_socket& add_socket(std::unique_ptr<curl_boost_socket> socket) noexcept;
	void remove_socket(curl_boost_socket& socket) noexcept;

	// raw_ptr due to special destructor requirements
	CURLM* m_curl_handle;

	using request_list_t = unique_ptr_intrusive_list<curl_boost_socket>;
	request_list_t m_sockets;
	completion_handler_t m_completion_handler;
	deadline_timer m_timer;
	executor_type m_executor;
};
}
#endif //TORRENT_USE_CURL
#endif //TORRENT_CURL_POOL_HPP_INCLUDED
