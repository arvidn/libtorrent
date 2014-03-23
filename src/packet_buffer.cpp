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

#include <stdlib.h> // free and calloc
#include "libtorrent/packet_buffer.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/invariant_check.hpp"

namespace libtorrent {

	bool compare_less_wrap(boost::uint32_t lhs, boost::uint32_t rhs
		, boost::uint32_t mask);

	packet_buffer::packet_buffer()
		: m_storage(0)
		, m_capacity(0)
		, m_size(0)
		, m_first(0)
		, m_last(0)
	{}

#ifdef TORRENT_DEBUG
	void packet_buffer::check_invariant() const
	{
		int count = 0;
		for (int i = 0; i < int(m_capacity); ++i)
		{
			count += m_storage[i] ? 1 : 0;
		}
		TORRENT_ASSERT(count == int(m_size));
	}
#endif

	packet_buffer::~packet_buffer()
	{
		free(m_storage);
	}

	void* packet_buffer::insert(index_type idx, void* value)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT_VAL(idx <= 0xffff, idx);
		// you're not allowed to insert NULLs!
		TORRENT_ASSERT(value);

		if (value == 0) return remove(idx);

		if (m_size != 0)
		{
			if (compare_less_wrap(idx, m_first, 0xffff))
			{
				// Index comes before m_first. If we have room, we can simply
				// adjust m_first backward.

				std::size_t free_space = 0;

				for (index_type i = (m_first - 1) & (m_capacity - 1);
						i != (m_first & (m_capacity - 1)); i = (i - 1) & (m_capacity - 1))
				{
					if (m_storage[i & (m_capacity - 1)])
						break;
					++free_space;
				}

				if (((m_first - idx) & 0xffff) > free_space)
					reserve(((m_first - idx) & 0xffff) + m_capacity - free_space);

				m_first = idx;
			}
			else if (idx >= m_first + m_capacity)
			{
				reserve(idx - m_first + 1);
			}
			else if (idx < m_first)
			{
				// We have wrapped.
				if (idx >= ((m_first + m_capacity) & 0xffff) && m_capacity < 0xffff)
				{
					reserve(m_capacity + (idx + 1 - ((m_first + m_capacity) & 0xffff)));
				}
			}
			if (compare_less_wrap(m_last, (idx + 1) & 0xffff, 0xffff))
				m_last = (idx + 1) & 0xffff;
		}
		else
		{
			m_first = idx;
			m_last = (idx + 1) & 0xffff;
		}

		if (m_capacity == 0) reserve(16);

		void* old_value = m_storage[idx & (m_capacity - 1)];
		m_storage[idx & (m_capacity - 1)] = value;

		if (m_size == 0) m_first = idx;
		// if we're just replacing an old value, the number
		// of elements in the buffer doesn't actually increase
		if (old_value == 0) ++m_size;

		TORRENT_ASSERT_VAL(m_first <= 0xffff, m_first);
		return old_value;
	}

	void* packet_buffer::at(index_type idx) const
	{
		INVARIANT_CHECK;
		if (idx >= m_first + m_capacity)
			return 0;

		if (compare_less_wrap(idx, m_first, 0xffff))
		{
			return 0;
		}

		const int mask = (m_capacity - 1);
		return m_storage[idx & mask];
	}

	void packet_buffer::reserve(std::size_t size)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT_VAL(size <= 0xffff, size);
		std::size_t new_size = m_capacity == 0 ? 16 : m_capacity;

		while (new_size < size)
			new_size <<= 1;

		void** new_storage = (void**)malloc(sizeof(void*) * new_size);

		for (index_type i = 0; i < new_size; ++i)
			new_storage[i] = 0;

		for (index_type i = m_first; i < (m_first + m_capacity); ++i)
			new_storage[i & (new_size - 1)] = m_storage[i & (m_capacity - 1)];

		free(m_storage);

		m_storage = new_storage;
		m_capacity = new_size;
	}

	void* packet_buffer::remove(index_type idx)
	{
		INVARIANT_CHECK;
		// TODO: use compare_less_wrap for this comparison as well
		if (idx >= m_first + m_capacity)
			return 0;

		if (compare_less_wrap(idx, m_first, 0xffff))
			return 0;

		const int mask = (m_capacity - 1);
		void* old_value = m_storage[idx & mask];
		m_storage[idx & mask] = 0;

		if (old_value)
		{
			--m_size;
			if (m_size == 0) m_last = m_first;
		}

		if (idx == m_first && m_size != 0)
		{
			++m_first;
			for (boost::uint32_t i = 0; i < m_capacity; ++i, ++m_first)
				if (m_storage[m_first & mask]) break;
			m_first &= 0xffff;
		}

		if (((idx + 1) & 0xffff) == m_last && m_size != 0)
		{
			--m_last;
			for (boost::uint32_t i = 0; i < m_capacity; ++i, --m_last)
				if (m_storage[m_last & mask]) break;
			++m_last;
			m_last &= 0xffff;
		}

		TORRENT_ASSERT_VAL(m_first <= 0xffff, m_first);
		return old_value;
	}

}

