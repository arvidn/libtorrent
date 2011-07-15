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

#ifndef TORRENT_AIOCB_POOL
#define TORRENT_AIOCB_POOL

#include "libtorrent/config.hpp"
#include "libtorrent/file.hpp"

#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
#include <boost/pool/object_pool.hpp>
#endif

namespace libtorrent
{
	struct aiocb_pool
	{
		enum { max_iovec = 64 };

		aiocb_pool(): m_in_use(0), m_peak_in_use(0)
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
			, m_pool(sizeof(file::aiocb_t), 128)
			, m_vec_pool(sizeof(file::iovec_t) * max_iovec)
#endif
		{
#ifdef TORRENT_DISK_STATS
			file_access_log = 0;
#endif
		}

		file::iovec_t* alloc_vec()
		{
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
			file::iovec_t* ret = new file::iovec_t[max_iovec];
#else
			file::iovec_t* ret = (file::iovec_t*)m_vec_pool.malloc();
			m_pool.set_next_size(256);
#endif
			return ret;
		}

		void free_vec(file::iovec_t* vec)
		{
			if (vec == 0) return;
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
			delete[] vec;
#else
			m_vec_pool.free(vec);
#endif
		}

#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		bool is_from(file::aiocb_t* p) const { return true; }
#else
		bool is_from(file::aiocb_t* p) const { return m_pool.is_from(p); }
#endif

		file::aiocb_t* construct()
		{
			++m_in_use;
			if (m_in_use > m_peak_in_use) m_peak_in_use = m_in_use;
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
			file::aiocb_t* ret = new file::aiocb_t;
#else
			file::aiocb_t* ret = (file::aiocb_t*)m_pool.malloc();
			new (ret) file::aiocb_t;
			m_pool.set_next_size(256);
#endif
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			ret->in_use = true;
#endif
			return ret;
		}

		void destroy(file::aiocb_t* a)
		{
			--m_in_use;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			TORRENT_ASSERT(a->in_use);
			a->in_use = false;
#endif
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
			delete a;
#else
			TORRENT_ASSERT(m_pool.is_from(a));
			a->~aiocb_t();
			m_pool.free(a);
#endif
		}

		int in_use() const { return m_in_use; }
		int peak_in_use() const { return m_peak_in_use; }

#if TORRENT_USE_IOSUBMIT
		io_context_t io_queue;
		int event;
#endif

#ifdef TORRENT_DISK_STATS
		FILE* file_access_log;
#endif

	private:

		int m_in_use;
		int m_peak_in_use;
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		boost::pool<> m_pool;
		boost::pool<> m_vec_pool;
#endif
	};
}

#endif // AIOCB_POOL

