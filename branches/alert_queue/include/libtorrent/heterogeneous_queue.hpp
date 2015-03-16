/*

Copyright (c) 2015, Arvid Norberg
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
			: m_size(0)
		{}

		// TODO: 2 add emplace_back() version
		template <class U>
		typename boost::enable_if<boost::is_base_of<T, U> >::type
		push_back(U const& a)
		{
			// the size of the type rounded up to pointer alignment
			const int size = (sizeof(U) + sizeof(uintptr_t) - 1)
				/ sizeof(uintptr_t);

			// +1 for the length prefix
			// TODO: technically the storage here need to call move/copy-operators
			// here. For now, just require trivial move-types
			m_storage.resize(m_storage.size() + size + 1);
			uintptr_t* ptr = &m_storage[m_storage.size() - size - 1];

			// length prefix
			*ptr++ = size;

			try
			{
				// construct in-place
				new (ptr) U(a);
				++m_size;
			}
			catch (...)
			{
				// in case the copy constructor throws, restore the size of the
				// storage to not include the space we allocated for it
				m_storage.resize(m_storage.size() - size - 1);
				throw;
			}
		}

		void get_pointers(std::vector<T*>& out)
		{
			out.clear();
			if (m_storage.empty()) return;

			uintptr_t* ptr = &m_storage[0];
			uintptr_t const* const end = &m_storage[0] + m_storage.size();
			while (ptr < end)
			{
				int len = *ptr++;
				TORRENT_ASSERT(ptr + len <= end);
				out.push_back((T*)ptr);
				ptr += len;
			}
		}

		void swap(heterogeneous_queue& rhs)
		{
			m_storage.swap(rhs.m_storage);
			std::swap(m_size, rhs.m_size);
		}

		int size() const { return m_size; }
		bool empty() const { return m_size == 0; }

		void clear()
		{
			uintptr_t* ptr = &m_storage[0];
			uintptr_t const* const end = &m_storage[0] + m_storage.size();
			while (ptr < end)
			{
				int len = *ptr++;
				TORRENT_ASSERT(ptr + len <= end);
				T* a = (T*)ptr;
				a->~T();
				ptr += len;
			}
			m_storage.clear();
			m_size = 0;
		}

		T* front()
		{
			if (m_storage.empty()) return NULL;

			TORRENT_ASSERT(m_storage.size() > 1);
			uintptr_t* ptr = &m_storage[0];
			int len = *ptr++;
			TORRENT_ASSERT(len <= m_storage.size());
			return (T*)ptr;
		}

		~heterogeneous_queue()
		{ clear(); }

	private:

		// non-copyable
		heterogeneous_queue(heterogeneous_queue const&);
		heterogeneous_queue& operator=(heterogeneous_queue const&);

		std::vector<uintptr_t> m_storage;

		int m_size;
	};
}

#endif

