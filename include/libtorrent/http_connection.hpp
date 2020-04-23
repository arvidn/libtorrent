/*

Copyright (c) 2007-2018, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_HTTP_CONNECTION
#define TORRENT_HTTP_CONNECTION

#ifdef TORRENT_USE_OPENSSL
// there is no forward declaration header for asio
namespace boost {
namespace asio {
namespace ssl {
	class context;
}
}
}
#endif

#include <functional>
#include <vector>
#include <string>

#include "libtorrent/socket.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/i2p_stream.hpp"
#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/resolver_interface.hpp"
#include "libtorrent/optional.hpp"

namespace libtorrent {

struct http_connection;
struct resolver_interface;

// internal
constexpr int default_max_bottled_buffer_size = 2 * 1024 * 1024;

using http_handler = std::function<void(error_code const&
	, http_parser const&, span<char const> data, http_connection&)>;

using http_connect_handler = std::function<void(http_connection&)>;

using http_filter_handler = std::function<void(http_connection&, std::vector<tcp::endpoint>&)>;

// when bottled, the last two arguments to the handler
// will always be 0
struct TORRENT_EXTRA_EXPORT http_connection
	: std::enable_shared_from_this<http_connection>
{
	http_connection(io_service& ios
		, resolver_interface& resolver
		, http_handler const& handler
		, bool bottled
		, int max_bottled_buffer_size
		, http_connect_handler const& ch
		, http_filter_handler const& fh
#ifdef TORRENT_USE_OPENSSL
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
		, boost::optional<address> const& bind_addr = boost::optional<address>()
		, resolver_flags resolve_flags = resolver_flags{}, std::string const& auth_ = std::string()
#if TORRENT_USE_I2P
		, i2p_connection* i2p_conn = nullptr
#endif
		);

	void start(std::string const& hostname, int port
		, time_duration timeout, int prio = 0, aux::proxy_settings const* ps = nullptr
		, bool ssl = false, int handle_redirect = 5
		, boost::optional<address> const& bind_addr = boost::optional<address>()
		, resolver_flags resolve_flags = resolver_flags{}
#if TORRENT_USE_I2P
		, i2p_connection* i2p_conn = nullptr
#endif
		);

	void close(bool force = false);

	aux::socket_type const& socket() const { return m_sock; }

	std::vector<tcp::endpoint> const& endpoints() const { return m_endpoints; }

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

	std::string m_hostname;
	std::string m_url;
	std::string m_user_agent;

	aux::vector<tcp::endpoint> m_endpoints;

	// if the current connection attempt fails, we'll connect to the
	// endpoint with this index (in m_endpoints) next
	int m_next_ep;

	aux::socket_type m_sock;

#ifdef TORRENT_USE_OPENSSL
	ssl::context* m_ssl_ctx;
#endif

#if TORRENT_USE_I2P
	i2p_connection* m_i2p_conn;
#endif
	resolver_interface& m_resolver;

	http_parser m_parser;
	http_handler m_handler;
	http_connect_handler m_connect_handler;
	http_filter_handler m_filter_handler;
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
	boost::optional<address> m_bind_addr;

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
	resolver_flags m_resolve_flags;

	std::uint16_t m_port;

	// bottled means that the handler is called once, when
	// everything is received (and buffered in memory).
	// non bottled means that once the headers have been
	// received, data is streamed to the handler
	bool m_bottled;

	// set to true the first time the handler is called
	bool m_called;

	// only hand out new quota 4 times a second if the
	// quota is 0. If it isn't 0 wait for it to reach
	// 0 and continue to hand out quota at that time.
	bool m_limiter_timer_active;

	// true if the connection is using ssl
	bool m_ssl;

	bool m_abort;

	// true while waiting for an async_connect
	bool m_connecting;
};

}

#endif
