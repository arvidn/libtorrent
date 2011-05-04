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

#include <boost/pool/object_pool.hpp>
#include "libtorrent/file.hpp"

namespace libtorrent
{
	struct aiocb_pool
	{
		aiocb_pool(): m_in_use(0), m_peak_in_use(0) {}

		bool is_from(file::aiocb_t* p) const { return m_pool.is_from(p); }

		file::aiocb_t* construct()
		{
			++m_in_use;
			if (m_in_use > m_peak_in_use) m_peak_in_use = m_in_use;
#ifdef TORRENT_DISABLE_POOL_ALLOCATORS
			file::aiocb_t* ret = new file::aiocb_t;
#else
			file::aiocb_t* ret = m_pool.construct();
			m_pool.set_next_size(64);
#endif
			return ret;
		}

		void destroy(file::aiocb_t* a)
		{
			--m_in_use;
#ifdef TORRENT_DISABLE_POOL_ALLOCATORS
			delete a;
#else
			TORRENT_ASSERT(m_pool.is_from(a));
			m_pool.destroy(a);
#endif
		}

		int in_use() const { return m_in_use; }
		int peak_in_use() const { return m_peak_in_use; }

#if TORRENT_USE_IOSUBMIT
		io_context_t io_queue;
		int event;
#endif

	private:

		int m_in_use;
		int m_peak_in_use;
#ifndef TORRENT_DISABLE_POOL_ALLOCATORS
		boost::object_pool<file::aiocb_t> m_pool;
#endif
	};
}

#endif // AIOCB_POOL

