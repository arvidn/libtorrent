/*

Copyright (c) 2010, 2012-2020, Arvid Norberg
Copyright (c) 2016-2017, Steven Siloti
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2017, Andrei Kurushin
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
#include <functional>

#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/packet_pool.hpp"

namespace libtorrent {

struct counters;

namespace aux {

	struct utp_stream;
	struct utp_socket_impl;

	// interface/handle to the underlying udp socket
	struct TORRENT_EXTRA_EXPORT utp_socket_interface
	{
		virtual udp::endpoint get_local_endpoint() = 0;
	protected:
		virtual ~utp_socket_interface() = default;
	};

	struct utp_socket_manager
	{
		using send_fun_t = std::function<void(std::weak_ptr<utp_socket_interface>
			, udp::endpoint const&
			, span<char const>
			, error_code&, udp_send_flags_t)>;

		using incoming_utp_callback_t =  std::function<void(aux::socket_type)>;

		utp_socket_manager(send_fun_t send_fun
			, incoming_utp_callback_t cb
			, io_context& ios
			, aux::session_settings const& sett
			, counters& cnt, void* ssl_context);
		~utp_socket_manager();

		// return false if this is not a uTP packet
		bool incoming_packet(std::weak_ptr<utp_socket_interface> socket
			, udp::endpoint const& ep, span<char const> p);

		// if the UDP socket failed with an EAGAIN or EWOULDBLOCK, this will be
		// called once the socket is writeable again
		void writable();

		// when the upper layer has drained the underlying UDP socket, this is
		// called, and uTP sockets will send their ACKs. This ensures ACKs at
		// least coalesce packets returned during the same wakeup
		void socket_drained();

		void tick(time_point now);

		void send_packet(std::weak_ptr<utp_socket_interface> sock, udp::endpoint const& ep
			, char const* p, int len
			, error_code& ec, udp_send_flags_t flags = {});
		void subscribe_writable(utp_socket_impl* s);

		void remove_udp_socket(std::weak_ptr<utp_socket_interface> sock);

		// internal, used by utp_stream
		void remove_socket(std::uint16_t id);

		utp_socket_impl* new_utp_socket(utp_stream* str);
		int gain_factor() const { return m_sett.get_int(settings_pack::utp_gain_factor); }
		int target_delay() const { return m_sett.get_int(settings_pack::utp_target_delay) * 1000; }
		int syn_resends() const { return m_sett.get_int(settings_pack::utp_syn_resends); }
		int fin_resends() const { return m_sett.get_int(settings_pack::utp_fin_resends); }
		int num_resends() const { return m_sett.get_int(settings_pack::utp_num_resends); }
		int connect_timeout() const { return m_sett.get_int(settings_pack::utp_connect_timeout); }
		int min_timeout() const { return m_sett.get_int(settings_pack::utp_min_timeout); }
		int loss_multiplier() const { return m_sett.get_int(settings_pack::utp_loss_multiplier); }
		int cwnd_reduce_timer() const { return m_sett.get_int(settings_pack::utp_cwnd_reduce_timer); }

		int mtu_for_dest(address const& addr) const;
		int num_sockets() const { return int(m_utp_sockets.size()); }

		void defer_ack(utp_socket_impl* s);
		void subscribe_drained(utp_socket_impl* s);

		void restrict_mtu(int const mtu)
		{
			m_restrict_mtu[std::size_t(m_mtu_idx)] = mtu;
			m_mtu_idx = (m_mtu_idx + 1) % int(m_restrict_mtu.size());
		}

		int restrict_mtu() const
		{
			return *std::max_element(m_restrict_mtu.begin(), m_restrict_mtu.end());
		}

		// used to keep stats of uTP events
		// the counter is the enum from ``counters``.
		void inc_stats_counter(int counter, int delta = 1);

		aux::packet_ptr acquire_packet(int const allocate) { return m_packet_pool.acquire(allocate); }
		void release_packet(aux::packet_ptr p) { m_packet_pool.release(std::move(p)); }
		void decay() { m_packet_pool.decay(); }

		// explicitly disallow assignment, to silence msvc warning
		utp_socket_manager& operator=(utp_socket_manager const&) = delete;

	private:

		send_fun_t m_send_fun;
		incoming_utp_callback_t m_cb;

		// replace with a hash-map
		using socket_map_t = std::multimap<std::uint16_t, std::unique_ptr<utp_socket_impl>>;
		socket_map_t m_utp_sockets;

		using socket_vector_t = std::vector<utp_socket_impl*>;

		// if this is set, it means this socket still needs to send an ACK. Once
		// we exit the loop processing packets, or switch to processing packets
		// for a different socket, issue the ACK packet and clear this.
		utp_socket_impl* m_deferred_ack = nullptr;

		// storage used for saving cpu time on "push_back"
		// by using already pre-allocated vector
		socket_vector_t m_temp_sockets;

		// sockets that have received or sent packets this
		// round, may subscribe to the event of draining the
		// UDP socket. At that point they may call the
		// user callback function to indicate bytes have been
		// sent or received.
		socket_vector_t m_drained_event;

		// list of sockets that received EWOULDBLOCK from the
		// underlying socket. They are notified when the socket
		// becomes writable again
		socket_vector_t m_stalled_sockets;

		// the last socket we received a packet on
		utp_socket_impl* m_last_socket = nullptr;

		int m_new_connection = -1;

		aux::session_settings const& m_sett;

		// stats counters
		counters& m_counters;

		io_context& m_ios;

		std::array<int, 3> m_restrict_mtu;
		int m_mtu_idx = 0;

		// this is  passed on to the instantiate connection
		// if this is non-nullptr it will create SSL connections over uTP
		void* m_ssl_context;

		aux::packet_pool m_packet_pool;
	};
}
}

#endif
