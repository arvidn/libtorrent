/*

Copyright (c) 2007, Arvid Norberg
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

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/noncopyable.hpp>
#include <vector>
#include <list>
#include <string>

#include "libtorrent/socket.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/socket_type.hpp"
#include "libtorrent/session_settings.hpp"

#ifdef TORRENT_USE_OPENSSL
#include "libtorrent/ssl_stream.hpp"
#include "libtorrent/variant_stream.hpp"
#endif

namespace libtorrent
{

struct http_connection;
class connection_queue;
	
typedef boost::function<void(error_code const&
	, http_parser const&, char const* data, int size, http_connection&)> http_handler;

typedef boost::function<void(http_connection&)> http_connect_handler;

typedef boost::function<void(http_connection&, std::list<tcp::endpoint>&)> http_filter_handler;

// when bottled, the last two arguments to the handler
// will always be 0
struct TORRENT_EXPORT http_connection : boost::enable_shared_from_this<http_connection>, boost::noncopyable
{
	http_connection(io_service& ios, connection_queue& cc
		, http_handler const& handler, bool bottled = true
		, http_connect_handler const& ch = http_connect_handler()
		, http_filter_handler const& fh = http_filter_handler())
		: m_sock(ios)
		, m_read_pos(0)
		, m_resolver(ios)
		, m_handler(handler)
		, m_connect_handler(ch)
		, m_filter_handler(fh)
		, m_timer(ios)
		, m_last_receive(time_now())
		, m_bottled(bottled)
		, m_called(false)
		, m_rate_limit(0)
		, m_download_quota(0)
		, m_limiter_timer_active(false)
		, m_limiter_timer(ios)
		, m_redirects(5)
		, m_connection_ticket(-1)
		, m_cc(cc)
		, m_ssl(false)
		, m_priority(0)
		, m_abort(false)
	{
		TORRENT_ASSERT(!m_handler.empty());
	}

	void rate_limit(int limit);

	int rate_limit() const
	{ return m_rate_limit; }

	std::string sendbuffer;

	void get(std::string const& url, time_duration timeout = seconds(30)
		, int prio = 0, proxy_settings const* ps = 0, int handle_redirects = 5
		, std::string const& user_agent = "", address const& bind_addr = address_v4::any());

	void start(std::string const& hostname, std::string const& port
		, time_duration timeout, int prio = 0, proxy_settings const* ps = 0
		, bool ssl = false, int handle_redirect = 5
		, address const& bind_addr = address_v4::any());

	void close();

#ifdef TORRENT_USE_OPENSSL
	variant_stream<socket_type, ssl_stream<socket_type> > const& socket() const { return m_sock; }
#else
	socket_type const& socket() const { return m_sock; }
#endif

	std::list<tcp::endpoint> const& endpoints() const { return m_endpoints; }
	
private:

	void on_resolve(error_code const& e
		, tcp::resolver::iterator i);
	void queue_connect();
	void connect(int ticket, tcp::endpoint target_address);
	void on_connect_timeout();
	void on_connect(error_code const& e);
	void on_write(error_code const& e);
	void on_read(error_code const& e, std::size_t bytes_transferred);
	static void on_timeout(boost::weak_ptr<http_connection> p
		, error_code const& e);
	void on_assign_bandwidth(error_code const& e);

	void callback(error_code const& e, char const* data = 0, int size = 0);

	std::vector<char> m_recvbuffer;
#ifdef TORRENT_USE_OPENSSL
	variant_stream<socket_type, ssl_stream<socket_type> > m_sock;
#else
	socket_type m_sock;
#endif
	int m_read_pos;
	tcp::resolver m_resolver;
	http_parser m_parser;
	http_handler m_handler;
	http_connect_handler m_connect_handler;
	http_filter_handler m_filter_handler;
	deadline_timer m_timer;
	time_duration m_timeout;
	ptime m_last_receive;
	// bottled means that the handler is called once, when
	// everything is received (and buffered in memory).
	// non bottled means that once the headers have been
	// received, data is streamed to the handler
	bool m_bottled;
	// set to true the first time the handler is called
	bool m_called;
	std::string m_hostname;
	std::string m_port;
	std::string m_url;

	std::list<tcp::endpoint> m_endpoints;

	// the current download limit, in bytes per second
	// 0 is unlimited.
	int m_rate_limit;

	// the number of bytes we are allowed to receive
	int m_download_quota;

	// only hand out new quota 4 times a second if the
	// quota is 0. If it isn't 0 wait for it to reach
	// 0 and continue to hand out quota at that time.
	bool m_limiter_timer_active;

	// the timer fires every 250 millisecond as long
	// as all the quota was used.
	deadline_timer m_limiter_timer;

	// the number of redirects to follow (in sequence)
	int m_redirects;

	int m_connection_ticket;
	connection_queue& m_cc;

	// specifies whether or not the connection is
	// configured to use a proxy
	proxy_settings m_proxy;

	// true if the connection is using ssl
	bool m_ssl;

	// the address to bind to. address_v4::any()
	// means do not bind
	address m_bind_addr;

	// the priority we have in the connection queue.
	// 0 is normal, 1 is high
	int m_priority;

	bool m_abort;
};

}

#endif
