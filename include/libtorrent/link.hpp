/*

Copyright (c) 2011-2018, Arvid Norberg
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

#ifndef TORRENT_LINK_HPP_INCLUDED
#define TORRENT_LINK_HPP_INCLUDED

#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/units.hpp"

namespace libtorrent {

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
