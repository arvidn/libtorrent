/*

Copyright (c) 2018-2021, Arvid Norberg
Copyright (c) 2024-2026, Martin Rodriguez Reboredo
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_INDEX_RANGE_HPP
#define TORRENT_INDEX_RANGE_HPP

#include <cstddef>
#include <iterator>

namespace libtorrent {

template <typename Index>
struct index_iter
{
	using value_type = Index;
	using difference_type = std::ptrdiff_t;
	using pointer = Index const*;
	using reference = Index;
	using iterator_category = std::random_access_iterator_tag;
	constexpr index_iter() : m_idx(Index(0)) {}
	explicit constexpr index_iter(Index i) : m_idx(i) {}
	constexpr index_iter& operator++()
	{
		++m_idx;
		return *this;
	}
	constexpr index_iter& operator--()
	{
		--m_idx;
		return *this;
	}
	constexpr index_iter operator++(int)
	{
		index_iter it(*this);
		++m_idx;
		return it;
	}
	constexpr index_iter operator--(int)
	{
		index_iter it(*this);
		--m_idx;
		return it;
	}
	constexpr reference operator*() const { return m_idx; }
	constexpr reference operator[](difference_type n) const
	{ return m_idx + static_cast<Index>(n); }
	friend constexpr inline bool operator==(index_iter lhs, index_iter rhs)
	{ return lhs.m_idx == rhs.m_idx; }
	friend constexpr inline bool operator!=(index_iter lhs, index_iter rhs)
	{ return lhs.m_idx != rhs.m_idx; }
	friend constexpr inline bool operator<(const index_iter& lhs, const index_iter& rhs)
	{ return lhs.m_idx < rhs.m_idx; }
	friend constexpr inline bool operator>(const index_iter& lhs, const index_iter& rhs)
	{ return lhs.m_idx > rhs.m_idx; }
	friend constexpr inline bool operator<=(const index_iter& lhs, const index_iter& rhs)
	{ return lhs.m_idx <= rhs.m_idx; }
	friend constexpr inline bool operator>=(const index_iter& lhs, const index_iter& rhs)
	{ return lhs.m_idx >= rhs.m_idx; }
	friend constexpr inline index_iter operator+(index_iter it, difference_type n)
	{ return index_iter(it.m_idx + static_cast<Index>(n)); }
	friend constexpr inline index_iter operator+(difference_type n, index_iter it)
	{ return index_iter(it.m_idx + static_cast<Index>(n)); }
	friend constexpr inline index_iter operator-(index_iter it, difference_type n)
	{ return index_iter(it.m_idx - static_cast<Index>(n)); }
	friend constexpr inline difference_type operator-(const index_iter& lhs, const index_iter& rhs)
	{ return static_cast<difference_type>(lhs.m_idx - rhs.m_idx); }
	constexpr inline index_iter& operator+=(difference_type n)
	{
		m_idx += static_cast<Index>(n);
		return *this;
	}
	constexpr inline index_iter& operator-=(difference_type n)
	{
		m_idx -= static_cast<Index>(n);
		return *this;
	}
	constexpr inline index_iter& operator-=(const index_iter& other)
	{
		m_idx -= other.m_idx;
		return *this;
	}
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
	friend bool operator==(index_range const& lhs, index_range const& rhs)
	{
		return lhs._begin == rhs._begin && lhs._end == rhs._end;
	}
	friend bool operator!=(index_range const& lhs, index_range const& rhs) { return !(lhs == rhs); }
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
