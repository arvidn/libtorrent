/*

Copyright (c) 2012-2016, Arvid Norberg
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

#ifndef TORRENT_LINKED_LIST_HPP
#define TORRENT_LINKED_LIST_HPP

#include "libtorrent/assert.hpp"

namespace libtorrent
{
	template <typename T>
	struct list_node
	{
		list_node() : prev(0), next(0) {}
		T* prev;
		T* next;
	};

	template <typename T>
	struct list_iterator
	{
		template <typename U>
		friend struct linked_list;

		T const* get() const { return m_current; }
		T* get() { return m_current; }
		void next() { m_current = m_current->next; }
		void prev() { m_current = m_current->prev; }

	private:
		list_iterator(T* cur)
			: m_current(cur) {}
		// the current element
		T* m_current;
	};

	// T must derive from list_node<T>. Having an enable_if here would require T
	// to be a complete type, which is a bit too restrictive.
	template <typename T>
	struct linked_list
	{
		linked_list(): m_first(NULL), m_last(NULL), m_size(0) {}

		list_iterator<T> iterate() const
		{ return list_iterator<T>(m_first); }

		void erase(T* e)
		{
#if TORRENT_USE_ASSERTS
			T* tmp = m_first;
			bool found = false;
			while (tmp)
			{
				if (tmp == e)
				{
					found = true;
					break;
				}
				tmp = tmp->next;
			}
			TORRENT_ASSERT(found);
#endif
			if (e == m_first)
			{
				TORRENT_ASSERT(e->prev == 0);
				m_first = e->next;
			}
			if (e == m_last)
			{
				TORRENT_ASSERT(e->next == 0);
				m_last = e->prev;
			}
			if (e->prev) e->prev->next = e->next;
			if (e->next) e->next->prev = e->prev;
			e->next = 0;
			e->prev = 0;
			TORRENT_ASSERT(m_size > 0);
			--m_size;
			TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
		}
		void push_front(T* e)
		{
			TORRENT_ASSERT(e->next == 0);
			TORRENT_ASSERT(e->prev== 0);
			TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
			e->prev = 0;
			e->next = m_first;
			if (m_first) m_first->prev = e;
			else m_last = e;
			m_first = e;
			++m_size;
		}
		void push_back(T* e)
		{
			TORRENT_ASSERT(e->next == 0);
			TORRENT_ASSERT(e->prev== 0);
			TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
			e->prev = m_last;
			e->next = 0;
			if (m_last) m_last->next = e;
			else m_first = e;
			m_last = e;
			++m_size;
		}
		T* get_all()
		{
			TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
			TORRENT_ASSERT(m_first == 0 || m_first->prev == 0);
			T* e = m_first;
			m_first = 0;
			m_last = 0;
			m_size = 0;
			return e;
		}
		T* back() { return m_last; }
		T* front() { return m_first; }
		T const* back() const { return m_last; }
		T const* front() const { return m_first; }
		int size() const { return m_size; }
		bool empty() const { return m_size == 0; }
	private:
		T* m_first;
		T* m_last;
		int m_size;
	};
}

#endif // LINKED_LIST_HPP

