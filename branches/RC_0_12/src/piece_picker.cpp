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

//#define TORRENT_PIECE_PICKER_INVARIANT_CHECK INVARIANT_CHECK
#define TORRENT_PIECE_PICKER_INVARIANT_CHECK

namespace libtorrent
{

	piece_picker::piece_picker(int blocks_per_piece, int total_num_blocks)
		: m_piece_info(2)
		, m_downloading_piece_info(2)
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
		// build a vector of all the pieces we don't have
		std::vector<int> piece_list;
		piece_list.reserve(std::count(pieces.begin(), pieces.end(), false));

		for (std::vector<bool>::const_iterator i = pieces.begin();
			i != pieces.end(); ++i)
		{
			if (*i) continue;
			int index = static_cast<int>(i - pieces.begin());
			if (m_piece_map[index].filtered)
			{
				++m_num_filtered;
				--m_num_have_filtered;
				m_piece_map[index].index = 0;
			}
			else
			{
				piece_list.push_back(index);
			}
		}

		// add the pieces to the piece_picker
		for (std::vector<int>::reverse_iterator i = piece_list.rbegin();
			i != piece_list.rend(); ++i)
		{
			int index = *i;
			assert(index >= 0);
			assert(index < (int)m_piece_map.size());
			assert(m_piece_map[index].index == piece_pos::we_have_index);
			assert(m_piece_map[index].peer_count == 0);
			assert(m_piece_info.size() == 2);

			add(index);
			assert(m_piece_map[index].index != piece_pos::we_have_index);
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
			
		int old_limit = m_sequenced_download_threshold;
		m_sequenced_download_threshold = sequenced_download_threshold;

		for (std::vector<piece_pos>::iterator i = m_piece_map.begin()
			, end(m_piece_map.end()); i != end; ++i)
		{
			if (i->priority(old_limit) != i->priority(m_sequenced_download_threshold))
			{
				piece_pos& p = *i;
				if (p.index == piece_pos::we_have_index) continue;
				int prev_priority = p.priority(old_limit);
				move(p.downloading, p.filtered, prev_priority, p.index);
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

		if (t != 0)
			assert((int)m_piece_map.size() == t->torrent_file().num_pieces());

		int num_filtered = 0;
		int num_have_filtered = 0;
		for (std::vector<piece_pos>::const_iterator i = m_piece_map.begin();
			i != m_piece_map.end(); ++i)
		{
			int index = static_cast<int>(i - m_piece_map.begin());
			if (i->filtered)
			{
				if (i->index != piece_pos::we_have_index)
					++num_filtered;
				else
					++num_have_filtered;
			}
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

			if (i->index == piece_pos::we_have_index)
			{
				assert(t == 0 || t->have_piece(index));
				assert(i->downloading == 0);

				// make sure there's no entry
				// with this index. (there shouldn't
				// be since the piece_map is piece_pos::we_have_index)
				for (std::vector<std::vector<int> >::const_iterator i = m_piece_info.begin();
					i != m_piece_info.end(); ++i)
				{
					for (std::vector<int>::const_iterator j= i->begin();
						j != i->end(); ++j)
					{
						assert(*j != index);
					}
				}

				for (std::vector<std::vector<int> >::const_iterator i = m_downloading_piece_info.begin();
					i != m_downloading_piece_info.end(); ++i)
				{
					for (std::vector<int>::const_iterator j = i->begin();
						j != i->end(); ++j)
					{
						assert(*j != index);
					}
				}

			}
			else if (!i->filtered)
			{
				if (t != 0)
					assert(!t->have_piece(index));

				const std::vector<std::vector<int> >& c_vec = pick_piece_info_vector(i->downloading, i->filtered);
				assert(i->priority(m_sequenced_download_threshold) < (int)c_vec.size());
				const std::vector<int>& vec = c_vec[i->priority(m_sequenced_download_threshold)];
				if (i->index >= vec.size())
				{
					assert(false);
				}
				assert(vec[i->index] == index);
			}

			std::vector<downloading_piece>::const_iterator down
				= std::find_if(m_downloads.begin(),
				m_downloads.end(),
				has_index(index));
			if (i->downloading == 1)
			{
				assert(down != m_downloads.end());
			}
			else
			{
				assert(down == m_downloads.end());
			}
		}
		assert(num_filtered == m_num_filtered);
		assert(num_have_filtered == m_num_have_filtered);
	}
#endif

	float piece_picker::distributed_copies() const
	{
		const float num_pieces = static_cast<float>(m_piece_map.size());

		for (int i = 0; i < (int)m_piece_info.size(); ++i)
		{
			int p = (int)m_piece_info[i].size();
			assert(num_pieces == 0 || float(p) / num_pieces <= 1.f);
			if (p > 0)
			{
				float fraction_above_count =
					1.f - float(p) / num_pieces;
				return i + fraction_above_count;
			}
		}
		return 1.f;
	}

	std::vector<std::vector<int> >& piece_picker::pick_piece_info_vector(
		bool downloading, bool filtered)
	{
		assert(!filtered);
		return downloading?m_downloading_piece_info:m_piece_info;
	}

	std::vector<std::vector<int> > const& piece_picker::pick_piece_info_vector(
		bool downloading, bool filtered) const
	{
		assert(!filtered);
		return downloading?m_downloading_piece_info:m_piece_info;
	}

	void piece_picker::add(int index)
	{
		assert(index >= 0);
		assert(index < (int)m_piece_map.size());
		piece_pos& p = m_piece_map[index];
		assert(!p.filtered);

		std::vector<std::vector<int> >& dst_vec = pick_piece_info_vector(
			p.downloading, p.filtered);

		int priority = p.priority(m_sequenced_download_threshold);
		if ((int)dst_vec.size() <= priority)
			dst_vec.resize(priority + 1);

		assert((int)dst_vec.size() > priority);

		if (p.ordered(m_sequenced_download_threshold))
		{
			// the piece should be inserted ordered, not randomly
			std::vector<int>& v = dst_vec[priority];
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
		else if (dst_vec[priority].size() < 2)
		{
			p.index = dst_vec[priority].size();
			dst_vec[priority].push_back(index);
		}
		else
		{
			// find a random position in the destination vector where we will place
			// this entry.
			int dst_index = rand() % dst_vec[priority].size();
			
			// copy the entry at that position to the back
			m_piece_map[dst_vec[priority][dst_index]].index
				= dst_vec[priority].size();
			dst_vec[priority].push_back(dst_vec[priority][dst_index]);

			// and then replace the one at dst_index with the one we're moving.
			// this procedure is to make sure there's no ordering when pieces
			// are moved in sequenced order.
			p.index = dst_index;
			dst_vec[priority][p.index] = index;
		}
	}

	// will update the piece with the given properties (downloading, filtered,
	// priority, elem_index) to place it at the correct position in the
	// vectors.
	void piece_picker::move(bool downloading, bool filtered, int priority
		, int elem_index)
	{
		assert(!filtered);
		assert(priority >= 0);
		assert(elem_index >= 0);
		assert(elem_index != piece_pos::we_have_index);
		std::vector<std::vector<int> >& src_vec(pick_piece_info_vector(
			downloading, filtered));
		assert(m_files_checked_called);

		assert((int)src_vec.size() > priority);
		assert((int)src_vec[priority].size() > elem_index);

		int index = src_vec[priority][elem_index];
		// update the piece_map
		piece_pos& p = m_piece_map[index];
		int new_priority = p.priority(m_sequenced_download_threshold);

		if (p.downloading == downloading
			&& p.filtered == filtered
			&& new_priority == priority)
		{
			assert(p.ordered(m_sequenced_download_threshold));
			return;
		}

		std::vector<std::vector<int> >& dst_vec(pick_piece_info_vector(
			p.downloading, p.filtered));

		assert(&dst_vec != &src_vec || new_priority != priority);

		if ((int)dst_vec.size() <= new_priority)
		{
			dst_vec.resize(new_priority + 1);
			assert((int)dst_vec.size() > new_priority);
		}

		if (p.ordered(m_sequenced_download_threshold))
		{
			// the piece should be inserted ordered, not randomly
			std::vector<int>& v = dst_vec[new_priority];
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
		else if (dst_vec[new_priority].size() < 2)
		{
			p.index = dst_vec[new_priority].size();
			dst_vec[new_priority].push_back(index);
		}
		else
		{
			// find a random position in the destination vector where we will place
			// this entry.
			int dst_index = rand() % dst_vec[new_priority].size();
			
			// copy the entry at that position to the back
			m_piece_map[dst_vec[new_priority][dst_index]].index
				= dst_vec[new_priority].size();
			dst_vec[new_priority].push_back(dst_vec[new_priority][dst_index]);

			// and then replace the one at dst_index with the one we're moving.
			// this procedure is to make sure there's no ordering when pieces
			// are moved in sequenced order.
			p.index = dst_index;
			dst_vec[new_priority][p.index] = index;
		}
		assert(p.index < dst_vec[p.priority(m_sequenced_download_threshold)].size());
		assert(dst_vec[p.priority(m_sequenced_download_threshold)][p.index] == index);

		if (priority >= m_sequenced_download_threshold)
		{
			// remove the element from the source vector and preserve the order
			std::vector<int>& v = src_vec[priority];
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
			int replace_index = src_vec[priority][elem_index] = src_vec[priority].back();
			if (index != replace_index)
			{
				// update the entry we moved from the back
				m_piece_map[replace_index].index = elem_index;

				assert((int)src_vec[priority].size() > elem_index);
				// this may not necessarily be the case. If we've just updated the threshold and are updating
				// the piece map
//				assert((int)m_piece_map[replace_index].priority(m_sequenced_download_threshold) == priority);
				assert((int)m_piece_map[replace_index].index == elem_index);
				assert(src_vec[priority][elem_index] == replace_index);
			}
			else
			{
				assert((int)src_vec[priority].size() == elem_index+1);
			}

			src_vec[priority].pop_back();
		}
	}

	void piece_picker::remove(bool downloading, bool filtered, int priority
		, int elem_index)
	{
		assert(!filtered);
		assert(priority >= 0);
		assert(elem_index >= 0);
		assert(m_files_checked_called);

		std::vector<std::vector<int> >& src_vec(pick_piece_info_vector(downloading, filtered));

		assert((int)src_vec.size() > priority);
		assert((int)src_vec[priority].size() > elem_index);

		int index = src_vec[priority][elem_index];

		if (downloading)
		{
			std::vector<downloading_piece>::iterator i
				= std::find_if(m_downloads.begin(),
				m_downloads.end(),
				has_index(index));
			assert(i != m_downloads.end());
			m_downloads.erase(i);
		}
		piece_pos& p = m_piece_map[index];
		p.downloading = 0;
		if (p.ordered(m_sequenced_download_threshold))
		{
			std::vector<int>& v = src_vec[priority];
//			assert(is_sorted(v.begin(), v.end()/*, std::greater<int>()*/));
			std::vector<int>::iterator i = v.begin() + elem_index;
			v.erase(i);
			i = v.begin() + elem_index;
			for (; i != v.end(); ++i)
			{
				--m_piece_map[*i].index;
				assert(v[m_piece_map[*i].index] == *i);
			}
//			assert(is_sorted(v.begin(), v.end()/*, std::greater<int>()*/));
		}
		else
		{
			// this will remove elem from the vector without
			// preserving order
			index = src_vec[priority][elem_index] = src_vec[priority].back();
			// update the entry we moved from the back
			if ((int)src_vec[priority].size() > elem_index+1)
				m_piece_map[index].index = elem_index;
			src_vec[priority].pop_back();
		}
	}

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

		m_piece_map[index].downloading = 0;
		piece_pos& p = m_piece_map[index];
		if (p.filtered) return;
		move(true, p.filtered, p.priority(m_sequenced_download_threshold), p.index);
	}

	void piece_picker::inc_refcount(int i)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
		assert(i >= 0);
		assert(i < (int)m_piece_map.size());
		assert(m_files_checked_called);

		int index = m_piece_map[i].index;
		int prev_priority = m_piece_map[i].priority(m_sequenced_download_threshold);

		assert(m_piece_map[i].peer_count < 2048);
		m_piece_map[i].peer_count++;
		assert(m_piece_map[i].peer_count != 0);	

		piece_pos& p = m_piece_map[i];

		// if we have the piece or if it's filtered
		// we don't have to move any entries in the piece_info vector
		if (index == piece_pos::we_have_index || p.filtered
			|| p.priority(m_sequenced_download_threshold) == prev_priority) return;

		move(p.downloading, p.filtered, prev_priority, index);

#ifndef NDEBUG
//		integrity_check();
#endif
		return;
	}

	void piece_picker::dec_refcount(int i)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;

		assert(m_files_checked_called);
		assert(i >= 0);
		assert(i < (int)m_piece_map.size());

		int prev_priority = m_piece_map[i].priority(m_sequenced_download_threshold);
		int index = m_piece_map[i].index;
		assert(m_piece_map[i].peer_count > 0);

		if (m_piece_map[i].peer_count > 0)
			m_piece_map[i].peer_count--;

		piece_pos& p = m_piece_map[i];

		if (index == piece_pos::we_have_index || p.filtered
			|| p.priority(m_sequenced_download_threshold) == prev_priority) return;

		move(p.downloading, p.filtered, prev_priority, index);
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

		int info_index = m_piece_map[index].index;
		int priority = m_piece_map[index].priority(m_sequenced_download_threshold);

		assert(m_piece_map[index].downloading == 1);

		assert(info_index != piece_pos::we_have_index);
		piece_pos& p = m_piece_map[index];
		if (p.filtered)
		{
			--m_num_filtered;
			++m_num_have_filtered;
		}
		if (info_index == piece_pos::we_have_index) return;
		remove(p.downloading, p.filtered, priority, info_index);
		p.index = piece_pos::we_have_index;
	}


	void piece_picker::mark_as_filtered(int index)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
		assert(index >= 0);
		assert(index < (int)m_piece_map.size());
		
		piece_pos& p = m_piece_map[index];
		if (p.filtered == 1) return;	
		p.filtered = 1;
		if (p.index != piece_pos::we_have_index)
		{
			++m_num_filtered;
			remove(p.downloading, false, p.priority(m_sequenced_download_threshold), p.index);
			assert(p.filtered == 1);
		}
		else
		{
			++m_num_have_filtered;
		}
	}
	
	// this function can be used for pieces that we don't
	// have, but have marked as filtered (so we didn't
	// want to download them) but later want to enable for
	// downloading, then we call this function and it will
	// be inserted in the available piece list again
	void piece_picker::mark_as_unfiltered(int index)
	{
		TORRENT_PIECE_PICKER_INVARIANT_CHECK;
		assert(index >= 0);
		assert(index < (int)m_piece_map.size());

		piece_pos& p = m_piece_map[index];
		if (p.filtered == 0) return;	
		p.filtered = 0;
		if (p.index != piece_pos::we_have_index)
		{
			--m_num_filtered;
			assert(m_num_filtered >= 0);
			add(index);
		}
		else
		{
			--m_num_have_filtered;
			assert(m_num_have_filtered >= 0);
		}
	}

	bool piece_picker::is_filtered(int index) const
	{
		assert(index >= 0);
		assert(index < (int)m_piece_map.size());

		return m_piece_map[index].filtered == 1;
	}

	void piece_picker::filtered_pieces(std::vector<bool>& mask) const
	{
		mask.resize(m_piece_map.size());
		std::vector<bool>::iterator j = mask.begin();
		for (std::vector<piece_pos>::const_iterator i = m_piece_map.begin(),
			end(m_piece_map.end()); i != end; ++i, ++j)
		{
			*j = i->filtered == 1;
		}
	}
	
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
		assert(m_downloading_piece_info.begin()
			!= m_downloading_piece_info.end());

		std::vector<std::vector<int> >::const_iterator partial
			= m_downloading_piece_info.begin() + 1;

		std::vector<piece_block> backup_blocks;
		
		// this loop will loop from pieces with 1 peer and up
		// until we either reach the end of the piece list or
		// has filled the interesting_blocks with num_blocks
		// blocks.
	
		// it iterates over two ranges simultaneously. The pieces that are
		// partially downloaded or partially requested, and the pieces that
		// hasn't been requested at all. The default is to prioritize pieces
		// that are partially requested/downloaded, so the loop will first
		// look for blocks among those pieces. And it will also take two steps
		// in that range when iterating. This has the effect that partial pieces
		// doesn't have to be as rare as non-requested pieces in order to be
		// prefered.
		
		// When prefer_whole_pieces is set (usually set when downloading from
		// fast peers) the partial pieces will not be prioritized, but actually
		// ignored as long as possible.

		while((free != m_piece_info.end())
			|| (partial != m_downloading_piece_info.end()))
		{
			if (partial != m_downloading_piece_info.end())
			{
				for (int i = 0; i < 2; ++i)
				{
					num_blocks = add_interesting_blocks_partial(*partial, pieces
						, interesting_blocks, backup_blocks, num_blocks
						, prefer_whole_pieces, peer);
					assert(num_blocks >= 0);
					if (num_blocks == 0) return;
					++partial;
					if (partial == m_downloading_piece_info.end()) break;
				}
			}

			if (free != m_piece_info.end())
			{
				num_blocks = add_interesting_blocks_free(*free, pieces
					, interesting_blocks, num_blocks, prefer_whole_pieces);
				assert(num_blocks >= 0);
				if (num_blocks == 0) return;
				++free;
			}
		}

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

	int piece_picker::add_interesting_blocks_free(std::vector<int> const& piece_list
		, std::vector<bool> const& pieces
		, std::vector<piece_block>& interesting_blocks
		, int num_blocks, bool prefer_whole_pieces) const
	{
		for (std::vector<int>::const_iterator i = piece_list.begin();
			i != piece_list.end(); ++i)
		{
			assert(*i >= 0);
			assert(*i < (int)m_piece_map.size());
			assert(m_piece_map[*i].downloading == 0);

			// if the peer doesn't have the piece
			// skip it
			if (!pieces[*i]) continue;

			int piece_blocks = blocks_in_piece(*i);
			if (!prefer_whole_pieces && piece_blocks > num_blocks)
				piece_blocks = num_blocks;
			for (int j = 0; j < piece_blocks; ++j)
			{
				interesting_blocks.push_back(piece_block(*i, j));
			}
			num_blocks -= (std::min)(piece_blocks, num_blocks);
			assert(num_blocks >= 0);
			if (num_blocks == 0) return num_blocks;
		}
		return num_blocks;
	}
	
	int piece_picker::add_interesting_blocks_partial(std::vector<int> const& piece_list
		, const std::vector<bool>& pieces
		, std::vector<piece_block>& interesting_blocks
		, std::vector<piece_block>& backup_blocks
		, int num_blocks, bool prefer_whole_pieces
		, tcp::endpoint peer) const
	{
		assert(num_blocks > 0);

		for (std::vector<int>::const_iterator i = piece_list.begin();
			i != piece_list.end(); ++i)
		{
			assert(*i >= 0);
			assert(*i < (int)m_piece_map.size());
			// if the peer doesn't have the piece
			// skip it
			if (!pieces[*i]) continue;

			assert(m_piece_map[*i].downloading == 1);

			// calculate the number of blocks in this
			// piece. It's always m_blocks_per_piece, except
			// in the last piece.
			int num_blocks_in_piece = blocks_in_piece(*i);

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
			assert(std::find_if(m_downloads.begin(), m_downloads.end(), has_index(index))
				== m_downloads.end());
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
			p.downloading = 1;
			move(false, p.filtered, p.priority(m_sequenced_download_threshold), p.index);

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
		if (p.index == piece_pos::we_have_index || p.filtered) return;

		if (p.downloading == 0)
		{
			p.downloading = 1;
			move(false, p.filtered, p.priority(m_sequenced_download_threshold), p.index);

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
			assert(std::find_if(m_downloads.begin(), m_downloads.end(), has_index(block.piece_index)) == m_downloads.end());
			return;
		}

		std::vector<downloading_piece>::iterator i
			= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(block.piece_index));
		assert(i != m_downloads.end());

		if (i->finished_blocks[block.block_index]) return;

		assert(block.block_index < blocks_in_piece(block.piece_index));
#ifndef NDEBUG
		if (i->requested_blocks[block.block_index] == false)
		{
			assert(false);
		}
#endif

		// clear this block as being downloaded
		i->requested_blocks[block.block_index] = false;
		
		// clear the downloader of this block
		i->info[block.block_index].peer = tcp::endpoint();

		// if there are no other blocks in this piece
		// that's being downloaded, remove it from the list
		if (i->requested_blocks.count() == 0)
		{
			m_downloads.erase(i);
			m_piece_map[block.piece_index].downloading = 0;
			piece_pos& p = m_piece_map[block.piece_index];
			move(true, p.filtered, p.priority(m_sequenced_download_threshold), p.index);
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

