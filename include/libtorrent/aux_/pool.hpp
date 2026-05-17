/*

Copyright (c) 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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
