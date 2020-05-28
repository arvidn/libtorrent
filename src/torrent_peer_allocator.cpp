/*

Copyright (c) 2014-2017, 2019, Arvid Norberg
Copyright (c) 2016, Alden Torres
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


#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/torrent_peer_allocator.hpp"

namespace libtorrent {

	torrent_peer* torrent_peer_allocator::allocate_peer_entry(int type)
	{
		TORRENT_ASSERT(m_in_use);
		torrent_peer* p = nullptr;
		switch(type)
		{
			case torrent_peer_allocator_interface::ipv4_peer_type:
				p = static_cast<torrent_peer*>(m_ipv4_peer_pool.malloc());
				if (p == nullptr) return nullptr;
				m_ipv4_peer_pool.set_next_size(500);
				m_total_bytes += sizeof(libtorrent::ipv4_peer);
				m_live_bytes += sizeof(libtorrent::ipv4_peer);
				++m_live_allocations;
				++m_total_allocations;
				break;
			case torrent_peer_allocator_interface::ipv6_peer_type:
				p = static_cast<torrent_peer*>(m_ipv6_peer_pool.malloc());
				if (p == nullptr) return nullptr;
				m_ipv6_peer_pool.set_next_size(500);
				m_total_bytes += sizeof(libtorrent::ipv6_peer);
				m_live_bytes += sizeof(libtorrent::ipv6_peer);
				++m_live_allocations;
				++m_total_allocations;
				break;
#if TORRENT_USE_I2P
			case torrent_peer_allocator_interface::i2p_peer_type:
				p = static_cast<torrent_peer*>(m_i2p_peer_pool.malloc());
				if (p == nullptr) return nullptr;
				m_i2p_peer_pool.set_next_size(500);
				m_total_bytes += sizeof(libtorrent::i2p_peer);
				m_live_bytes += sizeof(libtorrent::i2p_peer);
				++m_live_allocations;
				++m_total_allocations;
				break;
#endif
		}
		return p;
	}

	void torrent_peer_allocator::free_peer_entry(torrent_peer* p)
	{
		TORRENT_ASSERT(m_in_use);
		TORRENT_ASSERT(p->in_use);
		if (p->is_v6_addr)
		{
			TORRENT_ASSERT(m_ipv6_peer_pool.is_from(static_cast<libtorrent::ipv6_peer*>(p)));
			static_cast<libtorrent::ipv6_peer*>(p)->~ipv6_peer();
			m_ipv6_peer_pool.free(p);
			TORRENT_ASSERT(m_live_bytes >= int(sizeof(ipv6_peer)));
			m_live_bytes -= int(sizeof(ipv6_peer));
			TORRENT_ASSERT(m_live_allocations > 0);
			--m_live_allocations;
			return;
		}
#if TORRENT_USE_I2P
		if (p->is_i2p_addr)
		{
			TORRENT_ASSERT(m_i2p_peer_pool.is_from(static_cast<libtorrent::i2p_peer*>(p)));
			static_cast<libtorrent::i2p_peer*>(p)->~i2p_peer();
			m_i2p_peer_pool.free(p);
			TORRENT_ASSERT(m_live_bytes >= int(sizeof(i2p_peer)));
			m_live_bytes -= int(sizeof(i2p_peer));
			TORRENT_ASSERT(m_live_allocations > 0);
			--m_live_allocations;
			return;
		}
#endif
		TORRENT_ASSERT(m_ipv4_peer_pool.is_from(static_cast<libtorrent::ipv4_peer*>(p)));
		static_cast<libtorrent::ipv4_peer*>(p)->~ipv4_peer();
		m_ipv4_peer_pool.free(p);
		TORRENT_ASSERT(m_live_bytes >= int(sizeof(ipv4_peer)));
		m_live_bytes -= int(sizeof(ipv4_peer));
		TORRENT_ASSERT(m_live_allocations > 0);
		--m_live_allocations;
	}

}
