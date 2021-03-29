/*

Copyright (c) 2010, 2013-2014, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/peer_class_set.hpp"

#include <algorithm> // for find

namespace lt::aux {

	void peer_class_set::add_class(peer_class_pool& pool, peer_class_t c)
	{
		if (std::find(m_class.begin(), m_class.begin() + m_size, c)
			!= m_class.begin() + m_size) return;
		if (m_size >= int(m_class.size()) - 1)
		{
			TORRENT_ASSERT_FAIL();
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

	void peer_class_set::remove_class(peer_class_pool& pool, peer_class_t const c)
	{
		auto const i = std::find(m_class.begin(), m_class.begin() + m_size, c);
		int const idx = int(i - m_class.begin());
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
