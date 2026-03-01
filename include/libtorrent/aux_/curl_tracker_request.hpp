/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_CURL_TRACKER_REQUEST_HPP
#define LIBTORRENT_CURL_TRACKER_REQUEST_HPP
#include "libtorrent/config.hpp"

#if TORRENT_USE_CURL
#include "libtorrent/aux_/curl.hpp"
#include "libtorrent/aux_/curl_request.hpp"
#include "libtorrent/aux_/intrusive_list.hpp"
#include "libtorrent/aux_/http_tracker_request.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent::aux {
class curl_tracker_manager;
struct request_callback;
struct tracker_request;

class curl_tracker_request : public unique_ptr_intrusive_list_base<curl_tracker_request> {
public:
	using error_type = http_tracker_request::error_type;

	curl_tracker_request(
		curl_tracker_manager& owner,
		tracker_request&& req,
		std::weak_ptr<request_callback> c,
		const io_context::executor_type& executor);

	curl_tracker_request(const curl_tracker_request&) = delete;

	[[nodiscard]] static curl_tracker_request* from_handle(CURL* easy_handle)
	{
		return curl_request::from_handle<curl_tracker_request>(easy_handle);
	}

	[[nodiscard]] error_type initialize_request();

#ifndef TORRENT_DISABLE_LOGGING
	[[nodiscard]] std::shared_ptr<request_callback> requester() const { return m_callback.lock(); }
#endif

	void complete(CURLcode result);
	void fail(const error_type& info) { fail(info.code, info.op, info.failure_reason, info.interval, info.min_interval); }

	[[nodiscard]] curl_request& get_curl_request() noexcept { return m_request; }
	[[nodiscard]] const curl_request & get_curl_request() const noexcept { return m_request; }

	[[nodiscard]] const tracker_request& get_params() const noexcept { return *m_params; }
	[[nodiscard]] bool is_stopped_event() const noexcept;

	// precondition: transfer is finished
	[[nodiscard]] bool is_redirect_response() const;

	// precondition: handle should be removed from pool
	// postcondition: request is updated to the redirect location
	[[nodiscard]] error_type prepare_redirect();

	[[nodiscard]] curl_tracker_manager& get_owner() const noexcept { return m_owner; }

private:
	void fail(const error_code& ec,
			operation_t op = operation_t::unknown,
			const std::string& message = {},
			seconds32 interval = {},
			seconds32 min_interval = {});

	void on_response();

	[[nodiscard]] error_type filter_hostname(string_view hostname);
	[[nodiscard]] bool filter_impl(const address& ip);
	[[nodiscard]] static bool filter(curl_request& request, const address& ip);
	void track_statistics();

	// storing the entire tracker_request object should not be necessary
	// unique_ptr to prevent circular header includes
	const std::unique_ptr<const tracker_request> m_params;

	curl_tracker_manager& m_owner;
	curl_request m_request;
	std::weak_ptr<request_callback> m_callback;

	error_code m_error;
	operation_t m_error_operation = operation_t::unknown;

	int m_allowed_redirects = 0;
};
}
#endif //TORRENT_USE_CURL
#endif //LIBTORRENT_CURL_TRACKER_REQUEST_HPP
