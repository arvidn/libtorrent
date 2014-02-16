/*

Copyright (c) 2003, Arvid Norberg
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

#include "libtorrent/pch.hpp"

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

#include <boost/bind.hpp>
#include <boost/tuple/tuple.hpp>

#include "libtorrent/piece_picker.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/random.hpp"

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/policy.hpp" // for policy::peer
#endif

#include "libtorrent/invariant_check.hpp"

#define TORRENT_PIECE_PICKER_INVARIANT_CHECK INVARIANT_CHECK
//#define TORRENT_NO_EXPENSIVE_INVARIANT_CHECK
//#define TORRENT_PIECE_PICKER_INVARIANT_CHECK

//#define TORRENT_PICKER_LOG

namespace libtorrent
{

	const piece_block piece_block::invalid(0x7FFFF, 0x1FFF);

	piece_picker::piece_picker()
		: m_seeds(0)
		, m_priority_boundries(1, int(m_pieces.size()))
		, m_blocks_per_piece(0)
		, m_blocks_in_last_piece(0)
		, m_num_filtered(0)
		, m_num_have_filtered(0)
		, m_num_have(0)
		, m_cursor(0)
		, m_reverse_cursor(0)
		, m_sparse_regions(1)
		, m_dirty(false)
	{
#ifdef TORRENT_PICKER_LOG
		std::cerr << "new piece_picker" << std::endl;
#endif
#ifdef TORRENT_DEBUG
		check_invariant();
#endif
	}

	void piece_picker::init(int blocks_per_piece, int blocks_in_last_piece, int total_num_pieces)
	{
		TORRENT_ASSERT(blocks_per_piece > 0);
		TORRENT_ASSERT(total_num_pieces > 0);

#ifdef TORRENT_PICKER_LOG
		std::cerr << "piece_picker::init()" << std::endl;
#endif
		// allocate the piece_map to cover all pieces
		// and make them invalid (as if we don't have a single piece)
		m_piece_map.resize(total_num_pieces, piece_pos(0, 0));
		m_reverse_cursor = int(m_piece_map.size());
		m_cursor = 0;

		m_downloads.clear();
		m_block_info.clear();

		m_num_filtered += m_num_have_filtered;
		m_num_have_filtered = 0;
		m_num_have = 0;
		m_dirty = true;
		for (std::vector<piece_pos>::iterator i = m_piece_map.begin()
			, end(m_piece_map.end()); i != end; ++i)
		{
			i->peer_count = 0;
			i->downloading = 0;
			i->index = 0;
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

		if (m_piece_map[index].downloading)
		{
			std::vector<downloading_piece>::const_iterator piece = find_dl_piece(index);
			TORRENT_ASSERT(piece != m_downloads.end());
			st = *piece;
			st.info = 0;
			return;
		}
		st.info = 0;
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

	piece_picker::downloading_piece& piece_picker::add_download_piece(int piece)
	{
		int num_downloads = m_downloads.size();
		int block_index = num_downloads * m_blocks_per_piece;
		if (int(m_block_info.size()) < block_index + m_blocks_per_piece)
		{
			block_info* base = 0;
			if (!m_block_info.empty()) base = &m_block_info[0];
			m_block_info.resize(block_index + m_blocks_per_piece);
			if (!m_downloads.empty() && &m_block_info[0] != base)
			{
				// this means the memory was reallocated, update the pointers
				for (int i = 0; i < int(m_downloads.size()); ++i)
					m_downloads[i].info = &m_block_info[m_downloads[i].info - base];
			}
		}
		downloading_piece cmp;
		cmp.index = piece;
		std::vector<downloading_piece>::iterator i = std::lower_bound(m_downloads.begin()
			, m_downloads.end(), cmp);
		TORRENT_ASSERT(i == m_downloads.end() || i->index != piece);
		i = m_downloads.insert(i, downloading_piece());
		downloading_piece& ret = *i;
		ret.index = piece;
		ret.info = &m_block_info[block_index];
		for (int i = 0; i < m_blocks_per_piece; ++i)
		{
			ret.info[i].num_peers = 0;
			ret.info[i].state = block_info::state_none;
			ret.info[i].peer = 0;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			ret.info[i].piece_index = piece;
#endif
		}
		return ret;
	}

	void piece_picker::erase_download_piece(std::vector<downloading_piece>::iterator i)
	{
		std::vector<downloading_piece>::iterator other = std::find_if(
			m_downloads.begin(), m_downloads.end()
			, boost::bind(&downloading_piece::info, _1)
			== &m_block_info[(m_downloads.size() - 1) * m_blocks_per_piece]);
		TORRENT_ASSERT(other != m_downloads.end());

		if (i != other)
		{
			std::copy(other->info, other->info + m_blocks_per_piece, i->info);
			other->info = i->info;
		}
		m_piece_map[i->index].downloading = false;
		m_downloads.erase(i);
	}

#ifdef TORRENT_DEBUG

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
		for (std::vector<int>::const_iterator i = m_priority_boundries.begin()
			, end(m_priority_boundries.end()); i != end; ++i)
		{
			std::cerr << *i << " ";
		}
		std::cout << std::endl;
		int index = 0;
		std::vector<int>::const_iterator j = m_priority_boundries.begin();
		for (std::vector<int>::const_iterator i = m_pieces.begin()
			, end(m_pieces.end()); i != end; ++i, ++index)
		{
			if (*i == -1) break;
			while (j != m_priority_boundries.end() && *j <= index)
			{
				std::cerr << "| ";
				++j;
			}
			std::cerr << *i << "(" << m_piece_map[*i].index << ") ";
		}
		std::cerr << std::endl;
	}
#endif

	void piece_picker::check_invariant(const torrent* t) const
	{
#if TORRENT_COMPACT_PICKER
		TORRENT_ASSERT(sizeof(piece_pos) == 4);
#else
		TORRENT_ASSERT(sizeof(piece_pos) == 8);
#endif
		TORRENT_ASSERT(m_num_have >= 0);
		TORRENT_ASSERT(m_num_have_filtered >= 0);
		TORRENT_ASSERT(m_num_filtered >= 0);
		TORRENT_ASSERT(m_seeds >= 0);

		if (!m_downloads.empty())
		{
			for (std::vector<downloading_piece>::const_iterator i = m_downloads.begin();
				i != m_downloads.end() - 1; ++i)
			{
				downloading_piece const& dp = *i;
				downloading_piece const& next = *(i + 1);
//				TORRENT_ASSERT(dp.finished + dp.writing >= next.finished + next.writing);
				TORRENT_ASSERT(dp.index < next.index);
			}
		}

		if (t != 0)
			TORRENT_ASSERT((int)m_piece_map.size() == t->torrent_file().num_pieces());

		for (std::vector<downloading_piece>::const_iterator i = m_downloads.begin()
			, end(m_downloads.end()); i != end; ++i)
		{
			bool blocks_requested = false;
			int num_blocks = blocks_in_piece(i->index);
			int num_requested = 0;
			int num_finished = 0;
			int num_writing = 0;
			for (int k = 0; k < num_blocks; ++k)
			{
				TORRENT_ASSERT(i->info[k].piece_index == i->index);
				TORRENT_ASSERT(i->info[k].peer == 0 || static_cast<policy::peer*>(i->info[k].peer)->in_use);
				if (i->info[k].state == block_info::state_finished)
				{
					++num_finished;
					TORRENT_ASSERT(i->info[k].num_peers == 0);
				}
				else if (i->info[k].state == block_info::state_requested)
				{
					++num_requested;
					blocks_requested = true;
					TORRENT_ASSERT(i->info[k].num_peers > 0);
				}
				else if (i->info[k].state == block_info::state_writing)
				{
					++num_writing;
					TORRENT_ASSERT(i->info[k].num_peers == 0);
				}
			}
			TORRENT_ASSERT(blocks_requested == (i->state != none));
			TORRENT_ASSERT(num_requested == i->requested);
			TORRENT_ASSERT(num_writing == i->writing);
			TORRENT_ASSERT(num_finished == i->finished);
			if (m_piece_map[i->index].full)
				TORRENT_ASSERT(num_finished + num_writing + num_requested == num_blocks);
		}
		int num_pieces = int(m_piece_map.size());
		TORRENT_ASSERT(m_cursor >= 0);
		TORRENT_ASSERT(m_cursor <= num_pieces);
		TORRENT_ASSERT(m_reverse_cursor <= num_pieces);
		TORRENT_ASSERT(m_reverse_cursor >= 0);
		TORRENT_ASSERT(m_reverse_cursor > m_cursor
			|| (m_cursor == num_pieces && m_reverse_cursor == 0));

#ifdef TORRENT_NO_EXPENSIVE_INVARIANT_CHECK
		return;
#endif

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

				if (i->downloading)
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
				TORRENT_ASSERT(p.downloading == 0);
			}

			if (t != 0)
				TORRENT_ASSERT(!t->have_piece(index));

			int prio = p.priority(this);
			TORRENT_ASSERT(prio == -1 || p.downloading == (prio % piece_picker::prio_factor == 0));

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

			int count = std::count_if(m_downloads.begin(), m_downloads.end()
				, has_index(index));
			if (i->downloading == 1)
			{
				TORRENT_ASSERT(count == 1);
			}
			else
			{
				TORRENT_ASSERT(count == 0);
			}
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
		if (int(m_priority_boundries.size()) <= priority)
			m_priority_boundries.resize(priority + 1, m_pieces.size());

		TORRENT_ASSERT(int(m_priority_boundries.size()) >= priority);

		int range_start, range_end;
		priority_range(priority, &range_start, &range_end);
		int new_index;
		if (range_end == range_start) new_index = range_start;
		else new_index = random() % (range_end - range_start + 1) + range_start;

#ifdef TORRENT_PICKER_LOG
		std::cerr << "add " << index << " (" << priority << ")" << std::endl;
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
			std::cerr << " index: " << index
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
//			shuffle(priority, new_index);
#ifdef TORRENT_PICKER_LOG
//			print_pieces();
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
		std::cerr << "remove " << m_pieces[elem_index] << " (" << priority << ")" << std::endl;
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
		TORRENT_ASSERT(priority >= 0);
		TORRENT_ASSERT(elem_index >= 0);

		TORRENT_ASSERT(int(m_priority_boundries.size()) > priority);

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
		std::cerr << "update " << index << " (" << priority << "->" << new_priority << ")" << std::endl;
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
				--priority;
				new_index = m_priority_boundries[priority]++;
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
				new_index = --m_priority_boundries[priority];
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
		std::cerr << "shuffle()" << std::endl;
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
/*
	void piece_picker::sort_piece(std::vector<downloading_piece>::iterator dp)
	{
		TORRENT_ASSERT(m_piece_map[dp->index].downloading);
		int complete = dp->writing + dp->finished;
		if (dp != m_downloads.begin())
		{
			for (std::vector<downloading_piece>::iterator j(dp-1);
				dp != m_downloads.begin(); --dp, --j)
			{
				TORRENT_ASSERT(j >= m_downloads.begin());
				if (j->finished + j->writing >= complete) break;
				using std::swap;
				swap(*j, *dp);
				if (j == m_downloads.begin()) return;
			}
		}

		TORRENT_ASSERT(dp != m_downloads.end());
		for (std::vector<downloading_piece>::iterator j(dp+1);
			dp != m_downloads.end() - 1; ++dp, ++j)
		{
			TORRENT_ASSERT(j < m_downloads.end());
			if (j->finished + j->writing <= complete) break;
			using std::swap;
			swap(*j, *dp);
			if (j == m_downloads.end() - 1) return;
		}
	}
*/
	void piece_picker::restore_piece(int index)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < (int)m_piece_map.size());

		TORRENT_ASSERT(m_piece_map[index].downloading == 1);

		std::vector<downloading_piece>::iterator i = find_dl_piece(index);

		TORRENT_ASSERT(i != m_downloads.end());
#ifdef TORRENT_DEBUG
		int num_blocks = blocks_in_piece(i->index);
		for (int k = 0; k < num_blocks; ++k)
		{
			TORRENT_ASSERT(i->info[k].piece_index == index);
			TORRENT_ASSERT(i->info[k].state == block_info::state_finished);
			TORRENT_ASSERT(i->info[k].num_peers == 0);
		}
#endif

		piece_pos& p = m_piece_map[index];
		int prev_priority = p.priority(this);
		erase_download_piece(i);
		int new_priority = p.priority(this);

		if (new_priority == prev_priority) return;
		if (m_dirty) return;
		if (prev_priority == -1) add(index);
		else update(prev_priority, p.index);
	}

	void piece_picker::inc_refcount_all()
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
	}

	void piece_picker::dec_refcount_all()
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
			return;
		}
		TORRENT_ASSERT(m_seeds == 0);

		for (std::vector<piece_pos>::iterator i = m_piece_map.begin()
			, end(m_piece_map.end()); i != end; ++i)
		{
			TORRENT_ASSERT(i->peer_count > 0);
			--i->peer_count;
		}

		m_dirty = true;
	}

	void piece_picker::inc_refcount(int index)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif

		piece_pos& p = m_piece_map[index];
	
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

	void piece_picker::dec_refcount(int index)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif

		piece_pos& p = m_piece_map[index];
		int prev_priority = p.priority(this);
		TORRENT_ASSERT(p.peer_count > 0);
		--p.peer_count;
		if (m_dirty) return;
		if (prev_priority >= 0) update(prev_priority, p.index);
	}

	void piece_picker::inc_refcount(bitfield const& bitmask)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
		TORRENT_ASSERT(bitmask.size() == m_piece_map.size());

		int index = 0;
		bool updated = false;
		for (bitfield::const_iterator i = bitmask.begin()
			, end(bitmask.end()); i != end; ++i, ++index)
		{
			if (*i)
			{
				++m_piece_map[index].peer_count;
				updated = true;
			}
		}

		if (updated) m_dirty = true;
	}

	void piece_picker::dec_refcount(bitfield const& bitmask)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
		TORRENT_ASSERT(bitmask.size() <= m_piece_map.size());

		int index = 0;
		bool updated = false;
		for (bitfield::const_iterator i = bitmask.begin()
			, end(bitmask.end()); i != end; ++i, ++index)
		{
			if (*i)
			{
				--m_piece_map[index].peer_count;
				updated = true;
			}
		}

		if (updated) m_dirty = true;
	}

	void piece_picker::update_pieces() const
	{
		TORRENT_ASSERT(m_dirty);
		if (m_priority_boundries.empty()) m_priority_boundries.resize(1, 0);
#ifdef TORRENT_PICKER_LOG
		std::cerr << "update_pieces" << std::endl;
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
			std::random_shuffle(&m_pieces[0] + start, &m_pieces[0] + *i);
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

	void piece_picker::we_dont_have(int index)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < (int)m_piece_map.size());

		piece_pos& p = m_piece_map[index];
		TORRENT_ASSERT(p.downloading == 0);

#ifdef TORRENT_PICKER_LOG
		std::cerr << "piece_picker::we_dont_have(" << index << ")" << std::endl;
#endif
		if (!p.have()) return;

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
		TORRENT_ASSERT(index < (int)m_piece_map.size());

#ifdef TORRENT_PICKER_LOG
		std::cerr << "piece_picker::we_have(" << index << ")" << std::endl;
#endif
		piece_pos& p = m_piece_map[index];
		int info_index = p.index;
		int priority = p.priority(this);
		TORRENT_ASSERT(priority < int(m_priority_boundries.size()) || m_dirty);

		if (p.downloading)
		{
			std::vector<downloading_piece>::iterator i
				= find_dl_piece(index);
			TORRENT_ASSERT(i != m_downloads.end());
			erase_download_piece(i);
		}

		TORRENT_ASSERT(find_dl_piece(index) == m_downloads.end());

		if (p.have()) return;

// maintain sparse_regions
		if (index == 0)
		{
			if (index == int(m_piece_map.size()) - 1
				|| m_piece_map[index + 1].have())
				--m_sparse_regions;
		}
		else if (index == int(m_piece_map.size() - 1))
		{
			if (index == 0
				|| m_piece_map[index - 1].have())
				--m_sparse_regions;
		}
		else
		{
			bool have_before = m_piece_map[index-1].have();
			bool have_after = m_piece_map[index+1].have();
			if (have_after && have_before) --m_sparse_regions;
			else if (!have_after && !have_before) ++m_sparse_regions;
		}

		if (p.filtered())
		{
			--m_num_filtered;
			++m_num_have_filtered;
		}
		++m_num_have;
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
		TORRENT_ASSERT(new_piece_priority >= 0);
		TORRENT_ASSERT(new_piece_priority <= 7);
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < (int)m_piece_map.size());
		
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

		if (prev_priority == new_priority) return ret;

		if (m_dirty) return ret;
		if (prev_priority == -1)
		{
			add(index);
		}
		else
		{
			update(prev_priority, p.index);
		}
		return ret;
	}

	int piece_picker::piece_priority(int index) const
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < (int)m_piece_map.size());

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
			, int num_blocks)
		{
			if (src.empty()) return num_blocks;
			int to_copy;
//			if (prefer_whole_pieces == 0)
				to_copy = (std::min)(int(src.size()), num_blocks);
//			else
//				to_copy = int(src.size());

			dst.insert(dst.end()
				, src.begin(), src.begin() + to_copy);
			src.clear();
			return num_blocks - to_copy;
		}
	}

	// pieces describes which pieces the peer we're requesting from
	// has.
	// interesting_blocks is an out parameter, and will be filled
	// with (up to) num_blocks of interesting blocks that the peer has.
	// prefer_whole_pieces can be set if this peer should download
	// whole pieces rather than trying to download blocks from the
	// same piece as other peers.
	//	the void* is the pointer to the policy::peer of the peer we're
	// picking pieces from. This is used when downloading whole pieces,
	// to only pick from the same piece the same peer is downloading
	// from. state is supposed to be set to fast if the peer is downloading
	// relatively fast, by some notion. Slow peers will prefer not
	// to pick blocks from the same pieces as fast peers, and vice
	// versa. Downloading pieces are marked as being fast, medium
	// or slow once they're started.

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
	// * speed_affinity
	//     have an affinity to pick pieces in the same speed
	//     category.
	// * ignore_whole_pieces
	//     ignores the prefer_whole_pieces parameter (as if
	//     it was 0)

	// only one of rarest_first, sequential can be set

	void piece_picker::pick_pieces(bitfield const& pieces
		, std::vector<piece_block>& interesting_blocks, int num_blocks
		, int prefer_whole_pieces, void* peer, piece_state_t speed
		, int options, std::vector<int> const& suggested_pieces
		, int num_peers) const
	{
		TORRENT_ASSERT(peer == 0 || static_cast<policy::peer*>(peer)->in_use);

		// prevent the number of partial pieces to grow indefinitely
		// make this scale by the number of peers we have. For large
		// scale clients, we would have more peers, and allow a higher
		// threshold for the number of partials
		// TODO: 2 m_downloads size will be > 0 just by having pad-files
		// in the torrent. That should be taken into account here.
		if (m_downloads.size() > num_peers * 3 / 2) options |= prioritize_partials;

		if (options & ignore_whole_pieces) prefer_whole_pieces = 0;

		// only one of rarest_first and sequential can be set.
		TORRENT_ASSERT(((options & rarest_first) ? 1 : 0)
			+ ((options & sequential) ? 1 : 0) <= 1);
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
		TORRENT_ASSERT(num_blocks > 0);
		TORRENT_ASSERT(pieces.size() == m_piece_map.size());

		TORRENT_ASSERT(!m_priority_boundries.empty()
			|| m_dirty);

		// this will be filled with blocks that we should not request
		// unless we can't find num_blocks among the other ones.
		// blocks that belong to pieces with a mismatching speed
		// category for instance, or if we prefer whole pieces,
		// blocks belonging to a piece that others have
		// downloaded to
		std::vector<piece_block> backup_blocks;
		std::vector<piece_block> backup_blocks2;
		const std::vector<int> empty_vector;
	
		// When prefer_whole_pieces is set (usually set when downloading from
		// fast peers) the partial pieces will not be prioritized, but actually
		// ignored as long as possible. All blocks found in downloading
		// pieces are regarded as backup blocks

		if (options & prioritize_partials)
		{
			for (std::vector<downloading_piece>::const_iterator i = m_downloads.begin()
				, end(m_downloads.end()); i != end; ++i)
			{
				if (!is_piece_free(i->index, pieces)) continue;
				if (m_piece_map[i->index].full
					&& backup_blocks.size() >= num_blocks
					&& backup_blocks2.size() >= num_blocks)
					continue;

				num_blocks = add_blocks_downloading(*i, pieces
					, interesting_blocks, backup_blocks, backup_blocks2
					, num_blocks, prefer_whole_pieces, peer, speed, options);
				if (num_blocks <= 0) return;
			}

			num_blocks = append_blocks(interesting_blocks, backup_blocks
				, num_blocks);
			if (num_blocks <= 0) return;

			num_blocks = append_blocks(interesting_blocks, backup_blocks2
				, num_blocks);
			if (num_blocks <= 0) return;
		}

		if (!suggested_pieces.empty())
		{
			for (std::vector<int>::const_iterator i = suggested_pieces.begin();
				i != suggested_pieces.end(); ++i)
			{
				if (!is_piece_free(*i, pieces)) continue;
				num_blocks = add_blocks(*i, pieces
					, interesting_blocks, backup_blocks
					, backup_blocks2, num_blocks
					, prefer_whole_pieces, peer, empty_vector
					, speed, options);
				if (num_blocks <= 0) return;
			}
		}

		if (options & sequential)
		{
			if (options & reverse)
			{
				for (int i = m_reverse_cursor - 1; i >= m_cursor; --i)
				{	
					if (!is_piece_free(i, pieces)) continue;
					num_blocks = add_blocks(i, pieces
						, interesting_blocks, backup_blocks
						, backup_blocks2, num_blocks
						, prefer_whole_pieces, peer, suggested_pieces
						, speed, options);
					if (num_blocks <= 0) return;
				}
			}
			else
			{
				for (int i = m_cursor; i < m_reverse_cursor; ++i)
				{	
					if (!is_piece_free(i, pieces)) continue;
					num_blocks = add_blocks(i, pieces
						, interesting_blocks, backup_blocks
						, backup_blocks2, num_blocks
						, prefer_whole_pieces, peer, suggested_pieces
						, speed, options);
					if (num_blocks <= 0) return;
				}
			}
		}
		else if (options & rarest_first)
		{
			if (m_dirty) update_pieces();
			TORRENT_ASSERT(!m_dirty);

			if (options & reverse)
			{
				// it's a bit complicated in order to always prioritize
				// partial pieces, and respect priorities. Every chunk
				// of 4 priority levels are traversed in forward order, but otherwise
				// they are traversed in reverse order
				// round up to an even 4 priority boundry, to make it simpler
				// to do the akward reverse traversing
#define div_round_up(n, d) (((n) + (d) - 1) / (d))
				m_priority_boundries.resize(div_round_up(m_priority_boundries.size()
					, prio_factor) * prio_factor, m_priority_boundries.back());
				for (int i = m_priority_boundries.size() - 1; i >= 0; --i)
				{
					int prio = (i / prio_factor) * prio_factor
						+ prio_factor - 1 - (i % prio_factor);
				
					TORRENT_ASSERT(prio >= 0);
					TORRENT_ASSERT(prio < int(m_priority_boundries.size()));
					int start = prio == 0 ? 0 : m_priority_boundries[prio - 1];
					for (int p = start; p < m_priority_boundries[prio]; ++p)
					{
						if (!is_piece_free(m_pieces[p], pieces)) continue;
						num_blocks = add_blocks(m_pieces[p], pieces
							, interesting_blocks, backup_blocks
							, backup_blocks2, num_blocks
							, prefer_whole_pieces, peer, suggested_pieces
							, speed, options);
						if (num_blocks <= 0) return;
					}
				}
#undef div_round_up
			}
			else
			{
				for (std::vector<int>::const_iterator i = m_pieces.begin();
					i != m_pieces.end(); ++i)
				{
					if (!is_piece_free(*i, pieces)) continue;
					num_blocks = add_blocks(*i, pieces
						, interesting_blocks, backup_blocks
						, backup_blocks2, num_blocks
						, prefer_whole_pieces, peer, suggested_pieces
						, speed, options);
					if (num_blocks <= 0) return;
				}
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
				bool done = false;
				// skip pieces we can't pick, and suggested pieces
				// since we've already picked those
				while (!can_pick(piece, pieces)
					|| std::find(suggested_pieces.begin()
					, suggested_pieces.end(), piece)
					!= suggested_pieces.end())
				{
					++piece;
					if (piece == int(m_piece_map.size())) piece = 0;
					// could not find any more pieces
					if (piece == start_piece) { done = true; break; }
				}
				if (done) break;

				TORRENT_ASSERT(can_pick(piece, pieces));
				TORRENT_ASSERT(m_piece_map[piece].downloading == false);

				int start, end;
				boost::tie(start, end) = expand_piece(piece, prefer_whole_pieces, pieces);
				for (int k = start; k < end; ++k)
				{
					TORRENT_ASSERT(m_piece_map[k].downloading == false);
					TORRENT_ASSERT(m_piece_map[k].priority(this) >= 0);
					int num_blocks_in_piece = blocks_in_piece(k);
					if (prefer_whole_pieces == 0 && num_blocks_in_piece > num_blocks)
						num_blocks_in_piece = num_blocks;
					for (int j = 0; j < num_blocks_in_piece; ++j)
					{
						TORRENT_ASSERT(is_piece_free(k, pieces));
						interesting_blocks.push_back(piece_block(k, j));
						--num_blocks;
					}
				}
				piece = end;
				if (piece == int(m_piece_map.size())) piece = 0;
				// could not find any more pieces
				if (piece == start_piece) break;
			}
		
		}

		if (num_blocks <= 0) return;

		// we might have to re-pick some backup blocks
		// from full pieces, since we skipped those the
		// first pass over
		for (std::vector<downloading_piece>::const_iterator i = m_downloads.begin()
			, end(m_downloads.end()); i != end; ++i)
		{
			if (!pieces[i->index]) continue;
			// we've already considered the non-full pieces
			if (!m_piece_map[i->index].full) continue;
			std::vector<piece_block> temp;
			add_blocks_downloading(*i, pieces
				, temp, backup_blocks, backup_blocks2
				, num_blocks, prefer_whole_pieces, peer, speed, options);
		}

#ifdef TORRENT_DEBUG
		verify_pick(interesting_blocks, pieces);
		verify_pick(backup_blocks, pieces);
		verify_pick(backup_blocks2, pieces);
#endif

		std::vector<piece_block> temp;
		for (std::vector<downloading_piece>::const_iterator i = m_downloads.begin()
			, end(m_downloads.end()); i != end; ++i)
		{
			if (!pieces[i->index]) continue;
			if (piece_priority(i->index) == 0) continue;

			int num_blocks_in_piece = blocks_in_piece(i->index);

			// fill in with blocks requested from other peers
			// as backups
			bool done = false;
			for (int j = 0; j < num_blocks_in_piece; ++j)
			{
				block_info const& info = i->info[j];
				TORRENT_ASSERT(info.peer == 0 || static_cast<policy::peer*>(info.peer)->in_use);
				TORRENT_ASSERT(info.piece_index == i->index);
				if (info.state != block_info::state_requested
					|| info.peer == peer)
					continue;
				temp.push_back(piece_block(i->index, j));
				done = true;
			}
			if (done) break;
		}

		num_blocks = append_blocks(interesting_blocks, backup_blocks
			, num_blocks);
		if (num_blocks <= 0) return;

		num_blocks = append_blocks(interesting_blocks, backup_blocks2, num_blocks);
		if (num_blocks <= 0) return;

		// don't double-pick anything if the peer is on parole
		if (options & on_parole) return;

		// pick one random block from the first busy piece we encountered
		// none of these blocks have more than one request to them
		if (!temp.empty()) interesting_blocks.push_back(temp[random() % temp.size()]);

#ifdef TORRENT_DEBUG
//		make sure that we at this point have added requests to all unrequested blocks
//		in all downloading pieces

		for (std::vector<downloading_piece>::const_iterator i = m_downloads.begin()
			, end(m_downloads.end()); i != end; ++i)
		{
			if (!pieces[i->index]) continue;
			if (piece_priority(i->index) == 0) continue;
				
			int num_blocks_in_piece = blocks_in_piece(i->index);
			for (int j = 0; j < num_blocks_in_piece; ++j)
			{
				block_info const& info = i->info[j];
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
				
				for (std::vector<downloading_piece>::const_iterator l = m_downloads.begin()
					, end(m_downloads.end()); l != end; ++l)
				{
					fprintf(stderr, "%d : ", l->index);
					int num_blocks_in_piece = blocks_in_piece(l->index);
					for (int m = 0; m < num_blocks_in_piece; ++m)
						fprintf(stderr, "%d", l->info[m].state);
					fprintf(stderr, "\n");
				}

				TORRENT_ASSERT(false);
			}
		}

		if (interesting_blocks.empty())
		{
//			print_pieces();
			for (int i = 0; i < num_pieces(); ++i)
			{
				if (!pieces[i]) continue;
				if (piece_priority(i) == 0) continue;
				if (have_piece(i)) continue;

				std::vector<downloading_piece>::const_iterator k = find_dl_piece(i);

				TORRENT_ASSERT(k != m_downloads.end());
				if (k == m_downloads.end()) continue;

				// this assert is not valid for web_seeds
				/*
				int num_blocks_in_piece = blocks_in_piece(k->index);
				for (int j = 0; j < num_blocks_in_piece; ++j)
				{
					block_info const& info = k->info[j];
					TORRENT_ASSERT(info.piece_index == k->index);
					if (info.state == block_info::state_finished) continue;
					TORRENT_ASSERT(info.peer != 0);
				}
				*/
			}
		}
#endif

	}

	int piece_picker::blocks_in_piece(int index) const
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < (int)m_piece_map.size());
		if (index+1 == (int)m_piece_map.size())
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
			&& !m_piece_map[piece].downloading
			&& !m_piece_map[piece].filtered();
	}

	void piece_picker::clear_peer(void* peer)
	{
		for (std::vector<block_info>::iterator i = m_block_info.begin()
			, end(m_block_info.end()); i != end; ++i)
		{
			TORRENT_ASSERT(i->peer == 0 || static_cast<policy::peer*>(i->peer)->in_use);
			if (i->peer == peer) i->peer = 0;
		}
	}

	namespace
	{
		// the first bool is true if this is the only peer that has requested and downloaded
		// blocks from this piece.
		// the second bool is true if this is the only active peer that is requesting
		// and downloading blocks from this piece. Active means having a connection.
		boost::tuple<bool, bool> requested_from(piece_picker::downloading_piece const& p
			, int num_blocks_in_piece, void* peer)
		{
			bool exclusive = true;
			bool exclusive_active = true;
			for (int j = 0; j < num_blocks_in_piece; ++j)
			{
				piece_picker::block_info const& info = p.info[j];
				TORRENT_ASSERT(info.peer == 0 || static_cast<policy::peer*>(info.peer)->in_use);
				TORRENT_ASSERT(info.piece_index == p.index);
				if (info.state != piece_picker::block_info::state_none
					&& info.peer != peer)
				{
					exclusive = false;
					if (info.state == piece_picker::block_info::state_requested
						&& info.peer != 0)
					{
						exclusive_active = false;
						return boost::make_tuple(exclusive, exclusive_active);
					}
				}
			}
			return boost::make_tuple(exclusive, exclusive_active);
		}
	}

	int piece_picker::add_blocks(int piece
		, bitfield const& pieces
		, std::vector<piece_block>& interesting_blocks
		, std::vector<piece_block>& backup_blocks
		, std::vector<piece_block>& backup_blocks2
		, int num_blocks, int prefer_whole_pieces
		, void* peer, std::vector<int> const& ignore
		, piece_state_t speed, int options) const
	{
		TORRENT_ASSERT(piece >= 0);
		TORRENT_ASSERT(piece < (int)m_piece_map.size());
		TORRENT_ASSERT(is_piece_free(piece, pieces));

//		std::cout << "add_blocks(" << piece << ")" << std::endl;
//		std::cout << "  num_blocks " << num_blocks << std::endl;

		// ignore pieces found in the ignore list
		if (std::find(ignore.begin(), ignore.end(), piece) != ignore.end()) return num_blocks;

		TORRENT_ASSERT(m_piece_map[piece].priority(this) >= 0);
		if (m_piece_map[piece].downloading)
		{
			if (m_piece_map[piece].full) return num_blocks;

			// if we're prioritizing partials, we've already
			// looked through the downloading pieces
			if (options & prioritize_partials) return num_blocks;

			std::vector<downloading_piece>::const_iterator i = find_dl_piece(piece);
			TORRENT_ASSERT(i != m_downloads.end());

//			std::cout << "add_blocks_downloading(" << piece << ")" << std::endl;

			return add_blocks_downloading(*i, pieces
				, interesting_blocks, backup_blocks, backup_blocks2
				, num_blocks, prefer_whole_pieces, peer, speed, options);
		}

		int num_blocks_in_piece = blocks_in_piece(piece);

		// pick a new piece
		if (prefer_whole_pieces == 0)
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
			boost::tie(start, end) = expand_piece(piece, prefer_whole_pieces, pieces);
			for (int k = start; k < end; ++k)
			{
				TORRENT_ASSERT(m_piece_map[k].priority(this) > 0);
				num_blocks_in_piece = blocks_in_piece(k);
				TORRENT_ASSERT(is_piece_free(k, pieces));
				for (int j = 0; j < num_blocks_in_piece; ++j)
				{
					interesting_blocks.push_back(piece_block(k, j));
					--num_blocks;
				}
			}
		}
#ifdef TORRENT_DEBUG
		verify_pick(interesting_blocks, pieces);
#endif
		if (num_blocks <= 0) return 0;
		return num_blocks;
	}

	int piece_picker::add_blocks_downloading(downloading_piece const& dp
		, bitfield const& pieces
		, std::vector<piece_block>& interesting_blocks
		, std::vector<piece_block>& backup_blocks
		, std::vector<piece_block>& backup_blocks2
		, int num_blocks, int prefer_whole_pieces
		, void* peer, piece_state_t speed, int options) const
	{
		if (!pieces[dp.index]) return num_blocks;
		if (m_piece_map[dp.index].filtered()) return num_blocks;

		int num_blocks_in_piece = blocks_in_piece(dp.index);

		// if all blocks have been requested (and we don't need any backup
		// blocks), we might as well return immediately
/*		if (int(backup_blocks2.size()) >= num_blocks
			&& int(backup_blocks.size()) >= num_blocks
			&& dp.requested + dp.writing + dp.finished == num_blocks_in_piece)
			return num_blocks;
*/
		// is true if all the other pieces that are currently
		// requested from this piece are from the same
		// peer as 'peer'.
		bool exclusive;
		bool exclusive_active;
		boost::tie(exclusive, exclusive_active)
			= requested_from(dp, num_blocks_in_piece, peer);

		// peers on parole are only allowed to pick blocks from
		// pieces that only they have downloaded/requested from
		if ((options & on_parole) && !exclusive) return num_blocks;

		// we prefer whole blocks, but there are other peers
		// downloading from this piece, add it as backups
		if (prefer_whole_pieces > 0 && !exclusive_active)
		{
			if (int(backup_blocks2.size()) >= num_blocks)
				return num_blocks;

			for (int j = 0; j < num_blocks_in_piece; ++j)
			{
				// ignore completed blocks and already requested blocks
				block_info const& info = dp.info[j];
				TORRENT_ASSERT(info.piece_index == dp.index);
				if (info.state != block_info::state_none) continue;
				backup_blocks2.push_back(piece_block(dp.index, j));
			}
			return num_blocks;
		}

		for (int j = 0; j < num_blocks_in_piece; ++j)
		{
			// ignore completed blocks and already requested blocks
			block_info const& info = dp.info[j];
			TORRENT_ASSERT(info.piece_index == dp.index);
			if (info.state != block_info::state_none) continue;

			// if the piece is fast and the peer is slow, or vice versa,
			// add the block as a backup.
			// override this behavior if all the other blocks
			// have been requested from the same peer or
			// if the state of the piece is none (the
			// piece will in that case change state).
			if (dp.state != none && dp.state != speed
				&& !exclusive_active && (options & speed_affinity))
			{
				if (abs(dp.state - speed) == 1)
				{
					// don't pick too many back-up blocks
					if (int(backup_blocks.size()) >= num_blocks) return num_blocks;
					backup_blocks.push_back(piece_block(dp.index, j));
				}
				else
				{
					// don't pick too many back-up blocks
					if (int(backup_blocks2.size()) >= num_blocks) return num_blocks;
					backup_blocks2.push_back(piece_block(dp.index, j));
				}
				continue;
			}
			
			// this block is interesting (we don't have it
			// yet).
			interesting_blocks.push_back(piece_block(dp.index, j));
			// we have found a block that's free to download
			num_blocks--;
			// if we prefer whole pieces, continue picking from this
			// piece even though we have num_blocks
			if (prefer_whole_pieces > 0) continue;
			TORRENT_ASSERT(num_blocks >= 0);
			if (num_blocks <= 0) return num_blocks;
		}
	
		TORRENT_ASSERT(num_blocks >= 0 || prefer_whole_pieces > 0);

		if (num_blocks <= 0) return 0;
		if (options & on_parole) return num_blocks;

		if (int(backup_blocks.size()) >= num_blocks) return num_blocks;

#ifdef TORRENT_DEBUG
		verify_pick(backup_blocks, pieces);
#endif
		return num_blocks;
	}
	
	std::pair<int, int> piece_picker::expand_piece(int piece, int whole_pieces
		, bitfield const& have) const
	{
		if (whole_pieces == 0) return std::make_pair(piece, piece + 1);

		int start = piece - 1;
		int lower_limit = piece - whole_pieces;
		if (lower_limit < -1) lower_limit = -1;
		while (start > lower_limit
			&& can_pick(start, have))
			--start;
		++start;
		TORRENT_ASSERT(start >= 0);
		int end = piece + 1;
		int upper_limit = start + whole_pieces;
		if (upper_limit > int(m_piece_map.size())) upper_limit = int(m_piece_map.size());
		while (end < upper_limit
			&& can_pick(end, have))
			++end;
		return std::make_pair(start, end);
	}

	bool piece_picker::is_piece_finished(int index) const
	{
		TORRENT_ASSERT(index < (int)m_piece_map.size());
		TORRENT_ASSERT(index >= 0);

		if (m_piece_map[index].downloading == 0)
		{
			TORRENT_ASSERT(find_dl_piece(index) == m_downloads.end());
			return false;
		}
		std::vector<downloading_piece>::const_iterator i = find_dl_piece(index);
		TORRENT_ASSERT(i != m_downloads.end());
		TORRENT_ASSERT((int)i->finished <= m_blocks_per_piece);
		int max_blocks = blocks_in_piece(index);
		if (int(i->finished) + int(i->writing) < max_blocks) return false;
		TORRENT_ASSERT(int(i->finished) + int(i->writing) == max_blocks);

#ifdef TORRENT_DEBUG
		for (int k = 0; k < max_blocks; ++k)
		{
			TORRENT_ASSERT(i->info[k].piece_index == index);
			TORRENT_ASSERT(i->info[k].state == block_info::state_finished
				|| i->info[k].state == block_info::state_writing);
		}
#endif

		return true;
	}

	std::vector<piece_picker::downloading_piece>::iterator piece_picker::find_dl_piece(int index)
	{
//		return std::find_if(m_downloads.begin(), m_downloads.end(), has_index(index));
		downloading_piece cmp;
		cmp.index = index;
		std::vector<piece_picker::downloading_piece>::iterator i = std::lower_bound(
			m_downloads.begin(), m_downloads.end(), cmp);
		if (i == m_downloads.end()) return i;
		if (i->index == index) return i;
		return m_downloads.end();
	}

	std::vector<piece_picker::downloading_piece>::const_iterator piece_picker::find_dl_piece(int index) const
	{
//		return std::find_if(m_downloads.begin(), m_downloads.end(), has_index(index));
		downloading_piece cmp;
		cmp.index = index;
		std::vector<piece_picker::downloading_piece>::const_iterator i = std::lower_bound(
			m_downloads.begin(), m_downloads.end(), cmp);
		if (i == m_downloads.end()) return i;
		if (i->index == index) return i;
		return m_downloads.end();
	}

	void piece_picker::update_full(downloading_piece& dp)
	{
		int num_blocks = blocks_in_piece(dp.index);
		m_piece_map[dp.index].full = dp.requested + dp.finished + dp.writing == num_blocks;
	}

	bool piece_picker::is_requested(piece_block block) const
	{
		TORRENT_ASSERT(block.piece_index >= 0);
		TORRENT_ASSERT(block.block_index >= 0);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());

		if (m_piece_map[block.piece_index].downloading == 0) return false;
		std::vector<downloading_piece>::const_iterator i = find_dl_piece(block.piece_index);

		TORRENT_ASSERT(i != m_downloads.end());
		TORRENT_ASSERT(i->info[block.block_index].piece_index == block.piece_index);
		return i->info[block.block_index].state == block_info::state_requested;
	}

	bool piece_picker::is_downloaded(piece_block block) const
	{
		TORRENT_ASSERT(block.piece_index >= 0);
		TORRENT_ASSERT(block.block_index >= 0);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());

		if (m_piece_map[block.piece_index].index == piece_pos::we_have_index) return true;
		if (m_piece_map[block.piece_index].downloading == 0) return false;
		std::vector<downloading_piece>::const_iterator i = find_dl_piece(block.piece_index);
		TORRENT_ASSERT(i != m_downloads.end());
		TORRENT_ASSERT(i->info[block.block_index].piece_index == block.piece_index);
		return i->info[block.block_index].state == block_info::state_finished
			|| i->info[block.block_index].state == block_info::state_writing;
	}

	bool piece_picker::is_finished(piece_block block) const
	{
		TORRENT_ASSERT(block.piece_index >= 0);
		TORRENT_ASSERT(block.block_index >= 0);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());

		if (m_piece_map[block.piece_index].index == piece_pos::we_have_index) return true;
		if (m_piece_map[block.piece_index].downloading == 0) return false;
		std::vector<downloading_piece>::const_iterator i = find_dl_piece(block.piece_index);
		TORRENT_ASSERT(i != m_downloads.end());
		TORRENT_ASSERT(i->info[block.block_index].piece_index == block.piece_index);
		return i->info[block.block_index].state == block_info::state_finished;
	}

	bool piece_picker::mark_as_downloading(piece_block block
		, void* peer, piece_state_t state)
	{
		TORRENT_ASSERT(peer == 0 || static_cast<policy::peer*>(peer)->in_use);
		TORRENT_ASSERT(state != piece_picker::none);
		TORRENT_ASSERT(block.piece_index >= 0);
		TORRENT_ASSERT(block.block_index >= 0);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());
		TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));
		TORRENT_ASSERT(!m_piece_map[block.piece_index].have());

		piece_pos& p = m_piece_map[block.piece_index];
		if (p.downloading == 0)
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
			int prio = p.priority(this);
			TORRENT_ASSERT(prio < int(m_priority_boundries.size())
				|| m_dirty);
			p.downloading = 1;
			if (prio >= 0 && !m_dirty) update(prio, p.index);

			downloading_piece& dp = add_download_piece(block.piece_index);
			dp.state = state;
			block_info& info = dp.info[block.block_index];
			TORRENT_ASSERT(info.piece_index == block.piece_index);
			info.state = block_info::state_requested;
			info.peer = peer;
			info.num_peers = 1;
			++dp.requested;
			update_full(dp);
		}
		else
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
			std::vector<downloading_piece>::iterator i = find_dl_piece(block.piece_index);
			TORRENT_ASSERT(i != m_downloads.end());
			block_info& info = i->info[block.block_index];
			TORRENT_ASSERT(info.piece_index == block.piece_index);
			if (info.state == block_info::state_writing
				|| info.state == block_info::state_finished)
				return false;
			TORRENT_ASSERT(info.state == block_info::state_none
				|| (info.state == block_info::state_requested
					&& (info.num_peers > 0)));
			info.peer = peer;
			if (info.state != block_info::state_requested)
			{
				info.state = block_info::state_requested;
				++i->requested;
				update_full(*i);
			}
			++info.num_peers;
			if (i->state == none) i->state = state;
		}
		return true;
	}

	int piece_picker::num_peers(piece_block block) const
	{
		TORRENT_ASSERT(block.piece_index >= 0);
		TORRENT_ASSERT(block.block_index >= 0);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());
		TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));

		piece_pos const& p = m_piece_map[block.piece_index];
		if (!p.downloading) return 0;

		std::vector<downloading_piece>::const_iterator i = find_dl_piece(block.piece_index);
		TORRENT_ASSERT(i != m_downloads.end());

		block_info const& info = i->info[block.block_index];
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

	bool piece_picker::mark_as_writing(piece_block block, void* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif

		TORRENT_ASSERT(peer == 0 || static_cast<policy::peer*>(peer)->in_use);

		TORRENT_ASSERT(block.piece_index >= 0);
		TORRENT_ASSERT(block.block_index >= 0);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());
		TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));

		piece_pos& p = m_piece_map[block.piece_index];
		if (p.downloading == 0)
		{
			// if we already have this piece, just ignore this
			if (have_piece(block.piece_index)) return false;

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
			int prio = p.priority(this);
			TORRENT_ASSERT(prio < int(m_priority_boundries.size())
				|| m_dirty);
			p.downloading = 1;
			// prio being -1 can happen if a block is requested before
			// the piece priority was set to 0
			if (prio >= 0 && !m_dirty) update(prio, p.index);

			downloading_piece& dp = add_download_piece(block.piece_index);
			dp.state = none;
			block_info& info = dp.info[block.block_index];
			TORRENT_ASSERT(info.piece_index == block.piece_index);
			info.state = block_info::state_writing;
			info.peer = peer;
			info.num_peers = 0;
			dp.writing = 1;
			update_full(dp);
//			sort_piece(m_downloads.end()-1);
		}
		else
		{
			std::vector<downloading_piece>::iterator i = find_dl_piece(block.piece_index);
			TORRENT_ASSERT(i != m_downloads.end());
			block_info& info = i->info[block.block_index];

			TORRENT_ASSERT(info.piece_index == block.piece_index);

			info.peer = peer;
			if (info.state == block_info::state_requested) --i->requested;
			TORRENT_ASSERT(i->requested >= 0);
			if (info.state == block_info::state_writing
				|| info.state == block_info::state_finished)
				return false;

			++i->writing;
			info.state = block_info::state_writing;
			TORRENT_ASSERT(info.piece_index == block.piece_index);

			// all other requests for this block should have been
			// cancelled now
			info.num_peers = 0;

			if (i->requested == 0)
			{
				// there are no blocks requested in this piece.
				// remove the fast/slow state from it
				i->state = none;
			}
//			sort_piece(i);
		}
		return true;
	}

	void piece_picker::write_failed(piece_block block)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

		std::vector<downloading_piece>::iterator i = find_dl_piece(block.piece_index);
		TORRENT_ASSERT(i != m_downloads.end());
		if (i == m_downloads.end()) return;

		block_info& info = i->info[block.block_index];
		TORRENT_ASSERT(info.piece_index == block.piece_index);
		TORRENT_ASSERT(info.state == block_info::state_writing);
		TORRENT_ASSERT(info.num_peers == 0);

		TORRENT_ASSERT(i->writing > 0);
		TORRENT_ASSERT(info.state == block_info::state_writing);

		if (info.state == block_info::state_finished) return;
		if (info.state == block_info::state_writing) --i->writing;

		info.peer = 0;

		info.state = block_info::state_none;

		update_full(*i);

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
		else
		{
//			sort_piece(i);
		}
	}

	void piece_picker::mark_as_finished(piece_block block, void* peer)
	{
		TORRENT_ASSERT(peer == 0 || static_cast<policy::peer*>(peer)->in_use);
		TORRENT_ASSERT(block.piece_index >= 0);
		TORRENT_ASSERT(block.block_index >= 0);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());
		TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));

		piece_pos& p = m_piece_map[block.piece_index];

		if (p.downloading == 0)
		{
			// if we already have this piece, just ignore this
			if (have_piece(block.piece_index)) return;

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
			
			TORRENT_ASSERT(peer == 0);
			int prio = p.priority(this);
			TORRENT_ASSERT(prio < int(m_priority_boundries.size())
				|| m_dirty);
			p.downloading = 1;
			if (prio >= 0 && !m_dirty) update(prio, p.index);

			downloading_piece& dp = add_download_piece(block.piece_index);
			dp.state = none;
			block_info& info = dp.info[block.block_index];
			TORRENT_ASSERT(info.piece_index == block.piece_index);
			info.peer = peer;
			TORRENT_ASSERT(info.state == block_info::state_none);
			TORRENT_ASSERT(info.num_peers == 0);
			if (info.state != block_info::state_finished)
			{
				++dp.finished;
//				sort_piece(m_downloads.end() - 1);
			}
			info.state = block_info::state_finished;
		}
		else
		{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
			TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif
			
			std::vector<downloading_piece>::iterator i = find_dl_piece(block.piece_index);
			TORRENT_ASSERT(i != m_downloads.end());
			block_info& info = i->info[block.block_index];
			TORRENT_ASSERT(info.piece_index == block.piece_index);

			if (info.state == block_info::state_finished) return;

			TORRENT_ASSERT(info.num_peers == 0);
			info.peer = peer;
			TORRENT_ASSERT(info.state == block_info::state_writing
				|| peer == 0);
			TORRENT_ASSERT(i->writing >= 0);
			++i->finished;
			if (info.state == block_info::state_writing)
			{
				--i->writing;
				info.state = block_info::state_finished;
			}
			else
			{
				TORRENT_ASSERT(info.state == block_info::state_none);
				info.state = block_info::state_finished;
//				sort_piece(i);
			}
		}
	}

	void piece_picker::get_downloaders(std::vector<void*>& d, int index) const
	{
		TORRENT_ASSERT(index >= 0 && index <= (int)m_piece_map.size());
		std::vector<downloading_piece>::const_iterator i = find_dl_piece(index);
		TORRENT_ASSERT(i != m_downloads.end());

		d.clear();
		for (int j = 0, end(blocks_in_piece(index)); j != end; ++j)
		{
			TORRENT_ASSERT(i->info[j].peer == 0 || static_cast<policy::peer*>(i->info[j].peer)->in_use);
			d.push_back(i->info[j].peer);
		}
	}

	void* piece_picker::get_downloader(piece_block block) const
	{
		std::vector<downloading_piece>::const_iterator i = find_dl_piece(block.piece_index);

		if (i == m_downloads.end()) return 0;

		TORRENT_ASSERT(block.block_index >= 0);

		TORRENT_ASSERT(i->info[block.block_index].piece_index == block.piece_index);
		if (i->info[block.block_index].state == block_info::state_none)
			return 0;

		void* peer = i->info[block.block_index].peer;
		TORRENT_ASSERT(peer == 0 || static_cast<policy::peer*>(peer)->in_use);
		return peer;
	}

	// this is called when a request is rejected or when
	// a peer disconnects. The piece might be in any state
	void piece_picker::abort_download(piece_block block, void* peer)
	{
#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
#endif

		TORRENT_ASSERT(peer == 0 || static_cast<policy::peer*>(peer)->in_use);

		TORRENT_ASSERT(block.piece_index >= 0);
		TORRENT_ASSERT(block.block_index >= 0);
		TORRENT_ASSERT(block.piece_index < m_piece_map.size());
		TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));

		if (m_piece_map[block.piece_index].downloading == 0)
		{
			TORRENT_ASSERT(find_dl_piece(block.piece_index) == m_downloads.end());
			return;
		}

		std::vector<downloading_piece>::iterator i = find_dl_piece(block.piece_index);
		TORRENT_ASSERT(i != m_downloads.end());

		block_info& info = i->info[block.block_index];
		TORRENT_ASSERT(info.peer == 0 || static_cast<policy::peer*>(info.peer)->in_use);
		TORRENT_ASSERT(info.piece_index == block.piece_index);

		TORRENT_ASSERT(info.state != block_info::state_none);

		if (info.state == block_info::state_finished
			|| info.state == block_info::state_none
			|| info.state == block_info::state_writing)
			return;

		if (info.state == block_info::state_requested)
		{
			TORRENT_ASSERT(info.num_peers > 0);
			if (info.num_peers > 0) --info.num_peers;
			if (info.peer == peer) info.peer = 0;

			TORRENT_ASSERT(int(block.block_index) < blocks_in_piece(block.piece_index));

			// if there are other peers, leave the block requested
			if (info.num_peers > 0) return;

			// clear the downloader of this block
			info.peer = 0;

			// clear this block as being downloaded
			info.state = block_info::state_none;
			--i->requested;
			update_full(*i);
		}

		// if there are no other blocks in this piece
		// that's being downloaded, remove it from the list
		if (i->requested + i->finished + i->writing == 0)
		{
			piece_pos& p = m_piece_map[block.piece_index];
			int prev_prio = p.priority(this);
			TORRENT_ASSERT(prev_prio < int(m_priority_boundries.size())
				|| m_dirty);
			erase_download_piece(i);
			if (!m_dirty)
			{
				int prio = p.priority(this);
				if (prev_prio == -1 && prio >= 0) add(block.piece_index);
				else if (prev_prio >= 0) update(prev_prio, p.index);
			}

			TORRENT_ASSERT(find_dl_piece(block.piece_index) == m_downloads.end());
		}
		else if (i->requested == 0)
		{
			// there are no blocks requested in this piece.
			// remove the fast/slow state from it
			i->state = none;
		}
	}

	int piece_picker::unverified_blocks() const
	{
		int counter = 0;
		for (std::vector<downloading_piece>::const_iterator i = m_downloads.begin();
			i != m_downloads.end(); ++i)
		{
			counter += (int)i->finished;
		}
		return counter;
	}

}

