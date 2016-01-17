/*

Copyright (c) 2012-2016, Arvid Norberg
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

#ifndef TORRENT_PEER_CLASS_TYPE_FILTER_HPP_INCLUDED
#define TORRENT_PEER_CLASS_TYPE_FILTER_HPP_INCLUDED

#include <string.h> // for memset
#include <boost/cstdint.hpp>

namespace libtorrent
{

	// ``peer_class_type_filter`` is a simple container for rules for adding and subtracting
	// peer-classes from peers. It is applied *after* the peer class filter is applied (which
	// is based on the peer's IP address).
	struct peer_class_type_filter
	{
		peer_class_type_filter()
		{
			memset(m_peer_class_type_mask, 0xff, sizeof(m_peer_class_type_mask));
			memset(m_peer_class_type, 0, sizeof(m_peer_class_type));
		}

		enum socket_type_t
		{
			// these match the socket types from socket_type.hpp
			// shifted one down
			tcp_socket = 0,
			utp_socket,
			ssl_tcp_socket,
			ssl_utp_socket,
			i2p_socket,
			num_socket_types
		};


		// ``add()`` and ``remove()`` adds and removes a peer class to be added
		// to new peers based on socket type.
		void add(socket_type_t st, int peer_class)
		{
			TORRENT_ASSERT(peer_class >= 0);
			TORRENT_ASSERT(peer_class < 32);
			if (peer_class < 0 || peer_class > 31) return;

			TORRENT_ASSERT(st < num_socket_types && st >= 0);
			if (st < 0 || st >= num_socket_types) return;
			m_peer_class_type[st] |= 1 << peer_class;
		}
		void remove(socket_type_t st, int peer_class)
		{
			TORRENT_ASSERT(peer_class >= 0);
			TORRENT_ASSERT(peer_class < 32);
			if (peer_class < 0 || peer_class > 31) return;

			TORRENT_ASSERT(st < num_socket_types && st >= 0);
			if (st < 0 || st >= num_socket_types) return;
			m_peer_class_type[st] &= ~(1 << peer_class);
		}

		// ``disallow()`` and ``allow()`` adds and removes a peer class to be
		// removed from new peers based on socket type.
		// 
		// The ``peer_class`` argument cannot be greater than 31. The bitmasks representing
		// peer classes in the ``peer_class_type_filter`` are 32 bits.
		void disallow(socket_type_t st, int peer_class)
		{
			TORRENT_ASSERT(peer_class >= 0);
			TORRENT_ASSERT(peer_class < 32);
			if (peer_class < 0 || peer_class > 31) return;

			TORRENT_ASSERT(st < num_socket_types && st >= 0);
			if (st < 0 || st >= num_socket_types) return;
			m_peer_class_type_mask[st] &= ~(1 << peer_class);
		}
		void allow(socket_type_t st, int peer_class)
		{
			TORRENT_ASSERT(peer_class >= 0);
			TORRENT_ASSERT(peer_class < 32);
			if (peer_class < 0 || peer_class > 31) return;

			TORRENT_ASSERT(st < num_socket_types && st >= 0);
			if (st < 0 || st >= num_socket_types) return;
			m_peer_class_type_mask[st] |= 1 << peer_class;
		}

		// takes a bitmask of peer classes and returns a new bitmask of
		// peer classes after the rules have been applied, based on the socket type argument
		// (``st``).
		boost::uint32_t apply(int st, boost::uint32_t peer_class_mask)
		{
			TORRENT_ASSERT(st < num_socket_types && st >= 0);
			if (st < 0 || st >= num_socket_types) return peer_class_mask;

			// filter peer classes based on type
			peer_class_mask &= m_peer_class_type_mask[st];
			// add peer classes based on type
			peer_class_mask |= m_peer_class_type[st];
			return peer_class_mask;
		}

	private:
		// maps socket type to a bitmask that's used to filter out
		// (mask) bits from the m_peer_class_filter.
		boost::uint32_t m_peer_class_type_mask[5];
		// peer class bitfield added based on socket type
		boost::uint32_t m_peer_class_type[5];
	};

}

#endif

