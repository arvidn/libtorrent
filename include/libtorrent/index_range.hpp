/*

Copyright (c) 2018-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_INDEX_RANGE_HPP
#define TORRENT_INDEX_RANGE_HPP

namespace lt {

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

template <typename Index>
struct index_range
{
	Index _begin;
	Index _end;
	index_iter<Index> begin() const { return index_iter<Index>{_begin}; }
	index_iter<Index> end() const { return index_iter<Index>{_end}; }
};

}

#endif
