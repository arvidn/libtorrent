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

namespace libtorrent::aux {
struct proxy_settings;

// Convenience/type-safe wrapper around Curl_easy for curl's built-in features
class TORRENT_EXTRA_EXPORT curl_basic_request {
public:
	struct error_type {
		error_code code;
		operation_t op = operation_t::unknown;
		std::string message = {};
		constexpr explicit operator bool() const noexcept { return static_cast<bool>(code); }
	};

	curl_basic_request();

	void set_defaults();

	template<typename T>
	[[nodiscard]] static T* from_handle(CURL* easy_handle)
	{
		if (!easy_handle)
			return nullptr;
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

	[[nodiscard]] bool bind(const std::string& device, const address& local_address);
	void set_proxy(const proxy_settings& ps, bool verify_ssl);

	void set_user_agent(const std::string& s);
	void set_url(const std::string& s);
	void set_ipresolve(long option);

	void set_ssl_verify_host(bool onoff);
	void set_ssl_verify_peer(bool onoff);

	void set_pipewait(bool onoff);

	void set_userpwd(const std::string& s);
	void clear_userpwd();

	[[nodiscard]] const char* get_url()                  const;
	[[nodiscard]] const char* get_redirect_url()         const;
	[[nodiscard]] long        get_num_connects()         const;
	[[nodiscard]] std::size_t get_header_size()          const;
	[[nodiscard]] std::size_t get_compressed_body_size() const;

	// includes bytes from redirection requests
	[[nodiscard]] std::size_t get_request_size()         const;

	void set_write_callback(curl_write_callback cb);
	void set_write_callback_data(void* data);

protected:
	void set_private_data(void* obj);
	void set_debug_logging(bool onoff);

	void set_opensocket_callback(curl_opensocket_callback cb);
	void set_opensocket_callback_data(void* data);

	void set_prereq_callback(curl_prereq_callback cb);
	void set_prereq_callback_data(void* data);

	void set_resolver_callback(curl_resolver_start_callback cb);
	void set_resolver_callback_data(void* data);

private:
	template<CURLoption option>
	void setopt(bool value);

	template<CURLoption option, typename T>
	void setopt(const T& value);

	template<typename T, CURLINFO option>
	[[nodiscard]] T getopt() const;

	const unique_ptr_with_deleter<CURL, curl_easy_cleanup> m_curl_handle;
};
}

#endif //TORRENT_USE_CURL
#endif //LIBTORRENT_CURL_BASIC_REQUEST_H
