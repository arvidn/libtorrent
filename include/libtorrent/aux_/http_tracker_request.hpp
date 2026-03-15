/*

Copyright (c) 2004, 2006-2009, 2012, 2014-2017, 2019-2026 Arvid Norberg
Copyright (c) 2016, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_HTTP_TRACKER_REQUEST_HPP_INCLUDED
#define TORRENT_HTTP_TRACKER_REQUEST_HPP_INCLUDED

#include <list>
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/operations.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent::aux {
struct request_callback;
struct tracker_request;
struct session_settings;

class http_tracker_request {
	const tracker_request& m_params;
	const session_settings& m_settings;
public:
	http_tracker_request(const tracker_request& req, const session_settings& settings)
		: m_params(req), m_settings(settings)
	{
	}

	constexpr static int max_redirects = 5;

	struct error_type{
		error_code code;
		operation_t op = operation_t::bittorrent;
		std::string failure_reason = {};
		seconds32 interval{};
		seconds32 min_interval{};
		explicit operator bool() const noexcept { return static_cast<bool>(code); }
	};

	[[nodiscard]] error_type process_response(
		request_callback& cb,
		const address& tracker_ip,
		const std::list<address>& ip_list,
		span<char const> data) const;

	[[nodiscard]] std::string build_tracker_url(bool i2p, error_type& error) const;

	[[nodiscard]] std::string get_user_agent() const;
	[[nodiscard]] seconds32 get_timeout() const;

#ifndef TORRENT_DISABLE_LOGGING
	void log_request(request_callback& cb, const std::string& url) const;
#endif

	[[nodiscard]] error_type validate_socket(bool i2p) const;
	[[nodiscard]] bool allowed_by_idna(string_view hostname) const;

	[[nodiscard]] error_type filter(
		std::weak_ptr<request_callback>& logger,
		std::vector<tcp::endpoint>& endpoints,
		string_view url) const;

	[[nodiscard]] error_type filter(
		std::weak_ptr<request_callback>& logger,
		const address& ip,
		string_view url) const;

private:
	template<typename T>
	[[nodiscard]] error_type filter_impl(
		std::weak_ptr<request_callback>& logger,
		std::vector<T>& endpoints,
		string_view url) const;
};
}

#endif //TORRENT_HTTP_TRACKER_REQUEST_HPP_INCLUDED
