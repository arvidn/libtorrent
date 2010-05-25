/*

Copyright (c) 2010, Arvid Norberg, Daniel Wallin.
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

#ifndef TORRENT_PACKET_BUFFER_HPP_INCLUDED
#define TORRENT_PACKET_BUFFER_HPP_INCLUDED

#include "libtorrent/assert.hpp"
#include <stdlib.h> // free and calloc

namespace libtorrent
{
	// this is a circular buffer that automatically resizes
	// itself as elements are inserted. Elements are indexed
	// by integers and are assumed to be sequential. Unless the
	// old elements are removed when new elements are inserted,
	// the buffer will be resized.

	// if m_mask is 0xf, m_array has 16 elements
	// m_cursor is the lowest index that has an element
	// it also determines which indices the other slots
	// refers to. Since it's a circular buffer, it wraps
	// around. For example

	//                    m_cursor = 9
	//                    |           refers to index 14
	//                    |           |
	//                    V           V
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// | | | | | | | | | | | | | | | | |  m_mask = 0xf
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	//  ^
	//  |
	//  refers to index 15

	// whenever the element at the cursor is removed, the
	// cursor is bumped to the next occupied element

    bool compare_less_wrap(boost::uint32_t lhs, boost::uint32_t rhs
        , boost::uint32_t mask);

    class packet_buffer
    {
    public:
        typedef boost::uint32_t index_type;

        packet_buffer();
        ~packet_buffer();

        void* insert(index_type idx, void* value);

        std::size_t size() const
        {
            return m_size;
        }

        std::size_t capacity() const
        {
            return m_capacity;
        }

        void* at(index_type idx) const;

        void* remove(index_type idx);

        void reserve(std::size_t size);

        index_type cursor() const
        {
            return m_first;
        }

    private:
        void** m_storage;
        std::size_t m_capacity;
        std::size_t m_size;

        // This defines the first index that is part of the m_storage.
        // The last index is m_first + (m_capacity - 1).
        index_type m_first;
    };

    inline packet_buffer::packet_buffer()
      : m_capacity(16)
      , m_size(0)
      , m_first(0)
    {
        m_storage = (void**)malloc(sizeof(void*) * m_capacity);

        for (index_type i = 0; i < m_capacity; ++i)
            m_storage[i] = 0;
    }

    inline packet_buffer::~packet_buffer()
    {
        free(m_storage);
    }

    inline void* packet_buffer::insert(index_type idx, void* value)
    {
        if (m_size != 0)
        {
            if (idx >= m_first + m_capacity)
            {
                reserve(idx - m_first + 1);
            }
            else if (compare_less_wrap(idx, m_first, 0xffff))
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

                if (m_first - idx > free_space)
                    reserve(m_first - idx + m_capacity - free_space);

                m_first = idx;
            }
            else if (idx < m_first)
            {
                // We have wrapped.
                if (idx > ((m_first + m_capacity) & 0xffff))
                {
                    reserve(m_capacity + (idx - ((m_first + m_capacity) & 0xffff)));
                }
            }
        }

        void* old_value = m_storage[idx & (m_capacity - 1)];
        m_storage[idx & (m_capacity - 1)] = value;

        if (m_size++ == 0)
        {
            m_first = idx;
        }

        return old_value;
    }

    inline void* packet_buffer::at(index_type idx) const
    {
        if (idx >= m_first + m_capacity)
            return 0;

        if (idx < m_first)
        {
            if (((m_first + m_capacity) & 0xffff) < m_first)
            {
                if (idx >= ((m_first + m_capacity) & 0xffff))
                    return 0;
            }
            else return 0;
        }

        return m_storage[idx & (m_capacity - 1)];
    }

    inline void packet_buffer::reserve(std::size_t size)
    {
        std::size_t new_size = m_capacity;

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

    inline void* packet_buffer::remove(index_type idx)
    {
        if (idx >= m_first + m_capacity)
            return 0;

        if (idx < m_first)
        {
            if (((m_first + m_capacity) & 0xffff) < m_first)
            {
                if (idx >= ((m_first + m_capacity) & 0xffff))
                    return 0;
            }
            else return 0;
        }

        void* old_value = m_storage[idx & (m_capacity - 1)];
        m_storage[idx & (m_capacity - 1)] = 0;

        if (old_value)
        {
            --m_size;
        }

        if (idx == m_first && m_size != 0)
        {
            while (!m_storage[++m_first & (m_capacity - 1)]);
        }

        return old_value;
    }

}

#endif // TORRENT_PACKET_BUFFER_HPP_INCLUDED

