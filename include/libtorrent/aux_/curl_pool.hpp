/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_CURL_POOL_HPP
#define LIBTORRENT_CURL_POOL_HPP
#include "libtorrent/config.hpp"

#if TORRENT_USE_CURL
#include <optional>
#include <string>
#include "libtorrent/io_context.hpp"
#include "libtorrent/aux_/curl.hpp"
#include "libtorrent/aux_/curl_boost_socket.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/aux_/intrusive_list.hpp"
#include "libtorrent/string_view.hpp"

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
	// Will return true, if the `socket` parameter is still alive.
	bool socket_event(curl_boost_socket& socket, curl_cselect_t event);

	void add_request(CURL*);
	void remove_request(CURL*);

	void set_max_connections(int max_connections);
	void set_max_host_connections(long value);

	using completion_handler_t = std::function<void(CURL*, CURLcode)>;
	// completion-handler is not allowed to call curl_pool recursively
	void set_completion_callback(completion_handler_t cb) { m_completion_handler = std::move(cb); }

	[[nodiscard]] executor_type get_executor() const noexcept { return m_executor; }
	[[nodiscard]] int count() const noexcept { return m_active_requests; }
private:
	template<typename T>
	using enable_if_no_string_t = std::enable_if_t<
		!std::is_same_v<std::decay_t<T>, std::string> &&
		!std::is_same_v<std::decay_t<T>, string_view>
	>;

	template<typename T, typename = enable_if_no_string_t<T>>
	void setopt(CURLMoption option, T value);

	int set_timeout(long timeout_ms) noexcept;

	// Default argument CURL_SOCKET_TIMEOUT can be used on timeouts but can also
	// be used to kickstart the processing of newly added handles
	// May call destructor of sockets before it returns
	// May call completion handler for easy handles before it returns (which may delete or remove them)
	void process_socket_action(curl_socket_t native_socket = CURL_SOCKET_TIMEOUT, curl_cselect_t event = curl_cselect_t::none);

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

	// May call completion handler for easy handles before it returns (which may delete or remove them)
	void process_completed_requests();

	curl_boost_socket& add_socket(std::unique_ptr<curl_boost_socket> socket) noexcept;
	void remove_socket(curl_boost_socket& socket) noexcept;

	// raw_ptr due to special destructor requirements
	CURLM* m_curl_handle;

	using request_list_t = unique_ptr_intrusive_list<curl_boost_socket>;
	request_list_t m_sockets;
	completion_handler_t m_completion_handler;
	curl_boost_socket* m_calling_socket = nullptr;
	deadline_timer m_timer;
	executor_type m_executor;
	int m_active_requests = 0;
	std::optional<int> m_cached_max_connections;
};
}
#endif //TORRENT_USE_CURL
#endif //LIBTORRENT_CURL_POOL_HPP
