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
#include <boost/date_time/posix_time/posix_time.hpp>
#include <asio/ip/host_name.hpp>
#include <asio/ip/multicast.hpp>
#include <boost/thread/mutex.hpp>
#include <cstdlib>

using boost::bind;
using namespace libtorrent;
using boost::posix_time::microsec_clock;
using boost::posix_time::milliseconds;
using boost::posix_time::seconds;
using boost::posix_time::second_clock;
using boost::posix_time::ptime;

namespace
{
	// Bittorrent Local discovery multicast address and port
	address_v4 multicast_address = address_v4::from_string("239.192.152.143");
	udp::endpoint multicast_endpoint(multicast_address, 6771);
}

lsd::lsd(io_service& ios, address const& listen_interface
	, peer_callback_t const& cb)
	: m_callback(cb)
	, m_retry_count(0)
	, m_socket(ios)
	, m_broadcast_timer(ios)
	, m_disabled(false)
{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log.open("lsd.log", std::ios::in | std::ios::out | std::ios::trunc);
#endif
	assert(multicast_address.is_multicast());
	rebind(listen_interface);
}

lsd::~lsd() {}

void lsd::rebind(address const& listen_interface)
{
	address_v4 local_ip;
	if (listen_interface.is_v4() && listen_interface != address_v4::from_string("0.0.0.0"))
	{
		local_ip = listen_interface.to_v4();
	}
	else
	{
		// make a best guess of the interface we're using and its IP
		udp::resolver r(m_socket.io_service());
		udp::resolver::iterator i = r.resolve(udp::resolver::query(asio::ip::host_name(), "0"));
		for (;i != udp::resolver_iterator(); ++i)
		{
			if (i->endpoint().address().is_v4()) break;
		}

		if (i == udp::resolver_iterator())
		{
	#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
			m_log << "local host name did not resolve to an IPv4 address. "
				"disabling local service discovery" << std::endl;
	#endif
			m_disabled = true;
			return;
		}

		local_ip = i->endpoint().address().to_v4();
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

		m_socket.set_option(join_group(multicast_address));
//		m_socket.set_option(outbound_interface(address_v4()));
		m_socket.set_option(enable_loopback(false));
//		m_socket.set_option(hops(255));
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
	m_socket.send_to(asio::buffer(msg.c_str(), msg.size() - 1)
		, multicast_endpoint);

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << to_simple_string(microsec_clock::universal_time())
		<< " ==> announce: ih: " << ih << " port: " << listen_port << std::endl;
#endif

	m_broadcast_timer.expires_from_now(milliseconds(250 * m_retry_count));
	m_broadcast_timer.async_wait(bind(&lsd::resend_announce, this, _1, msg));
}

void lsd::resend_announce(asio::error_code const& e, std::string msg)
{
	using boost::posix_time::hours;
	if (e) return;

	m_socket.send_to(asio::buffer(msg, msg.size() - 1)
		, multicast_endpoint);

	++m_retry_count;
	if (m_retry_count >= 5)
		return;

	m_broadcast_timer.expires_from_now(milliseconds(250 * m_retry_count));
	m_broadcast_timer.async_wait(bind(&lsd::resend_announce, this, _1, msg));
}

void lsd::on_announce(asio::error_code const& e
	, std::size_t bytes_transferred)
{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << to_simple_string(microsec_clock::universal_time())
			<< " <== on_announce" << std::endl;
#endif
	using namespace libtorrent::detail;
	using boost::posix_time::seconds;
	if (e) return;

	char* p = m_receive_buffer;
	char* end = m_receive_buffer + bytes_transferred;
	char* line = std::find(p, end, '\n');
	for (char* i = p; i < line; ++i) *i = std::tolower(*i);
	if (line == end || std::strcmp("bt-search", p))
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << to_simple_string(microsec_clock::universal_time())
			<< " <== Got incorrect method in announce" << std::string(p, line) << std::endl;
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
		if (!strcmp(p, "port:"))
		{
			port = atoi(p + 5);
		}
		else if (!strcmp(p, "infohash:"))
		{
			ih = boost::lexical_cast<sha1_hash>(p + 9);
		}
		p = line + 1;
	}

	if (!ih.is_all_zeros() && port != 0)
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << to_simple_string(microsec_clock::universal_time())
			<< " <== Got incoming local announce " << m_remote.address()
			<< ":" << port << " ih: " << ih << std::endl;
#endif
		// we got an announce, pass it on through the callback
		try { m_callback(tcp::endpoint(m_remote.address(), port), ih); }
		catch (std::exception&) {}
	}
	setup_receive();
}

void lsd::setup_receive()
{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << to_simple_string(microsec_clock::universal_time())
		<< " *** setup_receive" << std::endl;
#endif
	assert(m_socket.is_open());
	m_socket.async_receive_from(asio::buffer(m_receive_buffer
		, sizeof(m_receive_buffer)), m_remote, bind(&lsd::on_announce, this, _1, _2));
}

void lsd::close()
{
	m_socket.close();
}

