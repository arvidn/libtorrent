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
#include "libtorrent/xml_parse.hpp"
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <asio/ip/host_name.hpp>
#include <asio/ip/multicast.hpp>
#include <boost/thread/mutex.hpp>
#include <cstdlib>
#include <boost/config.hpp>

using boost::bind;
using namespace libtorrent;

namespace libtorrent
{
	// defined in upnp.cpp
	address_v4 guess_local_address(asio::io_service&);
}

address_v4 lsd::lsd_multicast_address;
udp::endpoint lsd::lsd_multicast_endpoint;

lsd::lsd(io_service& ios, address const& listen_interface
	, peer_callback_t const& cb)
	: m_callback(cb)
	, m_retry_count(0)
	, m_socket(ios)
	, m_broadcast_timer(ios)
	, m_disabled(false)
{
	// Bittorrent Local discovery multicast address and port
	lsd_multicast_address = address_v4::from_string("239.192.152.143");
	lsd_multicast_endpoint = udp::endpoint(lsd_multicast_address, 6771);

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log.open("lsd.log", std::ios::in | std::ios::out | std::ios::trunc);
#endif
	assert(lsd_multicast_address.is_multicast());
	rebind(listen_interface);
}

lsd::~lsd() {}

void lsd::rebind(address const& listen_interface)
{
	address_v4 local_ip = address_v4::any();
	if (listen_interface.is_v4() && listen_interface != address_v4::any())
	{
		local_ip = listen_interface.to_v4();
	}

	try
	{
		// the local interface hasn't changed
		if (m_socket.is_open()
			&& m_socket.local_endpoint().address() == local_ip)
			return;
		
		m_socket.close();
		
		using namespace asio::ip::multicast;

		m_socket.open(udp::v4());
		m_socket.set_option(datagram_socket::reuse_address(true));
		m_socket.bind(udp::endpoint(local_ip, 6771));

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << "local ip: " << local_ip << std::endl;
#endif

		m_socket.set_option(join_group(lsd_multicast_address));
		m_socket.set_option(outbound_interface(local_ip));
		m_socket.set_option(enable_loopback(true));
		m_socket.set_option(hops(255));
	}
	catch (std::exception& e)
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << "socket multicast error " << e.what()
			<< ". disabling local service discovery" << std::endl;
#endif
		m_disabled = true;
		return;
	}
	m_disabled = false;

	setup_receive();
}

void lsd::announce(sha1_hash const& ih, int listen_port)
{
	if (m_disabled) return;

	std::stringstream btsearch;
	btsearch << "BT-SEARCH * HTTP/1.1\r\n"
		"Host: 239.192.152.143:6771\r\n"
		"Port: " << listen_port << "\r\n"
		"Infohash: " << ih << "\r\n"
		"\r\n\r\n";
	std::string const& msg = btsearch.str();

	m_retry_count = 0;
	asio::error_code ec;
	m_socket.send_to(asio::buffer(msg.c_str(), msg.size() - 1)
		, lsd_multicast_endpoint, 0, ec);
	if (ec)
	{
		m_disabled = true;
		return;
	}

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << time_now_string()
		<< " ==> announce: ih: " << ih << " port: " << listen_port << std::endl;
#endif

	m_broadcast_timer.expires_from_now(milliseconds(250 * m_retry_count));
	m_broadcast_timer.async_wait(bind(&lsd::resend_announce, this, _1, msg));
}

void lsd::resend_announce(asio::error_code const& e, std::string msg) try
{
	if (e) return;

	m_socket.send_to(asio::buffer(msg, msg.size() - 1)
		, lsd_multicast_endpoint);

	++m_retry_count;
	if (m_retry_count >= 5)
		return;

	m_broadcast_timer.expires_from_now(milliseconds(250 * m_retry_count));
	m_broadcast_timer.async_wait(bind(&lsd::resend_announce, this, _1, msg));
}
catch (std::exception&)
{}

void lsd::on_announce(asio::error_code const& e
	, std::size_t bytes_transferred)
{
	using namespace libtorrent::detail;
	if (e) return;

	char* p = m_receive_buffer;
	char* end = m_receive_buffer + bytes_transferred;
	char* line = std::find(p, end, '\n');
	for (char* i = p; i < line; ++i) *i = std::tolower(*i);
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << time_now_string()
		<< " <== announce: " << std::string(p, line) << std::endl;
#endif
	if (line == end || (line - p >= 9 && std::memcmp("bt-search", p, 9)))
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << time_now_string()
			<< " *** assumed 'bt-search', ignoring" << std::endl;
#endif
		setup_receive();
		return;
	}
	p = line + 1;
	int port = 0;
	sha1_hash ih(0);
	while (p != end)
	{
		line = std::find(p, end, '\n');
		if (line == end) break;
		*line = 0;
		for (char* i = p; i < line; ++i) *i = std::tolower(*i);
		if (line - p >= 5 && memcmp(p, "port:", 5) == 0)
		{
			p += 5;
			while (*p == ' ') ++p;
			port = atoi(p);
		}
		else if (line - p >= 9 && memcmp(p, "infohash:", 9) == 0)
		{
			p += 9;
			while (*p == ' ') ++p;
			if (line - p > 40) p[40] = 0;
			try { ih = boost::lexical_cast<sha1_hash>(p); }
			catch (std::exception&) {}
		}
		p = line + 1;
	}

	if (!ih.is_all_zeros() && port != 0)
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << time_now_string()
			<< " *** incoming local announce " << m_remote.address()
			<< ":" << port << " ih: " << ih << std::endl;
#endif
		// we got an announce, pass it on through the callback
		try { m_callback(tcp::endpoint(m_remote.address(), port), ih); }
		catch (std::exception&) {}
	}
	setup_receive();
}

void lsd::setup_receive() try
{
	assert(m_socket.is_open());
	m_socket.async_receive_from(asio::buffer(m_receive_buffer
		, sizeof(m_receive_buffer)), m_remote, bind(&lsd::on_announce, this, _1, _2));
}
catch (std::exception&)
{}

void lsd::close()
{
	m_socket.close();
}

