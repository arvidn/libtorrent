/*

Copyright (c) 2007-2016, Arvid Norberg
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
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

#include <cstdlib>
#include <functional>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ip/v6_only.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

using namespace std::placeholders;

// this class hold the state of the SOCKS5 connection to maintain the UDP
// ASSOCIATE tunnel. It's instantiated on the heap for two reasons:
//
// 1. since its asynchronous functions may refer to it after the udp_socket has
//    been destructed, it needs to be held by a shared_ptr
// 2. since using a socks proxy is assumed to be a less common case, it makes
//    the common case cheaper by not allocating this space unconditionally
struct socks5 : std::enable_shared_from_this<socks5>
{
	explicit socks5(io_service& ios)
		: m_socks5_sock(ios)
		, m_resolver(ios)
		, m_timer(ios)
		, m_retry_timer(ios)
		, m_abort(false)
		, m_active(false)
	{
		std::memset(m_tmp_buf, 0, sizeof(m_tmp_buf));
	}

	void start(aux::proxy_settings const& ps);
	void close();

	bool active() const { return m_active; }
	udp::endpoint target() const { return m_udp_proxy_addr; }

private:

	std::shared_ptr<socks5> self() { return shared_from_this(); }

	void on_name_lookup(error_code const& e, tcp::resolver::iterator i);
	void on_connect_timeout(error_code const& ec);
	void on_connected(error_code const& ec);
	void handshake1(error_code const& e);
	void handshake2(error_code const& e);
	void handshake3(error_code const& e);
	void handshake4(error_code const& e);
	void socks_forward_udp();
	void connect1(error_code const& e);
	void connect2(error_code const& e);
	void hung_up(error_code const& e);
	void retry_socks_connect(error_code const& e);

	tcp::socket m_socks5_sock;
	tcp::resolver m_resolver;
	deadline_timer m_timer;
	deadline_timer m_retry_timer;
	char m_tmp_buf[270];

	aux::proxy_settings m_proxy_settings;

	// this is the endpoint the proxy server lives at.
	// when performing a UDP associate, we get another
	// endpoint (presumably on the same IP) where we're
	// supposed to send UDP packets.
	udp::endpoint m_proxy_addr;

	// this is where UDP packets that are to be forwarded
	// are sent. The result from UDP ASSOCIATE is stored
	// in here.
	udp::endpoint m_udp_proxy_addr;

	// set to true when we've been asked to shut down
	bool m_abort;

	// set to true once the tunnel is established
	bool m_active;
};

#ifdef TORRENT_HAS_DONT_FRAGMENT
struct set_dont_frag
{
	set_dont_frag(udp::socket& sock, bool const df)
		: m_socket(sock)
		, m_df(df)
	{
		if (!m_df) return;
		error_code ignore_errors;
		m_socket.set_option(libtorrent::dont_fragment(true), ignore_errors);
		TORRENT_ASSERT_VAL(!ignore_errors, ignore_errors.message());
	}

	~set_dont_frag()
	{
		if (!m_df) return;
		error_code ignore_errors;
		m_socket.set_option(libtorrent::dont_fragment(false), ignore_errors);
		TORRENT_ASSERT_VAL(!ignore_errors, ignore_errors.message());
	}

private:
	udp::socket& m_socket;
	bool const m_df;
};
#else
struct set_dont_frag
{ set_dont_frag(udp::socket&, int) {} };
#endif

udp_socket::udp_socket(io_service& ios)
	: m_socket(ios)
	, m_buf(new receive_buffer())
	, m_bind_port(0)
	, m_force_proxy(false)
	, m_abort(true)
{}

int udp_socket::read(span<packet> pkts, error_code& ec)
{
	int const num = int(pkts.size());
	int ret = 0;
	packet p;

	while (ret < num)
	{
		int const len = int(m_socket.receive_from(boost::asio::buffer(*m_buf)
			, p.from, 0, ec));

		if (ec == error::would_block
			|| ec == error::try_again
			|| ec == error::operation_aborted
			|| ec == error::bad_descriptor)
		{
			return ret;
		}

		if (ec == error::interrupted)
		{
			continue;
		}

		if (ec)
		{
			// SOCKS5 cannot wrap ICMP errors. And even if it could, they certainly
			// would not arrive as unwrapped (regular) ICMP errors. If we're using
			// a proxy we must ignore these
			if (m_force_proxy
				|| (m_socks5_connection
				&&  m_socks5_connection->active())) continue;

			p.error = ec;
			p.data = span<char>();
		}
		else
		{
			p.data = {m_buf->data(), aux::numeric_cast<std::size_t>(len)};

			// support packets coming from the SOCKS5 proxy
			if (m_socks5_connection && m_socks5_connection->active())
			{
				// if the source IP doesn't match the proxy's, ignore the packet
				if (p.from != m_socks5_connection->target()) continue;
				if (!unwrap(p.from, p.data)) continue;
			}
			// block incoming packets that aren't coming via the proxy
			// if force proxy mode is enabled
			else if (m_force_proxy) continue;
		}

		pkts[aux::numeric_cast<std::size_t>(ret)] = p;
		++ret;

		// we only have a single buffer for now, so we can only return a
		// single packet. In the future though, we could attempt to drain
		// the socket here, or maybe even use recvmmsg()
		break;
	}

	return ret;
}

void udp_socket::send_hostname(char const* hostname, int const port
	, span<char const> p, error_code& ec, udp_send_flags_t const flags)
{
	TORRENT_ASSERT(is_single_thread());

	// if the sockets are closed, the udp_socket is closing too
	if (!is_open())
	{
		ec = error_code(boost::system::errc::bad_file_descriptor, generic_category());
		return;
	}

	if (m_socks5_connection && m_socks5_connection->active())
	{
		// send udp packets through SOCKS5 server
		wrap(hostname, port, p, ec, flags);
		return;
	}

	if (m_force_proxy)
	{
		ec = error_code(boost::system::errc::permission_denied, generic_category());
		return;
	}

	// the overload that takes a hostname is really only supported when we're
	// using a proxy
	address target = address::from_string(hostname, ec);
	if (!ec) send(udp::endpoint(target, std::uint16_t(port)), p, ec, flags);
}

void udp_socket::send(udp::endpoint const& ep, span<char const> p
	, error_code& ec, udp_send_flags_t const flags)
{
	TORRENT_ASSERT(is_single_thread());

	// if the sockets are closed, the udp_socket is closing too
	if (!is_open())
	{
		ec = error_code(boost::system::errc::bad_file_descriptor, generic_category());
		return;
	}

	const bool allow_proxy
		= ((flags & peer_connection) && m_proxy_settings.proxy_peer_connections)
		|| ((flags & tracker_connection) && m_proxy_settings.proxy_tracker_connections)
		|| !(flags & (tracker_connection | peer_connection))
		;

	if (allow_proxy && m_socks5_connection && m_socks5_connection->active())
	{
		// send udp packets through SOCKS5 server
		wrap(ep, p, ec, flags);
		return;
	}

	if (m_force_proxy) return;

	// set the DF flag for the socket and clear it again in the destructor
	set_dont_frag df(m_socket, (flags & dont_fragment)
		&& ep.protocol() == udp::v4());

	m_socket.send_to(boost::asio::buffer(p.data(), p.size()), ep, 0, ec);
}

void udp_socket::wrap(udp::endpoint const& ep, span<char const> p
	, error_code& ec, udp_send_flags_t const flags)
{
	TORRENT_UNUSED(flags);
	using namespace libtorrent::detail;

	char header[25];
	char* h = header;

	write_uint16(0, h); // reserved
	write_uint8(0, h); // fragment
	write_uint8(ep.address().is_v4()?1:4, h); // atyp
	write_endpoint(ep, h);

	std::array<boost::asio::const_buffer, 2> iovec;
	iovec[0] = boost::asio::const_buffer(header, aux::numeric_cast<std::size_t>(h - header));
	iovec[1] = boost::asio::const_buffer(p.data(), p.size());

	// set the DF flag for the socket and clear it again in the destructor
	set_dont_frag df(m_socket, (flags & dont_fragment)
		&& ep.protocol() == udp::v4());

	m_socket.send_to(iovec, m_socks5_connection->target(), 0, ec);
}

void udp_socket::wrap(char const* hostname, int const port, span<char const> p
	, error_code& ec, udp_send_flags_t const flags)
{
	TORRENT_UNUSED(flags);
	using namespace libtorrent::detail;

	char header[270];
	char* h = header;

	write_uint16(0, h); // reserved
	write_uint8(0, h); // fragment
	write_uint8(3, h); // atyp
	std::size_t const hostlen = std::min(std::strlen(hostname), std::size_t(255));
	write_uint8(hostlen, h); // hostname len
	std::memcpy(h, hostname, hostlen);
	h += hostlen;
	write_uint16(port, h);

	std::array<boost::asio::const_buffer, 2> iovec;
	iovec[0] = boost::asio::const_buffer(header, aux::numeric_cast<std::size_t>(h - header));
	iovec[1] = boost::asio::const_buffer(p.data(), p.size());

	// set the DF flag for the socket and clear it again in the destructor
	set_dont_frag df(m_socket, (flags & dont_fragment)
		&& m_socket.local_endpoint(ec).protocol() == udp::v4());

	m_socket.send_to(iovec, m_socks5_connection->target(), 0, ec);
}

// unwrap the UDP packet from the SOCKS5 header
// buf is an in-out parameter. It will be updated
// return false if the packet should be ignored. It's not a valid Socks5 UDP
// forwarded packet
bool udp_socket::unwrap(udp::endpoint& from, span<char>& buf)
{
	using namespace libtorrent::detail;

	// the minimum socks5 header size
	int const size = aux::numeric_cast<int>(buf.size());
	if (size <= 10) return false;

	char* p = buf.data();
	p += 2; // reserved
	int const frag = read_uint8(p);
	// fragmentation is not supported
	if (frag != 0) return false;

	int const atyp = read_uint8(p);
	if (atyp == 1)
	{
		// IPv4
		from = read_v4_endpoint<udp::endpoint>(p);
	}
#if TORRENT_USE_IPV6
	else if (atyp == 4)
	{
		// IPv6
		from = read_v6_endpoint<udp::endpoint>(p);
	}
#endif
	else
	{
		int const len = read_uint8(p);
		if (len > buf.end() - p) return false;
		std::string hostname(p, p + len);
		error_code ec;
		address addr = address::from_string(hostname, ec);
		// we only support "hostnames" that are a dotted decimal IP
		if (ec) return false;
		p += len;
		from = udp::endpoint(addr, read_uint16(p));
	}

	buf = {p, aux::numeric_cast<std::size_t>(size - (p - buf.data()))};
	return true;
}

#if !defined BOOST_ASIO_ENABLE_CANCELIO && defined TORRENT_WINDOWS
#error BOOST_ASIO_ENABLE_CANCELIO needs to be defined when building libtorrent to enable cancel() in asio on windows
#endif

void udp_socket::close()
{
	TORRENT_ASSERT(is_single_thread());

	error_code ec;
	m_socket.close(ec);
	TORRENT_ASSERT_VAL(!ec || ec == error::bad_descriptor, ec);
	if (m_socks5_connection)
	{
		m_socks5_connection->close();
		m_socks5_connection.reset();
	}
	m_abort = true;
}

void udp_socket::open(udp const& protocol, error_code& ec)
{
	TORRENT_ASSERT(is_single_thread());

	m_abort = false;

	if (m_socket.is_open()) m_socket.close(ec);
	ec.clear();

	m_socket.open(protocol, ec);
	if (ec) return;
#if TORRENT_USE_IPV6
	if (protocol == udp::v6())
	{
		error_code err;
		m_socket.set_option(boost::asio::ip::v6_only(true), err);

#ifdef TORRENT_WINDOWS
		// enable Teredo on windows
		m_socket.set_option(v6_protection_level(PROTECTION_LEVEL_UNRESTRICTED), err);
#endif // TORRENT_WINDOWS
	}
#endif

	// this is best-effort. ignore errors
	error_code err;
#ifdef TORRENT_WINDOWS
	m_socket.set_option(exclusive_address_use(true), err);
#else
	m_socket.set_option(boost::asio::socket_base::reuse_address(true), err);
#endif
}

void udp_socket::bind(udp::endpoint const& ep, error_code& ec)
{
	if (!m_socket.is_open()) open(ep.protocol(), ec);
	if (ec) return;
	m_socket.bind(ep, ec);
	if (ec) return;
	udp::socket::non_blocking_io ioc(true);
	m_socket.io_control(ioc, ec);
	if (ec) return;

	error_code err;
	m_bind_port = m_socket.local_endpoint(err).port();
	if (err) m_bind_port = ep.port();
}

void udp_socket::set_proxy_settings(aux::proxy_settings const& ps)
{
	TORRENT_ASSERT(is_single_thread());

	if (m_socks5_connection)
	{
		m_socks5_connection->close();
		m_socks5_connection.reset();
	}

	m_proxy_settings = ps;

	if (m_abort) return;

	if (ps.type == settings_pack::socks5
		|| ps.type == settings_pack::socks5_pw)
	{
		// connect to socks5 server and open up the UDP tunnel

		m_socks5_connection = std::make_shared<socks5>(m_socket.get_io_service());
		m_socks5_connection->start(ps);
	}
}

// ===================== SOCKS 5 =========================

void socks5::start(aux::proxy_settings const& ps)
{
	m_proxy_settings = ps;

	// TODO: use the system resolver_interface here
	tcp::resolver::query q(ps.hostname, to_string(ps.port).data());
	ADD_OUTSTANDING_ASYNC("socks5::on_name_lookup");
	m_resolver.async_resolve(q, std::bind(
		&socks5::on_name_lookup, self(), _1, _2));
}

void socks5::on_name_lookup(error_code const& e, tcp::resolver::iterator i)
{
	COMPLETE_ASYNC("socks5::on_name_lookup");

	if (m_abort) return;

	if (e == boost::asio::error::operation_aborted) return;

	if (e) return;

	m_proxy_addr.address(i->endpoint().address());
	m_proxy_addr.port(i->endpoint().port());

	error_code ec;
	m_socks5_sock.open(m_proxy_addr.address().is_v4()?tcp::v4():tcp::v6(), ec);

	// enable keepalives
	m_socks5_sock.set_option(boost::asio::socket_base::keep_alive(true), ec);

	ADD_OUTSTANDING_ASYNC("socks5::on_connected");
	m_socks5_sock.async_connect(tcp::endpoint(m_proxy_addr.address(), m_proxy_addr.port())
		, std::bind(&socks5::on_connected, self(), _1));

	ADD_OUTSTANDING_ASYNC("socks5::on_connect_timeout");
	m_timer.expires_from_now(seconds(10));
	m_timer.async_wait(std::bind(&socks5::on_connect_timeout
		, self(), _1));
}

void socks5::on_connect_timeout(error_code const& ec)
{
	COMPLETE_ASYNC("socks5::on_connect_timeout");

	if (ec == boost::asio::error::operation_aborted) return;

	if (m_abort) return;

	error_code ignore;
	m_socks5_sock.close(ignore);
}

void socks5::on_connected(error_code const& e)
{
	COMPLETE_ASYNC("socks5::on_connected");

	m_timer.cancel();

	if (e == boost::asio::error::operation_aborted) return;

	if (m_abort) return;

	// we failed to connect to the proxy
	if (e) return;

	using namespace libtorrent::detail;

	// send SOCKS5 authentication methods
	char* p = &m_tmp_buf[0];
	write_uint8(5, p); // SOCKS VERSION 5
	if (m_proxy_settings.username.empty()
		|| m_proxy_settings.type == settings_pack::socks5)
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
	TORRENT_ASSERT_VAL(p - m_tmp_buf < int(sizeof(m_tmp_buf)), (p - m_tmp_buf));
	ADD_OUTSTANDING_ASYNC("socks5::on_handshake1");
	boost::asio::async_write(m_socks5_sock, boost::asio::buffer(m_tmp_buf
		, aux::numeric_cast<std::size_t>(p - m_tmp_buf))
		, std::bind(&socks5::handshake1, self(), _1));
}

void socks5::handshake1(error_code const& e)
{
	COMPLETE_ASYNC("socks5::on_handshake1");
	if (m_abort) return;
	if (e) return;

	ADD_OUTSTANDING_ASYNC("socks5::on_handshake2");
	boost::asio::async_read(m_socks5_sock, boost::asio::buffer(m_tmp_buf, 2)
		, std::bind(&socks5::handshake2, self(), _1));
}

void socks5::handshake2(error_code const& e)
{
	COMPLETE_ASYNC("socks5::on_handshake2");
	if (m_abort) return;

	if (e) return;

	using namespace libtorrent::detail;

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p);
	int method = read_uint8(p);

	if (version < 5)
	{
		error_code ec;
		m_socks5_sock.close(ec);
		return;
	}

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
		p = &m_tmp_buf[0];
		write_uint8(1, p);
		TORRENT_ASSERT(m_proxy_settings.username.size() < 0x100);
		write_uint8(uint8_t(m_proxy_settings.username.size()), p);
		write_string(m_proxy_settings.username, p);
		TORRENT_ASSERT(m_proxy_settings.password.size() < 0x100);
		write_uint8(uint8_t(m_proxy_settings.password.size()), p);
		write_string(m_proxy_settings.password, p);
		TORRENT_ASSERT_VAL(p - m_tmp_buf < int(sizeof(m_tmp_buf)), (p - m_tmp_buf));
		ADD_OUTSTANDING_ASYNC("socks5::on_handshake3");
		boost::asio::async_write(m_socks5_sock
			, boost::asio::buffer(m_tmp_buf, aux::numeric_cast<std::size_t>(p - m_tmp_buf))
			, std::bind(&socks5::handshake3, self(), _1));
	}
	else
	{
		error_code ec;
		m_socks5_sock.close(ec);
		return;
	}
}

void socks5::handshake3(error_code const& e)
{
	COMPLETE_ASYNC("socks5::on_handshake3");
	if (m_abort) return;
	if (e) return;

	ADD_OUTSTANDING_ASYNC("socks5::on_handshake4");
	boost::asio::async_read(m_socks5_sock, boost::asio::buffer(m_tmp_buf, 2)
		, std::bind(&socks5::handshake4, self(), _1));
}

void socks5::handshake4(error_code const& e)
{
	COMPLETE_ASYNC("socks5::on_handshake4");
	if (m_abort) return;
	if (e) return;

	using namespace libtorrent::detail;

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p);
	int status = read_uint8(p);

	if (version != 1 || status != 0) return;

	socks_forward_udp(/*l*/);
}

void socks5::socks_forward_udp()
{
	using namespace libtorrent::detail;

	// send SOCKS5 UDP command
	char* p = &m_tmp_buf[0];
	write_uint8(5, p); // SOCKS VERSION 5
	write_uint8(3, p); // UDP ASSOCIATE command
	write_uint8(0, p); // reserved
	write_uint8(1, p); // ATYP = IPv4
	write_uint32(0, p); // 0.0.0.0
	write_uint16(0, p); // :0
	TORRENT_ASSERT_VAL(p - m_tmp_buf < int(sizeof(m_tmp_buf)), (p - m_tmp_buf));
	ADD_OUTSTANDING_ASYNC("socks5::connect1");
	boost::asio::async_write(m_socks5_sock
		, boost::asio::buffer(m_tmp_buf, aux::numeric_cast<std::size_t>(p - m_tmp_buf))
		, std::bind(&socks5::connect1, self(), _1));
}

void socks5::connect1(error_code const& e)
{
	COMPLETE_ASYNC("socks5::connect1");
	if (m_abort) return;
	if (e) return;

	ADD_OUTSTANDING_ASYNC("socks5::connect2");
	boost::asio::async_read(m_socks5_sock, boost::asio::buffer(m_tmp_buf, 10)
		, std::bind(&socks5::connect2, self(), _1));
}

void socks5::connect2(error_code const& e)
{
	COMPLETE_ASYNC("socks5::connect2");

	if (m_abort) return;
	if (e) return;

	using namespace libtorrent::detail;

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p); // VERSION
	int status = read_uint8(p); // STATUS
	++p; // RESERVED
	int atyp = read_uint8(p); // address type

	if (version != 5 || status != 0) return;

	if (atyp == 1)
	{
		m_udp_proxy_addr.address(address_v4(read_uint32(p)));
		m_udp_proxy_addr.port(read_uint16(p));
	}
	else
	{
		// in this case we need to read more data from the socket
		// no IPv6 support for UDP socks5
		TORRENT_ASSERT_FAIL();
		return;
	}

	// we're done!
	m_active = true;

	ADD_OUTSTANDING_ASYNC("socks5::hung_up");
	boost::asio::async_read(m_socks5_sock, boost::asio::buffer(m_tmp_buf, 10)
		, std::bind(&socks5::hung_up, self(), _1));
}

void socks5::hung_up(error_code const& e)
{
	COMPLETE_ASYNC("socks5::hung_up");
	m_active = false;

	if (e == boost::asio::error::operation_aborted || m_abort) return;

	// the socks connection was closed, re-open it in a bit
	m_retry_timer.expires_from_now(seconds(5));
	m_retry_timer.async_wait(std::bind(&socks5::retry_socks_connect
		, self(), _1));
}

void socks5::retry_socks_connect(error_code const& e)
{
	if (e) return;
	start(m_proxy_settings);
}

void socks5::close()
{
	m_abort = true;
	error_code ec;
	m_socks5_sock.close(ec);
	m_resolver.cancel();
	m_timer.cancel();
}

constexpr udp_send_flags_t udp_socket::peer_connection;
constexpr udp_send_flags_t udp_socket::tracker_connection;
constexpr udp_send_flags_t udp_socket::dont_queue;
constexpr udp_send_flags_t udp_socket::dont_fragment;

}
