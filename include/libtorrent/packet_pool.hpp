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

#include "libtorrent/aux_/vector.hpp"

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


	struct packet_mem_manager
	{
		// deleter for std::unique_ptr
		void operator()(packet* p) const
		{
			p->~packet();
			std::free(p);
		}

		static packet *create_packet(int size)
		{
			packet *p = static_cast<packet*>(std::malloc(sizeof(packet) + size));
			new (p) packet();
			return p;
		}
	};

	using packet_ptr = std::unique_ptr<packet, packet_mem_manager>;

	template<int ALLOCATE_SIZE>
	struct TORRENT_EXTRA_EXPORT packet_slab
	{
		static const int allocate_size{ ALLOCATE_SIZE };

		explicit packet_slab(std::size_t limit) : m_limit(limit) { m_storage.reserve(m_limit); }

		packet_slab(const packet_slab&) = delete;

		void try_push_back(packet_ptr &p)
		{
			if (m_storage.size() < m_limit)
				m_storage.push_back(std::move(p));
		}

		packet_ptr alloc()
		{
			if (m_storage.empty())
				return packet_ptr(packet_mem_manager::create_packet(allocate_size));
			else
			{
				auto ret = std::move(m_storage.back());
				m_storage.pop_back();
				return ret;
			}
		}

		void flush() { m_storage.clear(); }
	private:
		const std::size_t m_limit;
		aux::vector<packet_ptr, std::size_t> m_storage;
	};

	// single thread packet allocation packet pool
	// can handle common cases of packet size by 3 pools
	struct TORRENT_EXTRA_EXPORT packet_pool : private single_threaded
	{
		packet *acquire(int allocate)
		{
			TORRENT_ASSERT(is_single_thread());
			TORRENT_ASSERT(allocate >= 0);
			TORRENT_ASSERT(allocate <= std::numeric_limits<std::uint16_t>::max());

			packet_ptr p{ alloc(allocate) };
			p->allocated = static_cast<std::uint16_t>(allocate);
			return p.release();
		}

		void release(packet *p)
		{
			TORRENT_ASSERT(is_single_thread());

			if (p == nullptr) return;

			packet_ptr pp(p); // takes ownership and may auto free if slab container does not move it
			std::size_t allocated = pp->allocated;

			if (allocated <= m_syn_slab.allocate_size) { m_syn_slab.try_push_back(pp); }
			else if (allocated <= m_mtu_floor_slab.allocate_size) { m_mtu_floor_slab.try_push_back(pp); }
			else if (allocated <= m_mtu_ceiling_slab.allocate_size) { m_mtu_ceiling_slab.try_push_back(pp); }
		}

		//TODO: call from utp manager to flush mem
		void flush()
		{
			TORRENT_ASSERT(is_single_thread());

			m_syn_slab.flush();
			m_mtu_floor_slab.flush();
			m_mtu_ceiling_slab.flush();
		}

	private:
		packet_ptr alloc(int allocate)
		{
			if (allocate <= m_syn_slab.allocate_size) { return m_syn_slab.alloc(); }
			else if (allocate <= m_mtu_floor_slab.allocate_size) { return m_mtu_floor_slab.alloc(); }
			else if (allocate <= m_mtu_ceiling_slab.allocate_size) { return m_mtu_ceiling_slab.alloc(); }
			return packet_ptr(packet_mem_manager::create_packet(allocate));
		}
		packet_slab<sizeof(utp_header)> m_syn_slab{ 100 };
		packet_slab<(TORRENT_INET_MIN_MTU - TORRENT_IPV4_HEADER - TORRENT_UDP_HEADER)> m_mtu_floor_slab{ 512 };
		packet_slab<(TORRENT_ETHERNET_MTU - TORRENT_IPV4_HEADER - TORRENT_UDP_HEADER)> m_mtu_ceiling_slab{ 256 };
	};
}

#endif // TORRENT_PACKET_POOL_HPP
