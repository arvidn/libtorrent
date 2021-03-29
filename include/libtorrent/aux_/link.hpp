/*

Copyright (c) 2010, 2013-2020, Arvid Norberg
Copyright (c) 2017, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_LINK_HPP_INCLUDED
#define TORRENT_LINK_HPP_INCLUDED

#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/units.hpp"

namespace lt::aux {

	using torrent_list_index_t = aux::strong_typedef<int, struct torrent_list_tag>;

	struct link
	{
		link() : index(-1) {}
		// this is either -1 (not in the list)
		// or the index of where in the list this
		// element is found
		int index;

		bool in_list() const { return index >= 0; }

		void clear() { index = -1; }

		template <class T>
		void unlink(aux::vector<T*>& list
			, torrent_list_index_t const link_index)
		{
			if (index == -1) return;
			TORRENT_ASSERT(index >= 0 && index < int(list.size()));
			int const last = int(list.size()) - 1;
			if (index < last)
			{
				list[last]->m_links[link_index].index = index;
				list[index] = list[last];
			}
			list.resize(last);
			index = -1;
		}

		template <class T>
		void insert(aux::vector<T*>& list, T* self)
		{
			if (index >= 0) return;
			TORRENT_ASSERT(index == -1);
			list.push_back(self);
			index = int(list.size()) - 1;
		}
	};
}

#endif
