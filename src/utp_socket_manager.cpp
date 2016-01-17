/*

Copyright (c) 2009-2016, Arvid Norberg
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
#include "libtorrent/instantiate_connection.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/broadcast_socket.hpp" // for is_teredo
#include "libtorrent/random.hpp"

// #define TORRENT_DEBUG_MTU 1135

namespace libtorrent
{

	utp_socket_manager::utp_socket_manager(session_settings const& sett, udp_socket& s
		, incoming_utp_callback_t cb)
		: m_sock(s)
		, m_cb(cb)
		, m_last_socket(0)
		, m_new_connection(-1)
		, m_sett(sett)
		, m_last_route_update(min_time())
		, m_last_if_update(min_time())
		, m_sock_buf_size(0)
	{
		memset(m_counters, 0, sizeof(m_counters));
	}

	utp_socket_manager::~utp_socket_manager()
	{
		for (socket_map_t::iterator i = m_utp_sockets.begin()
			, end(m_utp_sockets.end()); i != end; ++i)
		{
			delete_utp_impl(i->second);
		}
	}

	void utp_socket_manager::get_status(utp_status& s) const
	{
		s.num_idle = 0;
		s.num_syn_sent = 0;
		s.num_connected = 0;
		s.num_fin_sent = 0;
		s.num_close_wait = 0;

		s.packet_loss = m_counters[packet_loss];
		s.timeout = m_counters[timeout];
		s.packets_in = m_counters[packets_in];
		s.packets_out = m_counters[packets_out];
		s.fast_retransmit = m_counters[fast_retransmit];
		s.packet_resend = m_counters[packet_resend];
		s.samples_above_target = m_counters[samples_above_target];
		s.samples_below_target = m_counters[samples_below_target];
		s.payload_pkts_in = m_counters[payload_pkts_in];
		s.payload_pkts_out = m_counters[payload_pkts_out];
		s.invalid_pkts_in = m_counters[invalid_pkts_in];
		s.redundant_pkts_in = m_counters[redundant_pkts_in];

		for (socket_map_t::const_iterator i = m_utp_sockets.begin()
			, end(m_utp_sockets.end()); i != end; ++i)
		{
			int state = utp_socket_state(i->second);
			switch (state)
			{
				case 0: ++s.num_idle; break;
				case 1: ++s.num_syn_sent; break;
				case 2: ++s.num_connected; break;
				case 3: ++s.num_fin_sent; break;
				case 4: ++s.num_close_wait; break;
				case 5: ++s.num_close_wait; break;
			}
		}
	}

	void utp_socket_manager::tick(ptime now)
	{
		for (socket_map_t::iterator i = m_utp_sockets.begin()
			, end(m_utp_sockets.end()); i != end;)
		{
			if (should_delete(i->second))
			{
				delete_utp_impl(i->second);
				if (m_last_socket == i->second) m_last_socket = 0;
				m_utp_sockets.erase(i++);
				continue;
			}
			tick_utp_impl(i->second, now);
			++i;
		}
	}

	void utp_socket_manager::mtu_for_dest(address const& addr, int& link_mtu, int& utp_mtu)
	{
		if (time_now() - m_last_route_update > seconds(60))
		{
			m_last_route_update = time_now();
			error_code ec;
			m_routes = enum_routes(m_sock.get_io_service(), ec);
		}

		int mtu = 0;
		if (!m_routes.empty())
		{
			for (std::vector<ip_route>::iterator i = m_routes.begin()
				, end(m_routes.end()); i != end; ++i)
			{
				if (!match_addr_mask(addr, i->destination, i->netmask)) continue;

				// assume that we'll actually use the route with the largest
				// MTU (seems like a reasonable assumption).
				// this could however be improved by using the route metrics
				// and the prefix length of the netmask to order the matches
				if (mtu < i->mtu) mtu = i->mtu;
			}
		}

		if (mtu == 0)
		{
			if (is_teredo(addr)) mtu = TORRENT_TEREDO_MTU;
			else mtu = TORRENT_ETHERNET_MTU;
		}

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

		// clamp the MTU within reasonable bounds
		if (mtu < TORRENT_INET_MIN_MTU) mtu = TORRENT_INET_MIN_MTU;
		else if (mtu > TORRENT_INET_MAX_MTU) mtu = TORRENT_INET_MAX_MTU;

		link_mtu = mtu;

		mtu -= TORRENT_UDP_HEADER;

		if (m_sock.get_proxy_settings().type == proxy_settings::socks5
			|| m_sock.get_proxy_settings().type == proxy_settings::socks5_pw)
		{
			// this is for the IP layer
			address proxy_addr = m_sock.proxy_addr().address();
			if (proxy_addr.is_v4()) mtu -= TORRENT_IPV4_HEADER;
			else mtu -= TORRENT_IPV6_HEADER;

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

		utp_mtu = mtu;
	}

	void utp_socket_manager::send_packet(udp::endpoint const& ep, char const* p
		, int len, error_code& ec, int flags)
	{
		if (!m_sock.is_open())
		{
			ec = asio::error::operation_aborted;
			return;
		}

#ifdef TORRENT_DEBUG_MTU
		// drop packets that exceed the debug MTU
		if ((flags & dont_fragment) && len > TORRENT_DEBUG_MTU) return;
#endif

#ifdef TORRENT_HAS_DONT_FRAGMENT
		error_code tmp;
		if (flags & utp_socket_manager::dont_fragment)
			m_sock.set_option(libtorrent::dont_fragment(true), tmp);
#endif
		m_sock.send(ep, p, len, ec);
#ifdef TORRENT_HAS_DONT_FRAGMENT
		if (flags & utp_socket_manager::dont_fragment)
			m_sock.set_option(libtorrent::dont_fragment(false), tmp);
#endif
	}

	int utp_socket_manager::local_port(error_code& ec) const
	{
		return m_sock.local_endpoint(ec).port();
	}

	tcp::endpoint utp_socket_manager::local_endpoint(address const& remote, error_code& ec) const
	{
		tcp::endpoint socket_ep = m_sock.local_endpoint(ec);

		// first enumerate the routes in the routing table
		if (time_now() - m_last_route_update > seconds(60))
		{
			m_last_route_update = time_now();
			error_code ec;
			m_routes = enum_routes(m_sock.get_io_service(), ec);
			if (ec) return socket_ep;
		}

		if (m_routes.empty()) return socket_ep;
		// then find the best match
		ip_route* best = &m_routes[0];
		for (std::vector<ip_route>::iterator i = m_routes.begin()
			, end(m_routes.end()); i != end; ++i)
		{
			if (is_any(i->destination) && i->destination.is_v4() == remote.is_v4())
			{
				best = &*i;
				continue;
			}

			if (match_addr_mask(remote, i->destination, i->netmask))
			{
				best = &*i;
				continue;
			}
		}

		// best now tells us which interface we would send over
		// for this target. Now figure out what the local address
		// is for that interface

		if (time_now() - m_last_if_update > seconds(60))
		{
			m_last_if_update = time_now();
			error_code ec;
			m_interfaces = enum_net_interfaces(m_sock.get_io_service(), ec);
			if (ec) return socket_ep;
		}

		for (std::vector<ip_interface>::iterator i = m_interfaces.begin()
			, end(m_interfaces.end()); i != end; ++i)
		{
			if (i->interface_address.is_v4() != remote.is_v4())
				continue;

			if (strcmp(best->name, i->name) == 0)
				return tcp::endpoint(i->interface_address, socket_ep.port());
		}
		return socket_ep;
	}

	bool utp_socket_manager::incoming_packet(error_code const& ec, udp::endpoint const& ep
			, char const* p, int size)
	{
//		UTP_LOGV("incoming packet size:%d\n", size);

		if (size < int(sizeof(utp_header))) return false;

		utp_header const* ph = (utp_header*)p;

//		UTP_LOGV("incoming packet version:%d\n", int(ph->get_version()));

		if (ph->get_version() != 1) return false;

		const ptime receive_time = time_now_hires();
		
		// parse out connection ID and look for existing
		// connections. If found, forward to the utp_stream.
		boost::uint16_t id = ph->connection_id;

		// first test to see if it's the same socket as last time
		// in most cases it is
		if (m_last_socket
			&& utp_match(m_last_socket, ep, id))
		{
			return utp_incoming_packet(m_last_socket, p, size, ep, receive_time);
		}

		std::pair<socket_map_t::iterator, socket_map_t::iterator> r =
			m_utp_sockets.equal_range(id);

		for (; r.first != r.second; ++r.first)
		{
			if (!utp_match(r.first->second, ep, id)) continue;
			bool ret = utp_incoming_packet(r.first->second, p, size, ep, receive_time);
			if (ret) m_last_socket = r.first->second;
			return ret;
		}

//		UTP_LOGV("incoming packet id:%d source:%s\n", id, print_endpoint(ep).c_str());

		if (!m_sett.enable_incoming_utp)
			return false;

		// if not found, see if it's a SYN packet, if it is,
		// create a new utp_stream
		if (ph->get_type() == ST_SYN)
		{
			// possible SYN flood. Just ignore
			if (int(m_utp_sockets.size()) > m_sett.connections_limit * 2)
				return false;

//			UTP_LOGV("not found, new connection id:%d\n", m_new_connection);

			boost::shared_ptr<socket_type> c(new (std::nothrow) socket_type(m_sock.get_io_service()));
			if (!c) return false;

			TORRENT_ASSERT(m_new_connection == -1);
			// create the new socket with this ID
			m_new_connection = id;

			instantiate_connection(m_sock.get_io_service(), proxy_settings(), *c, 0, this);
			utp_stream* str = c->get<utp_stream>();
			TORRENT_ASSERT(str);
			int link_mtu, utp_mtu;
			mtu_for_dest(ep.address(), link_mtu, utp_mtu);
			utp_init_mtu(str->get_impl(), link_mtu, utp_mtu);
			bool ret = utp_incoming_packet(str->get_impl(), p, size, ep, receive_time);
			if (!ret) return false;
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
		std::vector<utp_socket_impl*> stalled_sockets;
		m_stalled_sockets.swap(stalled_sockets);
		for (std::vector<utp_socket_impl*>::iterator i = stalled_sockets.begin()
			, end(stalled_sockets.end()); i != end; ++i)
		{
			utp_socket_impl* s = *i;
			utp_writable(s);
		}
	}

	void utp_socket_manager::socket_drained()
	{
		// flush all deferred acks
		
		std::vector<utp_socket_impl*> deferred_acks;
		m_deferred_acks.swap(deferred_acks);
		for (std::vector<utp_socket_impl*>::iterator i = deferred_acks.begin()
			, end(deferred_acks.end()); i != end; ++i)
		{
			utp_socket_impl* s = *i;
			utp_send_ack(s);
		}

		std::vector<utp_socket_impl*> drained_event;
		m_drained_event.swap(drained_event);
		for (std::vector<utp_socket_impl*>::iterator i = drained_event.begin()
			, end(drained_event.end()); i != end; ++i)
		{
			utp_socket_impl* s = *i;
			utp_socket_drained(s);
		}
	}

	void utp_socket_manager::defer_ack(utp_socket_impl* s)
	{
		TORRENT_ASSERT(std::find(m_deferred_acks.begin(), m_deferred_acks.end(), s)
			== m_deferred_acks.end());
		m_deferred_acks.push_back(s);
	}

	void utp_socket_manager::subscribe_drained(utp_socket_impl* s)
	{
		TORRENT_ASSERT(std::find(m_drained_event.begin(), m_drained_event.end(), s)
			== m_drained_event.end());
		m_drained_event.push_back(s);
	}

	void utp_socket_manager::remove_socket(boost::uint16_t id)
	{
		socket_map_t::iterator i = m_utp_sockets.find(id);
		if (i == m_utp_sockets.end()) return;
		delete_utp_impl(i->second);
		if (m_last_socket == i->second) m_last_socket = 0;
		m_utp_sockets.erase(i);
	}
	
	void utp_socket_manager::set_sock_buf(int size)
	{
		if (size < m_sock_buf_size) return;
		m_sock.set_buf_size(size);
		error_code ec;
		// add more socket buffer storage on the lower level socket
		// to avoid dropping packets because of a full receive buffer
		// while processing a packet

		// only update the buffer size if it's bigger than
		// what we already have
		datagram_socket::receive_buffer_size recv_buf_size_opt;
		m_sock.get_option(recv_buf_size_opt, ec);
		if (recv_buf_size_opt.value() < size * 10)
		{
			m_sock.set_option(datagram_socket::receive_buffer_size(size * 10), ec);
			m_sock.set_option(datagram_socket::send_buffer_size(size * 3), ec);
		}
		m_sock_buf_size = size;
	}

	void utp_socket_manager::inc_stats_counter(int counter)
	{
		TORRENT_ASSERT(counter >= 0);
		TORRENT_ASSERT(counter < num_counters);
		++m_counters[counter];
	}

	utp_socket_impl* utp_socket_manager::new_utp_socket(utp_stream* str)
	{
		boost::uint16_t send_id = 0;
		boost::uint16_t recv_id = 0;
		if (m_new_connection != -1)
		{
			send_id = m_new_connection;
			recv_id = m_new_connection + 1;
			m_new_connection = -1;
		}
		else
		{
			send_id = random() & 0xffff;
			recv_id = send_id - 1;
		}
		utp_socket_impl* impl = construct_utp_impl(recv_id, send_id, str, this);
		m_utp_sockets.insert(std::make_pair(recv_id, impl));
		return impl;
	}
}

