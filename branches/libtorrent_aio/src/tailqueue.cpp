/*

Copyright (c) 2012-2013, Arvid Norberg
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

#include "libtorrent/tailqueue.hpp"

namespace libtorrent
{
	tailqueue::tailqueue(): m_first(0), m_last(0), m_size(0) {}

	void tailqueue::append(tailqueue& rhs)
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

	void tailqueue::prepend(tailqueue& rhs)
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

	tailqueue_node* tailqueue::pop_front()
	{
		TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
		tailqueue_node* e = m_first;
		m_first = m_first->next;
		if (e == m_last) m_last = 0;
		e->next = 0;
		--m_size;
		return e;
	}
	void tailqueue::push_front(tailqueue_node* e)
	{
		TORRENT_ASSERT(e->next == 0);
		TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
		e->next = m_first;
		m_first = e;
		if (!m_last) m_last = e;
		++m_size;
	}
	void tailqueue::push_back(tailqueue_node* e)
	{
		TORRENT_ASSERT(e->next == 0);
		TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
		if (m_last) m_last->next = e;
		else m_first = e;
		m_last = e;
		e->next = 0;
		++m_size;
	}
	tailqueue_node* tailqueue::get_all()
	{
		TORRENT_ASSERT(m_last == 0 || m_last->next == 0);
		tailqueue_node* e = m_first;
		m_first = 0;
		m_last = 0;
		m_size = 0;
		return e;
	}
	void tailqueue::swap(tailqueue& rhs)
	{
		tailqueue_node* tmp = m_first;
		m_first = rhs.m_first;
		rhs.m_first = tmp;
		tmp = m_last;
		m_last = rhs.m_last;
		rhs.m_last = tmp;
		int tmp2 = m_size;
		m_size = rhs.m_size;
		rhs.m_size = tmp2;
	}
}
