/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_CURL_REQUEST_H
#define LIBTORRENT_CURL_REQUEST_H

#include "libtorrent/config.hpp"

#if TORRENT_USE_CURL
#include <functional>
#include <string>
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/curl.hpp"
#include "libtorrent/aux_/curl_basic_request.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/time.hpp"
#include <boost/beast/core/flat_buffer.hpp>

namespace libtorrent::aux {
class exploded_url;

// extends curl_basic_request with features not natively supported by curl
class TORRENT_EXTRA_EXPORT curl_request : public curl_basic_request {
public:
	explicit curl_request(std::size_t max_buffer_size, const io_context::executor_type& executor);
	~curl_request() = default;

	void set_defaults();

	[[nodiscard]] span<const char> data() const noexcept;
	[[nodiscard]] error_type get_error(CURLcode result) const;

	using filter_t = bool (*)(curl_request& request, const address& ip);
	void set_filter(filter_t filter) noexcept { m_filter = filter; }

	using redirect_t = error_code (*)(curl_request& request, const exploded_url& parts);
	void set_redirect_callback(redirect_t cb) noexcept { m_redirect_cb = cb; }

	using timeout_callback_t = std::function<void(curl_request& request)>;
	// callback is called async, request object can be deleted inside the callback
	void set_timeout_callback(timeout_callback_t cb) noexcept { m_timeout_callback = cb; }
	void set_timeout(seconds32 timeout) { m_timeout = timeout; }
	void cancel_timeout() { m_timer.cancel(); };

	// returns false on error
	bool prepare_to_follow_redirect();

	[[nodiscard]] bool is_redirect_response() const;
	void set_max_redirects(int value) { m_allowed_redirects = value; }

	template<typename T>
	T* get_userdata() const { return static_cast<T*>(m_userdata); }
	void set_userdata(void* value) { m_userdata = value; }

private:
	void start_timeout(seconds32 timeout);

	[[nodiscard]] error_type redirect_security_settings(const exploded_url& parts);

	[[nodiscard]] bool filter(const address& ep);

	static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

	static curl_socket_t opensocket(void* clientp,
									curlsocktype purpose,
									curl_sockaddr* addr);

	static curl_socket_t before_curl_request(void* clientp,
											char* conn_primary_ip,
											char* conn_local_ip,
											int conn_primary_port,
											int conn_local_port);

	static int before_resolving(void *resolver_state, void *reserved, void *userdata);

	timeout_callback_t m_timeout_callback = nullptr;
	filter_t m_filter = nullptr;
	redirect_t m_redirect_cb = nullptr;
	void* m_userdata = nullptr;

	boost::beast::flat_buffer m_read_buffer;
	deadline_timer m_timer;
	seconds32 m_timeout{};
	address m_filter_allowed;
	error_type m_error;

	int m_allowed_redirects = 0;
};
}

#endif //TORRENT_USE_CURL
#endif //LIBTORRENT_CURL_REQUEST_H
