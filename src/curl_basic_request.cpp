/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/curl_basic_request.hpp"

#if TORRENT_USE_CURL
#include <optional>
#include "libtorrent/aux_/parse_url.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/string_view.hpp"

using namespace libtorrent;

namespace libtorrent::aux {

curl_basic_request::curl_basic_request()
	: m_curl_handle(curl_easy_init())
{
}

errors::http_errors curl_basic_request::http_status() const
{
	// will be 0 if no response code is set.
	auto code = getopt<long, CURLINFO_RESPONSE_CODE>();
	return static_cast<errors::http_errors>(code);
}

address curl_basic_request::get_ip(error_code& ec) const
{
	const auto ip = getopt<char*, CURLINFO_PRIMARY_IP>();
	return make_address(ip, ec);
}

void curl_basic_request::set_defaults()
{
	// initialize a set of basic defaults, some default options are documented, and not explicitly set

	// Network config
	// - CURLOPT_SSL_VERIFYSTATUS (default disabled, a.k.a. OSCP stapling/verification)
	// - CURLOPT_SSL_ENABLE_ALPN (default enabled)
	// - CURLOPT_CAPATH (unset, use system default)
	// - CURLOPT_CAINFO (unset, use system default)
	// - CURLOPT_ECH (default disabled, experimental, requires https-rr and is currently only supported with DoH resolver)
	// - CURLOPT_HSTS_CTRL (default disabled, it is not port aware, and simply redirects all non-https traffic to
	//                      port :443)
	// - CURLOPT_TCP_KEEPALIVE (default disabled, keeping connections alive when idle is not supported by curl)

	// Setting SSL requirements protects against downgrading to insecure SSL configurations during a downgrade attack
	// - CURLOPT_SSLVERSION (default = v1.2 or newer, note that this option is used to set a lower bound)
	// - CURLOPT_SSL_CIPHER_LIST (use ssl library default, this is usually a subset of the "HIGH" encryption cipher list)

	// TLSv1.2+ became the default in 8.16.0
	static const bool has_insecure_minimum_ssl_version = curl_version_lower_than(0x081000);

	if (has_insecure_minimum_ssl_version)
		setopt<CURLOPT_SSLVERSION, long>(CURL_SSLVERSION_TLSv1_2); // TLSv1.2 or newer

	// curl can pick up certain environment variables (HTTP_PROXY, NO_PROXY ...) which can make it diverge
	// from the settings libtorrent uses. Setting PROXY to an empty string explicitly prevents this behavior.
	setopt<CURLOPT_PROXY>("");

	// NOSIGNAL allows curl to request the OS not to use signals on the socket operations. The os will then use an
	// error code instead of signals to communicate certain error conditions. It also stops curl from constantly
	// toggling the SIGPIPE signal handler through expensive syscalls. Note that this means that SIGPIPE can
	// *potentially* be received by libtorrent and it's users. Curl supports many platforms and that warning
	// is not relevant to us. With NOSIGNAL the behavior should be similar to that of the boost library.
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
	// - CURLOPT_FOLLOWLOCATION (default: off)
	// - CURLOPT_MAXREDIRS (default: 30, depends on CURLOPT_FOLLOWLOCATION)
	// - CURLOPT_AUTOREFERER (default: disabled)

	// CURLOPT_PIPEWAIT: Queue when a multiplexing connection is connecting/upgrading instead of creating a new connection.
	// However, if the multiplexing connection is fully connected/upgraded but is unavailable due to reaching the maximum
	// number of streams, a new connection is created instead of queuing. Default: false

	// prevent connecting/redirecting to a wrong scheme (e.g. file://evil/file")
	setopt<CURLOPT_PROTOCOLS_STR>("http,https");

	// HTTP headers
	// empty string enables all built-in encodings
	setopt<CURLOPT_ACCEPT_ENCODING>("");

#if TORRENT_DEBUG_LIBCURL
	set_debug_logging(true);
#endif
}

bool curl_basic_request::bind(const std::string& device, const address& local_address)
{
	// note: curl binding network interfaces on device name is not supported on `windows`, binding on local IP works.
	// No need to do anything special, device binding will quietly be ignored if not supported.

	// Users don't expect to leak DNS requests when explicitly binding a VPN interface.
	// However, binding DNS is not implemented (same for the boost resolver).
	// - If multiple nameservers are configured it is unclear which one to use and on which interface.
	// - DNS nameservers might be hidden behind local DNS proxies (e.g. `127.0.0.1:53`, resolvectl).
	// Adding settings for `nameservers` and `nameserver outgoing_interfaces` would be required.

	const bool valid_address = local_address.is_unspecified();
	if (device.empty() && !valid_address)
		return false;

	// unset the ipv6 scope to remove it from to_string(), curl does not accept it
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

void curl_basic_request::set_proxy(const proxy_settings& ps, const bool verify_ssl)
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

namespace {
constexpr string_view curl_easy_option_str(CURLoption option) noexcept
{
	switch (option)
	{
		case CURLOPT_VERBOSE:					return "CURLOPT_VERBOSE";
		case CURLOPT_DEBUGFUNCTION:				return "CURLOPT_DEBUGFUNCTION";
		case CURLOPT_DEBUGDATA:					return "CURLOPT_DEBUGDATA";
		case CURLOPT_WRITEFUNCTION:				return "CURLOPT_WRITEFUNCTION";
		case CURLOPT_PIPEWAIT:					return "CURLOPT_PIPEWAIT";
		case CURLOPT_USERPWD:					return "CURLOPT_USERPWD";
		case CURLOPT_SSL_VERIFYPEER:			return "CURLOPT_SSL_VERIFYPEER";
		case CURLOPT_SSL_VERIFYHOST:			return "CURLOPT_SSL_VERIFYHOST";
		case CURLOPT_IPRESOLVE:					return "CURLOPT_IPRESOLVE";
		case CURLOPT_PRIVATE:					return "CURLOPT_PRIVATE";
		case CURLOPT_URL:						return "CURLOPT_URL";
		case CURLOPT_USERAGENT:					return "CURLOPT_USERAGENT";
		case CURLOPT_PREREQFUNCTION:			return "CURLOPT_PREREQFUNCTION";
		case CURLOPT_PREREQDATA:				return "CURLOPT_PREREQDATA";
		case CURLOPT_OPENSOCKETFUNCTION:		return "CURLOPT_OPENSOCKETFUNCTION";
		case CURLOPT_OPENSOCKETDATA:			return "CURLOPT_OPENSOCKETDATA";
		case CURLOPT_WRITEDATA:					return "CURLOPT_WRITEDATA";
		case CURLOPT_ACCEPT_ENCODING:			return "CURLOPT_ACCEPT_ENCODING";
		case CURLOPT_FOLLOWLOCATION:			return "CURLOPT_FOLLOWLOCATION";
		case CURLOPT_NOSIGNAL:					return "CURLOPT_NOSIGNAL";
		case CURLOPT_PROXY:						return "CURLOPT_PROXY";
		case CURLOPT_DNS_INTERFACE:				return "CURLOPT_DNS_INTERFACE";
		case CURLOPT_INTERFACE:					return "CURLOPT_INTERFACE";
		case CURLOPT_DNS_LOCAL_IP4:				return "CURLOPT_DNS_LOCAL_IP4";
		case CURLOPT_DNS_LOCAL_IP6:				return "CURLOPT_DNS_LOCAL_IP6";
		case CURLOPT_PROXY_SSL_VERIFYHOST:		return "CURLOPT_PROXY_SSL_VERIFYHOST";
		case CURLOPT_PROXY_SSL_VERIFYPEER:		return "CURLOPT_PROXY_SSL_VERIFYPEER";
		case CURLOPT_PROXYUSERNAME:				return "CURLOPT_PROXYUSERNAME";
		case CURLOPT_PROXYPASSWORD:				return "CURLOPT_PROXYPASSWORD";
		case CURLOPT_PROXYPORT:					return "CURLOPT_PROXYPORT";
		case CURLOPT_PROXYTYPE:					return "CURLOPT_PROXYTYPE";
		case CURLOPT_SSLVERSION:				return "CURLOPT_SSLVERSION";
		case CURLOPT_RESOLVER_START_FUNCTION:	return "CURLOPT_RESOLVER_START_FUNCTION";
		case CURLOPT_RESOLVER_START_DATA:		return "CURLOPT_RESOLVER_START_DATA";
		case CURLOPT_PROTOCOLS_STR:				return "CURLOPT_PROTOCOLS_STR";
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
		else if constexpr (
			std::is_same_v<basic_type, std::string> ||
			std::is_same_v<basic_type, char *> ||
			std::is_same_v<basic_type, const char *>)
		{
			value_str = value;
			value_set = true;
		}
	}

	if (value_set)
		value_str = " to '" + value_str + "' ";

	const auto option_name = std::string(curl_easy_option_str(option));
	throw_ex<curl_easy_error>(error, option_name + value_str);
}

// Making `option` a compile time constant allows curl's typechecker
// to verify the types (it's currently not working/enabled for C++)
template<CURLoption option, typename T>
CURLcode curl_easy_setopt_wrapper(CURL* easy_handle, const T value)
{
	return curl_easy_setopt(easy_handle, option, value);
}

template<CURLoption option>
CURLcode curl_easy_setopt_wrapper(CURL* easy_handle, const std::string& value)
{
	return curl_easy_setopt_wrapper<option>(easy_handle, value.c_str());
}
} // anonymous namespace

template<CURLoption option>
void curl_basic_request::setopt(bool value)
{
	setopt<option,long>(value ? 1L : 0L);
}

template<CURLoption option, typename T>
void curl_basic_request::setopt(const T& value)
{
	// creates a compiler error when the option is not added the str() function
	static_assert(!curl_easy_option_str(option).empty());

	auto error = curl_easy_setopt_wrapper<option>(handle(), value);
	if (!error)
		return;

	throw_setop_ex(option, error, value);
}

void curl_basic_request::set_user_agent(const std::string& s)	{ setopt<CURLOPT_USERAGENT>(s); }
void curl_basic_request::set_url(const std::string& s)			{ setopt<CURLOPT_URL>(s); }
void curl_basic_request::set_private_data(void* const obj)		{ setopt<CURLOPT_PRIVATE>(obj); }
void curl_basic_request::set_ipresolve(const long option)		{ setopt<CURLOPT_IPRESOLVE>(option); }
void curl_basic_request::set_ssl_verify_host(const bool onoff)	{ setopt<CURLOPT_SSL_VERIFYHOST>(onoff); }
void curl_basic_request::set_ssl_verify_peer(const bool onoff)	{ setopt<CURLOPT_SSL_VERIFYPEER>(onoff); }
void curl_basic_request::set_pipewait(const bool onoff)			{ setopt<CURLOPT_PIPEWAIT>(onoff); }
void curl_basic_request::set_debug_logging(const bool onoff)	{ setopt<CURLOPT_VERBOSE>(onoff); }
void curl_basic_request::set_userpwd(const std::string& s)		{ setopt<CURLOPT_USERPWD>(s); }
void curl_basic_request::clear_userpwd()						{ setopt<CURLOPT_USERPWD, const char*>(nullptr); }

void curl_basic_request::set_prereq_callback(const curl_prereq_callback cb)	{ setopt<CURLOPT_PREREQFUNCTION>(cb); }
void curl_basic_request::set_prereq_callback_data(void* const data)			{ setopt<CURLOPT_PREREQDATA>(data); }

void curl_basic_request::set_write_callback(const curl_write_callback cb)	{ setopt<CURLOPT_WRITEFUNCTION>(cb); }
void curl_basic_request::set_write_callback_data(void* const data)			{ setopt<CURLOPT_WRITEDATA>(data); }

void curl_basic_request::set_resolver_callback(const curl_resolver_start_callback cb) { setopt<CURLOPT_RESOLVER_START_FUNCTION>(cb); }
void curl_basic_request::set_resolver_callback_data(void* const data)				  { setopt<CURLOPT_RESOLVER_START_DATA>(data); }

void curl_basic_request::set_opensocket_callback(const curl_opensocket_callback cb)	{ setopt<CURLOPT_OPENSOCKETFUNCTION>(cb); }
void curl_basic_request::set_opensocket_callback_data(void* const data)				{ setopt<CURLOPT_OPENSOCKETDATA>(data); }

template<typename T, CURLINFO option>
T curl_basic_request::getopt() const
{
	T value{};
	auto error = curl_easy_getinfo(handle(), option, &value);
	if (!error)
		return value;
	// This is not expect to return errors, possibly:
	// - incorrect type parameter (T) i.e. a programming error.
	// - CURLE_UNKNOWN_OPTION.
	throw_ex<curl_easy_error>(error, "curl_easy_getinfo(" + std::to_string(option) + ")");
}

const char* curl_basic_request::get_redirect_url()         const { return getopt<const char*, CURLINFO_REDIRECT_URL>(); }
const char* curl_basic_request::get_url()                  const { return getopt<const char*, CURLINFO_EFFECTIVE_URL>(); }
long        curl_basic_request::get_num_connects()         const { return getopt<long,  CURLINFO_NUM_CONNECTS>(); }
std::size_t curl_basic_request::get_header_size()          const { return static_cast<std::size_t>(getopt<long, CURLINFO_HEADER_SIZE>()); }
std::size_t curl_basic_request::get_request_size()         const { return static_cast<std::size_t>(getopt<long, CURLINFO_REQUEST_SIZE>()); }
std::size_t curl_basic_request::get_compressed_body_size() const { return static_cast<std::size_t>(getopt<curl_off_t, CURLINFO_SIZE_DOWNLOAD_T>()); }

}
#endif
