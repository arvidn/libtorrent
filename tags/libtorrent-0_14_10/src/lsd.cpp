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

#include "libtorrent/pch.hpp"

#include "libtorrent/lsd.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/http_parser.hpp"
#include "libtorrent/escape_string.hpp"

#include <boost/bind.hpp>
#include <boost/ref.hpp>
#if BOOST_VERSION < 103500
#include <asio/ip/host_name.hpp>
#include <asio/ip/multicast.hpp>
#else
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/ip/multicast.hpp>
#endif
#include <boost/thread/mutex.hpp>
#include <cstdlib>
#include <boost/config.hpp>

using boost::bind;
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
	, m_retry_count(1)
	, m_socket(ios, udp::endpoint(address_v4::from_string("239.192.152.143", ec), 6771)
		, bind(&lsd::on_announce, self(), _1, _2, _3))
	, m_broadcast_timer(ios)
	, m_disabled(false)
{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log.open("lsd.log", std::ios::in | std::ios::out | std::ios::trunc);
#endif
}

lsd::~lsd() {}

void lsd::announce(sha1_hash const& ih, int listen_port)
{
	if (m_disabled) return;

	std::stringstream btsearch;
	btsearch << "BT-SEARCH * HTTP/1.1\r\n"
		"Host: 239.192.152.143:6771\r\n"
		"Port: " << to_string(listen_port).elems << "\r\n"
		"Infohash: " << ih << "\r\n"
		"\r\n\r\n";
	std::string const& msg = btsearch.str();

	m_retry_count = 1;
	error_code ec;
	m_socket.send(msg.c_str(), int(msg.size()), ec);
	if (ec)
	{
		m_disabled = true;
		return;
	}

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << time_now_string()
		<< " ==> announce: ih: " << ih << " port: " << to_string(listen_port).elems << std::endl;
#endif

	m_broadcast_timer.expires_from_now(milliseconds(250 * m_retry_count), ec);
	m_broadcast_timer.async_wait(boost::bind(&lsd::resend_announce, self(), _1, msg));
}

void lsd::resend_announce(error_code const& e, std::string msg)
{
	if (e) return;

	error_code ec;
	m_socket.send(msg.c_str(), int(msg.size()), ec);

	++m_retry_count;
	if (m_retry_count >= 5)
		return;

	m_broadcast_timer.expires_from_now(milliseconds(250 * m_retry_count), ec);
	m_broadcast_timer.async_wait(boost::bind(&lsd::resend_announce, self(), _1, msg));
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
	m_log << time_now_string()
		<< " <== announce: incomplete HTTP message\n";
#endif
		return;
	}

	if (p.method() != "bt-search")
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << time_now_string()
		<< " <== announce: invalid HTTP method: " << p.method() << std::endl;
#endif
		return;
	}

	std::string const& port_str = p.header("port");
	if (port_str.empty())
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << time_now_string()
		<< " <== announce: invalid BT-SEARCH, missing port" << std::endl;
#endif
		return;
	}

	std::string const& ih_str = p.header("infohash");
	if (ih_str.empty())
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << time_now_string()
		<< " <== announce: invalid BT-SEARCH, missing infohash" << std::endl;
#endif
		return;
	}

	sha1_hash ih(0);
	std::istringstream ih_sstr(ih_str);
	ih_sstr >> ih;
	int port = std::atoi(port_str.c_str());

	if (!ih.is_all_zeros() && port != 0)
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << time_now_string()
			<< " *** incoming local announce " << from.address()
			<< ":" << to_string(port).elems << " ih: " << ih << std::endl;
#endif
		// we got an announce, pass it on through the callback
#ifndef BOOST_NO_EXCEPTIONS
		try {
#endif
			m_callback(tcp::endpoint(from.address(), port), ih);
#ifndef BOOST_NO_EXCEPTIONS
		}
		catch (std::exception&) {}
#endif
	}
}

void lsd::close()
{
	m_socket.close();
	error_code ec;
	m_broadcast_timer.cancel(ec);
	m_disabled = true;
	m_callback.clear();
}

