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
#include <string>

#include "libtorrent/socket.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{

struct http_connection;
	
typedef boost::function<void(asio::error_code const&
	, http_parser const&, char const* data, int size, http_connection&)> http_handler;

typedef boost::function<void(http_connection&)> http_connect_handler;

// TODO: add bind interface

// when bottled, the last two arguments to the handler
// will always be 0
struct http_connection : boost::enable_shared_from_this<http_connection>, boost::noncopyable
{
	http_connection(asio::io_service& ios, connection_queue& cc
		, http_handler const& handler, bool bottled = true
		, http_connect_handler const& ch = http_connect_handler())
		: m_sock(ios)
		, m_read_pos(0)
		, m_resolver(ios)
		, m_handler(handler)
		, m_connect_handler(ch)
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
	{
		TORRENT_ASSERT(!m_handler.empty());
	}

	void rate_limit(int limit);

	int rate_limit() const
	{ return m_rate_limit; }

	std::string sendbuffer;

	void get(std::string const& url, time_duration timeout = seconds(30)
		, int handle_redirects = 5);

	void start(std::string const& hostname, std::string const& port
		, time_duration timeout, int handle_redirect = 5);
	void close();

	tcp::socket const& socket() const { return m_sock; }

private:

	void on_resolve(asio::error_code const& e
		, tcp::resolver::iterator i);
	void connect(int ticket, tcp::endpoint target_address);
	void on_connect_timeout();
	void on_connect(asio::error_code const& e
/*		, tcp::resolver::iterator i*/);
	void on_write(asio::error_code const& e);
	void on_read(asio::error_code const& e, std::size_t bytes_transferred);
	static void on_timeout(boost::weak_ptr<http_connection> p
		, asio::error_code const& e);
	void on_assign_bandwidth(asio::error_code const& e);

	void callback(asio::error_code const& e, char const* data = 0, int size = 0);

	std::vector<char> m_recvbuffer;
	tcp::socket m_sock;
	int m_read_pos;
	tcp::resolver m_resolver;
	http_parser m_parser;
	http_handler m_handler;
	http_connect_handler m_connect_handler;
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
};

}

#endif
