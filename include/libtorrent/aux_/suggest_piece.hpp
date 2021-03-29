/*

Copyright (c) 2016, 2018, 2020, Arvid Norberg
Copyright (c) 2016-2017, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SUGGEST_PIECE_HPP_INCLUDE
#define TORRENT_SUGGEST_PIECE_HPP_INCLUDE

#include <vector>
#include <algorithm>

#include "libtorrent/bitfield.hpp"
#include "libtorrent/aux_/sliding_average.hpp"
#include "libtorrent/aux_/vector.hpp"

namespace lt::aux {

struct suggest_piece
{
	// pick at most n piece indices that are _not_ in p (which represents
	// pieces the peer has already sent a suggest for) nor in bits (which are
	// pieces the peer already has, and should not be suggested)
	int get_pieces(std::vector<piece_index_t>& p
		, typed_bitfield<piece_index_t> const& bits
		, int n)
	{
		if (m_priority_pieces.empty()) return 0;

		int ret = 0;

		// the highest priority pieces are at the end of m_priority_pieces.
		// if we add any piece to the result (p), the farther back the better.
		// the prioritization in p is the same, which means we have to first push
		// back and then reverse the items we put there.
		for (int i = int(m_priority_pieces.size()) - 1; i >= 0; --i)
		{
			piece_index_t const piece = m_priority_pieces[i];
			if (bits.get_bit(piece)) continue;
			if (std::any_of(p.begin(), p.end() - ret
				, [piece](piece_index_t pi) { return pi == piece; }))
				continue;

			p.push_back(piece);
			++ret;
			--n;
			if (n == 0) break;
		}

		// this it to maintain a strict priority order of pieces. The farther
		// back, the higher priority
		std::reverse(p.end() - ret, p.end());

		return ret;
	}

	void add_piece(piece_index_t const index, int const availability
		, int const max_queue_size)
	{
		// keep a running average of the availability of pieces, and filter
		// anything above average.
		int const mean = m_availability.mean();
		m_availability.add_sample(availability);

		if (availability > mean) return;

		auto const it = std::find(m_priority_pieces.begin()
			, m_priority_pieces.end(), index);

		if (it != m_priority_pieces.end())
		{
			// increase the priority of this piece by moving it to the front
			// of the queue
			m_priority_pieces.erase(it);
		}

		if (int(m_priority_pieces.size()) >= max_queue_size)
		{
			int const to_remove = int(m_priority_pieces.size()) - max_queue_size + 1;
			m_priority_pieces.erase(m_priority_pieces.begin()
				, m_priority_pieces.begin() + to_remove);
		}

		m_priority_pieces.push_back(index);
	}

private:

	// these are pieces that would be good candidates for suggesting
	// to a peer. They represent low availability pieces that we recently
	// read from disk (and are likely in our read cache).
	// pieces closer to the end were inserted into the cache more recently and
	// have higher priority
	vector<piece_index_t, int> m_priority_pieces;

	sliding_average<int, 30> m_availability;
};

}

#endif
