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
#include <string>
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/curl.hpp"
#include "libtorrent/aux_/memory.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/operations.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/time.hpp"
#include <boost/beast/core/flat_buffer.hpp>

namespace libtorrent {
struct ip_filter;
}

namespace libtorrent::aux {
class curl_pool;
struct proxy_settings;

class TORRENT_EXTRA_EXPORT curl_request {
public:
	struct error_type {
		error_code ec;
		operation_t op = operation_t::unknown;
		std::string message;

		constexpr explicit operator bool() const noexcept { return static_cast<bool>(ec); }
	};

	explicit curl_request(std::size_t max_buffer_size);
	~curl_request() = default;

	void set_defaults();

	template<typename T>
	[[nodiscard]] static T* from_handle(CURL* easy_handle)
	{
		T* ptr = nullptr;
		const auto error = curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &ptr);
		if (error)
			throw_ex<curl_easy_error>(error, "curl_easy_getinfo (CURLOPT_PRIVATE)");
		return ptr;
	}

	curl_request(curl_request const&) = delete;
	curl_request& operator=(curl_request const&) = delete;

	[[nodiscard]] CURL* handle() const noexcept { return m_curl_handle.get(); }

	[[nodiscard]] span<const char> data() const noexcept;
	[[nodiscard]] errors::http_errors http_status() const;
	[[nodiscard]] address get_ip(error_code& ec) const;
	[[nodiscard]] error_type get_error(CURLcode result) const;

	bool bind(const std::string& device, const address& local_address);
	void set_proxy(const proxy_settings& ps, bool verify_ssl);
	void set_ip_filter(std::shared_ptr<const ip_filter> filter) noexcept { m_ip_filter = std::move(filter); }
	void set_ssrf_mitigation(bool enabled) noexcept { m_ssrf_mitigation = enabled; }

	void set_user_agent(const std::string& s);
	void set_url(const std::string& s);
	void set_private_data(void* obj);
	void set_ipresolve(long option);

	void set_timeout(seconds32 timeout);

	void set_ssl_verify_host(bool onoff);
	void set_ssl_verify_peer(bool onoff);

	void set_pipewait(bool onoff);
	void set_write_callback(curl_write_callback option);

#if TORRENT_ABI_VERSION == 1
	void set_userpwd(const std::string& s);
#endif

	[[nodiscard]] long        get_num_connects()         const;
	[[nodiscard]] std::size_t get_header_size()          const;
	[[nodiscard]] std::size_t get_compressed_body_size() const;

	// includes bytes from redirection requests
	[[nodiscard]] std::size_t get_request_size()         const;
private:
	void set_debug_logging(bool onoff);
	[[nodiscard]] char* get_url() const;

	template<CURLoption option>
	void setopt(bool value);

	template<CURLoption option, typename T>
	void setopt(const T& value);

	template<typename T>
	using enable_if_no_string_t = std::enable_if_t<
		!std::is_same_v<std::decay_t<T>, std::string> &&
		!std::is_same_v<std::decay_t<T>, string_view>
	>;

	template<typename T, CURLINFO option, typename = enable_if_no_string_t<T>>
	[[nodiscard]] T getopt() const;

	[[nodiscard]] bool allowed_by_ip_filter(const address& ip);
	[[nodiscard]] bool allowed_by_ssrf(const address& address, string_view url);

	static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

	static curl_socket_t opensocket(void* clientp,
									curlsocktype purpose,
									curl_sockaddr* addr);

	static curl_socket_t approve_curl_request(void* clientp,
											char* conn_primary_ip,
											char* conn_local_ip,
											int conn_primary_port,
											int conn_local_port);

	const unique_ptr_with_deleter<CURL, curl_easy_cleanup> m_curl_handle;
	std::shared_ptr<const ip_filter> m_ip_filter;
	boost::beast::flat_buffer m_read_buffer;
	error_code m_status;
	operation_t m_error_operation = operation_t::unknown;
	bool m_ssrf_mitigation = false;
};
}

#endif //TORRENT_USE_CURL
#endif //LIBTORRENT_CURL_REQUEST_H
