/*

Copyright (c) 2010, 2013-2015, 2019-2020, Arvid Norberg
Copyright (c) 2016-2017, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PEER_CLASS_SET_HPP_INCLUDED
#define TORRENT_PEER_CLASS_SET_HPP_INCLUDED

#include "libtorrent/peer_class.hpp"
#include "libtorrent/aux_/array.hpp"

namespace lt::aux {

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
