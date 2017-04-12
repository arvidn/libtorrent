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
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/disk_observer.hpp"
#include "libtorrent/platform_util.hpp" // for total_physical_ram

#include "libtorrent/aux_/disable_warnings_push.hpp"

#ifdef TORRENT_BSD
#include <sys/sysctl.h>
#endif

#ifdef TORRENT_LINUX
#include <linux/unistd.h>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

	namespace {

	// this is posted to the network thread
	void watermark_callback(std::vector<std::weak_ptr<disk_observer>> const& cbs)
	{
		for (auto const& i : cbs)
		{
			std::shared_ptr<disk_observer> o = i.lock();
			if (o) o->on_disk();
		}
	}

	} // anonymous namespace

	disk_buffer_pool::disk_buffer_pool(int block_size, io_service& ios
		, std::function<void()> const& trigger_trim)
		: m_block_size(block_size)
		, m_in_use(0)
		, m_max_use(64)
		, m_low_watermark((std::max)(m_max_use - 32, 0))
		, m_trigger_cache_trim(trigger_trim)
		, m_exceeded_max_size(false)
		, m_ios(ios)
		, m_cache_buffer_chunk_size(0)
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		, m_using_pool_allocator(false)
		, m_want_pool_allocator(false)
		, m_pool(block_size, 32)
#endif
	{
#if TORRENT_USE_ASSERTS
		m_magic = 0x1337;
		m_settings_set = false;
#endif
	}

	disk_buffer_pool::~disk_buffer_pool()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#if TORRENT_USE_ASSERTS
		m_magic = 0;
#endif

	}

	int disk_buffer_pool::num_to_evict(int const num_needed)
	{
		int ret = 0;

		std::unique_lock<std::mutex> l(m_pool_mutex);

		if (m_exceeded_max_size)
			ret = m_in_use - std::min(m_low_watermark, m_max_use - int(m_observers.size()) * 2);

		if (m_in_use + num_needed > m_max_use)
			ret = std::max(ret, m_in_use + num_needed - m_max_use);

		if (ret < 0) ret = 0;
		else if (ret > m_in_use) ret = m_in_use;

		return ret;
	}

	// checks to see if we're no longer exceeding the high watermark,
	// and if we're in fact below the low watermark. If so, we need to
	// post the notification messages to the peers that are waiting for
	// more buffers to received data into
	void disk_buffer_pool::check_buffer_level(std::unique_lock<std::mutex>& l)
	{
		TORRENT_ASSERT(l.owns_lock());
		if (!m_exceeded_max_size || m_in_use > m_low_watermark) return;

		m_exceeded_max_size = false;

		std::vector<std::weak_ptr<disk_observer>> cbs;
		m_observers.swap(cbs);
		l.unlock();
		m_ios.post(std::bind(&watermark_callback, std::move(cbs)));
	}

#if TORRENT_USE_ASSERTS
	bool disk_buffer_pool::is_disk_buffer(char* buffer
		, std::unique_lock<std::mutex>& l) const
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(l.owns_lock());
		TORRENT_UNUSED(l);

#if TORRENT_USE_INVARIANT_CHECKS
		return m_buffers_in_use.count(buffer) == 1;
#elif defined TORRENT_DEBUG_BUFFERS
		return page_aligned_allocator::in_use(buffer);
#elif defined TORRENT_DISABLE_POOL_ALLOCATOR
		return true;
#else
		if (m_using_pool_allocator)
			return m_pool.is_from(buffer);
		else
			return true;
#endif
	}

	bool disk_buffer_pool::is_disk_buffer(char* buffer) const
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);
		return is_disk_buffer(buffer, l);
	}
#endif

	char* disk_buffer_pool::allocate_buffer(char const* category)
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);
		return allocate_buffer_impl(l, category);
	}

	// we allow allocating more blocks even after we exceed the max size,
	// but communicate back to the allocator (typically the peer_connection)
	// that we have exceeded the limit via the out-parameter "exceeded". The
	// caller is expected to honor this by not allocating any more buffers
	// until the disk_observer object (passed in as "o") is invoked, indicating
	// that there's more room in the pool now. This caps the amount of over-
	// allocation to one block per peer connection.
	char* disk_buffer_pool::allocate_buffer(bool& exceeded
		, std::shared_ptr<disk_observer> o, char const* category)
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);
		char* ret = allocate_buffer_impl(l, category);
		if (m_exceeded_max_size)
		{
			exceeded = true;
			if (o) m_observers.push_back(o);
		}
		return ret;
	}

// this function allocates buffers and
// fills in the iovec array with the buffers
	int disk_buffer_pool::allocate_iovec(span<iovec_t> iov)
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);
		for (auto& i : iov)
		{
			i.iov_base = allocate_buffer_impl(l, "pending read");
			i.iov_len = std::size_t(block_size());
			if (i.iov_base == nullptr)
			{
				// uh oh. We failed to allocate the buffer!
				// we need to roll back and free all the buffers
				// we've already allocated
				for (auto j : iov)
				{
					if (j.iov_base == nullptr) break;
					char* buf = static_cast<char*>(j.iov_base);
					TORRENT_ASSERT(is_disk_buffer(buf, l));
					free_buffer_impl(buf, l);
					remove_buffer_in_use(buf);
				}
				return -1;
			}
		}
		return 0;
	}

	void disk_buffer_pool::free_iovec(span<iovec_t const> iov)
	{
		// TODO: perhaps we should sort the buffers here?
		std::unique_lock<std::mutex> l(m_pool_mutex);
		for (auto i : iov)
		{
			char* buf = static_cast<char*>(i.iov_base);
			TORRENT_ASSERT(is_disk_buffer(buf, l));
			free_buffer_impl(buf, l);
			remove_buffer_in_use(buf);
		}
		check_buffer_level(l);
	}

	char* disk_buffer_pool::allocate_buffer_impl(std::unique_lock<std::mutex>& l
		, char const*)
	{
		TORRENT_ASSERT(m_settings_set);
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(l.owns_lock());
		TORRENT_UNUSED(l);

		char* ret;
#if defined TORRENT_DISABLE_POOL_ALLOCATOR

		ret = page_aligned_allocator::malloc(m_block_size);

#else
		if (m_using_pool_allocator)
		{
			int const effective_block_size
				= m_in_use >= m_max_use
				? 20 // use small increments once we've exceeded the cache size
				: m_cache_buffer_chunk_size
				? m_cache_buffer_chunk_size
				: std::max(m_max_use / 10, 1);
			m_pool.set_next_size(effective_block_size);
			ret = static_cast<char*>(m_pool.malloc());
		}
		else
		{
			ret = page_aligned_allocator::malloc(m_block_size);
		}
#endif
		if (ret == nullptr)
		{
			m_exceeded_max_size = true;
			m_trigger_cache_trim();
			return nullptr;
		}

		++m_in_use;

#if TORRENT_USE_INVARIANT_CHECKS
		try
		{
			TORRENT_ASSERT(m_buffers_in_use.count(ret) == 0);
			m_buffers_in_use.insert(ret);
		}
		catch (...)
		{
			free_buffer_impl(ret, l);
			return nullptr;
		}
#endif

		if (m_in_use >= m_low_watermark + (m_max_use - m_low_watermark)
			/ 2 && !m_exceeded_max_size)
		{
			m_exceeded_max_size = true;
			m_trigger_cache_trim();
		}

		TORRENT_ASSERT(is_disk_buffer(ret, l));
		return ret;
	}

	void disk_buffer_pool::free_multiple_buffers(span<char*> bufvec)
	{
		// sort the pointers in order to maximize cache hits
		std::sort(bufvec.begin(), bufvec.end());

		std::unique_lock<std::mutex> l(m_pool_mutex);
		for (char* buf : bufvec)
		{
			TORRENT_ASSERT(is_disk_buffer(buf, l));
			free_buffer_impl(buf, l);
			remove_buffer_in_use(buf);
		}

		check_buffer_level(l);
	}

	void disk_buffer_pool::free_buffer(char* buf)
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);
		TORRENT_ASSERT(is_disk_buffer(buf, l));
		free_buffer_impl(buf, l);
		remove_buffer_in_use(buf);
		check_buffer_level(l);
	}

	void disk_buffer_pool::set_settings(aux::session_settings const& sett)
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);

		// 0 cache_buffer_chunk_size means 'automatic' (i.e.
		// proportional to the total disk cache size)
		m_cache_buffer_chunk_size = sett.get_int(settings_pack::cache_buffer_chunk_size);
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		// if the chunk size is set to 1, there's no point in creating a pool
		m_want_pool_allocator = sett.get_bool(settings_pack::use_disk_cache_pool)
			&& (m_cache_buffer_chunk_size != 1);
		// if there are no allocated blocks, it's OK to switch allocator
		if (m_in_use == 0)
			m_using_pool_allocator = m_want_pool_allocator;
#endif

		int const cache_size = sett.get_int(settings_pack::cache_size);
		if (cache_size < 0)
		{
			std::int64_t phys_ram = total_physical_ram();
			if (phys_ram == 0) m_max_use = 1024;
			else
			{
				// this is the logic to calculate the automatic disk cache size
				// based on the amount of physical RAM.
				// The more physical RAM, the smaller portion of it is allocated
				// for the cache.

				// we take a 30th of everything exceeding 4 GiB
				// a 20th of everything exceeding 1 GiB
				// and a 10th of everything below a GiB

				constexpr std::int64_t gb = 1024 * 1024 * 1024;

				std::int64_t result = 0;
				if (phys_ram > 4 * gb)
				{
					result += (phys_ram - 4 * gb) / 30;
					phys_ram = 4 * gb;
				}
				if (phys_ram > 1 * gb)
				{
					result += (phys_ram - 1 * gb) / 20;
					phys_ram = 1 * gb;
				}
				result += phys_ram / 10;
				m_max_use = int(result / m_block_size);
			}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127 ) /* warning C4127: conditional expression is constant */
#endif // _MSC_VER
			if (sizeof(void*) == 4)
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER
			{
				// 32 bit builds should  capped below 2 GB of memory, even
				// when more actual ram is available, because we're still
				// constrained by the 32 bit virtual address space.
				m_max_use = std::min(2 * 1024 * 1024 * 3 / 4 * 1024
					/ m_block_size, m_max_use);
			}
		}
		else
		{
			m_max_use = cache_size;
		}
		m_low_watermark = m_max_use - std::max(16, sett.get_int(settings_pack::max_queued_disk_bytes) / 0x4000);
		if (m_low_watermark < 0) m_low_watermark = 0;
		if (m_in_use >= m_max_use && !m_exceeded_max_size)
		{
			m_exceeded_max_size = true;
			m_trigger_cache_trim();
		}
		if (m_cache_buffer_chunk_size > m_max_use)
			m_cache_buffer_chunk_size = m_max_use;

#if TORRENT_USE_ASSERTS
		m_settings_set = true;
#endif
	}

	void disk_buffer_pool::remove_buffer_in_use(char* buf)
	{
		TORRENT_UNUSED(buf);
#if TORRENT_USE_INVARIANT_CHECKS
		std::set<char*>::iterator i = m_buffers_in_use.find(buf);
		TORRENT_ASSERT(i != m_buffers_in_use.end());
		m_buffers_in_use.erase(i);
#endif
	}

	void disk_buffer_pool::free_buffer_impl(char* buf, std::unique_lock<std::mutex>& l)
	{
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(m_settings_set);
		TORRENT_ASSERT(l.owns_lock());
		TORRENT_UNUSED(l);

#if defined TORRENT_DISABLE_POOL_ALLOCATOR

		page_aligned_allocator::free(buf);

#else
		if (m_using_pool_allocator)
			m_pool.free(buf);
		else
			page_aligned_allocator::free(buf);
#endif // TORRENT_DISABLE_POOL_ALLOCATOR

		--m_in_use;

#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		// should we switch which allocator to use?
		if (m_in_use == 0 && m_want_pool_allocator != m_using_pool_allocator)
		{
			m_pool.release_memory();
			m_using_pool_allocator = m_want_pool_allocator;
		}
#endif
	}

	void disk_buffer_pool::release_memory()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#ifndef TORRENT_DISABLE_POOL_ALLOCATOR
		std::unique_lock<std::mutex> l(m_pool_mutex);
		if (m_using_pool_allocator)
			m_pool.release_memory();
#endif
	}

}
