/*

Copyright (c) 2010, 2013-2017, 2019, 2022, Arvid Norberg
Copyright (c) 2017, Alden Torres
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
#include <utility> // for std::move

namespace libtorrent {

	template <typename T>
	struct tailqueue_node
	{
		tailqueue_node() : next(nullptr) {}
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
		explicit tailqueue_iterator(T* cur)
			: m_current(cur) {}
		// the current element
		T* m_current;
	};

	template <typename T>
	struct tailqueue
	{
		tailqueue(): m_first(nullptr), m_last(nullptr), m_size(0) {}

		tailqueue(tailqueue const&) = delete;
		tailqueue(tailqueue&& t): m_first(t.m_first), m_last(t.m_last), m_size(t.m_size)
		{
			t.m_first = nullptr;
			t.m_last = nullptr;
			t.m_size = 0;
		}

		tailqueue& operator=(tailqueue const&) = delete;
		tailqueue& operator=(tailqueue&& t)
		{
			TORRENT_ASSERT(m_first == nullptr);
			TORRENT_ASSERT(m_last == nullptr);
			TORRENT_ASSERT(m_size == 0);

			m_first = t.m_first;
			m_last = t.m_last;
			m_size = t.m_size;

			t.m_first = nullptr;
			t.m_last = nullptr;
			t.m_size = 0;
			return *this;
		}

		tailqueue_iterator<const T> iterate() const
		{ return tailqueue_iterator<const T>(m_first); }

		tailqueue_iterator<T> iterate()
		{ return tailqueue_iterator<T>(m_first); }

		void append(tailqueue<T> rhs) &
		{
			TORRENT_ASSERT(m_last == nullptr || m_last->next == nullptr);
			TORRENT_ASSERT(rhs.m_last == nullptr || rhs.m_last->next == nullptr);

			if (rhs.m_first == nullptr) return;

			if (m_first == nullptr)
			{
				swap(rhs);
				return;
			}

			m_last->next = rhs.m_first;
			m_last = rhs.m_last;
			m_size += rhs.m_size;

			TORRENT_ASSERT(m_last == nullptr || m_last->next == nullptr);
		}

		void prepend(tailqueue<T> rhs) &
		{
			TORRENT_ASSERT(m_last == nullptr || m_last->next == nullptr);
			TORRENT_ASSERT(rhs.m_last == nullptr || rhs.m_last->next == nullptr);
			TORRENT_ASSERT((m_last == nullptr) == (m_first == nullptr));
			TORRENT_ASSERT((rhs.m_last == nullptr) == (rhs.m_first == nullptr));

			if (rhs.m_first == nullptr) return;

			if (m_first == nullptr)
			{
				swap(rhs);
				return;
			}

			swap(rhs);
			append(std::move(rhs));
			TORRENT_ASSERT(m_last == nullptr || m_last->next == nullptr);
			TORRENT_ASSERT(rhs.m_last == nullptr || rhs.m_last->next == nullptr);
		}

		T* pop_front()
		{
			TORRENT_ASSERT(m_last == nullptr || m_last->next == nullptr);
			T* e = m_first;
			m_first = m_first->next;
			if (e == m_last) m_last = nullptr;
			e->next = nullptr;
			--m_size;
			return e;
		}
		void push_front(T* e) &
		{
			TORRENT_ASSERT(e->next == nullptr);
			TORRENT_ASSERT(m_last == nullptr || m_last->next == nullptr);
			e->next = m_first;
			m_first = e;
			if (!m_last) m_last = e;
			++m_size;
		}
		void push_back(T* e) &
		{
			TORRENT_ASSERT(e->next == nullptr);
			TORRENT_ASSERT(m_last == nullptr || m_last->next == nullptr);
			if (m_last) m_last->next = e;
			else m_first = e;
			m_last = e;
			e->next = nullptr;
			++m_size;
		}
		T* get_all() &
		{
			TORRENT_ASSERT(m_last == nullptr || m_last->next == nullptr);
			T* e = m_first;
			m_first = nullptr;
			m_last = nullptr;
			m_size = 0;
			return e;
		}
		void swap(tailqueue<T>& rhs)
		{
			std::swap(m_first, rhs.m_first);
			std::swap(m_last, rhs.m_last);
			std::swap(m_size, rhs.m_size);
		}
		int size() const { TORRENT_ASSERT(m_size >= 0); return m_size; }
		bool empty() const { TORRENT_ASSERT(m_size >= 0); return m_size == 0; }
		T* first() const& { TORRENT_ASSERT(m_size > 0); return m_first; }
		T* last() const& { TORRENT_ASSERT(m_size > 0); return m_last; }
	private:
		T* m_first;
		T* m_last;
		int m_size;
	};
}

namespace std {

template <typename T>
void swap(libtorrent::tailqueue<T>& lhs, libtorrent::tailqueue<T>& rhs)
{
	lhs.swap(rhs);
}

}

#endif // TAILQUEUE_HPP
