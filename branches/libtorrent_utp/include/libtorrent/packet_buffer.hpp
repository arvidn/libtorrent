/*

Copyright (c) 2010, Arvid Norberg
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

#ifndef TORRENT_PACKET_BUFFER_HPP_INCLUDED
#define TORRENT_PACKET_BUFFER_HPP_INCLUDED

#include "libtorrent/assert.hpp"
#include <stdlib.h> // free and calloc

namespace libtorrent
{
	// this is a circular buffer that automatically resizes
	// itself as elements are inserted. Elements are indexed
	// by integers and are assumed to be sequential. Unless the
	// old elements are removed when new elements are inserted,
	// the buffer will be resized.

	// if m_mask is 0xf, m_array has 16 elements
	// m_cursor is the lowest index that has an element
	// it also determines which indices the other slots
	// refers to. Since it's a circular buffer, it wraps
	// around. For example

	//                    m_cursor = 9
	//                    |           refers to index 14
	//                    |           |
	//                    V           V
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	// | | | | | | | | | | | | | | | | |  m_mask = 0xf
	// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	//  ^
	//  |
	//  refers to index 15

	// whenever the element at the cursor is removed, the
	// cursor is bumped to the next occupied element

	struct packet_buffer
	{
		packet_buffer(): m_array(0), m_mask(0), m_cursor(0), m_num_elements(0) {}
		~packet_buffer();
	
		int capacity() const { return m_array ? m_mask + 1 : 0; }
		int size() const { return m_num_elements; }
		int cursor() const { return m_cursor; }

		// inserts an element at the given index
		// returns the previous element at the index
		void* insert(int i, void* packet);

		// returns the element at the given index
		void* at(int i) const;

		// removes the element at the given index
		void* remove(int i);

	private:
		// this is the array of elements (packets)
		void** m_array;

		// this is the size of the array - 1. This is used to mask
		// the index into the array. This also implies that the
		// array of elements has to double in size each time it grows
		int m_mask;

		// this is the lowest index in the array
		int m_cursor;

		// the number of used slots in the array
		int m_num_elements;
	};
}

#endif // TORRENT_PACKET_BUFFER_HPP_INCLUDED

