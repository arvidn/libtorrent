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
		if (!request->allowed_by_ssrf(*ip, url))
			return CURL_SOCKET_BAD;

		if (!request->allowed_by_ip_filter(*ip))
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
		if (!request->allowed_by_ssrf(ip, url))
			return CURL_PREREQFUNC_ABORT;

		if (!request->allowed_by_ip_filter(ip))
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
	: m_curl_handle(curl_easy_init())
	, m_read_buffer(max_buffer_size)
{
}

void curl_request::set_defaults()
{
	// initialize a set of basic defaults, some default options are documented, and not explicitly set

	// Network config
	// - CURLOPT_SSL_VERIFYSTATUS (default disabled, a.k.a. OSCP stapling/verification)
	// - CURLOPT_SSL_ENABLE_ALPN (default enabled)
	// - CURLOPT_CAPATH (unset, use system default)
	// - CURLOPT_CAINFO (unset, use system default)
	// - CURLOPT_ECH (default disabled, requires https-rr and is only supported with DoH resolver)
	// - CURLOPT_HSTS_CTRL (default disabled, it is not port aware, and simply redirects all non-https traffic to
	//                      port :443.)
	// - CURLOPT_TCP_KEEPALIVE (default disabled, keeping connections alive when idle is not supported by curl)

	// Setting SSL requirements protects from downgrading to insecure SSL configurations during a downgrade attack
	// - CURLOPT_SSLVERSION (default = v1.2+)
	// - CURLOPT_SSL_CIPHER_LIST (use ssl library default, this is usually a subset of the "HIGH" encryption cipher list)
	static const bool need_ssl_requirements = []() {
		const auto ver = curl_version_info(CURLVERSION_NOW);
		return !ver || ver->version_num < 0x081000; // TLSv1.2 became the default in 8.16.0
	}();

	if (need_ssl_requirements)
		setopt<CURLOPT_SSLVERSION, long>(CURL_SSLVERSION_TLSv1_2);

	// curl can pick up certain environment variables (HTTP_PROXY, NO_PROXY ...) which can make it diverge
	// from the settings libtorrent uses. Setting PROXY to an empty string explicitly prevents this behavior.
	setopt<CURLOPT_PROXY>("");

	// NOSIGNAL allows curl to request the OS not to use signals on the socket operations. The os will then use an
	// error code instead of signals to communicate certain error conditions.
	// It also stops curl from constantly toggling the SIGPIPE signal handler through
	// expensive syscalls. Note that that means that SIGPIPE can *potentially* be received by libtorrent
	// and it's users but this behavior should be similar to that of the boost library.
	setopt<CURLOPT_NOSIGNAL>(true);

	// CURLOPT_SHARE
	// When using the curl multi interface these are always shared even without CURLOPT_SHARE:
	// - CURL_LOCK_DATA_DNS
	// - CURL_LOCK_DATA_SSL_SESSION
	// - CURL_LOCK_DATA_PSL
	// The following additional shares can be set:
	// - CURL_LOCK_DATA_COOKIE  (not used, trackers connections don't need them)
	// - CURL_LOCK_DATA_HSTS    (not used, by default disabled through CURLOPT_HSTS_CTRL)

	// Flow control
	// - CURLOPT_CONNECTTIMEOUT (default: 300)
	// - CURLOPT_DNS_CACHE_TIMEOUT (default: 60)
	setopt<CURLOPT_FOLLOWLOCATION>(true);
	setopt<CURLOPT_MAXREDIRS>(5L);
	// prioritizes connection reuse
	set_pipewait(true);

	// HTTP headers
	// empty string enables all built-in encodings
	setopt<CURLOPT_ACCEPT_ENCODING>("");

	// callbacks to curl (shouldn't throw exceptions)
	setopt<CURLOPT_WRITEDATA>(this);
	set_write_callback(write_callback);

	// to apply the ip filter before socket is opened
	setopt<CURLOPT_OPENSOCKETDATA>(this);
	setopt<CURLOPT_OPENSOCKETFUNCTION, curl_opensocket_callback>(opensocket);

	// to apply checks on each request (including redirects)
	setopt<CURLOPT_PREREQDATA>(this);
	setopt<CURLOPT_PREREQFUNCTION, curl_prereq_callback>(approve_curl_request);

#if TORRENT_DEBUG_LIBCURL
	set_debug_logging(true);
#endif
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

void curl_request::set_timeout(seconds32 timeout)
{
	// Curl's CURLOPT_TIMEOUT tracks the total amount of time a request takes, including the queuing time.
	// Requests can switch between queuing and processing more than once due to redirects (HTTP redirects, ALT-SRV,
	// HTTPS-RR).
	//
	// A timeout constant is usually meant to deal with network conditions, and does not take queuing into account.
	// Only curl can know how much time is spent queuing. The best "hack" we can implement is to start the timer on the
	// first action we see (`CURLOPT_OPENSOCKETFUNCTION` or `CURLOPT_PREREQFUNCTION` when a socket is reused). This
	// can't take into account time spend on DNS lookups. It also can't exclude time spent requeueing for redirects.
	// Ideally this should be fixed by the curl project.

	TORRENT_ASSERT(timeout >= seconds32{0});

	// normally a timeout of 0 means no timeout at all, but the `http_connection.cpp` implementation forces the connection to
	// timeout immediately
	if (timeout == seconds32{0})
	{
		// 1 ms is the smallest timeout curl supports
		setopt<CURLOPT_TIMEOUT_MS, long>(1);
	}
	else
	{
		setopt<CURLOPT_TIMEOUT, long>(timeout.count());
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

errors::http_errors curl_request::http_status() const
{
	auto code = getopt<long, CURLINFO_RESPONSE_CODE>();
	return static_cast<errors::http_errors>(code);
}

address curl_request::get_ip(error_code& ec) const
{
	const auto ip = getopt<char*, CURLINFO_PRIMARY_IP>();
	return make_address(ip, ec);
}

bool curl_request::bind(const std::string& device, const address& local_address)
{
	// note: curl binding network interfaces on device name is not supported on `windows`, binding on local IP works.
	// it will quietly be ignored and won't throw an error

	// Users don't expect to leak DNS requests when explicitly binding a VPN interface.
	// However, binding DNS is not implemented (same for the boost resolver).
	// - If multiple nameservers are configured it is unclear which one to use and on which interface.
	// - DNS nameservers might be hidden behind local DNS proxies (e.g. `127.0.0.1:53`, resolvectl).
	// Adding settings for `nameservers` and `nameserver outgoing_interfaces` would be required.

	const bool valid_address = local_address.is_unspecified();
	if (device.empty() && !valid_address)
		return false;

	// unset the ipv6 scope to remove it from to_string(), which does not make sense for address binding
	address addr = local_address.is_v4() ? local_address : make_address_v6(local_address.to_v6().to_bytes());
	const auto addr_str = addr.to_string();

	std::string curl_interface;
	if (!device.empty())
	{
		if (!valid_address)
			curl_interface = "if!" + device;
		else
			curl_interface = "ifhost!" + device + "!" + addr_str;
	}
	else
		curl_interface = "host!" + addr_str;

	if (!curl_interface.empty())
		setopt<CURLOPT_INTERFACE>(curl_interface);

	return true;
}

void curl_request::set_proxy(const proxy_settings& ps, const bool verify_ssl)
{
	if (ps.type == settings_pack::none)
		return;

	if (!verify_ssl)
	{
		setopt<CURLOPT_PROXY_SSL_VERIFYHOST>(0L);
		setopt<CURLOPT_PROXY_SSL_VERIFYPEER>(0L);
	}

	if (ps.type == settings_pack::http_pw || ps.type == settings_pack::socks5_pw)
	{
		setopt<CURLOPT_PROXYUSERNAME>(ps.username);
		setopt<CURLOPT_PROXYPASSWORD>(ps.password);
	}

	setopt<CURLOPT_PROXY>(ps.hostname);
	setopt<CURLOPT_PROXYPORT>(static_cast<long>(ps.port));

	// Ignored for HTTP proxies:
	// - send_host_in_connect: libcurl always sends the hostname in the "CONNECT: " for dns resolution, and in
	//   the "Host:" header
	// - proxy_hostnames: the HTTP proxy is required to resolve the DNS by libcurl

	switch (ps.type)
	{
		case settings_pack::socks4:
			setopt<CURLOPT_PROXYTYPE>(ps.proxy_hostnames ? CURLPROXY_SOCKS4A : CURLPROXY_SOCKS4);
			break;
		case settings_pack::socks5:
		case settings_pack::socks5_pw:
			setopt<CURLOPT_PROXYTYPE>(ps.proxy_hostnames ? CURLPROXY_SOCKS5_HOSTNAME : CURLPROXY_SOCKS5);
			break;
		case settings_pack::http:
		case settings_pack::http_pw:
			// Let libcurl set proxy type from hostname string without specifying it here
			// CURLPROXY_HTTP     (default)
			// CURLPROXY_HTTP_1_0 (unused)
			// CURLPROXY_HTTPS    (when hostname starts with https://)
			// CURLPROXY_HTTPS2   (unused)
			break;
#if TORRENT_USE_I2P
		case settings_pack::i2p_proxy:
			TORRENT_ASSERT_FAIL();
			break;
#endif
		case settings_pack::none:
			break;
	}
}

bool curl_request::allowed_by_ip_filter(const address& ip)
{
	if (m_ip_filter && m_ip_filter->access(ip) == ip_filter::blocked)
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

bool curl_request::allowed_by_ssrf(const address& address, const string_view url)
{
	if (!m_ssrf_mitigation)
		return true;

	if (address.is_loopback())
	{
		// for a loopback address, the PATH must be /announce
		std::string path;
		error_code ec;
		std::tie(std::ignore, std::ignore, std::ignore, std::ignore, path)
				= parse_url_components(std::string(url), ec);
		if (ec)
		{
			m_status = ec;
			m_error_operation = operation_t::parse_address;
			return false;
		}

		if (path.substr(0, 9) != "/announce")
		{
			m_status = errors::ssrf_mitigation;
			return false;
		}
	}

	if (m_status == errors::ssrf_mitigation)
	{
		m_status = errors::no_error; // recovered from error
	}

	return true;
}

namespace {
constexpr string_view curl_easy_option_str(CURLoption option) noexcept
{
	switch (option)
	{
		case CURLOPT_VERBOSE:				return "CURLOPT_VERBOSE";
		case CURLOPT_DEBUGFUNCTION:			return "CURLOPT_DEBUGFUNCTION";
		case CURLOPT_DEBUGDATA:				return "CURLOPT_DEBUGDATA";
		case CURLOPT_WRITEFUNCTION:			return "CURLOPT_WRITEFUNCTION";
		case CURLOPT_PIPEWAIT:				return "CURLOPT_PIPEWAIT";
		case CURLOPT_USERPWD:				return "CURLOPT_USERPWD";
		case CURLOPT_SSL_VERIFYPEER:		return "CURLOPT_SSL_VERIFYPEER";
		case CURLOPT_SSL_VERIFYHOST:		return "CURLOPT_SSL_VERIFYHOST";
		case CURLOPT_IPRESOLVE:				return "CURLOPT_IPRESOLVE";
		case CURLOPT_PRIVATE:				return "CURLOPT_PRIVATE";
		case CURLOPT_URL:					return "CURLOPT_URL";
		case CURLOPT_USERAGENT:				return "CURLOPT_USERAGENT";
		case CURLOPT_PREREQFUNCTION:		return "CURLOPT_PREREQFUNCTION";
		case CURLOPT_PREREQDATA:			return "CURLOPT_PREREQDATA";
		case CURLOPT_OPENSOCKETFUNCTION:	return "CURLOPT_OPENSOCKETFUNCTION";
		case CURLOPT_OPENSOCKETDATA:		return "CURLOPT_OPENSOCKETDATA";
		case CURLOPT_WRITEDATA:				return "CURLOPT_WRITEDATA";
		case CURLOPT_ACCEPT_ENCODING:		return "CURLOPT_ACCEPT_ENCODING";
		case CURLOPT_MAXREDIRS:				return "CURLOPT_MAXREDIRS";
		case CURLOPT_FOLLOWLOCATION:		return "CURLOPT_FOLLOWLOCATION";
		case CURLOPT_NOSIGNAL:				return "CURLOPT_NOSIGNAL";
		case CURLOPT_PROXY:					return "CURLOPT_PROXY";
		case CURLOPT_TIMEOUT:				return "CURLOPT_TIMEOUT";
		case CURLOPT_TIMEOUT_MS:			return "CURLOPT_TIMEOUT_MS";
		case CURLOPT_DNS_INTERFACE:			return "CURLOPT_DNS_INTERFACE";
		case CURLOPT_INTERFACE:				return "CURLOPT_INTERFACE";
		case CURLOPT_DNS_LOCAL_IP4:			return "CURLOPT_DNS_LOCAL_IP4";
		case CURLOPT_DNS_LOCAL_IP6:			return "CURLOPT_DNS_LOCAL_IP6";
		case CURLOPT_PROXY_SSL_VERIFYHOST:	return "CURLOPT_PROXY_SSL_VERIFYHOST";
		case CURLOPT_PROXY_SSL_VERIFYPEER:	return "CURLOPT_PROXY_SSL_VERIFYPEER";
		case CURLOPT_PROXYUSERNAME:			return "CURLOPT_PROXYUSERNAME";
		case CURLOPT_PROXYPASSWORD:			return "CURLOPT_PROXYPASSWORD";
		case CURLOPT_PROXYPORT:				return "CURLOPT_PROXYPORT";
		case CURLOPT_PROXYTYPE:				return "CURLOPT_PROXYTYPE";
		case CURLOPT_SSLVERSION:			return "CURLOPT_SSLVERSION";
		default:
			return "";
	}
}

template<typename T>
void throw_setop_ex(CURLoption option, CURLcode error, const T& value)
{
	if (error == CURLE_OUT_OF_MEMORY)
		throw_ex<std::bad_alloc>();

	std::string value_str;
	bool value_set = false;

	if (error == CURLE_BAD_FUNCTION_ARGUMENT)
	{
		using basic_type = std::decay_t<T>;

		if constexpr (std::is_integral_v<basic_type>)
		{
			value_str = std::to_string(value);
			value_set = true;
		}
		else if constexpr (std::is_same_v<basic_type, std::string>)
		{
			value_str = value;
			value_set = true;
		}
		else if constexpr (
			std::is_same_v<basic_type, char *> ||
			std::is_same_v<basic_type, const char *>)
		{
			value_str = std::string(value);
			value_set = true;
		}
	}

	if (value_set)
		value_str = " to '" + value_str + "' ";

	const auto option_name = std::string(curl_easy_option_str(option));
	auto context = "setting " + option_name + value_str ;
	throw_ex<curl_easy_error>(error, context.c_str());
}
} // anonymous namespace

template<CURLoption option>
void curl_request::setopt(bool value)
{
	setopt<option,long>(value ? 1L : 0L);
}

template<CURLoption option, typename T>
void curl_request::setopt(const T& value)
{
	// creates a compiler error when the option is not added the str() function
	static_assert(!curl_easy_option_str(option).empty());

	auto error = curl_easy_setopt_typechecked<option>(handle(), value);
	if (!error)
		return;

	throw_setop_ex(option, error, value);
}

void curl_request::set_user_agent(const std::string& s)  { setopt<CURLOPT_USERAGENT>(s); }
void curl_request::set_url(const std::string& s)         { setopt<CURLOPT_URL>(s); }
void curl_request::set_private_data(void* const obj)     { setopt<CURLOPT_PRIVATE>(obj); }
void curl_request::set_ipresolve(const long option)      { setopt<CURLOPT_IPRESOLVE>(option); }
void curl_request::set_ssl_verify_host(const bool onoff) { setopt<CURLOPT_SSL_VERIFYHOST>(onoff); }
void curl_request::set_ssl_verify_peer(const bool onoff) { setopt<CURLOPT_SSL_VERIFYPEER>(onoff); }
void curl_request::set_pipewait(const bool onoff)        { setopt<CURLOPT_PIPEWAIT>(onoff); }
void curl_request::set_debug_logging(const bool onoff)   { setopt<CURLOPT_VERBOSE>(onoff); }

#if TORRENT_ABI_VERSION == 1
void curl_request::set_userpwd(const std::string& s)    { setopt<CURLOPT_USERPWD>(s); }
#endif

void curl_request::set_write_callback(const curl_write_callback option)
{
	setopt<CURLOPT_WRITEFUNCTION>(option);
}

template<typename T, CURLINFO option, typename>
T curl_request::getopt() const
{
	T value;
	auto error = curl_easy_getinfo_typechecked<T, option>(handle(), value);
	if (!error)
		return value;
	// This is not expect to return errors, possibly:
	// - incorrect type parameter (T) i.e. a programming error.
	// - CURLE_UNKNOWN_OPTION.
	throw_ex<curl_easy_error>(error, "curl_easy_getinfo (" + std::to_string(option) + "): ");
}

char*       curl_request::get_url()                  const { return getopt<char*, CURLINFO_EFFECTIVE_URL>(); }
long        curl_request::get_num_connects()         const { return getopt<long,  CURLINFO_NUM_CONNECTS>(); }
std::size_t curl_request::get_header_size()          const { return static_cast<std::size_t>(getopt<long, CURLINFO_HEADER_SIZE>()); }
std::size_t curl_request::get_request_size()         const { return static_cast<std::size_t>(getopt<long, CURLINFO_REQUEST_SIZE>()); }
std::size_t curl_request::get_compressed_body_size() const { return static_cast<std::size_t>(getopt<curl_off_t, CURLINFO_SIZE_DOWNLOAD_T>()); }
}
#endif
