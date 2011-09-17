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

#include "libtorrent/udp_socket.hpp"
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/escape_string.hpp"
#include <stdlib.h>
#include <boost/bind.hpp>
#include <boost/array.hpp>
#if BOOST_VERSION < 103500
#include <asio/read.hpp>
#else
#include <boost/asio/read.hpp>
#endif

using namespace libtorrent;

udp_socket::udp_socket(asio::io_service& ios, udp_socket::callback_t const& c
	, connection_queue& cc)
	: m_callback(c)
	, m_ipv4_sock(ios)
#if TORRENT_USE_IPV6
	, m_ipv6_sock(ios)
#endif
	, m_bind_port(0)
	, m_v4_outstanding(0)
#if TORRENT_USE_IPV6
	, m_v6_outstanding(0)
#endif
	, m_socks5_sock(ios)
	, m_connection_ticket(-1)
	, m_cc(cc)
	, m_resolver(ios)
	, m_queue_packets(false)
	, m_tunnel_packets(false)
	, m_abort(false)
	, m_outstanding_ops(0)
{
#ifdef TORRENT_DEBUG
	m_magic = 0x1337;
	m_started = false;
	m_outstanding_when_aborted = -1;
#endif
}

udp_socket::~udp_socket()
{
#if TORRENT_USE_IPV6
	TORRENT_ASSERT(m_v6_outstanding == 0);
#endif
	TORRENT_ASSERT(m_v4_outstanding == 0);
	TORRENT_ASSERT(m_magic == 0x1337);
	TORRENT_ASSERT(!m_callback || !m_started);
#ifdef TORRENT_DEBUG
	m_magic = 0;
#endif
	TORRENT_ASSERT(m_outstanding_ops == 0);
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

bool udp_socket::maybe_clear_callback(mutex_t::scoped_lock& l)
{
	if (m_outstanding_ops + m_v4_outstanding + m_v6_outstanding == 0)
	{
		// "this" may be destructed in the callback
		// that's why we need to unlock
		callback_t tmp = m_callback;
		m_callback.clear();
		l.unlock();
		return true;
	}
	return false;
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
	mutex_t::scoped_lock l(m_mutex);	

#if TORRENT_USE_IPV6
	if (s == &m_ipv6_sock)
	{
		TORRENT_ASSERT(m_v6_outstanding > 0);
		--m_v6_outstanding;
	}
	else
#endif
	{
		TORRENT_ASSERT(m_v4_outstanding > 0);
		--m_v4_outstanding;
	}

	if (m_abort)
	{
		maybe_clear_callback(l);
		return;
	}

	CHECK_MAGIC;
	if (!m_callback) return;

	if (e)
	{
		l.unlock();
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
		l.lock();

		// don't stop listening on recoverable errors
		if (e != asio::error::host_unreachable
			&& e != asio::error::fault
			&& e != asio::error::connection_reset
			&& e != asio::error::connection_refused
			&& e != asio::error::connection_aborted
			&& e != asio::error::operation_aborted
			&& e != asio::error::message_size)
		{
			maybe_clear_callback(l);
			return;
		}

		if (m_abort) return;

#if TORRENT_USE_IPV6
		if (s == &m_ipv4_sock && m_v4_outstanding == 0)
#endif
		{
			++m_v4_outstanding;
			s->async_receive_from(asio::buffer(m_v4_buf, sizeof(m_v4_buf))
				, m_v4_ep, boost::bind(&udp_socket::on_read, self(), s, _1, _2));
		}
#if TORRENT_USE_IPV6
		else if (m_v6_outstanding == 0)
		{
			++m_v6_outstanding;
			s->async_receive_from(asio::buffer(m_v6_buf, sizeof(m_v6_buf))
				, m_v6_ep, boost::bind(&udp_socket::on_read, self(), s, _1, _2));
		}
#endif

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
			l.unlock();
			// if the source IP doesn't match the proxy's, ignore the packet
			if (m_v4_ep == m_proxy_addr)
				unwrap(e, m_v4_buf, bytes_transferred);
		}
		else
		{
			l.unlock();
			m_callback(e, m_v4_ep, m_v4_buf, bytes_transferred);
		}
		l.lock();

#ifndef BOOST_NO_EXCEPTIONS
		} catch(std::exception&) {}
#endif

		if (m_abort) return;

		if (m_v4_outstanding == 0)
		{
			++m_v4_outstanding;
			s->async_receive_from(asio::buffer(m_v4_buf, sizeof(m_v4_buf))
				, m_v4_ep, boost::bind(&udp_socket::on_read, self(), s, _1, _2));
		}
	}
#if TORRENT_USE_IPV6
	else
	{
#ifndef BOOST_NO_EXCEPTIONS
		try {
#endif

		if (m_tunnel_packets)
		{
			l.unlock();
			// if the source IP doesn't match the proxy's, ignore the packet
			if (m_v6_ep == m_proxy_addr)
				unwrap(e, m_v6_buf, bytes_transferred);
		}
		else
		{
			l.unlock();
			m_callback(e, m_v6_ep, m_v6_buf, bytes_transferred);
		}

#ifndef BOOST_NO_EXCEPTIONS
		} catch(std::exception&) {}
#endif
		l.lock();

		if (m_abort) return;

		if (m_v6_outstanding == 0)
		{
			++m_v6_outstanding;
			s->async_receive_from(asio::buffer(m_v6_buf, sizeof(m_v6_buf))
				, m_v6_ep, boost::bind(&udp_socket::on_read, self(), s, _1, _2));
		}
	}
#endif // TORRENT_USE_IPV6

#ifdef TORRENT_DEBUG
	m_started = true;
#endif
}

void udp_socket::wrap(udp::endpoint const& ep, char const* p, int len, error_code& ec)
{
	CHECK_MAGIC;
	using namespace libtorrent::detail;

	char header[25];
	char* h = header;

	write_uint16(0, h); // reserved
	write_uint8(0, h); // fragment
	write_uint8(ep.address().is_v4()?1:4, h); // atyp
	write_address(ep.address(), h);
	write_uint16(ep.port(), h);

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
		// domain name not supported
		return;
	}

	m_callback(e, sender, p, size - (p - buf));
}

void udp_socket::close()
{
	mutex_t::scoped_lock l(m_mutex);	
	TORRENT_ASSERT(m_magic == 0x1337);

	error_code ec;
	m_ipv4_sock.close(ec);
#if TORRENT_USE_IPV6
	m_ipv6_sock.close(ec);
#endif
	m_socks5_sock.close(ec);
	m_resolver.cancel();
	m_abort = true;
#ifdef TORRENT_DEBUG
	m_outstanding_when_aborted = m_v4_outstanding + m_v6_outstanding;
#endif
	if (m_connection_ticket >= 0)
	{
		m_cc.done(m_connection_ticket);
		m_connection_ticket = -1;

		// we just called done, which means on_timeout
		// won't be called. Decrement the outstanding
		// ops counter for that
		TORRENT_ASSERT(m_outstanding_ops > 0);
		--m_outstanding_ops;
		if (m_abort)
		{
			maybe_clear_callback(l);
			return;
		}
	}

	maybe_clear_callback(l);
}

void udp_socket::bind(udp::endpoint const& ep, error_code& ec)
{
	mutex_t::scoped_lock l(m_mutex);	
	CHECK_MAGIC;

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

		if (m_v4_outstanding == 0)
		{
			++m_v4_outstanding;
			m_ipv4_sock.async_receive_from(asio::buffer(m_v4_buf, sizeof(m_v4_buf))
				, m_v4_ep, boost::bind(&udp_socket::on_read, self(), &m_ipv4_sock, _1, _2));
		}
	}
#if TORRENT_USE_IPV6
	else
	{
		m_ipv6_sock.set_option(v6only(true), ec);
		if (ec) return;
		m_ipv6_sock.bind(ep, ec);
		if (ec) return;
		if (m_v6_outstanding == 0)
		{
			++m_v6_outstanding;
			m_ipv6_sock.async_receive_from(asio::buffer(m_v6_buf, sizeof(m_v6_buf))
				, m_v6_ep, boost::bind(&udp_socket::on_read, self(), &m_ipv6_sock, _1, _2));
		}
	}
#endif
#ifdef TORRENT_DEBUG
	m_started = true;
#endif
	m_bind_port = ep.port();
}

void udp_socket::bind(int port)
{
	mutex_t::scoped_lock l(m_mutex);	
	CHECK_MAGIC;

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
		if (m_v4_outstanding == 0)
		{
			++m_v4_outstanding;
			m_ipv4_sock.async_receive_from(asio::buffer(m_v4_buf, sizeof(m_v4_buf))
				, m_v4_ep, boost::bind(&udp_socket::on_read, self(), &m_ipv4_sock, _1, _2));
		}
	}
#if TORRENT_USE_IPV6
	m_ipv6_sock.open(udp::v6(), ec);
	if (!ec)
	{
		m_ipv6_sock.set_option(v6only(true), ec);
		m_ipv6_sock.bind(udp::endpoint(address_v6::any(), port), ec);
		if (m_v6_outstanding == 0)
		{
			++m_v6_outstanding;
			m_ipv6_sock.async_receive_from(asio::buffer(m_v6_buf, sizeof(m_v6_buf))
				, m_v6_ep, boost::bind(&udp_socket::on_read, self(), &m_ipv6_sock, _1, _2));
		}
	}
#endif // TORRENT_USE_IPV6

#ifdef TORRENT_DEBUG
	m_started = true;
#endif
	m_bind_port = port;
}

void udp_socket::set_proxy_settings(proxy_settings const& ps)
{
	mutex_t::scoped_lock l(m_mutex);	
	CHECK_MAGIC;

	error_code ec;
	m_socks5_sock.close(ec);
	m_tunnel_packets = false;
	
	m_proxy_settings = ps;

	if (ps.type == proxy_settings::socks5
		|| ps.type == proxy_settings::socks5_pw)
	{
		m_queue_packets = true;
		// connect to socks5 server and open up the UDP tunnel
		tcp::resolver::query q(ps.hostname, to_string(ps.port).elems);
		++m_outstanding_ops;
		m_resolver.async_resolve(q, boost::bind(
			&udp_socket::on_name_lookup, self(), _1, _2));
	}
}

void udp_socket::on_name_lookup(error_code const& e, tcp::resolver::iterator i)
{
	mutex_t::scoped_lock l(m_mutex);	
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;

	if (m_abort)
	{
		maybe_clear_callback(l);
		return;
	}
	CHECK_MAGIC;

	if (e == asio::error::operation_aborted) return;

	if (e)
	{
#ifndef BOOST_NO_EXCEPTIONS
		try {
#endif
			m_callback(e, udp::endpoint(), 0, 0);
#ifndef BOOST_NO_EXCEPTIONS
		} catch(std::exception&) {}
#endif
		return;
	}

	m_proxy_addr.address(i->endpoint().address());
	m_proxy_addr.port(i->endpoint().port());
	l.unlock();
	// on_connect may be called from within this thread
	// the semantics for on_connect and on_timeout is 
	// a bit complicated. See comments in connection_queue.hpp
	// for more details. This semantic determines how and
	// when m_outstanding_ops may be decremented
	// To simplyfy this, it's probably a good idea to
	// merge on_connect and on_timeout to a single function
	++m_outstanding_ops;
	m_cc.enqueue(boost::bind(&udp_socket::on_connect, self(), _1)
		, boost::bind(&udp_socket::on_timeout, self()), seconds(10));
}

void udp_socket::on_timeout()
{
	mutex_t::scoped_lock l(m_mutex);	
	CHECK_MAGIC;

	error_code ec;
	m_socks5_sock.close(ec);
	m_connection_ticket = -1;
}

void udp_socket::on_connect(int ticket)
{
	mutex_t::scoped_lock l(m_mutex);	
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	if (m_abort)
	{
		maybe_clear_callback(l);
		return;
	}
	CHECK_MAGIC;

	if (is_closed()) return;

	m_connection_ticket = ticket;
	// at this point on_timeout may be called before on_connected
	// so increment the outstanding ops
	// it may also not be called in case we call
	// connection_queue::done first, so be sure to
	// decrement if that happens
	++m_outstanding_ops;

	error_code ec;
	m_socks5_sock.open(m_proxy_addr.address().is_v4()?tcp::v4():tcp::v6(), ec);
	++m_outstanding_ops;
	m_socks5_sock.async_connect(tcp::endpoint(m_proxy_addr.address(), m_proxy_addr.port())
		, boost::bind(&udp_socket::on_connected, self(), _1));
}

void udp_socket::on_connected(error_code const& e)
{
	mutex_t::scoped_lock l(m_mutex);	
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	if (m_abort)
	{
		maybe_clear_callback(l);
		return;
	}
	CHECK_MAGIC;

	if (e == asio::error::operation_aborted) return;

	m_cc.done(m_connection_ticket);
	m_connection_ticket = -1;

	// we just called done, which meand on_timeout
	// won't be called. Decrement the outstanding
	// ops counter for that
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	if (m_abort)
	{
		maybe_clear_callback(l);
		return;
	}

	if (e)
	{
#ifndef BOOST_NO_EXCEPTIONS
		try {
#endif
			m_callback(e, udp::endpoint(), 0, 0);
#ifndef BOOST_NO_EXCEPTIONS
		} catch(std::exception&) {}
#endif
		return;
	}

	if (is_closed()) return;

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
	++m_outstanding_ops;
	asio::async_write(m_socks5_sock, asio::buffer(m_tmp_buf, p - m_tmp_buf)
		, boost::bind(&udp_socket::handshake1, self(), _1));
}

void udp_socket::handshake1(error_code const& e)
{
	mutex_t::scoped_lock l(m_mutex);	
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	if (m_abort)
	{
		maybe_clear_callback(l);
		return;
	}

	CHECK_MAGIC;
	if (e) return;

	++m_outstanding_ops;
	asio::async_read(m_socks5_sock, asio::buffer(m_tmp_buf, 2)
		, boost::bind(&udp_socket::handshake2, self(), _1));
}

void udp_socket::handshake2(error_code const& e)
{
	mutex_t::scoped_lock l(m_mutex);	
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	if (m_abort)
	{
		maybe_clear_callback(l);
		return;
	}
	CHECK_MAGIC;

	if (e) return;

	using namespace libtorrent::detail;

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p);
	int method = read_uint8(p);

	if (version < 5) return;

	if (method == 0)
	{
		socks_forward_udp(l);
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
		++m_outstanding_ops;
		asio::async_write(m_socks5_sock, asio::buffer(m_tmp_buf, p - m_tmp_buf)
			, boost::bind(&udp_socket::handshake3, self(), _1));
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
	mutex_t::scoped_lock l(m_mutex);	
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	if (m_abort)
	{
		maybe_clear_callback(l);
		return;
	}

	CHECK_MAGIC;
	if (e) return;

	++m_outstanding_ops;
	asio::async_read(m_socks5_sock, asio::buffer(m_tmp_buf, 2)
		, boost::bind(&udp_socket::handshake4, self(), _1));
}

void udp_socket::handshake4(error_code const& e)
{
	mutex_t::scoped_lock l(m_mutex);	
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	if (m_abort)
	{
		maybe_clear_callback(l);
		return;
	}

	CHECK_MAGIC;
	if (e) return;

	using namespace libtorrent::detail;

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p);
	int status = read_uint8(p);

	if (version != 1) return;
	if (status != 0) return;

	socks_forward_udp(l);
}

void udp_socket::socks_forward_udp(mutex_t::scoped_lock& l)
{
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	if (m_abort)
	{
		maybe_clear_callback(l);
		return;
	}
	CHECK_MAGIC;
	using namespace libtorrent::detail;

	// send SOCKS5 UDP command
	char* p = &m_tmp_buf[0];
	write_uint8(5, p); // SOCKS VERSION 5
	write_uint8(3, p); // UDP ASSOCIATE command
	write_uint8(0, p); // reserved
	write_uint8(1, p); // ATYP IPv4
	write_uint32(0, p); // IP any
	write_uint16(m_bind_port, p);

	++m_outstanding_ops;
	asio::async_write(m_socks5_sock, asio::buffer(m_tmp_buf, p - m_tmp_buf)
		, boost::bind(&udp_socket::connect1, self(), _1));
}

void udp_socket::connect1(error_code const& e)
{
	mutex_t::scoped_lock l(m_mutex);	
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	if (m_abort)
	{
		maybe_clear_callback(l);
		return;
	}

	CHECK_MAGIC;
	if (e) return;

	++m_outstanding_ops;
	asio::async_read(m_socks5_sock, asio::buffer(m_tmp_buf, 10)
		, boost::bind(&udp_socket::connect2, self(), _1));
}

void udp_socket::connect2(error_code const& e)
{
	mutex_t::scoped_lock l(m_mutex);	
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	if (m_abort)
	{
		m_queue.clear();
		maybe_clear_callback(l);
		return;
	}

	CHECK_MAGIC;
	if (e)
	{
		m_queue.clear();
		return;
	}

	using namespace libtorrent::detail;

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p); // VERSION
	int status = read_uint8(p); // STATUS
	++p; // RESERVED
	int atyp = read_uint8(p); // address type

	if (version != 5 || status != 0)
	{
		m_queue.clear();
		return;
	}

	if (atyp == 1)
	{
		m_proxy_addr.address(address_v4(read_uint32(p)));
		m_proxy_addr.port(read_uint16(p));
	}
	else
	{
		// in this case we need to read more data from the socket
		TORRENT_ASSERT(false && "not implemented yet!");
		m_queue.clear();
		return;
	}
	
	m_tunnel_packets = true;
	m_queue_packets = false;

	// forward all packets that were put in the queue
	while (!m_queue.empty())
	{
		queued_packet const& p = m_queue.front();
		error_code ec;
		udp_socket::send(p.ep, &p.buf[0], p.buf.size(), ec);
		m_queue.pop_front();
	}

	++m_outstanding_ops;
	asio::async_read(m_socks5_sock, asio::buffer(m_tmp_buf, 10)
		, boost::bind(&udp_socket::hung_up, self(), _1));
}

void udp_socket::hung_up(error_code const& e)
{
	mutex_t::scoped_lock l(m_mutex);	
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	if (m_abort)
	{
		maybe_clear_callback(l);
		return;
	}

	CHECK_MAGIC;
	if (e == asio::error::operation_aborted || m_abort) return;

	l.unlock();

	// the socks connection was closed, re-open it
	set_proxy_settings(m_proxy_settings);
}

rate_limited_udp_socket::rate_limited_udp_socket(io_service& ios
	, callback_t const& c, connection_queue& cc)
	: udp_socket(ios, c, cc)
	, m_timer(ios)
	, m_queue_size_limit(200)
	, m_rate_limit(4000)
	, m_quota(4000)
	, m_last_tick(time_now())
{
	error_code ec;
	m_timer.expires_from_now(seconds(1), ec);
	m_timer.async_wait(boost::bind(&rate_limited_udp_socket::on_tick
		, boost::intrusive_ptr<rate_limited_udp_socket>(this), _1));
	TORRENT_ASSERT(!ec);
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
	m_timer.async_wait(boost::bind(&rate_limited_udp_socket::on_tick
		, boost::intrusive_ptr<rate_limited_udp_socket>(this), _1));

	time_duration delta = now - m_last_tick;
	m_last_tick = now;
	if (m_quota < m_rate_limit) m_quota += m_rate_limit * total_milliseconds(delta) / 1000;

	if (m_queue.empty()) return;

	while (!m_queue.empty() && int(m_queue.front().buf.size()) <= m_quota)
	{
		queued_packet const& p = m_queue.front();
		TORRENT_ASSERT(m_quota >= int(p.buf.size()));
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

