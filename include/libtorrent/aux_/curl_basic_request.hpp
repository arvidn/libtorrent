/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_CURL_BASIC_REQUEST_H
#define LIBTORRENT_CURL_BASIC_REQUEST_H

#include "libtorrent/config.hpp"

#if TORRENT_USE_CURL
#include <string>
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/curl.hpp"
#include "libtorrent/aux_/memory.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent {
struct ip_filter;
}

namespace libtorrent::aux {
struct proxy_settings;

// this class is a convenience/typesafe wrapper around Curl_easy only supporting curl built-in features
class TORRENT_EXTRA_EXPORT curl_basic_request {
public:
	struct error_type {
		error_code ec;
		operation_t op = operation_t::unknown;
		std::string message;
		constexpr explicit operator bool() const noexcept { return static_cast<bool>(ec); }
	};

	explicit curl_basic_request();
	~curl_basic_request() = default;
	curl_basic_request(curl_basic_request const&) = delete;

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

	[[nodiscard]] CURL* handle() const noexcept { return m_curl_handle.get(); }

	[[nodiscard]] errors::http_errors http_status() const;
	[[nodiscard]] address get_ip(error_code& ec) const;
	[[nodiscard]] error_type get_error(CURLcode result) const;

	bool bind(const std::string& device, const address& local_address);
	void set_proxy(const proxy_settings& ps, bool verify_ssl);

	void set_user_agent(const std::string& s);
	void set_url(const std::string& s);
	void set_private_data(void* obj);
	void set_ipresolve(long option);

	void set_ssl_verify_host(bool onoff);
	void set_ssl_verify_peer(bool onoff);

	void set_pipewait(bool onoff);

#if TORRENT_ABI_VERSION == 1
	void set_userpwd(const std::string& s);
#endif

	[[nodiscard]] long        get_num_connects()         const;
	[[nodiscard]] std::size_t get_header_size()          const;
	[[nodiscard]] std::size_t get_compressed_body_size() const;

	// includes bytes from redirection requests
	[[nodiscard]] std::size_t get_request_size()         const;

	void set_write_callback(curl_write_callback cb);
	void set_write_callback_data(const void* data);
protected:
	void set_debug_logging(bool onoff);
	[[nodiscard]] char* get_url() const;

	void set_opensocket_callback(curl_opensocket_callback cb);
	void set_opensocket_callback_data(const void* data);

	void set_prereq_callback(curl_prereq_callback cb);
	void set_prereq_callback_data(const void* data);

	void set_timeout(seconds value);
	void set_timeout(milliseconds option);
private:
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

protected:
	const unique_ptr_with_deleter<CURL, curl_easy_cleanup> m_curl_handle;
};
}

#endif //TORRENT_USE_CURL
#endif //LIBTORRENT_CURL_BASIC_REQUEST_H
