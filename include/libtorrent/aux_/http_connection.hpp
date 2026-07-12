/*

Copyright (c) 2017, Steven Siloti
Copyright (c) 2007-2022, Arvid Norberg
Copyright (c) 2015, Mikhail Titov
Copyright (c) 2016-2017, 2020-2021, Alden Torres
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

struct bind_info_t
{
	std::string device;
	address ip;
	bool operator==(bind_info_t const& rhs) const
	{
		return device == rhs.device && ip == rhs.ip;
	}
};

struct TORRENT_EXTRA_EXPORT http_connection
	: std::enable_shared_from_this<http_connection>
{

	http_connection(io_context& ios
		, aux::resolver_interface& resolver
		, http_handler handler
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

	void get(std::string const& url,
		time_duration timeout = seconds(30),
		aux::proxy_settings const* ps = nullptr,
		int handle_redirects = 5,
		std::string const& user_agent = std::string(),
		std::optional<bind_info_t> const& bind_addr = std::nullopt,
		aux::resolver_flags resolve_flags = aux::resolver_flags{},
		std::string const& auth_ = std::string(),
#if TORRENT_USE_I2P
		i2p_connection* i2p_conn = nullptr,
#endif
		bool keep_alive = false,
		// fire-and-forget: write the request but read no response. On each write
		// completion the write-handler (set_write_handler) is invoked instead of
		// starting a read. Used for best-effort stop announces at shutdown, where
		// the response is irrelevant.
		bool write_only = false);

	// set the handler invoked when a write completes in write_only mode (the
	// caller then writes the next request or closes the connection).
	void set_write_handler(http_connect_handler h) { m_write_handler = std::move(h); }

	void start(std::string const& hostname, int port
		, time_duration timeout, aux::proxy_settings const* ps = nullptr
		, bool ssl = false, int handle_redirect = 5
		, std::optional<bind_info_t> const& bind_addr = std::nullopt
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
	void on_resolve(error_code const& e, std::vector<address> const& addresses);
	void connect();
	void on_connect(error_code const& e);
	void on_write(error_code const& e);
	void on_read(error_code const& e, std::size_t bytes_transferred);
	// write_only drain loop: read into m_drain_buffer and discard, repeatedly.
	void start_drain();
	void on_drain(error_code const& e, std::size_t bytes_transferred);
	// records that a write was just dispatched, advancing m_write_only_state
	// (see its declaration). A no-op outside write-only mode.
	void note_write_dispatched();
	// true once this connection cycle is in fire-and-forget write-only mode
	// (write requests but never parse a response; the socket stays reusable
	// for the next write without waiting for or reading a reply).
	bool write_only() const { return m_write_only_state != write_only_state_t::normal_mode; }
	// switches this connection cycle in or out of write-only mode for a new
	// get() call. Entering write-only mode only (re-)starts
	// write_only_state_t's progress from not_started if this connection
	// wasn't already in write-only mode -- a later, promoted dispatch calls
	// get() again with write_only=true on the same object, and must not
	// have its in-progress state clobbered back to the start.
	void set_write_only(bool enable);
	static void on_timeout(std::weak_ptr<http_connection> p
		, error_code const& e);
	void on_assign_bandwidth(error_code const& e);

	void callback(error_code e, span<char> data = {});

	aux::vector<char> m_recvbuffer;

	// scratch buffer for the write_only drain loop (responses are discarded).
	// Must not be m_recvbuffer: on_write() can invoke m_write_handler
	// synchronously, before this loop's read completes, which re-enters
	// start() for the next pipelined write and unconditionally clears/resizes
	// m_recvbuffer, racing the still-outstanding drain read's buffer.
	aux::vector<char> m_drain_buffer;
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
	// invoked on write completion in write_only mode, instead of reading a
	// response (see set_write_handler / get()'s write_only).
	http_connect_handler m_write_handler;
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

	// the address and/or device to bind to. unset means do not bind
	std::optional<bind_info_t> m_bind_addr;

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

	// used for DNS lookups
	aux::resolver_flags m_resolve_flags;

	std::uint16_t m_port;

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

	// set to true when a response completes on a connection that may be reused
	// for a subsequent request (i.e. the server did not ask to close it and the
	// socket is still open). This is captured when the response finishes, before
	// start() resets the parser, and gates the socket-reuse fast-path in start().
	bool m_reusable = false;

	// whether the most recent get() call requested keep-alive. Preserved so
	// that a redirect re-issues get() with the same keep-alive intent.
	bool m_keep_alive = false;

	// the mode and, for write-only mode, the write/drain lifecycle of this
	// connection, tracked as a single state machine. on_drain() only reacts
	// to the peer closing while idle (see on_drain()); the drain loop starts
	// on the first write's completion (see on_write()).
	enum class write_only_state_t : std::uint8_t
	{
		// normal (response-reading) mode: fire-and-forget write-only
		// semantics do not apply, and none of the other states below are
		// ever entered.
		normal_mode,
		// write-only mode; nothing dispatched yet this connection cycle, or
		// the peer was found dead and this connection is about to reconnect
		// from scratch. The drain loop is not running.
		not_started,
		// the very first write of this cycle is outstanding; the drain loop
		// starts once it completes (see on_write()).
		first_write,
		// the drain loop is running and a later, promoted write is
		// outstanding. on_drain() must not react to the peer closing while
		// here: this write's own completion (on_write(), success or
		// failure) already drives the next step.
		write_in_flight,
		// the drain loop is running and idle: nothing outstanding, waiting
		// for either a promoted follow-up write (-> write_in_flight) or for
		// the drain loop to observe the peer's own close.
		idle,
	};
	write_only_state_t m_write_only_state = write_only_state_t::normal_mode;
};

}

#endif
