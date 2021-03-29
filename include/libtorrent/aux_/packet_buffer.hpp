/*

Copyright (c) 2010-2018, Arvid Norberg, Daniel Wallin.
Copyright (c) 2010-2012, 2014-2020, Arvid Norberg
Copyright (c) 2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PACKET_BUFFER_HPP_INCLUDED
#define TORRENT_PACKET_BUFFER_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/unique_ptr.hpp"
#include "libtorrent/aux_/packet_pool.hpp" // for packet_ptr/packet_deleter
#include <cstdint>
#include <cstddef>
#include <memory> // for unique_ptr

namespace lt::aux {

	struct packet;

	// this is a circular buffer that automatically resizes
	// itself as elements are inserted. Elements are indexed
	// by integers and are assumed to be sequential. Unless the
	// old elements are removed when new elements are inserted,
	// the buffer will be resized.

	// m_capacity is the number of elements in m_array
	// and must be an even 2^x.
	// m_first is the lowest index that has an element
	// it also determines which indices the other slots
	// refers to. Since it's a circular buffer, it wraps
	// around. For example

	//                    m_first = 9
	//                    |           refers to index 14
	//                    |           |
	//                    V           V
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// | | | | | | | | | | | | | | | | |  mask = (m_capacity-1)
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	//  ^
	//  |
	//  refers to index 15

	// whenever the element at the cursor is removed, the
	// cursor is bumped to the next occupied element

	class TORRENT_EXTRA_EXPORT packet_buffer
	{
	public:
		using index_type = std::uint32_t;

		packet_ptr insert(index_type idx, packet_ptr value);

		int size() const { return m_size; }

		bool empty() const { return m_size == 0; }

		std::uint32_t capacity() const
		{ return m_capacity; }

		packet* at(index_type idx) const;

		packet_ptr remove(index_type idx);

		void reserve(std::uint32_t size);

		index_type cursor() const { return m_first; }

		index_type span() const { return (m_last - m_first) & 0xffff; }

#if TORRENT_USE_INVARIANT_CHECKS
		void check_invariant() const;
#endif

	private:
		aux::unique_ptr<packet_ptr[], index_type> m_storage;
		std::uint32_t m_capacity = 0;

		// this is the total number of elements that are occupied
		// in the array
		int m_size = 0;

		// This defines the first index that is part of the m_storage.
		// last is one passed the last used slot
		index_type m_first{0};
		index_type m_last{0};
	};
}

#endif // TORRENT_PACKET_BUFFER_HPP_INCLUDED
