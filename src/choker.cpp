/*

Copyright (c) 2019, Amir Abrams
Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
Copyright (c) 2016, Steven Siloti
Copyright (c) 2019, Monson Shao
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
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/torrent.hpp"

#include <functional>

using namespace std::placeholders;

namespace libtorrent {

namespace {

	int compare_peers(peer_connection const* lhs, peer_connection const* rhs)
	{
		int const prio1 = lhs->get_priority(peer_connection::upload_channel);
		int const prio2 = rhs->get_priority(peer_connection::upload_channel);

		if (prio1 != prio2) return prio1 > prio2 ? 1 : -1;

		// compare how many bytes they've sent us
		std::int64_t const c1 = lhs->downloaded_in_last_round();
		std::int64_t const c2 = rhs->downloaded_in_last_round();

		if (c1 != c2) return c1 > c2 ? 1 : -1;
		return 0;
	}

	// return true if 'lhs' peer should be preferred to be unchoke over 'rhs'
	bool unchoke_compare_rr(peer_connection const* lhs
		, peer_connection const* rhs, int pieces)
	{
		int const cmp = compare_peers(lhs, rhs);
		if (cmp != 0) return cmp > 0;

		// when seeding, rotate which peer is unchoked in a round-robin fasion

		// the amount uploaded since unchoked (not just in the last round)
		std::int64_t const u1 = lhs->uploaded_since_unchoked();
		std::int64_t const u2 = rhs->uploaded_since_unchoked();

		// the way the round-robin unchoker works is that it,
		// by default, prioritizes any peer that is already unchoked.
		// this maintain the status quo across unchoke rounds. However,
		// peers that are unchoked, but have sent more than one quota
		// since they were unchoked, they get de-prioritized.

		std::shared_ptr<torrent> const t1 = lhs->associated_torrent().lock();
		std::shared_ptr<torrent> const t2 = rhs->associated_torrent().lock();
		TORRENT_ASSERT(t1);
		TORRENT_ASSERT(t2);

		// if a peer is already unchoked, the number of bytes sent since it was unchoked
		// is greater than the send quanta, and it has been unchoked for at least one minute
		// then it's done with its upload slot, and we can de-prioritize it
		bool const c1_quota_complete = !lhs->is_choked()
			&& u1 > std::int64_t(t1->torrent_file().piece_length()) * pieces
			&& aux::time_now() - lhs->time_of_last_unchoke() > minutes(1);
		bool const c2_quota_complete = !rhs->is_choked()
			&& u2 > std::int64_t(t2->torrent_file().piece_length()) * pieces
			&& aux::time_now() - rhs->time_of_last_unchoke() > minutes(1);

		// if c2 has completed a quanta, it should be de-prioritized
		// and vice versa
		if (c1_quota_complete != c2_quota_complete)
			return int(c1_quota_complete) < int(c2_quota_complete);

		// when seeding, prefer the peer we're uploading the fastest to

		// force the upload rate to zero for choked peers because
		// if the peers just got choked the previous round
		// there may have been a residual transfer which was already
		// in-flight at the time and we don't want that to cause the peer
		// to be ranked at the top of the choked peers
		std::int64_t const c1 = lhs->is_choked() ? 0 : lhs->uploaded_in_last_round();
		std::int64_t const c2 = rhs->is_choked() ? 0 : rhs->uploaded_in_last_round();

		if (c1 != c2) return c1 > c2;

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
		int const cmp = compare_peers(lhs, rhs);
		if (cmp != 0) return cmp > 0;

		// when seeding, prefer the peer we're uploading the fastest to
		std::int64_t const c1 = lhs->uploaded_in_last_round();
		std::int64_t const c2 = rhs->uploaded_in_last_round();

		if (c1 != c2) return c1 > c2;

		// prioritize the one that has waited the longest to be unchoked
		// the round-robin unchoker relies on this logic. Don't change it
		// without moving this into that unchoker logic
		return lhs->time_of_last_unchoke() < rhs->time_of_last_unchoke();
	}

	int anti_leech_score(peer_connection const* peer)
	{
		// the anti-leech seeding algorithm is based on the paper "Improving
		// BitTorrent: A Simple Approach" from Chow et. al. and ranks peers based
		// on how many pieces they have, preferring to unchoke peers that just
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
		std::shared_ptr<torrent> const t = peer->associated_torrent().lock();
		TORRENT_ASSERT(t);

		std::int64_t const total_size = t->torrent_file().total_size();
		if (total_size == 0) return 0;
		std::int64_t const have_size = std::max(peer->statistics().total_payload_upload()
			, std::int64_t(t->torrent_file().piece_length()) * peer->num_have_pieces());
		return int(std::abs((have_size - total_size / 2) * 2000 / total_size));
	}

	// return true if 'lhs' peer should be preferred to be unchoke over 'rhs'
	bool unchoke_compare_anti_leech(peer_connection const* lhs
		, peer_connection const* rhs)
	{
		int const cmp = compare_peers(lhs, rhs);
		if (cmp != 0) return cmp > 0;

		int const score1 = anti_leech_score(lhs);
		int const score2 = anti_leech_score(rhs);
		if (score1 != score2) return score1 > score2;

		// prioritize the one that has waited the longest to be unchoked
		// the round-robin unchoker relies on this logic. Don't change it
		// without moving this into that unchoker logic
		return lhs->time_of_last_unchoke() < rhs->time_of_last_unchoke();
	}

	bool upload_rate_compare(peer_connection const* lhs
		, peer_connection const* rhs)
	{
		// take torrent priority into account
		std::int64_t const c1 = lhs->uploaded_in_last_round()
			* lhs->get_priority(peer_connection::upload_channel);
		std::int64_t const c2 = rhs->uploaded_in_last_round()
			* rhs->get_priority(peer_connection::upload_channel);

		return c1 > c2;
	}

	} // anonymous namespace

	int unchoke_sort(std::vector<peer_connection*>& peers
		, time_duration const unchoke_interval
		, aux::session_settings const& sett)
	{
#if TORRENT_USE_ASSERTS
		for (auto p : peers)
		{
			TORRENT_ASSERT(p->self());
			TORRENT_ASSERT(p->associated_torrent().lock());
		}
#endif

		int upload_slots = sett.get_int(settings_pack::unchoke_slots_limit);
		if (upload_slots < 0)
			upload_slots = std::numeric_limits<int>::max();

		// ==== rate-based ====
		//
		// The rate based unchoker looks at our upload rate to peers, and find
		// a balance between number of upload slots and the rate we achieve. The
		// intention is to not spread upload bandwidth too thin, but also to not
		// unchoke few enough peers to not be able to saturate the up-link.
		// this is done by traversing the peers sorted by our upload rate to
		// them in decreasing rates. For each peer we increase the threshold by
		// 2 kiB/s. The first peer we get to whom we upload slower than
		// the threshold, we stop and that's the number of unchoke slots we have.
		if (sett.get_int(settings_pack::choking_algorithm)
			== settings_pack::rate_based_choker)
		{
			// first reset the number of unchoke slots, because we'll calculate
			// it purely based on the current state of our peers.
			upload_slots = 0;

			int rate_threshold = sett.get_int(settings_pack::rate_choker_initial_threshold);

			std::sort(peers.begin(), peers.end()
				, [](peer_connection const* lhs, peer_connection const* rhs)
				{ return upload_rate_compare(lhs, rhs); });

			for (auto const* p : peers)
			{
				int const rate = int(p->uploaded_in_last_round()
					* 1000 / total_milliseconds(unchoke_interval));

				// always have at least 1 unchoke slot
				if (rate < rate_threshold) break;

				++upload_slots;

				// TODO: make configurable
				rate_threshold += 2048;
			}
			++upload_slots;
		}

		// sorts the peers that are eligible for unchoke by download rate and
		// secondary by total upload. The reason for this is, if all torrents are
		// being seeded, the download rate will be 0, and the peers we have sent
		// the least to should be unchoked

		// we use partial sort here, because we only care about the top
		// upload_slots peers.

		int const slots = std::min(upload_slots, int(peers.size()));

		if (sett.get_int(settings_pack::seed_choking_algorithm)
			== settings_pack::round_robin)
		{
			int const pieces = sett.get_int(settings_pack::seeding_piece_quota);

			std::nth_element(peers.begin(), peers.begin()
				+ slots, peers.end()
				, [pieces](peer_connection const* lhs, peer_connection const* rhs)
				{ return unchoke_compare_rr(lhs, rhs, pieces); });
		}
		else if (sett.get_int(settings_pack::seed_choking_algorithm)
			== settings_pack::fastest_upload)
		{
			std::nth_element(peers.begin(), peers.begin()
				+ slots, peers.end()
				, [](peer_connection const* lhs, peer_connection const* rhs)
				{ return unchoke_compare_fastest_upload(lhs, rhs); });
		}
		else if (sett.get_int(settings_pack::seed_choking_algorithm)
			== settings_pack::anti_leech)
		{
			std::nth_element(peers.begin(), peers.begin()
				+ slots, peers.end()
				, [](peer_connection const* lhs, peer_connection const* rhs)
				{ return unchoke_compare_anti_leech(lhs, rhs); });
		}
		else
		{
			int const pieces = sett.get_int(settings_pack::seeding_piece_quota);
			std::nth_element(peers.begin(), peers.begin()
				+ slots, peers.end()
				, [pieces](peer_connection const* lhs, peer_connection const* rhs)
				{ return unchoke_compare_rr(lhs, rhs, pieces); } );

			TORRENT_ASSERT_FAIL();
		}

		return upload_slots;
	}

}
