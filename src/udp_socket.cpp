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
#include "libtorrent/assert.hpp" // for print_backtrace
#include "libtorrent/socket.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/string_util.hpp" // for allocate_string_copy
#include "libtorrent/broadcast_socket.hpp" // for is_any
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/aux_/time.hpp" // for aux::time_now()

#include <stdlib.h>
#include <boost/bind.hpp>
#include <boost/array.hpp>
#include <boost/system/system_error.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ip/v6_only.hpp>

#if defined TORRENT_ASIO_DEBUGGING
#include "libtorrent/debug.hpp"
#endif

using namespace libtorrent;

udp_socket::udp_socket(io_service& ios)
	: m_observers_locked(false)
	, m_ipv4_sock(ios)
	, m_timer(ios)
	, m_buf_size(0)
	, m_new_buf_size(0)
	, m_buf(0)
#if TORRENT_USE_IPV6
	, m_ipv6_sock(ios)
#endif
	, m_bind_port(0)
	, m_v4_outstanding(0)
	, m_restart_v4(0)
#if TORRENT_USE_IPV6
	, m_v6_outstanding(0)
	, m_restart_v6(false)
#endif
	, m_socks5_sock(ios)
	, m_retry_timer(ios)
	, m_resolver(ios)
	, m_queue_packets(false)
	, m_tunnel_packets(false)
	, m_force_proxy(false)
	, m_abort(true)
	, m_outstanding_ops(0)
#if TORRENT_USE_IPV6
	, m_v6_write_subscribed(false)
#endif
	, m_v4_write_subscribed(false)
{
#if TORRENT_USE_ASSERTS
	m_magic = 0x1337;
	m_started = false;
	m_outstanding_when_aborted = -1;
	m_outstanding_connect = 0;
	m_outstanding_timeout = 0;
	m_outstanding_resolve = 0;
	m_outstanding_socks = 0;
#endif

	m_buf_size = 2048;
	m_new_buf_size = m_buf_size;
	m_buf = static_cast<char*>(std::malloc(m_buf_size));
}

udp_socket::~udp_socket()
{
	for (std::deque<queued_packet>::iterator i = m_queue.begin()
		, end(m_queue.end()); i != end; ++i)
	{
		if (i->hostname) std::free(i->hostname);
	}
	std::free(m_buf);
#if TORRENT_USE_IPV6
	TORRENT_ASSERT_VAL(m_v6_outstanding == 0, m_v6_outstanding);
#endif
	TORRENT_ASSERT_VAL(m_v4_outstanding == 0, m_v4_outstanding);
	TORRENT_ASSERT(m_magic == 0x1337);
	TORRENT_ASSERT(m_observers_locked == false);
#if TORRENT_USE_ASSERTS
	m_magic = 0;
#endif
	TORRENT_ASSERT(m_outstanding_ops == 0);
}

#if TORRENT_USE_ASSERTS
	#define CHECK_MAGIC check_magic_ cm_(m_magic)
	struct check_magic_
	{
		check_magic_(int& m_): m(m_) { TORRENT_ASSERT(m == 0x1337); }
		~check_magic_() { TORRENT_ASSERT(m == 0x1337); }
		int& m;
	};
#else
	#define CHECK_MAGIC do {} TORRENT_WHILE_0
#endif

void udp_socket::send_hostname(char const* hostname, int port
	, char const* p, int len, error_code& ec, int flags)
{
	CHECK_MAGIC;

	TORRENT_ASSERT(is_single_thread());

	// if the sockets are closed, the udp_socket is closing too
	if (!is_open())
	{
		ec = error_code(boost::system::errc::bad_file_descriptor, generic_category());
		return;
	}

	if (m_tunnel_packets)
	{
		// send udp packets through SOCKS5 server
		wrap(hostname, port, p, len, ec);
		return;
	}

	// this function is only supported when we're using a proxy
	if (!m_queue_packets && !m_force_proxy)
	{
		address target = address::from_string(hostname, ec);
		if (!ec) send(udp::endpoint(target, port), p, len, ec, 0);
		return;
	}

	if (m_queue.size() > 1000 || (flags & dont_queue)) return;

	m_queue.push_back(queued_packet());
	queued_packet& qp = m_queue.back();
	qp.ep.port(port);

	address target = address::from_string(hostname, ec);
	if (ec) qp.ep.address(target);
	else qp.hostname = allocate_string_copy(hostname);
	qp.buf.insert(qp.buf.begin(), p, p + len);
	qp.flags = 0;
}

void udp_socket::send(udp::endpoint const& ep, char const* p, int len
	, error_code& ec, int flags)
{
	CHECK_MAGIC;

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
		|| (flags & (tracker_connection | peer_connection)) == 0
		;

	if (allow_proxy)
	{
		if (m_tunnel_packets)
		{
			// send udp packets through SOCKS5 server
			wrap(ep, p, len, ec);
			return;
		}

		if (m_queue_packets)
		{
			if (m_queue.size() > 1000 || (flags & dont_queue)) return;

			m_queue.push_back(queued_packet());
			queued_packet& qp = m_queue.back();
			qp.ep = ep;
			qp.hostname = 0;
			qp.flags = flags;
			qp.buf.insert(qp.buf.begin(), p, p + len);
			return;
		}
	}

	if (m_force_proxy) return;

#if TORRENT_USE_IPV6
	if (ep.address().is_v6() && m_ipv6_sock.is_open())
		m_ipv6_sock.send_to(boost::asio::buffer(p, len), ep, 0, ec);
	else
#endif
		m_ipv4_sock.send_to(boost::asio::buffer(p, len), ep, 0, ec);

	if (ec == error::would_block || ec == error::try_again)
	{
#if TORRENT_USE_IPV6
		if (ep.address().is_v6() && m_ipv6_sock.is_open())
		{
			if (!m_v6_write_subscribed)
			{
				m_ipv6_sock.async_send(null_buffers()
					, boost::bind(&udp_socket::on_writable, this, _1, &m_ipv6_sock));
				m_v6_write_subscribed = true;
			}
		}
		else
#endif
		{
			if (!m_v4_write_subscribed)
			{
				m_ipv4_sock.async_send(null_buffers()
					, boost::bind(&udp_socket::on_writable, this, _1, &m_ipv4_sock));
				m_v4_write_subscribed = true;
			}
		}
	}
}

void udp_socket::on_writable(error_code const& ec, udp::socket* s)
{
#if TORRENT_USE_IPV6
	if (s == &m_ipv6_sock)
		m_v6_write_subscribed = false;
	else
#else
		TORRENT_UNUSED(s);
#endif
		m_v4_write_subscribed = false;

	if (ec == boost::asio::error::operation_aborted) return;

	call_writable_handler();
}

// called whenever the socket is readable
void udp_socket::on_read(error_code const& ec, udp::socket* s)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("udp_socket::on_read");
#endif

	TORRENT_ASSERT(m_magic == 0x1337);
	TORRENT_ASSERT(is_single_thread());

#if TORRENT_USE_IPV6
	if (s == &m_ipv6_sock)
	{
		TORRENT_ASSERT(m_v6_outstanding > 0);
		--m_v6_outstanding;
	}
	else
#else
		TORRENT_UNUSED(s);
#endif
	{
		TORRENT_ASSERT(m_v4_outstanding > 0);
		--m_v4_outstanding;
	}

	if (ec == boost::asio::error::operation_aborted)
	{
#if TORRENT_USE_IPV6
		if (s == &m_ipv6_sock)
		{
			if (m_restart_v6) {
				--m_restart_v6;
				setup_read(s);
			}
		}
		else
#endif
		{
			if (m_restart_v4) {
				--m_restart_v4;
				setup_read(s);
			}
		}
		return;
	}
	if (m_abort)
	{
		close_impl();
		return;
	}

	CHECK_MAGIC;

	for (;;)
	{
		error_code err;
		udp::endpoint ep;
		size_t bytes_transferred = s->receive_from(boost::asio::buffer(m_buf, m_buf_size), ep, 0, err);

		// TODO: it would be nice to detect this on posix systems also
#ifdef TORRENT_WINDOWS
		if ((err == error_code(ERROR_MORE_DATA, system_category())
			|| err == error_code(WSAEMSGSIZE, system_category()))
			&& m_buf_size < 65536)
		{
			// if this function fails to allocate memory, m_buf_size
			// is set to 0. In that case, don't issue the async_read().
			set_buf_size(m_buf_size * 2);
			if (m_buf_size == 0) return;
			continue;
		}
#endif

		if (err == boost::asio::error::would_block || err == boost::asio::error::try_again) break;
		on_read_impl(ep, err, bytes_transferred);

		// found on iOS, socket will be disconnected when app goes backgroud. try to reopen it.
		if (err == boost::asio::error::not_connected || err == boost::asio::error::bad_descriptor)
		{
			ep = s->local_endpoint(err);
			if (!err) {
				bind(ep, err);
			}
			return;
		}
	}
	call_drained_handler();
	setup_read(s);
}

void udp_socket::call_handler(error_code const& ec, udp::endpoint const& ep, char const* buf, int size)
{
	m_observers_locked = true;
	for (std::vector<udp_socket_observer*>::iterator i = m_observers.begin();
		i != m_observers.end();)
	{
		bool ret = false;
		TORRENT_TRY {
			ret = (*i)->incoming_packet(ec, ep, buf, size);
		} TORRENT_CATCH (std::exception&) {}
		if (*i == NULL) i = m_observers.erase(i);
		else ++i;
		if (ret) break;
	}
	if (!m_added_observers.empty())
	{
		m_observers.insert(m_observers.end(), m_added_observers.begin(), m_added_observers.end());
		m_added_observers.clear();
	}
	m_observers_locked = false;
	if (m_new_buf_size != m_buf_size)
		set_buf_size(m_new_buf_size);
}

void udp_socket::call_handler(error_code const& ec, const char* host, char const* buf, int size)
{
	m_observers_locked = true;
	for (std::vector<udp_socket_observer*>::iterator i = m_observers.begin();
		i != m_observers.end();)
	{
		bool ret = false;
		TORRENT_TRY {
			ret = (*i)->incoming_packet(ec, host, buf, size);
		} TORRENT_CATCH (std::exception&) {}
		if (*i == NULL) i = m_observers.erase(i);
		else ++i;
		if (ret) break;
	}
	if (!m_added_observers.empty())
	{
		m_observers.insert(m_observers.end(), m_added_observers.begin(), m_added_observers.end());
		m_added_observers.clear();
	}
	m_observers_locked = false;
	if (m_new_buf_size != m_buf_size)
		set_buf_size(m_new_buf_size);
}

void udp_socket::call_drained_handler()
{
	m_observers_locked = true;
	for (std::vector<udp_socket_observer*>::iterator i = m_observers.begin();
		i != m_observers.end();)
	{
		TORRENT_TRY {
			(*i)->socket_drained();
		} TORRENT_CATCH (std::exception&) {}
		if (*i == NULL) i = m_observers.erase(i);
		else ++i;
	}
	if (!m_added_observers.empty())
	{
		m_observers.insert(m_observers.end(), m_added_observers.begin(), m_added_observers.end());
		m_added_observers.clear();
	}
	m_observers_locked = false;
	if (m_new_buf_size != m_buf_size)
		set_buf_size(m_new_buf_size);
}

void udp_socket::call_writable_handler()
{
	m_observers_locked = true;
	for (std::vector<udp_socket_observer*>::iterator i = m_observers.begin();
		i != m_observers.end();)
	{
		TORRENT_TRY {
			(*i)->writable();
		} TORRENT_CATCH (std::exception&) {}
		if (*i == NULL) i = m_observers.erase(i);
		else ++i;
	}
	if (!m_added_observers.empty())
	{
		m_observers.insert(m_observers.end(), m_added_observers.begin(), m_added_observers.end());
		m_added_observers.clear();
	}
	m_observers_locked = false;
	if (m_new_buf_size != m_buf_size)
		set_buf_size(m_new_buf_size);
}

void udp_socket::subscribe(udp_socket_observer* o)
{
	TORRENT_ASSERT(std::find(m_observers.begin(), m_observers.end(), o) == m_observers.end());
	if (m_observers_locked)
		m_added_observers.push_back(o);
	else
		m_observers.push_back(o);
}

void udp_socket::unsubscribe(udp_socket_observer* o)
{
	std::vector<udp_socket_observer*>::iterator i = std::find(m_observers.begin(), m_observers.end(), o);
	if (i == m_observers.end()) return;
	if (m_observers_locked)
		*i = NULL;
	else
		m_observers.erase(i);
}

void udp_socket::on_read_impl(udp::endpoint const& ep
	, error_code const& e, std::size_t bytes_transferred)
{
	TORRENT_ASSERT(m_magic == 0x1337);
	TORRENT_ASSERT(is_single_thread());

	if (e)
	{
		call_handler(e, ep, 0, 0);

		// don't stop listening on recoverable errors
		if (e != boost::asio::error::host_unreachable
			&& e != boost::asio::error::fault
			&& e != boost::asio::error::connection_reset
			&& e != boost::asio::error::connection_refused
			&& e != boost::asio::error::connection_aborted
			&& e != boost::asio::error::operation_aborted
			&& e != boost::asio::error::network_reset
			&& e != boost::asio::error::network_unreachable
#ifdef _WIN32
			// ERROR_MORE_DATA means the same thing as EMSGSIZE
			&& e != error_code(ERROR_MORE_DATA, system_category())
			&& e != error_code(ERROR_HOST_UNREACHABLE, system_category())
			&& e != error_code(ERROR_PORT_UNREACHABLE, system_category())
			&& e != error_code(ERROR_RETRY, system_category())
			&& e != error_code(ERROR_NETWORK_UNREACHABLE, system_category())
			&& e != error_code(ERROR_CONNECTION_REFUSED, system_category())
			&& e != error_code(ERROR_CONNECTION_ABORTED, system_category())
#endif
			&& e != boost::asio::error::message_size)
		{
			return;
		}

		if (m_abort)
		{
			close_impl();
			return;
		}

		return;
	}

	TORRENT_TRY {

		if (m_tunnel_packets)
		{
			// if the source IP doesn't match the proxy's, ignore the packet
			if (ep == m_udp_proxy_addr)
				unwrap(e, m_buf, bytes_transferred);
		}
		else if (!m_force_proxy) // block incoming packets that aren't coming via the proxy
		{
			call_handler(e, ep, m_buf, bytes_transferred);
		}

	} TORRENT_CATCH (std::exception&) {}
}

void udp_socket::setup_read(udp::socket* s)
{
	if (m_abort)
	{
		close_impl();
		return;
	}

#if TORRENT_USE_IPV6
	if (s == &m_ipv6_sock)
	{
		if (m_v6_outstanding)
		{
			++m_restart_v6;
			m_ipv6_sock.cancel();
			return;
		}
		++m_v6_outstanding;
	}
	else
#endif
	{
		if (m_v4_outstanding)
		{
			++m_restart_v4;
			m_ipv4_sock.cancel();
			return;
		}
		++m_v4_outstanding;
	}

#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("udp_socket::on_read");
#endif

	udp::endpoint ep;
	TORRENT_TRY
	{
#if TORRENT_USE_IPV6
		if (s == &m_ipv6_sock)
		{
			s->async_receive_from(null_buffers()
				, ep, make_read_handler6(boost::bind(&udp_socket::on_read, this, _1, s)));
		}
		else
#endif
		{
			s->async_receive_from(null_buffers()
				, ep, make_read_handler4(boost::bind(&udp_socket::on_read, this, _1, s)));
		}
	}
	TORRENT_CATCH(boost::system::system_error& e)
	{
#ifdef BOOST_NO_EXCEPTIONS
		// dummy
		error_code ec;
		boost::system::system_error e(ec);
#endif
		get_io_service().post(boost::bind(&udp_socket::on_read
			, this, e.code(), s));
	}
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
	write_endpoint(ep, h);

	boost::array<boost::asio::const_buffer, 2> iovec;
	iovec[0] = boost::asio::const_buffer(header, h - header);
	iovec[1] = boost::asio::const_buffer(p, len);

#if TORRENT_USE_IPV6
	if (m_udp_proxy_addr.address().is_v4() && m_ipv4_sock.is_open())
#endif
		m_ipv4_sock.send_to(iovec, m_udp_proxy_addr, 0, ec);
#if TORRENT_USE_IPV6
	else
		m_ipv6_sock.send_to(iovec, m_udp_proxy_addr, 0, ec);
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

	boost::array<boost::asio::const_buffer, 2> iovec;
	iovec[0] = boost::asio::const_buffer(header, h - header);
	iovec[1] = boost::asio::const_buffer(p, len);

#if TORRENT_USE_IPV6
	if (m_udp_proxy_addr.address().is_v6() && m_ipv6_sock.is_open())
		m_ipv6_sock.send_to(iovec, m_udp_proxy_addr, 0, ec);
	else
#endif
		m_ipv4_sock.send_to(iovec, m_udp_proxy_addr, 0, ec);
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
		call_handler(e, hostname.c_str(), p, size - (p - buf));
		return;
	}

	call_handler(e, sender, p, size - (p - buf));
}

#if !defined BOOST_ASIO_ENABLE_CANCELIO && defined TORRENT_WINDOWS
#error BOOST_ASIO_ENABLE_CANCELIO needs to be defined when building libtorrent to enable cancel() in asio on windows
#endif

void udp_socket::close()
{
	TORRENT_ASSERT(is_single_thread());
	TORRENT_ASSERT(m_magic == 0x1337);

	error_code ec;
	m_ipv4_sock.close(ec);
	TORRENT_ASSERT_VAL(!ec || ec == error::bad_descriptor, ec);
#if TORRENT_USE_IPV6
	m_ipv6_sock.close(ec);
	TORRENT_ASSERT_VAL(!ec || ec == error::bad_descriptor, ec);
#endif
	m_socks5_sock.close(ec);
	TORRENT_ASSERT_VAL(!ec || ec == error::bad_descriptor, ec);
	m_resolver.cancel();
	m_timer.cancel();
	m_abort = true;

#if TORRENT_USE_ASSERTS
	m_outstanding_when_aborted = num_outstanding();
#endif
}

void udp_socket::set_buf_size(int s)
{
	TORRENT_ASSERT(is_single_thread());

	if (m_observers_locked)
	{
		// we can't actually reallocate the buffer while
		// it's being used by the observers, we have to
		// do that once we're done iterating over them
		m_new_buf_size = s;
		return;
	}

	if (s == m_buf_size) return;

	bool no_mem = false;
	char* tmp = static_cast<char*>(realloc(m_buf, s));
	if (tmp != 0)
	{
		m_buf = tmp;
		m_buf_size = s;
		m_new_buf_size = s;
	}
	else
	{
		no_mem = true;
	}

	if (no_mem)
	{
		std::free(m_buf);
		m_buf = 0;
		m_buf_size = 0;
		m_new_buf_size = 0;
		udp::endpoint ep;
		call_handler(error::no_memory, ep, 0, 0);
		close();
	}

	int size = m_buf_size;

	// don't shrink the size of the receive buffer
	error_code ec;
	boost::asio::socket_base::receive_buffer_size recv_size;
	m_ipv4_sock.get_option(recv_size, ec);
	if (!ec) size = (std::max)(recv_size.value(), size);
#if TORRENT_USE_IPV6
	m_ipv6_sock.get_option(recv_size, ec);
	if (!ec) size = (std::max)(recv_size.value(), size);
#endif

	error_code ignore_errors;
	// set the internal buffer sizes as well
	m_ipv4_sock.set_option(boost::asio::socket_base::receive_buffer_size(size)
		, ignore_errors);
#if TORRENT_USE_IPV6
	m_ipv6_sock.set_option(boost::asio::socket_base::receive_buffer_size(size)
		, ignore_errors);
#endif
}

void udp_socket::bind(udp::endpoint const& ep, error_code& ec)
{
	CHECK_MAGIC;
	TORRENT_ASSERT(is_single_thread());

	m_abort = false;

	if (m_ipv4_sock.is_open()) m_ipv4_sock.close(ec);
#if TORRENT_USE_IPV6
	if (m_ipv6_sock.is_open()) m_ipv6_sock.close(ec);
#endif
	ec.clear();

	if (ep.address().is_v4())
	{
		m_ipv4_sock.open(udp::v4(), ec);
		if (ec) return;

		// this is best-effort. ignore errors
		error_code err;
#ifdef TORRENT_WINDOWS
		m_ipv4_sock.set_option(exclusive_address_use(true), err);
#else
		m_ipv4_sock.set_option(boost::asio::socket_base::reuse_address(true), err);
#endif

		m_ipv4_sock.bind(ep, ec);
		if (ec) return;
		m_ipv4_sock.non_blocking(true, ec);
		if (ec) return;
		setup_read(&m_ipv4_sock);
	}

#if TORRENT_USE_IPV6
	// TODO: 2 the udp_socket should really just be a single socket, and the
	// session should support having more than one, just like with TCP sockets
	// for now, just make bind failures non-fatal
	if (supports_ipv6() && (ep.address().is_v6() || is_any(ep.address())))
	{
		udp::endpoint ep6 = ep;
		if (is_any(ep.address())) ep6.address(address_v6::any());
		m_ipv6_sock.open(udp::v6(), ec);
		if (ec) return;

		// this is best-effort. ignore errors
		error_code err;
#ifdef TORRENT_WINDOWS
		m_ipv6_sock.set_option(exclusive_address_use(true), err);
#else
		m_ipv6_sock.set_option(boost::asio::socket_base::reuse_address(true), err);
#endif
		m_ipv6_sock.set_option(boost::asio::ip::v6_only(true), err);

		m_ipv6_sock.bind(ep6, ec);
		if (ec != error_code(boost::system::errc::address_not_available
			, boost::system::generic_category()))
		{
			if (ec) return;
			m_ipv6_sock.non_blocking(true, ec);
			if (ec) return;
			setup_read(&m_ipv6_sock);
		}
		else
		{
			ec.clear();
		}
	}
#endif

	error_code err;
	m_bind_port = m_ipv4_sock.local_endpoint(err).port();
	if (err) m_bind_port = ep.port();

#if TORRENT_USE_ASSERTS
	m_started = true;
#endif
}

void udp_socket::set_proxy_settings(aux::proxy_settings const& ps)
{
	CHECK_MAGIC;
	TORRENT_ASSERT(is_single_thread());

	error_code ec;
	m_socks5_sock.close(ec);
	m_tunnel_packets = false;

	m_proxy_settings = ps;

	if (m_abort)
	{
		close_impl();
		return;
	}

	if (ps.type == settings_pack::socks5
		|| ps.type == settings_pack::socks5_pw)
	{
		m_queue_packets = true;
		// connect to socks5 server and open up the UDP tunnel

		// TODO: use the system resolver_interface here
		tcp::resolver::query q(ps.hostname, to_string(ps.port).elems);
		++m_outstanding_ops;
#if TORRENT_USE_ASSERTS
		++m_outstanding_resolve;
#endif
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("udp_socket::on_name_lookup");
#endif
		m_resolver.async_resolve(q, boost::bind(
			&udp_socket::on_name_lookup, this, _1, _2));
	}
}

void udp_socket::close_impl()
{
	if (m_outstanding_ops == 0)
	{
		error_code ec;
		m_ipv4_sock.close(ec);
#if TORRENT_USE_IPV6
		m_ipv6_sock.close(ec);
#endif
		m_socks5_sock.close(ec);
	}
}

void udp_socket::on_name_lookup(error_code const& e, tcp::resolver::iterator i)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("udp_socket::on_name_lookup");
#endif
#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(m_outstanding_resolve > 0);
	--m_outstanding_resolve;
#endif

	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	TORRENT_ASSERT(m_outstanding_ops == m_outstanding_connect
		+ m_outstanding_timeout
		+ m_outstanding_resolve
		+ m_outstanding_socks);

	if (m_abort)
	{
		close_impl();
		return;
	}

	CHECK_MAGIC;

	if (e == boost::asio::error::operation_aborted) return;

	TORRENT_ASSERT(is_single_thread());

	if (e)
	{
		if (m_force_proxy)
		{
			call_handler(e, udp::endpoint(), 0, 0);
		}
		else
		{
			// if we can't connect to the proxy, and
			// we're not in privacy mode, try to just
			// not use a proxy
			m_proxy_settings = aux::proxy_settings();
			m_tunnel_packets = false;
		}

		drain_queue();
		return;
	}

	m_proxy_addr.address(i->endpoint().address());
	m_proxy_addr.port(i->endpoint().port());

#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("udp_socket::on_connected");
#endif

	error_code ec;
	m_socks5_sock.open(m_proxy_addr.address().is_v4()?tcp::v4():tcp::v6(), ec);

	// enable keepalives
	m_socks5_sock.set_option(boost::asio::socket_base::keep_alive(true), ec);

	++m_outstanding_ops;
#if TORRENT_USE_ASSERTS
	++m_outstanding_connect;
#endif
	m_socks5_sock.async_connect(tcp::endpoint(m_proxy_addr.address(), m_proxy_addr.port())
		, boost::bind(&udp_socket::on_connected, this, _1));

	++m_outstanding_ops;
#if TORRENT_USE_ASSERTS
	++m_outstanding_timeout;
#endif

#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("udp_socket::on_connect_timeout");
#endif
	m_timer.expires_from_now(seconds(10));
	m_timer.async_wait(boost::bind(&udp_socket::on_connect_timeout
		, this, _1));
}

void udp_socket::on_connect_timeout(error_code const& ec)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("udp_socket::on_connect_timeout");
#endif
#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(m_outstanding_timeout > 0);
	--m_outstanding_timeout;
#endif
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	TORRENT_ASSERT(m_outstanding_ops == m_outstanding_connect
		+ m_outstanding_timeout
		+ m_outstanding_resolve
		+ m_outstanding_socks);

	if (ec == boost::asio::error::operation_aborted) return;

	m_queue_packets = false;

	if (m_abort)
	{
		close_impl();
		return;
	}

	CHECK_MAGIC;
	TORRENT_ASSERT(is_single_thread());

	error_code ignore;
	m_socks5_sock.close(ignore);
}

void udp_socket::on_connected(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("udp_socket::on_connected");
#endif

	TORRENT_ASSERT(is_single_thread());

#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(m_outstanding_connect > 0);
	--m_outstanding_connect;
#endif
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	TORRENT_ASSERT(m_outstanding_ops == m_outstanding_connect
		+ m_outstanding_timeout
		+ m_outstanding_resolve
		+ m_outstanding_socks);
	CHECK_MAGIC;

	m_timer.cancel();

	if (e == boost::asio::error::operation_aborted) return;

	if (m_abort)
	{
		close_impl();
		return;
	}

	if (e)
	{
		// we failed to connect to the proxy, if we don't have force_proxy set,
		// drain the queue over the UDP socket
		if (!m_force_proxy)
		{
			drain_queue();
		}

		call_handler(e, udp::endpoint(), 0, 0);
		return;
	}

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
#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("udp_socket::on_handshake1");
#endif
	++m_outstanding_ops;
#if TORRENT_USE_ASSERTS
	++m_outstanding_socks;
#endif
	boost::asio::async_write(m_socks5_sock, boost::asio::buffer(m_tmp_buf, p - m_tmp_buf)
		, boost::bind(&udp_socket::handshake1, this, _1));
}

void udp_socket::handshake1(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("udp_socket::on_handshake1");
#endif
#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(m_outstanding_socks > 0);
	--m_outstanding_socks;
#endif
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	TORRENT_ASSERT(m_outstanding_ops == m_outstanding_connect
		+ m_outstanding_timeout
		+ m_outstanding_resolve
		+ m_outstanding_socks);
	if (m_abort)
	{
		close_impl();
		return;
	}
	CHECK_MAGIC;
	if (e)
	{
		drain_queue();
		return;
	}

	TORRENT_ASSERT(is_single_thread());

#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("udp_socket::on_handshake2");
#endif
	++m_outstanding_ops;
#if TORRENT_USE_ASSERTS
	++m_outstanding_socks;
#endif
	boost::asio::async_read(m_socks5_sock, boost::asio::buffer(m_tmp_buf, 2)
		, boost::bind(&udp_socket::handshake2, this, _1));
}

void udp_socket::handshake2(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("udp_socket::on_handshake2");
#endif
#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(m_outstanding_socks > 0);
	--m_outstanding_socks;
#endif
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	TORRENT_ASSERT(m_outstanding_ops == m_outstanding_connect
		+ m_outstanding_timeout
		+ m_outstanding_resolve
		+ m_outstanding_socks);
	if (m_abort)
	{
		close_impl();
		return;
	}
	CHECK_MAGIC;

	if (e)
	{
		drain_queue();
		return;
	}

	using namespace libtorrent::detail;

	TORRENT_ASSERT(is_single_thread());

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p);
	int method = read_uint8(p);

	if (version < 5)
	{
		error_code ec;
		m_socks5_sock.close(ec);
		drain_queue();
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
			drain_queue();
			return;
		}

		// start sub-negotiation
		p = &m_tmp_buf[0];
		write_uint8(1, p);
		write_uint8(m_proxy_settings.username.size(), p);
		write_string(m_proxy_settings.username, p);
		write_uint8(m_proxy_settings.password.size(), p);
		write_string(m_proxy_settings.password, p);
		TORRENT_ASSERT_VAL(p - m_tmp_buf < int(sizeof(m_tmp_buf)), (p - m_tmp_buf));
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("udp_socket::on_handshake3");
#endif
		++m_outstanding_ops;
#if TORRENT_USE_ASSERTS
		++m_outstanding_socks;
#endif
		boost::asio::async_write(m_socks5_sock, boost::asio::buffer(m_tmp_buf, p - m_tmp_buf)
			, boost::bind(&udp_socket::handshake3, this, _1));
	}
	else
	{
		drain_queue();
		error_code ec;
		m_socks5_sock.close(ec);
		return;
	}
}

void udp_socket::handshake3(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("udp_socket::on_handshake3");
#endif
#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(m_outstanding_socks > 0);
	--m_outstanding_socks;
#endif
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	TORRENT_ASSERT(m_outstanding_ops == m_outstanding_connect
		+ m_outstanding_timeout
		+ m_outstanding_resolve
		+ m_outstanding_socks);
	if (m_abort)
	{
		close_impl();
		return;
	}
	CHECK_MAGIC;
	if (e)
	{
		drain_queue();
		return;
	}

	TORRENT_ASSERT(is_single_thread());

#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("udp_socket::on_handshake4");
#endif
	++m_outstanding_ops;
#if TORRENT_USE_ASSERTS
	++m_outstanding_socks;
#endif
	boost::asio::async_read(m_socks5_sock, boost::asio::buffer(m_tmp_buf, 2)
		, boost::bind(&udp_socket::handshake4, this, _1));
}

void udp_socket::handshake4(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("udp_socket::on_handshake4");
#endif
#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(m_outstanding_socks > 0);
	--m_outstanding_socks;
#endif
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	TORRENT_ASSERT(m_outstanding_ops == m_outstanding_connect
		+ m_outstanding_timeout
		+ m_outstanding_resolve
		+ m_outstanding_socks);
	if (m_abort)
	{
		close_impl();
		return;
	}
	CHECK_MAGIC;
	if (e)
	{
		drain_queue();
		return;
	}

	TORRENT_ASSERT(is_single_thread());

	using namespace libtorrent::detail;

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p);
	int status = read_uint8(p);

	if (version != 1 || status != 0)
	{
		drain_queue();
		return;
	}

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
	write_uint8(1, p); // ATYP = IPv4
	write_uint32(0, p); // 0.0.0.0
	write_uint16(0, p); // :0
	TORRENT_ASSERT_VAL(p - m_tmp_buf < int(sizeof(m_tmp_buf)), (p - m_tmp_buf));
#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("udp_socket::connect1");
#endif
	++m_outstanding_ops;
#if TORRENT_USE_ASSERTS
	++m_outstanding_socks;
#endif
	boost::asio::async_write(m_socks5_sock, boost::asio::buffer(m_tmp_buf, p - m_tmp_buf)
		, boost::bind(&udp_socket::connect1, this, _1));
}

void udp_socket::connect1(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("udp_socket::connect1");
#endif
#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(m_outstanding_socks > 0);
	--m_outstanding_socks;
#endif
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	TORRENT_ASSERT(m_outstanding_ops == m_outstanding_connect
		+ m_outstanding_timeout
		+ m_outstanding_resolve
		+ m_outstanding_socks);
	if (m_abort)
	{
		close_impl();
		return;
	}
	CHECK_MAGIC;
	if (e)
	{
		drain_queue();
		return;
	}

	TORRENT_ASSERT(is_single_thread());

#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("udp_socket::connect2");
#endif
	++m_outstanding_ops;
#if TORRENT_USE_ASSERTS
	++m_outstanding_socks;
#endif
	boost::asio::async_read(m_socks5_sock, boost::asio::buffer(m_tmp_buf, 10)
		, boost::bind(&udp_socket::connect2, this, _1));
}

void udp_socket::connect2(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("udp_socket::connect2");
#endif
#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(m_outstanding_socks > 0);
	--m_outstanding_socks;
#endif
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	TORRENT_ASSERT(m_outstanding_ops == m_outstanding_connect
		+ m_outstanding_timeout
		+ m_outstanding_resolve
		+ m_outstanding_socks);

	if (m_abort)
	{
		m_queue.clear();
		return;
	}
	CHECK_MAGIC;
	if (e)
	{
		drain_queue();
		return;
	}

	TORRENT_ASSERT(is_single_thread());

	using namespace libtorrent::detail;

	char* p = &m_tmp_buf[0];
	int version = read_uint8(p); // VERSION
	int status = read_uint8(p); // STATUS
	++p; // RESERVED
	int atyp = read_uint8(p); // address type

	if (version != 5 || status != 0)
	{
		drain_queue();
		return;
	}

	if (atyp == 1)
	{
		m_udp_proxy_addr.address(address_v4(read_uint32(p)));
		m_udp_proxy_addr.port(read_uint16(p));
	}
	else
	{
		// in this case we need to read more data from the socket
		TORRENT_ASSERT(false);
		drain_queue();
		return;
	}

	m_tunnel_packets = true;
	drain_queue();

#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("udp_socket::hung_up");
#endif
	++m_outstanding_ops;
#if TORRENT_USE_ASSERTS
	++m_outstanding_socks;
#endif
	boost::asio::async_read(m_socks5_sock, boost::asio::buffer(m_tmp_buf, 10)
		, boost::bind(&udp_socket::hung_up, this, _1));
}

void udp_socket::hung_up(error_code const& e)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("udp_socket::hung_up");
#endif
#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(m_outstanding_socks > 0);
	--m_outstanding_socks;
#endif
	TORRENT_ASSERT(m_outstanding_ops > 0);
	--m_outstanding_ops;
	TORRENT_ASSERT(m_outstanding_ops == m_outstanding_connect
		+ m_outstanding_timeout
		+ m_outstanding_resolve
		+ m_outstanding_socks);
	if (m_abort)
	{
		close_impl();
		return;
	}
	CHECK_MAGIC;
	TORRENT_ASSERT(is_single_thread());

	if (e == boost::asio::error::operation_aborted || m_abort) return;

	// the socks connection was closed, re-open it in a bit
	m_retry_timer.expires_from_now(seconds(5));
	m_retry_timer.async_wait(boost::bind(&udp_socket::retry_socks_connect
		, this, _1));
}

void udp_socket::retry_socks_connect(error_code const& ec)
{
	if (ec) return;
	set_proxy_settings(m_proxy_settings);
}

void udp_socket::drain_queue()
{
	m_queue_packets = false;

	// forward all packets that were put in the queue
	while (!m_queue.empty())
	{
		queued_packet const& p = m_queue.front();
		error_code ec;
		if (p.hostname)
		{
			udp_socket::send_hostname(p.hostname, p.ep.port(), &p.buf[0]
				, p.buf.size(), ec, p.flags | dont_queue);
			std::free(p.hostname);
		}
		else
		{
			udp_socket::send(p.ep, &p.buf[0], p.buf.size(), ec, p.flags | dont_queue);
		}
		m_queue.pop_front();
	}
}

rate_limited_udp_socket::rate_limited_udp_socket(io_service& ios)
	: udp_socket(ios)
	, m_rate_limit(8000)
	, m_quota(8000)
	, m_last_tick(aux::time_now())
{
}

bool rate_limited_udp_socket::has_quota()
{
	time_point now = clock_type::now();
	time_duration delta = now - m_last_tick;
	m_last_tick = now;
	// add any new quota we've accrued since last time
	m_quota += boost::uint64_t(m_rate_limit) * total_microseconds(delta) / 1000000;
	return m_quota > 0;
}

bool rate_limited_udp_socket::send(udp::endpoint const& ep, char const* p
	, int len, error_code& ec, int flags)
{
	time_point now = clock_type::now();
	time_duration delta = now - m_last_tick;
	m_last_tick = now;

	// add any new quota we've accrued since last time
	m_quota += boost::uint64_t(m_rate_limit) * total_microseconds(delta) / 1000000;

	// allow 3 seconds worth of burst
	if (m_quota > 3 * m_rate_limit) m_quota = 3 * m_rate_limit;

	// if there's no quota, and it's OK to drop, just
	// drop the packet
	if (m_quota < 0 && (flags & dont_drop) == 0) return false;

	m_quota -= len;
	if (m_quota < 0) m_quota = 0;
	udp_socket::send(ep, p, len, ec, flags);
	return true;
}

