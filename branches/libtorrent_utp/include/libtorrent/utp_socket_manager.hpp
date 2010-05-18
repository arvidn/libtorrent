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

namespace libtorrent
{
	class udp_socket;
	class utp_stream;
	struct utp_socket_impl;

	typedef boost::function<void(boost::shared_ptr<socket_type> const&)> incoming_utp_callback_t;

	struct utp_socket_manager
	{
		utp_socket_manager(udp_socket& s, incoming_utp_callback_t cb);
		~utp_socket_manager();

		// return false if this is not a uTP packet
		bool incoming_packet(char const* p, int size, udp::endpoint const& ep);

		void tick(ptime now);

		tcp::endpoint local_endpoint() const;

		void send_packet(udp::endpoint const& ep, char const* p, int len
			, error_code& ec);

		// internal, used by utp_stream
		void remove_socket(boost::uint16_t id);

		utp_socket_impl* new_utp_socket(utp_stream* str);
		int gain_factor() const { return m_gain; }
		int target_delay() const { return m_target_delay; }

		void set_gain_factor(int gain)
		{
			TORRENT_ASSERT(gain > 0);
			m_gain = gain;
		}

		void set_target_delay(int target)
		{
			TORRENT_ASSERT(target >= 10);
			m_target_delay = target * 1000;
		}

	private:
		udp_socket& m_sock;
		incoming_utp_callback_t m_cb;

		// replace with a hash-map
		typedef std::multimap<boost::uint16_t, utp_socket_impl*> socket_map_t;
		socket_map_t m_utp_sockets;

		// the last socket we received a packet on
		utp_socket_impl* m_last_socket;

		int m_new_connection;

		// max increase of cwnd per RTT
		int m_gain;

		// target delay in microseconds
		int m_target_delay;
	};
}

#endif

