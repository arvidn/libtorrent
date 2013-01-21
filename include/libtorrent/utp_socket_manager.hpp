/*

Copyright (c) 2009, Arvid Norberg
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

#ifndef TORRENT_UTP_SOCKET_MANAGER_HPP_INCLUDED
#define TORRENT_UTP_SOCKET_MANAGER_HPP_INCLUDED

#include <map>

#include "libtorrent/socket_type.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/enum_net.hpp"

namespace libtorrent
{
	class udp_socket;
	class utp_stream;
	struct utp_socket_impl;

	typedef boost::function<void(boost::shared_ptr<socket_type> const&)> incoming_utp_callback_t;

	struct utp_socket_manager
	{
		utp_socket_manager(session_settings const& sett, udp_socket& s, incoming_utp_callback_t cb);
		~utp_socket_manager();

		void get_status(utp_status& s) const;

		// return false if this is not a uTP packet
		bool incoming_packet(char const* p, int size, udp::endpoint const& ep);

		void tick(ptime now);

		tcp::endpoint local_endpoint(address const& remote, error_code& ec) const;
		int local_port(error_code& ec) const;

		// flags for send_packet
		enum { dont_fragment = 1 };
		void send_packet(udp::endpoint const& ep, char const* p, int len
			, error_code& ec, int flags = 0);

		// internal, used by utp_stream
		void remove_socket(boost::uint16_t id);

		utp_socket_impl* new_utp_socket(utp_stream* str);
		int gain_factor() const { return m_sett.utp_gain_factor; }
		int target_delay() const { return m_sett.utp_target_delay * 1000; }
		int syn_resends() const { return m_sett.utp_syn_resends; }
		int fin_resends() const { return m_sett.utp_fin_resends; }
		int num_resends() const { return m_sett.utp_num_resends; }
		int connect_timeout() const { return m_sett.utp_connect_timeout; }
		int delayed_ack() const { return m_sett.utp_delayed_ack; }
		int min_timeout() const { return m_sett.utp_min_timeout; }
		int loss_multiplier() const { return m_sett.utp_loss_multiplier; }
		bool allow_dynamic_sock_buf() const { return m_sett.utp_dynamic_sock_buf; }

		void mtu_for_dest(address const& addr, int& link_mtu, int& utp_mtu);
		void set_sock_buf(int size);
		int num_sockets() const { return m_utp_sockets.size(); }

	private:
		udp_socket& m_sock;
		incoming_utp_callback_t m_cb;

		// replace with a hash-map
		typedef std::multimap<boost::uint16_t, utp_socket_impl*> socket_map_t;
		socket_map_t m_utp_sockets;

		// the last socket we received a packet on
		utp_socket_impl* m_last_socket;

		int m_new_connection;

		session_settings const& m_sett;

		// this is a copy of the routing table, used
		// to initialize MTU sizes of uTP sockets
		mutable std::vector<ip_route> m_routes;

		// the timestamp for the last time we updated
		// the routing table
		mutable ptime m_last_route_update;

		// cache of interfaces
		mutable std::vector<ip_interface> m_interfaces;
		mutable ptime m_last_if_update;

		// the buffer size of the socket. This is used
		// to now lower the buffer size
		int m_sock_buf_size;
	};
}

#endif

