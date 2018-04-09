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

#include "libtorrent/lsd.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/socket_io.hpp" // for print_address

#if defined TORRENT_ASIO_DEBUGGING
#include "libtorrent/debug.hpp"
#endif

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/bind.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/ip/multicast.hpp>
#include <cstdlib>
#include <boost/config.hpp>
#include <cstdarg>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent
{
namespace {

int render_lsd_packet(char* dst, int const len, int const listen_port
	, char const* info_hash_hex, int const cookie, char const* host)
{
	TORRENT_ASSERT(len > 0);
	return snprintf(dst, len,
		"BT-SEARCH * HTTP/1.1\r\n"
		"Host: %s:6771\r\n"
		"Port: %d\r\n"
		"Infohash: %s\r\n"
		"cookie: %x\r\n"
		"\r\n\r\n", host, listen_port, info_hash_hex, cookie);
}
} // anonymous namespace

static error_code dummy;

lsd::lsd(io_service& ios, peer_callback_t const& cb
#ifndef TORRENT_DISABLE_LOGGING
	, log_callback_t const& log
#endif
	)
	: m_callback(cb)
	, m_socket(udp::endpoint(address_v4::from_string("239.192.152.143", dummy), 6771))
#if TORRENT_USE_IPV6
	, m_socket6(udp::endpoint(address_v6::from_string("ff15::efc0:988f", dummy), 6771))
#endif
#ifndef TORRENT_DISABLE_LOGGING
	, m_log_cb(log)
#endif
	, m_broadcast_timer(ios)
	, m_cookie((random() ^ uintptr_t(this)) & 0x7fffffff)
	, m_disabled(false)
#if TORRENT_USE_IPV6
	, m_disabled6(false)
#endif
{
}

#ifndef TORRENT_DISABLE_LOGGING
TORRENT_FORMAT(2,3)
void lsd::debug_log(char const* fmt, ...) const
{
	va_list v;
	va_start(v, fmt);

	char buf[1024];
	vsnprintf(buf, sizeof(buf), fmt, v);
	va_end(v);
	m_log_cb(buf);
}
#endif

void lsd::start(error_code& ec)
{
	m_socket.open(boost::bind(&lsd::on_announce, self(), _1, _2, _3)
		, m_broadcast_timer.get_io_service(), ec);
	if (ec) return;

#if TORRENT_USE_IPV6
	m_socket6.open(boost::bind(&lsd::on_announce, self(), _1, _2, _3)
		, m_broadcast_timer.get_io_service(), ec);
#endif
}

lsd::~lsd() {}

void lsd::announce(sha1_hash const& ih, int listen_port, bool broadcast)
{
	announce_impl(ih, listen_port, broadcast, 0);
}

void lsd::announce_impl(sha1_hash const& ih, int const listen_port
	, bool const broadcast, int retry_count)
{
#if TORRENT_USE_IPV6
	if (m_disabled && m_disabled6) return;
#else
	if (m_disabled) return;
#endif

	char ih_hex[41];
	to_hex(ih.data(), 20, ih_hex);
	char msg[200];

#ifndef TORRENT_DISABLE_LOGGING
	debug_log("==> LSD: ih: %s port: %u\n", ih_hex, listen_port);
#endif

	error_code ec;
	if (!m_disabled)
	{
		int const msg_len = render_lsd_packet(msg, sizeof(msg), listen_port, ih_hex
			, m_cookie, "239.192.152.143");
		m_socket.send(msg, msg_len, ec, broadcast ? broadcast_socket::broadcast : 0);
		if (ec)
		{
			m_disabled = true;
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("*** LSD: failed to send message: (%d) %s", ec.value()
				, ec.message().c_str());
#endif
		}
	}

#if TORRENT_USE_IPV6
	if (!m_disabled6)
	{
		int const msg_len = render_lsd_packet(msg, sizeof(msg), listen_port, ih_hex
			, m_cookie, "[ff15::efc0:988f]");
		m_socket6.send(msg, msg_len, ec, broadcast ? broadcast_socket::broadcast : 0);
		if (ec)
		{
			m_disabled6 = true;
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("*** LSD: failed to send message6: (%d) %s", ec.value()
				, ec.message().c_str());
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

void lsd::on_announce(udp::endpoint const& from, char* buf
	, std::size_t bytes_transferred)
{
	using namespace libtorrent::detail;

	http_parser p;

	bool error = false;
	p.incoming(buffer::const_interval(buf, buf + bytes_transferred)
		, error);

	if (!p.header_finished() || error)
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("<== LSD: incomplete HTTP message");
#endif
		return;
	}

	if (p.method() != "bt-search")
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("<== LSD: invalid HTTP method: %s", p.method().c_str());
#endif
		return;
	}

	std::string const& port_str = p.header("port");
	if (port_str.empty())
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("<== LSD: invalid BT-SEARCH, missing port");
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
		boost::int32_t const cookie = strtol(cookie_iter->second.c_str(), NULL, 16);
		if (cookie == m_cookie)
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("<== LSD: ignoring packet (cookie matched our own): %x"
				, cookie);
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
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("<== LSD: invalid BT-SEARCH, invalid infohash: %s"
				, ih_str.c_str());
#endif
			continue;
		}

		sha1_hash ih(0);
		from_hex(ih_str.c_str(), 40, ih.data());

		if (!ih.is_all_zeros() && port != 0)
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("<== LSD: %s:%d ih: %s"
				, print_address(from.address()).c_str()
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

} // libtorrent namespace


