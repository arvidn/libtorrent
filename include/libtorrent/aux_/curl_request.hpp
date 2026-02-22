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
#include "libtorrent/aux_/curl_basic_request.hpp"
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

// this class extends curl_basic_request with features not natively supported by curl
class TORRENT_EXTRA_EXPORT curl_request : public curl_basic_request {
public:
	explicit curl_request(std::size_t max_buffer_size);
	~curl_request() = default;

	void set_defaults();

	[[nodiscard]] span<const char> data() const noexcept;
	[[nodiscard]] error_type get_error(CURLcode result) const;

	void set_ip_filter(std::shared_ptr<const ip_filter> filter) noexcept { m_ip_filter = std::move(filter); }
	void set_ssrf_mitigation(bool enabled) noexcept { m_enable_ssrf_mitigation = enabled; }
	void set_block_idna(bool enabled) noexcept { m_block_idna = enabled; }

	void set_timeout(seconds32 timeout);

private:
	[[nodiscard]] bool allowed_by_ip_filter(const address& ip);
	[[nodiscard]] bool allowed_by_ssrf(const address& ip, const std::string& path);
	[[nodiscard]] bool allowed_by_idna(const std::string& hostname);
	[[nodiscard]] bool validate_request(const address& address, string_view url);

	static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

	static curl_socket_t opensocket(void* clientp,
									curlsocktype purpose,
									curl_sockaddr* addr);

	static curl_socket_t approve_curl_request(void* clientp,
											char* conn_primary_ip,
											char* conn_local_ip,
											int conn_primary_port,
											int conn_local_port);

	std::shared_ptr<const ip_filter> m_ip_filter;
	boost::beast::flat_buffer m_read_buffer;
	error_code m_status;
	operation_t m_error_operation = operation_t::unknown;
	bool m_enable_ssrf_mitigation = false;
	bool m_block_idna = false;
};
}

#endif //TORRENT_USE_CURL
#endif //LIBTORRENT_CURL_REQUEST_H
