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

#ifndef TORRENT_PEER_CLASS_SET_HPP_INCLUDED
#define TORRENT_PEER_CLASS_SET_HPP_INCLUDED

#include "libtorrent/peer_class.hpp"
#include "libtorrent/aux_/array.hpp"

namespace libtorrent {

	// this represents an object that can have many peer classes applied
	// to it. Most notably, peer connections and torrents derive from this.
	struct TORRENT_EXTRA_EXPORT peer_class_set
	{
		peer_class_set() : m_size(0) {}
		void add_class(peer_class_pool& pool, peer_class_t c);
		bool has_class(peer_class_t c) const;
		void remove_class(peer_class_pool& pool, peer_class_t c);
		int num_classes() const { return m_size; }
		peer_class_t class_at(int i) const
		{
			TORRENT_ASSERT(i >= 0 && i < int(m_size));
			return m_class[i];
		}

	private:

		// the number of elements used in the m_class array
		std::int8_t m_size;

		// if this object belongs to any peer-class, this vector contains all
		// class IDs. Each ID refers to a an entry in m_ses.m_peer_classes which
		// holds the metadata about the class. Classes affect bandwidth limits
		// among other things
		aux::array<peer_class_t, 15> m_class;
	};
}

#endif // TORRENT_PEER_CLASS_SET_HPP_INCLUDED
