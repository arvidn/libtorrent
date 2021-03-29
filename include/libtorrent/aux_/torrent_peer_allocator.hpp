/*

Copyright (c) 2010, 2013-2017, 2019-2020, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PEER_ALLOCATOR_HPP_INCLUDED
#define TORRENT_PEER_ALLOCATOR_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/torrent_peer.hpp"
#include "libtorrent/aux_/pool.hpp"

namespace lt::aux {

	struct TORRENT_EXTRA_EXPORT torrent_peer_allocator_interface
	{
		enum peer_type_t
		{
			ipv4_peer_type,
			ipv6_peer_type,
			i2p_peer_type,
			rtc_peer_type
		};

		virtual torrent_peer* allocate_peer_entry(int type) = 0;
		virtual void free_peer_entry(torrent_peer* p) = 0;
	protected:
		~torrent_peer_allocator_interface() {}
	};

	struct TORRENT_EXTRA_EXPORT torrent_peer_allocator final
		: torrent_peer_allocator_interface
	{
#if TORRENT_USE_ASSERTS
		~torrent_peer_allocator() {
			m_in_use = false;
		}
#endif

		torrent_peer* allocate_peer_entry(int type) override;
		void free_peer_entry(torrent_peer* p) override;

		std::uint64_t total_bytes() const { return m_total_bytes; }
		std::uint64_t total_allocations() const { return m_total_allocations; }
		int live_bytes() const { return m_live_bytes; }
		int live_allocations() const { return m_live_allocations; }

	private:

		// this is a shared pool where torrent_peer objects
		// are allocated. It's a pool since we're likely
		// to have tens of thousands of peers, and a pool
		// saves significant overhead

		aux::pool m_ipv4_peer_pool{sizeof(ipv4_peer), 500};
		aux::pool m_ipv6_peer_pool{sizeof(ipv6_peer), 500};
#if TORRENT_USE_I2P
		aux::pool m_i2p_peer_pool{sizeof(i2p_peer), 500};
#endif
#if TORRENT_USE_RTC
		aux::pool m_rtc_peer_pool{sizeof(rtc_peer), 500};
#endif
		// the total number of bytes allocated (cumulative)
		std::uint64_t m_total_bytes = 0;
		// the total number of allocations (cumulative)
		std::uint64_t m_total_allocations = 0;
		// the number of currently live bytes
		int m_live_bytes = 0;
		// the number of currently live allocations
		int m_live_allocations = 0;
#if TORRENT_USE_ASSERTS
		bool m_in_use = true;
#endif
	};
}

#endif
