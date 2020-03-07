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

#include <cstdlib>
#include <cstdarg>
#include <functional>
#include <cstdio> // for vsnprintf

#include "libtorrent/lsd.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/socket_io.hpp" // for print_address
#include "libtorrent/debug.hpp"
#include "libtorrent/hex.hpp" // to_hex, from_hex
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/enum_net.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ip/multicast.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

using namespace std::placeholders;

namespace libtorrent {

namespace {

int render_lsd_packet(char* dst, int const len, int const listen_port
	, char const* info_hash_hex, int const cookie, char const* host)
{
	TORRENT_ASSERT(len > 0);
	return std::snprintf(dst, aux::numeric_cast<std::size_t>(len),
		"BT-SEARCH * HTTP/1.1\r\n"
		"Host: %s:6771\r\n"
		"Port: %d\r\n"
		"Infohash: %s\r\n"
		"cookie: %x\r\n"
		"\r\n\r\n", host, listen_port, info_hash_hex, cookie);
}
} // anonymous namespace

lsd::lsd(io_service& ios, aux::lsd_callback& cb
	, address const& listen_address, address const& netmask)
	: m_callback(cb)
	, m_listen_address(listen_address)
	, m_netmask(netmask)
	, m_socket(ios)
	, m_broadcast_timer(ios)
	, m_cookie((random(0x7fffffff) ^ std::uintptr_t(this)) & 0x7fffffff)
{
}

#ifndef TORRENT_DISABLE_LOGGING
bool lsd::should_log() const
{
	return m_callback.should_log_lsd();
}

TORRENT_FORMAT(2, 3)
void lsd::debug_log(char const* fmt, ...) const
{
	if (!should_log()) return;
	va_list v;
	va_start(v, fmt);

	char buf[1024];
	std::vsnprintf(buf, sizeof(buf), fmt, v);
	va_end(v);
	m_callback.log_lsd(buf);
}
#endif

namespace {
	address_v4 const lsd_multicast_addr4 = make_address_v4("239.192.152.143");
	address_v6 const lsd_multicast_addr6 = make_address_v6("ff15::efc0:988f");
	int const lsd_port = 6771;
}

void lsd::start(error_code& ec)
{
	using namespace boost::asio::ip::multicast;
	bool const v4 = m_listen_address.is_v4();
	m_socket.open(v4 ? udp::v4() : udp::v6(), ec);
	if (ec) return;

	m_socket.set_option(udp::socket::reuse_address(true), ec);
	if (ec) return;

	m_socket.bind(udp::endpoint(v4 ? address(address_v4::any()) : address(address_v6::any()), lsd_port), ec);
	if (ec) return;
	if (v4)
		m_socket.set_option(join_group(lsd_multicast_addr4, m_listen_address.to_v4()), ec);
	else
		m_socket.set_option(join_group(lsd_multicast_addr6, m_listen_address.to_v6().scope_id()), ec);
	if (ec) return;
	m_socket.set_option(hops(32), ec);
	if (ec) return;
	m_socket.set_option(enable_loopback(true), ec);
	if (ec) return;
	if (v4)
	{
		m_socket.set_option(outbound_interface(m_listen_address.to_v4()), ec);
		if (ec) return;
	}

	ADD_OUTSTANDING_ASYNC("lsd::on_announce");
	m_socket.async_receive(boost::asio::null_buffers{}
		, std::bind(&lsd::on_announce, self(), _1));
}

lsd::~lsd() = default;

void lsd::announce(sha1_hash const& ih, int listen_port)
{
	announce_impl(ih, listen_port, 0);
}

void lsd::announce_impl(sha1_hash const& ih, int const listen_port
	, int retry_count)
{
	if (m_disabled) return;

	char msg[200];

	error_code ec;
	if (!m_disabled)
	{
		bool const v4 = m_listen_address.is_v4();
		char const* v4_address = "239.192.152.143";
		char const* v6_address = "[ff15::efc0:988f]";

		int const msg_len = render_lsd_packet(msg, sizeof(msg), listen_port, aux::to_hex(ih).c_str()
			, m_cookie, v4 ? v4_address : v6_address);

		udp::endpoint const to(v4 ? address(lsd_multicast_addr4) : address(lsd_multicast_addr6)
			, lsd_port);

#ifndef TORRENT_DISABLE_LOGGING
		debug_log("==> LSD: ih: %s port: %u [iface: %s]", aux::to_hex(ih).c_str()
			, listen_port, m_listen_address.to_string().c_str());
#endif

		m_socket.send_to(boost::asio::buffer(msg, static_cast<std::size_t>(msg_len))
			, to, {}, ec);
		if (ec)
		{
			m_disabled = true;
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				debug_log("*** LSD: failed to send message: (%d) %s", ec.value()
					, ec.message().c_str());
			}
#endif
		}
	}

	++retry_count;
	if (retry_count >= 3) return;

	if (m_disabled) return;

	ADD_OUTSTANDING_ASYNC("lsd::resend_announce");
	m_broadcast_timer.expires_from_now(seconds(2 * retry_count), ec);
	m_broadcast_timer.async_wait(std::bind(&lsd::resend_announce, self(), _1
		, ih, listen_port, retry_count));
}

void lsd::resend_announce(error_code const& e, sha1_hash const& info_hash
	, int listen_port, int retry_count)
{
	COMPLETE_ASYNC("lsd::resend_announce");
	if (e) return;

	announce_impl(info_hash, listen_port, retry_count);
}

void lsd::on_announce(error_code const& ec)
{
	COMPLETE_ASYNC("lsd::on_announce");
	if (ec) return;

	std::array<char, 1500> buffer;
	udp::endpoint from;
	error_code err;
	int const len = static_cast<int>(m_socket.receive_from(
		boost::asio::buffer(buffer), from, {}, err));

	ADD_OUTSTANDING_ASYNC("lsd::on_announce");
	m_socket.async_receive(boost::asio::null_buffers{}
		, std::bind(&lsd::on_announce, self(), _1));

	if (!match_addr_mask(from.address(), m_listen_address, m_netmask))
	{
		// we don't care about this network. Ignore this packet
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("<== LSD: receive from out of network: %s"
			, from.address().to_string().c_str());
#endif
		return;
	}

	if (err)
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("<== LSD: receive error: %s", err.message().c_str());
#endif
		return;
	}

	http_parser p;

	bool error = false;
	p.incoming(span<char const>{buffer.data(), len}, error);

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

	long const port = std::strtol(port_str.c_str(), nullptr, 10);
	if (port <= 0 || port >= int(std::numeric_limits<std::uint16_t>::max()))
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("<== LSD: invalid BT-SEARCH port value: %s", port_str.c_str());
#endif
		return;
	}

	auto const& headers = p.headers();

	auto const cookie_iter = headers.find("cookie");
	if (cookie_iter != headers.end())
	{
		// we expect it to be hexadecimal
		// if it isn't, it's not our cookie anyway
		long const cookie = std::strtol(cookie_iter->second.c_str(), nullptr, 16);
		if (cookie == m_cookie)
		{
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("<== LSD: ignoring packet (cookie matched our own): %x"
				, m_cookie);
#endif
			return;
		}
	}

	auto const ihs = headers.equal_range("infohash");
	for (auto i = ihs.first; i != ihs.second; ++i)
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

		sha1_hash ih;
		aux::from_hex(ih_str, ih.data());

		if (!ih.is_all_zeros())
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				debug_log("<== LSD: %s:%d ih: %s"
					, print_address(from.address()).c_str()
					, int(port), ih_str.c_str());
			}
#endif
			// we got an announce, pass it on through the callback
			m_callback.on_lsd_peer(tcp::endpoint(from.address(), std::uint16_t(port)), ih);
		}
	}
}

void lsd::close()
{
	error_code ec;
	m_socket.close(ec);
	m_broadcast_timer.cancel(ec);
	m_disabled = true;
}

} // libtorrent namespace
