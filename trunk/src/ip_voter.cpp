/*

Copyright (c) 2013, Arvid Norberg
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

#include "libtorrent/ip_voter.hpp"
#include "libtorrent/broadcast_socket.hpp" // for is_any() etc.
#include "libtorrent/socket_io.hpp" // for hash_address
#include "libtorrent/random.hpp" // for random()

#include <boost/bind.hpp>

namespace libtorrent
{
	bool ip_voter::cast_vote(address const& ip
		, int source_type, address const& source)
	{
		if (is_any(ip)) return false;
		if (is_local(ip)) return false;
		if (is_loopback(ip)) return false;

		// don't trust source that aren't connected to us
		// on a different address family than the external
		// IP they claim we have
		if (ip.is_v4() != source.is_v4()) return false;

		// this is the key to use for the bloom filters
		// it represents the identity of the voter
		sha1_hash k;
		hash_address(source, k);

		// do we already have an entry for this external IP?
		std::vector<external_ip_t>::iterator i = std::find_if(m_external_addresses.begin()
			, m_external_addresses.end(), boost::bind(&external_ip_t::addr, _1) == ip);

		if (i == m_external_addresses.end())
		{
			// each IP only gets to add a new IP once
			if (m_external_address_voters.find(k)) return false;
		
			if (m_external_addresses.size() > 40)
			{
				if (random() % 100 < 50)
					return false;

				// use stable sort here to maintain the fifo-order
				// of the entries with the same number of votes
				// this will sort in ascending order, i.e. the lowest
				// votes first. Also, the oldest are first, so this
				// is a sort of weighted LRU.
				std::stable_sort(m_external_addresses.begin(), m_external_addresses.end());

				// erase the first element, since this is the
				// oldest entry and the one with lowst number
				// of votes. This makes sense because the oldest
				// entry has had the longest time to receive more
				// votes to be bumped up
				m_external_addresses.erase(m_external_addresses.begin());
			}
			m_external_addresses.push_back(external_ip_t());
			i = m_external_addresses.end() - 1;
			i->addr = ip;
		}
		// add one more vote to this external IP
		if (!i->add_vote(k, source_type)) return false;
		
		i = std::max_element(m_external_addresses.begin(), m_external_addresses.end());
		TORRENT_ASSERT(i != m_external_addresses.end());

		if (i->addr == m_external_address) return false;

		m_external_address = i->addr;
		m_external_address_voters.clear();

		return true;
	}

	bool ip_voter::external_ip_t::add_vote(sha1_hash const& k, int type)
	{
		sources |= type;
		if (voters.find(k)) return false;
		voters.set(k);
		++num_votes;
		return true;
	}

	bool external_ip::cast_vote(address const& ip, int source_type, address const& source)
	{
		return m_vote_group[ip.is_v6()].cast_vote(ip, source_type, source);
	}

	address external_ip::external_address(address const& ip) const
	{
		return m_vote_group[ip.is_v6()].external_address();
	}
}

