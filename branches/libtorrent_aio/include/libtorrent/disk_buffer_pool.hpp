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

#ifndef TORRENT_DISK_BUFFER_POOL_HPP
#define TORRENT_DISK_BUFFER_POOL_HPP

#include <boost/utility.hpp>

#include "libtorrent/config.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/io_service_fwd.hpp"
#include <vector>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
#include <set>
#endif

#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
#include "libtorrent/allocator.hpp" // for page_aligned_allocator
#include <boost/pool/pool.hpp>
#endif

#ifdef TORRENT_BUFFER_STATS
#include <boost/unordered_map.hpp>
#endif

namespace libtorrent
{
	namespace aux { struct session_settings; }
	class alert;
	struct alert_dispatcher;
	struct disk_observer;

	struct TORRENT_EXTRA_EXPORT disk_buffer_pool : boost::noncopyable
	{
		disk_buffer_pool(int block_size, io_service& ios
			, boost::function<void()> const& trigger_trim
			, alert_dispatcher* alert_disp);
		~disk_buffer_pool();

#if TORRENT_USE_ASSERTS || TORRENT_BUFFER_STATS
		bool is_disk_buffer(char* buffer
			, mutex::scoped_lock& l) const;
		bool is_disk_buffer(char* buffer) const;
#endif

		// tries to allocate a disk buffer. If the cache is full, this function will
		// return NULL and call the disk_observer once a buffer becomes available
		char* async_allocate_buffer(char const* category, boost::function<void(char*)> const& handler);

		char* allocate_buffer(char const* category);
		char* allocate_buffer(bool& exceeded, boost::shared_ptr<disk_observer> o, char const* category);
		void free_buffer(char* buf);
		void free_multiple_buffers(char** bufvec, int numbufs);

		int block_size() const { return m_block_size; }

#ifdef TORRENT_STATS
		int disk_allocations() const
		{ return m_allocations; }
#endif

		void release_memory();

		boost::uint32_t in_use() const
		{
			mutex::scoped_lock l(m_pool_mutex);
			return m_in_use;
		}
		boost::uint32_t num_to_evict(int num_needed = 0);
		bool exceeded_max_size() const { return m_exceeded_max_size; }

		void set_settings(aux::session_settings const& sett);

		struct handler_t
		{
			char* buffer; // argument to the callback
			char const* category; // category of allocation
			boost::function<void(char*)> callback;
		};

	protected:

		void free_buffer_impl(char* buf, mutex::scoped_lock& l);
		char* allocate_buffer_impl(mutex::scoped_lock& l, char const* category);

		// number of bytes per block. The BitTorrent
		// protocol defines the block size to 16 KiB.
		const int m_block_size;

		// number of disk buffers currently allocated
		int m_in_use;

		// cache size limit
		int m_max_use;

		// if we have exceeded the limit, we won't start
		// allowing allocations again until we drop below
		// this low watermark
		int m_low_watermark;

		// if we exceed the max number of buffers, we start
		// adding up callbacks to this queue. Once the number
		// of buffers in use drops below the low watermark,
		// we start calling these functions back
		// TODO: try to remove the observers, only using the async_allocate handlers
		std::vector<boost::shared_ptr<disk_observer> > m_observers;

		// these handlers are executed when a new buffer is available
		std::vector<handler_t> m_handlers;

		// callback used to tell the cache it needs to free up some blocks
		boost::function<void()> m_trigger_cache_trim;

		// set to true to throttle more allocations
		bool m_exceeded_max_size;

		// this is the main thread io_service. Callbacks are
		// posted on this in order to have them execute in
		// the main thread.
		io_service& m_ios;

	private:

		void check_buffer_level(mutex::scoped_lock& l);

		mutable mutex m_pool_mutex;

		int m_cache_buffer_chunk_size;
		bool m_lock_disk_cache;

#if TORRENT_HAVE_MMAP
		// the file descriptor of the cache mmap file
		int m_cache_fd;
		// the pointer to the block of virtual address space
		// making up the mmapped cache space
		char* m_cache_pool;
		// list of block indices that are not in use. block_index
		// times 0x4000 + m_cache_pool is the address where the
		// corresponding memory lives
		std::vector<int> m_free_list;
#endif

		alert_dispatcher* m_post_alert;

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

		// this is the actual user setting
		bool m_want_pool_allocator;

		// memory pool for read and write operations
		// and disk cache
		boost::pool<page_aligned_allocator> m_pool;
#endif

#if defined TORRENT_BUFFER_STATS || defined TORRENT_STATS
		int m_allocations;
#endif

#ifdef TORRENT_BUFFER_STATS
	public:
		void rename_buffer(char* buf, char const* category);
		boost::unordered_map<std::string, int> m_categories;
	protected:
		boost::unordered_map<char*, std::string> m_buf_to_category;
		FILE* m_log;
	private:
#endif

		// this is specifically exempt from release_asserts
		// since it's a quite costly check. Only for debug
		// builds.
#if defined TORRENT_DEBUG
		std::set<char*> m_buffers_in_use;
#endif
#if TORRENT_USE_ASSERTS
		int m_magic;
		bool m_settings_set;
#endif
	};

}

#endif // TORRENT_DISK_BUFFER_POOL_HPP

