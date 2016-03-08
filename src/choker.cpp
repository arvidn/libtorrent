/*

Copyright (c) 2014-2016, Arvid Norberg
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
#include "libtorrent/torrent.hpp"

#include <boost/bind.hpp>

namespace libtorrent
{

	namespace {

	// return true if 'lhs' peer should be preferred to be unchoke over 'rhs'
	bool unchoke_compare_rr(peer_connection const* lhs
		, peer_connection const* rhs, int pieces)
	{
		// if one peer belongs to a higher priority torrent than the other one
		// that one should be unchoked.
		boost::shared_ptr<torrent> t1 = lhs->associated_torrent().lock();
		TORRENT_ASSERT(t1);
		boost::shared_ptr<torrent> t2 = rhs->associated_torrent().lock();
		TORRENT_ASSERT(t2);

		int prio1 = lhs->get_priority(peer_connection::upload_channel);
		int prio2 = rhs->get_priority(peer_connection::upload_channel);

		if (prio1 != prio2)
			return prio1 > prio2;

		// compare how many bytes they've sent us
		boost::int64_t c1;
		boost::int64_t c2;
		c1 = lhs->downloaded_in_last_round();
		c2 = rhs->downloaded_in_last_round();

		if (c1 != c2) return c1 > c2;

		// when seeding, rotate which peer is unchoked in a round-robin fasion

		// the amount uploaded since unchoked (not just in the last round)
		c1 = lhs->uploaded_since_unchoked();
		c2 = rhs->uploaded_since_unchoked();

		// the way the round-robin unchoker works is that it,
		// by default, prioritizes any peer that is already unchoked.
		// this maintain the status quo across unchoke rounds. However,
		// peers that are unchoked, but have sent more than one quota
		// since they were unchoked, they get de-prioritized.

		// if a peer is already unchoked, and the number of bytes sent since it was unchoked
		// is greater than the send quanta, then it's done with it' upload slot, and we
		// can de-prioritize it
		bool c1_quota_complete = !lhs->is_choked() && c1
			> (std::max)(t1->torrent_file().piece_length() * pieces, 256 * 1024);
		bool c2_quota_complete = !rhs->is_choked() && c2
			> (std::max)(t2->torrent_file().piece_length() * pieces, 256 * 1024);

		// if c2 has completed a quanta, it shuold be de-prioritized
		// and vice versa
		if (c1_quota_complete < c2_quota_complete) return true;
		if (c1_quota_complete > c2_quota_complete) return false;

		// if both peers have either completed a quanta, or not.
		// keep unchoked peers prioritized over choked ones, to let
		// peers keep working on uploading a full quanta
		if (lhs->is_choked() < rhs->is_choked()) return true;
		if (lhs->is_choked() > rhs->is_choked()) return false;

		// if the peers are still identical (say, they're both waiting to be unchoked)
		// prioritize the one that has waited the longest to be unchoked
		// the round-robin unchoker relies on this logic. Don't change it
		// without moving this into that unchoker logic
		return lhs->time_of_last_unchoke() < rhs->time_of_last_unchoke();
	}

	// return true if 'lhs' peer should be preferred to be unchoke over 'rhs'
	bool unchoke_compare_fastest_upload(peer_connection const* lhs
		, peer_connection const* rhs)
	{
		// if one peer belongs to a higher priority torrent than the other one
		// that one should be unchoked.
		boost::shared_ptr<torrent> t1 = lhs->associated_torrent().lock();
		TORRENT_ASSERT(t1);
		boost::shared_ptr<torrent> t2 = rhs->associated_torrent().lock();
		TORRENT_ASSERT(t2);

		int prio1 = lhs->get_priority(peer_connection::upload_channel);
		int prio2 = rhs->get_priority(peer_connection::upload_channel);

		if (prio1 != prio2)
			return prio1 > prio2;

		// compare how many bytes they've sent us
		boost::int64_t c1;
		boost::int64_t c2;
		c1 = lhs->downloaded_in_last_round();
		c2 = rhs->downloaded_in_last_round();

		if (c1 != c2) return c1 > c2;

		// when seeding, prefer the peer we're uploading the fastest to
		c1 = lhs->uploaded_in_last_round();
		c2 = rhs->uploaded_in_last_round();

		// take torrent priority into account
		c1 *= prio1;
		c2 *= prio2;

		if (c1 > c2) return true;
		if (c2 > c1) return false;

		// prioritize the one that has waited the longest to be unchoked
		// the round-robin unchoker relies on this logic. Don't change it
		// without moving this into that unchoker logic
		return lhs->time_of_last_unchoke() < rhs->time_of_last_unchoke();
	}

	// return true if 'lhs' peer should be preferred to be unchoke over 'rhs'
	bool unchoke_compare_anti_leech(peer_connection const* lhs
		, peer_connection const* rhs)
	{
		// if one peer belongs to a higher priority torrent than the other one
		// that one should be unchoked.
		boost::shared_ptr<torrent> t1 = lhs->associated_torrent().lock();
		TORRENT_ASSERT(t1);
		boost::shared_ptr<torrent> t2 = rhs->associated_torrent().lock();
		TORRENT_ASSERT(t2);

		int prio1 = lhs->get_priority(peer_connection::upload_channel);
		int prio2 = rhs->get_priority(peer_connection::upload_channel);

		if (prio1 != prio2)
			return prio1 > prio2;

		// compare how many bytes they've sent us
		boost::int64_t c1;
		boost::int64_t c2;
		c1 = lhs->downloaded_in_last_round();
		c2 = rhs->downloaded_in_last_round();

		if (c1 != c2) return c1 > c2;

		// the anti-leech seeding algorithm is based on the paper "Improving
		// BitTorrent: A Simple Approach" from Chow et. al. and ranks peers based
		// on how many pieces they have, prefering to unchoke peers that just
		// started and peers that are close to completing. Like this:
		//   ^
		//   | \                       / |
		//   |  \                     /  |
		//   |   \                   /   |
		// s |    \                 /    |
		// c |     \               /     |
		// o |      \             /      |
		// r |       \           /       |
		// e |        \         /        |
		//   |         \       /         |
		//   |          \     /          |
		//   |           \   /           |
		//   |            \ /            |
		//   |             V             |
		//   +---------------------------+
		//   0%    num have pieces     100%
		int t1_total = t1->torrent_file().num_pieces();
		int t2_total = t2->torrent_file().num_pieces();
		int score1 = (lhs->num_have_pieces() < t1_total / 2
			? t1_total - lhs->num_have_pieces() : lhs->num_have_pieces()) * 1000 / t1_total;
		int score2 = (rhs->num_have_pieces() < t2_total / 2
			? t2_total - rhs->num_have_pieces() : rhs->num_have_pieces()) * 1000 / t2_total;
		if (score1 > score2) return true;
		if (score2 > score1) return false;

		// prioritize the one that has waited the longest to be unchoked
		// the round-robin unchoker relies on this logic. Don't change it
		// without moving this into that unchoker logic
		return lhs->time_of_last_unchoke() < rhs->time_of_last_unchoke();
	}

	bool upload_rate_compare(peer_connection const* lhs
		, peer_connection const* rhs)
	{
		boost::int64_t c1;
		boost::int64_t c2;

		c1 = lhs->uploaded_in_last_round();
		c2 = rhs->uploaded_in_last_round();

		// take torrent priority into account
		c1 *= lhs->get_priority(peer_connection::upload_channel);
		c2 *= rhs->get_priority(peer_connection::upload_channel);

		return c1 > c2;
	}

	bool bittyrant_unchoke_compare(peer_connection const* lhs
		, peer_connection const* rhs)
	{
		boost::int64_t d1, d2, u1, u2;

		// first compare how many bytes they've sent us
		d1 = lhs->downloaded_in_last_round();
		d2 = rhs->downloaded_in_last_round();
		// divided by the number of bytes we've sent them
		u1 = lhs->uploaded_in_last_round();
		u2 = rhs->uploaded_in_last_round();

		// take torrent priority into account
		d1 *= lhs->get_priority(peer_connection::upload_channel);
		d2 *= rhs->get_priority(peer_connection::upload_channel);

		d1 = d1 * 1000 / (std::max)(boost::int64_t(1), u1);
		d2 = d2 * 1000 / (std::max)(boost::int64_t(1), u2);
		if (d1 > d2) return true;
		if (d1 < d2) return false;

		// if both peers are still in their send quota or not in their send quota
		// prioritize the one that has waited the longest to be unchoked
		return lhs->time_of_last_unchoke() < rhs->time_of_last_unchoke();
	}

	} // anonymous namespace

	int unchoke_sort(std::vector<peer_connection*>& peers
		, int max_upload_rate
		, time_duration unchoke_interval
		, aux::session_settings const& sett)
	{
#if TORRENT_USE_ASSERTS
		for (std::vector<peer_connection*>::iterator i = peers.begin()
			, end(peers.end()); i != end; ++i)
		{
			TORRENT_ASSERT((*i)->self());
			TORRENT_ASSERT((*i)->associated_torrent().lock());
		}
#endif

		int upload_slots = sett.get_int(settings_pack::unchoke_slots_limit);
		if (upload_slots < 0)
			upload_slots = (std::numeric_limits<int>::max)();

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
			std::sort(peers.begin(), peers.end()
				, boost::bind(&bittyrant_unchoke_compare, _1, _2));

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
				, boost::bind(&upload_rate_compare, _1, _2));

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

		// sorts the peers that are eligible for unchoke by download rate and
		// secondary by total upload. The reason for this is, if all torrents are
		// being seeded, the download rate will be 0, and the peers we have sent
		// the least to should be unchoked

		// we use partial sort here, because we only care about the top
		// upload_slots peers.

		if (sett.get_int(settings_pack::seed_choking_algorithm)
			== settings_pack::round_robin)
		{
			int pieces = sett.get_int(settings_pack::seeding_piece_quota);

			std::partial_sort(peers.begin(), peers.begin()
				+ (std::min)(upload_slots, int(peers.size())), peers.end()
				, boost::bind(&unchoke_compare_rr, _1, _2, pieces));
		}
		else if (sett.get_int(settings_pack::seed_choking_algorithm)
			== settings_pack::fastest_upload)
		{
			std::partial_sort(peers.begin(), peers.begin()
				+ (std::min)(upload_slots, int(peers.size())), peers.end()
				, boost::bind(&unchoke_compare_fastest_upload, _1, _2));
		}
		else if (sett.get_int(settings_pack::seed_choking_algorithm)
			== settings_pack::anti_leech)
		{
			std::partial_sort(peers.begin(), peers.begin()
				+ (std::min)(upload_slots, int(peers.size())), peers.end()
				, boost::bind(&unchoke_compare_anti_leech, _1, _2));
		}
		else
		{
			TORRENT_ASSERT(false);

			int pieces = sett.get_int(settings_pack::seeding_piece_quota);
			std::partial_sort(peers.begin(), peers.begin()
				+ (std::min)(upload_slots, int(peers.size())), peers.end()
				, boost::bind(&unchoke_compare_rr, _1, _2, pieces));
		}

		return upload_slots;
	}

}

