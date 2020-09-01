/*

Copyright (c) 2010-2020, Arvid Norberg
Copyright (c) 2016-2019, Alden Torres
Copyright (c) 2017, Steven Siloti
Copyright (c) 2017, Andrei Kurushin
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/utp_stream.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/aux_/utp_socket_manager.hpp"
#include "libtorrent/aux_/instantiate_connection.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/socket.hpp" // for TORRENT_HAS_DONT_FRAGMENT
#include "libtorrent/aux_/ip_helpers.hpp" // for is_teredo
#include "libtorrent/random.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/time.hpp" // for aux::time_now()
#include "libtorrent/span.hpp"

// #define TORRENT_DEBUG_MTU 1135

namespace libtorrent {
namespace aux {

	utp_socket_manager::utp_socket_manager(
		send_fun_t send_fun
		, incoming_utp_callback_t cb
		, io_context& ios
		, aux::session_settings const& sett
		, counters& cnt
		, void* ssl_context)
		: m_send_fun(std::move(send_fun))
		, m_cb(std::move(cb))
		, m_sett(sett)
		, m_counters(cnt)
		, m_ios(ios)
		, m_ssl_context(ssl_context)
	{
		m_restrict_mtu.fill(65536);
	}

	utp_socket_manager::~utp_socket_manager() = default;

	void utp_socket_manager::tick(time_point now)
	{
		for (auto i = m_utp_sockets.begin()
			, end(m_utp_sockets.end()); i != end;)
		{
			if (i->second->should_delete())
			{
				if (m_last_socket == i->second.get()) m_last_socket = nullptr;
				if (m_deferred_ack == i->second.get()) m_deferred_ack = nullptr;
				i = m_utp_sockets.erase(i);
				continue;
			}
			i->second->tick(now);
			++i;
		}
	}

	std::pair<int, int> utp_socket_manager::mtu_for_dest(address const& addr)
	{
		int mtu = 0;
		if (aux::is_teredo(addr)) mtu = TORRENT_TEREDO_MTU;
		else mtu = TORRENT_ETHERNET_MTU;

#if defined __APPLE__
		// apple has a very strange loopback. It appears you can't
		// send messages of the reported MTU size, and you don't get
		// EWOULDBLOCK either.
		if (addr.is_loopback())
		{
			if (aux::is_teredo(addr)) mtu = TORRENT_TEREDO_MTU;
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
		std::uint16_t const id = ph->connection_id;

		// first test to see if it's the same socket as last time
		// in most cases it is
		if (m_last_socket && m_last_socket->match(ep, id))
		{
			return m_last_socket->incoming_packet(p, ep, receive_time);
		}

		if (m_deferred_ack)
		{
			m_deferred_ack->send_ack();
			m_deferred_ack = nullptr;
		}

		auto r = m_utp_sockets.equal_range(id);

		for (; r.first != r.second; ++r.first)
		{
			if (!r.first->second->match(ep, id)) continue;
			bool const ret = r.first->second->incoming_packet(p, ep, receive_time);
			if (ret) m_last_socket = r.first->second.get();
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

			TORRENT_ASSERT(m_new_connection == -1);
			// create the new socket with this ID
			m_new_connection = id;

//			UTP_LOGV("not found, new connection id:%d\n", m_new_connection);

			// TODO: this should not be heap allocated, sockets should be movable
			aux::socket_type c(aux::instantiate_connection(m_ios, aux::proxy_settings(), m_ssl_context, this, true, false));

			utp_stream* str = nullptr;
#ifdef TORRENT_SSL_PEERS
			if (is_ssl(c))
				str = &std::get<ssl_stream<utp_stream>>(c).next_layer();
			else
#endif
				str = std::get_if<utp_stream>(&c);

			TORRENT_ASSERT(str);
			int link_mtu, utp_mtu;
			std::tie(link_mtu, utp_mtu) = mtu_for_dest(ep.address());
			str->get_impl()->init_mtu(link_mtu, utp_mtu);
			str->get_impl()->m_sock = std::move(socket);
			bool const ret = str->get_impl()->incoming_packet(p, ep, receive_time);
			if (!ret) return false;
			m_last_socket = str->get_impl();
			m_cb(std::move(c));
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
				s->writable();
			}
		}
	}

	void utp_socket_manager::socket_drained()
	{
		if (m_deferred_ack)
		{
			utp_socket_impl* s = m_deferred_ack;
			m_deferred_ack = nullptr;
			s->send_ack();
		}

		if (!m_drained_event.empty())
		{
			m_temp_sockets.clear();
			m_drained_event.swap(m_temp_sockets);
			for (auto const &s : m_temp_sockets)
				s->socket_drained();
		}
	}

	void utp_socket_manager::defer_ack(utp_socket_impl* s)
	{
		TORRENT_ASSERT(m_deferred_ack == nullptr || m_deferred_ack == s);
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
		auto iface = sock.lock();
		for (auto& s : m_utp_sockets)
		{
			if (s.second->m_sock.lock() != iface)
				continue;

			s.second->abort();
		}
	}

	void utp_socket_manager::remove_socket(std::uint16_t const id)
	{
		auto const i = m_utp_sockets.find(id);
		if (i == m_utp_sockets.end()) return;
		if (m_last_socket == i->second.get()) m_last_socket = nullptr;
		if (m_deferred_ack == i->second.get()) m_deferred_ack = nullptr;
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
		auto impl = std::make_unique<utp_socket_impl>(recv_id, send_id, str, *this);
		auto* const ret = impl.get();
		m_utp_sockets.emplace(recv_id, std::move(impl));
		return ret;
	}
}
}
