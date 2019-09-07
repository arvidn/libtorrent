/*

Copyright (c) 2012-2018, Arvid Norberg
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

#include <cstdint>
#include <array>

#include "aux_/export.hpp"
#include "peer_class.hpp" // for peer_class_t

namespace libtorrent {

	// ``peer_class_type_filter`` is a simple container for rules for adding and subtracting
	// peer-classes from peers. It is applied *after* the peer class filter is applied (which
	// is based on the peer's IP address).
	struct TORRENT_EXPORT peer_class_type_filter
	{
		// hidden
		peer_class_type_filter()
		{
			m_peer_class_type_mask.fill(0xffffffff);
			m_peer_class_type.fill(0);
		}

		enum socket_type_t : std::uint8_t
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
		void add(socket_type_t const st, peer_class_t const peer_class)
		{
			TORRENT_ASSERT(peer_class < peer_class_t{32});
			if (peer_class > peer_class_t{31}) return;

			TORRENT_ASSERT(st < num_socket_types);
			if (st >= num_socket_types) return;
			m_peer_class_type[st] |= 1 << static_cast<std::uint32_t>(peer_class);
		}
		void remove(socket_type_t const st, peer_class_t const peer_class)
		{
			TORRENT_ASSERT(peer_class < peer_class_t{32});
			if (peer_class > peer_class_t{31}) return;

			TORRENT_ASSERT(st < num_socket_types);
			if (st >= num_socket_types) return;
			m_peer_class_type[st] &= ~(1 << static_cast<std::uint32_t>(peer_class));
		}

		// ``disallow()`` and ``allow()`` adds and removes a peer class to be
		// removed from new peers based on socket type.
		//
		// The ``peer_class`` argument cannot be greater than 31. The bitmasks representing
		// peer classes in the ``peer_class_type_filter`` are 32 bits.
		void disallow(socket_type_t const st, peer_class_t const peer_class)
		{
			TORRENT_ASSERT(peer_class < peer_class_t{32});
			if (peer_class > peer_class_t{31}) return;

			TORRENT_ASSERT(st < num_socket_types);
			if (st >= num_socket_types) return;
			m_peer_class_type_mask[st] &= ~(1 << static_cast<std::uint32_t>(peer_class));
		}
		void allow(socket_type_t const st, peer_class_t const peer_class)
		{
			TORRENT_ASSERT(peer_class < peer_class_t{32});
			if (peer_class > peer_class_t{31}) return;

			TORRENT_ASSERT(st < num_socket_types);
			if (st >= num_socket_types) return;
			m_peer_class_type_mask[st] |= 1 << static_cast<std::uint32_t>(peer_class);
		}

		// takes a bitmask of peer classes and returns a new bitmask of
		// peer classes after the rules have been applied, based on the socket type argument
		// (``st``).
		std::uint32_t apply(socket_type_t const st, std::uint32_t peer_class_mask)
		{
			TORRENT_ASSERT(st < num_socket_types);
			if (st >= num_socket_types) return peer_class_mask;

			// filter peer classes based on type
			peer_class_mask &= m_peer_class_type_mask[st];
			// add peer classes based on type
			peer_class_mask |= m_peer_class_type[st];
			return peer_class_mask;
		}

		friend bool operator==(peer_class_type_filter const& lhs
			, peer_class_type_filter const& rhs)
		{
			return lhs.m_peer_class_type_mask == rhs.m_peer_class_type_mask
				&& lhs.m_peer_class_type == rhs.m_peer_class_type;
		}

	private:
		// maps socket type to a bitmask that's used to filter out
		// (mask) bits from the m_peer_class_filter.
		std::array<std::uint32_t, num_socket_types> m_peer_class_type_mask;
		// peer class bitfield added based on socket type
		std::array<std::uint32_t, num_socket_types> m_peer_class_type;
	};

}

#endif
