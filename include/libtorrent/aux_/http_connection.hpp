/*

Copyright (c) 2007-2020, Arvid Norberg
Copyright (c) 2015, Mikhail Titov
Copyright (c) 2016-2017, 2020, Alden Torres
Copyright (c) 2017, Steven Siloti
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_HTTP_CONNECTION
#define TORRENT_HTTP_CONNECTION

#include <functional>
#include <vector>
#include <string>
#include <optional>

#include "libtorrent/socket.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/http_parser.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/i2p_stream.hpp"
#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/resolver_interface.hpp"
#include "libtorrent/aux_/ssl.hpp"

namespace libtorrent::aux {

struct http_connection;

// internal
constexpr int default_max_bottled_buffer_size = 2 * 1024 * 1024;

using http_handler = std::function<void(error_code const&
	, aux::http_parser const&, span<char const> data, http_connection&)>;

using http_connect_handler = std::function<void(http_connection&)>;

using http_filter_handler = std::function<void(http_connection&, std::vector<tcp::endpoint>&)>;
using hostname_filter_handler = std::function<bool(http_connection&, string_view)>;

// when bottled, the last two arguments to the handler
// will always be 0
struct TORRENT_EXTRA_EXPORT http_connection
	: std::enable_shared_from_this<http_connection>
{

	http_connection(io_context& ios
		, aux::resolver_interface& resolver
		, http_handler handler
		, bool bottled
		, int max_bottled_buffer_size
		, http_connect_handler ch
		, http_filter_handler fh
		, hostname_filter_handler hfh
#if TORRENT_USE_SSL
		, ssl::context* ssl_ctx
#endif
		);

	// non-copyable
	http_connection(http_connection const&) = delete;
	http_connection& operator=(http_connection const&) = delete;

	virtual ~http_connection();

	void rate_limit(int limit);

	int rate_limit() const
	{ return m_rate_limit; }

	std::string m_sendbuffer;

	void get(std::string const& url, time_duration timeout = seconds(30)
		, int prio = 0, aux::proxy_settings const* ps = nullptr, int handle_redirects = 5
		, std::string const& user_agent = std::string()
		, std::optional<address> const& bind_addr = std::optional<address>()
		, aux::resolver_flags resolve_flags = aux::resolver_flags{}, std::string const& auth_ = std::string()
#if TORRENT_USE_I2P
		, i2p_connection* i2p_conn = nullptr
#endif
		);

	void start(std::string const& hostname, int port
		, time_duration timeout, int prio = 0, aux::proxy_settings const* ps = nullptr
		, bool ssl = false, int handle_redirect = 5
		, std::optional<address> const& bind_addr = std::optional<address>()
		, aux::resolver_flags resolve_flags = aux::resolver_flags{}
#if TORRENT_USE_I2P
		, i2p_connection* i2p_conn = nullptr
#endif
		);

	void close(bool force = false);

	aux::socket_type const& socket() const { return *m_sock; }

	std::vector<tcp::endpoint> const& endpoints() const { return m_endpoints; }

	std::string const& url() const { return m_url; }

private:

#if TORRENT_USE_I2P
	void connect_i2p_tracker(char const* destination);
	void on_i2p_resolve(error_code const& e
		, char const* destination);
#endif
	void on_resolve(error_code const& e
		, std::vector<address> const& addresses);
	void connect();
	void on_connect(error_code const& e);
	void on_write(error_code const& e);
	void on_read(error_code const& e, std::size_t bytes_transferred);
	static void on_timeout(std::weak_ptr<http_connection> p
		, error_code const& e);
	void on_assign_bandwidth(error_code const& e);

	void callback(error_code e, span<char> data = {});

	aux::vector<char> m_recvbuffer;
	io_context& m_ios;

	std::string m_hostname;
	std::string m_url;
	std::string m_user_agent;

	aux::vector<tcp::endpoint> m_endpoints;

	// if the current connection attempt fails, we'll connect to the
	// endpoint with this index (in m_endpoints) next
	int m_next_ep;

	std::optional<aux::socket_type> m_sock;

#if TORRENT_USE_SSL
	ssl::context* m_ssl_ctx;
#endif

#if TORRENT_USE_I2P
	i2p_connection* m_i2p_conn;
#endif
	aux::resolver_interface& m_resolver;

	aux::http_parser m_parser;
	http_handler m_handler;
	http_connect_handler m_connect_handler;
	http_filter_handler m_filter_handler;
	hostname_filter_handler m_hostname_filter_handler;
	deadline_timer m_timer;

	time_duration m_completion_timeout;

	// the timer fires every 250 millisecond as long
	// as all the quota was used.
	deadline_timer m_limiter_timer;

	time_point m_last_receive;
	time_point m_start_time;

	// specifies whether or not the connection is
	// configured to use a proxy
	aux::proxy_settings m_proxy;

	// the address to bind to. unset means do not bind
	std::optional<address> m_bind_addr;

	// if username password was passed in, remember it in case we need to
	// re-issue the request for a redirect
	std::string m_auth;

	int m_read_pos;

	// the number of redirects to follow (in sequence)
	int m_redirects;

	// maximum size of bottled buffer
	int m_max_bottled_buffer_size;

	// the current download limit, in bytes per second
	// 0 is unlimited.
	int m_rate_limit;

	// the number of bytes we are allowed to receive
	int m_download_quota;

	// the priority we have in the connection queue.
	// 0 is normal, 1 is high
	int m_priority;

	// used for DNS lookups
	aux::resolver_flags m_resolve_flags;

	std::uint16_t m_port;

	// bottled means that the handler is called once, when
	// everything is received (and buffered in memory).
	// non bottled means that once the headers have been
	// received, data is streamed to the handler
	bool m_bottled;

	// set to true the first time the handler is called
	bool m_called = false;

	// only hand out new quota 4 times a second if the
	// quota is 0. If it isn't 0 wait for it to reach
	// 0 and continue to hand out quota at that time.
	bool m_limiter_timer_active = false;

	// true if the connection is using ssl
	bool m_ssl = false;

	bool m_abort = false;

	// true while waiting for an async_connect
	bool m_connecting = false;

	// true while resolving hostname
	bool m_resolving_host = false;
};

}

#endif
