/*

Copyright (c) 2014-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/


#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/torrent_peer_allocator.hpp"

namespace lt::aux {

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
				m_total_bytes += sizeof(ipv4_peer);
				m_live_bytes += sizeof(ipv4_peer);
				++m_live_allocations;
				++m_total_allocations;
				break;
			case torrent_peer_allocator_interface::ipv6_peer_type:
				p = static_cast<torrent_peer*>(m_ipv6_peer_pool.malloc());
				if (p == nullptr) return nullptr;
				m_ipv6_peer_pool.set_next_size(500);
				m_total_bytes += sizeof(ipv6_peer);
				m_live_bytes += sizeof(ipv6_peer);
				++m_live_allocations;
				++m_total_allocations;
				break;
#if TORRENT_USE_I2P
			case torrent_peer_allocator_interface::i2p_peer_type:
				p = static_cast<torrent_peer*>(m_i2p_peer_pool.malloc());
				if (p == nullptr) return nullptr;
				m_i2p_peer_pool.set_next_size(500);
				m_total_bytes += sizeof(i2p_peer);
				m_live_bytes += sizeof(i2p_peer);
				++m_live_allocations;
				++m_total_allocations;
				break;
#endif
#if TORRENT_USE_RTC
            case torrent_peer_allocator_interface::rtc_peer_type:
                p = static_cast<torrent_peer*>(m_rtc_peer_pool.malloc());
                if (p == nullptr) return nullptr;
                m_rtc_peer_pool.set_next_size(500);
                m_total_bytes += sizeof(rtc_peer);
                m_live_bytes += sizeof(rtc_peer);
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
			TORRENT_ASSERT(m_ipv6_peer_pool.is_from(static_cast<ipv6_peer*>(p)));
			static_cast<ipv6_peer*>(p)->~ipv6_peer();
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
			TORRENT_ASSERT(m_i2p_peer_pool.is_from(static_cast<i2p_peer*>(p)));
			static_cast<i2p_peer*>(p)->~i2p_peer();
			m_i2p_peer_pool.free(p);
			TORRENT_ASSERT(m_live_bytes >= int(sizeof(i2p_peer)));
			m_live_bytes -= int(sizeof(i2p_peer));
			TORRENT_ASSERT(m_live_allocations > 0);
			--m_live_allocations;
			return;
		}
#endif
#if TORRENT_USE_RTC
        if (p->is_rtc_addr)
        {
            TORRENT_ASSERT(m_rtc_peer_pool.is_from(static_cast<rtc_peer*>(p)));
            static_cast<rtc_peer*>(p)->~rtc_peer();
            m_rtc_peer_pool.free(p);
            TORRENT_ASSERT(m_live_bytes >= int(sizeof(rtc_peer)));
            m_live_bytes -= int(sizeof(rtc_peer));
            TORRENT_ASSERT(m_live_allocations > 0);
            --m_live_allocations;
            return;
        }
#endif
		TORRENT_ASSERT(m_ipv4_peer_pool.is_from(static_cast<ipv4_peer*>(p)));
		static_cast<ipv4_peer*>(p)->~ipv4_peer();
		m_ipv4_peer_pool.free(p);
		TORRENT_ASSERT(m_live_bytes >= int(sizeof(ipv4_peer)));
		m_live_bytes -= int(sizeof(ipv4_peer));
		TORRENT_ASSERT(m_live_allocations > 0);
		--m_live_allocations;
	}

}
