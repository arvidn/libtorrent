/*

Copyright (c) 2003-2016, Arvid Norberg
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

#include "libtorrent/peer_class_set.hpp"
#include "libtorrent/peer_class.hpp"
#include <vector>
#include <algorithm> // for find

namespace libtorrent
{
	void peer_class_set::add_class(peer_class_pool& pool, peer_class_t c)
	{
		if (std::find(m_class.begin(), m_class.begin() + m_size, c)
			!= m_class.begin() + m_size) return;
		if (m_size >= m_class.size() - 1)
		{
			TORRENT_ASSERT(false);
			return;
		}
		m_class[m_size] = c;
		pool.incref(c);
		++m_size;
	}

	bool peer_class_set::has_class(peer_class_t c) const
	{
		return std::find(m_class.begin(), m_class.begin() + m_size, c)
			!= m_class.begin() + m_size;
	}

	void peer_class_set::remove_class(peer_class_pool& pool, peer_class_t c)
	{
		boost::array<peer_class_t, 15>::iterator i = std::find(m_class.begin()
			, m_class.begin() + m_size, c);
		int idx = i - m_class.begin();
		if (idx == m_size) return; // not found
		if (idx < m_size - 1)
		{
			// place the last element in the slot of the erased one
			m_class[idx] = m_class[m_size - 1];
		}
		--m_size;
		pool.decref(c);
	}
}

