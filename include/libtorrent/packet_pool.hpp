/*

Copyright (c) 2005-2017, Arvid Norberg
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

#ifndef TORRENT_PACKET_POOL_HPP
#define TORRENT_PACKET_POOL_HPP

#include "libtorrent/config.hpp"

#include <boost/pool/pool.hpp>

namespace libtorrent
{

	// used for out-of-order incoming packets
	// as well as sent packets that are waiting to be ACKed
	struct packet
	{
		// the last time this packet was sent
		time_point send_time;

		// the number of bytes actually allocated in 'buf'
		std::uint16_t allocated;

		// the size of the buffer 'buf' points to
		std::uint16_t size;

		// this is the offset to the payload inside the buffer
		// this is also used as a cursor to describe where the
		// next payload that hasn't been consumed yet starts
		std::uint16_t header_size;

		// the number of times this packet has been sent
		std::uint8_t num_transmissions:6;

		// true if we need to send this packet again. All
		// outstanding packets are marked as needing to be
		// resent on timeouts
		bool need_resend:1;

		// this is set to true for packets that were
		// sent with the DF bit set (Don't Fragment)
		bool mtu_probe:1;

	#if TORRENT_USE_ASSERTS
		int num_fast_resend;
	#endif

		// the actual packet buffer
		std::uint8_t buf[1];
	};

	// single thread packet allocation packet pool
	// can handle common cases of packet size by 3 pools
	struct TORRENT_EXTRA_EXPORT packet_pool : single_threaded
	{

		packet *acquire(std::size_t allocate)
		{
			TORRENT_ASSERT(is_single_thread());
			TORRENT_ASSERT(allocate <= std::numeric_limits<std::uint16_t>::max());

			packet *p;

			// every if block contains less or equal *_size check.
			// "packet::allocate" field indicates only requested "packet::buf" size.
			// (which may be less than claimed memory of corresponding slab)
			if (allocate <= utp_header_size)
			{
				p = static_cast<packet*>(m_syn_slabs.malloc());
			}
			else if (allocate <= mtu_floor_size)
			{
				p = static_cast<packet*>(m_mtu_floor_slabs.malloc());
			}
			else if (allocate <= mtu_ceiling_size)
			{
				p = static_cast<packet*>(m_mtu_ceiling_slabs.malloc());
			}
			else
			{
				// fallback for huge packets, no need to use fixed size pool, just ordinary system malloc
				p = static_cast<packet*>(malloc(sizeof(packet) + allocate));
			}

			p->allocated = static_cast<std::uint16_t>(allocate);
			return p;
		}

		void release(packet *p)
		{
			TORRENT_ASSERT(is_single_thread());

			if (p == nullptr) return;
			if (p->allocated <= utp_header_size)
			{
				m_syn_slabs.free(p);
			}
			else if (p->allocated <= mtu_floor_size)
			{
				m_mtu_floor_slabs.free(p);
			}
			else if (p->allocated <= mtu_ceiling_size)
			{
				m_mtu_ceiling_slabs.free(p);
			}
			else
			{
				// release huge blocks
				free(p);
			}
		}
	private:

		static const std::uint16_t utp_header_size = sizeof(utp_header);

		static const std::uint16_t mtu_floor_size = TORRENT_INET_MIN_MTU - TORRENT_IPV4_HEADER - TORRENT_UDP_HEADER;

		static const std::uint16_t mtu_ceiling_size = TORRENT_ETHERNET_MTU - TORRENT_IPV4_HEADER - TORRENT_UDP_HEADER;

		using malloc_pool = boost::pool<boost::default_user_allocator_malloc_free>;

		malloc_pool m_syn_slabs{ sizeof(packet) + utp_header_size };
		malloc_pool m_mtu_floor_slabs{ sizeof(packet) + mtu_floor_size };
		malloc_pool m_mtu_ceiling_slabs{ sizeof(packet) + mtu_ceiling_size };
	};

}

#endif // TORRENT_PACKET_POOL_HPP
