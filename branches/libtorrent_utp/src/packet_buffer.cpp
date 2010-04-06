/*

Copyright (c) 2010, Arvid Norberg
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

#include "libtorrent/packet_buffer.hpp"

enum { packet_mask = 0xffff };

namespace libtorrent
{
	void* packet_buffer::insert(int i, void* packet)
	{
		TORRENT_ASSERT(i >= 0 && i <= packet_mask);
		TORRENT_ASSERT(packet);

		// if we don't have any elements, update the cursor
		// to the index of the new element we're inserting
		if (m_num_elements == 0) m_cursor = i;
	
		if (m_array == 0)
		{
			// start at 16 elements
			m_mask = 0xf;
			m_array = (void**)calloc(m_mask + 1, sizeof(void*));
			m_cursor = i;
		}
		else if ((unsigned(i - m_cursor) & packet_mask) > m_mask)
		{
			// we need to grow the array around m_cursor
			// find the new size
			int size_to_fit = i - m_cursor;
			int new_size = m_mask + 1;
			do
			{
				new_size <<= 1;
			} while (size_to_fit > new_size);

			// allocate the new array and copy over all the elements
			void **tmp = (void**)calloc(new_size, sizeof(void*));

			// turn the size into a mask
			new_size--;

			for (int j = 0; j <= m_mask; ++j)
			{
				tmp[unsigned(m_cursor + j) & new_size]
					= m_array[unsigned(m_cursor + j) & m_mask];
			}

			// swap arrays
			m_mask = new_size;
			free(m_array);
			m_array = tmp;
		}

		void* ret = m_array[i & m_mask];
		m_array[i & m_mask] = packet;
		if (ret == 0) ++m_num_elements;
		return ret;
	}
	
	packet_buffer::~packet_buffer()
	{
		free(m_array);
	}

	void* packet_buffer::at(int i) const
	{
		TORRENT_ASSERT(i >= 0 && i <= packet_mask);

		// if we don't have an array, return 0
		if (m_array == 0) return 0;

		// if the index is outside of the array, return 0
		if (unsigned(i - m_cursor) & packet_mask > m_mask) return 0;

		return m_array[i & m_mask];
	}

	void* packet_buffer::remove(int i)
	{
		TORRENT_ASSERT(i >= 0 && i <= packet_mask);
//		TORRENT_ASSERT((unsigned(i - m_cursor) & packet_mask) <= m_mask);

		if ((unsigned(i - m_cursor) & packet_mask) > m_mask) return 0;

		if (!m_array)
		{
			TORRENT_ASSERT((unsigned(i - m_cursor) & packet_mask) == 0);
			return 0;	
		}
		
		void* ret = m_array[i & m_mask];
		TORRENT_ASSERT(ret);
		if (ret) --m_num_elements;
		m_array[i & m_mask] = 0;
		// if we removed the cursor element, increment it
		// to the next used slot
		if (m_cursor == i && m_num_elements > 0)
		{
			++m_cursor;
			while (m_array[m_cursor & m_mask] == 0)
				++m_cursor;
		}
		return ret;
	}
}

