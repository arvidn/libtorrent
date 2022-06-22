/*

Copyright (c) 2021-2022, Arvid Norberg
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

