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

#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/debug.hpp" // for single_threaded

#include <cstdlib>

namespace libtorrent {

	// internal: some MTU and protocol header sizes constants
	constexpr int TORRENT_IPV4_HEADER = 20;
	constexpr int TORRENT_IPV6_HEADER = 40;
	constexpr int TORRENT_UDP_HEADER = 8;
	constexpr int TORRENT_UTP_HEADER = 20;
	constexpr int TORRENT_SOCKS5_HEADER = 6; // plus the size of the destination address
	constexpr int TORRENT_ETHERNET_MTU = 1500;
	constexpr int TORRENT_TEREDO_MTU = 1280;
	constexpr int TORRENT_INET_MIN_MTU = 576;

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

	struct packet_deleter
	{
		// deleter for std::unique_ptr
		void operator()(packet* p) const
		{
			TORRENT_ASSERT(p != nullptr);
			p->~packet();
			std::free(p);
		}
	};

	using packet_ptr = std::unique_ptr<packet, packet_deleter>;

	// internal
	inline packet_ptr create_packet(int const size)
	{
		packet* p = static_cast<packet*>(std::malloc(sizeof(packet) + aux::numeric_cast<std::uint16_t>(size)));
		if (p == nullptr) aux::throw_ex<std::bad_alloc>();
		p = new (p) packet();
		p->allocated = aux::numeric_cast<std::uint16_t>(size);
		return packet_ptr(p);
	}

	struct TORRENT_EXTRA_EXPORT packet_slab
	{
		int const allocate_size;

		explicit packet_slab(int const alloc_size, std::size_t const limit = 10)
			: allocate_size(alloc_size)
			, m_limit(limit)
		{
			m_storage.reserve(m_limit);
		}

		packet_slab(const packet_slab&) = delete;
		packet_slab(packet_slab&&) = default;

		void try_push_back(packet_ptr &p)
		{
			if (m_storage.size() < m_limit)
				m_storage.push_back(std::move(p));
		}

		packet_ptr alloc()
		{
			if (m_storage.empty()) return create_packet(allocate_size);
			auto ret = std::move(m_storage.back());
			m_storage.pop_back();
			return ret;
		}

		void decay()
		{
			if (m_storage.empty()) return;
			m_storage.erase(m_storage.end() - 1);
		}

	private:
		const std::size_t m_limit;
		std::vector<packet_ptr> m_storage;
	};

	// single thread packet allocation packet pool
	// can handle common cases of packet size by 3 pools
	struct TORRENT_EXTRA_EXPORT packet_pool : private single_threaded
	{
		// there's a bug in GCC where allocating these in
		// member initializer expressions won't propagate exceptions.
		// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80683
		packet_pool()
			: m_syn_slab(TORRENT_UTP_HEADER)
			, m_mtu_floor_slab(mtu_floor_size)
			, m_mtu_ceiling_slab(mtu_ceiling_size)
		{}
		packet_pool(packet_pool&&) = default;

		packet_ptr acquire(int const allocate)
		{
			TORRENT_ASSERT(is_single_thread());
			TORRENT_ASSERT(allocate >= 0);
			TORRENT_ASSERT(allocate <= (std::numeric_limits<std::uint16_t>::max)());

			return alloc(allocate);
		}

		void release(packet_ptr p)
		{
			TORRENT_ASSERT(is_single_thread());

			if (!p) return;

			int const allocated = p->allocated;

			if (allocated == m_syn_slab.allocate_size) { m_syn_slab.try_push_back(p); }
			else if (allocated == m_mtu_floor_slab.allocate_size) { m_mtu_floor_slab.try_push_back(p); }
			else if (allocated == m_mtu_ceiling_slab.allocate_size) { m_mtu_ceiling_slab.try_push_back(p); }
		}

		// periodically free up some of the cached packets
		void decay()
		{
			TORRENT_ASSERT(is_single_thread());

			m_syn_slab.decay();
			m_mtu_floor_slab.decay();
			m_mtu_ceiling_slab.decay();
		}

	private:
		packet_ptr alloc(int const allocate)
		{
			if (allocate <= m_syn_slab.allocate_size) { return m_syn_slab.alloc(); }
			else if (allocate <= m_mtu_floor_slab.allocate_size) { return m_mtu_floor_slab.alloc(); }
			else if (allocate <= m_mtu_ceiling_slab.allocate_size) { return m_mtu_ceiling_slab.alloc(); }
			return create_packet(allocate);
		}
		static constexpr int mtu_floor_size = TORRENT_INET_MIN_MTU - TORRENT_IPV4_HEADER - TORRENT_UDP_HEADER;
		static constexpr int mtu_ceiling_size = TORRENT_ETHERNET_MTU - TORRENT_IPV4_HEADER - TORRENT_UDP_HEADER;
		packet_slab m_syn_slab;
		packet_slab m_mtu_floor_slab;
		packet_slab m_mtu_ceiling_slab;
	};
}

#endif // TORRENT_PACKET_POOL_HPP
