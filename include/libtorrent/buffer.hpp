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

#ifndef TORRENT_BUFFER_HPP_INCLUDED
#define TORRENT_BUFFER_HPP_INCLUDED

#include <cstring>
#include <limits> // for numeric_limits
#include <cstdlib> // malloc/free/realloc
#include <algorithm> // for std::swap
#include <cstdint>

#include "libtorrent/invariant_check.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/span.hpp"

#if defined __GLIBC__
#include <malloc.h>
#elif defined _MSC_VER
#include <malloc.h>
#elif defined TORRENT_BSD
#include <malloc/malloc.h>
#endif

namespace libtorrent {

// the buffer is allocated once and cannot be resized. The size() may be
// larger than requested, in case the underlying allocator over allocated. In
// order to "grow" an allocation, create a new buffer and initialize it by
// the range of bytes from the existing, and move-assign the new over the
// old.
class buffer
{
public:
	// allocate an uninitialized buffer of the specified size
	explicit buffer(std::size_t size = 0)
	{
		TORRENT_ASSERT(size < std::size_t((std::numeric_limits<std::int32_t>::max)()));

		if (size == 0) return;

		// this rounds up the size to be 8 bytes aligned
		// it mostly makes sense for platforms without support
		// for a variation of "malloc_size()"
		size = (size + 7) & (~std::size_t(0x7));

		m_begin = static_cast<char*>(std::malloc(size));
		if (m_begin == nullptr)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw std::bad_alloc();
#else
			std::terminate();
#endif
		}

		// the actual allocation may be larger than we requested. If so, let the
		// user take advantage of every single byte
#if defined __GLIBC__
		m_size = malloc_usable_size(m_begin);
#elif defined _MSC_VER
		m_size = _msize(m_begin);
#elif defined TORRENT_BSD
		m_size = malloc_size(m_begin);
#else
		m_size = size;
#endif
	}

	// allocate an uninitialized buffer of the specified size
	// and copy the initialization range into the start of the buffer
	buffer(std::size_t const size, span<char const> initialize)
		: buffer(size)
	{
		TORRENT_ASSERT(initialize.size() <= size);
		if (initialize.size() > 0)
		{
			std::memcpy(m_begin, initialize.data(), (std::min)(initialize.size(), size));
		}
	}

	buffer(buffer const& b) = delete;

	buffer(buffer&& b)
		: m_begin(b.m_begin)
		, m_size(b.m_size)
	{
		b.m_begin = nullptr;
		b.m_size = 0;
	}

	buffer& operator=(buffer&& b)
	{
		if (&b == this) return *this;
		std::free(m_begin);
		m_begin = b.m_begin;
		m_size = b.m_size;
		b.m_begin = nullptr;
		b.m_size = 0;
		return *this;
	}

	buffer& operator=(buffer const& b) = delete;

	~buffer() { std::free(m_begin); }

	char* data() { return m_begin; }
	char const* data() const { return m_begin; }
	std::size_t size() const { return m_size; }

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
	}

private:
	char* m_begin = nullptr;
	// m_begin points to an allocation of this size.
	std::size_t m_size = 0;
};

}

#endif // BTORRENT_BUFFER_HPP_INCLUDED
