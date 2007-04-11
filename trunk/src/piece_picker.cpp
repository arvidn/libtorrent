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

// non-standard header, is_sorted()
//#include <algo.h>

#include "libtorrent/piece_picker.hpp"
#include "libtorrent/aux_/session_impl.hpp"

#ifndef NDEBUG
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/torrent.hpp"
#endif

#define TORRENT_PIECE_PICKER_INVARIANT_CHECK INVARIANT_CHECK
//#define TORRENT_PIECE_PICKER_INVARIANT_CHECK

namespace libtorrent
{

	piece_picker::piece_picker(int blocks_per_piece, int total_num_blocks)
		: m_piece_info(2)
		, m_piece_map((total_num_blocks + blocks_per_piece-1) / blocks_per_piece)
		, m_num_filtered(0)
		, m_num_have_filtered(0)
		, m_sequenced_download_threshold(100)
	{
		assert(blocks_per_piece > 0);
		assert(total_num_blocks >= 0);
#ifndef NDEBUG
		m_files_checked_called = false;
#endif

		// the piece index is stored in 20 bits, which limits the allowed
		// number of pieces somewhat
		if (m_piece_map.size() >= piece_pos::we_have_index)
			throw std::runtime_error("too many pieces in torrent");
		
		m_blocks_per_piece = blocks_per_piece;
		m_blocks_in_last_piece = total_num_blocks % blocks_per_piece;
		if (m_blocks_in_last_piece == 0) m_blocks_in_last_piece = blocks_per_piece;

		assert(m_blocks_per_piece <= max_blocks_per_piece);
		assert(m_blocks_in_last_piece <= m_blocks_per_piece);
		assert(m_blocks_in_last_piece <= max_blocks_per_piece);

		// allocate the piece_map to cover all pieces
		// and make them invalid (as if though we already had every piece)
		std::fill(m_piece_map.begin(), m_piece_map.end()
			, piece_pos(0, piece_pos::we_have_index));
	}

	// pieces is a bitmask with the pieces we have
	void piece_picker::files_checked(
		const std::vector<bool>& pieces
		, const std::vector<downloading_piece>& unfinished)
	{
#ifndef NDEBUG
		m_files_checked_called = true;
#endif
		for (std::vector<bool>::const_iterator i = pieces.begin();
			i != pieces.end(); ++i)
		{
			if (*i) continue;
			int index = static_cast<int>(i - pieces.begin());
			m_piece_map[index].index = 0;
			if (m_piece_map[index].filtered())
			{
				++m_num_filtered;
				--m_num_have_filtered;
			}
		}

		// if we have fast resume info
		// use it
		if (!unfinished.empty())
		{
			for (std::vector<downloading_piece>::const_iterator i
				= unfinished.begin(); i != unfinished.end(); ++i)
			{
				tcp::endpoint peer;
				for (int j = 0; j < m_blocks_per_piece; ++j)
				{
					if (i->finished_blocks[j])
						mark_as_finished(piece_block(i->index, j), peer);
				}
				if (is_piece_finished(i->index))
				{
					// TODO: handle this case by verifying the
					// piece and either accept it or discard it
					assert(false);
				}
			}
		}
	}

	void piece_picker::set_sequenced_download_threshold(
		int sequenced_download_threshold)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

		if (sequenced_download_threshold == m_sequenced_download_threshold)
			return;
			
		assert(sequenced_download_threshold > 0);
			
		int old_limit = m_sequenced_download_threshold;
		m_sequenced_download_threshold = sequenced_download_threshold;

		for (std::vector<piece_pos>::iterator i = m_piece_map.begin()
			, end(m_piece_map.end()); i != end; ++i)
		{
			if (i->priority(old_limit) != i->priority(m_sequenced_download_threshold))
			{
				piece_pos& p = *i;
				int prev_priority = p.priority(old_limit);
				if (prev_priority == 0) continue;
				move(prev_priority, p.index);
			}
		}
		
		typedef std::vector<int> info_t;

		if (old_limit < sequenced_download_threshold)
		{
			// the threshold was incremented, in case
			// the previous max availability was reached
			// we need to shuffle that bucket, if not, we
			// don't have to do anything
			if (int(m_piece_info.size()) > old_limit)
			{
				info_t& in = m_piece_info[old_limit];
				std::random_shuffle(in.begin(), in.end());
				int c = 0;
				for (info_t::iterator i = in.begin()
					, end(in.end()); i != end; ++i)
				{
					m_piece_map[*i].index = c++;
					assert(m_piece_map[*i].priority(old_limit) == old_limit);
				}
			}
		}
		else if (int(m_piece_info.size()) > sequenced_download_threshold)
		{
			info_t& in = m_piece_info[sequenced_download_threshold];
			std::sort(in.begin(), in.end());
			int c = 0;
			for (info_t::iterator i = in.begin()
				, end(in.end()); i != end; ++i)
			{
				m_piece_map[*i].index = c++;
				assert(m_piece_map[*i].priority(
					sequenced_download_threshold) == sequenced_download_threshold);
			}
		}
	}

#ifndef NDEBUG

	void piece_picker::check_invariant(const torrent* t) const
	{
		assert(sizeof(piece_pos) == 4);

		assert(m_piece_info.empty() || m_piece_info[0].empty());

		if (t != 0)
			assert((int)m_piece_map.size() == t->torrent_file().num_pieces());

		int num_filtered = 0;
		int num_have_filtered = 0;
		for (std::vector<piece_pos>::const_iterator i = m_piece_map.begin();
			i != m_piece_map.end(); ++i)
		{
			int index = static_cast<int>(i - m_piece_map.begin());
			if (i->filtered())
			{
				if (i->index != piece_pos::we_have_index)
					++num_filtered;
				else
					++num_have_filtered;
			}
#if 0
			if (t != 0)
			{
				int actual_peer_count = 0;
				for (torrent::const_peer_iterator peer = t->begin();
					peer != t->end(); ++peer)
				{
					if (peer->second->has_piece(index)) actual_peer_count++;
				}

				assert((int)i->peer_count == actual_peer_count);
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
					assert(num_downloaders == 1);
				}
				else
				{
					assert(num_downloaders == 0);
				}
*/
			}
#endif
/*
			if (i->index == piece_pos::we_have_index)
			{
				assert(t == 0 || t->have_piece(index));
				assert(i->downloading == 0);

				// make sure there's no entry
				// with this index. (there shouldn't
				// be since the piece_map is piece_pos::we_have_index)
				for (int i = 0; i < int(m_piece_info.size()); ++i)
				{
					for (int j = 0; j < int(m_piece_info[i].size()); ++j)
					{
						assert(m_piece_info[i][j] != index);
					}
				}
			}
			else
			{
				if (t != 0)
					assert(!t->have_piece(index));

				int prio = i->priority(m_sequenced_download_threshold);
				if (prio > 0)
				{
					const std::vector<int>& vec = m_piece_info[prio];
					assert (i->index < vec.size());
					assert(vec[i->index] == index);
				}

				for (int k = 0; k < int(m_piece_info.size()); ++k)
				{
					for (int j = 0; j < int(m_piece_info[k].size()); ++j)
					{
						assert(int(m_piece_info[k][j]) != index
							|| (prio > 0 && prio == k && int(i->index) == j));
					}
				}
			}
*/
			int count = std::count_if(m_downloads.begin(), m_downloads.end()
				, has_index(index));
			if (i->downloading == 1)
			{
				assert(count == 1);
			}
			else
			{
				assert(count == 0);
			}
		}
		assert(num_filtered == m_num_filtered);
		assert(num_have_filtered == m_num_have_filtered);
	}
#endif

	float piece_picker::distributed_copies() const
	{
		const float num_pieces = static_cast<float>(m_piece_map.size());

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
				min_availability = i->peer_count;
				fraction_part += integer_part;
				integer_part = 1;
			}
			else if (peer_count == min_availability)
			{
				++integer_part;
			}
			else
			{
				assert(peer_count > min_availability);
				++fraction_part;
			}
		}
		assert(integer_part + fraction_part == num_pieces);
		return float(min_availability) + (fraction_part / num_pieces);
	}

	void piece_picker::add(int index)
	{
		assert(index >= 0);
		assert(index < int(m_piece_map.size()));
		piece_pos& p = m_piece_map[index];
		assert(!p.filtered());
		assert(!p.have());

		int priority = p.priority(m_sequenced_download_threshold);
		assert(priority > 0);
		if (int(m_piece_info.size()) <= priority)
			m_piece_info.resize(priority + 1);

		assert(int(m_piece_info.size()) > priority);

		if (is_ordered(priority))
		{
			// the piece should be inserted ordered, not randomly
			std::vector<int>& v = m_piece_info[priority];
//			assert(is_sorted(v.begin(), v.end()/*, std::greater<int>()*/));
			std::vector<int>::iterator i = std::lower_bound(v.begin(), v.end()
				, index/*, std::greater<int>()*/);
			p.index = i - v.begin();
			v.insert(i, index);
			i = v.begin() + p.index + 1;
			for (;i != v.end(); ++i)
			{
				++m_piece_map[*i].index;
				assert(v[m_piece_map[*i].index] == *i);
			}
//			assert(is_sorted(v.begin(), v.end()/*, std::greater<int>()*/));
		}
		else if (m_piece_info[priority].size() < 2)
		{
			p.index = m_piece_info[priority].size();
			m_piece_info[priority].push_back(index);
		}
		else
		{
			// find a random position in the destination vector where we will place
			// this entry.
			int dst_index = rand() % m_piece_info[priority].size();
			
			// copy the entry at that position to the back
			m_piece_map[m_piece_info[priority][dst_index]].index
				= m_piece_info[priority].size();
			m_piece_info[priority].push_back(m_piece_info[priority][dst_index]);

			// and then replace the one at dst_index with the one we're moving.
			// this procedure is to make sure there's no ordering when pieces
			// are moved in sequenced order.
			p.index = dst_index;
			m_piece_info[priority][p.index] = index;
		}
	}

	// will update the piece with the given properties (priority, elem_index)
	// to place it at the correct position in the vectors.
	void piece_picker::move(int priority, int elem_index)
	{
		assert(priority > 0);
		assert(elem_index >= 0);
		assert(m_files_checked_called);

		assert(int(m_piece_info.size()) > priority);
		assert(int(m_piece_info[priority].size()) > elem_index);

		int index = m_piece_info[priority][elem_index];
		// update the piece_map
		piece_pos& p = m_piece_map[index];
		assert(int(p.index) == elem_index || p.have());		

		int new_priority = p.priority(m_sequenced_download_threshold);

		if (new_priority == priority) return;

		if (int(m_piece_info.size()) <= new_priority
			&& new_priority > 0)
		{
			m_piece_info.resize(new_priority + 1);
			assert(int(m_piece_info.size()) > new_priority);
		}

		if (new_priority == 0)
		{
			// this means the piece should not have an entry
		}
		else if (is_ordered(new_priority))
		{
			// the piece should be inserted ordered, not randomly
			std::vector<int>& v = m_piece_info[new_priority];
//			assert(is_sorted(v.begin(), v.end()/*, std::greater<int>()*/));
			std::vector<int>::iterator i = std::lower_bound(v.begin(), v.end()
				, index/*, std::greater<int>()*/);
			p.index = i - v.begin();
			v.insert(i, index);
			i = v.begin() + p.index + 1;
			for (;i != v.end(); ++i)
			{
				++m_piece_map[*i].index;
				assert(v[m_piece_map[*i].index] == *i);
			}
//			assert(is_sorted(v.begin(), v.end()/*, std::greater<int>()*/));
		}
		else if (m_piece_info[new_priority].size() < 2)
		{
			p.index = m_piece_info[new_priority].size();
			m_piece_info[new_priority].push_back(index);
		}
		else
		{
			// find a random position in the destination vector where we will place
			// this entry.
			int dst_index = rand() % m_piece_info[new_priority].size();
			
			// copy the entry at that position to the back
			m_piece_map[m_piece_info[new_priority][dst_index]].index
				= m_piece_info[new_priority].size();
			m_piece_info[new_priority].push_back(m_piece_info[new_priority][dst_index]);

			// and then replace the one at dst_index with the one we're moving.
			// this procedure is to make sure there's no ordering when pieces
			// are moved in sequenced order.
			p.index = dst_index;
			m_piece_info[new_priority][p.index] = index;
		}
		assert(new_priority == 0 || p.index < m_piece_info[p.priority(m_sequenced_download_threshold)].size());
		assert(new_priority == 0 || m_piece_info[p.priority(m_sequenced_download_threshold)][p.index] == index);

		if (is_ordered(priority))
		{
			// remove the element from the source vector and preserve the order
			std::vector<int>& v = m_piece_info[priority];
			v.erase(v.begin() + elem_index);
			for (std::vector<int>::iterator i = v.begin() + elem_index;
				i != v.end(); ++i)
			{
				--m_piece_map[*i].index;
				assert(v[m_piece_map[*i].index] == *i);
			}
		}
		else
		{
			// this will remove elem from the source vector without
			// preserving order, but the order is random anyway
			int replace_index = m_piece_info[priority][elem_index] = m_piece_info[priority].back();
			if (index != replace_index)
			{
				// update the entry we moved from the back
				m_piece_map[replace_index].index = elem_index;

				assert(int(m_piece_info[priority].size()) > elem_index);
				// this may not necessarily be the case. If we've just updated the threshold and are updating
				// the piece map
//				assert((int)m_piece_map[replace_index].priority(m_sequenced_download_threshold) == priority);
				assert(int(m_piece_map[replace_index].index) == elem_index);
				assert(m_piece_info[priority][elem_index] == replace_index);
			}
			else
			{
				assert(int(m_piece_info[priority].size()) == elem_index+1);
			}

			m_piece_info[priority].pop_back();
		}
	}
/*
	void piece_picker::remove(int priority, int elem_index)
	{
		assert(priority > 0);
		assert(elem_index >= 0);
		assert(m_files_checked_called);

		assert(int(m_piece_info.size()) > priority);
		assert(int(m_piece_info[priority].size()) > elem_index);

		int index = m_piece_info[priority][elem_index];

		piece_pos& p = m_piece_map[index];

		if (p.downloading)
		{
			std::vector<downloading_piece>::iterator i
				= std::find_if(m_downloads.begin(),
				m_downloads.end(),
				has_index(index));
			assert(i != m_downloads.end());
			m_downloads.erase(i);
		}

		p.downloading = 0;
		if (is_ordered(priority))
		{
			std::vector<int>& v = m_piece_info[priority];
			std::vector<int>::iterator i = v.begin() + elem_index;
			v.erase(i);
			i = v.begin() + elem_index;
			for (; i != v.end(); ++i)
			{
				--m_piece_map[*i].index;
				assert(v[m_piece_map[*i].index] == *i);
			}
		}
		else
		{
			// this will remove elem from the vector without
			// preserving order
			index = m_piece_info[priority][elem_index] = m_piece_info[priority].back();
			// update the entry we moved from the back
			m_piece_map[index].index = elem_index;
			m_piece_info[priority].pop_back();
		}
	}
*/
	void piece_picker::restore_piece(int index)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

		assert(index >= 0);
		assert(index < (int)m_piece_map.size());
		assert(m_files_checked_called);

		assert(m_piece_map[index].downloading == 1);

		std::vector<downloading_piece>::iterator i
			= std::find_if(m_downloads.begin(),
			m_downloads.end(),
			has_index(index));
		assert(i != m_downloads.end());
		m_downloads.erase(i);

		piece_pos& p = m_piece_map[index];
		int prev_priority = p.priority(m_sequenced_download_threshold);
		p.downloading = 0;
		int new_priority = p.priority(m_sequenced_download_threshold);

		if (new_priority == prev_priority) return;

		if (prev_priority == 0)
		{
			add(index);
		}
		else
		{
			move(prev_priority, p.index);
		}
	}

	void piece_picker::inc_refcount(int i)
	{
//		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
		assert(i >= 0);
		assert(i < (int)m_piece_map.size());
		assert(m_files_checked_called);

		piece_pos& p = m_piece_map[i];
		int index = p.index;
		int prev_priority = p.priority(m_sequenced_download_threshold);

		assert(p.peer_count < piece_pos::max_peer_count);
		p.peer_count++;
		assert(p.peer_count != 0);	

		// if we have the piece or if it's filtered
		// we don't have to move any entries in the piece_info vector
		if (p.priority(m_sequenced_download_threshold) == prev_priority) return;

		if (prev_priority == 0)
		{
			add(i);
		}
		else
		{
			move(prev_priority, index);
		}

#ifndef NDEBUG
//		integrity_check();
#endif
		return;
	}

	void piece_picker::dec_refcount(int i)
	{
//		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

		assert(m_files_checked_called);
		assert(i >= 0);
		assert(i < (int)m_piece_map.size());

		piece_pos& p = m_piece_map[i];
		int prev_priority = p.priority(m_sequenced_download_threshold);
		int index = p.index;
		assert(p.peer_count > 0);

		if (p.peer_count > 0)
			p.peer_count--;

		if (p.priority(m_sequenced_download_threshold) == prev_priority) return;

		move(prev_priority, index);
	}

	// this is used to indicate that we succesfully have
	// downloaded a piece, and that no further attempts
	// to pick that piece should be made. The piece will
	// be removed from the available piece list.
	void piece_picker::we_have(int index)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
		assert(index >= 0);
		assert(index < (int)m_piece_map.size());

		piece_pos& p = m_piece_map[index];
		int info_index = p.index;
		int priority = p.priority(m_sequenced_download_threshold);

		assert(p.downloading == 1);
		assert(!p.have());

		std::vector<downloading_piece>::iterator i
			= std::find_if(m_downloads.begin()
			, m_downloads.end()
			, has_index(index));
		assert(i != m_downloads.end());
		m_downloads.erase(i);
		p.downloading = 0;

		assert(std::find_if(m_downloads.begin(), m_downloads.end()
			, has_index(index)) == m_downloads.end());

		if (p.filtered())
		{
			--m_num_filtered;
			++m_num_have_filtered;
		}
		if (p.have()) return;
		p.set_have();
		if (priority == 0) return;
		assert(p.priority(m_sequenced_download_threshold) == 0);
		move(priority, info_index);
	}


	void piece_picker::set_piece_priority(int index, int new_piece_priority)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
		assert(new_piece_priority >= 0);
		assert(new_piece_priority <= 7);
		assert(index >= 0);
		assert(index < (int)m_piece_map.size());
		
		piece_pos& p = m_piece_map[index];

		// if the priority isn't changed, don't do anything
		if (new_piece_priority == int(p.piece_priority)) return;
		
		int prev_priority = p.priority(m_sequenced_download_threshold);

		if (new_piece_priority == piece_pos::filter_priority
			&& p.piece_priority != piece_pos::filter_priority)
		{
			// the piece just got filtered
			if (p.have()) ++m_num_have_filtered;
			else ++m_num_filtered;

			if (p.downloading)
			{
				std::vector<downloading_piece>::iterator i
					= std::find_if(m_downloads.begin(),
					m_downloads.end(),
					has_index(index));
				assert(i != m_downloads.end());
				m_downloads.erase(i);
				assert(std::find_if(m_downloads.begin(), m_downloads.end()
					, has_index(index)) == m_downloads.end());
			}
			p.downloading = 0;
		}
		else if (new_piece_priority != piece_pos::filter_priority
			&& p.piece_priority == piece_pos::filter_priority)
		{
			// the piece just got unfiltered
			if (p.have()) --m_num_have_filtered;
			else --m_num_filtered;
		}
		assert(m_num_filtered >= 0);
		assert(m_num_have_filtered >= 0);
		
		p.piece_priority = new_piece_priority;
		int new_priority = p.priority(m_sequenced_download_threshold);

		if (new_priority == prev_priority) return;
		
		if (prev_priority == 0)
		{
			add(index);
		}
		else
		{
			move(prev_priority, p.index);
		}
	}

	int piece_picker::piece_priority(int index) const
	{
		assert(index >= 0);
		assert(index < (int)m_piece_map.size());

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
	
	void piece_picker::pick_pieces(const std::vector<bool>& pieces
		, std::vector<piece_block>& interesting_blocks
		, int num_blocks, bool prefer_whole_pieces
		, tcp::endpoint peer) const
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
		assert(num_blocks > 0);
		assert(pieces.size() == m_piece_map.size());
		assert(m_files_checked_called);

		// free refers to pieces that are free to download, no one else
		// is downloading them.
		// partial is pieces that are partially being downloaded, and
		// parts of them may be free for download as well, the
		// partially downloaded pieces will be prioritized
		assert(m_piece_info.begin() != m_piece_info.end());
		// +1 is to ignore pieces that no peer has. The bucket with index 0 contains
		// pieces that 0 other peers has.
		std::vector<std::vector<int> >::const_iterator free
			= m_piece_info.begin() + 1;

		std::vector<piece_block> backup_blocks;
		
		// this loop will loop from pieces with priority 1 and up
		// until we either reach the end of the piece list or
		// has filled the interesting_blocks with num_blocks
		// blocks.

		// When prefer_whole_pieces is set (usually set when downloading from
		// fast peers) the partial pieces will not be prioritized, but actually
		// ignored as long as possible.

		while (free != m_piece_info.end())
		{
			num_blocks = add_interesting_blocks(*free, pieces
				, interesting_blocks, backup_blocks, num_blocks
				, prefer_whole_pieces, peer);
			assert(num_blocks >= 0);
			if (num_blocks == 0) return;
			++free;
		}

// TODO: what's up with this?
		if (!prefer_whole_pieces) return;
		assert(num_blocks > 0);

#ifdef TORRENT_VERBOSE_LOGGING
//		std::ofstream f("piece_picker.log", std::ios_base::app);
//		f << "backup_blocks: " << backup_blocks.size() << "\n"
//			<< "used: " << std::min(num_blocks, (int)backup_blocks.size()) << "\n----\n";
#endif

		interesting_blocks.insert(interesting_blocks.end()
			, backup_blocks.begin(), backup_blocks.begin()
			+ (std::min)(num_blocks, (int)backup_blocks.size()));
	}

	namespace
	{
		bool exclusively_requested_from(piece_picker::downloading_piece const& p
			, int num_blocks_in_piece, tcp::endpoint peer)
		{
			for (int j = 0; j < num_blocks_in_piece; ++j)
			{
				if ((p.finished_blocks[j] == 1
					|| p.requested_blocks[j] == 1)
					&& p.info[j].peer != peer
					&& p.info[j].peer != tcp::endpoint())
				{
					return false;
				}
			}
			return true;
		}
	}

	int piece_picker::add_interesting_blocks(std::vector<int> const& piece_list
		, std::vector<bool> const& pieces
		, std::vector<piece_block>& interesting_blocks
		, std::vector<piece_block>& backup_blocks
		, int num_blocks, bool prefer_whole_pieces
		, tcp::endpoint peer) const
	{
		for (std::vector<int>::const_iterator i = piece_list.begin();
			i != piece_list.end(); ++i)
		{
			assert(*i >= 0);
			assert(*i < (int)m_piece_map.size());

			// if the peer doesn't have the piece
			// skip it
			if (!pieces[*i]) continue;

			int num_blocks_in_piece = blocks_in_piece(*i);

			if (m_piece_map[*i].downloading == 1)
			{
				std::vector<downloading_piece>::const_iterator p
					= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(*i));
				assert(p != m_downloads.end());

				// this means that this partial piece has
				// been downloaded/requested partially from
				// another peer that isn't us. And since
				// we prefer whole pieces, add this piece's
				// blocks to the backup list. If the prioritized
				// blocks aren't enough, blocks from this list
				// will be picked.
				if (prefer_whole_pieces
					&& !exclusively_requested_from(*p, num_blocks_in_piece, peer))
				{
					if ((int)backup_blocks.size() >= num_blocks) continue;
					for (int j = 0; j < num_blocks_in_piece; ++j)
					{
						if (p->finished_blocks[j] == 1) continue;
						if (p->requested_blocks[j] == 1
							&& p->info[j].peer == peer) continue;
						backup_blocks.push_back(piece_block(*i, j));
					}
					continue;
				}

				for (int j = 0; j < num_blocks_in_piece; ++j)
				{
					if (p->finished_blocks[j] == 1) continue;
					if (p->requested_blocks[j] == 1
						&& p->info[j].peer == peer) continue;
					// this block is interesting (we don't have it
					// yet). But it may already have been requested
					// from another peer. We have to add it anyway
					// to allow the requester to determine if the
					// block should be requested from more than one
					// peer. If it is being downloaded, we continue
					// to look for blocks until we have num_blocks
					// blocks that have not been requested from any
					// other peer.
					interesting_blocks.push_back(piece_block(*i, j));
					if (p->requested_blocks[j] == 0)
					{
						// we have found a block that's free to download
						num_blocks--;
						if (prefer_whole_pieces) continue;
						assert(num_blocks >= 0);
						if (num_blocks == 0) return num_blocks;
					}
				}
				assert(num_blocks >= 0 || prefer_whole_pieces);
				if (num_blocks < 0) num_blocks = 0;
			}
			else
			{
				if (!prefer_whole_pieces && num_blocks_in_piece > num_blocks)
					num_blocks_in_piece = num_blocks;
				for (int j = 0; j < num_blocks_in_piece; ++j)
				{
					interesting_blocks.push_back(piece_block(*i, j));
				}
				num_blocks -= (std::min)(num_blocks_in_piece, num_blocks);
			}
			assert(num_blocks >= 0);
			if (num_blocks == 0) return num_blocks;
		}
		return num_blocks;
	}

	bool piece_picker::is_piece_finished(int index) const
	{
		assert(index < (int)m_piece_map.size());
		assert(index >= 0);

		if (m_piece_map[index].downloading == 0)
		{
			assert(std::find_if(m_downloads.begin(), m_downloads.end()
				, has_index(index)) == m_downloads.end());
			return false;
		}
		std::vector<downloading_piece>::const_iterator i
			= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(index));
		assert(i != m_downloads.end());
		assert((int)i->finished_blocks.count() <= m_blocks_per_piece);
		int max_blocks = blocks_in_piece(index);
		if ((int)i->finished_blocks.count() < max_blocks) return false;

		assert((int)i->requested_blocks.count() == max_blocks);
		return true;
	}

	bool piece_picker::is_downloading(piece_block block) const
	{
		assert(block.piece_index >= 0);
		assert(block.block_index >= 0);
		assert(block.piece_index < (int)m_piece_map.size());
		assert(block.block_index < (int)max_blocks_per_piece);

		if (m_piece_map[block.piece_index].downloading == 0) return false;
		std::vector<downloading_piece>::const_iterator i
			= std::find_if(
				m_downloads.begin()
				, m_downloads.end()
				, has_index(block.piece_index));

		assert(i != m_downloads.end());
		return i->requested_blocks[block.block_index];
	}

	bool piece_picker::is_finished(piece_block block) const
	{
		assert(block.piece_index >= 0);
		assert(block.block_index >= 0);
		assert(block.piece_index < (int)m_piece_map.size());
		assert(block.block_index < (int)max_blocks_per_piece);

		if (m_piece_map[block.piece_index].index == piece_pos::we_have_index) return true;
		if (m_piece_map[block.piece_index].downloading == 0) return false;
		std::vector<downloading_piece>::const_iterator i
			= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(block.piece_index));
		assert(i != m_downloads.end());
		return i->finished_blocks[block.block_index];
	}


	void piece_picker::mark_as_downloading(piece_block block, const tcp::endpoint& peer)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

		assert(block.piece_index >= 0);
		assert(block.block_index >= 0);
		assert(block.piece_index < (int)m_piece_map.size());
		assert(block.block_index < blocks_in_piece(block.piece_index));

		piece_pos& p = m_piece_map[block.piece_index];
		if (p.downloading == 0)
		{
			int prio = p.priority(m_sequenced_download_threshold);
			p.downloading = 1;
			move(prio, p.index);

			downloading_piece dp;
			dp.index = block.piece_index;
			dp.requested_blocks[block.block_index] = 1;
			dp.info[block.block_index].peer = peer;
			m_downloads.push_back(dp);
		}
		else
		{
			std::vector<downloading_piece>::iterator i
				= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(block.piece_index));
			assert(i != m_downloads.end());
			assert(i->requested_blocks[block.block_index] == 0);
			i->info[block.block_index].peer = peer;
			i->requested_blocks[block.block_index] = 1;
		}
	}

	void piece_picker::mark_as_finished(piece_block block, const tcp::endpoint& peer)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

		assert(block.piece_index >= 0);
		assert(block.block_index >= 0);
		assert(block.piece_index < (int)m_piece_map.size());
		assert(block.block_index < blocks_in_piece(block.piece_index));

		piece_pos& p = m_piece_map[block.piece_index];
		int prio = p.priority(m_sequenced_download_threshold);

		if (p.downloading == 0)
		{
			p.downloading = 1;
			if (prio > 0) move(prio, p.index);

			downloading_piece dp;
			dp.index = block.piece_index;
			dp.requested_blocks[block.block_index] = 1;
			dp.finished_blocks[block.block_index] = 1;
			dp.info[block.block_index].peer = peer;
			m_downloads.push_back(dp);
		}
		else
		{
			std::vector<downloading_piece>::iterator i
				= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(block.piece_index));
			assert(i != m_downloads.end());
			i->info[block.block_index].peer = peer;
			i->requested_blocks[block.block_index] = 1;
			i->finished_blocks[block.block_index] = 1;
		}
	}
/*
	void piece_picker::mark_as_finished(piece_block block, const peer_id& peer)
	{
#ifndef NDEBUG
		integrity_check();
#endif
		assert(block.piece_index >= 0);
		assert(block.block_index >= 0);
		assert(block.piece_index < m_piece_map.size());
		assert(block.block_index < blocks_in_piece(block.piece_index));

		assert(m_piece_map[block.piece_index].downloading == 1);

		std::vector<downloading_piece>::iterator i
			= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(block.piece_index));
		assert(i != m_downloads.end());
		i->finished_blocks[block.block_index] = 1;
		// the block may have been requested, then cancled
		// and requested by a peer that disconnects
		// that way we can actually receive the piece
		// without the requested bit is set.
		i->requested_blocks[block.block_index] = 1;
		i->info[block.block_index].num_downloads++;
		i->info[block.block_index].peer = peer;
#ifndef NDEBUG
		integrity_check();
#endif
	}
*/
	void piece_picker::get_downloaders(std::vector<tcp::endpoint>& d, int index) const
	{
		assert(index >= 0 && index <= (int)m_piece_map.size());
		std::vector<downloading_piece>::const_iterator i
			= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(index));
		assert(i != m_downloads.end());

		d.clear();
		for (int j = 0; j < blocks_in_piece(index); ++j)
		{
			d.push_back(i->info[j].peer);
		}
	}

	boost::optional<tcp::endpoint> piece_picker::get_downloader(piece_block block) const
	{
		std::vector<downloading_piece>::const_iterator i = std::find_if(
			m_downloads.begin()
			, m_downloads.end()
			, has_index(block.piece_index));

		if (i == m_downloads.end())
			return boost::optional<tcp::endpoint>();

		assert(block.block_index < max_blocks_per_piece);
		assert(block.block_index >= 0);

		if (i->requested_blocks[block.block_index] == false
			|| i->finished_blocks[block.block_index] == true)
			return boost::optional<tcp::endpoint>();

		return boost::optional<tcp::endpoint>(i->info[block.block_index].peer);
	}

	void piece_picker::abort_download(piece_block block)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

		assert(block.piece_index >= 0);
		assert(block.block_index >= 0);
		assert(block.piece_index < (int)m_piece_map.size());
		assert(block.block_index < blocks_in_piece(block.piece_index));

		if (m_piece_map[block.piece_index].downloading == 0)
		{
			assert(std::find_if(m_downloads.begin(), m_downloads.end()
				, has_index(block.piece_index)) == m_downloads.end());
			return;
		}

		std::vector<downloading_piece>::iterator i = std::find_if(m_downloads.begin()
			, m_downloads.end(), has_index(block.piece_index));
		assert(i != m_downloads.end());

		if (i->finished_blocks[block.block_index])
		{
			assert(std::find_if(m_downloads.begin(), m_downloads.end()
				, has_index(block.piece_index)) == m_downloads.end());
			return;
		}

		assert(block.block_index < blocks_in_piece(block.piece_index));
		assert(i->requested_blocks[block.block_index]);

		// clear this block as being downloaded
		i->requested_blocks[block.block_index] = false;
		
		// clear the downloader of this block
		i->info[block.block_index].peer = tcp::endpoint();

		// if there are no other blocks in this piece
		// that's being downloaded, remove it from the list
		if (i->requested_blocks.count() == 0)
		{
			m_downloads.erase(i);
			piece_pos& p = m_piece_map[block.piece_index];
			int prio = p.priority(m_sequenced_download_threshold);
			p.downloading = 0;
			move(prio, p.index);

			assert(std::find_if(m_downloads.begin(), m_downloads.end()
				, has_index(block.piece_index)) == m_downloads.end());
		}
	}

	int piece_picker::unverified_blocks() const
	{
		int counter = 0;
		for (std::vector<downloading_piece>::const_iterator i = m_downloads.begin();
			i != m_downloads.end(); ++i)
		{
			counter += (int)i->finished_blocks.count();
		}
		return counter;
	}

}

