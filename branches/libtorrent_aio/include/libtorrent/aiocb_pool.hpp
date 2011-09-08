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
#include "libtorrent/file.hpp" // for file::iovec_t
#include "libtorrent/thread.hpp"
#include "libtorrent/max.hpp" // for min<> metafunction

//#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
#include <boost/pool/object_pool.hpp>
//#endif

namespace libtorrent
{
	struct async_handler;
	struct disk_io_job;

	struct aiocb_pool
	{
		enum { max_iovec = min<64, TORRENT_IOV_MAX>::value };

		aiocb_pool();

		disk_io_job* allocate_job(int type);
		void free_job(disk_io_job* j);

		async_handler* alloc_handler();
		void free_handler(async_handler* h);

		file::iovec_t* alloc_vec();
		void free_vec(file::iovec_t* vec);

#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		bool is_from(file::aiocb_t* p) const { return true; }
#else
		bool is_from(file::aiocb_t* p) const { return m_pool.is_from(p); }
#endif

		file::aiocb_t* construct();
		void destroy(file::aiocb_t* a);

		int in_use() const { return m_in_use; }
		int peak_in_use() const { return m_peak_in_use; }

#if TORRENT_USE_IOSUBMIT
		io_context_t io_queue;
		int event;
#endif

#if TORRENT_USE_AIO_PORTS
		int port;
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
		boost::pool<> m_handler_pool;
#endif
		mutex m_job_mutex;
		boost::pool<> m_job_pool;

	};
}

#endif // AIOCB_POOL

