/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/curl_request.hpp"

#if TORRENT_USE_CURL
#include <optional>
#include "libtorrent/aux_/debug.hpp"
#include "libtorrent/aux_/parse_url.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/error.hpp"

using namespace libtorrent;

namespace libtorrent::aux {
namespace{
std::optional<address> curl_addr_to_boost(const curl_sockaddr& addr)
{
	if (addr.family == AF_INET)
	{
		if (addr.addrlen < sizeof(sockaddr_in))
			return {};
		const auto sin = reinterpret_cast<const sockaddr_in *>(&addr.addr);
		address_v4::bytes_type bytes;
		std::memcpy(bytes.data(), &sin->sin_addr.s_addr, bytes.size());
		return std::optional<address>{std::in_place, address_v4(bytes)};
	}
	else if (addr.family == AF_INET6)
	{
		if (addr.addrlen < sizeof(sockaddr_in6))
			return {};
		const auto sin6 = reinterpret_cast<const sockaddr_in6 *>(&addr.addr);
		address_v6::bytes_type bytes;
		std::memcpy(bytes.data(), &sin6->sin6_addr, bytes.size());
		return std::optional<address>{std::in_place, address_v6(bytes, sin6->sin6_scope_id)};
	}
	return {};
}
} // anonymous namespace

// To enable ip based filtering before the socket is opened, there is no special logic for opening the socket.
curl_socket_t curl_request::opensocket(void* clientp,
										const curlsocktype purpose,
										curl_sockaddr* addr)
{
	(void) purpose;
	if (!addr || !clientp)
		return CURL_SOCKET_BAD;

	auto request = static_cast<curl_request*>(clientp);
	try
	{
		auto ip = curl_addr_to_boost(*addr);
		if (!ip)
			return CURL_SOCKET_BAD;

		if (!request->filter(*ip))
			return CURL_SOCKET_BAD;

		request->start_timeout(request->m_timeout);

		return socket(addr->family, addr->socktype, addr->protocol);
	}
	catch (const std::exception& e) {
		request->m_error = {
			error::no_recovery,
			operation_t::unknown,
			std::string("curl_request::opensocket exception: ") + e.what()
		};
	}
	catch (...)
	{
		request->m_error = {
			error::no_recovery,
			operation_t::unknown,
			"curl_request::opensocket unknown exception"
		};
	}
	return CURL_SOCKET_BAD;
}

curl_socket_t curl_request::before_curl_request(void* clientp,
												char* conn_primary_ip,
												char* conn_local_ip,
												int conn_primary_port,
												int conn_local_port)
{
	(void) conn_local_ip;
	(void) conn_primary_port;
	(void) conn_local_port;

	if (!clientp || !conn_primary_ip)
		return CURL_PREREQFUNC_ABORT;

	auto request = static_cast<curl_request*>(clientp);
	try
	{
		error_code ec;
		const address ip = make_address(conn_primary_ip, ec);
		if (ec)
			return CURL_PREREQFUNC_ABORT;

		if (!request->filter(ip))
			return CURL_PREREQFUNC_ABORT;

		request->start_timeout(request->m_timeout);

		return CURL_PREREQFUNC_OK;
	}
	catch (const std::exception& e) {
		request->m_error = {
			error::no_recovery,
			operation_t::unknown,
			std::string("curl_request::approve_curl_request exception: ") + e.what()
		};
	}
	catch (...)
	{
		request->m_error = {
			error::no_recovery,
			operation_t::unknown,
			"curl_request::approve_curl_request unknown exception"
		};
	}

	return CURL_PREREQFUNC_ABORT;
}

int curl_request::before_resolving(void* /*resolver_state*/, void* /*reserved*/, void* userdata)
{
	auto request = static_cast<curl_request*>(userdata);
	TORRENT_ASSERT(request);

	try
	{
		// DNS has twice the timeout in http_connection.cpp (because it can be queued behind a slow DNS request)
		request->start_timeout(request->m_timeout * 2);
		return 0;
	}
	catch (const std::exception& e) {
		request->m_error = {
			error::no_recovery,
			operation_t::unknown,
			std::string("curl_request::approve_curl_request exception: ") + e.what()
		};
	}
	catch (...)
	{
		request->m_error = {
			error::no_recovery,
			operation_t::unknown,
			"curl_request::approve_curl_request unknown exception"
		};
	}

	return -1;
}

size_t curl_request::write_callback(
	char* ptr,
	const size_t /* size */,
	const size_t nmemb,
	void* userdata)
{
	TORRENT_ASSERT(userdata);
	auto request = static_cast<curl_request*>(userdata);
	try
	{
		auto& read_buf = request->m_read_buffer;
		auto buffer = read_buf.prepare(nmemb);
		std::memcpy(buffer.data(), ptr, nmemb);
		read_buf.commit(nmemb);
		return nmemb;
	}
	catch (std::length_error&)
	{
		request->m_error.code = make_error_code(boost::system::errc::file_too_large);
	}
	catch (...)
	{
		request->m_error.code = errors::no_memory;
	}
	// return a value different from nmemb to signal an error
	return nmemb + 1;
}

curl_request::curl_request(const std::size_t max_buffer_size, const io_context::executor_type& executor)
	: m_read_buffer(max_buffer_size)
	, m_timer(executor)
{
}

void curl_request::set_defaults()
{
	curl_basic_request::set_defaults();

	set_write_callback_data(this);
	set_write_callback(write_callback);

	// to filter before the socket is opened
	set_opensocket_callback_data(this);
	set_opensocket_callback(opensocket);

	// to filter before request when socket is reused
	set_prereq_callback_data(this);
	set_prereq_callback(before_curl_request);

	// to start timer as early as possible
	set_resolver_callback_data(this);
	set_resolver_callback(before_resolving);
}

curl_request::error_type curl_request::redirect_security_settings(const exploded_url& parts)
{
	error_code ec;
	auto [prev_protocol, prev_auth, prev_hostname, prev_port, prev_path]
		= parse_url_components(get_url(), ec);
	if (ec)
	{
		return {ec, operation_t::parse_address};
	}

	// don't use auth credentials for other hosts
	const bool same_host = parts.protocol() == prev_protocol && parts.hostname() == prev_hostname && parts.port() == prev_port;
	if (same_host && parts.auth().empty())
	{
		if (!prev_auth.empty())
		{
			set_userpwd(prev_auth);
		}
		else
		{
			// reuse `userpwd` from previous request
		}
	}
	else
	{
		clear_userpwd();
	}

	// Following security settings are not (yet) needed for our use cases:
	// - switch from POST to GET when switching from HTTPS to HTTP
	return {};
}

curl_request::error_type curl_request::redirect(const std::string& url, const exploded_url& parts)
{
	if (auto error = redirect_security_settings(parts))
		return error;

	set_url(url);
	// no referrer

	m_timer.cancel();
	m_filter_allowed = address{};
	m_read_buffer.clear();

	return {};
}

// Curl's CURLOPT_TIMEOUT tracks the total amount of time a request takes, including the queuing time.
// Requests can switch between queuing and processing more than once due to HTTP redirects.
//
// Instead of using curls built-in timeout, a custom implementation is used modeled after http_connection.cpp:
// - restart timer for each network related action (DNS resolve, connect, http request)
// - HTTP redirects are performed manually outside of curl to stop them from requeuing.
//
// Note that curl has not implemented a retry mechanism for ALT-SVC. And it performs HTTPS-RR retries as part of a
// happy-eyeballs algorithm which does not requeue. For now only HTTP redirects can cause a connection to be requeued.
void curl_request::start_timeout(seconds32 timeout)
{
	if (!m_timeout_callback)
		return;
	m_timer.expires_after(timeout);
	ADD_OUTSTANDING_ASYNC("curl_request::start_timer_if_needed");
	m_timer.async_wait([request = this](const error_code& ec) {
		COMPLETE_ASYNC("curl_request::start_timer_if_needed");
		if (ec == error::operation_aborted)
			return;
		request->m_timeout_callback(*request);
	});
}

span<const char> curl_request::data() const noexcept
{
	const auto buffer = m_read_buffer.data();
	using span_type = span<const char>;
	return span_type{
		static_cast<const char *>(buffer.data()),
		static_cast<span_type::difference_type>(buffer.size())
	};
}

bool curl_request::filter(const address& ip)
{
	// This is called:
	// 1. before opening a new socket, but not when a connection is reused.
	// 2. before a new request is made.
	//
	// Essentially this function can be executed multiple times for a single request. E.g. multiple sockets can be
	// opened when they fail.
	if (!m_filter)
		return true;

	// Prevent calling filter more than once for the same IP+url combination
	// Note: that m_filter_allowed is reset when the url changes in a redirect
	if (ip == m_filter_allowed)
		return true;

	if (!m_filter(*this, ip))
		return false;

	m_filter_allowed = ip;
	return true;
}

curl_request::error_type curl_request::get_error(const CURLcode result) const
{
	// request has its own error conditions that curl is not aware of, in those cases the curl error
	// will be too generic (e.g. memory error in write_callback)
	if (m_error)
		return {m_error};

	const auto errstr = curl_easy_strerror(result);
	auto f = [](auto error, operation_t op = operation_t::unknown) {
		return error_type{error, op, {}};
	};
	auto fs = [=](auto error, operation_t op = operation_t::unknown) {
		return error_type{error, op, errstr};
	};
	switch (result)
	{
		case CURLE_OK:
		case CURLE_AGAIN:
			return {};
		case CURLE_OPERATION_TIMEDOUT:
			return f(errors::timed_out);
		case CURLE_COULDNT_CONNECT:
		case CURLE_PROXY:
			return fs(error::host_unreachable);
		case CURLE_REMOTE_ACCESS_DENIED:
			return fs(error::access_denied);
		case CURLE_COULDNT_RESOLVE_HOST:
			return f(error::host_not_found);
		case CURLE_COULDNT_RESOLVE_PROXY:
			return fs(error::host_not_found);
		case CURLE_HTTP_RETURNED_ERROR:
			return f(error_code(http_status(), http_category()));
		case CURLE_TOO_MANY_REDIRECTS:
			return fs(errors::redirecting);
		case CURLE_WEIRD_SERVER_REPLY:
		case CURLE_BAD_CONTENT_ENCODING:
			return fs(errors::http_parse_error);
		case CURLE_FILESIZE_EXCEEDED:
			return fs(make_error_code(boost::system::errc::file_too_large));
		case CURLE_TOO_LARGE:
			return f(make_error_code(boost::system::errc::value_too_large));
		case CURLE_OUT_OF_MEMORY:
			return f(errors::no_memory);
		case CURLE_URL_MALFORMAT:
			return fs(errors::url_parse_error, operation_t::parse_address);
		case CURLE_UNSUPPORTED_PROTOCOL:
			return f(errors::unsupported_url_protocol);
		case CURLE_ABORTED_BY_CALLBACK:
			return f(error::no_recovery);
		default:
			return fs(error::no_recovery);
	}
}
}
#endif
