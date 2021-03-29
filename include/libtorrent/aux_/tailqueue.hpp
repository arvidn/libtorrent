/*

Copyright (c) 2010, 2013-2017, 2019-2020, Arvid Norberg
Copyright (c) 2017, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_TAILQUEUE_HPP
#define TORRENT_TAILQUEUE_HPP

#include "libtorrent/assert.hpp"

namespace lt::aux {

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

		tailqueue_iterator<const T> iterate() const
		{ return tailqueue_iterator<const T>(m_first); }

		tailqueue_iterator<T> iterate()
		{ return tailqueue_iterator<T>(m_first); }

		void append(tailqueue<T>& rhs)
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
			rhs.m_first = nullptr;
			rhs.m_last = nullptr;
			rhs.m_size = 0;

			TORRENT_ASSERT(m_last == nullptr || m_last->next == nullptr);
		}

		void prepend(tailqueue<T>& rhs)
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
			append(rhs);
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
		void push_front(T* e)
		{
			TORRENT_ASSERT(e->next == nullptr);
			TORRENT_ASSERT(m_last == nullptr || m_last->next == nullptr);
			e->next = m_first;
			m_first = e;
			if (!m_last) m_last = e;
			++m_size;
		}
		void push_back(T* e)
		{
			TORRENT_ASSERT(e->next == nullptr);
			TORRENT_ASSERT(m_last == nullptr || m_last->next == nullptr);
			if (m_last) m_last->next = e;
			else m_first = e;
			m_last = e;
			e->next = nullptr;
			++m_size;
		}
		T* get_all()
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
		int size() const { TORRENT_ASSERT(m_size >= 0); return m_size; }
		bool empty() const { TORRENT_ASSERT(m_size >= 0); return m_size == 0; }
		T* first() const { TORRENT_ASSERT(m_size > 0); return m_first; }
		T* last() const { TORRENT_ASSERT(m_size > 0); return m_last; }
	private:
		T* m_first;
		T* m_last;
		int m_size;
	};
}

#endif // TAILQUEUE_HPP
