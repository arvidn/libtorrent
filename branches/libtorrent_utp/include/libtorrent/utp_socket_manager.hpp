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

#include <boost/shared_ptr.hpp>
#include <map>

namespace libtorrent
{
	struct udp_socket;
	struct utp_stream;

	typedef void (*incoming_utp_fun)(void*, boost::shared_ptr<utp_stream> const&);

	struct utp_socket_manager
	{
		utp_socket_manager(udp_socket& s, incoming_utp_fun cb, void* userdata);

		// return false if this is not a uTP packet
		bool incoming_packet(char const* p, int size);

		void tick();

		// internal, used by utp_stream
		void remove_socket(boost::uint16_t id);

	private:
		udp_socket& m_sock;
		incoming_utp_fun m_cb;
		void* m_userdata;
		
		// replace with a hash-map
		typedef std::map<boost::uint16_t, utp_stream*> socket_map_t;
		socket_map_t m_utp_sockets;

		void add_socket(boost::uint16_t id, utp_stream* s);
	};
}

#endif

