/*

Copyright (c) 2007-2014, Arvid Norberg
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

#include "libtorrent/lsd.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/socket_io.hpp" // for print_address

#if defined TORRENT_ASIO_DEBUGGING
#include "libtorrent/debug.hpp"
#endif

#include <boost/bind.hpp>
#include <boost/ref.hpp>
#if BOOST_VERSION < 103500
#include <asio/ip/host_name.hpp>
#include <asio/ip/multicast.hpp>
#else
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/ip/multicast.hpp>
#endif
#include <cstdlib>
#include <boost/config.hpp>

using namespace libtorrent;

namespace libtorrent
{
	// defined in broadcast_socket.cpp
	address guess_local_address(io_service&);
}

static error_code ec;

lsd::lsd(io_service& ios, address const& listen_interface
	, peer_callback_t const& cb)
	: m_callback(cb)
	, m_socket(udp::endpoint(address_v4::from_string("239.192.152.143", ec), 6771)
		, boost::bind(&lsd::on_announce, self(), _1, _2, _3))
#if TORRENT_USE_IPV6
	, m_socket6(udp::endpoint(address_v6::from_string("ff15::efc0:988f", ec), 6771)
		, boost::bind(&lsd::on_announce, self(), _1, _2, _3))
#endif
	, m_broadcast_timer(ios)
	, m_cookie(random())
	, m_disabled(false)
#if TORRENT_USE_IPV6
	, m_disabled6(false)
#endif
{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log = fopen("lsd.log", "w+");
	if (m_log == NULL)
	{
		fprintf(stderr, "failed to open 'lsd.log': (%d) %s"
			, errno, strerror(errno));
	}
#endif

	error_code ec;
	m_socket.open(ios, ec);

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	if (ec)
	{
		if (m_log) fprintf(m_log, "FAILED TO OPEN SOCKET: (%d) %s\n"
			, ec.value(), ec.message().c_str());
	}
#endif

#if TORRENT_USE_IPV6
	m_socket6.open(ios, ec);
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	if (ec)
	{
		if (m_log) fprintf(m_log, "FAILED TO OPEN SOCKET6: (%d) %s\n"
			, ec.value(), ec.message().c_str());
	}
#endif
#endif
}

lsd::~lsd()
{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	if (m_log) fclose(m_log);
#endif
}

int render_lsd_packet(char* dst, int len, int listen_port
	, char const* info_hash_hex, int m_cookie, char const* host)
{
	return snprintf(dst, len,
		"BT-SEARCH * HTTP/1.1\r\n"
		"Host: %s:6771\r\n"
		"Port: %d\r\n"
		"Infohash: %s\r\n"
		"cookie: %x\r\n"
		"\r\n\r\n", host, listen_port, info_hash_hex, m_cookie);
}

void lsd::announce(sha1_hash const& ih, int listen_port, bool broadcast)
{
	announce_impl(ih, listen_port, broadcast, 0);
}

void lsd::announce_impl(sha1_hash const& ih, int listen_port, bool broadcast
	, int retry_count)
{
#if TORRENT_USE_IPV6
	if (m_disabled && m_disabled6) return;
#else
	if (m_disabled) return;
#endif

	char ih_hex[41];
	to_hex((char const*)&ih[0], 20, ih_hex);
	char msg[200];

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	if (m_log) fprintf(m_log, "%s ==> announce: ih: %s port: %u\n"
		, time_now_string(), ih_hex, listen_port);
#endif

	error_code ec;
	if (!m_disabled)
	{
		int msg_len = render_lsd_packet(msg, sizeof(msg), listen_port, ih_hex
			, m_cookie, "239.192.152.143");
		m_socket.send(msg, msg_len, ec, broadcast ? broadcast_socket::broadcast : 0);
		if (ec)
		{
			m_disabled = true;
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
			if (m_log) fprintf(m_log, "%s failed to send message: (%d) %s"
				, time_now_string(), ec.value(), ec.message().c_str());
#endif
		}
	}

#if TORRENT_USE_IPV6
	if (!m_disabled6)
	{
		int msg_len = render_lsd_packet(msg, sizeof(msg), listen_port, ih_hex
			, m_cookie, "[ff15::efc0:988f]");
		m_socket6.send(msg, msg_len, ec, broadcast ? broadcast_socket::broadcast : 0);
		if (ec)
		{
			m_disabled6 = true;
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
			if (m_log) fprintf(m_log, "%s failed to send message6: (%d) %s"
				, time_now_string(), ec.value(), ec.message().c_str());
#endif
		}
	}
#endif

	++retry_count;
	if (retry_count >= 3) return;

#if TORRENT_USE_IPV6
	if (m_disabled && m_disabled6) return;
#else
	if (m_disabled) return;
#endif

#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("lsd::resend_announce");
#endif
	m_broadcast_timer.expires_from_now(seconds(2 * retry_count), ec);
	m_broadcast_timer.async_wait(boost::bind(&lsd::resend_announce, self(), _1
		, ih, listen_port, retry_count));
}

void lsd::resend_announce(error_code const& e, sha1_hash const& info_hash
	, int listen_port, int retry_count)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("lsd::resend_announce");
#endif
	if (e) return;

	announce_impl(info_hash, listen_port, false, retry_count);
}

void lsd::on_announce(udp::endpoint const& from, char* buffer
	, std::size_t bytes_transferred)
{
	using namespace libtorrent::detail;

	http_parser p;

	bool error = false;
	p.incoming(buffer::const_interval(buffer, buffer + bytes_transferred)
		, error);

	if (!p.header_finished() || error)
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		if (m_log) fprintf(m_log, "%s <== announce: incomplete HTTP message\n", time_now_string());
#endif
		return;
	}

	if (p.method() != "bt-search")
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		if (m_log) fprintf(m_log, "%s <== announce: invalid HTTP method: %s\n"
			, time_now_string(), p.method().c_str());
#endif
		return;
	}

	std::string const& port_str = p.header("port");
	if (port_str.empty())
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		if (m_log) fprintf(m_log, "%s <== announce: invalid BT-SEARCH, missing port\n"
			, time_now_string());
#endif
		return;
	}

	int port = std::atoi(port_str.c_str());

	typedef std::multimap<std::string, std::string> headers_t;
	headers_t const& headers = p.headers();

	headers_t::const_iterator cookie_iter = headers.find("cookie");
	if (cookie_iter != headers.end())
	{
		// we expect it to be hexadecimal
		// if it isn't, it's not our cookie anyway
		boost::int32_t cookie = strtol(cookie_iter->second.c_str(), NULL, 16);
		if (cookie == m_cookie)
		{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
			if (m_log) fprintf(m_log, "%s <== announce: ignoring packet (cookie matched our own): %x == %x\n"
				, time_now_string(), cookie, m_cookie);
#endif
			return;
		}
	}

	std::pair<headers_t::const_iterator, headers_t::const_iterator> ihs
		= headers.equal_range("infohash");

	for (headers_t::const_iterator i = ihs.first; i != ihs.second; ++i)
	{
		std::string const& ih_str = i->second;
		if (ih_str.size() != 40)
		{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
			if (m_log) fprintf(m_log, "%s <== announce: invalid BT-SEARCH, invalid infohash: %s\n"
				, time_now_string(), ih_str.c_str());
#endif
			continue;
		}

		sha1_hash ih(0);
		from_hex(ih_str.c_str(), 40, (char*)&ih[0]);

		if (!ih.is_all_zeros() && port != 0)
		{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
			if (m_log) fprintf(m_log, "%s *** incoming local announce %s:%d ih: %s\n"
				, time_now_string(), print_address(from.address()).c_str()
				, port, ih_str.c_str());
#endif
			// we got an announce, pass it on through the callback
			TORRENT_TRY {
				m_callback(tcp::endpoint(from.address(), port), ih);
			} TORRENT_CATCH(std::exception&) {}
		}
	}
}

void lsd::close()
{
	m_socket.close();
#if TORRENT_USE_IPV6
	m_socket6.close();
#endif
	error_code ec;
	m_broadcast_timer.cancel(ec);
	m_disabled = true;
#if TORRENT_USE_IPV6
	m_disabled6 = true;
#endif
	m_callback.clear();
}

