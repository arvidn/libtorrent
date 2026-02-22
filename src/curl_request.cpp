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
#include "libtorrent/aux_/parse_url.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/ip_filter.hpp"

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

		const auto url = request->get_url();
		if (!request->validate_request(*ip, url))
			return CURL_SOCKET_BAD;

		return socket(addr->family, addr->socktype, addr->protocol);
	}
#ifndef TORRENT_DEBUG_LIBCURL
	catch (const std::exception& e) {
		std::fprintf(stderr, "curl_request::opensocket exception: %s\n", e.what());
	}
#endif
	catch (...)
	{
#if TORRENT_DEBUG_LIBCURL
		std::fprintf(stderr, "curl_request::opensocket unknown exception\n");
#endif
	}
	return CURL_SOCKET_BAD;
}

curl_socket_t curl_request::approve_curl_request(void* clientp,
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

		const auto url = request->get_url();
		if (!request->validate_request(ip, url))
			return CURL_PREREQFUNC_ABORT;

		return CURL_PREREQFUNC_OK;
	}
#if TORRENT_DEBUG_LIBCURL
	catch (const std::exception& e) {
		std::fprintf(stderr, "curl_request::approve_curl_request exception: %s\n", e.what());
	}
#endif
	catch (...)
	{
#if TORRENT_DEBUG_LIBCURL
		std::fprintf(stderr, "curl_request::approve_curl_request unknown exception\n");
#endif
	}
	return CURL_PREREQFUNC_ABORT;
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
		request->m_status = make_error_code(boost::system::errc::file_too_large);
	}
	catch (...)
	{
		request->m_status = errors::no_memory;
	}
	// return a value different from nmemb to signal an error
	return nmemb + 1;
}

curl_request::curl_request(const std::size_t max_buffer_size)
	: m_read_buffer(max_buffer_size)
{
}

void curl_request::set_defaults()
{
	curl_basic_request::set_defaults();

	set_write_callback_data(this);
	set_write_callback(write_callback);

	// to validate requests before the socket is opened
	set_opensocket_callback_data(this);
	set_opensocket_callback(opensocket);

	// to apply checks on each request (including redirects)
	set_prereq_callback_data(this);
	set_prereq_callback(approve_curl_request);
}

void curl_request::set_timeout(seconds32 timeout)
{
	// Curl's CURLOPT_TIMEOUT tracks the total amount of time a request takes, including the queuing time.
	// Requests can switch between queuing and processing more than once due to redirects (HTTP redirects, ALT-SRV,
	// HTTPS-RR).
	//
	// A timeout constant is usually meant to deal with network conditions, and does not take queuing into account.
	// Only curl can know how much time is spent queuing. The best "hack" we can implement is to start the timer on the
	// first action we see (`CURLOPT_RESOLVER_START_FUNCTION`, `CURLOPT_OPENSOCKETFUNCTION` or `CURLOPT_PREREQFUNCTION`). This
	// can't take into account time spent requeuing for redirects. Ideally this should be fixed by the curl project.

	TORRENT_ASSERT(timeout >= seconds32{0});

	// normally a timeout of 0 means no timeout at all, but the `http_connection.cpp` implementation forces the connection to
	// timeout immediately
	if (timeout == seconds32{0})
	{
		// smallest timeout curl supports
		curl_basic_request::set_timeout(milliseconds{1});
	}
	else
	{
		curl_basic_request::set_timeout(seconds{timeout});
	}
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

bool curl_request::allowed_by_ip_filter(const address& ip)
{
	if (!m_ip_filter)
		return true;

	if (m_ip_filter->access(ip) == ip_filter::blocked)
	{
		m_status = errors::banned_by_ip_filter;
		return false;
	}

	if (m_status == errors::banned_by_ip_filter)
	{
		// recovered by using a different resolved ip address
		m_status = errors::no_error;
	}

	return true;
}

bool curl_request::allowed_by_ssrf(const address& ip, const std::string& path)
{
	if (!m_enable_ssrf_mitigation)
		return true;

	if (ip.is_loopback() && !is_announce_path(path))
	{
		m_status = errors::ssrf_mitigation;
		return false;
	}

	if (m_status == errors::ssrf_mitigation)
	{
		// recovered by using a different resolved ip address
		m_status = errors::no_error;
	}

	return true;
}

bool curl_request::allowed_by_idna(const std::string& hostname)
{
	if (m_block_idna && is_idna(hostname))
	{
		m_status = errors::blocked_by_idna;
		return false;
	}

	return true;
}

bool curl_request::validate_request(const address& ip, const string_view url)
{
	if (!allowed_by_ip_filter(ip))
		return false;

	if (!m_enable_ssrf_mitigation && !m_block_idna)
		return true;

	std::string hostname;
	std::string path;
	error_code ec;
	std::tie(std::ignore, std::ignore, hostname, std::ignore, path)
			= parse_url_components(std::string(url), ec);
	if (ec)
	{
		m_status = ec;
		m_error_operation = operation_t::parse_address;
		return false;
	}

	if (!allowed_by_ssrf(ip, path))
		return false;

	if (!allowed_by_idna(hostname))
		return false;

	return true;
}

curl_request::error_type curl_request::get_error(const CURLcode result) const
{
	// request has its own error conditions that curl is not aware of, in those cases the curl error
	// will be too generic (e.g. ssrf)
	if (m_status)
		return {m_status, m_error_operation, {}};

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
			return fs(boost::asio::error::host_unreachable);
		case CURLE_REMOTE_ACCESS_DENIED:
			return fs(boost::asio::error::access_denied);
		case CURLE_COULDNT_RESOLVE_HOST:
			return f(boost::asio::error::host_not_found);
		case CURLE_COULDNT_RESOLVE_PROXY:
			return fs(boost::asio::error::host_not_found);
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
			return f(boost::asio::error::no_recovery);
		default:
			return fs(boost::asio::error::no_recovery);
	}
}
}
#endif
