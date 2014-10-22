/*

Copyright (c) 2014, Arvid Norberg
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

#include "libtorrent/choker.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/aux_/session_settings.hpp"

#include <boost/bind.hpp>

namespace libtorrent
{
	int unchoke_sort(std::vector<peer_connection*>& peers
		, int max_upload_rate
		, time_duration unchoke_interval
		, aux::session_settings const& sett)
	{
		int upload_slots = sett.get_int(settings_pack::unchoke_slots_limit);

		// ==== BitTyrant ====
		//
		// if we're using the bittyrant unchoker, go through all peers that
		// we have unchoked already, and adjust our estimated reciprocation
		// rate. If the peer has reciprocated, lower the estimate, if it hasn't,
		// increase the estimate (this attempts to optimize "ROI" of upload
		// capacity, by sending just enough to be reciprocated).
		// For more information, see: http://bittyrant.cs.washington.edu/
		if (sett.get_int(settings_pack::choking_algorithm)
			== settings_pack::bittyrant_choker)
		{
			for (std::vector<peer_connection*>::const_iterator i = peers.begin()
				, end(peers.end()); i != end; ++i)
			{
				peer_connection* p = *i;
				if (p->is_choked() || !p->is_interesting()) continue;

				if (!p->has_peer_choked())
				{
					// we're unchoked, we may want to lower our estimated
					// reciprocation rate
					p->decrease_est_reciprocation_rate();
				}
				else
				{
					// we've unchoked this peer, and it hasn't reciprocated
					// we may want to increase our estimated reciprocation rate
					p->increase_est_reciprocation_rate();
				}
			}

			// if we're using the bittyrant choker, sort peers by their return
			// on investment. i.e. download rate / upload rate

			// TODO: make the comparison function a free function and move it
			// into this source file
			std::sort(peers.begin(), peers.end()
				, boost::bind(&peer_connection::bittyrant_unchoke_compare, _1, _2));

			int upload_capacity_left = max_upload_rate;

			// now, figure out how many peers should be unchoked. We deduct the
			// estimated reciprocation rate from our upload_capacity estimate
			// until there none left
			upload_slots = 0;

			for (std::vector<peer_connection*>::iterator i = peers.begin()
				, end(peers.end()); i != end; ++i)
			{
				peer_connection* p = *i;
				TORRENT_ASSERT(p);

				if (p->est_reciprocation_rate() > upload_capacity_left) break;

				++upload_slots;
				upload_capacity_left -= p->est_reciprocation_rate();
			}

			return upload_slots;
		}

		// ==== rate-based ====
		//
		// The rate based unchoker looks at our upload rate to peers, and find
		// a balance between number of upload slots and the rate we achieve. The
		// intention is to not spread upload bandwidth too thin, but also to not
		// unchoke few enough peers to not be able to saturate the up-link.
		// this is done by traversing the peers sorted by our upload rate to
		// them in decreasing rates. For each peer we increase our threshold
		// by 1 kB/s. The first peer we get to to whom we upload slower than
		// the threshold, we stop and that's the number of unchoke slots we have.
		if (sett.get_int(settings_pack::choking_algorithm)
			== settings_pack::rate_based_choker)
		{
			// first reset the number of unchoke slots, because we'll calculate
			// it purely based on the current state of our peers.
			upload_slots = 0;

			// TODO: optimize this using partial_sort or something. We don't need
			// to sort the entire list
			
			// TODO: make the comparison function a free function and move it
			// into this cpp file
			std::sort(peers.begin(), peers.end()
				, boost::bind(&peer_connection::upload_rate_compare, _1, _2));

			// TODO: make configurable
			int rate_threshold = 1024;

			for (std::vector<peer_connection*>::const_iterator i = peers.begin()
				, end(peers.end()); i != end; ++i)
			{
				peer_connection const& p = **i;
				int rate = int(p.uploaded_in_last_round()
					* 1000 / total_milliseconds(unchoke_interval));

				if (rate < rate_threshold) break;

				++upload_slots;

				// TODO: make configurable
				rate_threshold += 1024;
			}
			++upload_slots;
		}

		// sorts the peers that are eligible for unchoke by download rate and secondary
		// by total upload. The reason for this is, if all torrents are being seeded,
		// the download rate will be 0, and the peers we have sent the least to should
		// be unchoked
		
		// TODO: use partial_sort

		// TODO: make the comparison function a free function and move it into
		// this cpp file
		std::sort(peers.begin(), peers.end()
			, boost::bind(&peer_connection::unchoke_compare, _1, _2));

		return upload_slots;
	}

}

