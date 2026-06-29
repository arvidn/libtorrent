/*

Copyright (c) 2018-2019, Arvid Norberg
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

#ifndef TORRENT_INDEX_RANGE_HPP
#define TORRENT_INDEX_RANGE_HPP

namespace libtorrent {

template <typename Index>
struct index_iter
{
	explicit index_iter(Index i) : m_idx(i) {}
	index_iter operator++()
	{
		++m_idx;
		return *this;
	}
	index_iter operator--()
	{
		--m_idx;
		return *this;
	}
	Index operator*() const { return m_idx; }
	friend inline bool operator==(index_iter lhs, index_iter rhs)
	{ return lhs.m_idx == rhs.m_idx; }
	friend inline bool operator!=(index_iter lhs, index_iter rhs)
	{ return lhs.m_idx != rhs.m_idx; }
private:
	Index m_idx;
};

// Ranges can be represented with both a starting and an ending index. Functions
// from libtorrent that are called with a range parameter will always take the
// starting index as is and the ending index will either be used exclusively or
// inclusively, depending on each method.
template <typename Index>
struct index_range
{
	// hidden
	Index _begin;
	Index _end;
	// With these two methods a range can be iterated upon.
	//
	// ``begin()`` returns an iterator at the start of the range.
	// ``end()`` return an iterator at the end of the range.
	index_iter<Index> begin() const { return index_iter<Index>{_begin}; }
	index_iter<Index> end() const { return index_iter<Index>{_end}; }
	// Check if an index value is part of a range.
	//
	// ``belongs()`` will check from start to end exclusive.
	// ``bounds()`` will check from start to end inclusive.
	bool belongs(Index value) const { return _begin <= value && _end > value; }
	bool bounds(Index value) const { return _begin <= value && _end >= value; }
	// Check if this range contains another.
	bool contains(const index_range<Index>& other) const
	{ return _begin <= other._begin && _begin <= other._end && _end >= other._begin && _end >= other._end; }
};

}

#endif
