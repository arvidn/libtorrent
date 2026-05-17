/*

Copyright (c) 2020, Arvid Norberg
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

#ifndef TORRENT_POOL_HPP
#define TORRENT_POOL_HPP

#include "libtorrent/assert.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/pool/pool.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <cstddef>
#include <new> // for placement new

namespace libtorrent {
namespace aux {

struct allocator_new_delete
{
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;

	// TODO: ensure the alignment is good here
	static char* malloc(size_type const bytes)
	{ return new char[bytes]; }
	static void free(char* const block)
	{ delete [] block; }
};

using pool = boost::pool<allocator_new_delete>;

// Like boost::object_pool but uses unordered free() rather than the
// O(N) ordered_free() that boost::object_pool::destroy() relies on.
// The pool itself does not track outstanding allocations, so callers
// must destroy() every object before the pool is destroyed.
template <typename T>
struct object_pool
{
	object_pool() : m_pool(sizeof(T)) {}
	object_pool(object_pool const&) = delete;
	object_pool& operator=(object_pool const&) = delete;

	T* construct()
	{
		void* const storage = m_pool.malloc();
		try
		{
			return new (storage) T();
		}
		catch (...)
		{
			m_pool.free(storage);
			throw;
		}
	}

	void destroy(T* const p)
	{
		TORRENT_ASSERT(p != nullptr);
		p->~T();
		m_pool.free(p);
	}

	void set_next_size(std::size_t const n) { m_pool.set_next_size(n); }

private:
	pool m_pool;
};

}
}

#endif
