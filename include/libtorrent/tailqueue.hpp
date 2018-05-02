/*

Copyright (c) 2011-2018, Arvid Norberg
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

#ifndef TORRENT_TAILQUEUE_HPP
#define TORRENT_TAILQUEUE_HPP

#include "libtorrent/assert.hpp"

namespace libtorrent
{
	template <typename T>
	struct tailqueue_node
	{
		tailqueue_node() : next(0) {}
		T* next;
	};

	template<class N>
	inline N* postinc(N*& e)
	{
		N* ret = e;
		e = static_cast<N*>(ret->next);
		return ret;
	}

	template <typename T>
	struct tailqueue_iterator
	{
		template <typename U> friend struct tailqueue;

		T* get() const { return m_current; }
		void next() { m_current = m_current->next; }

	private:
		tailqueue_iterator(T* cur)
			: m_current(cur) {}
		// the current element
		T* m_current;
	};

	template <typename T>
	struct tailqueue
	{
		tailqueue(): m_first(NULL), m_last(NULL), m_size(0) {}

		tailqueue_iterator<const T> iterate() const
		{ return tailqueue_iterator<const T>(m_first); }

		tailqueue_iterator<T> iterate()
		{ return tailqueue_iterator<T>(m_first); }

		void append(tailqueue<T>& rhs)
		{
			TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
			TORRENT_ASSERT(rhs.m_last == 0 || rhs.m_last->next == 0);

			if (rhs.m_first == 0) return;

			if (m_first == 0)
			{
				swap(rhs);
				return;
			}

			m_last->next = rhs.m_first;
			m_last = rhs.m_last;
			m_size += rhs.m_size;
			rhs.m_first = 0;
			rhs.m_last = 0;
			rhs.m_size = 0;

			TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
		}

		void prepend(tailqueue<T>& rhs)
		{
			TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
			TORRENT_ASSERT(rhs.m_last == 0 || rhs.m_last->next == 0);

			if (rhs.m_first == 0) return;

			if (m_first == 0)
			{
				swap(rhs);
				return;
			}

			swap(rhs);
			append(rhs);
		}

		T* pop_front()
		{
			TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
			T* e = m_first;
			m_first = m_first->next;
			if (e == m_last) m_last = 0;
			e->next = 0;
			--m_size;
			return e;
		}
		void push_front(T* e)
		{
			TORRENT_ASSERT(e->next == 0);
			TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
			e->next = m_first;
			m_first = e;
			if (!m_last) m_last = e;
			++m_size;
		}
		void push_back(T* e)
		{
			TORRENT_ASSERT(e->next == 0);
			TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
			if (m_last) m_last->next = e;
			else m_first = e;
			m_last = e;
			e->next = 0;
			++m_size;
		}
		T* get_all()
		{
			TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
			T* e = m_first;
			m_first = 0;
			m_last = 0;
			m_size = 0;
			return e;
		}
		void swap(tailqueue<T>& rhs)
		{
			T* tmp = m_first;
			m_first = rhs.m_first;
			rhs.m_first = tmp;
			tmp = m_last;
			m_last = rhs.m_last;
			rhs.m_last = tmp;
			int tmp2 = m_size;
			m_size = rhs.m_size;
			rhs.m_size = tmp2;
		}
		int size() const { return m_size; }
		bool empty() const { return m_size == 0; }
		T* first() const { TORRENT_ASSERT(m_size > 0); return m_first; }
		T* last() const { TORRENT_ASSERT(m_size > 0); return m_last; }
	private:
		T* m_first;
		T* m_last;
		int m_size;
	};
}

#endif // TAILQUEUE_HPP

