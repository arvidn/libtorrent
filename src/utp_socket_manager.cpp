/*

Copyright (c) 2009-2018, Arvid Norberg
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

#include "libtorrent/utp_stream.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/utp_socket_manager.hpp"
#include "libtorrent/aux_/instantiate_connection.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/socket.hpp" // for TORRENT_HAS_DONT_FRAGMENT
#include "libtorrent/broadcast_socket.hpp" // for is_teredo
#include "libtorrent/random.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/time.hpp" // for aux::time_now()
#include "libtorrent/span.hpp"

// #define TORRENT_DEBUG_MTU 1135

namespace libtorrent {

	utp_socket_manager::utp_socket_manager(
		send_fun_t const& send_fun
		, incoming_utp_callback_t const& cb
		, io_service& ios
		, aux::session_settings const& sett
		, counters& cnt
		, void* ssl_context)
		: m_send_fun(send_fun)
		, m_cb(cb)
		, m_sett(sett)
		, m_counters(cnt)
		, m_ios(ios)
		, m_ssl_context(ssl_context)
	{
		m_restrict_mtu.fill(65536);
	}

	utp_socket_manager::~utp_socket_manager()
	{
		for (auto& i : m_utp_sockets)
		{
			delete_utp_impl(i.second);
		}
	}

	void utp_socket_manager::tick(time_point now)
	{
		for (auto i = m_utp_sockets.begin()
			, end(m_utp_sockets.end()); i != end;)
		{
			if (should_delete(i->second))
			{
				delete_utp_impl(i->second);
				if (m_last_socket == i->second) m_last_socket = nullptr;
				i = m_utp_sockets.erase(i);
				continue;
			}
			tick_utp_impl(i->second, now);
			++i;
		}
	}

	std::pair<int, int> utp_socket_manager::mtu_for_dest(address const& addr)
	{
		int mtu = 0;
		if (is_teredo(addr)) mtu = TORRENT_TEREDO_MTU;
		else mtu = TORRENT_ETHERNET_MTU;

#if defined __APPLE__
		// apple has a very strange loopback. It appears you can't
		// send messages of the reported MTU size, and you don't get
		// EWOULDBLOCK either.
		if (is_loopback(addr))
		{
			if (is_teredo(addr)) mtu = TORRENT_TEREDO_MTU;
			else mtu = TORRENT_ETHERNET_MTU;
		}
#endif

		int const link_mtu = mtu;

		mtu -= TORRENT_UDP_HEADER;

		if (m_sett.get_int(settings_pack::proxy_type) == settings_pack::socks5
			|| m_sett.get_int(settings_pack::proxy_type) == settings_pack::socks5_pw)
		{
			// this is for the IP layer
			// assume the proxy is running over IPv4
			mtu -= TORRENT_IPV4_HEADER;

			// this is for the SOCKS layer
			mtu -= TORRENT_SOCKS5_HEADER;

			// the address field in the SOCKS header
			if (addr.is_v4()) mtu -= 4;
			else mtu -= 16;
		}
		else
		{
			if (addr.is_v4()) mtu -= TORRENT_IPV4_HEADER;
			else mtu -= TORRENT_IPV6_HEADER;
		}

		return std::make_pair(link_mtu, std::min(mtu, restrict_mtu()));
	}

	void utp_socket_manager::send_packet(std::weak_ptr<utp_socket_interface> sock
		, udp::endpoint const& ep, char const* p
		, int const len, error_code& ec, udp_send_flags_t const flags)
	{
#if !defined TORRENT_HAS_DONT_FRAGMENT && !defined TORRENT_DEBUG_MTU
		TORRENT_UNUSED(flags);
#endif

#ifdef TORRENT_DEBUG_MTU
		// drop packets that exceed the debug MTU
		if ((flags & dont_fragment) && len > TORRENT_DEBUG_MTU) return;
#endif

		m_send_fun(std::move(sock), ep, {p, len}, ec
			, (flags & udp_socket::dont_fragment)
				| udp_socket::peer_connection);
	}

	bool utp_socket_manager::incoming_packet(std::weak_ptr<utp_socket_interface> socket
		, udp::endpoint const& ep, span<char const> p)
	{
//		UTP_LOGV("incoming packet size:%d\n", size);

		if (p.size() < std::ptrdiff_t(sizeof(utp_header))) return false;

		auto const* ph = reinterpret_cast<utp_header const*>(p.data());

//		UTP_LOGV("incoming packet version:%d\n", int(ph->get_version()));

		if (ph->get_version() != 1) return false;

		const time_point receive_time = clock_type::now();

		// parse out connection ID and look for existing
		// connections. If found, forward to the utp_stream.
		std::uint16_t id = ph->connection_id;

		// first test to see if it's the same socket as last time
		// in most cases it is
		if (m_last_socket
			&& utp_match(m_last_socket, ep, id))
		{
			return utp_incoming_packet(m_last_socket, p, ep, receive_time);
		}

		if (m_deferred_ack)
		{
			utp_send_ack(m_deferred_ack);
			m_deferred_ack = nullptr;
		}

		auto r = m_utp_sockets.equal_range(id);

		for (; r.first != r.second; ++r.first)
		{
			if (!utp_match(r.first->second, ep, id)) continue;
			bool ret = utp_incoming_packet(r.first->second, p, ep, receive_time);
			if (ret) m_last_socket = r.first->second;
			return ret;
		}

//		UTP_LOGV("incoming packet id:%d source:%s\n", id, print_endpoint(ep).c_str());

		if (!m_sett.get_bool(settings_pack::enable_incoming_utp))
			return false;

		// if not found, see if it's a SYN packet, if it is,
		// create a new utp_stream
		if (ph->get_type() == ST_SYN)
		{
			// possible SYN flood. Just ignore
			if (int(m_utp_sockets.size()) > m_sett.get_int(settings_pack::connections_limit) * 2)
				return false;

//			UTP_LOGV("not found, new connection id:%d\n", m_new_connection);

			std::shared_ptr<aux::socket_type> c(new (std::nothrow) aux::socket_type(m_ios));
			if (!c) return false;

			TORRENT_ASSERT(m_new_connection == -1);
			// create the new socket with this ID
			m_new_connection = id;

			aux::instantiate_connection(m_ios, aux::proxy_settings(), *c
				, m_ssl_context, this, true, false);

			utp_stream* str = nullptr;
#ifdef TORRENT_USE_OPENSSL
			if (is_ssl(*c))
				str = &c->get<ssl_stream<utp_stream>>()->next_layer();
			else
#endif
				str = c->get<utp_stream>();

			TORRENT_ASSERT(str);
			int link_mtu, utp_mtu;
			std::tie(link_mtu, utp_mtu) = mtu_for_dest(ep.address());
			utp_init_mtu(str->get_impl(), link_mtu, utp_mtu);
			utp_init_socket(str->get_impl(), std::move(socket));
			bool ret = utp_incoming_packet(str->get_impl(), p, ep, receive_time);
			if (!ret) return false;
			m_last_socket = str->get_impl();
			m_cb(c);
			// the connection most likely changed its connection ID here
			// we need to move it to the correct ID
			return true;
		}

		if (ph->get_type() == ST_RESET) return false;

		// #error send reset

		return false;
	}

	void utp_socket_manager::subscribe_writable(utp_socket_impl* s)
	{
		TORRENT_ASSERT(std::find(m_stalled_sockets.begin(), m_stalled_sockets.end()
			, s) == m_stalled_sockets.end());
		m_stalled_sockets.push_back(s);
	}

	void utp_socket_manager::writable()
	{
		if (!m_stalled_sockets.empty())
		{
			m_temp_sockets.clear();
			m_stalled_sockets.swap(m_temp_sockets);
			for (auto const &s : m_temp_sockets)
			{
				utp_writable(s);
			}
		}
	}

	void utp_socket_manager::socket_drained()
	{
		if (m_deferred_ack)
		{
			utp_socket_impl* s = m_deferred_ack;
			m_deferred_ack = nullptr;
			utp_send_ack(s);
		}

		if (!m_drained_event.empty())
		{
			m_temp_sockets.clear();
			m_drained_event.swap(m_temp_sockets);
			for (auto const &s : m_temp_sockets)
			{
				utp_socket_drained(s);
			}
		}
	}

	void utp_socket_manager::defer_ack(utp_socket_impl* s)
	{
		TORRENT_ASSERT(m_deferred_ack == NULL || m_deferred_ack == s);
		m_deferred_ack = s;
	}

	void utp_socket_manager::subscribe_drained(utp_socket_impl* s)
	{
		TORRENT_ASSERT(std::find(m_drained_event.begin(), m_drained_event.end(), s)
			== m_drained_event.end());
		m_drained_event.push_back(s);
	}

	void utp_socket_manager::remove_udp_socket(std::weak_ptr<utp_socket_interface> sock)
	{
		for (auto& s : m_utp_sockets)
		{
			if (!bound_to_udp_socket(s.second, sock))
				continue;

			utp_abort(s.second);
		}
	}

	void utp_socket_manager::remove_socket(std::uint16_t const id)
	{
		auto const i = m_utp_sockets.find(id);
		if (i == m_utp_sockets.end()) return;
		delete_utp_impl(i->second);
		if (m_last_socket == i->second) m_last_socket = nullptr;
		if (m_deferred_ack == i->second) m_deferred_ack = nullptr;
		m_utp_sockets.erase(i);
	}

	void utp_socket_manager::inc_stats_counter(int counter, int delta)
	{
		TORRENT_ASSERT((counter >= counters::utp_packet_loss
				&& counter <= counters::utp_redundant_pkts_in)
			|| (counter >= counters::num_utp_idle
				&& counter <= counters::num_utp_deleted));
		m_counters.inc_stats_counter(counter, delta);
	}

	utp_socket_impl* utp_socket_manager::new_utp_socket(utp_stream* str)
	{
		std::uint16_t send_id = 0;
		std::uint16_t recv_id = 0;
		if (m_new_connection != -1)
		{
			send_id = std::uint16_t(m_new_connection);
			recv_id = std::uint16_t(m_new_connection + 1);
			m_new_connection = -1;
		}
		else
		{
			send_id = std::uint16_t(random(0xffff));
			recv_id = send_id - 1;
		}
		utp_socket_impl* impl = construct_utp_impl(recv_id, send_id, str, *this);
		m_utp_sockets.emplace(recv_id, impl);
		return impl;
	}
}
