/*

Copyright (c) 2007-2014, Arvid Norberg
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

#ifndef TORRENT_DISK_BUFFER_POOL
#define TORRENT_DISK_BUFFER_POOL

#include <boost/utility.hpp>

#include "libtorrent/config.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/allocator.hpp"

#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
#include <boost/pool/pool.hpp>
#endif

#ifdef TORRENT_DISK_STATS
#include <fstream>
#endif

#if TORRENT_USE_ASSERTS || TORRENT_DISK_STATS
#include <boost/unordered_map.hpp>
#endif

namespace libtorrent
{
	struct TORRENT_EXTRA_EXPORT disk_buffer_pool : boost::noncopyable
	{
		disk_buffer_pool(int block_size);
#if TORRENT_USE_ASSERTS
		~disk_buffer_pool();
#endif

#if TORRENT_USE_ASSERTS || TORRENT_DISK_STATS
		bool is_disk_buffer(char* buffer
			, mutex::scoped_lock& l) const;
		bool is_disk_buffer(char* buffer) const;
#endif

		char* allocate_buffer(char const* category);
		void free_buffer(char* buf);
		void free_multiple_buffers(char** bufvec, int numbufs);

		int block_size() const { return m_block_size; }

#ifdef TORRENT_STATS
		int disk_allocations() const
		{ return m_allocations; }
#endif

#ifdef TORRENT_DISK_STATS
		std::ofstream m_disk_access_log;
#endif

		void release_memory();

		int in_use() const { return m_in_use; }

	protected:

		void free_buffer_impl(char* buf, mutex::scoped_lock& l);

		// number of bytes per block. The BitTorrent
		// protocol defines the block size to 16 KiB.
		const int m_block_size;

		// number of disk buffers currently allocated
		int m_in_use;

		session_settings m_settings;

	private:

		mutable mutex m_pool_mutex;

#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		// if this is true, all buffers are allocated
		// from m_pool. If this is false, all buffers
		// are allocated using page_aligned_allocator.
		// if the settings change to prefer the other
		// allocator, this bool will not switch over
		// to match the settings until all buffers have
		// been freed. That way, we never have a mixture
		// of buffers allocated from different sources.
		// in essence, this make the setting only take
		// effect after a restart (which seems fine).
		// or once the client goes idle for a while.
		bool m_using_pool_allocator;

		// memory pool for read and write operations
		// and disk cache
		boost::pool<page_aligned_allocator> m_pool;
#endif

#if defined TORRENT_DISK_STATS || defined TORRENT_STATS
		int m_allocations;
#endif
#ifdef TORRENT_DISK_STATS
	public:
		void rename_buffer(char* buf, char const* category);
	protected:
		boost::unordered_map<std::string, int> m_categories;
		boost::unordered_map<char*, std::string> m_buf_to_category;
		std::ofstream m_log;
	private:
#endif
#if TORRENT_USE_ASSERTS
		int m_magic;
#endif
	};

}

#endif // TORRENT_DISK_BUFFER_POOL

