/*

Copyright (c) 2003-2013, Arvid Norberg
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

namespace libtorrent
{

	torrent_peer_allocator::torrent_peer_allocator()
		: m_ipv4_peer_pool(500)
#if TORRENT_USE_IPV6
		  , m_ipv6_peer_pool(500)
#endif
	{

	}

	torrent_peer* torrent_peer_allocator::allocate_peer_entry(int type)
	{
		torrent_peer* p = NULL;
		switch(type)
		{
			case torrent_peer_allocator_interface::ipv4_peer:
				p = (torrent_peer*)m_ipv4_peer_pool.malloc();
				m_ipv4_peer_pool.set_next_size(500);
				break;
#if TORRENT_USE_IPV6
			case torrent_peer_allocator_interface::ipv6_peer:
				p = (torrent_peer*)m_ipv6_peer_pool.malloc();
				m_ipv6_peer_pool.set_next_size(500);
				break;
#endif
#if TORRENT_USE_I2P
			case torrent_peer_allocator_interface::i2p_peer:
				p = (torrent_peer*)m_i2p_peer_pool.malloc();
				m_i2p_peer_pool.set_next_size(500);
				break;
#endif
		}
		return p;
	}

	void torrent_peer_allocator::free_peer_entry(torrent_peer* p)
	{
#if TORRENT_USE_IPV6
		if (p->is_v6_addr)
		{
			TORRENT_ASSERT(m_ipv6_peer_pool.is_from((libtorrent::ipv6_peer*)p));
			m_ipv6_peer_pool.destroy((libtorrent::ipv6_peer*)p);
			return;
		}
#endif
#if TORRENT_USE_I2P
		if (p->is_i2p_addr)
		{
			TORRENT_ASSERT(m_i2p_peer_pool.is_from((libtorrent::i2p_peer*)p));
			m_i2p_peer_pool.destroy((libtorrent::i2p_peer*)p);
			return;
		}
#endif
		TORRENT_ASSERT(m_ipv4_peer_pool.is_from((libtorrent::ipv4_peer*)p));
		m_ipv4_peer_pool.destroy((libtorrent::ipv4_peer*)p);
	}

}

