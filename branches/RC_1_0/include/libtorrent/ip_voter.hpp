/*

Copyright (c) 2013-2014, Arvid Norberg
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

#ifndef TORRENT_IP_VOTER_HPP_INCLUDED
#define TORRENT_IP_VOTER_HPP_INCLUDED

#include <vector>
#include "libtorrent/address.hpp"
#include "libtorrent/bloom_filter.hpp"
#include "libtorrent/time.hpp" // for ptime

namespace libtorrent
{
	// this is an object that keeps the state for a single external IP
	// based on peoples votes
	struct TORRENT_EXTRA_EXPORT ip_voter
	{
		ip_voter();

		// returns true if a different IP is the top vote now
		// i.e. we changed our idea of what our external IP is
		bool cast_vote(address const& ip, int source_type, address const& sorce);

		address external_address() const { return m_external_address; }

	private:

		bool maybe_rotate();

		struct external_ip_t
		{
			external_ip_t(): sources(0), num_votes(0) {}

			bool add_vote(sha1_hash const& k, int type);

			// we want to sort decending
			bool operator<(external_ip_t const& rhs) const
			{
				if (num_votes > rhs.num_votes) return true;
				if (num_votes < rhs.num_votes) return false;
				return sources > rhs.sources;
			}

			// this is a bloom filter of the IPs that have
			// reported this address
			bloom_filter<16> voters;
			// this is the actual external address
			address addr;
			// a bitmask of sources the reporters have come from
			boost::uint16_t sources;
			// the total number of votes for this IP
			boost::uint16_t num_votes;
		};

		// this is a bloom filter of all the IPs that have
		// been the first to report an external address. Each
		// IP only gets to add a new item once.
		bloom_filter<32> m_external_address_voters;

		std::vector<external_ip_t> m_external_addresses;
		address m_external_address;

		// the total number of unique IPs that have voted
		int m_total_votes;

		// this is true from the first time we rotate. Before
		// we rotate for the first time, we keep updating the
		// external address as we go, since we don't have any
		// stable setting to fall back on. Once this is true,
		// we stop updating it on the fly, and just use the
		// address from when we rotated.
		bool m_valid_external;

		// the last time we rotated this ip_voter. i.e. threw
		// away all the votes and started from scratch, in case
		// our IP has changed
		ptime m_last_rotate;
	};

	// this keeps track of multiple external IPs (for now, just IPv6 and IPv4, but
	// it could be extended to deal with loopback and local network addresses as well)
	struct TORRENT_EXTRA_EXPORT external_ip
	{
		// returns true if a different IP is the top vote now
		// i.e. we changed our idea of what our external IP is
		bool cast_vote(address const& ip, int source_type, address const& source);

		// the external IP as it would be observed from `ip`
		address external_address(address const& ip) const;

	private:

		// for now, assume one external IPv4 and one external IPv6 address
		// 0 = IPv4 1 = IPv6
		// TODO: 1 instead, have one instance per possible subnet, global IPv4, global IPv6, loopback, 192.168.x.x, 10.x.x.x, etc.
		ip_voter m_vote_group[2];
	};

}

#endif

