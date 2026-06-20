/*

Copyright (c) 2010, 2013-2015, 2020-2021, Arvid Norberg
Copyright (c) 2016-2017, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PEER_CLASS_SET_HPP_INCLUDED
#define TORRENT_PEER_CLASS_SET_HPP_INCLUDED

#include "libtorrent/peer_class.hpp"
#include "libtorrent/aux_/array.hpp"

namespace libtorrent::aux {

	// this represents an object that can have many peer classes applied
	// to it. Most notably, peer connections and torrents derive from this.
	// the membership list is tracked here; reference counting on the
	// peer_class_pool is the caller's responsibility (rate_limits owns
	// that part).
	struct TORRENT_EXTRA_EXPORT peer_class_set
	{
		peer_class_set() : m_size(0) {}
		// returns true iff `c` was actually added (false if already a
		// member, or if the fixed-size membership array is full).
		bool add(peer_class_t c);
		// returns true iff `c` was actually removed (false if not a member).
		bool remove(peer_class_t c);
		bool has_class(peer_class_t c) const;
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
