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

#include "libtorrent/utp_stream.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/utp_socket_manager.hpp"

namespace libtorrent
{

	utp_socket_manager::utp_socket_manager(udp_socket& s, incoming_utp_callback_t cb)
		: m_sock(s)
		, m_cb(cb)
	{}

	utp_socket_manager::~utp_socket_manager()
	{
		for (socket_map_t::iterator i = m_utp_sockets.begin()
			, end(m_utp_sockets.end()); i != end; ++i)
		{
			delete_utp_impl( i->second);
		}
	}

	void utp_socket_manager::tick()
	{
		ptime now = time_now_hires();
		for (socket_map_t::iterator i = m_utp_sockets.begin()
			, end(m_utp_sockets.end()); i != end;)
		{
			if (should_delete(i->second))
			{
				delete_utp_impl(i->second);
				m_utp_sockets.erase(i++);
				continue;
			}
			tick_utp_impl(i->second, now);
			++i;
		}
	}

	void utp_socket_manager::send_packet(udp::endpoint const& ep, char const* p, int len)
	{
		error_code ec;
		m_sock.send(ep, p, len, ec);
	}

	tcp::endpoint utp_socket_manager::local_endpoint() const
	{
		return m_sock.local_endpoint();
	}

	bool utp_socket_manager::incoming_packet(char const* p, int size, udp::endpoint const& ep)
	{
		if (size < sizeof(utp_header)) return false;

		utp_header const* ph = (utp_header*)p;

		if (ph->ver != 1) return false;

		// parse out connection ID and look for existing
		// connections. If found, forward to the utp_stream.
		boost::uint16_t id = ph->connection_id;
		socket_map_t::iterator i = m_utp_sockets.find(id);

		// if not found, see if it's a SYN packet, if it is,
		// create a new utp_stream
		if (i == m_utp_sockets.end() && ph->type == ST_SYN)
		{
			// #error we need an empty utp_stream here to fill in
			utp_stream* str = 0;

			boost::uint16_t id = rand();
			utp_socket_impl* c = construct_utp_impl(str, id);
			if (c == 0) return false;

			TORRENT_ASSERT(m_utp_sockets.find(id) == m_utp_sockets.end());
			i = m_utp_sockets.insert(i, std::make_pair(id, c));
//			m_cb(c);
		}

		// only accept a packet if it's from the right source
		if (i != m_utp_sockets.end() && ep == utp_remote_endpoint(i->second))
			return utp_incoming_packet(i->second, p, size);

		return false;
	}

	void utp_socket_manager::remove_socket(boost::uint16_t id)
	{
		socket_map_t::iterator i = m_utp_sockets.find(id);
		if (i == m_utp_sockets.end()) return;
		delete_utp_impl(i->second);
		m_utp_sockets.erase(i);
	}
}

