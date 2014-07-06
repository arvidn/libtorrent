/*

Copyright (c) 2011-2013, Arvid Norberg
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
	struct tailqueue_node
	{
		tailqueue_node() : next(0) {}
		tailqueue_node* next;
	};

	template<class N>
	inline N* postinc(N*& e)
	{
		N* ret = e;
		e = (N*)ret->next;
		return ret;
	}

	struct tailqueue_iterator
	{
		friend struct tailqueue;
		tailqueue_node const* get() const { return m_current; }
		void next() { m_current = m_current->next; }

	private:
		tailqueue_iterator(tailqueue_node const* cur)
			: m_current(cur) {}
		// the current element
		tailqueue_node const* m_current;
	};

	struct TORRENT_EXTRA_EXPORT tailqueue
	{
		tailqueue();

		tailqueue_iterator iterate() const
		{ return tailqueue_iterator(m_first); }

		void append(tailqueue& rhs);
		void prepend(tailqueue& rhs);
		tailqueue_node* pop_front();
		void push_front(tailqueue_node* e);
		void push_back(tailqueue_node* e);
		tailqueue_node* get_all();
		int size() const { return m_size; }
		bool empty() const { return m_size == 0; }
		void swap(tailqueue& rhs);
		tailqueue_node* first() const { TORRENT_ASSERT(m_size > 0); return m_first; }
		tailqueue_node* last() const { TORRENT_ASSERT(m_size > 0); return m_last; }
	private:
		tailqueue_node* m_first;
		tailqueue_node* m_last;
		int m_size;
	};
};

#endif // TAILQUEUE_HPP

