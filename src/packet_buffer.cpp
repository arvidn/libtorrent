/*

Copyright (c) 2010-2018, Arvid Norberg
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
#include "libtorrent/assert.hpp"
#include "libtorrent/invariant_check.hpp"

namespace libtorrent {

	bool compare_less_wrap(std::uint32_t lhs, std::uint32_t rhs
		, std::uint32_t mask);

#if TORRENT_USE_INVARIANT_CHECKS
	void packet_buffer::check_invariant() const
	{
		int count = 0;
		for (index_type i = 0; i < m_capacity; ++i)
		{
			count += m_storage[i] ? 1 : 0;
		}
		TORRENT_ASSERT(count == m_size);
	}
#endif

	packet_ptr packet_buffer::insert(index_type idx, packet_ptr value)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT_VAL(idx <= 0xffff, idx);
		// you're not allowed to insert NULLs!
		TORRENT_ASSERT(value);

		if (!value) return remove(idx);

		if (m_size != 0)
		{
			if (compare_less_wrap(idx, m_first, 0xffff))
			{
				// Index comes before m_first. If we have room, we can simply
				// adjust m_first backward.

				std::uint32_t free_space = 0;

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

		packet_ptr old_value = std::move(m_storage[idx & (m_capacity - 1)]);
		m_storage[idx & (m_capacity - 1)] = std::move(value);

		if (m_size == 0) m_first = idx;
		// if we're just replacing an old value, the number
		// of elements in the buffer doesn't actually increase
		if (!old_value) ++m_size;

		TORRENT_ASSERT_VAL(m_first <= 0xffff, m_first);
		return old_value;
	}

	packet* packet_buffer::at(index_type idx) const
	{
		INVARIANT_CHECK;
		if (idx >= m_first + m_capacity)
			return nullptr;

		if (compare_less_wrap(idx, m_first, 0xffff))
			return nullptr;

		std::size_t const mask = m_capacity - 1;
		return m_storage[idx & mask].get();
	}

	void packet_buffer::reserve(std::uint32_t size)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT_VAL(size <= 0xffff, size);
		std::uint32_t new_size = m_capacity == 0 ? 16 : m_capacity;

		while (new_size < size)
			new_size <<= 1;

		aux::unique_ptr<packet_ptr[], index_type> new_storage(new packet_ptr[new_size]);

		for (index_type i = m_first; i < (m_first + m_capacity); ++i)
			new_storage[i & (new_size - 1)] = std::move(m_storage[i & (m_capacity - 1)]);

		m_storage = std::move(new_storage);
		m_capacity = new_size;
	}

	packet_ptr packet_buffer::remove(index_type idx)
	{
		INVARIANT_CHECK;
		// TODO: use compare_less_wrap for this comparison as well
		if (idx >= m_first + m_capacity)
			return packet_ptr();

		if (compare_less_wrap(idx, m_first, 0xffff))
			return packet_ptr();

		std::size_t const mask = m_capacity - 1;
		packet_ptr old_value = std::move(m_storage[idx & mask]);
		m_storage[idx & mask].reset();

		if (old_value)
		{
			--m_size;
			if (m_size == 0) m_last = m_first;
		}

		if (idx == m_first && m_size != 0)
		{
			++m_first;
			for (index_type i = 0; i < m_capacity; ++i, ++m_first)
				if (m_storage[m_first & mask]) break;
			m_first &= 0xffff;
		}

		if (((idx + 1) & 0xffff) == m_last && m_size != 0)
		{
			--m_last;
			for (index_type i = 0; i < m_capacity; ++i, --m_last)
				if (m_storage[m_last & mask]) break;
			++m_last;
			m_last &= 0xffff;
		}

		TORRENT_ASSERT_VAL(m_first <= 0xffff, m_first);
		return old_value;
	}
}
