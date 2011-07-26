/*

Copyright (c) 2011, Arvid Norberg
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

#include "libtorrent/aiocb_pool.hpp"
#include "libtorrent/disk_io_job.hpp"

namespace libtorrent
{
	aiocb_pool::aiocb_pool(): m_in_use(0), m_peak_in_use(0)
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		, m_pool(sizeof(file::aiocb_t), 128)
		, m_vec_pool(sizeof(file::iovec_t) * max_iovec)
		, m_handler_pool(sizeof(async_handler))
#endif
		, m_job_pool(sizeof(disk_io_job))
	{
#ifdef TORRENT_DISK_STATS
		file_access_log = 0;
#endif
	}

	disk_io_job* aiocb_pool::allocate_job(int type)
	{
		disk_io_job* ptr = (disk_io_job*)m_job_pool.malloc();

		if (ptr == 0) return 0;
		new (ptr) disk_io_job;
		ptr->action = (disk_io_job::action_t)type;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		ptr->in_use = true;
#endif
		return ptr;
	}

	void aiocb_pool::free_job(disk_io_job* j)
	{
		TORRENT_ASSERT(j);
		if (j == 0) return;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		TORRENT_ASSERT(j->in_use);
		j->in_use = false;
#endif
		j->~disk_io_job();
		m_job_pool.free(j);	
	}

	async_handler* aiocb_pool::alloc_handler()
	{
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		async_handler* ret = new async_handler(time_now_hires());
#else
		async_handler* ret = (async_handler*)m_handler_pool.malloc();
		new (ret) async_handler(time_now_hires());
		m_pool.set_next_size(50);
#endif
		return ret;
	}

	void aiocb_pool::free_handler(async_handler* h)
	{
		if (h == 0) return;
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		delete h;
#else
		h->~async_handler();
		m_handler_pool.free(h);
#endif
	}

	file::iovec_t* aiocb_pool::alloc_vec()
	{
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		file::iovec_t* ret = new file::iovec_t[max_iovec];
#else
		file::iovec_t* ret = (file::iovec_t*)m_vec_pool.malloc();
		m_pool.set_next_size(50);
#endif
		return ret;
	}

	void aiocb_pool::free_vec(file::iovec_t* vec)
	{
		if (vec == 0) return;
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		delete[] vec;
#else
		m_vec_pool.free(vec);
#endif
	}

	file::aiocb_t* aiocb_pool::construct()
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

	void aiocb_pool::destroy(file::aiocb_t* a)
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
}

