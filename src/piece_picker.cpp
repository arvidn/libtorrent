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

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

#include <boost/bind.hpp>
#include <boost/tuple/tuple.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/piece_picker.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/alert_types.hpp" // for picker_log_alert

#if TORRENT_USE_ASSERTS
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/torrent_peer.hpp"
#endif

#ifdef TORRENT_USE_VALGRIND
#include <valgrind/memcheck.h>
#endif

#include "libtorrent/invariant_check.hpp"

#define TORRENT_PIECE_PICKER_INVARIANT_CHECK INVARIANT_CHECK
//#define TORRENT_NO_EXPENSIVE_INVARIANT_CHECK
//#define TORRENT_PIECE_PICKER_INVARIANT_CHECK

// this is really only useful for debugging unit tests
//#define TORRENT_PICKER_LOG

namespace libtorrent
{

	const piece_block piece_block::invalid((std::numeric_limits<int>::max)(), (std::numeric_limits<int>::max)());

	piece_picker::piece_picker()
		: m_seeds(0)
		, m_num_passed(0)
		, m_priority_boundries(1, int(m_pieces.size()))
		, m_blocks_per_piece(0)
		, m_blocks_in_last_piece(0)
		, m_num_filtered(0)
		, m_num_have_filtered(0)
		, m_cursor(0)
		, m_reverse_cursor(0)
		, m_num_have(0)
		, m_num_pad_files(0)
		, m_dirty(false)
	{
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "new piece_picker" << std::endl;
#endif
#if TORRENT_USE_INVARIANT_CHECKS
		check_invariant();
#endif
	}

	void piece_picker::init(int blocks_per_piece, int blocks_in_last_piece, int total_num_pieces)
	{
		TORRENT_ASSERT(blocks_per_piece > 0);
		TORRENT_ASSERT(total_num_pieces > 0);

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "piece_picker::init()" << std::endl;
#endif
		// allocate the piece_map to cover all pieces
		// and make them invalid (as if we don't have a single piece)
		m_piece_map.resize(total_num_pieces, piece_pos(0, 0));
		m_reverse_cursor = int(m_piece_map.size());
		m_cursor = 0;

		for (int i = 0; i < piece_pos::num_download_categories; ++i)
			m_downloads[i].clear();
		m_block_info.clear();
		m_free_block_infos.clear();

		m_num_filtered += m_num_have_filtered;
		m_num_have_filtered = 0;
		m_num_have = 0;
		m_num_passed = 0;
		m_dirty = true;
		for (std::vector<piece_pos>::iterator i = m_piece_map.begin()
			, end(m_piece_map.end()); i != end; ++i)
		{
			i->peer_count = 0;
			i->download_state = piece_pos::piece_open;
			i->index = 0;
#ifdef TORRENT_DEBUG_REFCOUNTS
			i->have_peers.clear();
#endif
		}

		for (std::vector<piece_pos>::iterator i = m_piece_map.begin() + m_cursor
			, end(m_piece_map.end()); i != end && (i->have() || i->filtered());
			++i, ++m_cursor);
		for (std::vector<piece_pos>::reverse_iterator i = m_piece_map.rend()
			- m_reverse_cursor; m_reverse_cursor > 0 && (i->have() || i->filtered());
			++i, --m_reverse_cursor);

		// the piece index is stored in 20 bits, which limits the allowed
		// number of pieces somewhat
		TORRENT_ASSERT(m_piece_map.size() < piece_pos::we_have_index);

		m_blocks_per_piece = blocks_per_piece;
		m_blocks_in_last_piece = blocks_in_last_piece;
		if (m_blocks_in_last_piece == 0) m_blocks_in_last_piece = blocks_per_piece;

		TORRENT_ASSERT(m_blocks_in_last_piece <= m_blocks_per_piece);
	}

	void piece_picker::piece_info(int index, piece_picker::downloading_piece& st) const
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_piece_map.size()));

		int state = m_piece_map[index].download_queue();
		if (state != piece_pos::piece_open)
		{
			std::vector<downloading_piece>::const_iterator piece = find_dl_piece(state, index);
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
			st.finished = blocks_in_piece(index);
			return;
		}
		st.finished = 0;
	}

	piece_picker::piece_stats_t piece_picker::piece_stats(int index) const
	{
		TORRENT_ASSERT(index >= 0 && index < int(m_piece_map.size()));
		piece_pos const& pp = m_piece_map[index];
		piece_stats_t ret = {
			pp.peer_count + m_seeds,
			pp.priority(this),
			pp.have(),
			pp.downloading()
		};
		return ret;
	}

	piece_picker::dlpiece_iter piece_picker::add_download_piece(int piece)
	{
		TORRENT_ASSERT(piece >= 0);
		TORRENT_ASSERT(piece < int(m_piece_map.size()));
#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

		int block_index;

		if (m_free_block_infos.empty())
		{
			// we need to allocate more space in m_block_info
			block_index = m_block_info.size() / m_blocks_per_piece;
			TORRENT_ASSERT((m_block_info.size() % m_blocks_per_piece) == 0);
			m_block_info.resize(m_block_info.size() + m_blocks_per_piece);
		}
		else
		{
			// there is already free space in m_block_info, grab one range
			block_index = m_free_block_infos.back();
			m_free_block_infos.pop_back();
		}

		// always insert into bucket 0 (piece_downloading)
		downloading_piece ret;
		ret.index = piece;
		int download_state = piece_pos::piece_downloading;
		std::vector<downloading_piece>::iterator downloading_iter
			= std::lower_bound(m_downloads[download_state].begin()
			, m_downloads[download_state].end(), ret);
		TORRENT_ASSERT(downloading_iter == m_downloads[download_state].end()
			|| downloading_iter->index != piece);
		TORRENT_ASSERT(block_index >= 0);
		TORRENT_ASSERT(block_index < (std::numeric_limits<boost::uint16_t>::max)());
		ret.info_idx = block_index;
		TORRENT_ASSERT(int(ret.info_idx) * m_blocks_per_piece
			+ m_blocks_per_piece <= int(m_block_info.size()));

#ifdef TORRENT_USE_VALGRIND
		VALGRIND_CHECK_VALUE_IS_DEFINED(piece);
		VALGRIND_CHECK_VALUE_IS_DEFINED(block_index);
#endif
		block_info* info = blocks_for_piece(ret);
		for (int i = 0; i < m_blocks_per_piece; ++i)
		{
			info[i].num_peers = 0;
			if (m_pad_blocks.count(piece_block(piece, i)))
			{
				info[i].state = block_info::state_finished;
				++ret.finished;
			}
			else
			{
				info[i].state = block_info::state_none;
			}
			info[i].peer = 0;
#ifdef TORRENT_USE_VALGRIND
			VALGRIND_CHECK_VALUE_IS_DEFINED(info[i].peer);
#endif
#if TORRENT_USE_ASSERTS
			info[i].piece_index = piece;
			info[i].peers.clear();
#endif
		}
#ifdef TORRENT_USE_VALGRIND
		VALGRIND_CHECK_VALUE_IS_DEFINED(ret.info_idx);
		VALGRIND_CHECK_VALUE_IS_DEFINED(ret.index);
#endif

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

		int download_state = m_piece_map[i->index].download_queue();
		TORRENT_ASSERT(download_state != piece_pos::piece_open);
		TORRENT_ASSERT(find_dl_piece(download_state, i->index) == i);
#if TORRENT_USE_ASSERTS
		int prev_size = m_downloads[download_state].size();
#endif

		// since we're removing a downloading_piece, we also need to free its
		// blocks that are allocated from the m_block_info array.
		m_free_block_infos.push_back(i->info_idx);

		TORRENT_ASSERT(find_dl_piece(download_state, i->index) == i);
		m_piece_map[i->index].download_state = piece_pos::piece_open;
		m_downloads[download_state].erase(i);

		TORRENT_ASSERT(prev_size == m_downloads[download_state].size() + 1);

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
		for (int k = 0; k < piece_pos::num_download_categories; ++k)
			ret.insert(ret.end(), m_downloads[k].begin(), m_downloads[k].end());
		return ret;
	}

	int piece_picker::get_download_queue_size() const
	{
		int ret = 0;
		for (int k = 0; k < piece_pos::num_download_categories; ++k)
			ret += m_downloads[k].size();
		return ret;
	}

	void piece_picker::get_download_queue_sizes(int* partial
		, int* full, int* finished, int* zero_prio) const
	{
		*partial = m_downloads[piece_pos::piece_downloading].size();
		*full = m_downloads[piece_pos::piece_full].size();
		*finished = m_downloads[piece_pos::piece_finished].size();
		*zero_prio = m_downloads[piece_pos::piece_zero_prio].size();
	}

	piece_picker::block_info* piece_picker::blocks_for_piece(
		downloading_piece const& dp)
	{
		int idx = int(dp.info_idx) * m_blocks_per_piece;
		TORRENT_ASSERT(idx + m_blocks_per_piece <= m_block_info.size());
		return &m_block_info[idx];
	}

	piece_picker::block_info const* piece_picker::blocks_for_piece(
		downloading_piece const& dp) const
	{
		return const_cast<piece_picker*>(this)->blocks_for_piece(dp);
	}

#if TORRENT_USE_INVARIANT_CHECKS

	void piece_picker::check_piece_state() const
	{
#ifndef TORRENT_DISABLE_INVARIANT_CHECKS
		for (int k = 0; k < piece_pos::num_download_categories; ++k)
		{
			if (!m_downloads[k].empty())
			{
				for (std::vector<downloading_piece>::const_iterator i = m_downloads[k].begin();
						i != m_downloads[k].end() - 1; ++i)
				{
					downloading_piece const& dp = *i;
					downloading_piece const& next = *(i + 1);
//					TORRENT_ASSERT(dp.finished + dp.writing >= next.finished + next.writing);
					TORRENT_ASSERT(dp.index < next.index);
					TORRENT_ASSERT(int(dp.info_idx) * m_blocks_per_piece
						+ m_blocks_per_piece <= int(m_block_info.size()));
					block_info const* info = blocks_for_piece(dp);
					for (int j = 0; j < m_blocks_per_piece; ++j)
					{
						if (info[j].peer)
						{
							torrent_peer* p = info[j].peer;
							TORRENT_ASSERT(p->in_use);
							TORRENT_ASSERT(p->connection == NULL
								|| static_cast<peer_connection*>(p->connection)->m_in_use);
						}
					}
				}
			}
		}
#endif
	}

	void piece_picker::verify_pick(std::vector<piece_block> const& picked
		, bitfield const& bits) const
	{
		TORRENT_ASSERT(bits.size() == m_piece_map.size());
		for (std::vector<piece_block>::const_iterator i = picked.begin()
			, end(picked.end()); i != end; ++i)
		{
			TORRENT_ASSERT(i->piece_index >= 0);
			TORRENT_ASSERT(i->piece_index < bits.size());
			TORRENT_ASSERT(bits[i->piece_index]);
			TORRENT_ASSERT(!m_piece_map[i->piece_index].have());
			TORRENT_ASSERT(!m_piece_map[i->piece_index].filtered());
		}
	}

	void piece_picker::verify_priority(int range_start, int range_end, int prio) const
	{
		TORRENT_ASSERT(range_start <= range_end);
		TORRENT_ASSERT(range_end <= int(m_pieces.size()));
		for (std::vector<int>::const_iterator i = m_pieces.begin() + range_start
			, end(m_pieces.begin() + range_end); i != end; ++i)
		{
			int index = *i;
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < int(m_piece_map.size()));
			int p = m_piece_map[index].priority(this);
			TORRENT_ASSERT(p == prio);
		}
	}

#if defined TORRENT_PICKER_LOG
	void piece_picker::print_pieces() const
	{
		int limit = 20;
		std::cerr << "[" << this << "] ";
		if (m_dirty)
		{
			std::cerr << " === dirty ===" << std::endl;
			return;
		}

		for (std::vector<int>::const_iterator i = m_priority_boundries.begin()
			, end(m_priority_boundries.end()); i != end; ++i)
		{
			std::cerr << *i << " ";
		}
		std::cerr << std::endl;
		int index = 0;
		std::cerr << "[" << this << "] ";
		std::vector<int>::const_iterator j = m_priority_boundries.begin();
		for (std::vector<int>::const_iterator i = m_pieces.begin()
			, end(m_pieces.end()); i != end; ++i, ++index)
		{
			if (limit == 0)
			{
				std::cerr << " ...";
				break;
			}
			if (*i == -1) break;
			while (j != m_priority_boundries.end() && *j <= index)
			{
				std::cerr << "| ";
				++j;
			}
			std::cerr << *i << "(" << m_piece_map[*i].index << ") ";
			--limit;
		}
		std::cerr << std::endl;
	}
#endif // TORRENT_PICKER_LOG
#endif // TORRENT_USE_INVARIANT_CHECKS

#if TORRENT_USE_INVARIANT_CHECKS
	void piece_picker::check_peer_invariant(bitfield const& have
		, torrent_peer const* p) const
	{
#ifdef TORRENT_DEBUG_REFCOUNTS
		int num_pieces = have.size();
		for (int i = 0; i < num_pieces; ++i)
		{
			int h = have[i];
			TORRENT_ASSERT(m_piece_map[i].have_peers.count(p) == h);
		}
#else
		TORRENT_UNUSED(have);
		TORRENT_UNUSED(p);
#endif
	}

	void piece_picker::check_invariant(torrent const* t) const
	{
#ifndef TORRENT_DEBUG_REFCOUNTS
#ifdef TORRENT_OPTIMIZE_MEMORY_USAGE
		TORRENT_ASSERT(sizeof(piece_pos) == 4);
#else
		TORRENT_ASSERT(sizeof(piece_pos) == 8);
#endif
#endif
		TORRENT_ASSERT(m_num_have >= 0);
		TORRENT_ASSERT(m_num_have_filtered >= 0);
		TORRENT_ASSERT(m_num_filtered >= 0);
		TORRENT_ASSERT(m_seeds >= 0);

		for (int k = 0; k < piece_pos::num_download_categories; ++k)
		{
			if (!m_downloads[k].empty())
			{
				for (std::vector<downloading_piece>::const_iterator i = m_downloads[k].begin();
						i != m_downloads[k].end() - 1; ++i)
				{
					downloading_piece const& dp = *i;
					downloading_piece const& next = *(i + 1);
//					TORRENT_ASSERT(dp.finished + dp.writing >= next.finished + next.writing);
					TORRENT_ASSERT(dp.index < next.index);
					TORRENT_ASSERT(int(dp.info_idx) * m_blocks_per_piece
						+ m_blocks_per_piece <= int(m_block_info.size()));
#if TORRENT_USE_ASSERTS
					block_info const* info = blocks_for_piece(dp);
					for (int j = 0; j < m_blocks_per_piece; ++j)
					{
						if (!info[j].peer) continue;
						torrent_peer* p = info[j].peer;
						TORRENT_ASSERT(p->in_use);
						TORRENT_ASSERT(p->connection == NULL
							|| static_cast<peer_connection*>(p->connection)->m_in_use);
					}
#endif
				}
			}
		}

		if (t != 0)
			TORRENT_ASSERT(int(m_piece_map.size()) == t->torrent_file().num_pieces());

		for (int j = 0; j < piece_pos::num_download_categories; ++j)
		{
			for (std::vector<downloading_piece>::const_iterator i = m_downloads[j].begin()
				, end(m_downloads[j].end()); i != end; ++i)
			{
				TORRENT_ASSERT(m_piece_map[i->index].download_queue() == j);
				const int num_blocks = blocks_in_piece(i->index);
				int num_requested = 0;
				int num_finished = 0;
				int num_writing = 0;
				int num_open = 0;
				block_info const* info = blocks_for_piece(*i);
				for (int k = 0; k < num_blocks; ++k)
				{
					TORRENT_ASSERT(info[k].piece_index == i->index);
					TORRENT_ASSERT(info[k].peer == 0
						|| info[k].peer->in_use);

					if (info[k].state == block_info::state_finished)
					{
						++num_finished;
						TORRENT_ASSERT(info[k].num_peers == 0);
					}
					else if (info[k].state == block_info::state_requested)
					{
						++num_requested;
						TORRENT_ASSERT(info[k].num_peers > 0);
					}
					else if (info[k].state == block_info::state_writing)
					{
						++num_writing;
						TORRENT_ASSERT(info[k].num_peers == 0);
					}
					else if (info[k].state == block_info::state_none)
					{
						++num_open;
						TORRENT_ASSERT(info[k].num_peers == 0);
					}
				}

				switch(j)
				{
					case piece_pos::piece_downloading:
						TORRENT_ASSERT(!m_piece_map[i->index].filtered());
						TORRENT_ASSERT(num_open > 0);
					break;
					case piece_pos::piece_full:
						TORRENT_ASSERT(!m_piece_map[i->index].filtered());
						TORRENT_ASSERT(num_open == 0);
						// if requested == 0, the piece should be in the finished state
						TORRENT_ASSERT(num_requested > 0);
					break;
					case piece_pos::piece_finished:
						TORRENT_ASSERT(!m_piece_map[i->index].filtered());
						TORRENT_ASSERT(num_open == 0);
						TORRENT_ASSERT(num_requested == 0);
						TORRENT_ASSERT(num_finished + num_writing == num_blocks);
					break;
					case piece_pos::piece_zero_prio:
						TORRENT_ASSERT(m_piece_map[i->index].filtered());
					break;
				}

				TORRENT_ASSERT(num_requested == i->requested);
				TORRENT_ASSERT(num_writing == i->writing);
				TORRENT_ASSERT(num_finished == i->finished);

				if (m_piece_map[i->index].download_queue() == piece_pos::piece_full
					|| m_piece_map[i->index].download_queue() == piece_pos::piece_finished)
					TORRENT_ASSERT(num_finished + num_writing + num_requested == num_blocks);
			}
		}
		int num_pieces = int(m_piece_map.size());
		TORRENT_ASSERT(m_cursor >= 0);
		TORRENT_ASSERT(m_cursor <= num_pieces);
		TORRENT_ASSERT(m_reverse_cursor <= num_pieces);
		TORRENT_ASSERT(m_reverse_cursor >= 0);
		TORRENT_ASSERT(m_reverse_cursor > m_cursor
			|| (m_cursor == num_pieces && m_reverse_cursor == 0));

		if (!m_dirty)
		{
			TORRENT_ASSERT(!m_priority_boundries.empty());
			int prio = 0;
			int start = 0;
			for (std::vector<int>::const_iterator i = m_priority_boundries.begin()
				, end(m_priority_boundries.end()); i != end; ++i)
			{
				verify_priority(start, *i, prio);
				++prio;
				start = *i;
			}
			TORRENT_ASSERT(m_priority_boundries.back() == int(m_pieces.size()));
		}

#ifdef TORRENT_NO_EXPENSIVE_INVARIANT_CHECK
		return;
#endif

		{
			int index = 0;
			for (std::vector<piece_pos>::const_iterator i = m_piece_map.begin()
				, end(m_piece_map.end()); i != end && (i->have() || i->filtered());
				++i, ++index);
			TORRENT_ASSERT(m_cursor == index);
			index = num_pieces;
			if (num_pieces > 0)
			{
				for (std::vector<piece_pos>::reverse_iterator i = m_piece_map.rend()
					- index; index > 0 && (i->have() || i->filtered()); ++i, --index);
				TORRENT_ASSERT(index == num_pieces
					|| m_piece_map[index].have()
					|| m_piece_map[index].filtered());
				TORRENT_ASSERT(m_reverse_cursor == index);
			}
			else
			{
				TORRENT_ASSERT(m_reverse_cursor == 0);
			}
		}

		int num_filtered = 0;
		int num_have_filtered = 0;
		int num_have = 0;
		for (std::vector<piece_pos>::const_iterator i = m_piece_map.begin();
			i != m_piece_map.end(); ++i)
		{
			int index = static_cast<int>(i - m_piece_map.begin());
			piece_pos const& p = *i;

			if (p.filtered())
			{
				if (p.index != piece_pos::we_have_index)
					++num_filtered;
				else
					++num_have_filtered;
			}

#ifdef TORRENT_DEBUG_REFCOUNTS
			TORRENT_ASSERT(p.have_peers.size() == p.peer_count + m_seeds);
#endif
			if (p.index == piece_pos::we_have_index)
				++num_have;

#if 0
			if (t != 0)
			{
				int actual_peer_count = 0;
				for (torrent::const_peer_iterator peer = t->begin();
					peer != t->end(); ++peer)
				{
					if (peer->second->has_piece(index)) actual_peer_count++;
				}

				TORRENT_ASSERT((int)i->peer_count == actual_peer_count);
/*
				int num_downloaders = 0;
				for (std::vector<peer_connection*>::const_iterator peer = t->begin();
					peer != t->end();
					++peer)
				{
					const std::vector<piece_block>& queue = (*peer)->download_queue();
					if (std::find_if(queue.begin(), queue.end(), has_index(index)) == queue.end()) continue;

					++num_downloaders;
				}

				if (i->downloading())
				{
					TORRENT_ASSERT(num_downloaders == 1);
				}
				else
				{
					TORRENT_ASSERT(num_downloaders == 0);
				}
*/
			}
#endif

			if (p.index == piece_pos::we_have_index)
			{
				TORRENT_ASSERT(t == 0 || t->have_piece(index));
				TORRENT_ASSERT(p.downloading() == false);
			}

			if (t != 0)
				TORRENT_ASSERT(!t->have_piece(index));

			int prio = p.priority(this);
#if TORRENT_USE_ASSERTS
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
#endif

			if (!m_dirty)
			{
				TORRENT_ASSERT(prio < int(m_priority_boundries.size())
					|| m_dirty);
				if (prio >= 0)
				{
					TORRENT_ASSERT(p.index < m_pieces.size());
					TORRENT_ASSERT(m_pieces[p.index] == index);
				}
				else
				{
					TORRENT_ASSERT(prio == -1);
					// make sure there's no entry
					// with this index. (there shouldn't
					// be since the priority is -1)
					TORRENT_ASSERT(std::find(m_pieces.begin(), m_pieces.end(), index)
						== m_pieces.end());
				}
			}

			int count_downloading = std::count_if(
				m_downloads[piece_pos::piece_downloading].begin()
				, m_downloads[piece_pos::piece_downloading].end()
				, has_index(index));

			int count_full = std::count_if(
				m_downloads[piece_pos::piece_full].begin()
				, m_downloads[piece_pos::piece_full].end()
				, has_index(index));

			int count_finished = std::count_if(
				m_downloads[piece_pos::piece_finished].begin()
				, m_downloads[piece_pos::piece_finished].end()
				, has_index(index));

			int count_zero = std::count_if(
				m_downloads[piece_pos::piece_zero_prio].begin()
				, m_downloads[piece_pos::piece_zero_prio].end()
				, has_index(index));

			TORRENT_ASSERT(i->download_queue() == piece_pos::piece_open
				|| count_zero + count_downloading + count_full
					+ count_finished == 1);

			switch(i->download_queue())
			{
				case piece_pos::piece_open:
					TORRENT_ASSERT(count_downloading
						+ count_full + count_finished + count_zero == 0);
					break;
				case piece_pos::piece_downloading:
					TORRENT_ASSERT(count_downloading == 1);
					break;
				case piece_pos::piece_full:
					TORRENT_ASSERT(count_full == 1);
					break;
				case piece_pos::piece_finished:
					TORRENT_ASSERT(count_finished == 1);
					break;
				case piece_pos::piece_zero_prio:
					TORRENT_ASSERT(count_zero == 1);
					break;
			};
		}
		TORRENT_ASSERT(num_have == m_num_have);
		TORRENT_ASSERT(num_filtered == m_num_filtered);
		TORRENT_ASSERT(num_have_filtered == m_num_have_filtered);

		if (!m_dirty)
		{
			for (std::vector<int>::const_iterator i = m_pieces.begin()
				, end(m_pieces.end()); i != end; ++i)
			{
				TORRENT_ASSERT(m_piece_map[*i].priority(this) >= 0);
			}
		}
	}
#endif

	std::pair<int, int> piece_picker::distributed_copies() const
	{
		TORRENT_ASSERT(m_seeds >= 0);
		const int num_pieces = m_piece_map.size();

		if (num_pieces == 0) return std::make_pair(1, 0);
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
		TORRENT_ASSERT(integer_part + fraction_part == num_pieces);
		return std::make_pair(min_availability + m_seeds, fraction_part * 1000 / num_pieces);
	}

	void piece_picker::priority_range(int prio, int* start, int* end)
	{
		TORRENT_ASSERT(prio >= 0);
		TORRENT_ASSERT(prio < int(m_priority_boundries.size())
			|| m_dirty);
		if (prio == 0) *start = 0;
		else *start = m_priority_boundries[prio - 1];
		*end = m_priority_boundries[prio];
		TORRENT_ASSERT(*start <= *end);
	}

	void piece_picker::add(int index)
	{
		TORRENT_ASSERT(!m_dirty);
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_piece_map.size()));
		piece_pos& p = m_piece_map[index];
		TORRENT_ASSERT(!p.filtered());
		TORRENT_ASSERT(!p.have());

		int priority = p.priority(this);
		TORRENT_ASSERT(priority >= 0);
		if (priority < 0) return;

		if (int(m_priority_boundries.size()) <= priority)
			m_priority_boundries.resize(priority + 1, m_pieces.size());

		TORRENT_ASSERT(int(m_priority_boundries.size()) >= priority);

		int range_start, range_end;
		priority_range(priority, &range_start, &range_end);
		int new_index;
		if (range_end == range_start) new_index = range_start;
		else new_index = random() % (range_end - range_start + 1) + range_start;

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "add " << index << " (" << priority << ")" << std::endl;
		std::cerr << "[" << this << "] " << "  p: state: " << p.download_state
			<< " peer_count: " << p.peer_count
			<< " prio: " << p.piece_priority
			<< " index: " << p.index << std::endl;
		print_pieces();
#endif
		m_pieces.push_back(-1);

		for (;;)
		{
			TORRENT_ASSERT(new_index < int(m_pieces.size()));
			int temp = m_pieces[new_index];
			m_pieces[new_index] = index;
			m_piece_map[index].index = new_index;
			index = temp;
			do
			{
				temp = m_priority_boundries[priority]++;
				++priority;
			} while (temp == new_index && priority < int(m_priority_boundries.size()));
			new_index = temp;
#ifdef TORRENT_PICKER_LOG
			print_pieces();
			std::cerr << "[" << this << "] " << " index: " << index
				<< " prio: " << priority
				<< " new_index: " << new_index
				<< std::endl;
#endif
			if (priority >= int(m_priority_boundries.size())) break;
			TORRENT_ASSERT(temp >= 0);
		}
		if (index != -1)
		{
			TORRENT_ASSERT(new_index == int(m_pieces.size() - 1));
			m_pieces[new_index] = index;
			m_piece_map[index].index = new_index;

#ifdef TORRENT_PICKER_LOG
			print_pieces();
#endif
		}
	}

	void piece_picker::remove(int priority, int elem_index)
	{
		TORRENT_ASSERT(!m_dirty);
		TORRENT_ASSERT(priority >= 0);
		TORRENT_ASSERT(elem_index < int(m_pieces.size()));
		TORRENT_ASSERT(elem_index >= 0);

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "remove " << m_pieces[elem_index] << " (" << priority << ")" << std::endl;
#endif
		int next_index = elem_index;
		TORRENT_ASSERT(m_piece_map[m_pieces[elem_index]].priority(this) == -1);
		for (;;)
		{
#ifdef TORRENT_PICKER_LOG
			print_pieces();
#endif
			TORRENT_ASSERT(elem_index < int(m_pieces.size()));
			int temp;
			do
			{
				temp = --m_priority_boundries[priority];
				++priority;
			} while (next_index == temp && priority < int(m_priority_boundries.size()));
			if (next_index == temp) break;
			next_index = temp;

			int piece = m_pieces[next_index];
			m_pieces[elem_index] = piece;
			m_piece_map[piece].index = elem_index;
			TORRENT_ASSERT(m_piece_map[piece].priority(this) == priority - 1);
			TORRENT_ASSERT(elem_index < int(m_pieces.size() - 1));
			elem_index = next_index;

			if (priority == int(m_priority_boundries.size()))
				break;
		}
		m_pieces.pop_back();
		TORRENT_ASSERT(next_index == int(m_pieces.size()));
#ifdef TORRENT_PICKER_LOG
		print_pieces();
#endif
	}

	// will update the piece with the given properties (priority, elem_index)
	// to place it at the correct position
	void piece_picker::update(int priority, int elem_index)
	{
		TORRENT_ASSERT(!m_dirty);
		TORRENT_ASSERT(elem_index >= 0);
		TORRENT_ASSERT(elem_index < int(m_piece_map.size()));
		TORRENT_ASSERT(priority >= 0);
		TORRENT_ASSERT(int(m_priority_boundries.size()) > priority);

		// make sure the passed in elem_index actually lives in the specified
		// priority bucket. If it doesn't, it means this piece changed
		// state without updating the corresponding entry in the pieces list
		TORRENT_ASSERT(m_priority_boundries[priority] >= elem_index);
		TORRENT_ASSERT(priority == 0 || m_priority_boundries[priority-1] <= elem_index);
		TORRENT_ASSERT(priority + 1 == m_priority_boundries.size() || m_priority_boundries[priority+1] > elem_index);

		int index = m_pieces[elem_index];
		// update the piece_map
		piece_pos& p = m_piece_map[index];
		TORRENT_ASSERT(int(p.index) == elem_index || p.have());

		int new_priority = p.priority(this);

		if (new_priority == priority) return;

		if (new_priority == -1)
		{
			remove(priority, elem_index);
			return;
		}

		if (int(m_priority_boundries.size()) <= new_priority)
			m_priority_boundries.resize(new_priority + 1, m_pieces.size());

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "update " << index << " (" << priority << "->" << new_priority << ")" << std::endl;
#endif
		if (priority > new_priority)
		{
			int new_index;
			int temp = index;
			for (;;)
			{
#ifdef TORRENT_PICKER_LOG
				print_pieces();
#endif
				TORRENT_ASSERT(priority > 0);
				--priority;
				new_index = m_priority_boundries[priority]++;
				TORRENT_ASSERT(new_index >= 0);
				TORRENT_ASSERT(new_index < int(m_pieces.size()));
				if (temp != m_pieces[new_index])
				{
					temp = m_pieces[new_index];
					m_pieces[elem_index] = temp;
					m_piece_map[temp].index = elem_index;
					TORRENT_ASSERT(elem_index < int(m_pieces.size()));
				}
				elem_index = new_index;
				if (priority == new_priority) break;
			}
#ifdef TORRENT_PICKER_LOG
			print_pieces();
#endif
			m_pieces[elem_index] = index;
			m_piece_map[index].index = elem_index;
			TORRENT_ASSERT(elem_index < int(m_pieces.size()));
#ifdef TORRENT_PICKER_LOG
			print_pieces();
#endif
			shuffle(priority, elem_index);
#ifdef TORRENT_PICKER_LOG
			print_pieces();
#endif
			TORRENT_ASSERT(m_piece_map[index].priority(this) == priority);
		}
		else
		{
			int new_index;
			int temp = index;
			for (;;)
			{
#ifdef TORRENT_PICKER_LOG
				print_pieces();
#endif
				TORRENT_ASSERT(priority >= 0);
				TORRENT_ASSERT(priority < int(m_priority_boundries.size()));
				new_index = --m_priority_boundries[priority];
				TORRENT_ASSERT(new_index >= 0);
				TORRENT_ASSERT(new_index < int(m_pieces.size()));
				if (temp != m_pieces[new_index])
				{
					temp = m_pieces[new_index];
					m_pieces[elem_index] = temp;
					m_piece_map[temp].index = elem_index;
					TORRENT_ASSERT(elem_index < int(m_pieces.size()));
				}
				elem_index = new_index;
				++priority;
				if (priority == new_priority) break;
			}
#ifdef TORRENT_PICKER_LOG
			print_pieces();
#endif
			m_pieces[elem_index] = index;
			m_piece_map[index].index = elem_index;
			TORRENT_ASSERT(elem_index < int(m_pieces.size()));
#ifdef TORRENT_PICKER_LOG
			print_pieces();
#endif
			shuffle(priority, elem_index);
#ifdef TORRENT_PICKER_LOG
			print_pieces();
#endif
			TORRENT_ASSERT(m_piece_map[index].priority(this) == priority);
		}
	}

	void piece_picker::shuffle(int priority, int elem_index)
	{
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "shuffle()" << std::endl;
#endif

		TORRENT_ASSERT(!m_dirty);
		TORRENT_ASSERT(priority >= 0);
		TORRENT_ASSERT(elem_index >= 0);
		TORRENT_ASSERT(elem_index < int(m_pieces.size()));
		TORRENT_ASSERT(m_piece_map[m_pieces[elem_index]].priority(this) == priority);

		int range_start, range_end;
		priority_range(priority, &range_start, &range_end);
		TORRENT_ASSERT(range_start < range_end);
		int other_index = random() % (range_end - range_start) + range_start;

		if (other_index == elem_index) return;

		// swap other_index with elem_index
		piece_pos& p1 = m_piece_map[m_pieces[other_index]];
		piece_pos& p2 = m_piece_map[m_pieces[elem_index]];

		int temp = p1.index;
		p1.index = p2.index;
		p2.index = temp;
		std::swap(m_pieces[other_index], m_pieces[elem_index]);
	}

	void piece_picker::restore_piece(int index)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "restore_piece(" << index << ")" << std::endl;
#endif
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_piece_map.size()));

		int download_state = m_piece_map[index].download_queue();
		TORRENT_ASSERT(download_state != piece_pos::piece_open);
		if (download_state == piece_pos::piece_open) return;

		std::vector<downloading_piece>::iterator i = find_dl_piece(download_state, index);

		TORRENT_ASSERT(i != m_downloads[download_state].end());
		TORRENT_ASSERT(int(i->info_idx) * m_blocks_per_piece
			+ m_blocks_per_piece <= int(m_block_info.size()));

		i->locked = false;

		piece_pos& p = m_piece_map[index];
		int prev_priority = p.priority(this);
		erase_download_piece(i);
		int new_priority = p.priority(this);

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
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
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
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
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

		for (std::vector<piece_pos>::iterator i = m_piece_map.begin()
			, end(m_piece_map.end()); i != end; ++i)
		{
#ifdef TORRENT_DEBUG_REFCOUNTS
			TORRENT_ASSERT(i->have_peers.count(peer) == 1);
			i->have_peers.erase(peer);
#else
			TORRENT_UNUSED(peer);
#endif

			TORRENT_ASSERT(i->peer_count > 0);
			--i->peer_count;
		}

		m_dirty = true;
	}

	void piece_picker::inc_refcount(int index, const torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
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
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

		TORRENT_ASSERT(m_seeds > 0);
		--m_seeds;

		for (std::vector<piece_pos>::iterator i = m_piece_map.begin()
			, end(m_piece_map.end()); i != end; ++i)
		{
			++i->peer_count;
		}

		m_dirty = true;
	}

	void piece_picker::dec_refcount(int index, const torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
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

		int prev_priority = p.priority(this);

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

	void piece_picker::inc_refcount(bitfield const& bitmask, const torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "inc_refcount(bitfield)" << std::endl;
#endif

		// nothing set, nothing to do here
		if (bitmask.none_set()) return;

		if (bitmask.all_set() && bitmask.size() == m_piece_map.size())
		{
			inc_refcount_all(peer);
			return;
		}

		const int size = (std::min)(50, int(bitmask.size()/2));

		// this is an optimization where if just a few
		// pieces end up changing, instead of making
		// the piece list dirty, just update those pieces
		// instead
		int* incremented = TORRENT_ALLOCA(int, size);
		int num_inc = 0;

		if (!m_dirty)
		{
			// first count how many pieces we're updating. If it's few (less than half)
			// we'll just update them one at a time. Othewise we'll just update the counters
			// and mark the picker as dirty, so we'll rebuild it next time we need it.
			// this only matters if we're not already dirty, in which case the fasted
			// thing to do is to just update the counters and be done
			int index = 0;
			for (bitfield::const_iterator i = bitmask.begin()
				, end(bitmask.end()); i != end; ++i, ++index)
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
					int piece = incremented[i];
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

		int index = 0;
		bool updated = false;
		for (bitfield::const_iterator i = bitmask.begin()
			, end(bitmask.end()); i != end; ++i, ++index)
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

	void piece_picker::dec_refcount(bitfield const& bitmask, const torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
		TORRENT_ASSERT(bitmask.size() <= m_piece_map.size());

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "dec_refcount(bitfield)" << std::endl;
#endif

		// nothing set, nothing to do here
		if (bitmask.none_set()) return;

		if (bitmask.all_set() && bitmask.size() == m_piece_map.size())
		{
			dec_refcount_all(peer);
			return;
		}

		const int size = (std::min)(50, int(bitmask.size()/2));

		// this is an optimization where if just a few
		// pieces end up changing, instead of making
		// the piece list dirty, just update those pieces
		// instead
		int* decremented = TORRENT_ALLOCA(int, size);
		int num_dec = 0;

		if (!m_dirty)
		{
			// first count how many pieces we're updating. If it's few (less than half)
			// we'll just update them one at a time. Othewise we'll just update the counters
			// and mark the picker as dirty, so we'll rebuild it next time we need it.
			// this only matters if we're not already dirty, in which case the fasted
			// thing to do is to just update the counters and be done
			int index = 0;
			for (bitfield::const_iterator i = bitmask.begin()
				, end(bitmask.end()); i != end; ++i, ++index)
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
					int piece = decremented[i];
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

		int index = 0;
		bool updated = false;
		for (bitfield::const_iterator i = bitmask.begin()
			, end(bitmask.end()); i != end; ++i, ++index)
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
		if (m_priority_boundries.empty()) m_priority_boundries.resize(1, 0);
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "update_pieces" << std::endl;
#endif
		std::fill(m_priority_boundries.begin(), m_priority_boundries.end(), 0);
		for (std::vector<piece_pos>::iterator i = m_piece_map.begin()
			, end(m_piece_map.end()); i != end; ++i)
		{
			int prio = i->priority(this);
			if (prio == -1) continue;
			if (prio >= int(m_priority_boundries.size()))
				m_priority_boundries.resize(prio + 1, 0);
			i->index = m_priority_boundries[prio];
			++m_priority_boundries[prio];
		}

#ifdef TORRENT_PICKER_LOG
		print_pieces();
#endif

		int index = 0;
		for (std::vector<int>::iterator i = m_priority_boundries.begin()
			, end(m_priority_boundries.end()); i != end; ++i)
		{
			*i += index;
			index = *i;
		}
		m_pieces.resize(index, 0);

#ifdef TORRENT_PICKER_LOG
		print_pieces();
#endif

		index = 0;
		for (std::vector<piece_pos>::iterator i = m_piece_map.begin()
			, end(m_piece_map.end()); i != end; ++i, ++index)
		{
			piece_pos& p = *i;
			int prio = p.priority(this);
			if (prio == -1) continue;
			int new_index = (prio == 0 ? 0 : m_priority_boundries[prio - 1]) + p.index;
			m_pieces[new_index] = index;
		}

		int start = 0;
		for (std::vector<int>::iterator i = m_priority_boundries.begin()
			, end(m_priority_boundries.end()); i != end; ++i)
		{
			if (start == *i) continue;
			std::random_shuffle(&m_pieces[0] + start, &m_pieces[0] + *i, randint);
			start = *i;
		}

		index = 0;
		for (std::vector<int>::const_iterator i = m_pieces.begin()
			, end(m_pieces.end()); i != end; ++i, ++index)
		{
			TORRENT_ASSERT(*i >= 0 && *i < int(m_piece_map.size()));
			m_piece_map[*i].index = index;
		}

		m_dirty = false;
#ifdef TORRENT_PICKER_LOG
		print_pieces();
#endif
	}

	void piece_picker::piece_passed(int index)
	{
		piece_pos& p = m_piece_map[index];
		int download_state = p.download_queue();

		// this is kind of odd. Could this happen?
		TORRENT_ASSERT(download_state != piece_pos::piece_open);
		if (download_state == piece_pos::piece_open) return;

		std::vector<downloading_piece>::iterator i = find_dl_piece(download_state, index);
		TORRENT_ASSERT(i != m_downloads[download_state].end());

		TORRENT_ASSERT(i->locked == false);
		if (i->locked) return;

		TORRENT_ASSERT(!i->passed_hash_check);
		i->passed_hash_check = true;
		++m_num_passed;

		if (i->finished < blocks_in_piece(index)) return;

		we_have(index);
	}

	void piece_picker::we_dont_have(int index)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_piece_map.size()));

		piece_pos& p = m_piece_map[index];

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "piece_picker::we_dont_have("
			<< index << ")" << std::endl;
#endif

		if (!p.have())
		{
			// even though we don't have the piece, it
			// might still have passed hash check
			int download_state = p.download_queue();
			if (download_state == piece_pos::piece_open) return;

			std::vector<downloading_piece>::iterator i
				= find_dl_piece(download_state, index);
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
			++m_num_filtered;
			--m_num_have_filtered;
		}
		else
		{
			// update cursors
			if (index < m_cursor)
				m_cursor = index;
			if (index >= m_reverse_cursor)
				m_reverse_cursor = index + 1;
			if (m_reverse_cursor == m_cursor)
			{
				m_reverse_cursor = 0;
				m_cursor = num_pieces();
			}
		}

		--m_num_have;
		p.set_not_have();

		if (m_dirty) return;
		if (p.priority(this) >= 0) add(index);
	}

	// this is used to indicate that we succesfully have
	// downloaded a piece, and that no further attempts
	// to pick that piece should be made. The piece will
	// be removed from the available piece list.
	void piece_picker::we_have(int index)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_piece_map.size()));

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "piece_picker::we_have("
			<< index << ")" << std::endl;
#endif
		piece_pos& p = m_piece_map[index];
		int info_index = p.index;
		int priority = p.priority(this);
		TORRENT_ASSERT(priority < int(m_priority_boundries.size()) || m_dirty);

		if (p.have()) return;

		int state = p.download_queue();
		if (state != piece_pos::piece_open)
		{
			std::vector<downloading_piece>::iterator i
				= find_dl_piece(state, index);
			TORRENT_ASSERT(i != m_downloads[state].end());
			// decrement num_passed here to compensate
			// for the unconditional increment further down
			if (i->passed_hash_check) --m_num_passed;
			erase_download_piece(i);
		}

		if (p.filtered())
		{
			--m_num_filtered;
			++m_num_have_filtered;
		}
		++m_num_have;
		++m_num_passed;
		p.set_have();
		if (m_cursor == m_reverse_cursor - 1 &&
			m_cursor == index)
		{
			m_cursor = int(m_piece_map.size());
			m_reverse_cursor = 0;
			TORRENT_ASSERT(num_pieces() > 0);
		}
		else if (m_cursor == index)
		{
			++m_cursor;
			for (std::vector<piece_pos>::const_iterator i = m_piece_map.begin() + m_cursor
				, end(m_piece_map.end()); i != end && (i->have() || i->filtered());
				++i, ++m_cursor);
		}
		else if (m_reverse_cursor - 1 == index)
		{
			--m_reverse_cursor;
			TORRENT_ASSERT(m_piece_map[m_reverse_cursor].have()
				|| m_piece_map[m_reverse_cursor].filtered());
			for (std::vector<piece_pos>::const_iterator i = m_piece_map.begin()
				+ m_reverse_cursor - 1; m_reverse_cursor > 0 && (i->have() || i->filtered());
				--i, --m_reverse_cursor);
			TORRENT_ASSERT(m_piece_map[m_reverse_cursor].have()
				|| m_piece_map[m_reverse_cursor].filtered());
		}
		TORRENT_ASSERT(m_reverse_cursor > m_cursor
			|| (m_cursor == num_pieces() && m_reverse_cursor == 0));
		if (priority == -1) return;
		if (m_dirty) return;
		remove(priority, info_index);
		TORRENT_ASSERT(p.priority(this) == -1);
	}

	bool piece_picker::set_piece_priority(int index, int new_piece_priority)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "set_piece_priority(" << index
			<< ", " << new_piece_priority << ")" << std::endl;
#endif

		TORRENT_ASSERT(new_piece_priority >= 0);
		TORRENT_ASSERT(new_piece_priority < priority_levels);
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_piece_map.size()));

		piece_pos& p = m_piece_map[index];

		// if the priority isn't changed, don't do anything
		if (new_piece_priority == int(p.piece_priority)) return false;

		int prev_priority = p.priority(this);
		TORRENT_ASSERT(m_dirty || prev_priority < int(m_priority_boundries.size()));

		bool ret = false;
		if (new_piece_priority == piece_pos::filter_priority
			&& p.piece_priority != piece_pos::filter_priority)
		{
			// the piece just got filtered
			if (p.have())
			{
				++m_num_have_filtered;
			}
			else
			{
				++m_num_filtered;

				// update m_cursor
				if (m_cursor == m_reverse_cursor - 1 && m_cursor == index)
				{
					m_cursor = int(m_piece_map.size());
					m_reverse_cursor = 0;
				}
				else if (m_cursor == index)
				{
					++m_cursor;
					while (m_cursor < int(m_piece_map.size())
						&& (m_piece_map[m_cursor].have()
						|| m_piece_map[m_cursor].filtered()))
						++m_cursor;
				}
				else if (m_reverse_cursor == index + 1)
				{
					--m_reverse_cursor;
					while (m_reverse_cursor > 0
						&& (m_piece_map[m_reverse_cursor-1].have()
						|| m_piece_map[m_reverse_cursor-1].filtered()))
						--m_reverse_cursor;
				}
			}
			ret = true;
		}
		else if (new_piece_priority != piece_pos::filter_priority
			&& p.piece_priority == piece_pos::filter_priority)
		{
			// the piece just got unfiltered
			if (p.have())
			{
				--m_num_have_filtered;
			}
			else
			{
				--m_num_filtered;
				// update cursors
				if (index < m_cursor)
					m_cursor = index;
				if (index >= m_reverse_cursor)
					m_reverse_cursor = index + 1;
				if (m_reverse_cursor == m_cursor)
				{
					m_reverse_cursor = 0;
					m_cursor = num_pieces();
				}
			}
			ret = true;
		}
		TORRENT_ASSERT(m_num_filtered >= 0);
		TORRENT_ASSERT(m_num_have_filtered >= 0);

		p.piece_priority = new_piece_priority;
		int new_priority = p.priority(this);

		if (prev_priority != new_priority && !m_dirty)
		{
			if (prev_priority == -1)
			{
				add(index);
			}
			else
			{
				update(prev_priority, p.index);
			}
		}

		if (p.downloading())
		{
			std::vector<downloading_piece>::iterator i = find_dl_piece(
				p.download_queue(), index);
			if (i != m_downloads[p.download_queue()].end())
				update_piece_state(i);
		}

		return ret;
	}

	int piece_picker::piece_priority(int index) const
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_piece_map.size()));

		return m_piece_map[index].piece_priority;
	}

	void piece_picker::piece_priorities(std::vector<int>& pieces) const
	{
		pieces.resize(m_piece_map.size());
		std::vector<int>::iterator j = pieces.begin();
		for (std::vector<piece_pos>::const_iterator i = m_piece_map.begin(),
			end(m_piece_map.end()); i != end; ++i, ++j)
		{
			*j = i->piece_priority;
		}
	}

	// ============ start deprecation ==============

	void piece_picker::filtered_pieces(std::vector<bool>& mask) const
	{
		mask.resize(m_piece_map.size());
		std::vector<bool>::iterator j = mask.begin();
		for (std::vector<piece_pos>::const_iterator i = m_piece_map.begin(),
			end(m_piece_map.end()); i != end; ++i, ++j)
		{
			*j = i->filtered();
		}
	}

	// ============ end deprecation ==============

	namespace
	{
		int append_blocks(std::vector<piece_block>& dst, std::vector<piece_block>& src
			, int const num_blocks)
		{
			if (src.empty()) return num_blocks;
			int const to_copy = (std::min)(int(src.size()), num_blocks);

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

	// the return value is a combination of picker_log_alert::picker_flags_t,
	// indicating which path throught the picker we took to arrive at the
	// returned block picks.
	boost::uint32_t piece_picker::pick_pieces(bitfield const& pieces
		, std::vector<piece_block>& interesting_blocks, int num_blocks
		, int prefer_contiguous_blocks, torrent_peer* peer
		, int options, std::vector<int> const& suggested_pieces
		, int num_peers
		, counters& pc
		) const
	{
		TORRENT_ASSERT(peer == 0 || peer->in_use);
		boost::uint32_t ret = 0;

		// prevent the number of partial pieces to grow indefinitely
		// make this scale by the number of peers we have. For large
		// scale clients, we would have more peers, and allow a higher
		// threshold for the number of partials
		// deduct pad files because they case partial pieces which are OK
		// the second condition is to make sure we cap the number of partial
		// _bytes_. The larger the pieces are, the fewer partial pieces we want.
		// 2048 corresponds to 32 MiB
		// TODO: 2 make the 2048 limit configurable
		const int num_partials = int(m_downloads[piece_pos::piece_downloading].size())
			- m_num_pad_files;
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
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
		TORRENT_ASSERT(num_blocks > 0);
		TORRENT_ASSERT(pieces.size() == m_piece_map.size());

		TORRENT_ASSERT(!m_priority_boundries.empty() || m_dirty);

		// this will be filled with blocks that we should not request
		// unless we can't find num_blocks among the other ones.
		std::vector<piece_block> backup_blocks;
		std::vector<piece_block> backup_blocks2;
		const std::vector<int> empty_vector;

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
			downloading_piece const** ordered_partials = TORRENT_ALLOCA(
				downloading_piece const*, m_downloads[piece_pos::piece_downloading].size());
			int num_ordered_partials = 0;

			// now, copy over the pointers. We also apply a filter here to not
			// include ineligible pieces in certain modes. For instance, a piece
			// that the current peer doesn't have is not included.
			for (std::vector<downloading_piece>::const_iterator i
				= m_downloads[piece_pos::piece_downloading].begin()
				, end(m_downloads[piece_pos::piece_downloading].end()); i != end; ++i)
			{
				pc.inc_stats_counter(counters::piece_picker_partial_loops);

				// in time critical mode, only pick high priority pieces
				if ((options & time_critical_mode)
					&& piece_priority(i->index) != priority_levels - 1)
					continue;

				if (!is_piece_free(i->index, pieces)) continue;

				TORRENT_ASSERT(m_piece_map[i->index].download_queue()
					== piece_pos::piece_downloading);

				ordered_partials[num_ordered_partials++] = &*i;
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
				std::sort(ordered_partials, ordered_partials + num_ordered_partials
					, boost::bind(&piece_picker::partial_compare_rarest_first, this
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
			for (std::vector<int>::const_iterator i = suggested_pieces.begin();
				i != suggested_pieces.end(); ++i)
			{
				// in time critical mode, only pick high priority pieces
				if ((options & time_critical_mode)
					&& piece_priority(*i) != priority_levels - 1)
					continue;

				pc.inc_stats_counter(counters::piece_picker_suggest_loops);
				if (!is_piece_free(*i, pieces)) continue;

				ret |= picker_log_alert::suggested_pieces;

				num_blocks = add_blocks(*i, pieces
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

			for (std::vector<int>::const_iterator i = m_pieces.begin();
				i != m_pieces.end() && piece_priority(*i) == priority_levels - 1; ++i)
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
			if ((options & time_critical_mode) == 0)
			{
				if (options & reverse)
				{
					for (int i = m_reverse_cursor - 1; i >= m_cursor; --i)
					{
						pc.inc_stats_counter(counters::piece_picker_sequential_loops);
						if (!is_piece_free(i, pieces)) continue;
						// we've already added high priority pieces
						if (piece_priority(i) == priority_levels - 1) continue;

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
					for (int i = m_cursor; i < m_reverse_cursor; ++i)
					{
						pc.inc_stats_counter(counters::piece_picker_sequential_loops);
						if (!is_piece_free(i, pieces)) continue;
						// we've already added high priority pieces
						if (piece_priority(i) == priority_levels - 1) continue;

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
			if ((options & reverse) && (options & time_critical_mode) == 0)
			{
				for (int i = m_priority_boundries.size() - 1; i >= 0; --i)
				{
					int start = (i == 0) ? 0 : m_priority_boundries[i - 1];
					int end = m_priority_boundries[i];
					for (int p = end - 1; p >= start; --p)
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
				for (std::vector<int>::const_iterator i = m_pieces.begin();
					i != m_pieces.end(); ++i)
				{
					pc.inc_stats_counter(counters::piece_picker_rare_loops);

					// in time critical mode, only pick high priority pieces
					// it's safe to break here because in this mode we
					// pick pieces in priority order. Once we hit a lower priority
					// piece, we won't encounter any more high priority ones
					if ((options & time_critical_mode)
						&& piece_priority(*i) != priority_levels - 1)
						break;

					if (!is_piece_free(*i, pieces)) continue;

					ret |= picker_log_alert::rarest_first;

					num_blocks = add_blocks(*i, pieces
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
			for (std::vector<int>::const_iterator i = m_pieces.begin();
				i != m_pieces.end() && piece_priority(*i) == priority_levels - 1; ++i)
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
			int start_piece = random() % m_piece_map.size();

			int piece = start_piece;
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
					if (piece == int(m_piece_map.size())) piece = 0;
					// could not find any more pieces
					if (piece == start_piece) { goto get_out; }
				}

				if (prefer_contiguous_blocks > 1 && !m_piece_map[piece].downloading())
				{
					TORRENT_ASSERT(can_pick(piece, pieces));
					TORRENT_ASSERT(m_piece_map[piece].downloading() == false);

					int start, end;
					boost::tie(start, end) = expand_piece(piece
						, prefer_contiguous_blocks, pieces, options);
					TORRENT_ASSERT(end - start > 0);
					for (int k = start; k < end; ++k)
					{
						TORRENT_ASSERT(m_piece_map[k].downloading() == false);
						TORRENT_ASSERT(m_piece_map[k].priority(this) >= 0);
						const int num_blocks_in_piece = blocks_in_piece(k);

						ret |= picker_log_alert::random_pieces;

						for (int j = 0; j < num_blocks_in_piece; ++j)
						{
							pc.inc_stats_counter(counters::piece_picker_rand_loops);
							TORRENT_ASSERT(is_piece_free(k, pieces));
							interesting_blocks.push_back(piece_block(k, j));
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

				if (piece == int(m_piece_map.size())) piece = 0;
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

		int partials_size = (std::min)(200, int(
				m_downloads[piece_pos::piece_downloading].size()
			+ m_downloads[piece_pos::piece_full].size()));
		if (partials_size == 0) return ret;

		downloading_piece const** partials
			= TORRENT_ALLOCA(downloading_piece const*, partials_size);
		int c = 0;

#if TORRENT_USE_ASSERTS && !defined TORRENT_DISABLE_INVARIANT_CHECKS
		// if we get here, we're about to pick a busy block. First, make sure
		// we really exhausted the available blocks
		for (std::vector<downloading_piece>::const_iterator i
			= m_downloads[piece_pos::piece_downloading].begin()
			, end(m_downloads[piece_pos::piece_downloading].end()); i != end; ++i)
		{
			downloading_piece const& dp = *i;

			if ((options & time_critical_mode)
				&& piece_priority(dp.index) != priority_levels - 1)
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

		for (std::vector<downloading_piece>::const_iterator i
			= m_downloads[piece_pos::piece_full].begin()
			, end(m_downloads[piece_pos::piece_full].end());
			i != end; ++i)
		{
			if (c == partials_size) break;

			downloading_piece const& dp = *i;
			TORRENT_ASSERT(dp.requested > 0);
			// this peer doesn't have this piece, try again
			if (!pieces[dp.index]) continue;
			// don't pick pieces with priority 0
			TORRENT_ASSERT(piece_priority(dp.index) > 0);

			if ((options & time_critical_mode)
				&& piece_priority(dp.index) != priority_levels - 1)
				continue;

			partials[c++] = &dp;
		}

		partials_size = c;
		while (partials_size > 0)
		{
			pc.inc_stats_counter(counters::piece_picker_busy_loops);
			int piece = random() % partials_size;
			downloading_piece const* dp = partials[piece];
			TORRENT_ASSERT(pieces[dp->index]);
			TORRENT_ASSERT(piece_priority(dp->index) > 0);
			// fill in with blocks requested from other peers
			// as backups
			const int num_blocks_in_piece = blocks_in_piece(dp->index);
			TORRENT_ASSERT(dp->requested > 0);
			block_info const* binfo = blocks_for_piece(*dp);
			for (int j = 0; j < num_blocks_in_piece; ++j)
			{
				block_info const& info = binfo[j];
				TORRENT_ASSERT(info.peer == 0
					|| static_cast<torrent_peer*>(info.peer)->in_use);
				TORRENT_ASSERT(info.piece_index == dp->index);
				if (info.state != block_info::state_requested
					|| info.peer == peer)
					continue;
				temp.push_back(piece_block(dp->index, j));
			}
			// are we done?
			if (!temp.empty())
			{
				ret |= picker_log_alert::end_game;

				interesting_blocks.push_back(temp[random() % temp.size()]);
				--num_blocks;
				break;
			}

			// the piece we picked only had blocks outstanding requested
			// by ourself. Remove it and pick another one.
			partials[piece] = partials[partials_size-1];
			--partials_size;
		}

#if defined TORRENT_DEBUG && !defined TORRENT_DISABLE_INVARIANT_CHECKS
// make sure that we at this point have added requests to all unrequested blocks
// in all downloading pieces

		for (std::vector<downloading_piece>::const_iterator i
			= m_downloads[piece_pos::piece_downloading].begin()
			, end(m_downloads[piece_pos::piece_downloading].end()); i != end; ++i)
		{
			if (!pieces[i->index]) continue;
			if (piece_priority(i->index) == 0) continue;
			if (i->locked) continue;

			if ((options & time_critical_mode)
				&& piece_priority(i->index) != priority_levels - 1)
				continue;

			const int num_blocks_in_piece = blocks_in_piece(i->index);
			block_info const* binfo = blocks_for_piece(*i);
			for (int j = 0; j < num_blocks_in_piece; ++j)
			{
				block_info const& info = binfo[j];
				TORRENT_ASSERT(info.piece_index == i->index);
				if (info.state != block_info::state_none) continue;
				std::vector<piece_block>::iterator k = std::find(
					interesting_blocks.begin(), interesting_blocks.end()
					, piece_block(i->index, j));
				if (k != interesting_blocks.end()) continue;

				fprintf(stderr, "interesting blocks:\n");
				for (k = interesting_blocks.begin(); k != interesting_blocks.end(); ++k)
					fprintf(stderr, "(%d, %d)", k->piece_index, k->block_index);
				fprintf(stderr, "\nnum_blocks: %d\n", num_blocks);

				for (std::vector<downloading_piece>::const_iterator l
					= m_downloads[piece_pos::piece_downloading].begin()
					, end2(m_downloads[piece_pos::piece_downloading].end());
					l != end2; ++l)
				{
					block_info const* binfo2 = blocks_for_piece(*l);
					fprintf(stderr, "%d : ", l->index);
					const int cnt = blocks_in_piece(l->index);
					for (int m = 0; m < cnt; ++m)
						fprintf(stderr, "%d", binfo2[m].state);
					fprintf(stderr, "\n");
				}

				TORRENT_ASSERT(false);
			}
		}

		if (interesting_blocks.empty())
		{
			for (int i = 0; i < num_pieces(); ++i)
			{
				if (!pieces[i]) continue;
				if (m_piece_map[i].priority(this) <= 0) continue;
				if (have_piece(i)) continue;

				int download_state = m_piece_map[i].download_queue();
				if (download_state == piece_pos::piece_open) continue;
				std::vector<downloading_piece>::const_iterator k
					= find_dl_piece(download_state, i);

				TORRENT_ASSERT(k != m_downloads[download_state].end());
				if (k == m_downloads[download_state].end()) continue;
			}
		}
#endif // TORRENT_DEBUG && !defined TORRENT_DISABLE_INVARIANT_CHECKS
		return ret;
	}

	// have piece means that the piece passed hash check
	// AND has been successfully written to disk
	bool piece_picker::have_piece(int index) const
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_piece_map.size()));
		piece_pos const& p = m_piece_map[index];
		return p.index == piece_pos::we_have_index;
	}

	int piece_picker::blocks_in_piece(int index) const
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_piece_map.size()) || m_piece_map.empty());
		if (index + 1 == int(m_piece_map.size()))
			return m_blocks_in_last_piece;
		else
			return m_blocks_per_piece;
	}

	bool piece_picker::is_piece_free(int piece, bitfield const& bitmask) const
	{
		TORRENT_ASSERT(piece >= 0 && piece < int(m_piece_map.size()));
		return bitmask[piece]
			&& !m_piece_map[piece].have()
			&& !m_piece_map[piece].filtered();
	}

	bool piece_picker::can_pick(int piece, bitfield const& bitmask) const
	{
		TORRENT_ASSERT(piece >= 0 && piece < int(m_piece_map.size()));
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
		for (std::vector<block_info>::iterator i = m_block_info.begin()
			, end(m_block_info.end()); i != end; ++i)
		{
			TORRENT_ASSERT(i->peer == 0 || static_cast<torrent_peer*>(i->peer)->in_use);
		}
	}
#endif

	void piece_picker::clear_peer(torrent_peer* peer)
	{
		for (std::vector<block_info>::iterator i = m_block_info.begin()
			, end(m_block_info.end()); i != end; ++i)
		{
			if (i->peer == peer) i->peer = 0;
		}
	}

	// the first bool is true if this is the only peer that has requested and downloaded
	// blocks from this piece.
	// the second bool is true if this is the only active peer that is requesting
	// and downloading blocks from this piece. Active means having a connection.
	// TODO: 2 the first_block returned here is the largest free range, not
	// the first-fit range, which would be better
	boost::tuple<bool, bool, int, int> piece_picker::requested_from(
		piece_picker::downloading_piece const& p
		, int num_blocks_in_piece, torrent_peer* peer) const
	{
		bool exclusive = true;
		bool exclusive_active = true;
		int contiguous_blocks = 0;
		int max_contiguous = 0;
		int first_block = 0;
		block_info const* binfo = blocks_for_piece(p);
		for (int j = 0; j < num_blocks_in_piece; ++j)
		{
			piece_picker::block_info const& info = binfo[j];
			TORRENT_ASSERT(info.peer == 0 || static_cast<torrent_peer*>(info.peer)->in_use);
			TORRENT_ASSERT(info.piece_index == p.index);
			if (info.state == piece_picker::block_info::state_none)
			{
				++contiguous_blocks;
				continue;
			}
			if (contiguous_blocks > max_contiguous)
			{
				max_contiguous = contiguous_blocks;
				first_block = j - contiguous_blocks;
			}
			contiguous_blocks = 0;
			if (info.peer != peer)
			{
				exclusive = false;
				if (info.state == piece_picker::block_info::state_requested
					&& info.peer != 0)
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
		return boost::make_tuple(exclusive, exclusive_active, max_contiguous
			, first_block);
	}

	int piece_picker::add_blocks(int piece
		, bitfield const& pieces
		, std::vector<piece_block>& interesting_blocks
		, std::vector<piece_block>& backup_blocks
		, std::vector<piece_block>& backup_blocks2
		, int num_blocks, int prefer_contiguous_blocks
		, torrent_peer* peer, std::vector<int> const& ignore
		, int options) const
	{
		TORRENT_ASSERT(piece >= 0);
		TORRENT_ASSERT(piece < int(m_piece_map.size()));
		TORRENT_ASSERT(is_piece_free(piece, pieces));

//		std::cout << "add_blocks(" << piece << ")" << std::endl;
//		std::cout << "  num_blocks " << num_blocks << std::endl;

		// ignore pieces found in the ignore list
		if (std::find(ignore.begin(), ignore.end(), piece) != ignore.end()) return num_blocks;

		if (m_piece_map[piece].download_queue() != piece_pos::piece_open
			&& m_piece_map[piece].download_queue() != piece_pos::piece_downloading)
			return num_blocks;

		TORRENT_ASSERT(m_piece_map[piece].priority(this) >= 0);
		int state = m_piece_map[piece].download_queue();
		if (state == piece_pos::piece_downloading)
		{
			// if we're prioritizing partials, we've already
			// looked through the downloading pieces
			if (options & prioritize_partials) return num_blocks;

			std::vector<downloading_piece>::const_iterator i = find_dl_piece(
				piece_pos::piece_downloading, piece);
			TORRENT_ASSERT(i != m_downloads[state].end());

//			std::cout << "add_blocks_downloading(" << piece << ")" << std::endl;

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
				interesting_blocks.push_back(piece_block(piece, j));
			num_blocks -= num_blocks_in_piece;
		}
		else
		{
			int start, end;
			boost::tie(start, end) = expand_piece(piece, prefer_contiguous_blocks
				, pieces, options);
			for (int k = start; k < end; ++k)
			{
				TORRENT_ASSERT(m_piece_map[k].priority(this) > 0);
				num_blocks_in_piece = blocks_in_piece(k);
				TORRENT_ASSERT(is_piece_free(k, pieces));
				for (int j = 0; j < num_blocks_in_piece; ++j)
				{
					interesting_blocks.push_back(piece_block(k, j));
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
		return (std::max)(num_blocks, 0);
	}

	int piece_picker::add_blocks_downloading(downloading_piece const& dp
		, bitfield const& pieces
		, std::vector<piece_block>& interesting_blocks
		, std::vector<piece_block>& backup_blocks
		, std::vector<piece_block>& backup_blocks2
		, int num_blocks, int prefer_contiguous_blocks
		, torrent_peer* peer, int options) const
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
		boost::tie(exclusive, exclusive_active, contiguous_blocks, first_block)
			= requested_from(dp, num_blocks_in_piece, peer);

		// no need in picking from the largest contiguous block run unless
		// we're interested in it. In fact, we really want the opposite.
		if (prefer_contiguous_blocks == 0) first_block = 0;

		// peers on parole are only allowed to pick blocks from
		// pieces that only they have downloaded/requested from
		if ((options & on_parole) && !exclusive) return num_blocks;

		block_info const* binfo = blocks_for_piece(dp);

		// we prefer whole blocks, but there are other peers
		// downloading from this piece and there aren't enough contiguous blocks
		// to pick, add it as backups.
		// if we're on parole, don't let the contiguous blocks stop us, we want
		// to primarily request from a piece all by ourselves.
		if (prefer_contiguous_blocks > contiguous_blocks
			&& !exclusive_active
			&& (options & on_parole) == 0)
		{
			if (int(backup_blocks2.size()) >= num_blocks)
				return num_blocks;

			for (int j = 0; j < num_blocks_in_piece; ++j)
			{
				// ignore completed blocks and already requested blocks
				int block_idx = (j + first_block) % num_blocks_in_piece;
				block_info const& info = binfo[block_idx];
				TORRENT_ASSERT(info.piece_index == dp.index);
				if (info.state != block_info::state_none) continue;
				backup_blocks2.push_back(piece_block(dp.index, block_idx));
			}
			return num_blocks;
		}

		for (int j = 0; j < num_blocks_in_piece; ++j)
		{
			// ignore completed blocks and already requested blocks
			int block_idx = (j + first_block) % num_blocks_in_piece;
			block_info const& info = binfo[block_idx];
			TORRENT_ASSERT(info.piece_index == dp.index);
			if (info.state != block_info::state_none) continue;

			// this block is interesting (we don't have it yet).
			interesting_blocks.push_back(piece_block(dp.index, block_idx));
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

	std::pair<int, int> piece_picker::expand_piece(int piece, int contiguous_blocks
		, bitfield const& have, int options) const
	{
		if (contiguous_blocks == 0) return std::make_pair(piece, piece + 1);

		// round to even pieces and expand in order to get the number of
		// contiguous pieces we want
		int whole_pieces = (contiguous_blocks + m_blocks_per_piece - 1)
			/ m_blocks_per_piece;

		int start = piece;
		int lower_limit;

		if (options & align_expanded_pieces)
		{
			lower_limit = piece - (piece % whole_pieces);
		}
		else
		{
			lower_limit = piece - whole_pieces + 1;
			if (lower_limit < 0) lower_limit = 0;
		}

		while (start - 1 >= lower_limit
			&& can_pick(start - 1, have))
			--start;

		TORRENT_ASSERT(start >= 0);
		int end = piece + 1;
		int upper_limit ;
		if (options & align_expanded_pieces)
		{
			upper_limit = lower_limit + whole_pieces;
		}
		else
		{
			upper_limit = start + whole_pieces;
		}
		if (upper_limit > int(m_piece_map.size())) upper_limit = int(m_piece_map.size());
		while (end < upper_limit
			&& can_pick(end, have))
			++end;
		return std::make_pair(start, end);
	}

	bool piece_picker::is_piece_finished(int index) const
	{
		TORRENT_ASSERT(index < int(m_piece_map.size()));
		TORRENT_ASSERT(index >= 0);

		piece_pos const& p = m_piece_map[index];
		if (p.index == piece_pos::we_have_index) return true;

		int state = p.download_queue();
		if (state == piece_pos::piece_open)
		{
			for (int i = 0; i < piece_pos::num_download_categories; ++i)
				TORRENT_ASSERT(find_dl_piece(i, index) == m_downloads[i].end());
			return false;
		}
		std::vector<downloading_piece>::const_iterator i = find_dl_piece(state, index);
		TORRENT_ASSERT(i != m_downloads[state].end());
		TORRENT_ASSERT(int(i->finished) <= m_blocks_per_piece);
		int max_blocks = blocks_in_piece(index);
		if (int(i->finished) + int(i->writing) < max_blocks) return false;
		TORRENT_ASSERT(int(i->finished) + int(i->writing) == max_blocks);

#if TORRENT_USE_ASSERTS && !defined TORRENT_DISABLE_INVARIANT_CHECKS
		block_info const* info = blocks_for_piece(*i);
		for (int k = 0; k < max_blocks; ++k)
		{
			TORRENT_ASSERT(info[k].piece_index == index);
			TORRENT_ASSERT(info[k].state == block_info::state_finished
				|| info[k].state == block_info::state_writing);
		}
#endif

		return true;
	}

	bool piece_picker::has_piece_passed(int index) const
	{
		TORRENT_ASSERT(index < int(m_piece_map.size()));
		TORRENT_ASSERT(index >= 0);

		piece_pos const& p = m_piece_map[index];
		if (p.index == piece_pos::we_have_index) return true;

		int state = p.download_queue();
		if (state == piece_pos::piece_open)
		{
			for (int i = 0; i < piece_pos::num_download_categories; ++i)
				TORRENT_ASSERT(find_dl_piece(i, index) == m_downloads[i].end());
			return false;
		}
		std::vector<downloading_piece>::const_iterator i = find_dl_piece(state, index);
		TORRENT_ASSERT(i != m_downloads[state].end());
		return i->passed_hash_check;
	}

	std::vector<piece_picker::downloading_piece>::iterator piece_picker::find_dl_piece(
		int queue, int index)
	{
		TORRENT_ASSERT(queue >= 0 && queue < piece_pos::num_download_categories);
		downloading_piece cmp;
		cmp.index = index;
		std::vector<piece_picker::downloading_piece>::iterator i = std::lower_bound(
			m_downloads[queue].begin(), m_downloads[queue].end(), cmp);
		if (i == m_downloads[queue].end()) return i;
		if (i->index == index) return i;
		return m_downloads[queue].end();
	}

	std::vector<piece_picker::downloading_piece>::const_iterator piece_picker::find_dl_piece(
		int queue, int index) const
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
		int const current_state = p.download_state;
		TORRENT_ASSERT(current_state != piece_pos::piece_open);
		if (current_state == piece_pos::piece_open)
			return dp;

		// this function is not allowed to create new downloading pieces
		int new_state = 0;
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

		int prio = p.priority(this);
		p.download_state = new_state;
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << " " << dp_info.index << " state (" << current_state << " -> " << new_state << ")" << std::endl;
#endif

		// insert the downloading_piece in the list corresponding to
		// the new state
		downloading_piece cmp;
		cmp.index = dp_info.index;
		std::vector<downloading_piece>::iterator i = std::lower_bound(
			m_downloads[p.download_queue()].begin()
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
/*
	int piece_picker::get_block_state(piece_block block) const
	{
		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());

		// if we have the piece, the block state is considered finished
		if (m_piece_map[block.piece_index].index == piece_pos::we_have_index)
			return block_info::state_finished;

		int state = m_piece_map[block.piece_index].download_queue();
		if (state == piece_pos::piece_open) return block_info::state_none;
		std::vector<downloading_piece>::const_iterator i = find_dl_piece(state
			, block.piece_index);

		TORRENT_ASSERT(i != m_downloads[state].end());

		block_info const* info = blocks_for_piece(*i);
		TORRENT_ASSERT(info[block.block_index].piece_index == block.piece_index);
		return info[block.block_index].state;
	}
*/
	bool piece_picker::is_requested(piece_block block) const
	{
#ifdef TORRENT_USE_VALGRIND
		VALGRIND_CHECK_VALUE_IS_DEFINED(block);
#endif
		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());

		int state = m_piece_map[block.piece_index].download_queue();
		if (state == piece_pos::piece_open) return false;
		std::vector<downloading_piece>::const_iterator i = find_dl_piece(state
			, block.piece_index);

		TORRENT_ASSERT(i != m_downloads[state].end());

		block_info const* info = blocks_for_piece(*i);
		TORRENT_ASSERT(info[block.block_index].piece_index == block.piece_index);
		return info[block.block_index].state == block_info::state_requested;
	}

	bool piece_picker::is_downloaded(piece_block block) const
	{
#ifdef TORRENT_USE_VALGRIND
		VALGRIND_CHECK_VALUE_IS_DEFINED(block);
#endif
		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());

		if (m_piece_map[block.piece_index].index == piece_pos::we_have_index) return true;
		int state = m_piece_map[block.piece_index].download_queue();
		if (state == piece_pos::piece_open) return false;
		std::vector<downloading_piece>::const_iterator i = find_dl_piece(state
			, block.piece_index);
		TORRENT_ASSERT(i != m_downloads[state].end());

		block_info const* info = blocks_for_piece(*i);
		TORRENT_ASSERT(info[block.block_index].piece_index == block.piece_index);
		return info[block.block_index].state == block_info::state_finished
			|| info[block.block_index].state == block_info::state_writing;
	}

	bool piece_picker::is_finished(piece_block block) const
	{
#ifdef TORRENT_USE_VALGRIND
		VALGRIND_CHECK_VALUE_IS_DEFINED(block);
#endif
		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());

		piece_pos const& p = m_piece_map[block.piece_index];
		if (p.index == piece_pos::we_have_index) return true;
		if (p.download_queue() == piece_pos::piece_open) return false;
		std::vector<downloading_piece>::const_iterator i = find_dl_piece(p.download_queue()
			, block.piece_index);
		TORRENT_ASSERT(i != m_downloads[p.download_queue()].end());

		block_info const* info = blocks_for_piece(*i);
		TORRENT_ASSERT(info[block.block_index].piece_index == block.piece_index);
		return info[block.block_index].state == block_info::state_finished;
	}

	// options may be 0 or piece_picker::reverse
	bool piece_picker::mark_as_downloading(piece_block block
		, torrent_peer* peer, int options)
	{
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "mark_as_downloading( {"
			<< block.piece_index << ", " << block.block_index << "} )" << std::endl;
#endif

		TORRENT_ASSERT(peer == 0 || static_cast<torrent_peer*>(peer)->in_use);
		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());
		TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));
		TORRENT_ASSERT(!m_piece_map[block.piece_index].have());

		piece_pos& p = m_piece_map[block.piece_index];
		if (p.download_queue() == piece_pos::piece_open)
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
			int prio = p.priority(this);
			TORRENT_ASSERT(prio < int(m_priority_boundries.size())
				|| m_dirty);

			p.download_state = (options & reverse)
				? piece_pos::piece_downloading_reverse
				: piece_pos::piece_downloading;

			if (prio >= 0 && !m_dirty) update(prio, p.index);

			dlpiece_iter dp = add_download_piece(block.piece_index);
			block_info* binfo = blocks_for_piece(*dp);
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
			TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
			std::vector<downloading_piece>::iterator i = find_dl_piece(p.download_queue()
				, block.piece_index);
			TORRENT_ASSERT(i != m_downloads[p.download_queue()].end());
			block_info* binfo = blocks_for_piece(*i);
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
			if ((options & reverse) == 0 && p.reverse())
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

	int piece_picker::num_peers(piece_block block) const
	{
		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());
		TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));

		piece_pos const& p = m_piece_map[block.piece_index];
		if (!p.downloading()) return 0;

		std::vector<downloading_piece>::const_iterator i = find_dl_piece(p.download_queue()
			, block.piece_index);
		TORRENT_ASSERT(i != m_downloads[p.download_queue()].end());

		block_info const* binfo = blocks_for_piece(*i);
		block_info const& info = binfo[block.block_index];
		TORRENT_ASSERT(&info >= &m_block_info[0]);
		TORRENT_ASSERT(&info < &m_block_info[0] + m_block_info.size());
		TORRENT_ASSERT(info.piece_index == block.piece_index);
		return info.num_peers;
	}

	void piece_picker::get_availability(std::vector<int>& avail) const
	{
		TORRENT_ASSERT(m_seeds >= 0);
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

		avail.resize(m_piece_map.size());
		std::vector<int>::iterator j = avail.begin();
		for (std::vector<piece_pos>::const_iterator i = m_piece_map.begin()
			, end(m_piece_map.end()); i != end; ++i, ++j)
			*j = i->peer_count + m_seeds;
	}

	int piece_picker::get_availability(int piece) const
	{
		TORRENT_ASSERT(piece >= 0 && piece < int(m_piece_map.size()));
		return m_piece_map[piece].peer_count + m_seeds;
	}

	bool piece_picker::mark_as_writing(piece_block block, torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "mark_as_writing( {" << block.piece_index << ", " << block.block_index << "} )" << std::endl;
#endif

		TORRENT_ASSERT(peer == 0 || static_cast<torrent_peer*>(peer)->in_use);

		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());
		TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));
		// this is not valid for web peers
		// TORRENT_ASSERT(peer != 0);

		piece_pos& p = m_piece_map[block.piece_index];
		if (p.downloading() == 0)
		{
			// if we already have this piece, just ignore this
			if (have_piece(block.piece_index)) return false;

			int const prio = p.priority(this);
			TORRENT_ASSERT(prio < int(m_priority_boundries.size())
				|| m_dirty);
			p.download_state = piece_pos::piece_downloading;
			// prio being -1 can happen if a block is requested before
			// the piece priority was set to 0
			if (prio >= 0 && !m_dirty) update(prio, p.index);

			dlpiece_iter dp = add_download_piece(block.piece_index);
			block_info* binfo = blocks_for_piece(*dp);
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
			std::vector<downloading_piece>::iterator i = find_dl_piece(p.download_queue()
				, block.piece_index);
			TORRENT_ASSERT(i != m_downloads[p.download_queue()].end());
			block_info* binfo = blocks_for_piece(*i);
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
	void piece_picker::lock_piece(int piece)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "lock_piece(" << piece << ")" << std::endl;
#endif

		int state = m_piece_map[piece].download_queue();
		if (state == piece_pos::piece_open) return;
		std::vector<downloading_piece>::iterator i = find_dl_piece(state, piece);
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
	void piece_picker::write_failed(piece_block block)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "write_failed( {" << block.piece_index << ", " << block.block_index << "} )" << std::endl;
#endif

		int state = m_piece_map[block.piece_index].download_queue();
		if (state == piece_pos::piece_open) return;
		std::vector<downloading_piece>::iterator i = find_dl_piece(state, block.piece_index);
		if (i == m_downloads[state].end()) return;

		block_info* binfo = blocks_for_piece(*i);
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

		info.peer = 0;
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
			int prev_priority = p.priority(this);
			erase_download_piece(i);
			int new_priority = p.priority(this);

			if (m_dirty) return;
			if (new_priority == prev_priority) return;
			if (prev_priority == -1) add(block.piece_index);
			else update(prev_priority, p.index);
		}
	}

	void piece_picker::mark_as_canceled(const piece_block block, torrent_peer* peer)
	{
#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "mark_as_cancelled( {"
			<< block.piece_index << ", " << block.block_index
			<< "} )" << std::endl;
#endif

#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

		TORRENT_ASSERT(block.piece_index >= 0);
		TORRENT_ASSERT(block.block_index >= 0);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());
		TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));

		piece_pos& p = m_piece_map[block.piece_index];

		if (p.download_queue() == piece_pos::piece_open) return;

		std::vector<downloading_piece>::iterator i = find_dl_piece(p.download_queue()
			, block.piece_index);

		TORRENT_ASSERT(i != m_downloads[p.download_queue()].end());
		block_info* binfo = blocks_for_piece(*i);
		block_info& info = binfo[block.block_index];

		if (info.state == block_info::state_finished) return;

		TORRENT_ASSERT(info.num_peers == 0);
		info.peer = peer;
		TORRENT_ASSERT(info.state == block_info::state_writing
			|| peer == 0);
		if (info.state == block_info::state_writing)
		{
			--i->writing;
			info.state = block_info::state_none;
			// i may be invalid after this call
			i = update_piece_state(i);

			if (i->finished + i->writing + i->requested == 0)
			{
				int prev_priority = p.priority(this);
				erase_download_piece(i);
				int new_priority = p.priority(this);

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

	void piece_picker::mark_as_finished(piece_block block, torrent_peer* peer)
	{
#if TORRENT_USE_INVARIANT_CHECKS
		check_piece_state();
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "mark_as_finished( {"
			<< block.piece_index << ", " << block.block_index << "} )" << std::endl;
#endif

		TORRENT_ASSERT(peer == 0 || static_cast<torrent_peer*>(peer)->in_use);
		TORRENT_ASSERT(block.piece_index >= 0);
		TORRENT_ASSERT(block.block_index >= 0);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());
		TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));

		piece_pos& p = m_piece_map[block.piece_index];

		if (p.download_queue() == piece_pos::piece_open)
		{
			// if we already have this piece, just ignore this
			if (have_piece(block.piece_index)) return;

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif

			int const prio = p.priority(this);
			TORRENT_ASSERT(prio < int(m_priority_boundries.size())
				|| m_dirty);
			p.download_state = piece_pos::piece_downloading;
			if (prio >= 0 && !m_dirty) update(prio, p.index);

			dlpiece_iter dp = add_download_piece(block.piece_index);
			block_info* binfo = blocks_for_piece(*dp);
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
			TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif

			std::vector<downloading_piece>::iterator i = find_dl_piece(p.download_queue()
				, block.piece_index);
			TORRENT_ASSERT(i != m_downloads[p.download_queue()].end());
			block_info* binfo = blocks_for_piece(*i);
			block_info& info = binfo[block.block_index];
			TORRENT_ASSERT(info.piece_index == block.piece_index);

			if (info.state == block_info::state_finished) return;

			TORRENT_ASSERT(info.num_peers == 0);

			// peers may have been disconnected in between mark_as_writing
			// and mark_as_finished. When a peer disconnects, its m_peer_info
			// pointer is set to NULL. If so, preserve the previous peer
			// pointer, instead of forgetting who we downloaded this block from
			if (info.state != block_info::state_writing || peer != 0)
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
		m_pad_blocks.insert(block);
		// if we mark and entire piece as a pad file, we need to also
		// consder that piece as "had" and increment some counters
		typedef std::set<piece_block>::iterator iter;
		iter begin = m_pad_blocks.lower_bound(piece_block(block.piece_index, 0));
		int const blocks = blocks_in_piece(block.piece_index);
		iter end = m_pad_blocks.upper_bound(piece_block(block.piece_index, blocks));
		if (std::distance(begin, end) == blocks)
		{
			// the entire piece is a pad file
			we_have(block.piece_index);
		}
	}

/*
	void piece_picker::mark_as_checking(int index)
	{
		int state = m_piece_map[index].download_queue();
		if (state == piece_pos::piece_open) return;
		std::vector<downloading_piece>::iterator i = find_dl_piece(state, index);
		if (i == m_downloads[state].end()) return;
		TORRENT_ASSERT(i->outstanding_hash_check == false);
		i->outstanding_hash_check = true;
	}

	void piece_picker::mark_as_done_checking(int index)
	{
		int state = m_piece_map[index].download_queue();
		if (state == piece_pos::piece_open) return;
		std::vector<downloading_piece>::iterator i = find_dl_piece(state, index);
		if (i == m_downloads[state].end()) return;
		i->outstanding_hash_check = false;
	}
*/

	void piece_picker::get_downloaders(std::vector<torrent_peer*>& d, int index) const
	{
		TORRENT_ASSERT(index >= 0 && index <= int(m_piece_map.size()));

		d.clear();
		int state = m_piece_map[index].download_queue();
		const int num_blocks = blocks_in_piece(index);
		d.reserve(num_blocks);

		if (state == piece_pos::piece_open)
		{
			for (int i = 0; i < num_blocks; ++i) d.push_back(NULL);
			return;
		}

		std::vector<downloading_piece>::const_iterator i
			= find_dl_piece(state, index);
		TORRENT_ASSERT(i != m_downloads[state].end());
		block_info const* binfo = blocks_for_piece(*i);
		for (int j = 0; j != num_blocks; ++j)
		{
			TORRENT_ASSERT(binfo[j].peer == 0
				|| binfo[j].peer->in_use);
			d.push_back(binfo[j].peer);
		}
	}

	torrent_peer* piece_picker::get_downloader(piece_block block) const
	{
		int state = m_piece_map[block.piece_index].download_queue();
		if (state == piece_pos::piece_open) return 0;

		std::vector<downloading_piece>::const_iterator i = find_dl_piece(state
			, block.piece_index);

		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		block_info const* binfo = blocks_for_piece(*i);
		TORRENT_ASSERT(binfo[block.block_index].piece_index == block.piece_index);
		if (binfo[block.block_index].state == block_info::state_none)
			return NULL;

		torrent_peer* peer = binfo[block.block_index].peer;
		TORRENT_ASSERT(peer == 0 || static_cast<torrent_peer*>(peer)->in_use);
		return peer;
	}

	// this is called when a request is rejected or when
	// a peer disconnects. The piece might be in any state
	void piece_picker::abort_download(piece_block block, torrent_peer* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif

#ifdef TORRENT_PICKER_LOG
		std::cerr << "[" << this << "] " << "abort_download( {" << block.piece_index << ", " << block.block_index << "} )" << std::endl;
#endif
		TORRENT_ASSERT(peer == 0 || peer->in_use);

		TORRENT_ASSERT(block.block_index != piece_block::invalid.block_index);
		TORRENT_ASSERT(block.piece_index != piece_block::invalid.piece_index);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());
		TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));

		int state = m_piece_map[block.piece_index].download_queue();
		if (state == piece_pos::piece_open) return;

		std::vector<downloading_piece>::iterator i = find_dl_piece(state
			, block.piece_index);
		TORRENT_ASSERT(i != m_downloads[state].end());

		block_info* binfo = blocks_for_piece(*i);
		block_info& info = binfo[block.block_index];
		TORRENT_ASSERT(info.peer == 0 || info.peer->in_use);
		TORRENT_ASSERT(info.piece_index == block.piece_index);

		TORRENT_ASSERT(info.state != block_info::state_none);

		if (info.state != block_info::state_requested) return;

		piece_pos& p = m_piece_map[block.piece_index];
		int prev_prio = p.priority(this);

#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(info.peers.count(peer));
		info.peers.erase(peer);
#endif
		TORRENT_ASSERT(info.num_peers > 0);
		if (info.num_peers > 0) --info.num_peers;
		if (info.peer == peer) info.peer = 0;
		TORRENT_ASSERT(info.peers.size() == info.num_peers);

		TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));

		// if there are other peers, leave the block requested
		if (info.num_peers > 0) return;

		// clear the downloader of this block
		info.peer = 0;

		// clear this block as being downloaded
		info.state = block_info::state_none;
		TORRENT_ASSERT(i->requested > 0);
		--i->requested;

		// if there are no other blocks in this piece
		// that's being downloaded, remove it from the list
		if (i->requested + i->finished + i->writing == 0)
		{
			TORRENT_ASSERT(prev_prio < int(m_priority_boundries.size())
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

	int piece_picker::unverified_blocks() const
	{
		int counter = 0;
		for (int k = 0; k < piece_pos::num_download_categories; ++k)
		{
			for (std::vector<downloading_piece>::const_iterator i = m_downloads[k].begin();
				i != m_downloads[k].end(); ++i)
			{
				counter += int(i->finished);
			}
		}
		return counter;
	}

}

