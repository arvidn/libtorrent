/*

Copyright (c) 2013-2020, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
Copyright (c) 2016, Steven Siloti
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
#include "libtorrent/socket_io.hpp" // for hash_address
#include "libtorrent/random.hpp" // for random()
#include "libtorrent/aux_/time.hpp" // for aux::time_now()
#include "libtorrent/aux_/ip_helpers.hpp" // for is_local() etc.

namespace libtorrent {

	ip_voter::ip_voter()
		: m_total_votes(0)
		, m_valid_external(false)
		, m_last_rotate(aux::time_now())
	{
	}

	// returns true if our external IP changed
	bool ip_voter::maybe_rotate()
	{
		time_point now = aux::time_now();

		// if we have more than or equal to 50 votes,
		// we rotate. Also, if it's been more than 5 minutes
		// and we have at least one vote, we also rotate.
		// this is the inverse condition, since this is the case
		// were we exit, without rotating
		if (m_total_votes < 50
			&& (now - m_last_rotate < minutes(5) || m_total_votes == 0)
			&& m_valid_external)
			return false;

		// this shouldn't really happen if we have at least one
		// vote.
		if (m_external_addresses.empty()) return false;

		// if there's just one vote, go with that
		if (m_external_addresses.size() == 1)
		{
			// avoid flapping. We need more votes to change our mind on the
			// external IP
			if (m_external_addresses[0].num_votes < 2) return false;
		}
		else
		{
			// find the top two votes.
			std::partial_sort(m_external_addresses.begin()
				, m_external_addresses.begin() + 2, m_external_addresses.end());

			// if we don't have enough of a majority voting for the winning
			// IP, don't rotate. This avoids flapping
			if (m_external_addresses[0].num_votes * 2 / 3 <= m_external_addresses[1].num_votes)
				return false;
		}

		auto const i = m_external_addresses.begin();

		bool ret = m_external_address != i->addr;
		m_external_address = i->addr;

		m_external_address_voters.clear();
		m_total_votes = 0;
		m_external_addresses.clear();
		m_last_rotate = now;
		m_valid_external = true;
		return ret;
	}

	bool ip_voter::cast_vote(address const& ip
		, aux::ip_source_t const source_type, address const& source)
	{
		if (ip.is_unspecified()) return false;
		if (aux::is_local(ip)) return false;
		if (ip.is_loopback()) return false;

		// don't trust source that aren't connected to us
		// on a different address family than the external
		// IP they claim we have
		if (ip.is_v4() != source.is_v4()) return false;

		// this is the key to use for the bloom filters
		// it represents the identity of the voter
		sha1_hash const k = hash_address(source);

		// do we already have an entry for this external IP?
		auto i = std::find_if(m_external_addresses.begin()
			, m_external_addresses.end(), [&ip] (external_ip_t const& e) { return e.addr == ip; });

		if (i == m_external_addresses.end())
		{
			// each IP only gets to add a new IP once
			if (m_external_address_voters.find(k)) return maybe_rotate();

			if (m_external_addresses.size() > 40)
			{
				if (random(1)) return maybe_rotate();

				// use stable sort here to maintain the fifo-order
				// of the entries with the same number of votes
				// this will sort in ascending order, i.e. the lowest
				// votes first. Also, the oldest are first, so this
				// is a sort of weighted LRU.
				std::stable_sort(m_external_addresses.begin(), m_external_addresses.end());

				// erase the last element, since it is one of the
				// ones with the fewest votes
				m_external_addresses.erase(m_external_addresses.end() - 1);
			}
			m_external_addresses.emplace_back();
			i = m_external_addresses.end() - 1;
			i->addr = ip;
		}
		// add one more vote to this external IP
		if (!i->add_vote(k, source_type)) return maybe_rotate();
		++m_total_votes;

		if (m_valid_external) return maybe_rotate();

		i = std::min_element(m_external_addresses.begin(), m_external_addresses.end());
		TORRENT_ASSERT(i != m_external_addresses.end());

		if (i->addr == m_external_address) return maybe_rotate();

		if (m_external_address != address())
		{
			// we have a temporary external address. As soon as we have
			// more than 25 votes, consider deciding which one to settle for
			return (m_total_votes >= 25) ? maybe_rotate() : false;
		}

		m_external_address = i->addr;

		return true;
	}

	bool ip_voter::external_ip_t::add_vote(sha1_hash const& k
		, aux::ip_source_t const type)
	{
		sources |= type;
		if (voters.find(k)) return false;
		voters.set(k);
		++num_votes;
		return true;
	}

	external_ip::external_ip(address const& local4, address const& global4
		, address const& local6, address const& global6)
		: m_addresses{{global4, aux::ensure_v6(global6)}, {local4, aux::ensure_v6(local6)}}
	{
		TORRENT_ASSERT(m_addresses[0][1].is_v6());
		TORRENT_ASSERT(m_addresses[1][1].is_v6());
		TORRENT_ASSERT(m_addresses[0][0].is_v4());
		TORRENT_ASSERT(m_addresses[1][0].is_v4());
	}

	address external_ip::external_address(address const& ip) const
	{
		address ext = m_addresses[aux::is_local(ip)][ip.is_v6()];
		if (ip.is_v6() && ext == address_v4()) return address_v6();
		return ext;
	}
}
