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

namespace libtorrent
{

	utp_socket_manager::utp_socket_manager(udp_socket& s, incoming_utp_fun cb
		, void* userdata)
		: m_sock(s)
		, m_cb(cb)
		, m_userdata(userdata)
	{}

	void utp_socket_manager::tick()
	{
		for (socket_map::iterator i = m_utp_sockets.begin()
			, end(m_utp_sockets.end()); i != end; ++i)
		{
			i->second->tick();
		}
	}

	bool utp_socket_manager::incoming_packet(char const* p, int size)
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
			boost::uint16_t id = rand();
			boost::shared_ptr<utp_stream> c = new utp_stream(*this, id);

			TORRENT_ASSERT(m_utp_sockets.find(id) == m_utp_sockets.end());
			i = m_utp_sockets.insert(std::make_pair(id, c.get()));
			m_cb(m_userdata, c);
		}

		if (i != m_utp_sockets.end())
			return i->second->incoming_packet(p, size);

		return false;
	}

	void utp_socket_manager::remove_socket(boost::uint16_t id)
	{
		socket_map_t::iterator i = m_utp_sockets.find(id);
		if (i == m_utp_sockets.end()) return;
		m_utp_sockets.erase(i);
	}

	void utp_socket_manager::add_socket(boost::uint16_t id, utp_stream* s)
	{
	}
}

