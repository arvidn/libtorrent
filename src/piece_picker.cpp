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

#include "libtorrent/piece_picker.hpp"

#ifndef NDEBUG
#include "libtorrent/torrent.hpp"
#include "libtorrent/peer_connection.hpp"
#endif

#if defined(_MSC_VER) && _MSC_VER < 1300
#define for if (false) {} else for
namespace std
{
	template<class T>
	inline T min(T a, T b) { return a<b?a:b; }
}
#endif

namespace libtorrent
{

	piece_picker::piece_picker(int blocks_per_piece, int total_num_blocks)
		: m_piece_info(2)
		, m_downloading_piece_info(2)
		, m_piece_map((total_num_blocks + blocks_per_piece-1) / blocks_per_piece)
	{
		m_blocks_per_piece = blocks_per_piece;
		m_blocks_in_last_piece = total_num_blocks % blocks_per_piece;
		if (m_blocks_in_last_piece == 0) m_blocks_in_last_piece = blocks_per_piece;

		assert(m_blocks_per_piece <= max_blocks_per_piece);
		assert(m_blocks_in_last_piece <= m_blocks_per_piece);
		assert(m_blocks_in_last_piece <= max_blocks_per_piece);

		// allocate the piece_map to cover all pieces
		// and make them invalid (as if though we already had every piece)
		std::fill(m_piece_map.begin(), m_piece_map.end(), piece_pos(0, 0xffffff));
	}

	void piece_picker::files_checked(
		const std::vector<bool>& pieces
		, const std::vector<downloading_piece>& unfinished)
	{
		// build a vector of all the pieces we don't have
		std::vector<int> piece_list;
		piece_list.reserve(
			pieces.size()
			- std::accumulate(pieces.begin(), pieces.end(), 0));

		for (std::vector<bool>::const_iterator i = pieces.begin();
			i != pieces.end();
			++i)
		{
			if (*i) continue;
			int index = i - pieces.begin();
			piece_list.push_back(index);
		}
	
		// random shuffle the list
		std::random_shuffle(piece_list.begin(), piece_list.end());

		// add the pieces to the piece_picker
		for (std::vector<int>::iterator i = piece_list.begin();
			i != piece_list.end();
			++i)
		{
			int index = *i;
			assert(index < m_piece_map.size());
			assert(m_piece_map[index].index  == 0xffffff);

			int peer_count = m_piece_map[index].peer_count;
			assert(peer_count == 0);
			assert(m_piece_info.size() == 2);

			m_piece_map[index].index = m_piece_info[peer_count].size();
			m_piece_info[peer_count].push_back(index);
		}

		// if we have fast resume info
		// use it
		if (!unfinished.empty())
		{
			for (std::vector<downloading_piece>::const_iterator i
				= unfinished.begin();
				i != unfinished.end();
				++i)
			{
				address peer;
				for (int j = 0; j < m_blocks_per_piece; ++j)
				{
					if (i->finished_blocks[j])
						mark_as_finished(piece_block(i->index, j), peer);
				}
			}
		}
#ifndef NDEBUG
//		integrity_check();
#endif
	}

#ifndef NDEBUG

	void piece_picker::integrity_check(const torrent* t) const
	{
		assert(sizeof(piece_pos) == 4);

		if (t != 0)
			assert(m_piece_map.size() == t->torrent_file().num_pieces());

		int last_val = 0;
		for (std::vector<piece_pos>::const_iterator i = m_piece_map.begin();
			i != m_piece_map.end();
			++i)
		{
			int index = i - m_piece_map.begin();

			if (t != 0)
			{
				int actual_peer_count = 0;
				for (std::vector<peer_connection*>::const_iterator peer = t->begin();
					peer != t->end();
					++peer)
				{
					if ((*peer)->has_piece(index)) actual_peer_count++;
				}

				assert(i->peer_count == actual_peer_count);
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

			if (i->index == 0xffffff)
			{
				assert(t == 0 || t->have_piece(index));
				assert(i->downloading == 0);

				// make sure there's no entry
				// with this index. (there shouldn't
				// be since the piece_map is 0xffffff)
				for (std::vector<std::vector<int> >::const_iterator i = m_piece_info.begin();
					i != m_piece_info.end();
					++i)
				{
					for (std::vector<int>::const_iterator j= i->begin();
						j != i->end();
						++j)
					{
						assert(*j != index);
					}
				}

				for (std::vector<std::vector<int> >::const_iterator i = m_downloading_piece_info.begin();
					i != m_downloading_piece_info.end();
					++i)
				{
					for (std::vector<int>::const_iterator j = i->begin();
						j != i->end();
						++j)
					{
						assert(*j != index);
					}
				}

			}
			else
			{
				if (t != 0)
					assert(!t->have_piece(index));

				const std::vector<std::vector<int> >& c_vec = (i->downloading)?m_downloading_piece_info:m_piece_info;
				assert(i->peer_count < c_vec.size());
				const std::vector<int>& vec = c_vec[i->peer_count];
				assert(i->index < vec.size());
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

	}
	#endif

	void piece_picker::move(bool downloading, int peer_count, int elem_index)
	{
		std::vector<std::vector<int> >& src_vec = (downloading)?m_downloading_piece_info:m_piece_info;

		assert(src_vec.size() > peer_count);
		assert(src_vec[peer_count].size() > elem_index);

		int index = src_vec[peer_count][elem_index];
		// update the piece_map
		piece_pos& p = m_piece_map[index];

		assert(p.downloading != downloading || p.peer_count != peer_count);

		std::vector<std::vector<int> >& dst_vec = (p.downloading)?m_downloading_piece_info:m_piece_info;

		if (dst_vec.size() <= p.peer_count)
		{
			dst_vec.resize(p.peer_count+1);
			assert(dst_vec.size() > p.peer_count);
		}

		p.index = dst_vec[p.peer_count].size();
		dst_vec[p.peer_count].push_back(index);
		assert(p.index < dst_vec[p.peer_count].size());
		assert(dst_vec[p.peer_count][p.index] == index);

		// this will remove elem from the source vector without
		// preserving order
		int replace_index = src_vec[peer_count][elem_index] = src_vec[peer_count].back();
		if (index != replace_index)
		{
			// update the entry we moved from the back
			m_piece_map[replace_index].index = elem_index;

			assert(src_vec[peer_count].size() > elem_index);
			assert(m_piece_map[replace_index].peer_count == peer_count);
			assert(m_piece_map[replace_index].index == elem_index);
			assert(src_vec[peer_count][elem_index] == replace_index);
		}
		else
		{
			assert(src_vec[peer_count].size() == elem_index+1);
		}

		src_vec[peer_count].pop_back();

	}

	void piece_picker::remove(bool downloading, int peer_count, int elem_index)
	{
		std::vector<std::vector<int> >& src_vec = (downloading)?m_downloading_piece_info:m_piece_info;

		assert(src_vec.size() > peer_count);
		assert(src_vec[peer_count].size() > elem_index);

		int index = src_vec[peer_count][elem_index];
		m_piece_map[index].index = 0xffffff;

		if (downloading)
		{
			std::vector<downloading_piece>::iterator i
				= std::find_if(m_downloads.begin(),
				m_downloads.end(),
				has_index(index));
			assert(i != m_downloads.end());
			m_downloads.erase(i);
		}
		m_piece_map[index].downloading = 0;

		// this will remove elem from the vector without
		// preserving order
		index = src_vec[peer_count][elem_index] = src_vec[peer_count].back();
		// update the entry we moved from the back
		if (src_vec[peer_count].size() > elem_index+1)
			m_piece_map[index].index = elem_index;
		src_vec[peer_count].pop_back();

	}

	void piece_picker::restore_piece(int index)
	{
		assert(index >= 0);
		assert(index < m_piece_map.size());

		assert(m_piece_map[index].downloading == 1);

		std::vector<downloading_piece>::iterator i
			= std::find_if(m_downloads.begin(),
			m_downloads.end(),
			has_index(index));
		assert(i != m_downloads.end());
		m_downloads.erase(i);

		m_piece_map[index].downloading = 0;
		move(true, m_piece_map[index].peer_count, m_piece_map[index].index);

#ifndef NDEBUG
//		integrity_check();
#endif
	}

	void piece_picker::inc_refcount(int i)
	{
		assert(i >= 0);
		assert(i < m_piece_map.size());

		int peer_count = m_piece_map[i].peer_count;
		int index = m_piece_map[i].index;

		m_piece_map[i].peer_count++;

		// if we have the piece, we don't have to move
		// any entries in the piece_info vector
		if (index == 0xffffff) return;

		move(m_piece_map[i].downloading, peer_count, index);

#ifndef NDEBUG
//		integrity_check();
#endif
		return;
	}

	void piece_picker::dec_refcount(int i)
	{
		assert(i >= 0);
		assert(i < m_piece_map.size());

		int peer_count = m_piece_map[i].peer_count;
		int index = m_piece_map[i].index;
		assert(m_piece_map[i].peer_count > 0);

		m_piece_map[i].peer_count--;

		if (index == 0xffffff) return;
		move(m_piece_map[i].downloading, peer_count, index);
	}

	void piece_picker::we_have(int index)
	{
		assert(index < m_piece_map.size());
		int info_index = m_piece_map[index].index;
		int peer_count = m_piece_map[index].peer_count;

		assert(m_piece_map[index].downloading == 1);

		assert(info_index != 0xffffff);
		remove(m_piece_map[index].downloading, peer_count, info_index);
#ifndef NDEBUG
//		integrity_check();
#endif
	}

	void piece_picker::pick_pieces(const std::vector<bool>& pieces,
		std::vector<piece_block>& interesting_pieces,
		int num_blocks) const
	{
		assert(pieces.size() == m_piece_map.size());

#ifndef NDEBUG
//		integrity_check();
#endif

		// free refers to pieces that are free to download, noone else
		// is downloading them.
		// partial is pieces that are partially being downloaded, and
		// parts of them may be free for download as well, the
		// partially donloaded pieces will be prioritized
		std::vector<std::vector<int> >::const_iterator free = m_piece_info.begin()+1;
		std::vector<std::vector<int> >::const_iterator partial = m_downloading_piece_info.begin()+1;

		while((free != m_piece_info.end()) || (partial != m_downloading_piece_info.end()))
		{
			if (partial != m_downloading_piece_info.end())
			{
				for (int i = 0; i < 2; ++i)
				{
					num_blocks = add_interesting_blocks(*partial, pieces, interesting_pieces, num_blocks);
					if (num_blocks == 0) return;
					++partial;
					if (partial == m_downloading_piece_info.end()) break;
				}
			}

			if (free != m_piece_info.end())
			{
				num_blocks = add_interesting_blocks(*free, pieces, interesting_pieces, num_blocks);
				if (num_blocks == 0) return;
				++free;
			}
		}
	}

	int piece_picker::add_interesting_blocks(const std::vector<int>& piece_list,
		const std::vector<bool>& pieces,
		std::vector<piece_block>& interesting_blocks,
		int num_blocks) const
	{

		for (std::vector<int>::const_iterator i = piece_list.begin();
			i != piece_list.end();
			++i)
		{
			assert(*i < m_piece_map.size());
			// if the peer doesn't have the piece
			// skip it
			if (!pieces[*i]) continue;

			// if there's at least one block that
			// we can request from this peer
			// we can break our search (return)

			if (m_piece_map[*i].downloading == 0)
			{
				int piece_blocks = std::min(blocks_in_piece(*i), num_blocks);
				for (int j = 0; j < piece_blocks; ++j)
				{
					interesting_blocks.push_back(piece_block(*i, j));
				}
				num_blocks -= piece_blocks;
				if (num_blocks == 0) return num_blocks;
				continue;
			}

			// calculate the number of blocks in this
			// piece. It's always m_blocks_per_piece, except
			// in the last piece.
			int num_blocks_in_piece = blocks_in_piece(*i);

			std::vector<downloading_piece>::const_iterator p
				= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(*i));
			assert(p != m_downloads.end());
	
			for (int j = 0; j < num_blocks_in_piece; ++j)
			{
				if (p->finished_blocks[j] == 1) continue;

				interesting_blocks.push_back(piece_block(*i, j));
				if (p->requested_blocks[j] == 0)
				{
					// we have found a piece that's free to download
					num_blocks--;
					if (num_blocks == 0) return num_blocks;
				}
			}
		}
		return num_blocks;
	}

	bool piece_picker::is_piece_finished(int index) const
	{
		assert(index < m_piece_map.size());
		assert(index >= 0);

		if (m_piece_map[index].downloading == 0) return false;
		std::vector<downloading_piece>::const_iterator i
			= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(index));
		assert(i != m_downloads.end());
		assert(i->finished_blocks.count() <= m_blocks_per_piece);
		int max_blocks = blocks_in_piece(index);
		if (i->finished_blocks.count() != max_blocks) return false;

		assert(i->requested_blocks.count() == max_blocks);
		return true;
	}

	bool piece_picker::is_downloading(piece_block block) const
	{
		assert(block.piece_index < m_piece_map.size());
		assert(block.block_index < max_blocks_per_piece);

		if (m_piece_map[block.piece_index].downloading == 0) return false;
		std::vector<downloading_piece>::const_iterator i
			= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(block.piece_index));
		assert(i != m_downloads.end());
		return i->requested_blocks[block.block_index];
	}

	bool piece_picker::is_finished(piece_block block) const
	{
		assert(block.piece_index < m_piece_map.size());
		assert(block.block_index < max_blocks_per_piece);

		if (m_piece_map[block.piece_index].index == 0xffffff) return true;
		if (m_piece_map[block.piece_index].downloading == 0) return false;
		std::vector<downloading_piece>::const_iterator i
			= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(block.piece_index));
		assert(i != m_downloads.end());
		return i->finished_blocks[block.block_index];
	}


	void piece_picker::mark_as_downloading(piece_block block, const address& peer)
	{
#ifndef NDEBUG
//		integrity_check();
#endif
		assert(block.piece_index < m_piece_map.size());
		assert(block.block_index < blocks_in_piece(block.piece_index));

		piece_pos& p = m_piece_map[block.piece_index];
		if (p.downloading == 0)
		{
			p.downloading = 1;
			move(false, p.peer_count, p.index);

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
#ifndef NDEBUG
//		integrity_check();
#endif
	}

	void piece_picker::mark_as_finished(piece_block block, const address& peer)
	{
#ifndef NDEBUG
//		integrity_check();
#endif
		assert(block.piece_index < m_piece_map.size());
		assert(block.block_index < blocks_in_piece(block.piece_index));

		piece_pos& p = m_piece_map[block.piece_index];
		if (p.downloading == 0)
		{
			p.downloading = 1;
			move(false, p.peer_count, p.index);

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
#ifndef NDEBUG
//		integrity_check();
#endif
	}
/*
	void piece_picker::mark_as_finished(piece_block block, const peer_id& peer)
	{
#ifndef NDEBUG
		integrity_check();
#endif
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
	void piece_picker::get_downloaders(std::vector<address>& d, int index)
	{
		std::vector<downloading_piece>::iterator i
			= std::find_if(m_downloads.begin(), m_downloads.end(), has_index(index));
		assert(i != m_downloads.end());

		d.clear();
		for (int j = 0; j < blocks_in_piece(index); ++j)
		{
			d.push_back(i->info[j].peer);
		}
	}

	void piece_picker::abort_download(piece_block block)
	{
#ifndef NDEBUG
//		integrity_check();
#endif

		assert(block.piece_index < m_piece_map.size());
		assert(block.block_index < max_blocks_per_piece);

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
		assert(i->requested_blocks[block.block_index] == 1);

		// clear this block as being downloaded
		i->requested_blocks[block.block_index] = 0;

		// if there are no other blocks in this pieces
		// that's being downloaded, remove it from the list
		if (i->requested_blocks.count() == 0)
		{
			m_downloads.erase(i);
			m_piece_map[block.piece_index].downloading = 0;
			move(true, m_piece_map[block.piece_index].peer_count, m_piece_map[block.piece_index].index);
		}
#ifndef NDEBUG
//		integrity_check();
#endif
	}

	int piece_picker::unverified_blocks() const
	{
		int counter = 0;
		for (std::vector<downloading_piece>::const_iterator i = m_downloads.begin();
			i != m_downloads.end();
			++i)
		{
			counter += i->finished_blocks.count();
		}
		return counter;
	}

}
