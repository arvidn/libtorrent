/*

Copyright (c) 2003-2018, Arvid Norberg
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

#ifndef TORRENT_PEER_ALLOCATOR_HPP_INCLUDED
#define TORRENT_PEER_ALLOCATOR_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/torrent_peer.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/pool/pool.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

	struct TORRENT_EXTRA_EXPORT torrent_peer_allocator_interface
	{
		enum peer_type_t
		{
			ipv4_peer_type,
			ipv6_peer_type,
			i2p_peer_type
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

		boost::pool<> m_ipv4_peer_pool{sizeof(libtorrent::ipv4_peer), 500};
		boost::pool<> m_ipv6_peer_pool{sizeof(libtorrent::ipv6_peer), 500};
#if TORRENT_USE_I2P
		boost::pool<> m_i2p_peer_pool{sizeof(libtorrent::i2p_peer), 500};
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

