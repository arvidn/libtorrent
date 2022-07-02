/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_STORAGE_ARRAY_HPP
#define TORRENT_STORAGE_ARRAY_HPP

#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/storage_free_list.hpp"

#include <memory>

namespace libtorrent::aux {

template <typename Storage>
struct storage_array
{
	storage_index_t add(std::shared_ptr<Storage> storage)
	{
		storage_index_t const idx = m_free_slots.new_index(m_torrents.end_index());
		storage->set_storage_index(idx);
		if (idx == m_torrents.end_index())
			m_torrents.emplace_back(std::move(storage));
		else
			m_torrents[idx] = std::move(storage);
		return idx;
	}

	void remove(storage_index_t const idx)
	{
		TORRENT_ASSERT(m_torrents[idx] != nullptr);
		m_torrents[idx].reset();
		m_free_slots.add(idx);
	}

	std::shared_ptr<Storage>& operator[](storage_index_t const idx)
	{
		return m_torrents[idx];
	}

	bool empty() const { return m_torrents.size() == m_free_slots.size(); }

private:

	aux::vector<std::shared_ptr<Storage>, storage_index_t> m_torrents;

	// indices into m_torrents to empty slots
	aux::storage_free_list m_free_slots;

};

}

#endif
