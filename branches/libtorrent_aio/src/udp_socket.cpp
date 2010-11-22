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

#include "libtorrent/config.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/error.hpp"
#include <stdlib.h>
#include <boost/bind.hpp>
#include <boost/array.hpp>
#if BOOST_VERSION < 103500
#include <asio/read.hpp>
#else
#include <boost/asio/read.hpp>
#endif

using namespace libtorrent;

udp_socket::udp_socket(asio::io_service& ios
	, udp_socket::callback_t const& c
	, udp_socket::callback2_t const& c2
	, connection_queue& cc)
	: m_callback(c)
	, m_callback2(c2)
	, m_ipv4_sock(ios)
#if TORRENT_USE_IPV6
	, m_ipv6_sock(ios)
#endif
	, m_bind_port(0)
	, m_outstanding(0)
	, m_socks5_sock(ios)
	, m_connection_ticket(-1)
	, m_cc(cc)
	, m_resolver(ios)
	, m_queue_packets(false)
	, m_tunnel_packets(false)
	, m_abort(false)
{
#ifdef TORRENT_DEBUG
	m_magic = 0x1337;
	m_started = false;
	m_outstanding_when_aborted = -1;
#if defined BOOST_HAS_PTHREADS
	m_thread = 0;
#endif
#endif
}

udp_socket::~udp_socket()
{
#ifdef TORRENT_DEBUG
	TORRENT_ASSERT(m_magic == 0x1337);
	TORRENT_ASSERT(!m_callback || !m_started);
	TORRENT_ASSERT_VAL(m_outstanding == 0, m_outstanding);
	m_magic = 0;
#endif
}

#ifdef TORRENT_DEBUG
	#define CHECK_MAGIC check_magic_ cm_(m_magic)
	struct check_magic_
	{
		check_magic_(int& m_): m(m_) { TORRENT_ASSERT(m == 0x1337); }
		~check_magic_() { TORRENT_ASSERT(m == 0x1337); }
		int& m;
	};
#else
	#define CHECK_MAGIC do {} while (false)
#endif

void udp_socket::send_hostname(char const* hostname, int port
	, char const* p, int len, error_code& ec)
{
	CHECK_MAGIC;

	TORRENT_ASSERT(is_open());

	// if the sockets are closed, the udp_socket is closing too
	if (!is_open()) return;

	if (m_tunnel_packets)
	{
		// send udp packets through SOCKS5 server
		wrap(hostname, port, p, len, ec);
		return;	
	}

	// this function is only supported when we're using a proxy
	TORRENT_ASSERT(m_queue_packets);
	if (!m_queue_packets) return;

	m_queue.push_back(queued_packet());
	queued_packet& qp = m_queue.back();
	qp.ep.port(port);
	qp.hostname = strdup(hostname);
	qp.buf.insert(qp.buf.begin(), p, p + len);
}

void udp_socket::send(udp::endpoint const& ep, char const* p, int len, error_code& ec)
{
	CHECK_MAGIC;

	TORRENT_ASSERT(is_open());

	// if the sockets are closed, the udp_socket is closing too
	if (!is_open()) return;

	if (m_tunnel_packets)
	{
		// send udp packets through SOCKS5 server
		wrap(ep, p, len, ec);
		return;	
	}

	if (m_queue_packets)
	{
		m_queue.push_back(queued_packet());
		queued_packet& qp = m_queue.back();
		qp.ep = ep;
		qp.hostname = 0;
		qp.buf.insert(qp.buf.begin(), p, p + len);
		return;
	}

#if TORRENT_USE_IPV6
	if (ep.address().is_v4() && m_ipv4_sock.is_open())
#endif
		m_ipv4_sock.send_to(asio::buffer(p, len), ep, 0, ec);
#if TORRENT_USE_IPV6
	else
		m_ipv6_sock.send_to(asio::buffer(p, len), ep, 0, ec);
#endif
}

void udp_socket::on_read(udp::socket* s, error_code const& e, std::size_t bytes_transferred)
{
	TORRENT_ASSERT(m_magic == 0x1337);
	TORRENT_ASSERT(is_single_thread());

	TORRENT_ASSERT(m_outstanding > 0);
	--m_outstanding;

	if (e == asio::error::operation_aborted || m_abort)
	{
		if (m_outstanding == 0)
		{
			// "this" may be destructed in the callback
			callback_t tmp = m_callback;
			m_callback.clear();
		}
		return;
	}

	CHECK_MAGIC;
	if (!m_callback) return;

	if (e)
	{
#ifndef BOOST_NO_EXCEPTIONS
		try {
#endif

#if TORRENT_USE_IPV6
		if (s == &m_ipv4_sock)
#endif
			m_callback(e, m_v4_ep, 0, 0);
#if TORRENT_USE_IPV6
		else
			m_callback(e, m_v6_ep, 0, 0);
#endif

#ifndef BOOST_NO_EXCEPTIONS
		} catch(std::exception&) {}
#endif
		// don't stop listening on recoverable errors
		if (e != asio::error::host_unreachable
			&& e != asio::error::fault
			&& e != asio::error::connection_reset
			&& e != asio::error::connection_refused
			&& e != asio::error::connection_aborted
			&& e != asio::error::message_size)
		{
			if (m_outstanding == 0)
			{
				// "this" may be destructed in the callback
				callback_t tmp = m_callback;
				m_callback.clear();
			}
			return;
		}

		if (m_abort) return;

#if TORRENT_USE_IPV6
		if (s == &m_ipv4_sock)
#endif
			s->async_receive_from(asio::buffer(m_v4_buf, sizeof(m_v4_buf))
				, m_v4_ep, boost::bind(&udp_socket::on_read, this, s, _1, _2));
#if TORRENT_USE_IPV6
		else
			s->async_receive_from(asio::buffer(m_v6_buf, sizeof(m_v6_buf))
				, m_v6_ep, boost::bind(&udp_socket::on_read, this, s, _1, _2));
#endif

		++m_outstanding;
#ifdef TORRENT_DEBUG
		m_started = true;
#endif
		return;
	}

#if TORRENT_USE_IPV6
	if (s == &m_ipv4_sock)
#endif
	{

#ifndef BOOST_NO_EXCEPTIONS
		try {
#endif

		if (m_tunnel_packets)
		{
			// if the source IP doesn't match the proxy's, ignore the packet
			if (m_v4_ep == m_proxy_addr)
				unwrap(e, m_v4_buf, bytes_transferred);
		}
		else
		{
			m_callback(e, m_v4_ep, m_v4_buf, bytes_transferred);
		}

#ifndef BOOST_NO_EXCEPTIONS
		} catch(std::exception&) {}
#endif

		if (m_abort) return;

		s->async_receive_from(asio::buffer(m_v4_buf, sizeof(m_v4_buf))
			, m_v4_ep, boost::bind(&udp_socket::on_read, this, s, _1, _2));
	}
#if TORRENT_USE_IPV6
	else
	{
#ifndef BOOST_NO_EXCEPTIONS
		try {
#endif

		if (m_tunnel_packets)
		{
			// if the source IP doesn't match the proxy's, ignore the packet
			if (m_v6_ep == m_proxy_addr)
				unwrap(e, m_v6_buf, bytes_transferred);
		}
		else
		{
			m_callback(e, m_v6_ep, m_v6_buf, bytes_transferred);
		}

#ifndef BOOST_NO_EXCEPTIONS
		} catch(std::exception&) {}
#endif

		if (m_abort) return;

		s->async_receive_from(asio::buffer(m_v6_buf, sizeof(m_v6_buf))
			, m_v6_ep, boost::bind(&udp_socket::on_read, this, s, _1, _2));
	}
#endif
	++m_outstanding;
#ifdef TORRENT_DEBUG
	m_started = true;
#endif
}

void udp_socket::wrap(udp::endpoint const& ep, char const* p, int len, error_code& ec)
{
	CHECK_MAGIC;
	using namespace libtorrent::detail;

	char header[20];
	char* h = header;

	write_uint16(0, h); // reserved
	write_uint8(0, h); // fragment
	write_uint8(ep.address().is_v4()?1:4, h); // atyp
	write_endpoint(ep, h);

	boost::array<asio::const_buffer, 2> iovec;
	iovec[0] = asio::const_buffer(header, h - header);
	iovec[1] = asio::const_buffer(p, len);

#if TORRENT_USE_IPV6
	if (m_proxy_addr.address().is_v4() && m_ipv4_sock.is_open())
#endif
		m_ipv4_sock.send_to(iovec, m_proxy_addr, 0, ec);
#if TORRENT_USE_IPV6
	else
		m_ipv6_sock.send_to(iovec, m_proxy_addr, 0, ec);
#endif
}

void udp_socket::wrap(char const* hostname, int port, char const* p, int len, error_code& ec)
{
	CHECK_MAGIC;
	using namespace libtorrent::detail;

	char header[270];
	char* h = header;

	write_uint16(0, h); // reserved
	write_uint8(0, h); // fragment
	write_uint8(3, h); // atyp
	int hostlen = (std::min)(strlen(hostname), size_t(255));
	write_uint8(hostlen, h); // hostname len
	memcpy(h, hostname, hostlen);
	h += hostlen;
	write_uint16(port, h);

	boost::array<asio::const_buffer, 2> iovec;
	iovec[0] = asio::const_buffer(header, h - header);
	iovec[1] = asio::const_buffer(p, len);

#if TORRENT_USE_IPV6
	if (m_proxy_addr.address().is_v4() && m_ipv4_sock.is_open())
#endif
		m_ipv4_sock.send_to(iovec, m_proxy_addr, 0, ec);
#if TORRENT_USE_IPV6
	else
		m_ipv6_sock.send_to(iovec, m_proxy_addr, 0, ec);
#endif
}

// unwrap the UDP packet from the SOCKS5 header
void udp_socket::unwrap(error_code const& e, char const* buf, int size)
{
	CHECK_MAGIC;
	using namespace libtorrent::detail;

	// the minimum socks5 header size
	if (size <= 10) return;

	char const* p = buf;
	p += 2; // reserved
	int frag = read_uint8(p);
	// fragmentation is not supported
	if (frag != 0) return;

	udp::endpoint sender;

	int atyp = read_uint8(p);
	if (atyp == 1)
	{
		// IPv4
		sender = read_v4_endpoint<udp::endpoint>(p);
	}
#if TORRENT_USE_IPV6
	else if (atyp == 4)
	{
		// IPv6
		sender = read_v6_endpoint<udp::endpoint>(p);
	}
#endif
	else
	{
		int len = read_uint8(p);
		if (len > (buf + size) - p) return;
		std::string hostname(p, p + len);
		p += len;
		m_callback2(e, hostname.c_str(), p, size - (p - buf));
		return;
	}

	m_callback(e, sender, p, size - (p - buf));
}

void udp_socket::close()
{
	TORRENT_ASSERT(is_single_thread());
	TORRENT_ASSERT(m_magic == 0x1337);

	error_code ec;
	// if we close the socket here, we can't shut down
	// utp connections or NAT-PMP. We need to cancel the
	// outstanding operations
	m_ipv4_sock.cancel(ec);
	if (ec == error::operation_not_supported)
		m_ipv4_sock.close(ec);
	TORRENT_ASSERT(!ec || ec == error::bad_descriptor);
#if TORRENT_USE_IPV6
	m_ipv6_sock.cancel(ec);
	if (ec == error::operation_not_supported)
		m_ipv6_sock.close(ec);
	TORRENT_ASSERT(!ec || ec == error::bad_descriptor);
#endif
	m_socks5_sock.cancel(ec);
	if (ec == error::operation_not_supported)
		m_socks5_sock.close(ec);
	TORRENT_ASSERT(!ec || ec == error::bad_descriptor);
	m_resolver.cancel();
	m_abort = true;

	if (m_connection_ticket >= 0)
	{
		m_cc.done(m_connection_ticket);
		m_connection_ticket = -1;
	}

	if (m_outstanding == 0)
	{
		// "this" may be destructed in the callback
		callback_t tmp = m_callback;
		m_callback.clear();
	}
}

void udp_socket::bind(udp::endpoint const& ep, error_code& ec)
{
	CHECK_MAGIC;
	TORRENT_ASSERT(is_single_thread());

	TORRENT_ASSERT(m_abort == false);
	if (m_abort) return;

	if (m_ipv4_sock.is_open()) m_ipv4_sock.close(ec);
#if TORRENT_USE_IPV6
	if (m_ipv6_sock.is_open()) m_ipv6_sock.close(ec);
#endif

	if (ep.address().is_v4())
	{
		m_ipv4_sock.open(udp::v4(), ec);
		if (ec) return;
		m_ipv4_sock.bind(ep, ec);
		if (ec) return;
		m_ipv4_sock.async_receive_from(asio::buffer(m_v4_buf, sizeof(m_v4_buf))
			, m_v4_ep, boost::bind(&udp_socket::on_read, this, &m_ipv4_sock, _1, _2));
		++m_outstanding;
	}
#if TORRENT_USE_IPV6
	else
	{
		m_ipv6_sock.set_option(v6only(true), ec);
		if (ec) return;
		m_ipv6_sock.bind(ep, ec);
		if (ec) return;
		m_ipv6_sock.async_receive_from(asio::buffer(m_v6_buf, sizeof(m_v6_buf))
			, m_v6_ep, boost::bind(&udp_socket::on_read, this, &m_ipv6_sock, _1, _2));
		++m_outstanding;
	}
#endif
#ifdef TORRENT_DEBUG
	m_started = true;
#endif
	m_bind_port = ep.port();
}

void udp_socket::bind(int port)
{
	CHECK_MAGIC;
	TORRENT_ASSERT(is_single_thread());

	TORRENT_ASSERT(m_abort == false);
	if (m_abort) return;

	error_code ec;

	if (m_ipv4_sock.is_open()) m_ipv4_sock.close(ec);
#if TORRENT_USE_IPV6
	if (m_ipv6_sock.is_open()) m_ipv6_sock.close(ec);
#endif

	m_ipv4_sock.open(udp::v4(), ec);
	if (!ec)
	{
		m_ipv4_sock.bind(udp::endpoint(address_v4::any(), port), ec);
		m_ipv4_sock.async_receive_from(asio::buffer(m_v4_buf, sizeof(m_v4_buf))
			, m_v4_ep, boost::bind(&udp_socket::on_read, this, &m_ipv4_sock, _1, _2));
		++m_outstanding;
#ifdef TORRENT_DEBUG
		m_started = true;
#endif
	}
#if TORRENT_USE_IPV6
	m_ipv6_sock.open(udp::v6(), ec);
	if (!ec)
	{
		m_ipv6_sock.set_option(v6only(true), ec);
		m_ipv6_sock.bind(udp::endpoint(address_v6::any(), port), ec);
		m_ipv6_sock.async_receive_from(asio::buffer(m_v6_buf, sizeof(m_v6_buf))
			, m_v6_ep, boost::bind(&udp_socket::on_read, this, &m_ipv6_sock, _1, _2));
		++m_outstanding;
#ifdef TORRENT_DEBUG
		m_started = true;
#endif
	}
#endif
	m_bind_port = port;
}

void udp_socket::set_proxy_settings(proxy_settings const& ps)
{
	CHECK_MAGIC;
	TORRENT_ASSERT(is_single_thread());

	error_code ec;
	m_socks5_sock.close(ec);
	m_tunnel_packets = false;
	
	m_proxy_settings = ps;

	if (m_abort) return;

	if (ps.type == proxy_settings::socks5
		|| ps.type == proxy_settings::socks5_pw)
	{
		m_queue_packets = true;
		// connect to socks5 server and open up the UDP tunnel
		tcp::resolver::query q(ps.hostname, to_string(ps.port).elems);
		m_resolver.async_resolve(q, boost::bind(
			&udp_socket::on_name_lookup, this, _1, _2));
	}
}

void udp_socket::on_name_lookup(error_code const& e, tcp::resolver::iterator i)
{
	if (e) return;
	CHECK_MAGIC;

	TORRENT_ASSERT(is_single_thread());

	m_proxy_addr.address(i->endpoint().address());
	m_proxy_addr.port(i->endpoint().port());
	// on_connect may be called from within this thread
	m_cc.enqueue(boost::bind(&udp_socket::on_connect, this, _1)
		, boost::bind(&udp_socket::on_timeout, this), seconds(10));
}

void udp_socket::on_timeout()
{
	CHECK_MAGIC;
	TORRENT_ASSERT(is_single_thread());

	error_code ec;
	m_socks5_sock.close(ec);
	m_connection_ticket = -1;
}

void udp_socket::on_connect(int ticket)
{
	CHECK_MAGIC;
	TORRENT_ASSERT(is_single_thread());

	if (m_abort) return;

	m_connection_ticket = ticket;
	error_code ec;
	m_socks5_sock.open(m_proxy_addr.address().is_v4()?tcp::v4():tcp::v6(), ec);
	m_socks5_sock.async_connect(tcp::endpoint(m_proxy_addr.address(), m_proxy_addr.port())
		, boost::bind(&udp_socket::on_connected, this, _1));
}

void udp_socket::on_connected(error_code const& e)
{
	CHECK_MAGIC;

	TORRENT_ASSERT(is_single_thread());
	m_cc.done(m_connection_ticket);
	m_connection_ticket = -1;
	if (e) return;

	using namespace libtorrent::detail;

	// send SOCKS5 authentication methods
	char* p = &m_tmp_buf[0];
	write_uint8(5, p); // SOCKS VERSION 5
	if (m_proxy_settings.username.empty()
		|| m_proxy_settings.type == proxy_settings::socks5)
	{
		write_uint8(1, p); // 1 authentication method (no auth)
		write_uint8(0, p); // no authentication
	}
	else
	{
		write_uint8(2, p); // 2 authentication methods
		write_uint8(0, p); // no authentication
		write_uint8(2, p); // username/password
	}
	TORRENT_ASSERT_VAL(p - m_tmp_buf < sizeof(m_tmp_buf), (p - m_tmp_buf));
	asio::async_write(m_socks5_sock, asio::buffer(m_tmp_buf, p - m_tmp_buf)
		, boost::bind(&udp_socket::handshake1, this, _1));
}

void udp_socket::handshake1(error_code const& e)
{
	CHECK_MAGIC;
	if (e) return;

	TORRENT_ASSERT(is_single_thread());

	asio::async_read(m_socks5_sock, asio::buffer(m_tmp_buf, 2)
		, boost::bind(&udp_socket::handshake2, this, _1));
}

void udp_socket::handshake2(error_code const& e)
{
	CHECK_MAGIC;
	if (e) return;

	using namespace libtorrent::detail;

	TORRENT_ASSERT(is_single_thread());

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p);
	int method = read_uint8(p);

	if (version < 5) return;

	if (method == 0)
	{
		socks_forward_udp(/*l*/);
	}
	else if (method == 2)
	{
		if (m_proxy_settings.username.empty())
		{
			error_code ec;
			m_socks5_sock.close(ec);
			return;
		}

		// start sub-negotiation
		char* p = &m_tmp_buf[0];
		write_uint8(1, p);
		write_uint8(m_proxy_settings.username.size(), p);
		write_string(m_proxy_settings.username, p);
		write_uint8(m_proxy_settings.password.size(), p);
		write_string(m_proxy_settings.password, p);
		TORRENT_ASSERT_VAL(p - m_tmp_buf < sizeof(m_tmp_buf), (p - m_tmp_buf));
		asio::async_write(m_socks5_sock, asio::buffer(m_tmp_buf, p - m_tmp_buf)
			, boost::bind(&udp_socket::handshake3, this, _1));
	}
	else
	{
		error_code ec;
		m_socks5_sock.close(ec);
		return;
	}
}

void udp_socket::handshake3(error_code const& e)
{
	CHECK_MAGIC;
	if (e) return;

	TORRENT_ASSERT(is_single_thread());

	asio::async_read(m_socks5_sock, asio::buffer(m_tmp_buf, 2)
		, boost::bind(&udp_socket::handshake4, this, _1));
}

void udp_socket::handshake4(error_code const& e)
{
	CHECK_MAGIC;
	if (e) return;

	TORRENT_ASSERT(is_single_thread());

	using namespace libtorrent::detail;

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p);
	int status = read_uint8(p);

	if (version != 1) return;
	if (status != 0) return;

	socks_forward_udp(/*l*/);
}

void udp_socket::socks_forward_udp()
{
	CHECK_MAGIC;
	using namespace libtorrent::detail;

	// send SOCKS5 UDP command
	char* p = &m_tmp_buf[0];
	write_uint8(5, p); // SOCKS VERSION 5
	write_uint8(3, p); // UDP ASSOCIATE command
	write_uint8(0, p); // reserved
	error_code ec;
	tcp::endpoint local = m_socks5_sock.local_endpoint(ec);
	write_uint8(local.address().is_v4() ? 1 : 4, p); // ATYP IPv4
	detail::write_address(local.address(), p);
	int port = 0;
#if TORRENT_USE_IPV6
	if (local.address().is_v4())
#endif
		port = m_ipv4_sock.local_endpoint(ec).port();
#if TORRENT_USE_IPV6
	else
		port = m_ipv6_sock.local_endpoint(ec).port();
#endif
	detail::write_uint16(port , p);
	TORRENT_ASSERT_VAL(p - m_tmp_buf < sizeof(m_tmp_buf), (p - m_tmp_buf));
	asio::async_write(m_socks5_sock, asio::buffer(m_tmp_buf, p - m_tmp_buf)
		, boost::bind(&udp_socket::connect1, this, _1));
}

void udp_socket::connect1(error_code const& e)
{
	CHECK_MAGIC;
	if (e) return;

	TORRENT_ASSERT(is_single_thread());

	asio::async_read(m_socks5_sock, asio::buffer(m_tmp_buf, 10)
		, boost::bind(&udp_socket::connect2, this, _1));
}

void udp_socket::connect2(error_code const& e)
{
	CHECK_MAGIC;
	if (e) return;

	TORRENT_ASSERT(is_single_thread());

	using namespace libtorrent::detail;

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p); // VERSION
	int status = read_uint8(p); // STATUS
	read_uint8(p); // RESERVED
	int atyp = read_uint8(p); // address type

	if (version != 5) return;
	if (status != 0) return;

	if (atyp == 1)
	{
		m_proxy_addr.address(address_v4(read_uint32(p)));
		m_proxy_addr.port(read_uint16(p));
	}
	else
	{
		// in this case we need to read more data from the socket
		TORRENT_ASSERT(false && "not implemented yet!");
	}
	
	m_tunnel_packets = true;
	m_queue_packets = false;

	// forward all packets that were put in the queue
	while (!m_queue.empty())
	{
		queued_packet const& p = m_queue.front();
		error_code ec;
		if (p.hostname)
		{
			udp_socket::send_hostname(p.hostname, p.ep.port(), &p.buf[0], p.buf.size(), ec);
			free(p.hostname);
		}
		else
		{
			udp_socket::send(p.ep, &p.buf[0], p.buf.size(), ec);
		}
		m_queue.pop_front();
	}

	asio::async_read(m_socks5_sock, asio::buffer(m_tmp_buf, 10)
		, boost::bind(&udp_socket::hung_up, this, _1));
}

void udp_socket::hung_up(error_code const& e)
{
	CHECK_MAGIC;
	TORRENT_ASSERT(is_single_thread());

	if (e == asio::error::operation_aborted || m_abort) return;

	// the socks connection was closed, re-open it
	set_proxy_settings(m_proxy_settings);
}

rate_limited_udp_socket::rate_limited_udp_socket(io_service& ios
	, callback_t const& c
	, callback2_t const& c2
	, connection_queue& cc)
	: udp_socket(ios, c, c2, cc)
	, m_timer(ios)
	, m_queue_size_limit(200)
	, m_rate_limit(4000)
	, m_quota(4000)
	, m_last_tick(time_now())
{
	error_code ec;
	m_timer.expires_from_now(seconds(1), ec);
	m_timer.async_wait(boost::bind(&rate_limited_udp_socket::on_tick, this, _1));
	TORRENT_ASSERT_VAL(!ec, ec);
}

bool rate_limited_udp_socket::send(udp::endpoint const& ep, char const* p, int len, error_code& ec, int flags)
{
	if (m_quota < len)
	{
		// bit 1 of flags means "don't drop"
		if (int(m_queue.size()) >= m_queue_size_limit && (flags & 1) == 0)
			return false;
		m_queue.push_back(queued_packet());
		queued_packet& qp = m_queue.back();
		qp.ep = ep;
		qp.buf.insert(qp.buf.begin(), p, p + len);
		return true;
	}

	m_quota -= len;
	udp_socket::send(ep, p, len, ec);
	return true;
}

void rate_limited_udp_socket::on_tick(error_code const& e)
{
	if (e) return;
	if (is_closed()) return;
	error_code ec;
	ptime now = time_now_hires();
	m_timer.expires_at(now + seconds(1), ec);
	m_timer.async_wait(boost::bind(&rate_limited_udp_socket::on_tick, this, _1));

	time_duration delta = now - m_last_tick;
	m_last_tick = now;
	if (m_quota < m_rate_limit) m_quota += m_rate_limit * total_milliseconds(delta) / 1000;

	if (m_queue.empty()) return;

	while (!m_queue.empty() && int(m_queue.front().buf.size()) <= m_quota)
	{
		queued_packet const& p = m_queue.front();
		TORRENT_ASSERT(m_quota >= p.buf.size());
		m_quota -= p.buf.size();
		error_code ec;
		udp_socket::send(p.ep, &p.buf[0], p.buf.size(), ec);
		m_queue.pop_front();
	}
}

void rate_limited_udp_socket::close()
{
	error_code ec;
	m_timer.cancel(ec);
	udp_socket::close();
}

