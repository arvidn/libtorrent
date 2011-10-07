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

#include "libtorrent/config.hpp"
#include "libtorrent/disk_buffer_pool.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/allocator.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/io_service.hpp"

#include <algorithm>

#if TORRENT_USE_MLOCK && !defined TORRENT_WINDOWS
#include <sys/mman.h>
#endif

#ifdef TORRENT_BUFFER_STATS
#include "libtorrent/time.hpp"
#endif

namespace libtorrent
{

	disk_buffer_pool::disk_buffer_pool(int block_size, io_service& ios)
		: m_block_size(block_size)
		, m_in_use(0)
		, m_max_use(64)
		, m_low_watermark((std::max)(m_max_use - 32, 0))
		, m_exceeded_max_size(false)
		, m_ios(ios)
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		, m_pool(block_size, 32)
#endif
		, m_cache_buffer_chunk_size(10)
		, m_lock_disk_cache(false)
	{
#if defined TORRENT_BUFFER_STATS || defined TORRENT_STATS
		m_allocations = 0;
#endif
#ifdef TORRENT_BUFFER_STATS
		m_log.open("disk_buffers.log", std::ios::trunc);
		m_categories["read cache"] = 0;
		m_categories["write cache"] = 0;

		m_disk_access_log.open("disk_access.log", std::ios::trunc);
#endif
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		m_magic = 0x1337;
		m_settings_set = false;
#endif
	}

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
	disk_buffer_pool::~disk_buffer_pool()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_magic = 0;
	}
#endif

	boost::uint32_t disk_buffer_pool::num_to_evict(int num_needed) const
	{
		int ret = 0;

		mutex::scoped_lock l(m_pool_mutex);

		if (m_exceeded_max_size)
			ret = m_in_use - (std::min)(m_low_watermark, int(m_max_use - m_callbacks.size()));

		if (m_in_use + num_needed > m_max_use)
			ret = (std::max)(ret, int(m_in_use + num_needed - m_max_use));

		if (ret < 0) ret = 0;
		else if (ret > m_in_use) ret = m_in_use;

		return ret;
	}

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS || defined TORRENT_BUFFER_STATS
	bool disk_buffer_pool::is_disk_buffer(char* buffer
		, mutex::scoped_lock& l) const
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		return m_buffers_in_use.count(buffer) == 1;
#ifdef TORRENT_BUFFER_STATS
		if (m_buf_to_category.find(buffer)
			== m_buf_to_category.end()) return false;
#endif
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		return true;
#else
		return m_pool.is_from(buffer);
#endif
	}

	bool disk_buffer_pool::is_disk_buffer(char* buffer) const
	{
		mutex::scoped_lock l(m_pool_mutex);
		return is_disk_buffer(buffer, l);
	}
#endif

	char* disk_buffer_pool::allocate_buffer(char const* category)
	{
		mutex::scoped_lock l(m_pool_mutex);
		return allocate_buffer_impl(l, category);
	}

	char* disk_buffer_pool::allocate_buffer(bool& exceeded, bool& trigger_trim
		, boost::function<void()> const& cb, char const* category)
	{
		mutex::scoped_lock l(m_pool_mutex);
		bool was_exceeded = m_exceeded_max_size;
		char* ret = allocate_buffer_impl(l, category);
		if (m_exceeded_max_size)
		{
			exceeded = true;
			m_callbacks.push_back(cb);
			if (!was_exceeded) trigger_trim = true;
		}
		return ret;
	}

	char* disk_buffer_pool::allocate_buffer_impl(mutex::scoped_lock& l, char const* category)
	{
		TORRENT_ASSERT(m_settings_set);
		TORRENT_ASSERT(m_magic == 0x1337);
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		char* ret = page_aligned_allocator::malloc(m_block_size);
#else
		char* ret = (char*)m_pool.malloc();
		m_pool.set_next_size(m_cache_buffer_chunk_size);
#endif
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		TORRENT_ASSERT(m_buffers_in_use.count(ret) == 0);
		m_buffers_in_use.insert(ret);
#endif
		++m_in_use;
		if (m_in_use >= m_max_use) m_exceeded_max_size = true;
#if TORRENT_USE_MLOCK
		if (m_lock_disk_cache)
		{
#ifdef TORRENT_WINDOWS
			VirtualLock(ret, m_block_size);
#else
			mlock(ret, m_block_size);
#endif		
		}
#endif

#if defined TORRENT_BUFFER_STATS || defined TORRENT_STATS
		++m_allocations;
#endif
#ifdef TORRENT_BUFFER_STATS
		++m_categories[category];
		m_buf_to_category[ret] = category;
		m_log << log_time() << " " << category << ": " << m_categories[category] << "\n";
#endif
		TORRENT_ASSERT(ret == 0 || is_disk_buffer(ret, l));
		return ret;
	}

#ifdef TORRENT_BUFFER_STATS
	void disk_buffer_pool::rename_buffer(char* buf, char const* category)
	{
		mutex::scoped_lock l(m_pool_mutex);
		TORRENT_ASSERT(is_disk_buffer(buf, l));
		TORRENT_ASSERT(m_categories.find(m_buf_to_category[buf])
			!= m_categories.end());
		std::string const& prev_category = m_buf_to_category[buf];
		--m_categories[prev_category];
		m_log << log_time() << " " << prev_category << ": " << m_categories[prev_category] << "\n";

		++m_categories[category];
		m_buf_to_category[buf] = category;
		m_log << log_time() << " " << category << ": " << m_categories[category] << "\n";
		TORRENT_ASSERT(m_categories.find(m_buf_to_category[buf])
			!= m_categories.end());
	}
#endif

	void disk_buffer_pool::free_multiple_buffers(char** bufvec, int numbufs)
	{
		char** end = bufvec + numbufs;
		// sort the pointers in order to maximize cache hits
		std::sort(bufvec, end);

		mutex::scoped_lock l(m_pool_mutex);
		for (; bufvec != end; ++bufvec)
		{
			char* buf = *bufvec;
			TORRENT_ASSERT(buf);
			free_buffer_impl(buf, l);
		}
		check_usage(l);
	}

	void disk_buffer_pool::free_buffer(char* buf)
	{
		mutex::scoped_lock l(m_pool_mutex);
		free_buffer_impl(buf, l);
		check_usage(l);
	}

	void disk_buffer_pool::check_usage(mutex::scoped_lock& l)
	{
		// we previously exceeded the limit, but now we
		// have dropped below the low watermark
		if (m_exceeded_max_size && m_in_use <= m_low_watermark)
		{
			m_exceeded_max_size = false;
			std::vector<boost::function<void()> > cbs;
			m_callbacks.swap(cbs);
			l.unlock();
			for (std::vector<boost::function<void()> >::iterator i = cbs.begin()
				, end(cbs.end()); i != end; ++i)
			{
				m_ios.post(*i);
			}
		}
	}

	void disk_buffer_pool::set_settings(session_settings const& sett)
	{
		mutex::scoped_lock l(m_pool_mutex);
		m_cache_buffer_chunk_size = sett.cache_buffer_chunk_size;
		m_lock_disk_cache = sett.lock_disk_cache;
		m_max_use = sett.cache_size;
		m_low_watermark = m_max_use - (std::max)(16, sett.max_queued_disk_bytes / 0x4000);
		if (m_low_watermark < 0) m_low_watermark = 0;
		if (m_in_use >= m_max_use) m_exceeded_max_size = true;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		m_settings_set = true;
#endif
	}

	void disk_buffer_pool::free_buffer_impl(char* buf, mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(m_settings_set);
		TORRENT_ASSERT(is_disk_buffer(buf, l));
#if defined TORRENT_BUFFER_STATS || defined TORRENT_STATS
		--m_allocations;
#endif
#ifdef TORRENT_BUFFER_STATS
		TORRENT_ASSERT(m_categories.find(m_buf_to_category[buf])
			!= m_categories.end());
		std::string const& category = m_buf_to_category[buf];
		--m_categories[category];
		m_log << log_time() << " " << category << ": " << m_categories[category] << "\n";
		m_buf_to_category.erase(buf);
#endif
#if TORRENT_USE_MLOCK
		if (m_lock_disk_cache)
		{
#ifdef TORRENT_WINDOWS
			VirtualUnlock(buf, m_block_size);
#else
			munlock(buf, m_block_size);
#endif		
		}
#endif
#ifdef TORRENT_DISABLE_POOL_ALLOCATOR
		page_aligned_allocator::free(buf);
#else
		m_pool.free(buf);
#endif
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		std::set<char*>::iterator i = m_buffers_in_use.find(buf);
		TORRENT_ASSERT(i != m_buffers_in_use.end());
		m_buffers_in_use.erase(i);
#endif
		--m_in_use;
	}

	void disk_buffer_pool::release_memory()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		mutex::scoped_lock l(m_pool_mutex);
		m_pool.release_memory();
#endif
	}

}

