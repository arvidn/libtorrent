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
#include "libtorrent/instantiate_connection.hpp"
#include "libtorrent/socket_io.hpp"

#define TORRENT_UTP_LOG 1

#if TORRENT_UTP_LOG
namespace libtorrent {
extern void utp_log(char const* fmt, ...);
}

#define UTP_LOG utp_log
#define UTP_LOGV utp_log

#else

#define UTP_LOG if (false) printf
#define UTP_LOGV if (false) printf

#endif

namespace libtorrent
{

	utp_socket_manager::utp_socket_manager(udp_socket& s, incoming_utp_callback_t cb)
		: m_sock(s)
		, m_cb(cb)
		, m_new_connection(-1)
	{}

	utp_socket_manager::~utp_socket_manager()
	{
		for (socket_map_t::iterator i = m_utp_sockets.begin()
			, end(m_utp_sockets.end()); i != end; ++i)
		{
			delete_utp_impl( i->second);
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
				m_utp_sockets.erase(i++);
				continue;
			}
			tick_utp_impl(i->second, now);
			++i;
		}
	}

	void utp_socket_manager::send_packet(udp::endpoint const& ep, char const* p
		, int len, error_code& ec)
	{
		if (!m_sock.is_open())
		{
			ec = asio::error::operation_aborted;
			return;
		}
		m_sock.send(ep, p, len, ec);
	}

	tcp::endpoint utp_socket_manager::local_endpoint() const
	{
		return m_sock.local_endpoint();
	}

	bool utp_socket_manager::incoming_packet(char const* p, int size, udp::endpoint const& ep)
	{
//		UTP_LOGV("incoming packet size:%d\n", size);

		if (size < sizeof(utp_header)) return false;

		utp_header const* ph = (utp_header*)p;

//		UTP_LOGV("incoming packet version:%d\n", int(ph->ver));

		if (ph->ver != 1) return false;
		
		// #error cache the last socket used to test against before doing the lookup

		// parse out connection ID and look for existing
		// connections. If found, forward to the utp_stream.
		boost::uint16_t id = ph->connection_id;
		socket_map_t::iterator i = m_utp_sockets.find(id);

//		UTP_LOGV("incoming packet id:%d source:%s\n", id, print_endpoint(ep).c_str());

		// if not found, see if it's a SYN packet, if it is,
		// create a new utp_stream
		if (i == m_utp_sockets.end() && ph->type == ST_SYN)
		{
			// create the new socket with this ID
			m_new_connection = id;

//			UTP_LOGV("not found, new connection id:%d\n", m_new_connection);

			boost::shared_ptr<socket_type> c(new (std::nothrow) socket_type(m_sock.get_io_service()));
			if (!c) return false;
			instantiate_connection(m_sock.get_io_service(), proxy_settings(), this, *c);
			utp_stream* str = c->get<utp_stream>();
			TORRENT_ASSERT(str);
			bool ret = utp_incoming_packet(str->get_impl(), p, size, ep);
			if (!ret) return false;
			m_cb(c);
			// the connection most likely changed its connection ID here
			// we need to move it to the correct ID
			return true;
		}

		// only accept a packet if it's from the right source
		if (i != m_utp_sockets.end() && ep == utp_remote_endpoint(i->second))
		{
//			UTP_LOGV("found connection!\n");
			return utp_incoming_packet(i->second, p, size, ep);
		}
		else
		{
//			UTP_LOGV("ignoring packet\n");
		}

		// #error send reset

		return false;
	}

	void utp_socket_manager::remove_socket(boost::uint16_t id)
	{
		socket_map_t::iterator i = m_utp_sockets.find(id);
		if (i == m_utp_sockets.end()) return;
		delete_utp_impl(i->second);
		m_utp_sockets.erase(i);
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
			send_id = rand();
			recv_id = send_id - 1;
		}
		utp_socket_impl* impl = construct_utp_impl(recv_id, send_id, str, this);
		TORRENT_ASSERT(m_utp_sockets.find(recv_id) == m_utp_sockets.end());
		m_utp_sockets.insert(std::make_pair(recv_id, impl));
		return impl;
	}
}

