/*

Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_STORAGE_FREE_LIST_HPP_INCLUDE
#define TORRENT_STORAGE_FREE_LIST_HPP_INCLUDE

#include <vector>
#include "libtorrent/storage_defs.hpp"

namespace libtorrent {
namespace aux {

	struct storage_free_list
	{
		// if we don't already have any free slots, use next
		storage_index_t new_index(storage_index_t const next)
		{
			// make sure we can remove this torrent without causing a memory
			// allocation, by triggering the allocation now instead
			m_free_slots.reserve(static_cast<std::uint32_t>(next) + 1);
			return m_free_slots.empty() ? next : pop();
		}

		void add(storage_index_t const i) { m_free_slots.push_back(i); }

		std::size_t size() const { return m_free_slots.size(); }

	private:

		storage_index_t pop()
		{
			TORRENT_ASSERT(!m_free_slots.empty());
			storage_index_t const ret = m_free_slots.back();
			m_free_slots.pop_back();
			return ret;
		}

	private:
		std::vector<storage_index_t> m_free_slots;
	};
}
}
#endif

