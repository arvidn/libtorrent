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

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>
#include <functional>
#include <tuple>

#include "libtorrent/piece_picker.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/aux_/alloca.hpp"
#include "libtorrent/aux_/range.hpp"
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/alert_types.hpp" // for picker_log_alert
#include "libtorrent/download_priority.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size

#if TORRENT_USE_ASSERTS
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/torrent_peer.hpp"
#endif

#include "libtorrent/invariant_check.hpp"

// this is really only useful for debugging unit tests
//#define TORRENT_PICKER_LOG

using namespace std::placeholders;

#if defined TORRENT_PICKER_LOG
#include <iostream>

namespace libtorrent {
	void print_pieces(piece_picker const& p)
	{
		int limit = 20;
		std::cerr << "[" << &p << "] ";
		if (p.m_dirty)
		{
			std::cerr << " === dirty ===" << std::endl;
			return;
		}

		for (prio_index_t b : p.m_priority_boundaries)
			std::cerr << b << " ";

		std::cerr << std::endl;
		prio_index_t index(0);
		std::cerr << "[" << &p << "] ";
		auto j = p.m_priority_boundaries.begin();
		for (auto i = p.m_pieces.begin(), end(p.m_pieces.end()); i != end; ++i, ++index)
		{
			if (limit == 0)
			{
				std::cerr << " ...";
				break;
			}
			if (*i == -1) break;
			while (j != p.m_priority_boundaries.end() && *j <= index)
			{
				std::cerr << "| ";
				++j;
			}
			std::cerr << *i << "(" << p.m_piece_map[*i].index << ") ";
			--limit;
		}
		std::cerr << std::endl;
	}
}
#endif // TORRENT_PICKER_LOG
namespace libtorrent {

	// TODO: find a better place for this
	const piece_block piece_block::invalid(
		std::numeric_limits<piece_index_t>::max()
		, std::numeric_limits<int>::max());

	constexpr prio_index_t piece_picker::piece_pos::we_have_index;

	constexpr picker_options_t piece_picker::rarest_first;
	constexpr picker_options_t piece_picker::reverse;
	constexpr picker_options_t piece_picker::on_parole;
	constexpr picker_options_t piece_picker::prioritize_partials;
	constexpr picker_options_t piece_picker::sequential;
	constexpr picker_options_t piece_picker::time_critical_mode;
	constexpr picker_options_t piece_picker::align_expanded_pieces;
	constexpr picker_options_t piece_picker::piece_extent_affinity;

	constexpr download_queue_t piece_picker::piece_pos::piece_downloading;
	constexpr download_queue_t piece_picker::piece_pos::piece_full;
	constexpr download_queue_t piece_picker::piece_pos::piece_finished;
	constexpr download_queue_t piece_picker::piece_pos::piece_zero_prio;
	constexpr download_queue_t piece_picker::piece_pos::num_download_categories;
	constexpr download_queue_t piece_picker::piece_pos::piece_open;
	constexpr download_queue_t piece_picker::piece_pos::piece_downloading_reverse;
	constexpr download_queue_t piece_picker::piece_pos::piece_full_reverse;

	// the max number of blocks to create an affinity for
	constexpr int max_piece_affinity_extent = 4 * 1024 * 1024 / default_block_size;

	piece_picker::piece_picker(int const blocks_per_piece
		, int const blocks_in_last_piece, int const total_num_pieces)
		: m_priority_boundaries(1, m_pieces.end_index())
	{
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "new piece_picker" << std::endl;
#endif
#if TORRENT_USE_INVARIANT_CHECKS
		check_invariant();
#endif

		resize(blocks_per_piece, blocks_in_last_piece, total_num_pieces);
	}

	void piece_picker::resize(int const blocks_per_piece
		, int const blocks_in_last_piece, int const total_num_pieces)
	{
		TORRENT_ASSERT(blocks_per_piece > 0);
		TORRENT_ASSERT(total_num_pieces > 0);

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "piece_picker::resize()" << std::endl;
#endif

		if (blocks_per_piece > max_blocks_per_piece)
			throw system_error(errors::invalid_piece_size);

		// allocate the piece_map to cover all pieces
		// and make them invalid (as if we don't have a single piece)
		m_piece_map.resize(total_num_pieces, piece_pos(0, 0));
		m_reverse_cursor = m_piece_map.end_index();
		m_cursor = piece_index_t(0);

		for (auto& c : m_downloads) c.clear();
		m_block_info.clear();
		m_free_block_infos.clear();

		m_num_filtered += m_num_have_filtered;
		m_num_have_filtered = 0;
		m_num_have = 0;
		m_have_pad_blocks = 0;
		m_filtered_pad_blocks = 0;
		m_have_filtered_pad_blocks = 0;
		m_num_passed = 0;
		m_dirty = true;
		for (auto& m : m_piece_map)
		{
			m.peer_count = 0;
			m.state(piece_pos::piece_open);
			m.index = prio_index_t(0);
#ifdef TORRENT_DEBUG_REFCOUNTS
			m.have_peers.clear();
#endif
		}

		for (auto i = m_piece_map.begin() + static_cast<int>(m_cursor)
			, end(m_piece_map.end()); i != end && (i->have() || i->filtered());
			++i, ++m_cursor);

		for (auto i = m_piece_map.rend() - static_cast<int>(m_reverse_cursor);
			m_reverse_cursor > piece_index_t(0) && (i->have() || i->filtered());
			++i, --m_reverse_cursor);

		m_blocks_per_piece = aux::numeric_cast<std::uint16_t>(blocks_per_piece);
		m_blocks_in_last_piece = aux::numeric_cast<std::uint16_t>(blocks_in_last_piece);
		if (m_blocks_in_last_piece == 0) m_blocks_in_last_piece = aux::numeric_cast<std::uint16_t>(blocks_per_piece);

		TORRENT_ASSERT(m_blocks_in_last_piece <= m_blocks_per_piece);
	}

	void piece_picker::piece_info(piece_index_t const index, piece_picker::downloading_piece& st) const
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

		auto const state = m_piece_map[index].download_queue();
		if (state != piece_pos::piece_open)
		{
			auto piece = find_dl_piece(state, index);
			TORRENT_ASSERT(piece != m_downloads[state].end());
			st = *piece;
			return;
		}
		st.info_idx = 0;
		st.index = index;
		st.writing = 0;
		st.requested = 0;
		if (m_piece_map[index].have())
		{
			st.finished = std::uint16_t(blocks_in_piece(index));
			return;
		}
		st.finished = 0;
	}

	piece_picker::piece_stats_t piece_picker::piece_stats(piece_index_t const index) const
	{
		piece_pos const& pp = m_piece_map[index];
		piece_stats_t ret = {
			int(pp.peer_count + m_seeds),
			pp.priority(this),
			pp.have(),
			pp.downloading()
		};
		return ret;
	}

	std::vector<piece_picker::downloading_piece>::iterator
	piece_picker::add_download_piece(piece_index_t const piece)
	{
		TORRENT_ASSERT(piece >= piece_index_t(0));
		TORRENT_ASSERT(piece < m_piece_map.end_index());
#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

		int block_index;

		if (m_free_block_infos.empty())
		{
			// we need to allocate more space in m_block_info
			block_index = int(m_block_info.size() / m_blocks_per_piece);
			TORRENT_ASSERT((m_block_info.size() % m_blocks_per_piece) == 0);
			m_block_info.resize(m_block_info.size() + m_blocks_per_piece);
		}
		else
		{
			// there is already free space in m_block_info, grab one range
			block_index = int(m_free_block_infos.back());
			m_free_block_infos.pop_back();
		}

		// always insert into bucket 0 (piece_downloading)
		downloading_piece ret;
		ret.index = piece;
		auto const download_state = piece_pos::piece_downloading;
		auto downloading_iter = std::lower_bound(m_downloads[download_state].begin()
			, m_downloads[download_state].end(), ret);
		TORRENT_ASSERT(downloading_iter == m_downloads[download_state].end()
			|| downloading_iter->index != piece);
		TORRENT_ASSERT(block_index >= 0);
		TORRENT_ASSERT(block_index < std::numeric_limits<std::uint16_t>::max());
		ret.info_idx = std::uint16_t(block_index);
		TORRENT_ASSERT(int(ret.info_idx) * m_blocks_per_piece
			+ m_blocks_per_piece <= int(m_block_info.size()));

		int block_idx = 0;
		for (auto& info : mutable_blocks_for_piece(ret))
		{
			info.num_peers = 0;
			info.state = block_info::state_none;
			if (!m_pad_blocks.empty() && m_pad_blocks.get_bit(static_cast<int>(piece) * m_blocks_per_piece + block_idx))
			{
				info.state = block_info::state_finished;
				++ret.finished;
			}
			else
			{
				info.state = block_info::state_none;
			}
			++block_idx;
			info.peer = nullptr;
#if TORRENT_USE_ASSERTS
			info.piece_index = piece;
			info.peers.clear();
#endif
		}
		downloading_iter = m_downloads[download_state].insert(downloading_iter, ret);

		// in case every block was a pad block, we need to make sure the piece
		// structure is correctly categorised
		downloading_iter = update_piece_state(downloading_iter);

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif
		return downloading_iter;
	}

	void piece_picker::erase_download_piece(std::vector<downloading_piece>::iterator i)
	{
#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

		auto const download_state = m_piece_map[i->index].download_queue();
		TORRENT_ASSERT(download_state != piece_pos::piece_open);
		TORRENT_ASSERT(find_dl_piece(download_state, i->index) == i);
#if TORRENT_USE_ASSERTS
		int prev_size = int(m_downloads[download_state].size());
#endif

		// since we're removing a downloading_piece, we also need to free its
		// blocks that are allocated from the m_block_info array.
		m_free_block_infos.push_back(i->info_idx);

		TORRENT_ASSERT(find_dl_piece(download_state, i->index) == i);
		m_piece_map[i->index].state(piece_pos::piece_open);
		m_downloads[download_state].erase(i);

		TORRENT_ASSERT(prev_size == int(m_downloads[download_state].size()) + 1);

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif
	}

	std::vector<piece_picker::downloading_piece> piece_picker::get_download_queue() const
	{
#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

		std::vector<downloading_piece> ret;
		for (auto const& c : m_downloads)
			ret.insert(ret.end(), c.begin(), c.end());
		return ret;
	}

	int piece_picker::get_download_queue_size() const
	{
		return std::accumulate(m_downloads.begin(), m_downloads.end(), 0
			, [](int const acc, aux::vector<downloading_piece> const& q) { return acc + int(q.size()); });
	}

	void piece_picker::get_download_queue_sizes(int* partial
		, int* full, int* finished, int* zero_prio) const
	{
		*partial = int(m_downloads[piece_pos::piece_downloading].size());
		*full = int(m_downloads[piece_pos::piece_full].size());
		*finished = int(m_downloads[piece_pos::piece_finished].size());
		*zero_prio = int(m_downloads[piece_pos::piece_zero_prio].size());
	}

	span<piece_picker::block_info> piece_picker::mutable_blocks_for_piece(
		downloading_piece const& dp)
	{
		int idx = int(dp.info_idx) * m_blocks_per_piece;
		TORRENT_ASSERT(idx + m_blocks_per_piece <= int(m_block_info.size()));
		return { &m_block_info[idx], blocks_in_piece(dp.index) };
	}

	span<piece_picker::block_info const> piece_picker::blocks_for_piece(
		downloading_piece const& dp) const
	{
		return const_cast<piece_picker*>(this)->mutable_blocks_for_piece(dp);
	}

#if TORRENT_USE_INVARIANT_CHECKS

	void piece_picker::check_piece_state() const
	{
		for (auto const k : categories())
		{
			if (m_downloads[k].empty()) continue;
			for (auto i = m_downloads[k].begin(); i != m_downloads[k].end() - 1; ++i)
			{
				downloading_piece const& dp = *i;
				downloading_piece const& next = *(i + 1);
				TORRENT_ASSERT(dp.index < next.index);
				TORRENT_ASSERT(int(dp.info_idx) * m_blocks_per_piece
					+ m_blocks_per_piece <= int(m_block_info.size()));
				for (auto const& bl : blocks_for_piece(dp))
				{
					if (!bl.peer) continue;
					torrent_peer* p = bl.peer;
					TORRENT_ASSERT(p->in_use);
					TORRENT_ASSERT(p->connection == nullptr
						|| static_cast<peer_connection*>(p->connection)->m_in_use);
				}
			}
		}
	}

	void piece_picker::verify_pick(std::vector<piece_block> const& picked
		, typed_bitfield<piece_index_t> const& bits) const
	{
		TORRENT_ASSERT(bits.size() == num_pieces());
		for (piece_block const& pb : picked)
		{
			TORRENT_ASSERT(bits[pb.piece_index]);
			TORRENT_ASSERT(!m_piece_map[pb.piece_index].have());
			TORRENT_ASSERT(!m_piece_map[pb.piece_index].filtered());
		}
	}

	void piece_picker::verify_priority(prio_index_t const range_start
		, prio_index_t const range_end
		, int const prio) const
	{
		TORRENT_ASSERT(range_start <= range_end);
		TORRENT_ASSERT(range_end <= m_pieces.end_index());
		for (auto index : range(m_pieces, range_start, range_end))
		{
			int p = m_piece_map[index].priority(this);
			TORRENT_ASSERT(p == prio);
		}
	}
#endif // TORRENT_USE_INVARIANT_CHECKS

#if TORRENT_USE_INVARIANT_CHECKS
	void piece_picker::check_peer_invariant(typed_bitfield<piece_index_t> const& have
		, torrent_peer const* p) const
	{
#ifdef TORRENT_DEBUG_REFCOUNTS
		for (piece_index_t i(0); i < have.end_index(); ++i)
		{
			bool const h = have[i];
			TORRENT_ASSERT(int(m_piece_map[i].have_peers.count(p)) == (h ? 1 : 0));
		}
#else
		TORRENT_UNUSED(have);
		TORRENT_UNUSED(p);
#endif
	}

	void piece_picker::check_invariant(torrent const* t) const
	{
		TORRENT_ASSERT(m_num_have >= 0);
		TORRENT_ASSERT(m_num_have_filtered >= 0);
		TORRENT_ASSERT(m_num_have_filtered <= m_num_have);
		TORRENT_ASSERT(m_num_have_filtered + m_num_filtered <= num_pieces());
		TORRENT_ASSERT(m_num_filtered >= 0);
		TORRENT_ASSERT(m_seeds >= 0);
		TORRENT_ASSERT(m_have_pad_blocks <= num_pad_blocks());
		TORRENT_ASSERT(m_have_pad_blocks >= 0);
		TORRENT_ASSERT(m_filtered_pad_blocks <= num_pad_blocks());
		TORRENT_ASSERT(m_filtered_pad_blocks >= 0);
		TORRENT_ASSERT(m_have_filtered_pad_blocks <= num_pad_blocks());
		TORRENT_ASSERT(m_have_filtered_pad_blocks >= 0);
		TORRENT_ASSERT(m_have_filtered_pad_blocks + m_filtered_pad_blocks <= num_pad_blocks());
		TORRENT_ASSERT(m_have_filtered_pad_blocks <= m_have_pad_blocks);

		// make sure the priority boundaries are monotonically increasing. The
		// difference between two cursors cannot be negative, but ranges are
		// allowed to be empty.
		prio_index_t last(0);
		for (prio_index_t b : m_priority_boundaries)
		{
			TORRENT_ASSERT(b >= last);
			last = b;
		}

		check_piece_state();

		if (t != nullptr)
			TORRENT_ASSERT(num_pieces() == t->torrent_file().num_pieces());

		for (auto const j : categories())
		{
			for (auto const& dp : m_downloads[j])
			{
				TORRENT_ASSERT(m_piece_map[dp.index].download_queue() == j);
				const int num_blocks = blocks_in_piece(dp.index);
				int num_requested = 0;
				int num_finished = 0;
				int num_writing = 0;
				int num_open = 0;
				for (auto const& bl : blocks_for_piece(dp))
				{
					TORRENT_ASSERT(bl.piece_index == dp.index);
					TORRENT_ASSERT(bl.peer == nullptr
						|| bl.peer->in_use);

					if (bl.state == block_info::state_finished)
					{
						++num_finished;
						TORRENT_ASSERT(bl.num_peers == 0);
					}
					else if (bl.state == block_info::state_requested)
					{
						++num_requested;
						TORRENT_ASSERT(bl.num_peers > 0);
					}
					else if (bl.state == block_info::state_writing)
					{
						++num_writing;
						TORRENT_ASSERT(bl.num_peers == 0);
					}
					else if (bl.state == block_info::state_none)
					{
						++num_open;
						TORRENT_ASSERT(bl.num_peers == 0);
					}
				}

				if (j == piece_pos::piece_downloading)
				{
					TORRENT_ASSERT(!m_piece_map[dp.index].filtered());
					TORRENT_ASSERT(num_open > 0);
				}
				else if (j == piece_pos::piece_full)
				{
					TORRENT_ASSERT(!m_piece_map[dp.index].filtered());
					TORRENT_ASSERT(num_open == 0);
					// if requested == 0, the piece should be in the finished state
					TORRENT_ASSERT(num_requested > 0);
				}
				else if (j == piece_pos::piece_finished)
				{
					TORRENT_ASSERT(!m_piece_map[dp.index].filtered());
					TORRENT_ASSERT(num_open == 0);
					TORRENT_ASSERT(num_requested == 0);
					TORRENT_ASSERT(num_finished + num_writing == num_blocks);
				}
				else if (j == piece_pos::piece_zero_prio)
				{
					TORRENT_ASSERT(m_piece_map[dp.index].filtered());
				}

				TORRENT_ASSERT(num_requested == dp.requested);
				TORRENT_ASSERT(num_writing == dp.writing);
				TORRENT_ASSERT(num_finished == dp.finished);

				if (m_piece_map[dp.index].download_queue() == piece_pos::piece_full
					|| m_piece_map[dp.index].download_queue() == piece_pos::piece_finished)
					TORRENT_ASSERT(num_finished + num_writing + num_requested == num_blocks);
			}
		}
		TORRENT_ASSERT(m_cursor >= piece_index_t(0));
		TORRENT_ASSERT(m_cursor <= m_piece_map.end_index());
		TORRENT_ASSERT(m_reverse_cursor >= piece_index_t(0));
		TORRENT_ASSERT(m_reverse_cursor <= m_piece_map.end_index());
		TORRENT_ASSERT(m_reverse_cursor > m_cursor
			|| (m_cursor == m_piece_map.end_index()
				&& m_reverse_cursor == piece_index_t(0)));

		if (!m_dirty)
		{
			TORRENT_ASSERT(!m_priority_boundaries.empty());
			int prio = 0;
			prio_index_t start(0);
			for (prio_index_t b : m_priority_boundaries)
			{
				verify_priority(start, b, prio);
				++prio;
				start = b;
			}
			TORRENT_ASSERT(m_priority_boundaries.back() == m_pieces.end_index());
		}

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		{
			piece_index_t index(0);
			for (auto i = m_piece_map.begin()
				, end(m_piece_map.end()); i != end && (i->have() || i->filtered());
				++i, ++index);
			TORRENT_ASSERT(m_cursor == index);
			index = m_piece_map.end_index();
			if (num_pieces() > 0)
			{
				for (auto i = m_piece_map.rend() - static_cast<int>(index); index > piece_index_t(0)
					&& (i->have() || i->filtered()); ++i, --index);
				TORRENT_ASSERT(index == m_piece_map.end_index()
					|| m_piece_map[index].have()
					|| m_piece_map[index].filtered());
				TORRENT_ASSERT(m_reverse_cursor == index);
			}
			else
			{
				TORRENT_ASSERT(m_reverse_cursor == piece_index_t(0));
			}
		}

		int num_filtered = 0;
		int num_have_filtered = 0;
		int num_have = 0;
		int num_have_pad_blocks = 0;
		int num_filtered_pad_blocks = 0;
		int num_have_filtered_pad_blocks = 0;
		piece_index_t piece(0);
		for (auto i = m_piece_map.begin(); i != m_piece_map.end(); ++i, ++piece)
		{
			piece_pos const& p = *i;

			if (p.filtered())
			{
				if (p.index != piece_pos::we_have_index)
				{
					++num_filtered;
					num_filtered_pad_blocks += pad_blocks_in_piece(piece);
				}
				else
				{
					++num_have_filtered;
					num_have_filtered_pad_blocks += pad_blocks_in_piece(piece);
				}
			}

#ifdef TORRENT_DEBUG_REFCOUNTS
			TORRENT_ASSERT(int(p.have_peers.size()) == p.peer_count + m_seeds);
#endif
			if (p.index == piece_pos::we_have_index)
			{
				++num_have;
				num_have_pad_blocks += pad_blocks_in_piece(piece);
			}

			if (p.index == piece_pos::we_have_index)
			{
				TORRENT_ASSERT(t == nullptr || t->have_piece(piece));
				TORRENT_ASSERT(p.downloading() == false);
			}

			if (t != nullptr)
				TORRENT_ASSERT(!t->have_piece(piece));

			int const prio = p.priority(this);

			if (p.downloading())
			{
				if (p.reverse())
					TORRENT_ASSERT(prio == -1 || (prio % piece_picker::prio_factor == 2));
				else
					TORRENT_ASSERT(prio == -1 || (prio % piece_picker::prio_factor == 0));
			}
			else
			{
				TORRENT_ASSERT(prio == -1 || (prio % piece_picker::prio_factor == 1));
			}

			if (!m_dirty)
			{
				TORRENT_ASSERT(prio < int(m_priority_boundaries.size()));
				if (prio >= 0)
				{
					TORRENT_ASSERT(p.index < m_pieces.end_index());
					TORRENT_ASSERT(m_pieces[p.index] == piece);
				}
				else
				{
					TORRENT_ASSERT(prio == -1);
					// make sure there's no entry
					// with this index. (there shouldn't
					// be since the priority is -1)
					TORRENT_ASSERT(std::count(m_pieces.begin(), m_pieces.end(), piece) == 0);
				}
			}

			int const count_downloading = int(std::count_if(
				m_downloads[piece_pos::piece_downloading].begin()
				, m_downloads[piece_pos::piece_downloading].end()
				, has_index(piece)));

			int const count_full = int(std::count_if(
				m_downloads[piece_pos::piece_full].begin()
				, m_downloads[piece_pos::piece_full].end()
				, has_index(piece)));

			int const count_finished = int(std::count_if(
				m_downloads[piece_pos::piece_finished].begin()
				, m_downloads[piece_pos::piece_finished].end()
				, has_index(piece)));

			int const count_zero = int(std::count_if(
				m_downloads[piece_pos::piece_zero_prio].begin()
				, m_downloads[piece_pos::piece_zero_prio].end()
				, has_index(piece)));

			TORRENT_ASSERT(i->download_queue() == piece_pos::piece_open
				|| count_zero + count_downloading + count_full
					+ count_finished == 1);

			auto const dq = i->download_queue();
			if (dq == piece_pos::piece_open) {
				TORRENT_ASSERT(count_downloading
					+ count_full + count_finished + count_zero == 0);
			}
			else if (dq == piece_pos::piece_downloading) {
				TORRENT_ASSERT(count_downloading == 1);
			}
			else if (dq == piece_pos::piece_full) {
				TORRENT_ASSERT(count_full == 1);
			}
			else if (dq == piece_pos::piece_finished) {
				TORRENT_ASSERT(count_finished == 1);
			}
			else if (dq == piece_pos::piece_zero_prio) {
				TORRENT_ASSERT(count_zero == 1);
			}
		}
		TORRENT_ASSERT(num_have == m_num_have);
		TORRENT_ASSERT(num_filtered == m_num_filtered);
		TORRENT_ASSERT(num_have_filtered == m_num_have_filtered);
		TORRENT_ASSERT(num_have_pad_blocks == m_have_pad_blocks);
		TORRENT_ASSERT(num_filtered_pad_blocks == m_filtered_pad_blocks);
		TORRENT_ASSERT(num_have_filtered_pad_blocks == m_have_filtered_pad_blocks);

		if (!m_dirty)
		{
			for (piece_index_t i : m_pieces)
			{
				TORRENT_ASSERT(m_piece_map[i].priority(this) >= 0);
			}
		}
#endif // TORRENT_EXPENSIVE_INVARIANT_CHECKS
	}
#endif

	std::pair<int, int> piece_picker::distributed_copies() const
	{
		TORRENT_ASSERT(m_seeds >= 0);
		const int npieces = num_pieces();

		if (npieces == 0) return std::make_pair(1, 0);
		int min_availability = piece_pos::max_peer_count;
		// find the lowest availability count
		// count the number of pieces that have that availability
		// and also the number of pieces that have more than that.
		int integer_part = 0;
		int fraction_part = 0;
		for (std::vector<piece_pos>::const_iterator i = m_piece_map.begin()
			, end(m_piece_map.end()); i != end; ++i)
		{
			int peer_count = int(i->peer_count);
			// take ourself into account
			if (i->have()) ++peer_count;
			if (min_availability > peer_count)
			{
				min_availability = peer_count;
				fraction_part += integer_part;
				integer_part = 1;
			}
			else if (peer_count == min_availability)
			{
				++integer_part;
			}
			else
			{
				TORRENT_ASSERT(peer_count > min_availability);
				++fraction_part;
			}
		}
		TORRENT_ASSERT(integer_part + fraction_part == npieces);
		return std::make_pair(min_availability + m_seeds, fraction_part * 1000 / npieces);
	}

	prio_index_t piece_picker::priority_begin(int const prio) const
	{
		TORRENT_ASSERT(prio >= 0);
		TORRENT_ASSERT(prio < int(m_priority_boundaries.size()));
		return prio == 0 ? prio_index_t(0) : m_priority_boundaries[prio - 1];
	}

	prio_index_t piece_picker::priority_end(int const prio) const
	{
		TORRENT_ASSERT(prio >= 0);
		TORRENT_ASSERT(prio < int(m_priority_boundaries.size()));
		return m_priority_boundaries[prio];
	}

	std::pair<prio_index_t, prio_index_t> piece_picker::priority_range(int const prio) const
	{
		TORRENT_ASSERT(prio >= 0);
		TORRENT_ASSERT(prio < int(m_priority_boundaries.size()));
		return {priority_begin(prio), priority_end(prio)};
	}

	void piece_picker::add(piece_index_t index)
	{
		TORRENT_ASSERT(!m_dirty);
		piece_pos const& p = m_piece_map[index];
		TORRENT_ASSERT(!p.filtered());
		TORRENT_ASSERT(!p.have());

		int priority = p.priority(this);
		TORRENT_ASSERT(priority >= 0);
		if (priority < 0) return;

		if (int(m_priority_boundaries.size()) <= priority)
			m_priority_boundaries.resize(priority + 1, m_pieces.end_index());

		TORRENT_ASSERT(int(m_priority_boundaries.size()) >= priority);

		auto const range = priority_range(priority);
		prio_index_t new_index = (range.second == range.first)
			? range.first
			: prio_index_t(
				int(random(aux::numeric_cast<std::uint32_t>(static_cast<int>(range.second - range.first))))
				+ static_cast<int>(range.first));

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "add " << index << " (" << priority << ")" << std::endl;
		std::cerr << "[" << this << "] " << "  p: state: " << p.download_state
			<< " peer_count: " << p.peer_count
			<< " prio: " << p.piece_priority
			<< " index: " << p.index << std::endl;
		print_pieces(*this);
#endif
		m_pieces.push_back(piece_index_t(-1));

		for (;;)
		{
			TORRENT_ASSERT(new_index < m_pieces.end_index());
			{
				piece_index_t temp = m_pieces[new_index];
				m_pieces[new_index] = index;
				m_piece_map[index].index = new_index;
				index = temp;
			}
			prio_index_t temp(-1);
			do
			{
				temp = m_priority_boundaries[priority]++;
				++priority;
			} while (temp == new_index && priority < int(m_priority_boundaries.size()));
			new_index = temp;
#ifdef TORRENT_PICKER_LOG
			print_pieces(*this);
			std::cerr << "[" << this << "] " << " index: " << index
				<< " prio: " << priority
				<< " new_index: " << new_index
				<< std::endl;
#endif
			if (priority >= int(m_priority_boundaries.size())) break;
			TORRENT_ASSERT(temp >= prio_index_t(0));
		}
		if (index != piece_index_t(-1))
		{
			TORRENT_ASSERT(new_index == prev(m_pieces.end_index()));
			m_pieces[new_index] = index;
			m_piece_map[index].index = new_index;

#ifdef TORRENT_PICKER_LOG
			print_pieces(*this);
#endif
		}
	}

	void piece_picker::remove(int priority, prio_index_t elem_index)
	{
		TORRENT_ASSERT(!m_dirty);
		TORRENT_ASSERT(priority >= 0);

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "remove " << m_pieces[elem_index] << " (" << priority << ")" << std::endl;
#endif
		prio_index_t next_index = elem_index;
		TORRENT_ASSERT(m_piece_map[m_pieces[elem_index]].priority(this) == -1);
		for (;;)
		{
#ifdef TORRENT_PICKER_LOG
			print_pieces(*this);
#endif
			TORRENT_ASSERT(elem_index < m_pieces.end_index());
			prio_index_t temp{};
			do
			{
				temp = --m_priority_boundaries[priority];
				++priority;
			} while (next_index == temp && priority < int(m_priority_boundaries.size()));
			if (next_index == temp) break;
			next_index = temp;

			piece_index_t const piece = m_pieces[next_index];
			m_pieces[elem_index] = piece;
			m_piece_map[piece].index = elem_index;
			TORRENT_ASSERT(m_piece_map[piece].priority(this) == priority - 1);
			TORRENT_ASSERT(elem_index < prev(m_pieces.end_index()));
			elem_index = next_index;

			if (priority == int(m_priority_boundaries.size()))
				break;
		}
		m_pieces.pop_back();
		TORRENT_ASSERT(next_index == m_pieces.end_index());
#ifdef TORRENT_PICKER_LOG
		print_pieces(*this);
#endif
	}

	// will update the piece with the given properties (priority, elem_index)
	// to place it at the correct position
	void piece_picker::update(int priority, prio_index_t elem_index)
	{
		TORRENT_ASSERT(!m_dirty);
		TORRENT_ASSERT(priority >= 0);
		TORRENT_ASSERT(int(m_priority_boundaries.size()) > priority);

		// make sure the passed in elem_index actually lives in the specified
		// priority bucket. If it doesn't, it means this piece changed
		// state without updating the corresponding entry in the pieces list
		TORRENT_ASSERT(m_priority_boundaries[priority] >= elem_index);
		TORRENT_ASSERT(elem_index >= priority_begin(priority));
		TORRENT_ASSERT(elem_index < priority_end(priority));

		piece_index_t const index = m_pieces[elem_index];
		// update the piece_map
		piece_pos& p = m_piece_map[index];
		TORRENT_ASSERT(p.index == elem_index || p.have());

		int const new_priority = p.priority(this);

		if (new_priority == priority) return;

		if (new_priority == -1)
		{
			remove(priority, elem_index);
			return;
		}

		if (int(m_priority_boundaries.size()) <= new_priority)
			m_priority_boundaries.resize(new_priority + 1, m_pieces.end_index());

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "update " << index << " (" << priority << "->" << new_priority << ")" << std::endl;
#endif
		if (priority > new_priority)
		{
			prio_index_t new_index{};
			piece_index_t temp = index;
			for (;;)
			{
#ifdef TORRENT_PICKER_LOG
				print_pieces(*this);
#endif
				TORRENT_ASSERT(priority > 0);
				--priority;
				new_index = m_priority_boundaries[priority]++;
				if (temp != m_pieces[new_index])
				{
					temp = m_pieces[new_index];
					m_pieces[elem_index] = temp;
					m_piece_map[temp].index = elem_index;
					TORRENT_ASSERT(elem_index < m_pieces.end_index());
				}
				elem_index = new_index;
				if (priority == new_priority) break;
			}
#ifdef TORRENT_PICKER_LOG
			print_pieces(*this);
#endif
			m_pieces[elem_index] = index;
			m_piece_map[index].index = elem_index;
			TORRENT_ASSERT(elem_index < m_pieces.end_index());
#ifdef TORRENT_PICKER_LOG
			print_pieces(*this);
#endif
			shuffle(priority, elem_index);
#ifdef TORRENT_PICKER_LOG
			print_pieces(*this);
#endif
			TORRENT_ASSERT(m_piece_map[index].priority(this) == priority);
		}
		else
		{
			prio_index_t new_index{};
			piece_index_t temp = index;
			for (;;)
			{
#ifdef TORRENT_PICKER_LOG
				print_pieces(*this);
#endif
				TORRENT_ASSERT(priority >= 0);
				TORRENT_ASSERT(priority < int(m_priority_boundaries.size()));
				new_index = --m_priority_boundaries[priority];
				if (temp != m_pieces[new_index])
				{
					temp = m_pieces[new_index];
					m_pieces[elem_index] = temp;
					m_piece_map[temp].index = elem_index;
					TORRENT_ASSERT(elem_index < m_pieces.end_index());
				}
				elem_index = new_index;
				++priority;
				if (priority == new_priority) break;
			}
#ifdef TORRENT_PICKER_LOG
			print_pieces(*this);
#endif
			m_pieces[elem_index] = index;
			m_piece_map[index].index = elem_index;
			TORRENT_ASSERT(elem_index < m_pieces.end_index());
#ifdef TORRENT_PICKER_LOG
			print_pieces(*this);
#endif
			shuffle(priority, elem_index);
#ifdef TORRENT_PICKER_LOG
			print_pieces(*this);
#endif
			TORRENT_ASSERT(m_piece_map[index].priority(this) == priority);
		}
	}

	void piece_picker::shuffle(int const priority, prio_index_t const elem_index)
	{
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "shuffle()" << std::endl;
#endif

		TORRENT_ASSERT(!m_dirty);
		TORRENT_ASSERT(priority >= 0);
		TORRENT_ASSERT(elem_index >= prio_index_t(0));
		TORRENT_ASSERT(elem_index < m_pieces.end_index());
		TORRENT_ASSERT(m_piece_map[m_pieces[elem_index]].priority(this) == priority);

		auto const range = priority_range(priority);
		prio_index_t const other_index(
			int(random(aux::numeric_cast<std::uint32_t>(static_cast<int>(range.second - range.first) - 1)))
			+ static_cast<int>(range.first));

		if (other_index == elem_index) return;

		// swap other_index with elem_index
		piece_pos& p1 = m_piece_map[m_pieces[other_index]];
		piece_pos& p2 = m_piece_map[m_pieces[elem_index]];

		std::swap(p1.index, p2.index);
		std::swap(m_pieces[other_index], m_pieces[elem_index]);
	}

	void piece_picker::restore_piece(piece_index_t const index)
	{
		INVARIANT_CHECK;

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "restore_piece(" << index << ")" << std::endl;
#endif
		auto const download_state = m_piece_map[index].download_queue();
		TORRENT_ASSERT(download_state != piece_pos::piece_open);
		if (download_state == piece_pos::piece_open) return;

		auto i = find_dl_piece(download_state, index);

		TORRENT_ASSERT(i != m_downloads[download_state].end());
		TORRENT_ASSERT(int(i->info_idx) * m_blocks_per_piece
			+ m_blocks_per_piece <= int(m_block_info.size()));

		i->locked = false;

		piece_pos& p = m_piece_map[index];
		int const prev_priority = p.priority(this);
		erase_download_piece(i);
		int const new_priority = p.priority(this);

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

		if (new_priority == prev_priority) return;
		if (m_dirty) return;
		if (prev_priority == -1) add(index);
		else update(prev_priority, p.index);

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif
	}

	void piece_picker::inc_refcount_all(const torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

		++m_seeds;
		if (m_seeds == 1)
		{
			// when m_seeds is increased from 0 to 1
			// we may have to add pieces that previously
			// didn't have any peers
			m_dirty = true;
		}
#ifdef TORRENT_DEBUG_REFCOUNTS
		for (std::vector<piece_pos>::iterator i = m_piece_map.begin()
			, end(m_piece_map.end()); i != end; ++i)
		{
			TORRENT_ASSERT(i->have_peers.count(peer) == 0);
			i->have_peers.insert(peer);
		}
#else
		TORRENT_UNUSED(peer);
#endif
	}

	void piece_picker::dec_refcount_all(const torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

		if (m_seeds > 0)
		{
			--m_seeds;
			if (m_seeds == 0)
			{
				// when m_seeds is decreased from 1 to 0
				// we may have to remove pieces that previously
				// didn't have any peers
				m_dirty = true;
			}
#ifdef TORRENT_DEBUG_REFCOUNTS
			for (std::vector<piece_pos>::iterator i = m_piece_map.begin()
				, end(m_piece_map.end()); i != end; ++i)
			{
				TORRENT_ASSERT(i->have_peers.count(peer) == 1);
				i->have_peers.erase(peer);
			}
#else
			TORRENT_UNUSED(peer);
#endif
			return;
		}
		TORRENT_ASSERT(m_seeds == 0);

		for (auto& i : m_piece_map)
		{
#ifdef TORRENT_DEBUG_REFCOUNTS
			TORRENT_ASSERT(i.have_peers.count(peer) == 1);
			i.have_peers.erase(peer);
#else
			TORRENT_UNUSED(peer);
#endif

			TORRENT_ASSERT(i.peer_count > 0);
			--i.peer_count;
		}

		m_dirty = true;
	}

	void piece_picker::inc_refcount(piece_index_t const index
		, const torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "inc_refcount(" << index << ")" << std::endl;
#endif
		piece_pos& p = m_piece_map[index];

#ifdef TORRENT_DEBUG_REFCOUNTS
		TORRENT_ASSERT(p.have_peers.count(peer) == 0);
		p.have_peers.insert(peer);
#else
		TORRENT_UNUSED(peer);
#endif

		int prev_priority = p.priority(this);
		++p.peer_count;
		if (m_dirty) return;
		int new_priority = p.priority(this);
		if (prev_priority == new_priority) return;
		if (prev_priority == -1)
			add(index);
		else
			update(prev_priority, p.index);
	}

	// this function decrements the m_seeds counter
	// and increments the peer counter on every piece
	// instead. Sometimes of we connect to a seed that
	// later sends us a dont-have message, we'll need to
	// turn that m_seed into counts on the pieces since
	// they can't be negative
	void piece_picker::break_one_seed()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_seeds > 0);
		--m_seeds;

		for (auto& m : m_piece_map)
			++m.peer_count;

		m_dirty = true;
	}

	void piece_picker::dec_refcount(piece_index_t const index
		, const torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "dec_refcount(" << index << ")" << std::endl;
#endif

		piece_pos& p = m_piece_map[index];

		if (p.peer_count == 0)
		{
			TORRENT_ASSERT(m_seeds > 0);
			// this is the case where we have one or more
			// seeds, and one of them saying: I don't have this
			// piece anymore. we need to break up one of the seed
			// counters into actual peer counters on the pieces
			break_one_seed();
		}

		int const prev_priority = p.priority(this);

#ifdef TORRENT_DEBUG_REFCOUNTS
		TORRENT_ASSERT(p.have_peers.count(peer) == 1);
		p.have_peers.erase(peer);
#else
		TORRENT_UNUSED(peer);
#endif

		TORRENT_ASSERT(p.peer_count > 0);
		--p.peer_count;
		if (m_dirty) return;
		if (prev_priority >= 0) update(prev_priority, p.index);
	}

	void piece_picker::inc_refcount(typed_bitfield<piece_index_t> const& bitmask
		, const torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "inc_refcount(bitfield)" << std::endl;
#endif

		// nothing set, nothing to do here
		if (bitmask.none_set()) return;

		if (bitmask.all_set() && bitmask.size() == int(m_piece_map.size()))
		{
			inc_refcount_all(peer);
			return;
		}

		int const size = std::min(50, int(bitmask.size() / 2));

		// this is an optimization where if just a few
		// pieces end up changing, instead of making
		// the piece list dirty, just update those pieces
		// instead
		TORRENT_ALLOCA(incremented, piece_index_t, size);

		if (!m_dirty)
		{
			// first count how many pieces we're updating. If it's few (less than half)
			// we'll just update them one at a time. Otherwise we'll just update the counters
			// and mark the picker as dirty, so we'll rebuild it next time we need it.
			// this only matters if we're not already dirty, in which case the fasted
			// thing to do is to just update the counters and be done
			piece_index_t index = piece_index_t(0);
			int num_inc = 0;
			for (auto i = bitmask.begin(), end(bitmask.end()); i != end; ++i, ++index)
			{
				if (!*i) continue;
				if (num_inc < size) incremented[num_inc] = index;
				++num_inc;
				if (num_inc >= size) break;
			}

			if (num_inc < size)
			{
				// not that many pieces were updated
				// just update those individually instead of
				// rebuilding the whole piece list
				for (int i = 0; i < num_inc; ++i)
				{
					piece_index_t const piece = incremented[i];
					piece_pos& p = m_piece_map[piece];
					int prev_priority = p.priority(this);
					++p.peer_count;
#ifdef TORRENT_DEBUG_REFCOUNTS
					TORRENT_ASSERT(p.have_peers.count(peer) == 0);
					p.have_peers.insert(peer);
#else
					TORRENT_UNUSED(peer);
#endif
					int new_priority = p.priority(this);
					if (prev_priority == new_priority) continue;
					else if (prev_priority >= 0) update(prev_priority, p.index);
					else add(piece);
				}
				return;
			}
		}

		piece_index_t index = piece_index_t(0);
		bool updated = false;
		for (auto i = bitmask.begin(), end(bitmask.end()); i != end; ++i, ++index)
		{
			if (*i)
			{
#ifdef TORRENT_DEBUG_REFCOUNTS
				TORRENT_ASSERT(m_piece_map[index].have_peers.count(peer) == 0);
				m_piece_map[index].have_peers.insert(peer);
#else
				TORRENT_UNUSED(peer);
#endif

				++m_piece_map[index].peer_count;
				updated = true;
			}
		}

		// if we're already dirty, no point in doing anything more
		if (m_dirty) return;

		if (updated) m_dirty = true;
	}

	void piece_picker::dec_refcount(typed_bitfield<piece_index_t> const& bitmask
		, const torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif
		TORRENT_ASSERT(bitmask.size() <= int(m_piece_map.size()));

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "dec_refcount(bitfield)" << std::endl;
#endif

		// nothing set, nothing to do here
		if (bitmask.none_set()) return;

		if (bitmask.all_set() && bitmask.size() == int(m_piece_map.size()))
		{
			dec_refcount_all(peer);
			return;
		}

		int const size = std::min(50, int(bitmask.size() / 2));

		// this is an optimization where if just a few
		// pieces end up changing, instead of making
		// the piece list dirty, just update those pieces
		// instead
		TORRENT_ALLOCA(decremented, piece_index_t, size);

		if (!m_dirty)
		{
			// first count how many pieces we're updating. If it's few (less than half)
			// we'll just update them one at a time. Otherwise we'll just update the counters
			// and mark the picker as dirty, so we'll rebuild it next time we need it.
			// this only matters if we're not already dirty, in which case the fasted
			// thing to do is to just update the counters and be done
			piece_index_t index = piece_index_t(0);
			int num_dec = 0;
			for (auto i = bitmask.begin(), end(bitmask.end()); i != end; ++i, ++index)
			{
				if (!*i) continue;
				if (num_dec < size) decremented[num_dec] = index;
				++num_dec;
				if (num_dec >= size) break;
			}

			if (num_dec < size)
			{
				// not that many pieces were updated
				// just update those individually instead of
				// rebuilding the whole piece list
				for (int i = 0; i < num_dec; ++i)
				{
					piece_index_t const piece = decremented[i];
					piece_pos& p = m_piece_map[piece];
					int prev_priority = p.priority(this);

					if (p.peer_count == 0)
					{
						TORRENT_ASSERT(m_seeds > 0);
						// this is the case where we have one or more
						// seeds, and one of them saying: I don't have this
						// piece anymore. we need to break up one of the seed
						// counters into actual peer counters on the pieces
						break_one_seed();
					}

#ifdef TORRENT_DEBUG_REFCOUNTS
					TORRENT_ASSERT(p.have_peers.count(peer) == 1);
					p.have_peers.erase(peer);
#else
					TORRENT_UNUSED(peer);
#endif
					TORRENT_ASSERT(p.peer_count > 0);
					--p.peer_count;
					if (!m_dirty && prev_priority >= 0) update(prev_priority, p.index);
				}
				return;
			}
		}

		piece_index_t index = piece_index_t(0);
		bool updated = false;
		for (auto i = bitmask.begin(), end(bitmask.end()); i != end; ++i, ++index)
		{
			if (*i)
			{
				piece_pos& p = m_piece_map[index];
				if (p.peer_count == 0)
				{
					TORRENT_ASSERT(m_seeds > 0);
					// this is the case where we have one or more
					// seeds, and one of them saying: I don't have this
					// piece anymore. we need to break up one of the seed
					// counters into actual peer counters on the pieces
					break_one_seed();
				}

#ifdef TORRENT_DEBUG_REFCOUNTS
				TORRENT_ASSERT(p.have_peers.count(peer) == 1);
				p.have_peers.erase(peer);
#else
				TORRENT_UNUSED(peer);
#endif

				TORRENT_ASSERT(p.peer_count > 0);
				--p.peer_count;
				updated = true;
			}
		}

		// if we're already dirty, no point in doing anything more
		if (m_dirty) return;

		if (updated) m_dirty = true;
	}

	void piece_picker::update_pieces() const
	{
		TORRENT_ASSERT(m_dirty);
		if (m_priority_boundaries.empty()) m_priority_boundaries.resize(1, prio_index_t(0));
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "update_pieces" << std::endl;
#endif

		// This code is unfortunately not very straight-forward. What we do here
		// is to count the number of pieces at every priority level. After this
		// first step, m_priority_boundaries will contain *deltas* rather than
		// absolute indices. This is fixed up in a second pass below
		std::fill(m_priority_boundaries.begin(), m_priority_boundaries.end(), prio_index_t(0));
		for (auto& pos : m_piece_map)
		{
			int prio = pos.priority(this);
			if (prio == -1) continue;
			if (prio >= int(m_priority_boundaries.size()))
				m_priority_boundaries.resize(prio + 1, prio_index_t(0));
			pos.index = m_priority_boundaries[prio];
			++m_priority_boundaries[prio];
		}

#ifdef TORRENT_PICKER_LOG
		print_pieces(*this);
#endif

		// m_priority_boundaries just contain counters of
		// each priority level at this point. Now, make the m_priority_boundaries
		// be cumulative indices into m_pieces (but m_pieces hasn't been set up
		// yet)
		int new_size = 0;
		for (prio_index_t& b : m_priority_boundaries)
		{
			new_size += static_cast<int>(b);
			b = prio_index_t(new_size);
		}
		m_pieces.resize(new_size, piece_index_t(0));

#ifdef TORRENT_PICKER_LOG
		print_pieces(*this);
#endif

		// set up m_pieces to contain valid piece indices, based on piece
		// priority. m_piece_map[].index is still just an index relative to the
		// respective priority range.
		piece_index_t piece = piece_index_t(0);
		for (auto i = m_piece_map.begin(), end(m_piece_map.end()); i != end; ++i, ++piece)
		{
			piece_pos& p = *i;
			int const prio = p.priority(this);
			if (prio == -1) continue;
			prio_index_t const new_index(priority_begin(prio)
				+ prio_index_t::diff_type(static_cast<int>(p.index)));
			m_pieces[new_index] = piece;
		}

		prio_index_t start(0);
		for (auto b : m_priority_boundaries)
		{
			if (start == b) continue;
			span<piece_index_t> r(&m_pieces[start], static_cast<int>(b - start));
			aux::random_shuffle(r);
			start = b;
		}

		// this is where we set fix up the m_piece_map[].index to actually map
		// back to the piece list ordered by priority (m_pieces)
		prio_index_t index(0);
		for (auto p : m_pieces)
		{
			m_piece_map[p].index = index;
			++index;
		}

		m_dirty = false;
#ifdef TORRENT_PICKER_LOG
		print_pieces(*this);
#endif
	}

	void piece_picker::piece_passed(piece_index_t const index)
	{
		piece_pos& p = m_piece_map[index];
		auto const download_state = p.download_queue();

		// this is kind of odd. Could this happen?
		TORRENT_ASSERT(download_state != piece_pos::piece_open);
		if (download_state == piece_pos::piece_open) return;

		auto const i = find_dl_piece(download_state, index);
		TORRENT_ASSERT(i != m_downloads[download_state].end());

		TORRENT_ASSERT(i->locked == false);
		if (i->locked) return;

		TORRENT_ASSERT(!i->passed_hash_check);
		i->passed_hash_check = true;
		++m_num_passed;

		if (i->finished < blocks_in_piece(index)) return;

		we_have(index);
	}

	void piece_picker::we_dont_have(piece_index_t const index)
	{
		INVARIANT_CHECK;
		piece_pos& p = m_piece_map[index];

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "piece_picker::we_dont_have("
			<< index << ")" << std::endl;
#endif

		if (!p.have())
		{
			// even though we don't have the piece, it
			// might still have passed hash check
			auto const download_state = p.download_queue();
			if (download_state == piece_pos::piece_open) return;

			auto const i = find_dl_piece(download_state, index);
			if (i->passed_hash_check)
			{
				i->passed_hash_check = false;
				TORRENT_ASSERT(m_num_passed > 0);
				--m_num_passed;
			}
			erase_download_piece(i);
			return;
		}

		TORRENT_ASSERT(m_num_passed > 0);
		--m_num_passed;
		if (p.filtered())
		{
			m_filtered_pad_blocks += pad_blocks_in_piece(index);
			++m_num_filtered;

			TORRENT_ASSERT(m_have_filtered_pad_blocks >= pad_blocks_in_piece(index));
			m_have_filtered_pad_blocks -= pad_blocks_in_piece(index);
			TORRENT_ASSERT(m_num_have_filtered > 0);
			--m_num_have_filtered;
		}
		else
		{
			// update cursors
			if (index < m_cursor) m_cursor = index;
			if (index >= m_reverse_cursor) m_reverse_cursor = next(index);
			if (m_reverse_cursor == m_cursor)
			{
				m_reverse_cursor = piece_index_t(0);
				m_cursor = m_piece_map.end_index();
			}
		}

		--m_num_have;
		m_have_pad_blocks -= pad_blocks_in_piece(index);
		TORRENT_ASSERT(m_have_pad_blocks >= 0);
		p.set_not_have();

		if (m_dirty) return;
		if (p.priority(this) >= 0) add(index);
	}

	// this is used to indicate that we successfully have
	// downloaded a piece, and that no further attempts
	// to pick that piece should be made. The piece will
	// be removed from the available piece list.
	void piece_picker::we_have(piece_index_t const index)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "piece_picker::we_have("
			<< index << ")" << std::endl;
#endif
		piece_pos& p = m_piece_map[index];
		prio_index_t const info_index = p.index;
		int const priority = p.priority(this);
		TORRENT_ASSERT(priority < int(m_priority_boundaries.size()) || m_dirty);

		if (p.have()) return;

		auto const state = p.download_queue();
		if (state != piece_pos::piece_open)
		{
			auto const i = find_dl_piece(state, index);
			TORRENT_ASSERT(i != m_downloads[state].end());
			// decrement num_passed here to compensate
			// for the unconditional increment further down
			if (i->passed_hash_check) --m_num_passed;
			erase_download_piece(i);
		}

		if (p.filtered())
		{
			TORRENT_ASSERT(m_filtered_pad_blocks >= pad_blocks_in_piece(index));
			m_filtered_pad_blocks -= pad_blocks_in_piece(index);
			TORRENT_ASSERT(m_num_filtered > 0);
			--m_num_filtered;

			m_have_filtered_pad_blocks += pad_blocks_in_piece(index);
			++m_num_have_filtered;
		}
		++m_num_have;
		++m_num_passed;
		m_have_pad_blocks += pad_blocks_in_piece(index);
		TORRENT_ASSERT(m_have_pad_blocks <= num_pad_blocks());
		p.set_have();
		if (m_cursor == prev(m_reverse_cursor)
			&& m_cursor == index)
		{
			m_cursor = m_piece_map.end_index();
			m_reverse_cursor = piece_index_t(0);
			TORRENT_ASSERT(num_pieces() > 0);
		}
		else if (m_cursor == index)
		{
			++m_cursor;
			for (auto i = m_piece_map.begin() + static_cast<int>(m_cursor)
				, end(m_piece_map.end()); i != end && (i->have() || i->filtered());
				++i, ++m_cursor);
		}
		else if (prev(m_reverse_cursor) == index)
		{
			--m_reverse_cursor;
			TORRENT_ASSERT(m_piece_map[m_reverse_cursor].have()
				|| m_piece_map[m_reverse_cursor].filtered());
			for (auto i = m_piece_map.begin() + static_cast<int>(m_reverse_cursor) - 1;
				m_reverse_cursor > piece_index_t(0) && (i->have() || i->filtered());
				--i, --m_reverse_cursor);
			TORRENT_ASSERT(m_piece_map[m_reverse_cursor].have()
				|| m_piece_map[m_reverse_cursor].filtered());
		}
		TORRENT_ASSERT(m_reverse_cursor > m_cursor
			|| (m_cursor == m_piece_map.end_index() && m_reverse_cursor == piece_index_t(0)));
		if (priority == -1) return;
		if (m_dirty) return;
		remove(priority, info_index);
		TORRENT_ASSERT(p.priority(this) == -1);
	}

	void piece_picker::we_have_all()
	{
		INVARIANT_CHECK;
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "piece_picker::we_have_all()\n";
#endif

		m_priority_boundaries.clear();
		m_priority_boundaries.resize(1, prio_index_t(0));
		m_block_info.clear();
		m_free_block_infos.clear();
		m_pieces.clear();

		m_dirty = false;
		m_num_have_filtered += m_num_filtered;
		m_num_filtered = 0;
		m_have_filtered_pad_blocks += m_filtered_pad_blocks;
		m_filtered_pad_blocks = 0;
		m_cursor = m_piece_map.end_index();
		m_reverse_cursor = piece_index_t{0};
		m_num_passed = num_pieces();
		m_num_have = num_pieces();

		for (auto& queue : m_downloads) queue.clear();
		for (auto& p : m_piece_map)
		{
			p.set_have();
			p.state(piece_pos::piece_open);
		}
	}

	bool piece_picker::set_piece_priority(piece_index_t const index
		, download_priority_t const new_piece_priority)
	{
		INVARIANT_CHECK;

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "set_piece_priority(" << index
			<< ", " << new_piece_priority << ")" << std::endl;
#endif

		static_assert(std::is_unsigned<decltype(new_piece_priority)::underlying_type>::value
			, "we need assert new_piece_priority >= dont_download");
		TORRENT_ASSERT(new_piece_priority <= top_priority);

		piece_pos& p = m_piece_map[index];

		// if the priority isn't changed, don't do anything
		if (new_piece_priority == download_priority_t(p.piece_priority)) return false;

		int const prev_priority = p.priority(this);
		TORRENT_ASSERT(m_dirty || prev_priority < int(m_priority_boundaries.size()));

		bool ret = false;
		if (new_piece_priority == dont_download
			&& p.piece_priority != piece_pos::filter_priority)
		{
			// the piece just got filtered
			if (p.have())
			{
				m_have_filtered_pad_blocks += pad_blocks_in_piece(index);
				++m_num_have_filtered;
			}
			else
			{
				m_filtered_pad_blocks += pad_blocks_in_piece(index);
				++m_num_filtered;

				// update m_cursor
				if (m_cursor == prev(m_reverse_cursor) && m_cursor == index)
				{
					m_cursor = m_piece_map.end_index();
					m_reverse_cursor = piece_index_t(0);
				}
				else if (m_cursor == index)
				{
					++m_cursor;
					while (m_cursor < m_piece_map.end_index()
						&& (m_piece_map[m_cursor].have()
						|| m_piece_map[m_cursor].filtered()))
						++m_cursor;
				}
				else if (m_reverse_cursor == next(index))
				{
					--m_reverse_cursor;
					while (m_reverse_cursor > piece_index_t(0)
						&& (m_piece_map[prev(m_reverse_cursor)].have()
						|| m_piece_map[prev(m_reverse_cursor)].filtered()))
						--m_reverse_cursor;
				}
			}
			ret = true;
		}
		else if (new_piece_priority != dont_download
			&& p.piece_priority == piece_pos::filter_priority)
		{
			// the piece just got unfiltered
			if (p.have())
			{
				TORRENT_ASSERT(m_have_filtered_pad_blocks >= pad_blocks_in_piece(index));
				m_have_filtered_pad_blocks -= pad_blocks_in_piece(index);
				TORRENT_ASSERT(m_num_have_filtered > 0);
				--m_num_have_filtered;
			}
			else
			{
				TORRENT_ASSERT(m_filtered_pad_blocks >= pad_blocks_in_piece(index));
				m_filtered_pad_blocks -= pad_blocks_in_piece(index);
				TORRENT_ASSERT(m_num_filtered > 0);
				--m_num_filtered;
				// update cursors
				if (index < m_cursor) m_cursor = index;
				if (index >= m_reverse_cursor) m_reverse_cursor = next(index);
				if (m_reverse_cursor == m_cursor)
				{
					m_reverse_cursor = piece_index_t(0);
					m_cursor = m_piece_map.end_index();
				}
			}
			ret = true;
		}
		TORRENT_ASSERT(m_num_filtered >= 0);
		TORRENT_ASSERT(m_num_have_filtered >= 0);

		p.piece_priority = static_cast<std::uint8_t>(new_piece_priority);
		int const new_priority = p.priority(this);

		if (prev_priority != new_priority && !m_dirty)
		{
			if (prev_priority == -1) add(index);
			else update(prev_priority, p.index);
		}

		if (p.downloading())
		{
			auto const i = find_dl_piece(p.download_queue(), index);
			if (i != m_downloads[p.download_queue()].end())
				update_piece_state(i);
		}

		return ret;
	}

	download_priority_t piece_picker::piece_priority(piece_index_t const index) const
	{
		return download_priority_t(m_piece_map[index].piece_priority);
	}

	void piece_picker::piece_priorities(std::vector<download_priority_t>& pieces) const
	{
		pieces.resize(m_piece_map.size());
		auto j = pieces.begin();
		for (auto i = m_piece_map.begin(),
			end(m_piece_map.end()); i != end; ++i, ++j)
		{
			*j = download_priority_t(i->piece_priority);
		}
	}

namespace {

		int append_blocks(std::vector<piece_block>& dst, std::vector<piece_block>& src
			, int const num_blocks)
		{
			if (src.empty()) return num_blocks;
			int const to_copy = std::min(int(src.size()), num_blocks);

			dst.insert(dst.end(), src.begin(), src.begin() + to_copy);
			src.erase(src.begin(), src.begin() + to_copy);
			return num_blocks - to_copy;
		}
	}

	// lower availability comes first. This is a less-than comparison, it returns
	// true if lhs has lower availability than rhs
	bool piece_picker::partial_compare_rarest_first(downloading_piece const* lhs
		, downloading_piece const* rhs) const
	{
		int lhs_availability = m_piece_map[lhs->index].peer_count;
		int rhs_availability = m_piece_map[rhs->index].peer_count;
		if (lhs_availability != rhs_availability)
			return lhs_availability < rhs_availability;

		// if the availability is the same, prefer the piece that's closest to
		// being complete.
		int lhs_blocks_left = m_blocks_per_piece - lhs->finished - lhs->writing
			- lhs->requested;
		TORRENT_ASSERT(lhs_blocks_left > 0);
		int rhs_blocks_left = m_blocks_per_piece - rhs->finished - rhs->writing
			- rhs->requested;
		TORRENT_ASSERT(rhs_blocks_left > 0);
		return lhs_blocks_left < rhs_blocks_left;
	}

	// pieces describes which pieces the peer we're requesting from has.
	// interesting_blocks is an out parameter, and will be filled with (up to)
	// num_blocks of interesting blocks that the peer has.
	// prefer_contiguous_blocks can be set if this peer should download whole
	// pieces rather than trying to download blocks from the same piece as other
	// peers. the peer argument is the torrent_peer of the peer we're
	// picking pieces from. This is used when downloading whole pieces, to only
	// pick from the same piece the same peer is downloading from.

	// options are:
	// * rarest_first
	//     pick the rarest pieces first
	// * reverse
	//     reverse the piece picking. Pick the most common
	//     pieces first or the last pieces (if picking sequential)
	// * sequential
	//     download pieces in-order
	// * on_parole
	//     the peer is on parole, only pick whole pieces which
	//     has only been downloaded and requested from the same
	//     peer
	// * prioritize_partials
	//     pick blocks from downloading pieces first

	// only one of rarest_first or sequential can be set

	// the return value is a combination of picker_flags_t,
	// indicating which path thought the picker we took to arrive at the
	// returned block picks.
	picker_flags_t piece_picker::pick_pieces(typed_bitfield<piece_index_t> const& pieces
		, std::vector<piece_block>& interesting_blocks, int num_blocks
		, int prefer_contiguous_blocks, torrent_peer* peer
		, picker_options_t options, std::vector<piece_index_t> const& suggested_pieces
		, int num_peers
		, counters& pc
		) const
	{
		TORRENT_ASSERT(peer == nullptr || peer->in_use);
		picker_flags_t ret;

		// prevent the number of partial pieces to grow indefinitely
		// make this scale by the number of peers we have. For large
		// scale clients, we would have more peers, and allow a higher
		// threshold for the number of partials
		// the second condition is to make sure we cap the number of partial
		// _bytes_. The larger the pieces are, the fewer partial pieces we want.
		// 2048 corresponds to 32 MiB
		// TODO: 2 make the 2048 limit configurable
		const int num_partials = int(m_downloads[piece_pos::piece_downloading].size());
		if (num_partials > num_peers * 3 / 2
			|| num_partials * m_blocks_per_piece > 2048)
		{
			// if we have too many partial pieces, prioritize completing
			// them. In order for this to have an affect, also disable
			// prefer whole pieces (otherwise partial pieces would be de-prioritized)
			options |= prioritize_partials;
			prefer_contiguous_blocks = 0;

			ret |= picker_log_alert::partial_ratio;
		}

		if (prefer_contiguous_blocks) ret |= picker_log_alert::prefer_contiguous;

		// only one of rarest_first and sequential can be set.
		TORRENT_ASSERT(((options & rarest_first) ? 1 : 0)
			+ ((options & sequential) ? 1 : 0) <= 1);
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif
		TORRENT_ASSERT(num_blocks > 0);
		TORRENT_ASSERT(pieces.size() == int(m_piece_map.size()));

		TORRENT_ASSERT(!m_priority_boundaries.empty() || m_dirty);

		// this will be filled with blocks that we should not request
		// unless we can't find num_blocks among the other ones.
		std::vector<piece_block> backup_blocks;
		std::vector<piece_block> backup_blocks2;
		static const std::vector<piece_index_t> empty_vector;

		// When prefer_contiguous_blocks is set (usually set when downloading from
		// fast peers) the partial pieces will not be prioritized, but actually
		// ignored as long as possible. All blocks found in downloading
		// pieces are regarded as backup blocks

		if (options & prioritize_partials)
		{
			// first, allocate a small array on the stack of all the partial
			// pieces (downloading_piece). We'll then sort this list by
			// availability or by some other condition. The list of partial pieces
			// in m_downloads is ordered by piece index, this is to have O(log n)
			// lookups when finding a downloading_piece for a specific piece index.
			// this is important and needs to stay sorted that way, that's why
			// we're copying it here
			TORRENT_ALLOCA(ordered_partials, downloading_piece const*
				, m_downloads[piece_pos::piece_downloading].size());
			int num_ordered_partials = 0;

			// now, copy over the pointers. We also apply a filter here to not
			// include ineligible pieces in certain modes. For instance, a piece
			// that the current peer doesn't have is not included.
			for (auto& dp : m_downloads[piece_pos::piece_downloading])
			{
				pc.inc_stats_counter(counters::piece_picker_partial_loops);

				// in time critical mode, only pick high priority pieces
				if ((options & time_critical_mode)
					&& piece_priority(dp.index) != top_priority)
					continue;

				if (!is_piece_free(dp.index, pieces)) continue;

				TORRENT_ASSERT(m_piece_map[dp.index].download_queue()
					== piece_pos::piece_downloading);

				ordered_partials[num_ordered_partials++] = &dp;
			}

			// now, sort the list.
			if (options & rarest_first)
			{
				ret |= picker_log_alert::rarest_first_partials;

				// TODO: this could probably be optimized by incrementally
				// calling partial_sort to sort one more element in the list. Because
				// chances are that we'll just need a single piece, and once we've
				// picked from it we're done. Sorting the rest of the list in that
				// case is a waste of time.
				std::sort(ordered_partials.begin(), ordered_partials.begin() + num_ordered_partials
					, std::bind(&piece_picker::partial_compare_rarest_first, this
						, _1, _2));
			}

			for (int i = 0; i < num_ordered_partials; ++i)
			{
				ret |= picker_log_alert::prioritize_partials;

				num_blocks = add_blocks_downloading(*ordered_partials[i], pieces
					, interesting_blocks, backup_blocks, backup_blocks2
					, num_blocks, prefer_contiguous_blocks, peer, options);
				if (num_blocks <= 0) return ret;
				if (int(backup_blocks.size()) >= num_blocks
					&& int(backup_blocks2.size()) >= num_blocks)
					break;
			}

			num_blocks = append_blocks(interesting_blocks, backup_blocks
				, num_blocks);
			if (num_blocks <= 0) return ret;

			num_blocks = append_blocks(interesting_blocks, backup_blocks2
				, num_blocks);
			if (num_blocks <= 0) return ret;
		}

		if (!suggested_pieces.empty())
		{
			for (piece_index_t i : suggested_pieces)
			{
				// in time critical mode, only pick high priority pieces
				if ((options & time_critical_mode)
					&& piece_priority(i) != top_priority)
					continue;

				pc.inc_stats_counter(counters::piece_picker_suggest_loops);
				if (!is_piece_free(i, pieces)) continue;

				ret |= picker_log_alert::suggested_pieces;

				num_blocks = add_blocks(i, pieces
					, interesting_blocks, backup_blocks
					, backup_blocks2, num_blocks
					, prefer_contiguous_blocks, peer, empty_vector
					, options);
				if (num_blocks <= 0) return ret;
			}
		}

		if (options & sequential)
		{
			if (m_dirty) update_pieces();
			TORRENT_ASSERT(!m_dirty);

			for (auto i = m_pieces.begin();
				i != m_pieces.end() && piece_priority(*i) == top_priority; ++i)
			{
				if (!is_piece_free(*i, pieces)) continue;

				ret |= picker_log_alert::prio_sequential_pieces;

				num_blocks = add_blocks(*i, pieces
					, interesting_blocks, backup_blocks
					, backup_blocks2, num_blocks
					, prefer_contiguous_blocks, peer, suggested_pieces
					, options);
				if (num_blocks <= 0) return ret;
			}

			// in time critical mode, only pick high priority pieces
			if (!(options & time_critical_mode))
			{
				if (options & reverse)
				{
					for (piece_index_t i = prev(m_reverse_cursor); i >= m_cursor; --i)
					{
						if (!is_piece_free(i, pieces)) continue;
						// we've already added high priority pieces
						if (piece_priority(i) == top_priority) continue;

						ret |= picker_log_alert::reverse_sequential;

						num_blocks = add_blocks(i, pieces
							, interesting_blocks, backup_blocks
							, backup_blocks2, num_blocks
							, prefer_contiguous_blocks, peer, suggested_pieces
							, options);
						if (num_blocks <= 0) return ret;
					}
				}
				else
				{
					for (piece_index_t i = m_cursor; i < m_reverse_cursor; ++i)
					{
						if (!is_piece_free(i, pieces)) continue;
						// we've already added high priority pieces
						if (piece_priority(i) == top_priority) continue;

						ret |= picker_log_alert::sequential_pieces;

						num_blocks = add_blocks(i, pieces
							, interesting_blocks, backup_blocks
							, backup_blocks2, num_blocks
							, prefer_contiguous_blocks, peer, suggested_pieces
							, options);
						if (num_blocks <= 0) return ret;
					}
				}
			}
		}
		else if (options & rarest_first)
		{
			if (m_dirty) update_pieces();
			TORRENT_ASSERT(!m_dirty);

			// in time critical mode, we're only allowed to pick high priority
			// pieces. This is why reverse mode is disabled when we're in
			// time-critical mode, because all high priority pieces are at the
			// front of the list
			if ((options & reverse) && !(options & time_critical_mode))
			{
				for (int i = int(m_priority_boundaries.size()) - 1; i >= 0; --i)
				{
					prio_index_t const start = priority_begin(i);
					prio_index_t const end = priority_end(i);
					for (prio_index_t p = prev(end); p >= start; --p)
					{
						pc.inc_stats_counter(counters::piece_picker_reverse_rare_loops);

						if (!is_piece_free(m_pieces[p], pieces)) continue;

						ret |= picker_log_alert::reverse_rarest_first;

						num_blocks = add_blocks(m_pieces[p], pieces
							, interesting_blocks, backup_blocks
							, backup_blocks2, num_blocks
							, prefer_contiguous_blocks, peer, suggested_pieces
							, options);
						if (num_blocks <= 0) return ret;
					}
				}
			}
			else
			{
				// TODO: Is it a good idea that this affinity takes precedence over
				// piece priority?
				if (options & piece_extent_affinity)
				{
					int to_erase = -1;
					int idx = -1;
					for (piece_extent_t const e : m_recent_extents)
					{
						++idx;
						bool have_all = true;
						for (piece_index_t const p : extent_for(e))
						{
							if (!m_piece_map[p].have()) have_all = false;
							if (!is_piece_free(p, pieces)) continue;

							ret |= picker_log_alert::extent_affinity;

							num_blocks = add_blocks(p, pieces
								, interesting_blocks, backup_blocks
								, backup_blocks2, num_blocks
								, prefer_contiguous_blocks, peer, suggested_pieces
								, options);
							if (num_blocks <= 0)
							{
								// if we have all pieces belonging to this extent, remove it
								if (to_erase != -1) m_recent_extents.erase(m_recent_extents.begin() + to_erase);
								return ret;
							}
						}
						// if we have all pieces belonging to this extent, remove it
						if (have_all) to_erase = idx;
					}
					if (to_erase != -1) m_recent_extents.erase(m_recent_extents.begin() + to_erase);
				}

				for (piece_index_t i : m_pieces)
				{
					pc.inc_stats_counter(counters::piece_picker_rare_loops);

					// in time critical mode, only pick high priority pieces
					// it's safe to break here because in this mode we
					// pick pieces in priority order. Once we hit a lower priority
					// piece, we won't encounter any more high priority ones
					if ((options & time_critical_mode)
						&& piece_priority(i) != top_priority)
						break;

					if (!is_piece_free(i, pieces)) continue;

					ret |= picker_log_alert::rarest_first;

					num_blocks = add_blocks(i, pieces
						, interesting_blocks, backup_blocks
						, backup_blocks2, num_blocks
						, prefer_contiguous_blocks, peer, suggested_pieces
						, options);
					if (num_blocks <= 0) return ret;
				}
			}
		}
		else if (options & time_critical_mode)
		{
			// if we're in time-critical mode, we are only allowed to pick
			// high priority pieces.
			for (auto i = m_pieces.begin();
				i != m_pieces.end() && piece_priority(*i) == top_priority; ++i)
			{
				if (!is_piece_free(*i, pieces)) continue;

				ret |= picker_log_alert::time_critical;

				num_blocks = add_blocks(*i, pieces
					, interesting_blocks, backup_blocks
					, backup_blocks2, num_blocks
					, prefer_contiguous_blocks, peer, suggested_pieces
					, options);
				if (num_blocks <= 0) return ret;
			}
		}
		else
		{
			// we're not using rarest first (only for the first
			// bucket, since that's where the currently downloading
			// pieces are)
			piece_index_t const start_piece = piece_index_t(int(random(aux::numeric_cast<std::uint32_t>(m_piece_map.size() - 1))));

			piece_index_t piece = start_piece;
			while (num_blocks > 0)
			{
				// skip pieces we can't pick, and suggested pieces
				// since we've already picked those
				while (!is_piece_free(piece, pieces)
					|| std::find(suggested_pieces.begin()
						, suggested_pieces.end(), piece)
						!= suggested_pieces.end())
				{
					pc.inc_stats_counter(counters::piece_picker_rand_start_loops);
					++piece;
					if (piece == m_piece_map.end_index()) piece = piece_index_t(0);
					// could not find any more pieces
					if (piece == start_piece) { goto get_out; }
				}

				if (prefer_contiguous_blocks > 1 && !m_piece_map[piece].downloading())
				{
					TORRENT_ASSERT(can_pick(piece, pieces));
					TORRENT_ASSERT(m_piece_map[piece].downloading() == false);

					piece_index_t start, end;
					std::tie(start, end) = expand_piece(piece
						, prefer_contiguous_blocks, pieces, options);
					TORRENT_ASSERT(end > start);
					for (piece_index_t k = start; k < end; ++k)
					{
						TORRENT_ASSERT(m_piece_map[k].downloading() == false);
						TORRENT_ASSERT(m_piece_map[k].priority(this) >= 0);
						const int num_blocks_in_piece = blocks_in_piece(k);

						ret |= picker_log_alert::random_pieces;

						for (int j = 0; j < num_blocks_in_piece; ++j)
						{
							pc.inc_stats_counter(counters::piece_picker_rand_loops);
							TORRENT_ASSERT(is_piece_free(k, pieces));
							interesting_blocks.emplace_back(k, j);
							--num_blocks;
							--prefer_contiguous_blocks;
							if (prefer_contiguous_blocks <= 0
								&& num_blocks <= 0) break;
						}
					}
					piece = end;
				}
				else
				{
					ret |= picker_log_alert::random_pieces;

					num_blocks = add_blocks(piece, pieces
						, interesting_blocks, backup_blocks
						, backup_blocks2, num_blocks
						, prefer_contiguous_blocks, peer, empty_vector
						, options);
					++piece;
				}

				if (piece == m_piece_map.end_index()) piece = piece_index_t(0);
				// could not find any more pieces
				if (piece == start_piece) break;
			}
		}
get_out:

		if (num_blocks <= 0) return ret;

#if TORRENT_USE_INVARIANT_CHECKS
		verify_pick(interesting_blocks, pieces);
		verify_pick(backup_blocks, pieces);
		verify_pick(backup_blocks2, pieces);
#endif

		ret |= picker_log_alert::backup1;
		num_blocks = append_blocks(interesting_blocks, backup_blocks, num_blocks);
		if (num_blocks <= 0) return ret;

		ret |= picker_log_alert::backup2;
		num_blocks = append_blocks(interesting_blocks, backup_blocks2, num_blocks);
		if (num_blocks <= 0) return ret;

		// ===== THIS IS FOR END-GAME MODE =====

		// don't double-pick anything if the peer is on parole
		if (options & on_parole) return ret;

		// in end game mode we pick a single block
		// that has already been requested from someone
		// all pieces that are interesting are in
		// m_downloads[0] and m_download[1]
		// (i.e. partial and full pieces)

		std::vector<piece_block> temp;

		// pick one random block from one random partial piece.
		// only pick from non-downloaded blocks.
		// first, create a temporary array of the partial pieces
		// this peer has, and can pick from. Cap the stack allocation
		// at 200 pieces.

		int partials_size = std::min(200, int(
				m_downloads[piece_pos::piece_downloading].size()
			+ m_downloads[piece_pos::piece_full].size()));
		if (partials_size == 0) return ret;

		TORRENT_ALLOCA(partials, downloading_piece const*, partials_size);
		int c = 0;

#if TORRENT_USE_INVARIANT_CHECKS
		// if we get here, we're about to pick a busy block. First, make sure
		// we really exhausted the available blocks
		for (std::vector<downloading_piece>::const_iterator i
			= m_downloads[piece_pos::piece_downloading].begin()
			, end(m_downloads[piece_pos::piece_downloading].end()); i != end; ++i)
		{
			downloading_piece const& dp = *i;

			if ((options & time_critical_mode)
				&& piece_priority(dp.index) != top_priority)
				continue;

			// we either don't have this piece, or we've already requested from it
			if (!pieces[dp.index]) continue;

			// if we already have the piece, obviously we should not have
			// since this is a partial piece in the piece_downloading state, we
			// should not already have it
			TORRENT_ASSERT(!m_piece_map[dp.index].have());

			// if it was filtered, it would be in the prio_zero queue
			TORRENT_ASSERT(!m_piece_map[dp.index].filtered());

			// we're not allowed to pick from locked pieces
			if (dp.locked) continue;

			bool found = false;
			for (std::vector<piece_block>::const_iterator j
				= interesting_blocks.begin(), end2(interesting_blocks.end());
				j != end2; ++j)
			{
				if (j->piece_index != dp.index) continue;
				found = true;
				break;
			}

			// we expect to find this piece in our interesting_blocks list
			TORRENT_ASSERT(found);
		}
#endif

		for (auto const& dp : m_downloads[piece_pos::piece_full])
		{
			if (c == partials_size) break;

			TORRENT_ASSERT(dp.requested > 0);
			// this peer doesn't have this piece, try again
			if (!pieces[dp.index]) continue;
			// don't pick pieces with priority 0
			TORRENT_ASSERT(piece_priority(dp.index) > dont_download);

			if ((options & time_critical_mode)
				&& piece_priority(dp.index) != top_priority)
				continue;

			partials[c++] = &dp;
		}

		partials_size = c;
		while (partials_size > 0)
		{
			pc.inc_stats_counter(counters::piece_picker_busy_loops);
			int piece = int(random(aux::numeric_cast<std::uint32_t>(partials_size - 1)));
			downloading_piece const* dp = partials[piece];
			TORRENT_ASSERT(pieces[dp->index]);
			TORRENT_ASSERT(piece_priority(dp->index) > dont_download);
			// fill in with blocks requested from other peers
			// as backups
			TORRENT_ASSERT(dp->requested > 0);
			int idx = -1;
			for (auto const& info : blocks_for_piece(*dp))
			{
				++idx;
				TORRENT_ASSERT(info.peer == nullptr || info.peer->in_use);
				TORRENT_ASSERT(info.piece_index == dp->index);
				if (info.state != block_info::state_requested || info.peer == peer)
					continue;
				temp.emplace_back(dp->index, idx);
			}
			// are we done?
			if (!temp.empty())
			{
				ret |= picker_log_alert::end_game;
				interesting_blocks.push_back(temp[random(std::uint32_t(temp.size()) - 1)]);
				--num_blocks;
				break;
			}

			// the piece we picked only had blocks outstanding requested
			// by ourself. Remove it and pick another one.
			partials[piece] = partials[partials_size - 1];
			--partials_size;
		}

#if TORRENT_USE_INVARIANT_CHECKS
// make sure that we at this point have added requests to all unrequested blocks
// in all downloading pieces

		for (auto const& i : m_downloads[piece_pos::piece_downloading])
		{
			if (!pieces[i.index]) continue;
			if (piece_priority(i.index) == dont_download) continue;
			if (i.locked) continue;

			if ((options & time_critical_mode)
				&& piece_priority(i.index) != top_priority)
				continue;

			int idx = -1;
			for (auto const& info : blocks_for_piece(i))
			{
				++idx;
				TORRENT_ASSERT(info.piece_index == i.index);
				if (info.state != block_info::state_none) continue;
				auto k = std::find(interesting_blocks.begin(), interesting_blocks.end()
					, piece_block(i.index, idx));
				if (k != interesting_blocks.end()) continue;

				std::fprintf(stderr, "interesting blocks:\n");
				for (auto const& p : interesting_blocks)
				{
					std::fprintf(stderr, "(%d, %d)"
						, static_cast<int>(p.piece_index), p.block_index);
				}
				std::fprintf(stderr, "\nnum_blocks: %d\n", num_blocks);

				for (auto const& l : m_downloads[piece_pos::piece_downloading])
				{
					auto const binfo2 = blocks_for_piece(l);
					std::fprintf(stderr, "%d : ", static_cast<int>(l.index));
					const int cnt = blocks_in_piece(l.index);
					for (int m = 0; m < cnt; ++m)
						std::fprintf(stderr, "%d", binfo2[m].state);
					std::fprintf(stderr, "\n");
				}

				TORRENT_ASSERT_FAIL();
			}
		}

		if (interesting_blocks.empty())
		{
			for (piece_index_t i = piece_index_t(0);
				i != m_piece_map.end_index(); ++i)
			{
				if (!pieces[i]) continue;
				if (m_piece_map[i].priority(this) <= 0) continue;
				if (have_piece(i)) continue;

				auto const download_state = m_piece_map[i].download_queue();
				if (download_state == piece_pos::piece_open) continue;
				std::vector<downloading_piece>::const_iterator k
					= find_dl_piece(download_state, i);

				TORRENT_ASSERT(k != m_downloads[download_state].end());
				if (k == m_downloads[download_state].end()) continue;
			}
		}
#endif
		return ret;
	}

	// have piece means that the piece passed hash check
	// AND has been successfully written to disk
	bool piece_picker::have_piece(piece_index_t const index) const
	{
		piece_pos const& p = m_piece_map[index];
		return p.index == piece_pos::we_have_index;
	}

	int piece_picker::blocks_in_piece(piece_index_t const index) const
	{
		TORRENT_ASSERT(index >= piece_index_t(0));
		TORRENT_ASSERT(index < m_piece_map.end_index());
		if (next(index) == m_piece_map.end_index())
			return m_blocks_in_last_piece;
		else
			return m_blocks_per_piece;
	}

	bool piece_picker::is_piece_free(piece_index_t const piece
		, typed_bitfield<piece_index_t> const& bitmask) const
	{
		return bitmask[piece]
			&& !m_piece_map[piece].have()
			&& !m_piece_map[piece].filtered();
	}

	bool piece_picker::can_pick(piece_index_t const piece
		, typed_bitfield<piece_index_t> const& bitmask) const
	{
		return bitmask[piece]
			&& !m_piece_map[piece].have()
			// TODO: when expanding pieces for cache stripe reasons,
			// the !downloading condition doesn't make much sense
			&& !m_piece_map[piece].downloading()
			&& !m_piece_map[piece].filtered();
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void piece_picker::check_peers()
	{
		for (auto const& b : m_block_info)
		{
			TORRENT_ASSERT(b.peer == nullptr || static_cast<torrent_peer*>(b.peer)->in_use);
		}
	}
#endif

	void piece_picker::clear_peer(torrent_peer* peer)
	{
		for (auto& b : m_block_info)
		{
			if (b.peer == peer) b.peer = nullptr;
		}
	}

	// the first bool is true if this is the only peer that has requested and downloaded
	// blocks from this piece.
	// the second bool is true if this is the only active peer that is requesting
	// and downloading blocks from this piece. Active means having a connection.
	// TODO: 2 the first_block returned here is the largest free range, not
	// the first-fit range, which would be better
	std::tuple<bool, bool, int, int> piece_picker::requested_from(
		piece_picker::downloading_piece const& p
		, int const num_blocks_in_piece, torrent_peer* peer) const
	{
		bool exclusive = true;
		bool exclusive_active = true;
		int contiguous_blocks = 0;
		int max_contiguous = 0;
		int first_block = 0;
		int idx = -1;
		for (auto const& info : blocks_for_piece(p))
		{
			++idx;
			TORRENT_ASSERT(info.peer == nullptr || info.peer->in_use);
			TORRENT_ASSERT(info.piece_index == p.index);
			if (info.state == piece_picker::block_info::state_none)
			{
				++contiguous_blocks;
				continue;
			}
			if (contiguous_blocks > max_contiguous)
			{
				max_contiguous = contiguous_blocks;
				first_block = idx - contiguous_blocks;
			}
			contiguous_blocks = 0;
			if (info.peer != peer)
			{
				exclusive = false;
				if (info.state == piece_picker::block_info::state_requested
					&& info.peer != nullptr)
				{
					exclusive_active = false;
				}
			}
		}
		if (contiguous_blocks > max_contiguous)
		{
			max_contiguous = contiguous_blocks;
			first_block = num_blocks_in_piece - contiguous_blocks;
		}
		return std::make_tuple(exclusive, exclusive_active, max_contiguous
			, first_block);
	}

	int piece_picker::add_blocks(piece_index_t piece
		, typed_bitfield<piece_index_t> const& pieces
		, std::vector<piece_block>& interesting_blocks
		, std::vector<piece_block>& backup_blocks
		, std::vector<piece_block>& backup_blocks2
		, int num_blocks, int prefer_contiguous_blocks
		, torrent_peer* peer, std::vector<piece_index_t> const& ignore
		, picker_options_t const options) const
	{
		TORRENT_ASSERT(is_piece_free(piece, pieces));

		// ignore pieces found in the ignore list
		if (std::find(ignore.begin(), ignore.end(), piece) != ignore.end()) return num_blocks;

		auto const state = m_piece_map[piece].download_queue();
		if (state != piece_pos::piece_open
			&& state != piece_pos::piece_downloading)
			return num_blocks;

		TORRENT_ASSERT(m_piece_map[piece].priority(this) >= 0);
		if (state == piece_pos::piece_downloading)
		{
			// if we're prioritizing partials, we've already
			// looked through the downloading pieces
			if (options & prioritize_partials) return num_blocks;

			auto i = find_dl_piece(piece_pos::piece_downloading, piece);
			TORRENT_ASSERT(i != m_downloads[state].end());

			return add_blocks_downloading(*i, pieces
				, interesting_blocks, backup_blocks, backup_blocks2
				, num_blocks, prefer_contiguous_blocks, peer, options);
		}

		int num_blocks_in_piece = blocks_in_piece(piece);

		// pick a new piece
		if (prefer_contiguous_blocks == 0)
		{
			if (num_blocks_in_piece > num_blocks)
				num_blocks_in_piece = num_blocks;
			TORRENT_ASSERT(is_piece_free(piece, pieces));
			for (int j = 0; j < num_blocks_in_piece; ++j)
				interesting_blocks.emplace_back(piece, j);
			num_blocks -= num_blocks_in_piece;
		}
		else
		{
			piece_index_t start, end;
			std::tie(start, end) = expand_piece(piece, prefer_contiguous_blocks
				, pieces, options);
			for (piece_index_t k = start; k < end; ++k)
			{
				TORRENT_ASSERT(m_piece_map[k].priority(this) > 0);
				num_blocks_in_piece = blocks_in_piece(k);
				TORRENT_ASSERT(is_piece_free(k, pieces));
				for (int j = 0; j < num_blocks_in_piece; ++j)
				{
					interesting_blocks.emplace_back(k, j);
					--num_blocks;
					--prefer_contiguous_blocks;
					if (prefer_contiguous_blocks == 0
						&& num_blocks <= 0) break;
				}
			}
		}
#if TORRENT_USE_INVARIANT_CHECKS
		verify_pick(interesting_blocks, pieces);
#endif
		return std::max(num_blocks, 0);
	}

	int piece_picker::add_blocks_downloading(downloading_piece const& dp
		, typed_bitfield<piece_index_t> const& pieces
		, std::vector<piece_block>& interesting_blocks
		, std::vector<piece_block>& backup_blocks
		, std::vector<piece_block>& backup_blocks2
		, int num_blocks, int prefer_contiguous_blocks
		, torrent_peer* peer, picker_options_t const options) const
	{
		if (!pieces[dp.index]) return num_blocks;
		TORRENT_ASSERT(!m_piece_map[dp.index].filtered());

		// this piece failed to write. We're currently restoring
		// it. It's not OK to send more requests to it right now.
		if (dp.locked) return num_blocks;

		int num_blocks_in_piece = blocks_in_piece(dp.index);

		// is true if all the other pieces that are currently
		// requested from this piece are from the same
		// peer as 'peer'.
		bool exclusive;
		bool exclusive_active;

		// used to report back the largest contiguous block run
		int contiguous_blocks;
		int first_block;
		std::tie(exclusive, exclusive_active, contiguous_blocks, first_block)
			= requested_from(dp, num_blocks_in_piece, peer);

		// no need in picking from the largest contiguous block run unless
		// we're interested in it. In fact, we really want the opposite.
		if (prefer_contiguous_blocks == 0) first_block = 0;

		// peers on parole are only allowed to pick blocks from
		// pieces that only they have downloaded/requested from
		if ((options & on_parole) && !exclusive) return num_blocks;

		auto const binfo = blocks_for_piece(dp);

		// we prefer whole blocks, but there are other peers
		// downloading from this piece and there aren't enough contiguous blocks
		// to pick, add it as backups.
		// if we're on parole, don't let the contiguous blocks stop us, we want
		// to primarily request from a piece all by ourselves.
		if (prefer_contiguous_blocks > contiguous_blocks
			&& !exclusive_active
			&& !(options & on_parole))
		{
			if (int(backup_blocks2.size()) >= num_blocks)
				return num_blocks;

			for (int j = 0; j < num_blocks_in_piece; ++j)
			{
				// ignore completed blocks and already requested blocks
				int const block_idx = (j + first_block) % num_blocks_in_piece;
				block_info const& info = binfo[block_idx];
				TORRENT_ASSERT(info.piece_index == dp.index);
				if (info.state != block_info::state_none) continue;
				backup_blocks2.emplace_back(dp.index, block_idx);
			}
			return num_blocks;
		}

		for (int j = 0; j < num_blocks_in_piece; ++j)
		{
			// ignore completed blocks and already requested blocks
			int const block_idx = (j + first_block) % num_blocks_in_piece;
			block_info const& info = binfo[block_idx];
			TORRENT_ASSERT(info.piece_index == dp.index);
			if (info.state != block_info::state_none) continue;

			// this block is interesting (we don't have it yet).
			interesting_blocks.emplace_back(dp.index, block_idx);
			// we have found a block that's free to download
			--num_blocks;
			// if we prefer contiguous blocks, continue picking from this
			// piece even though we have num_blocks
			if (prefer_contiguous_blocks > 0)
			{
				--prefer_contiguous_blocks;
				continue;
			}
			if (num_blocks <= 0) return 0;
		}

		if (num_blocks <= 0) return 0;
		if (options & on_parole) return num_blocks;

		if (int(backup_blocks.size()) >= num_blocks) return num_blocks;

#if TORRENT_USE_INVARIANT_CHECKS
		verify_pick(backup_blocks, pieces);
#endif
		return num_blocks;
	}

	std::pair<piece_index_t, piece_index_t>
	piece_picker::expand_piece(piece_index_t const piece, int const contiguous_blocks
		, typed_bitfield<piece_index_t> const& have, picker_options_t const options) const
	{
		if (contiguous_blocks == 0) return std::make_pair(piece, next(piece));

		// round to even pieces and expand in order to get the number of
		// contiguous pieces we want
		int const whole_pieces = (contiguous_blocks + m_blocks_per_piece - 1)
			/ m_blocks_per_piece;

		piece_index_t start = piece;
		piece_index_t lower_limit;

		if (options & align_expanded_pieces)
		{
			lower_limit = piece_index_t(static_cast<int>(piece) - (static_cast<int>(piece) % whole_pieces));
		}
		else
		{
			lower_limit = piece_index_t(static_cast<int>(piece) - whole_pieces + 1);
			if (lower_limit < piece_index_t(0)) lower_limit = piece_index_t(0);
		}

		while (start > lower_limit && can_pick(prev(start), have))
			--start;

		TORRENT_ASSERT(start >= piece_index_t(0));
		piece_index_t end = next(piece);
		piece_index_t upper_limit;
		if (options & align_expanded_pieces)
		{
			upper_limit = piece_index_t(static_cast<int>(lower_limit) + whole_pieces);
		}
		else
		{
			upper_limit = piece_index_t(static_cast<int>(start) + whole_pieces);
		}
		if (upper_limit > have.end_index()) upper_limit = have.end_index();
		while (end < upper_limit && can_pick(end, have))
			++end;
		return std::make_pair(start, end);
	}

	bool piece_picker::is_piece_finished(piece_index_t const index) const
	{
		piece_pos const& p = m_piece_map[index];
		if (p.index == piece_pos::we_have_index) return true;

		auto const state = p.download_queue();
		if (state == piece_pos::piece_open)
		{
#if TORRENT_USE_ASSERTS
			for (auto const i : categories())
				TORRENT_ASSERT(find_dl_piece(i, index) == m_downloads[i].end());
#endif
			return false;
		}
		auto const i = find_dl_piece(state, index);
		TORRENT_ASSERT(i != m_downloads[state].end());
		TORRENT_ASSERT(int(i->finished) <= m_blocks_per_piece);
		int const max_blocks = blocks_in_piece(index);
		if (int(i->finished) + int(i->writing) < max_blocks) return false;
		TORRENT_ASSERT(int(i->finished) + int(i->writing) == max_blocks);

#if TORRENT_USE_INVARIANT_CHECKS
		for (auto const& info : blocks_for_piece(*i))
		{
			TORRENT_ASSERT(info.piece_index == index);
			TORRENT_ASSERT(info.state == block_info::state_finished
				|| info.state == block_info::state_writing);
		}
#endif

		return true;
	}

	bool piece_picker::has_piece_passed(piece_index_t const index) const
	{
		TORRENT_ASSERT(index < m_piece_map.end_index());
		TORRENT_ASSERT(index >= piece_index_t(0));

		piece_pos const& p = m_piece_map[index];
		if (p.index == piece_pos::we_have_index) return true;

		auto const state = p.download_queue();
		if (state == piece_pos::piece_open)
		{
#if TORRENT_USE_ASSERTS
			for (auto const i : categories())
				TORRENT_ASSERT(find_dl_piece(i, index) == m_downloads[i].end());
#endif
			return false;
		}
		auto const i = find_dl_piece(state, index);
		TORRENT_ASSERT(i != m_downloads[state].end());
		return bool(i->passed_hash_check);
	}

	std::vector<piece_picker::downloading_piece>::iterator piece_picker::find_dl_piece(
		download_queue_t const queue, piece_index_t const index)
	{
		TORRENT_ASSERT(queue == piece_pos::piece_downloading
			|| queue == piece_pos::piece_full
			|| queue == piece_pos::piece_finished
			|| queue == piece_pos::piece_zero_prio);

		downloading_piece cmp;
		cmp.index = index;
		auto const i = std::lower_bound(
			m_downloads[queue].begin(), m_downloads[queue].end(), cmp);
		if (i == m_downloads[queue].end()) return i;
		if (i->index == index) return i;
		return m_downloads[queue].end();
	}

	std::vector<piece_picker::downloading_piece>::const_iterator piece_picker::find_dl_piece(
		download_queue_t const queue, piece_index_t const index) const
	{
		return const_cast<piece_picker*>(this)->find_dl_piece(queue, index);
	}

	std::vector<piece_picker::downloading_piece>::iterator
	piece_picker::update_piece_state(
		std::vector<piece_picker::downloading_piece>::iterator dp)
	{
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "update_piece_state(" << dp->index << ")" << std::endl;
#endif

		int const num_blocks = blocks_in_piece(dp->index);
		piece_pos& p = m_piece_map[dp->index];
		auto const current_state = p.state();
		TORRENT_ASSERT(current_state != piece_pos::piece_open);
		if (current_state == piece_pos::piece_open)
			return dp;

		// this function is not allowed to create new downloading pieces
		download_queue_t new_state{};
		if (p.filtered())
		{
			new_state = piece_pos::piece_zero_prio;
		}
		else if (dp->requested + dp->finished + dp->writing == 0)
		{
			new_state = piece_pos::piece_open;
		}
		else if (dp->requested + dp->finished + dp->writing < num_blocks)
		{
			new_state = p.reverse()
				? piece_pos::piece_downloading_reverse
				: piece_pos::piece_downloading;
		}
		else if (dp->requested > 0)
		{
			TORRENT_ASSERT(dp->requested + dp->finished + dp->writing == num_blocks);
			new_state = p.reverse()
				? piece_pos::piece_full_reverse
				: piece_pos::piece_full;
		}
		else
		{
			TORRENT_ASSERT(dp->finished + dp->writing == num_blocks);
			new_state = piece_pos::piece_finished;
		}

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << " new_state: " << new_state << " current_state: " << current_state << std::endl;
#endif
		if (new_state == current_state) return dp;
		if (new_state == piece_pos::piece_open) return dp;

		// assert that the iterator that was passed-in in fact lives in
		// the correct list
		TORRENT_ASSERT(find_dl_piece(p.download_queue(), dp->index) == dp);

		// remove the downloading_piece from the list corresponding
		// to the old state
		downloading_piece dp_info = *dp;
		m_downloads[p.download_queue()].erase(dp);

		int const prio = p.priority(this);
		TORRENT_ASSERT(prio < int(m_priority_boundaries.size()) || m_dirty);
		p.state(new_state);
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << " " << dp_info.index << " state (" << current_state << " -> " << new_state << ")" << std::endl;
#endif

		// insert the downloading_piece in the list corresponding to
		// the new state
		downloading_piece cmp;
		cmp.index = dp_info.index;
		auto i = std::lower_bound(m_downloads[p.download_queue()].begin()
			, m_downloads[p.download_queue()].end(), cmp);
		TORRENT_ASSERT(i == m_downloads[p.download_queue()].end()
			|| i->index != dp_info.index);
		i = m_downloads[p.download_queue()].insert(i, dp_info);

		if (!m_dirty)
		{
			if (prio == -1 && p.priority(this) != -1) add(dp_info.index);
			else if (prio != -1) update(prio, p.index);
		}

		return i;
	}

	bool piece_picker::is_requested(piece_block const block) const
	{
		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.end_index());

		auto const state = m_piece_map[block.piece_index].download_queue();
		if (state == piece_pos::piece_open) return false;
		auto const i = find_dl_piece(state, block.piece_index);

		TORRENT_ASSERT(i != m_downloads[state].end());

		auto const info = blocks_for_piece(*i);
		TORRENT_ASSERT(info[block.block_index].piece_index == block.piece_index);
		return info[block.block_index].state == block_info::state_requested;
	}

	bool piece_picker::is_downloaded(piece_block const block) const
	{
		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.end_index());

		piece_pos const& p = m_piece_map[block.piece_index];
		if (p.index == piece_pos::we_have_index) return true;
		auto const state = p.download_queue();
		if (state == piece_pos::piece_open) return false;
		auto const i = find_dl_piece(state, block.piece_index);
		TORRENT_ASSERT(i != m_downloads[state].end());

		auto const info = blocks_for_piece(*i);
		TORRENT_ASSERT(info[block.block_index].piece_index == block.piece_index);
		return info[block.block_index].state == block_info::state_finished
			|| info[block.block_index].state == block_info::state_writing;
	}

	bool piece_picker::is_finished(piece_block const block) const
	{
		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.end_index());

		piece_pos const& p = m_piece_map[block.piece_index];
		if (p.index == piece_pos::we_have_index) return true;
		auto const state = p.download_queue();
		if (state == piece_pos::piece_open) return false;
		auto const i = find_dl_piece(state, block.piece_index);
		TORRENT_ASSERT(i != m_downloads[state].end());

		auto const info = blocks_for_piece(*i);
		TORRENT_ASSERT(info[block.block_index].piece_index == block.piece_index);
		return info[block.block_index].state == block_info::state_finished;
	}

	piece_extent_t piece_picker::extent_for(piece_index_t const p) const
	{
		int const extent_size = max_piece_affinity_extent / m_blocks_per_piece;
		return piece_extent_t{static_cast<int>(p) / extent_size};
	}

	index_range<piece_index_t> piece_picker::extent_for(piece_extent_t const e) const
	{
		int const extent_size = max_piece_affinity_extent / m_blocks_per_piece;
		int const begin = static_cast<int>(e) * extent_size;
		int const end = std::min(begin + extent_size, num_pieces());
		return { piece_index_t{begin}, piece_index_t{end}};
	}

	void piece_picker::record_downloading_piece(piece_index_t const p)
	{
		// if a single piece is large enough, don't bother with the affinity of
		// adjecent pieces.
		if (m_blocks_per_piece >= max_piece_affinity_extent) return;

		piece_extent_t const this_extent = extent_for(p);

		// if the extent is already in the list, nothing to do
		if (std::find(m_recent_extents.begin()
			, m_recent_extents.end(), this_extent) != m_recent_extents.end())
			return;

		download_priority_t const this_prio = piece_priority(p);

		// figure out if it's worth recording this downloading piece
		// if we already have all blocks in this extent, there's no point in
		// adding it
		bool have_all = true;

		for (auto const piece : extent_for(this_extent))
		{
			if (piece == p) continue;

			if (!m_piece_map[piece].have()) have_all = false;

			// if at least one piece in this extent has a different priority than
			// the one we just started downloading, don't create an affinity for
			// adjecent pieces. This probably means the pieces belong to different
			// files, or that some other mechanism determining the priority should
			// take precedence.
			if (piece_priority(piece) != this_prio) return;
		}

		// if we already have all the *other* pieces in this extent, there's no
		// need to inflate their priorities
		if (have_all) return;

		// TODO: should 5 be configurable?
		if (m_recent_extents.size() < 5)
			m_recent_extents.push_back(this_extent);

		// limit the number of extent affinities active at any given time to limit
		// the cost of checking them. Also, don't replace them, commit to
		// finishing them before starting another extent. This is analoguous to
		// limiting the number of partial pieces.
	}

	// options may be 0 or piece_picker::reverse
	// returns false if the block could not be marked as downloading
	bool piece_picker::mark_as_downloading(piece_block const block
		, torrent_peer* peer, picker_options_t const options)
	{
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "mark_as_downloading( {"
			<< block.piece_index << ", " << block.block_index << "} )" << std::endl;
#endif

		TORRENT_ASSERT(peer == nullptr || peer->in_use);
		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.end_index());
		TORRENT_ASSERT(block.block_index < blocks_in_piece(block.piece_index));
		TORRENT_ASSERT(!m_piece_map[block.piece_index].have());

		piece_pos& p = m_piece_map[block.piece_index];
		if (p.download_queue() == piece_pos::piece_open)
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			INVARIANT_CHECK;
#endif
			int const prio = p.priority(this);
			TORRENT_ASSERT(prio < int(m_priority_boundaries.size())
				|| m_dirty);

			p.state((options & reverse)
				? piece_pos::piece_downloading_reverse
				: piece_pos::piece_downloading);

			if (prio >= 0 && !m_dirty) update(prio, p.index);

			// if the piece extent affinity is enabled, (maybe) record downloading a
			// block from this piece to make other peers prefer adjecent pieces
			// if reverse is set, don't encourage other peers to pick nearby
			// pieces, as that's assumed to be low priority.
			// if time critical mode is enabled, we're likely to either download
			// adjacent pieces anyway, but more importantly, we don't want to
			// create artificially higher priority for adjecent pieces if they
			// aren't important or urgent
			if (options & piece_extent_affinity)
				record_downloading_piece(block.piece_index);

			auto const dp = add_download_piece(block.piece_index);
			auto const binfo = mutable_blocks_for_piece(*dp);
			block_info& info = binfo[block.block_index];
			TORRENT_ASSERT(info.piece_index == block.piece_index);
			if (info.state == block_info::state_finished)
				return false;

			info.state = block_info::state_requested;
			info.peer = peer;
			info.num_peers = 1;
#if TORRENT_USE_ASSERTS
			TORRENT_ASSERT(info.peers.count(peer) == 0);
			info.peers.insert(peer);
#endif
			++dp->requested;
			// update_full may move the downloading piece to
			// a different vector, so 'dp' may be invalid after
			// this call
			update_piece_state(dp);
		}
		else
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			INVARIANT_CHECK;
#endif
			auto i = find_dl_piece(p.download_queue(), block.piece_index);
			TORRENT_ASSERT(i != m_downloads[p.download_queue()].end());
			auto const binfo = mutable_blocks_for_piece(*i);
			block_info& info = binfo[block.block_index];
			TORRENT_ASSERT(info.piece_index == block.piece_index);
			if (info.state == block_info::state_writing
				|| info.state == block_info::state_finished)
			{
				return false;
			}

			if ((options & reverse) && !p.reverse() && i->requested == 0)
			{
				// this piece isn't reverse, but there's no other peer
				// downloading from it and we just requested a block from a
				// reverse peer. Make it reverse
				int prio = p.priority(this);
				p.make_reverse();
				if (prio >= 0 && !m_dirty) update(prio, p.index);
			}

			TORRENT_ASSERT(info.state == block_info::state_none
				|| (info.state == block_info::state_requested
					&& (info.num_peers > 0)));
			info.peer = peer;
			if (info.state != block_info::state_requested)
			{
				info.state = block_info::state_requested;
				++i->requested;
				i = update_piece_state(i);
			}
			++info.num_peers;

			// if we make a non-reverse request from a reversed piece,
			// undo the reverse state
			if (!(options & reverse) && p.reverse())
			{
				int prio = p.priority(this);
				// make it non-reverse
				p.unreverse();
				if (prio >= 0 && !m_dirty) update(prio, p.index);
			}

#if TORRENT_USE_ASSERTS
			TORRENT_ASSERT(info.peers.count(peer) == 0);
			info.peers.insert(peer);
#endif
		}
		return true;
	}

	int piece_picker::num_peers(piece_block const block) const
	{
		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.end_index());
		TORRENT_ASSERT(block.block_index < blocks_in_piece(block.piece_index));

		piece_pos const& p = m_piece_map[block.piece_index];
		if (!p.downloading()) return 0;

		auto const i = find_dl_piece(p.download_queue(), block.piece_index);
		TORRENT_ASSERT(i != m_downloads[p.download_queue()].end());

		auto const binfo = blocks_for_piece(*i);
		block_info const& info = binfo[block.block_index];
		TORRENT_ASSERT(&info >= &m_block_info[0]);
		TORRENT_ASSERT(&info < &m_block_info[0] + m_block_info.size());
		TORRENT_ASSERT(info.piece_index == block.piece_index);
		return info.num_peers;
	}

	void piece_picker::get_availability(aux::vector<int, piece_index_t>& avail) const
	{
		TORRENT_ASSERT(m_seeds >= 0);
		INVARIANT_CHECK;

		avail.resize(m_piece_map.size());
		auto j = avail.begin();
		for (auto i = m_piece_map.begin(), end(m_piece_map.end()); i != end; ++i, ++j)
			*j = i->peer_count + m_seeds;
	}

	int piece_picker::get_availability(piece_index_t const piece) const
	{
		return m_piece_map[piece].peer_count + m_seeds;
	}

	bool piece_picker::mark_as_writing(piece_block const block, torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "mark_as_writing( {" << block.piece_index << ", " << block.block_index << "} )" << std::endl;
#endif

		TORRENT_ASSERT(peer == nullptr || static_cast<torrent_peer*>(peer)->in_use);

		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.end_index());
		TORRENT_ASSERT(block.block_index < blocks_in_piece(block.piece_index));
		// this is not valid for web peers
		// TORRENT_ASSERT(peer != 0);

		piece_pos& p = m_piece_map[block.piece_index];
		if (p.downloading() == 0)
		{
			// if we already have this piece, just ignore this
			if (have_piece(block.piece_index)) return false;

			int const prio = p.priority(this);
			TORRENT_ASSERT(prio < int(m_priority_boundaries.size())
				|| m_dirty);
			p.state(piece_pos::piece_downloading);
			// prio being -1 can happen if a block is requested before
			// the piece priority was set to 0
			if (prio >= 0 && !m_dirty) update(prio, p.index);

			auto const dp = add_download_piece(block.piece_index);
			auto const binfo = mutable_blocks_for_piece(*dp);
			block_info& info = binfo[block.block_index];
			TORRENT_ASSERT(&info >= &m_block_info[0]);
			TORRENT_ASSERT(&info < &m_block_info[0] + m_block_info.size());
			TORRENT_ASSERT(info.piece_index == block.piece_index);

			TORRENT_ASSERT(info.state == block_info::state_none);
			if (info.state == block_info::state_finished)
				return false;

			info.state = block_info::state_writing;
			info.peer = peer;
			info.num_peers = 0;
#if TORRENT_USE_ASSERTS
			info.peers.clear();
#endif
			dp->writing = 1;

			update_piece_state(dp);
		}
		else
		{
			auto i = find_dl_piece(p.download_queue(), block.piece_index);
			TORRENT_ASSERT(i != m_downloads[p.download_queue()].end());
			auto const binfo = mutable_blocks_for_piece(*i);
			block_info& info = binfo[block.block_index];

			TORRENT_ASSERT(&info >= &m_block_info[0]);
			TORRENT_ASSERT(&info < &m_block_info[0] + m_block_info.size());
			TORRENT_ASSERT(info.piece_index == block.piece_index);

			info.peer = peer;
			if (info.state == block_info::state_requested) --i->requested;
			if (info.state == block_info::state_writing
				|| info.state == block_info::state_finished)
				return false;

			++i->writing;
			info.state = block_info::state_writing;
			TORRENT_ASSERT(info.piece_index == block.piece_index);

			// all other requests for this block should have been
			// cancelled now
			info.num_peers = 0;
#if TORRENT_USE_ASSERTS
			info.peers.clear();
#endif

			update_piece_state(i);
		}
		return true;
	}

	// calling this function prevents this piece from being picked
	// by the piece picker until the pieces is restored. This allow
	// the disk thread to synchronize and flush any failed state
	// (used for disk write failures and piece hash failures).
	void piece_picker::lock_piece(piece_index_t const piece)
	{
		INVARIANT_CHECK;

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "lock_piece(" << piece << ")" << std::endl;
#endif

		auto const state = m_piece_map[piece].download_queue();
		if (state == piece_pos::piece_open) return;
		auto const i = find_dl_piece(state, piece);
		if (i == m_downloads[state].end()) return;

		TORRENT_ASSERT(i->passed_hash_check == false);
		if (i->passed_hash_check)
		{
			// it's not clear why this would happen,
			// but it seems reasonable to not break the
			// accounting over it.
			i->passed_hash_check = false;
			TORRENT_ASSERT(m_num_passed > 0);
			--m_num_passed;
		}

		// prevent this piece from being picked until it's restored
		i->locked = true;
	}

	// TODO: 2 it would be nice if this could be folded into lock_piece()
	// the main distinction is that this also maintains the m_num_passed
	// counter and the passed_hash_check member
	// Is there ever a case where we call write filed without also locking
	// the piece? Perhaps write_failed() should imply locking it.
	void piece_picker::write_failed(piece_block const block)
	{
		INVARIANT_CHECK;

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "write_failed( {" << block.piece_index << ", " << block.block_index << "} )" << std::endl;
#endif

		auto const state = m_piece_map[block.piece_index].download_queue();
		if (state == piece_pos::piece_open) return;
		auto i = find_dl_piece(state, block.piece_index);
		if (i == m_downloads[state].end()) return;

		auto const binfo = mutable_blocks_for_piece(*i);
		block_info& info = binfo[block.block_index];
		TORRENT_ASSERT(&info >= &m_block_info[0]);
		TORRENT_ASSERT(&info < &m_block_info[0] + m_block_info.size());
		TORRENT_ASSERT(info.piece_index == block.piece_index);
		TORRENT_ASSERT(info.state == block_info::state_writing);
		TORRENT_ASSERT(info.num_peers == 0);

		TORRENT_ASSERT(i->writing > 0);
		TORRENT_ASSERT(info.state == block_info::state_writing);

		if (info.state == block_info::state_finished) return;
		if (info.state == block_info::state_writing) --i->writing;

		info.peer = nullptr;
		info.state = block_info::state_none;
		if (i->passed_hash_check)
		{
			// the hash was good, but we failed to write
			// some of the blocks to disk, which means we
			// can't consider the piece complete
			i->passed_hash_check = false;
			TORRENT_ASSERT(m_num_passed > 0);
			--m_num_passed;
		}

		// prevent this hash job from actually completing
		// this piece, by setting the failure state.
		// the piece is unlocked in the call to restore_piece()
		i->locked = true;

		i = update_piece_state(i);

		if (i->finished + i->writing + i->requested == 0)
		{
			piece_pos& p = m_piece_map[block.piece_index];
			int const prev_priority = p.priority(this);
			erase_download_piece(i);
			int const new_priority = p.priority(this);

			if (m_dirty) return;
			if (new_priority == prev_priority) return;
			if (prev_priority == -1) add(block.piece_index);
			else update(prev_priority, p.index);
		}
	}

	void piece_picker::mark_as_canceled(piece_block const block, torrent_peer* peer)
	{
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "mark_as_cancelled( {"
			<< block.piece_index << ", " << block.block_index
			<< "} )" << std::endl;
#endif

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif
		TORRENT_ASSERT(block.block_index >= 0);
		piece_pos& p = m_piece_map[block.piece_index];

		if (p.download_queue() == piece_pos::piece_open) return;

		auto i = find_dl_piece(p.download_queue(), block.piece_index);

		TORRENT_ASSERT(i != m_downloads[p.download_queue()].end());
		auto const binfo = mutable_blocks_for_piece(*i);
		block_info& info = binfo[block.block_index];

		if (info.state == block_info::state_finished) return;

		TORRENT_ASSERT(info.num_peers == 0);
		info.peer = peer;
		TORRENT_ASSERT(info.state == block_info::state_writing
			|| peer == nullptr);
		if (info.state == block_info::state_writing)
		{
			--i->writing;
			info.state = block_info::state_none;
			// i may be invalid after this call
			i = update_piece_state(i);

			if (i->finished + i->writing + i->requested == 0)
			{
				int const prev_priority = p.priority(this);
				erase_download_piece(i);
				int const new_priority = p.priority(this);

				if (m_dirty) return;
				if (new_priority == prev_priority) return;
				if (prev_priority == -1) add(block.piece_index);
				else update(prev_priority, p.index);
			}
		}
		else
		{
			TORRENT_ASSERT(info.state == block_info::state_none);
		}

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif
	}

	void piece_picker::mark_as_finished(piece_block const block, torrent_peer* peer)
	{
#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "mark_as_finished( {"
			<< block.piece_index << ", " << block.block_index << "} )" << std::endl;
#endif

		TORRENT_ASSERT(peer == nullptr || static_cast<torrent_peer*>(peer)->in_use);
		TORRENT_ASSERT(block.block_index >= 0);

		piece_pos& p = m_piece_map[block.piece_index];

		if (p.download_queue() == piece_pos::piece_open)
		{
			// if we already have this piece, just ignore this
			if (have_piece(block.piece_index)) return;

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			INVARIANT_CHECK;
#endif

			int const prio = p.priority(this);
			TORRENT_ASSERT(prio < int(m_priority_boundaries.size())
				|| m_dirty);
			p.state(piece_pos::piece_downloading);
			if (prio >= 0 && !m_dirty) update(prio, p.index);

			auto const dp = add_download_piece(block.piece_index);
			auto const binfo = mutable_blocks_for_piece(*dp);
			block_info& info = binfo[block.block_index];
			TORRENT_ASSERT(&info >= &m_block_info[0]);
			TORRENT_ASSERT(&info < &m_block_info[0] + m_block_info.size());
			TORRENT_ASSERT(info.piece_index == block.piece_index);
			if (info.state == block_info::state_finished)
				return;
			info.peer = peer;
			TORRENT_ASSERT(info.state == block_info::state_none);
			TORRENT_ASSERT(info.num_peers == 0);
			++dp->finished;
			info.state = block_info::state_finished;
			// dp may be invalid after this call
			update_piece_state(dp);
		}
		else
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			INVARIANT_CHECK;
#endif

			auto i = find_dl_piece(p.download_queue(), block.piece_index);
			TORRENT_ASSERT(i != m_downloads[p.download_queue()].end());
			auto const binfo = mutable_blocks_for_piece(*i);
			block_info& info = binfo[block.block_index];
			TORRENT_ASSERT(info.piece_index == block.piece_index);

			if (info.state == block_info::state_finished) return;

			TORRENT_ASSERT(info.num_peers == 0);

			// peers may have been disconnected in between mark_as_writing
			// and mark_as_finished. When a peer disconnects, its m_peer_info
			// pointer is set to nullptr. If so, preserve the previous peer
			// pointer, instead of forgetting who we downloaded this block from
			if (info.state != block_info::state_writing || peer != nullptr)
				info.peer = peer;

			++i->finished;
			if (info.state == block_info::state_writing)
			{
				TORRENT_ASSERT(i->writing > 0);
				--i->writing;
				info.state = block_info::state_finished;
			}
			else
			{
				TORRENT_ASSERT(info.state == block_info::state_none);
				info.state = block_info::state_finished;
			}

			i = update_piece_state(i);

			if (i->finished < blocks_in_piece(i->index))
				return;

			if (i->passed_hash_check)
				we_have(i->index);
		}

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif
	}

	void piece_picker::mark_as_pad(piece_block block)
	{
		// if this is the first block we mark as a pad, allocate the bitfield
		if (m_pad_blocks.empty())
		{
			m_pad_blocks.resize(int(m_piece_map.size() * m_blocks_per_piece));
		}

		int const block_index = static_cast<int>(block.piece_index) * m_blocks_per_piece + block.block_index;
		TORRENT_ASSERT(block_index < m_pad_blocks.size());
		TORRENT_ASSERT(block_index >= 0);
		TORRENT_ASSERT(m_pad_blocks.get_bit(block_index) == false);

		m_pad_blocks.set_bit(block_index);
		++m_num_pad_blocks;
		TORRENT_ASSERT(m_pad_blocks.count() == m_num_pad_blocks);

		++m_pads_in_piece[block.piece_index];

		piece_pos& p = m_piece_map[block.piece_index];
		if (p.filtered())
		{
			++m_filtered_pad_blocks;
		}

		// if we mark and entire piece as a pad file, we need to also
		// consder that piece as "had" and increment some counters
		int const blocks = blocks_in_piece(block.piece_index);
		if (pad_blocks_in_piece(block.piece_index) == blocks)
		{
			// the entire piece is a pad file
			we_have(block.piece_index);
		}
	}

	int piece_picker::pad_blocks_in_piece(piece_index_t const index) const
	{
		auto const it = m_pads_in_piece.find(index);
		if (it == m_pads_in_piece.end()) return 0;
		return it->second;
	}

	void piece_picker::get_downloaders(std::vector<torrent_peer*>& d
		, piece_index_t const index) const
	{
		d.clear();
		auto const state = m_piece_map[index].download_queue();
		int const num_blocks = blocks_in_piece(index);
		d.reserve(aux::numeric_cast<std::size_t>(num_blocks));

		if (state == piece_pos::piece_open)
		{
			for (int i = 0; i < num_blocks; ++i) d.push_back(nullptr);
			return;
		}

		auto const i = find_dl_piece(state, index);
		TORRENT_ASSERT(i != m_downloads[state].end());
		auto const binfo = blocks_for_piece(*i);
		for (int j = 0; j != num_blocks; ++j)
		{
			TORRENT_ASSERT(binfo[j].peer == nullptr
				|| binfo[j].peer->in_use);
			d.push_back(binfo[j].peer);
		}
	}

	torrent_peer* piece_picker::get_downloader(piece_block const block) const
	{
		auto const state = m_piece_map[block.piece_index].download_queue();
		if (state == piece_pos::piece_open) return nullptr;

		auto const i = find_dl_piece(state, block.piece_index);

		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		auto const binfo = blocks_for_piece(*i);
		TORRENT_ASSERT(binfo[block.block_index].piece_index == block.piece_index);
		if (binfo[block.block_index].state == block_info::state_none)
			return nullptr;

		torrent_peer* peer = binfo[block.block_index].peer;
		TORRENT_ASSERT(peer == nullptr || static_cast<torrent_peer*>(peer)->in_use);
		return peer;
	}

	// this is called when a request is rejected or when
	// a peer disconnects. The piece might be in any state
	void piece_picker::abort_download(piece_block const block, torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "abort_download( {" << block.piece_index << ", " << block.block_index << "} )" << std::endl;
#endif
		TORRENT_ASSERT(peer == nullptr || peer->in_use);

		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);

		auto const state = m_piece_map[block.piece_index].download_queue();
		if (state == piece_pos::piece_open) return;

		auto i = find_dl_piece(state, block.piece_index);
		TORRENT_ASSERT(i != m_downloads[state].end());

		auto const binfo = mutable_blocks_for_piece(*i);
		block_info& info = binfo[block.block_index];
		TORRENT_ASSERT(info.peer == nullptr || info.peer->in_use);
		TORRENT_ASSERT(info.piece_index == block.piece_index);

		TORRENT_ASSERT(info.state != block_info::state_none);

		if (info.state != block_info::state_requested) return;

		piece_pos const& p = m_piece_map[block.piece_index];
		int const prev_prio = p.priority(this);

#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(info.peers.count(peer));
		info.peers.erase(peer);
#endif
		TORRENT_ASSERT(info.num_peers > 0);
		if (info.num_peers > 0) --info.num_peers;
		if (info.peer == peer) info.peer = nullptr;
		TORRENT_ASSERT(info.peers.size() == info.num_peers);

		TORRENT_ASSERT(block.block_index < blocks_in_piece(block.piece_index));

		// if there are other peers, leave the block requested
		if (info.num_peers > 0) return;

		// clear the downloader of this block
		info.peer = nullptr;

		// clear this block as being downloaded
		info.state = block_info::state_none;
		TORRENT_ASSERT(i->requested > 0);
		--i->requested;

		// if there are no other blocks in this piece
		// that's being downloaded, remove it from the list
		if (i->requested + i->finished + i->writing == 0)
		{
			TORRENT_ASSERT(prev_prio < int(m_priority_boundaries.size())
				|| m_dirty);
			erase_download_piece(i);
			int const prio = p.priority(this);
			if (!m_dirty)
			{
				if (prev_prio == -1 && prio >= 0) add(block.piece_index);
				else if (prev_prio >= 0) update(prev_prio, p.index);
			}
			return;
		}

		i = update_piece_state(i);
	}

	piece_count piece_picker::want() const
	{
		bool const want_last = piece_priority(piece_index_t(num_pieces() - 1)) != dont_download;
		piece_count ret{ num_pieces() - m_num_filtered - m_num_have_filtered
			, num_pad_blocks() - m_filtered_pad_blocks - m_have_filtered_pad_blocks
			, want_last };
		TORRENT_ASSERT(!(ret.num_pieces == 0 && ret.last_piece == true));
		TORRENT_ASSERT(!(ret.num_pieces == 0 && ret.pad_blocks > 0));
		TORRENT_ASSERT(!(ret.num_pieces == num_pieces() && ret.last_piece == false));
		return ret;
	}

	piece_count piece_picker::have_want() const
	{
		bool const have_last = have_piece(piece_index_t(num_pieces() - 1));
		bool const want_last = piece_priority(piece_index_t(num_pieces() - 1)) != dont_download;
		piece_count ret{ m_num_have - m_num_have_filtered
			, m_have_pad_blocks - m_have_filtered_pad_blocks
			, have_last && want_last };
		TORRENT_ASSERT(!(ret.num_pieces == 0 && ret.last_piece == true));
		TORRENT_ASSERT(!(ret.num_pieces == 0 && ret.pad_blocks > 0));
		TORRENT_ASSERT(!(ret.num_pieces == num_pieces() && ret.last_piece == false));
		return ret;
	}

	piece_count piece_picker::have() const
	{
		bool const have_last = have_piece(piece_index_t(num_pieces() - 1));
		piece_count ret{ m_num_have
			, m_have_pad_blocks
			, have_last };
		TORRENT_ASSERT(!(ret.num_pieces == 0 && ret.last_piece == true));
		TORRENT_ASSERT(!(ret.num_pieces == 0 && ret.pad_blocks > 0));
		TORRENT_ASSERT(!(ret.num_pieces == num_pieces() && ret.last_piece == false));
		return ret;
	}

	piece_count piece_picker::all_pieces() const
	{
		piece_count ret{ num_pieces()
			, num_pad_blocks()
			, true};
		TORRENT_ASSERT(!(ret.num_pieces == 0 && ret.last_piece == true));
		TORRENT_ASSERT(!(ret.num_pieces == 0 && ret.pad_blocks > 0));
		TORRENT_ASSERT(!(ret.num_pieces == num_pieces() && ret.last_piece == false));
		return ret;
	}

}
