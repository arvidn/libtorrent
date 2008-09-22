#include "libtorrent/udp_socket.hpp"
#include "libtorrent/connection_queue.hpp"
#include <stdlib.h>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
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
	, m_ipv6_sock(ios)
	, m_bind_port(0)
	, m_outstanding(0)
	, m_socks5_sock(ios)
	, m_connection_ticket(-1)
	, m_cc(cc)
	, m_resolver(ios)
	, m_tunnel_packets(false)
{
#ifndef NDEBUG
	m_magic = 0x1337;
#endif
}

#ifndef NDEBUG
	#define CHECK_MAGIC check_magic_ cm_(m_magic)
	struct check_magic_
	{
		check_magic_(int& m_): m(m_) { TORRENT_ASSERT(m == 0x1337); }
		~check_magic_() { TORRENT_ASSERT(m == 0x1337); }
		int& m;
	};
#else
	#define CHECK_MAGIC (void)
#endif

void udp_socket::send(udp::endpoint const& ep, char const* p, int len, error_code& ec)
{
	CHECK_MAGIC;
	if (m_tunnel_packets)
	{
		// send udp packets through SOCKS5 server
		wrap(ep, p, len, ec);
		return;	
	}

	if (ep.address().is_v4() && m_ipv4_sock.is_open())
		m_ipv4_sock.send_to(asio::buffer(p, len), ep, 0, ec);
	else
		m_ipv6_sock.send_to(asio::buffer(p, len), ep, 0, ec);
}

void udp_socket::on_read(udp::socket* s, error_code const& e, std::size_t bytes_transferred)
{
	TORRENT_ASSERT(m_magic == 0x1337);
	mutex_t::scoped_lock l(m_mutex);	

	TORRENT_ASSERT(m_outstanding > 0);
	--m_outstanding;

	if (e == asio::error::operation_aborted)
	{
		if (m_outstanding == 0)
		{
			// "this" may be destructed in the callback
			// that's why we need to unlock
			l.unlock();
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
		if (s == &m_ipv4_sock)
			m_callback(e, m_v4_ep, 0, 0);
		else
			m_callback(e, m_v6_ep, 0, 0);
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
			return;

		if (s == &m_ipv4_sock)
			s->async_receive_from(asio::buffer(m_v4_buf, sizeof(m_v4_buf))
				, m_v4_ep, boost::bind(&udp_socket::on_read, this, s, _1, _2));
		else
			s->async_receive_from(asio::buffer(m_v6_buf, sizeof(m_v6_buf))
				, m_v6_ep, boost::bind(&udp_socket::on_read, this, s, _1, _2));

		return;
	}

	if (s == &m_ipv4_sock)
	{
#ifndef BOOST_NO_EXCEPTIONS
		try {
#endif

		if (m_tunnel_packets && m_v4_ep == m_proxy_addr)
			unwrap(e, m_v4_buf, bytes_transferred);
		else
			m_callback(e, m_v4_ep, m_v4_buf, bytes_transferred);

#ifndef BOOST_NO_EXCEPTIONS
		} catch(std::exception&) {}
#endif
		s->async_receive_from(asio::buffer(m_v4_buf, sizeof(m_v4_buf))
			, m_v4_ep, boost::bind(&udp_socket::on_read, this, s, _1, _2));
	}
	else
	{
#ifndef BOOST_NO_EXCEPTIONS
		try {
#endif

		if (m_tunnel_packets && m_v6_ep == m_proxy_addr)
			unwrap(e, m_v6_buf, bytes_transferred);
		else
			m_callback(e, m_v6_ep, m_v6_buf, bytes_transferred);

#ifndef BOOST_NO_EXCEPTIONS
		} catch(std::exception&) {}
#endif
		s->async_receive_from(asio::buffer(m_v6_buf, sizeof(m_v6_buf))
			, m_v6_ep, boost::bind(&udp_socket::on_read, this, s, _1, _2));
	}
	++m_outstanding;
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
	write_address(ep.address(), h);
	write_uint16(ep.port(), h);

	boost::array<asio::const_buffer, 2> iovec;
	iovec[0] = asio::const_buffer(header, h - header);
	iovec[1] = asio::const_buffer(p, len);

	if (m_proxy_addr.address().is_v4() && m_ipv4_sock.is_open())
		m_ipv4_sock.send_to(iovec, m_proxy_addr, 0, ec);
	else
		m_ipv6_sock.send_to(iovec, m_proxy_addr, 0, ec);
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
	else if (atyp == 4)
	{
		// IPv6
		sender = read_v6_endpoint<udp::endpoint>(p);
	}
	else
	{
		// domain name not supported
		return;
	}

	m_callback(e, sender, p, size - (p - buf));
}

void udp_socket::close()
{
	TORRENT_ASSERT(m_magic == 0x1337);
	mutex_t::scoped_lock l(m_mutex);	

	error_code ec;
	m_ipv4_sock.close(ec);
	m_ipv6_sock.close(ec);
	m_socks5_sock.close(ec);
	if (m_connection_ticket >= 0)
	{
		m_cc.done(m_connection_ticket);
		m_connection_ticket = -1;
	}

	if (m_outstanding == 0)
	{
		// "this" may be destructed in the callback
		// that's why we need to unlock
		l.unlock();
		callback_t tmp = m_callback;
		m_callback.clear();
	}
}

void udp_socket::bind(udp::endpoint const& ep, error_code& ec)
{
	CHECK_MAGIC;
	mutex_t::scoped_lock l(m_mutex);	

	if (m_ipv4_sock.is_open()) m_ipv4_sock.close(ec);
	if (m_ipv6_sock.is_open()) m_ipv6_sock.close(ec);

	if (ep.address().is_v4())
	{
		m_ipv4_sock.open(udp::v4(), ec);
		if (ec) return;
		m_ipv4_sock.bind(ep, ec);
		if (ec) return;
		m_ipv4_sock.async_receive_from(asio::buffer(m_v4_buf, sizeof(m_v4_buf))
			, m_v4_ep, boost::bind(&udp_socket::on_read, this, &m_ipv4_sock, _1, _2));
	}
	else
	{
		m_ipv6_sock.set_option(v6only(true), ec);
		if (ec) return;
		m_ipv6_sock.bind(ep, ec);
		if (ec) return;
		m_ipv6_sock.async_receive_from(asio::buffer(m_v6_buf, sizeof(m_v6_buf))
			, m_v6_ep, boost::bind(&udp_socket::on_read, this, &m_ipv6_sock, _1, _2));
	}
	++m_outstanding;
	m_bind_port = ep.port();
}

void udp_socket::bind(int port)
{
	CHECK_MAGIC;
	mutex_t::scoped_lock l(m_mutex);	

	error_code ec;

	if (m_ipv4_sock.is_open()) m_ipv4_sock.close(ec);
	if (m_ipv6_sock.is_open()) m_ipv6_sock.close(ec);

	m_ipv4_sock.open(udp::v4(), ec);
	if (!ec)
	{
		m_ipv4_sock.bind(udp::endpoint(address_v4::any(), port), ec);
		m_ipv4_sock.async_receive_from(asio::buffer(m_v4_buf, sizeof(m_v4_buf))
			, m_v4_ep, boost::bind(&udp_socket::on_read, this, &m_ipv4_sock, _1, _2));
		++m_outstanding;
	}
	m_ipv6_sock.open(udp::v6(), ec);
	if (!ec)
	{
		m_ipv6_sock.set_option(v6only(true), ec);
		m_ipv6_sock.bind(udp::endpoint(address_v6::any(), port), ec);
		m_ipv6_sock.async_receive_from(asio::buffer(m_v6_buf, sizeof(m_v6_buf))
			, m_v6_ep, boost::bind(&udp_socket::on_read, this, &m_ipv6_sock, _1, _2));
		++m_outstanding;
	}
	m_bind_port = port;
}

void udp_socket::set_proxy_settings(proxy_settings const& ps)
{
	CHECK_MAGIC;
	mutex_t::scoped_lock l(m_mutex);	

	error_code ec;
	m_socks5_sock.close(ec);
	m_tunnel_packets = false;
	
	m_proxy_settings = ps;

	if (ps.type == proxy_settings::socks5
		|| ps.type == proxy_settings::socks5_pw)
	{
		// connect to socks5 server and open up the UDP tunnel
		tcp::resolver::query q(ps.hostname
			, boost::lexical_cast<std::string>(ps.port));
		m_resolver.async_resolve(q, boost::bind(
			&udp_socket::on_name_lookup, this, _1, _2));
	}
}

void udp_socket::on_name_lookup(error_code const& e, tcp::resolver::iterator i)
{
	if (e) return;
	CHECK_MAGIC;

	mutex_t::scoped_lock l(m_mutex);	

	m_proxy_addr.address(i->endpoint().address());
	m_proxy_addr.port(i->endpoint().port());
	m_cc.enqueue(boost::bind(&udp_socket::on_connect, this, _1)
		, boost::bind(&udp_socket::on_timeout, this), seconds(10));
}

void udp_socket::on_timeout()
{
	CHECK_MAGIC;
	mutex_t::scoped_lock l(m_mutex);	

	error_code ec;
	m_socks5_sock.close(ec);
	m_connection_ticket = -1;
}

void udp_socket::on_connect(int ticket)
{
	CHECK_MAGIC;
	mutex_t::scoped_lock l(m_mutex);	

	m_connection_ticket = ticket;
	error_code ec;
	m_socks5_sock.open(m_proxy_addr.address().is_v4()?tcp::v4():tcp::v6(), ec);
	m_socks5_sock.async_connect(tcp::endpoint(m_proxy_addr.address(), m_proxy_addr.port())
		, boost::bind(&udp_socket::on_connected, this, _1));
}

void udp_socket::on_connected(error_code const& e)
{
	CHECK_MAGIC;

	mutex_t::scoped_lock l(m_mutex);	
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
	asio::async_write(m_socks5_sock, asio::buffer(m_tmp_buf, p - m_tmp_buf)
		, boost::bind(&udp_socket::handshake1, this, _1));
}

void udp_socket::handshake1(error_code const& e)
{
	CHECK_MAGIC;
	if (e) return;

	mutex_t::scoped_lock l(m_mutex);	

	asio::async_read(m_socks5_sock, asio::buffer(m_tmp_buf, 2)
		, boost::bind(&udp_socket::handshake2, this, _1));
}

void udp_socket::handshake2(error_code const& e)
{
	CHECK_MAGIC;
	if (e) return;

	using namespace libtorrent::detail;

	mutex_t::scoped_lock l(m_mutex);	

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p);
	int method = read_uint8(p);

	if (version < 5) return;

	if (method == 0)
	{
		socks_forward_udp();
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

	mutex_t::scoped_lock l(m_mutex);	

	asio::async_read(m_socks5_sock, asio::buffer(m_tmp_buf, 2)
		, boost::bind(&udp_socket::handshake4, this, _1));
}

void udp_socket::handshake4(error_code const& e)
{
	CHECK_MAGIC;
	if (e) return;

	mutex_t::scoped_lock l(m_mutex);	

	using namespace libtorrent::detail;

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p);
	int status = read_uint8(p);

	if (version != 1) return;
	if (status != 0) return;

	socks_forward_udp();
}

void udp_socket::socks_forward_udp()
{
	CHECK_MAGIC;
	using namespace libtorrent::detail;

	mutex_t::scoped_lock l(m_mutex);	

	// send SOCKS5 UDP command
	char* p = &m_tmp_buf[0];
	write_uint8(5, p); // SOCKS VERSION 5
	write_uint8(3, p); // UDP ASSOCIATE command
	write_uint8(0, p); // reserved
	write_uint8(0, p); // ATYP IPv4
	write_uint32(0, p); // IP any
	write_uint16(m_bind_port, p);

	asio::async_write(m_socks5_sock, asio::buffer(m_tmp_buf, p - m_tmp_buf)
		, boost::bind(&udp_socket::connect1, this, _1));
}

void udp_socket::connect1(error_code const& e)
{
	CHECK_MAGIC;
	if (e) return;

	mutex_t::scoped_lock l(m_mutex);	

	asio::async_read(m_socks5_sock, asio::buffer(m_tmp_buf, 10)
		, boost::bind(&udp_socket::connect2, this, _1));
}

void udp_socket::connect2(error_code const& e)
{
	CHECK_MAGIC;
	if (e) return;

	mutex_t::scoped_lock l(m_mutex);	

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
}

