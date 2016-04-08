/*

Copyright (c) 2015-2016, Arvid Norberg
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

#ifndef TORRENT_HETEROGENEOUS_QUEUE_HPP_INCLUDED
#define TORRENT_HETEROGENEOUS_QUEUE_HPP_INCLUDED

#include <vector>

#include <boost/cstdint.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_base_of.hpp>

#include "libtorrent/assert.hpp"

namespace libtorrent {

	template <class T>
	struct heterogeneous_queue
	{
		heterogeneous_queue()
			: m_storage(NULL)
			, m_capacity(0)
			, m_size(0)
			, m_num_items(0)
		{}

		// TODO: 2 add emplace_back() version
		template <class U>
		typename boost::enable_if<boost::is_base_of<T, U> >::type
		push_back(U const& a)
		{
			// the size of the type rounded up to pointer alignment
			const int object_size = (sizeof(U) + sizeof(*m_storage) - 1)
				/ sizeof(*m_storage);

			// +1 for the length prefix
			if (m_size + object_size + header_size > m_capacity)
				grow_capacity(object_size);

			uintptr_t* ptr = m_storage + m_size;

			// length prefix
			header_t* hdr = reinterpret_cast<header_t*>(ptr);
			hdr->len = object_size;
			hdr->move = &move<U>;
			ptr += header_size;

			// construct in-place
			new (ptr) U(a);

			// if we constructed the object without throwing any exception
			// update counters to indicate the new item is in there
			++m_num_items;
			m_size += header_size + object_size;
		}

		void get_pointers(std::vector<T*>& out)
		{
			out.clear();

			uintptr_t* ptr = m_storage;
			uintptr_t const* const end = m_storage + m_size;
			while (ptr < end)
			{
				header_t* hdr = reinterpret_cast<header_t*>(ptr);
				ptr += header_size;
				TORRENT_ASSERT(ptr + hdr->len <= end);
				out.push_back(reinterpret_cast<T*>(ptr));
				ptr += hdr->len;
			}
		}

		void swap(heterogeneous_queue& rhs)
		{
			std::swap(m_storage, rhs.m_storage);
			std::swap(m_capacity, rhs.m_capacity);
			std::swap(m_size, rhs.m_size);
			std::swap(m_num_items, rhs.m_num_items);
		}

		int size() const { return m_num_items; }
		bool empty() const { return m_num_items == 0; }

		void clear()
		{
			uintptr_t* ptr = m_storage;
			uintptr_t const* const end = m_storage + m_size;
			while (ptr < end)
			{
				header_t* hdr = reinterpret_cast<header_t*>(ptr);
				ptr += header_size;
				TORRENT_ASSERT(ptr + hdr->len <= end);
				T* a = reinterpret_cast<T*>(ptr);
				a->~T();
				ptr += hdr->len;
			}
			m_size = 0;
			m_num_items = 0;
		}

		T* front()
		{
			if (m_size == 0) return NULL;

			TORRENT_ASSERT(m_size > 1);
			uintptr_t* ptr = m_storage;
			TORRENT_ASSERT(reinterpret_cast<header_t*>(ptr)->len <= m_size);
			ptr += header_size;
			return reinterpret_cast<T*>(ptr);
		}

		~heterogeneous_queue()
		{
			clear();
			delete[] m_storage;
		}

	private:

		// non-copyable
		heterogeneous_queue(heterogeneous_queue const&);
		heterogeneous_queue& operator=(heterogeneous_queue const&);

		// this header is put in front of every element. It tells us
		// how many uintptr_t's it's using for its allocation, and it
		// also tells us how to move this type if we need to grow our
		// allocation.
		struct header_t
		{
			int len;
			void (*move)(uintptr_t* dst, uintptr_t* src);
		};

		const static int header_size = (sizeof(header_t) + sizeof(uintptr_t)
			- 1) / sizeof(uintptr_t);

		void grow_capacity(int const size)
		{
			int const amount_to_grow = (std::max)(size + header_size
				, (std::max)(m_capacity * 3 / 2, 128));

			uintptr_t* new_storage = new uintptr_t[m_capacity + amount_to_grow];

			uintptr_t* src = m_storage;
			uintptr_t* dst = new_storage;
			uintptr_t const* const end = m_storage + m_size;
			while (src < end)
			{
				header_t* src_hdr = reinterpret_cast<header_t*>(src);
				header_t* dst_hdr = reinterpret_cast<header_t*>(dst);
				*dst_hdr = *src_hdr;
				src += header_size;
				dst += header_size;
				TORRENT_ASSERT(src + src_hdr->len <= end);
				// TODO: if this throws, should we do anything?
				src_hdr->move(dst, src);
				src += src_hdr->len;
				dst += src_hdr->len;
			}

			delete[] m_storage;
			m_storage = new_storage;
			m_capacity += amount_to_grow;
		}

		template <class U>
		static void move(uintptr_t* dst, uintptr_t* src)
		{
			U* rhs = reinterpret_cast<U*>(src);
#if __cplusplus >= 201103L
			new (dst) U(std::move(*rhs));
#else
			new (dst) U(*rhs);
#endif
			rhs->~U();
		}

		uintptr_t* m_storage;
		// number of uintptr_t's allocated under m_storage
		int m_capacity;
		// the number of uintptr_t's used under m_storage
		int m_size;
		// the number of objects allocated under m_storage
		int m_num_items;
	};
}

#endif

