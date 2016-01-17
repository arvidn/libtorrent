/*
Copyright (c) 2007-2016, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of Rasterbar Software nor the names of its
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

#ifndef LIBTORRENT_BUFFER_HPP
#define LIBTORRENT_BUFFER_HPP

#include <cstring>
#include <limits> // for numeric_limits
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/assert.hpp"
#include <cstdlib> // malloc/free/realloc
#include <boost/cstdint.hpp>
#include <algorithm> // for std::swap

namespace libtorrent {

class buffer
{
public:
	struct interval
	{
	interval()
		: begin(0)
		, end(0)
		{}

	interval(char* b, char* e)
		: begin(b)
		, end(e)
		{}

		char operator[](int index) const
		{
			TORRENT_ASSERT(begin + index < end);
			return begin[index];
		}

		int left() const
		{
			TORRENT_ASSERT(end >= begin);
			TORRENT_ASSERT(end - begin < INT_MAX);
			return int(end - begin);
		}

		char* begin;
		char* end;
	};

	struct const_interval
	{
	const_interval(interval const& i)
		: begin(i.begin)
		, end(i.end)
		{}

	const_interval(char const* b, char const* e)
		: begin(b)
		, end(e)
		{}

		char operator[](int index) const
		{
			TORRENT_ASSERT(begin + index < end);
			return begin[index];
		}

		bool operator==(const const_interval& p_interval)
		{
			return begin == p_interval.begin
				&& end == p_interval.end;
		}

		int left() const
		{
			TORRENT_ASSERT(end >= begin);
			TORRENT_ASSERT(end - begin < INT_MAX);
			return int(end - begin);
		}

		char const* begin;
		char const* end;
	};

	buffer(std::size_t n = 0)
		: m_begin(0)
		, m_size(0)
		, m_capacity(0)
	{
		if (n) resize(n);
	}

	buffer(buffer const& b)
		: m_begin(0)
		, m_size(0)
		, m_capacity(0)
	{
		if (b.size() == 0) return;
		resize(b.size());
		std::memcpy(m_begin, b.begin(), b.size());
	}

#if __cplusplus > 199711L
	buffer(buffer&& b)
		: m_begin(b.m_begin)
		, m_size(b.m_size)
		, m_capacity(b.m_capacity)
	{
		b.m_begin = NULL;
		b.m_size = b.m_capacity = 0;
	}

	buffer& operator=(buffer&& b)
	{
		if (&b == this) return *this;
		std::free(m_begin);
		m_begin = b.m_begin;
		m_size = b.m_size;
		m_capacity = b.m_capacity;
		b.m_begin = NULL;
		b.m_size = b.m_capacity = 0;
		return *this;
	}
#endif

	buffer& operator=(buffer const& b)
	{
		if (&b == this) return *this;
		resize(b.size());
		if (b.size() == 0) return *this;
		std::memcpy(m_begin, b.begin(), b.size());
		return *this;
	}

	~buffer()
	{
		std::free(m_begin);
	}

	buffer::interval data()
	{ return interval(m_begin, m_begin + m_size); }
	buffer::const_interval data() const
	{ return const_interval(m_begin, m_begin + m_size); }

	void resize(std::size_t n)
	{
		TORRENT_ASSERT(n < 0xffffffffu);
		reserve(n);
		m_size = boost::uint32_t(n);
	}

	void insert(char* point, char const* first, char const* last)
	{
		std::size_t p = point - m_begin;
		if (point == m_begin + m_size)
		{
			resize(size() + last - first);
			std::memcpy(m_begin + p, first, last - first);
			return;
		}

		resize(size() + last - first);
		std::memmove(m_begin + p + (last - first), m_begin + p, last - first);
		std::memcpy(m_begin + p, first, last - first);
	}

	void erase(char* b, char* e)
	{
		TORRENT_ASSERT(e <= m_begin + m_size);
		TORRENT_ASSERT(b >= m_begin);
		TORRENT_ASSERT(b <= e);
		if (e == m_begin + m_size)
		{
			resize(b - m_begin);
			return;
		}
		std::memmove(b, e, m_begin + m_size - e);
		TORRENT_ASSERT(e >= b);
		TORRENT_ASSERT(e - b <= std::numeric_limits<boost::uint32_t>::max());
		TORRENT_ASSERT(boost::uint32_t(e - b) <= m_size);
		m_size -= e - b;
	}

	void clear() { m_size = 0; }
	std::size_t size() const { return m_size; }
	std::size_t capacity() const { return m_capacity; }
	void reserve(std::size_t n)
	{
		if (n <= capacity()) return;
		TORRENT_ASSERT(n > 0);
		TORRENT_ASSERT(n < 0xffffffffu);

		char* tmp = static_cast<char*>(std::realloc(m_begin, n));
#ifndef BOOST_NO_EXCEPTIONS
		if (tmp == NULL) throw std::bad_alloc();
#endif
		m_begin = tmp;
		m_capacity = boost::uint32_t(n);
	}

	bool empty() const { return m_size == 0; }
	char& operator[](std::size_t i) { TORRENT_ASSERT(i < size()); return m_begin[i]; }
	char const& operator[](std::size_t i) const { TORRENT_ASSERT(i < size()); return m_begin[i]; }

	char* begin() { return m_begin; }
	char const* begin() const { return m_begin; }
	char* end() { return m_begin + m_size; }
	char const* end() const { return m_begin + m_size; }

	void swap(buffer& b)
	{
		using std::swap;
		swap(m_begin, b.m_begin);
		swap(m_size, b.m_size);
		swap(m_capacity, b.m_capacity);
	}
private:
	char* m_begin;
	boost::uint32_t m_size;
	boost::uint32_t m_capacity;
};


}

#endif // LIBTORRENT_BUFFER_HPP

