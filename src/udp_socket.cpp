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

#include "libtorrent/config.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/broadcast_socket.hpp" // for is_v4
#include "libtorrent/alert_manager.hpp"
#include "libtorrent/socks5_stream.hpp" // for socks_error
#include "libtorrent/aux_/keepalive.hpp"

#include <cstdlib>
#include <functional>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ip/v6_only.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#ifdef _WIN32
// for SIO_KEEPALIVE_VALS
#include <mstcpip.h>
#endif

namespace libtorrent {

using namespace std::placeholders;

// used to build SOCKS messages in
std::size_t const tmp_buffer_size = 270;

// used for SOCKS5 UDP wrapper header
std::size_t const max_header_size = 255;

// this class hold the state of the SOCKS5 connection to maintain the UDP
// ASSOCIATE tunnel. It's instantiated on the heap for two reasons:
//
// 1. since its asynchronous functions may refer to it after the udp_socket has
//    been destructed, it needs to be held by a shared_ptr
// 2. since using a socks proxy is assumed to be a less common case, it makes
//    the common case cheaper by not allocating this space unconditionally
struct socks5 : std::enable_shared_from_this<socks5>
{
	explicit socks5(io_service& ios, aux::listen_socket_handle ls
		, alert_manager& alerts)
		: m_socks5_sock(ios)
		, m_resolver(ios)
		, m_timer(ios)
		, m_retry_timer(ios)
		, m_alerts(alerts)
		, m_listen_socket(std::move(ls))
	{}

	void start(aux::proxy_settings const& ps);
	void close();

	bool active() const { return m_active; }
	udp::endpoint target() const { return m_udp_proxy_addr; }

private:

	std::shared_ptr<socks5> self() { return shared_from_this(); }

	void on_name_lookup(error_code const& e, tcp::resolver::iterator i);
	void on_connect_timeout(error_code const& e);
	void on_connected(error_code const& e);
	void handshake1(error_code const& e);
	void handshake2(error_code const& e);
	void handshake3(error_code const& e);
	void handshake4(error_code const& e);
	void socks_forward_udp();
	void connect1(error_code const& e);
	void connect2(error_code const& e);
	void hung_up(error_code const& e);
	void on_retry_socks_connect(error_code const& e);

	void retry_connection();

	tcp::socket m_socks5_sock;
	tcp::resolver m_resolver;
	deadline_timer m_timer;
	deadline_timer m_retry_timer;
	alert_manager& m_alerts;
	aux::listen_socket_handle m_listen_socket;
	std::array<char, tmp_buffer_size> m_tmp_buf;

	aux::proxy_settings m_proxy_settings;

	// this is the endpoint the proxy server lives at.
	// when performing a UDP associate, we get another
	// endpoint (presumably on the same IP) where we're
	// supposed to send UDP packets.
	tcp::endpoint m_proxy_addr;

	// this is where UDP packets that are to be forwarded
	// are sent. The result from UDP ASSOCIATE is stored
	// in here.
	udp::endpoint m_udp_proxy_addr;

	// count failures to increase the retry timer
	int m_failures = 0;

	// set to true when we've been asked to shut down
	bool m_abort = false;

	// set to true once the tunnel is established
	bool m_active = false;
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

udp_socket::udp_socket(io_service& ios, aux::listen_socket_handle ls)
	: m_socket(ios)
	, m_buf(new receive_buffer())
	, m_listen_socket(std::move(ls))
	, m_bind_port(0)
	, m_abort(true)
{}

int udp_socket::read(span<packet> pkts, error_code& ec)
{
	auto const num = int(pkts.size());
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
			if (m_proxy_settings.type != settings_pack::none) continue;

			p.error = ec;
			p.data = span<char>();
		}
		else
		{
			p.data = {m_buf->data(), len};

			// support packets coming from the SOCKS5 proxy
			if (active_socks5())
			{
				// if the source IP doesn't match the proxy's, ignore the packet
				if (p.from != m_socks5_connection->target()) continue;
				// if we failed to unwrap, silently ignore the packet
				if (!unwrap(p.from, p.data)) continue;
			}
			else
			{
				// if we don't proxy trackers or peers, we may be receiving unwrapped
				// packets and we must let them through.
				bool const proxy_only
					= m_proxy_settings.proxy_peer_connections
					&& m_proxy_settings.proxy_tracker_connections
					;

				// if we proxy everything, block all packets that aren't coming from
				// the proxy
				if (m_proxy_settings.type != settings_pack::none && proxy_only) continue;
			}
		}

		pkts[ret] = p;
		++ret;

		// we only have a single buffer for now, so we can only return a
		// single packet. In the future though, we could attempt to drain
		// the socket here, or maybe even use recvmmsg()
		break;
	}

	return ret;
}

bool udp_socket::active_socks5() const
{
	return (m_socks5_connection && m_socks5_connection->active());
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

	bool const use_proxy
		= ((flags & peer_connection) && m_proxy_settings.proxy_peer_connections)
		|| ((flags & tracker_connection) && m_proxy_settings.proxy_tracker_connections)
		|| !(flags & (tracker_connection | peer_connection))
		;

	if (use_proxy && m_proxy_settings.type != settings_pack::none)
	{
		if (active_socks5())
		{
			// send udp packets through SOCKS5 server
			wrap(hostname, port, p, ec, flags);
		}
		else
		{
			ec = error_code(boost::system::errc::permission_denied, generic_category());
		}
		return;
	}

	// the overload that takes a hostname is really only supported when we're
	// using a proxy
	address const target = make_address(hostname, ec);
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

	bool const use_proxy
		= ((flags & peer_connection) && m_proxy_settings.proxy_peer_connections)
		|| ((flags & tracker_connection) && m_proxy_settings.proxy_tracker_connections)
		|| !(flags & (tracker_connection | peer_connection))
		;

	if (use_proxy && m_proxy_settings.type != settings_pack::none)
	{
		if (active_socks5())
		{
			// send udp packets through SOCKS5 server
			wrap(ep, p, ec, flags);
		}
		else
		{
			ec = error_code(boost::system::errc::permission_denied, generic_category());
		}
		return;
	}

	// set the DF flag for the socket and clear it again in the destructor
	set_dont_frag df(m_socket, (flags & dont_fragment)
		&& is_v4(ep));

	m_socket.send_to(boost::asio::buffer(p.data(), static_cast<std::size_t>(p.size())), ep, 0, ec);
}

void udp_socket::wrap(udp::endpoint const& ep, span<char const> p
	, error_code& ec, udp_send_flags_t const flags)
{
	TORRENT_UNUSED(flags);
	using namespace libtorrent::detail;

	std::array<char, max_header_size> header;
	char* h = header.data();

	write_uint16(0, h); // reserved
	write_uint8(0, h); // fragment
	write_uint8(is_v4(ep) ? 1 : 4, h); // atyp
	write_endpoint(ep, h);

	std::array<boost::asio::const_buffer, 2> iovec;
	iovec[0] = boost::asio::const_buffer(header.data(), aux::numeric_cast<std::size_t>(h - header.data()));
	iovec[1] = boost::asio::const_buffer(p.data(), static_cast<std::size_t>(p.size()));

	// set the DF flag for the socket and clear it again in the destructor
	set_dont_frag df(m_socket, (flags & dont_fragment) && is_v4(ep));

	m_socket.send_to(iovec, m_socks5_connection->target(), 0, ec);
}

void udp_socket::wrap(char const* hostname, int const port, span<char const> p
	, error_code& ec, udp_send_flags_t const flags)
{
	using namespace libtorrent::detail;

	std::array<char, max_header_size> header;
	char* h = header.data();

	write_uint16(0, h); // reserved
	write_uint8(0, h); // fragment
	write_uint8(3, h); // atyp
	std::size_t const hostlen = std::min(std::strlen(hostname), max_header_size - 7);
	write_uint8(hostlen, h); // hostname len
	std::memcpy(h, hostname, hostlen);
	h += hostlen;
	write_uint16(port, h);

	std::array<boost::asio::const_buffer, 2> iovec;
	iovec[0] = boost::asio::const_buffer(header.data(), aux::numeric_cast<std::size_t>(h - header.data()));
	iovec[1] = boost::asio::const_buffer(p.data(), static_cast<std::size_t>(p.size()));

	// set the DF flag for the socket and clear it again in the destructor
	set_dont_frag df(m_socket, (flags & dont_fragment)
		&& is_v4(m_socket.local_endpoint(ec)));

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
	auto const size = aux::numeric_cast<int>(buf.size());
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
	else if (atyp == 4)
	{
		// IPv6
		from = read_v6_endpoint<udp::endpoint>(p);
	}
	else
	{
		int const len = read_uint8(p);
		if (len > buf.end() - p) return false;
		std::string hostname(p, p + len);
		error_code ec;
		address addr = make_address(hostname, ec);
		// we only support "hostnames" that are a dotted decimal IP
		if (ec) return false;
		p += len;
		from = udp::endpoint(addr, read_uint16(p));
	}

	buf = {p, size - (p - buf.data())};
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
	if (protocol == udp::v6())
	{
		error_code err;
		m_socket.set_option(boost::asio::ip::v6_only(true), err);

#ifdef TORRENT_WINDOWS
		// enable Teredo on windows
		m_socket.set_option(v6_protection_level(PROTECTION_LEVEL_UNRESTRICTED), err);
#endif // TORRENT_WINDOWS
	}

	// this is best-effort. ignore errors
#ifdef TORRENT_WINDOWS
	error_code err;
	m_socket.set_option(exclusive_address_use(true), err);
#endif
}

void udp_socket::bind(udp::endpoint const& ep, error_code& ec)
{
	if (!m_socket.is_open()) open(ep.protocol(), ec);
	if (ec) return;
	m_socket.bind(ep, ec);
	if (ec) return;
	m_socket.non_blocking(true, ec);
	if (ec) return;

	error_code err;
	m_bind_port = m_socket.local_endpoint(err).port();
	if (err) m_bind_port = ep.port();
}

void udp_socket::set_proxy_settings(aux::proxy_settings const& ps
	, alert_manager& alerts)
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
		m_socks5_connection = std::make_shared<socks5>(lt::get_io_service(m_socket)
			, m_listen_socket, alerts);
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

	if (e)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_listen_socket.get_local_endpoint()
				, operation_t::hostname_lookup, e);
		++m_failures;
		retry_connection();
		return;
	}

	// only set up a SOCKS5 tunnel for sockets with the same address family
	// as the proxy
	// this is a hack to mitigate excessive SOCKS5 tunnels, until this can get
	// fixed properly.
	for (;;)
	{
		if (i == tcp::resolver::iterator{})
		{
			if (m_alerts.should_post<socks5_alert>())
				m_alerts.emplace_alert<socks5_alert>(m_listen_socket.get_local_endpoint()
					, operation_t::hostname_lookup
					, error_code(boost::system::errc::host_unreachable, generic_category()));
			++m_failures;
			retry_connection();
			return;
		}

		// we found a match
		if (m_listen_socket.can_route(i->endpoint().address()))
			break;
		++i;
	}

	m_proxy_addr = i->endpoint();

	error_code ec;
	m_socks5_sock.open(is_v4(m_proxy_addr) ? tcp::v4() : tcp::v6(), ec);
	if (ec)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::sock_open, ec);
		return;
	}

	// enable keepalives
	m_socks5_sock.set_option(boost::asio::socket_base::keep_alive(true), ec);
	if (ec)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::sock_option, ec);
		ec.clear();
	}

#if defined _WIN32 && !defined TORRENT_BUILD_SIMULATOR
	SOCKET sock = m_socks5_sock.native_handle();
	DWORD bytes = 0;
	tcp_keepalive timeout{};
	timeout.onoff = TRUE;
	timeout.keepalivetime = 30;
	timeout.keepaliveinterval = 30;
	auto const ret = WSAIoctl(sock, SIO_KEEPALIVE_VALS, &timeout, sizeof(timeout)
		, nullptr, 0, &bytes, nullptr, nullptr);
	if (ret != 0)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::sock_option
				, error_code(WSAGetLastError(), system_category()));
	}
#else
#if defined TORRENT_HAS_KEEPALIVE_IDLE
	// set keepalive timeouts
	m_socks5_sock.set_option(aux::tcp_keepalive_idle(30), ec);
	if (ec)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::sock_option, ec);
		ec.clear();
	}
#endif
#ifdef TORRENT_HAS_KEEPALIVE_INTERVAL
	m_socks5_sock.set_option(aux::tcp_keepalive_interval(1), ec);
	if (ec)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::sock_option, ec);
		ec.clear();
	}
#endif
#endif

	tcp::endpoint const bind_ep(m_listen_socket.get_local_endpoint().address(), 0);
	m_socks5_sock.bind(bind_ep, ec);
	if (ec)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::sock_bind, ec);
		++m_failures;
		retry_connection();
		return;
	}

	// TODO: perhaps an attempt should be made to bind m_socks5_sock to the
	// device of m_listen_socket

	ADD_OUTSTANDING_ASYNC("socks5::on_connected");
	m_socks5_sock.async_connect(m_proxy_addr
		, std::bind(&socks5::on_connected, self(), _1));

	ADD_OUTSTANDING_ASYNC("socks5::on_connect_timeout");
	m_timer.expires_from_now(seconds(10));
	m_timer.async_wait(std::bind(&socks5::on_connect_timeout
		, self(), _1));
}

void socks5::on_connect_timeout(error_code const& e)
{
	COMPLETE_ASYNC("socks5::on_connect_timeout");

	if (e == boost::asio::error::operation_aborted) return;

	if (m_abort) return;

	if (m_alerts.should_post<socks5_alert>())
		m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::connect, errors::timed_out);

	error_code ignore;
	m_socks5_sock.close(ignore);

	++m_failures;
	retry_connection();
}

void socks5::on_connected(error_code const& e)
{
	COMPLETE_ASYNC("socks5::on_connected");

	m_timer.cancel();

	if (e == boost::asio::error::operation_aborted) return;

	if (m_abort) return;

	// we failed to connect to the proxy
	if (e)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::connect, e);
		++m_failures;
		retry_connection();
		return;
	}

	using namespace libtorrent::detail;

	// send SOCKS5 authentication methods
	char* p = m_tmp_buf.data();
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
	TORRENT_ASSERT_VAL(p - m_tmp_buf.data() < int(m_tmp_buf.size()), (p - m_tmp_buf.data()));
	ADD_OUTSTANDING_ASYNC("socks5::on_handshake1");
	boost::asio::async_write(m_socks5_sock, boost::asio::buffer(m_tmp_buf.data()
		, aux::numeric_cast<std::size_t>(p - m_tmp_buf.data()))
		, std::bind(&socks5::handshake1, self(), _1));
}

void socks5::handshake1(error_code const& e)
{
	COMPLETE_ASYNC("socks5::on_handshake1");
	if (m_abort) return;
	if (e)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::handshake, e);
		++m_failures;
		retry_connection();
		return;
	}

	ADD_OUTSTANDING_ASYNC("socks5::on_handshake2");
	boost::asio::async_read(m_socks5_sock, boost::asio::buffer(m_tmp_buf.data(), 2)
		, std::bind(&socks5::handshake2, self(), _1));
}

void socks5::handshake2(error_code const& e)
{
	COMPLETE_ASYNC("socks5::on_handshake2");
	if (m_abort) return;

	if (e)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::handshake, e);
		++m_failures;
		retry_connection();
		return;
	}

	using namespace libtorrent::detail;

	char* p = m_tmp_buf.data();
	int const version = read_uint8(p);
	int const method = read_uint8(p);

	if (version < 5)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::handshake
				, socks_error::unsupported_version);
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
			if (m_alerts.should_post<socks5_alert>())
				m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::handshake
					, socks_error::username_required);
			error_code ec;
			m_socks5_sock.close(ec);
			return;
		}

		// start sub-negotiation
		p = m_tmp_buf.data();
		write_uint8(1, p);
		TORRENT_ASSERT(m_proxy_settings.username.size() < 0x100);
		write_uint8(uint8_t(m_proxy_settings.username.size()), p);
		write_string(m_proxy_settings.username, p);
		TORRENT_ASSERT(m_proxy_settings.password.size() < 0x100);
		write_uint8(uint8_t(m_proxy_settings.password.size()), p);
		write_string(m_proxy_settings.password, p);
		TORRENT_ASSERT_VAL(p - m_tmp_buf.data() < int(m_tmp_buf.size()), (p - m_tmp_buf.data()));
		ADD_OUTSTANDING_ASYNC("socks5::on_handshake3");
		boost::asio::async_write(m_socks5_sock
			, boost::asio::buffer(m_tmp_buf.data(), aux::numeric_cast<std::size_t>(p - m_tmp_buf.data()))
			, std::bind(&socks5::handshake3, self(), _1));
	}
	else
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::handshake
				, socks_error::unsupported_authentication_method);

		error_code ec;
		m_socks5_sock.close(ec);
		return;
	}
}

void socks5::handshake3(error_code const& e)
{
	COMPLETE_ASYNC("socks5::on_handshake3");
	if (m_abort) return;
	if (e)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::handshake, e);
		++m_failures;
		retry_connection();
		return;
	}

	ADD_OUTSTANDING_ASYNC("socks5::on_handshake4");
	boost::asio::async_read(m_socks5_sock, boost::asio::buffer(m_tmp_buf.data(), 2)
		, std::bind(&socks5::handshake4, self(), _1));
}

void socks5::handshake4(error_code const& e)
{
	COMPLETE_ASYNC("socks5::on_handshake4");
	if (m_abort) return;
	if (e)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::handshake, e);
		++m_failures;
		retry_connection();
		return;
	}

	using namespace libtorrent::detail;

	char* p = m_tmp_buf.data();
	int const version = read_uint8(p);
	int const status = read_uint8(p);

	if (version != 1 || status != 0) return;

	socks_forward_udp(/*l*/);
}

void socks5::socks_forward_udp()
{
	using namespace libtorrent::detail;

	// send SOCKS5 UDP command
	char* p = m_tmp_buf.data();
	write_uint8(5, p); // SOCKS VERSION 5
	write_uint8(3, p); // UDP ASSOCIATE command
	write_uint8(0, p); // reserved
	write_uint8(1, p); // ATYP = IPv4
	write_uint32(0, p); // 0.0.0.0
	write_uint16(0, p); // :0
	TORRENT_ASSERT_VAL(p - m_tmp_buf.data() < int(m_tmp_buf.size()), (p - m_tmp_buf.data()));
	ADD_OUTSTANDING_ASYNC("socks5::connect1");
	boost::asio::async_write(m_socks5_sock
		, boost::asio::buffer(m_tmp_buf.data(), aux::numeric_cast<std::size_t>(p - m_tmp_buf.data()))
		, std::bind(&socks5::connect1, self(), _1));
}

void socks5::connect1(error_code const& e)
{
	COMPLETE_ASYNC("socks5::connect1");
	if (m_abort) return;
	if (e)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::connect, e);
		++m_failures;
		retry_connection();
		return;
	}

	ADD_OUTSTANDING_ASYNC("socks5::connect2");
	boost::asio::async_read(m_socks5_sock, boost::asio::buffer(m_tmp_buf.data(), 10)
		, std::bind(&socks5::connect2, self(), _1));
}

void socks5::connect2(error_code const& e)
{
	COMPLETE_ASYNC("socks5::connect2");

	if (m_abort) return;
	if (e)
	{
		if (m_alerts.should_post<socks5_alert>())
			m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::handshake, e);
		++m_failures;
		retry_connection();
		return;
	}

	using namespace libtorrent::detail;

	char* p = m_tmp_buf.data();
	int const version = read_uint8(p); // VERSION
	int const status = read_uint8(p); // STATUS
	++p; // RESERVED
	int const atyp = read_uint8(p); // address type

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
	m_failures = 0;

	ADD_OUTSTANDING_ASYNC("socks5::hung_up");
	boost::asio::async_read(m_socks5_sock, boost::asio::buffer(m_tmp_buf.data(), 10)
		, std::bind(&socks5::hung_up, self(), _1));
}

void socks5::hung_up(error_code const& e)
{
	COMPLETE_ASYNC("socks5::hung_up");
	m_active = false;

	if (e == boost::asio::error::operation_aborted || m_abort) return;

	if (e && m_alerts.should_post<socks5_alert>())
		m_alerts.emplace_alert<socks5_alert>(m_proxy_addr, operation_t::sock_read, e);

	retry_connection();
}

void socks5::retry_connection()
{
	// the socks connection was closed, re-open it in a bit
	// back off exponentially
	if (m_failures > 200) m_failures = 200;
	m_retry_timer.expires_from_now(seconds(std::min(120, m_failures * m_failures / 2) + 5));
	m_retry_timer.async_wait(std::bind(&socks5::on_retry_socks_connect
		, self(), _1));
}

void socks5::on_retry_socks_connect(error_code const& e)
{
	if (e || m_abort) return;
	error_code ignore;
	m_socks5_sock.close(ignore);
	start(m_proxy_settings);
}

void socks5::close()
{
	m_abort = true;
	error_code ec;
	m_socks5_sock.close(ec);
	m_resolver.cancel();
	m_timer.cancel();
	m_retry_timer.cancel();
}

constexpr udp_send_flags_t udp_socket::peer_connection;
constexpr udp_send_flags_t udp_socket::tracker_connection;
constexpr udp_send_flags_t udp_socket::dont_queue;
constexpr udp_send_flags_t udp_socket::dont_fragment;

}
