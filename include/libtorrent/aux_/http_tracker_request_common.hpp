/*

Copyright (c) 2004, 2006-2009, 2012, 2014-2017, 2019-2026 Arvid Norberg
Copyright (c) 2016, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_HTTP_TRACKER_REQUEST_COMMON_HPP
#define LIBTORRENT_HTTP_TRACKER_REQUEST_COMMON_HPP

#include <list>
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/operations.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent::aux {
struct request_callback;
class tracker_request;
class session_settings;

class http_tracker_request_common {
	const tracker_request& m_params;
	const session_settings& m_settings;
public:
	http_tracker_request_common(const tracker_request& req, const session_settings& settings)
		: m_params(req), m_settings(settings)
	{
	}

	struct error_type{
		error_code code;
		operation_t op = operation_t::bittorrent;
		std::string failure_reason;
		seconds32 interval{0};
		seconds32 min_interval{0};
		explicit operator bool() const noexcept { return static_cast<bool>(code); }
	};

	error_type process_response(
		request_callback& cb,
		const address& tracker_ip,
		const std::list<address>& ip_list,
		span<char const> data) const;

	std::string build_tracker_url(bool i2p, error_type& error) const;
	std::string get_user_agent() const;
	seconds32 get_timeout() const;
#ifndef TORRENT_DISABLE_LOGGING
	void log_request(request_callback& cb, const std::string& url) const;
#endif
	error_type validate_socket(bool i2p) const;
};
}

#endif //LIBTORRENT_HTTP_TRACKER_REQUEST_COMMON_HPP
