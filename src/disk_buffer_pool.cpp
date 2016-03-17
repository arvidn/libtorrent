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
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/disk_observer.hpp"
#include "libtorrent/platform_util.hpp" // for total_physical_ram

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <algorithm>
#include <boost/bind.hpp>
#include <boost/system/error_code.hpp>
#include <boost/shared_ptr.hpp>

#if TORRENT_HAVE_MMAP
#include <sys/mman.h>
#endif

#ifdef TORRENT_BSD
#include <sys/sysctl.h>
#endif

#ifdef TORRENT_LINUX
#include <linux/unistd.h>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent
{
	namespace {

	// this is posted to the network thread
	void watermark_callback(std::vector<boost::weak_ptr<disk_observer> >* cbs)
	{
		if (cbs != NULL)
		{
			for (std::vector<boost::weak_ptr<disk_observer> >::iterator i = cbs->begin()
				, end(cbs->end()); i != end; ++i)
			{
				boost::shared_ptr<disk_observer> o = i->lock();
				if (o) o->on_disk();
			}
			delete cbs;
		}
	}

	} // anonymous namespace

	disk_buffer_pool::disk_buffer_pool(int block_size, io_service& ios
		, boost::function<void()> const& trigger_trim)
		: m_block_size(block_size)
		, m_in_use(0)
		, m_max_use(64)
		, m_low_watermark((std::max)(m_max_use - 32, 0))
		, m_trigger_cache_trim(trigger_trim)
		, m_exceeded_max_size(false)
		, m_ios(ios)
		, m_cache_buffer_chunk_size(0)
#if TORRENT_HAVE_MMAP
		, m_cache_fd(-1)
		, m_cache_pool(0)
#endif
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

#if TORRENT_HAVE_MMAP
		if (m_cache_pool)
		{
			munmap(m_cache_pool, boost::uint64_t(m_max_use) * 0x4000);
			m_cache_pool = 0;
			// attempt to make MacOS not flush this to disk, making close()
			// block for a long time
			ftruncate(m_cache_fd, 0);
			close(m_cache_fd);
			m_cache_fd = -1;
		}
#endif
	}

	boost::uint32_t disk_buffer_pool::num_to_evict(int num_needed)
	{
		int ret = 0;

		mutex::scoped_lock l(m_pool_mutex);

		if (m_exceeded_max_size)
			ret = m_in_use - (std::min)(m_low_watermark, int(m_max_use - m_observers.size()*2));

		if (m_in_use + num_needed > m_max_use)
			ret = (std::max)(ret, int(m_in_use + num_needed - m_max_use));

		if (ret < 0) ret = 0;
		else if (ret > m_in_use) ret = m_in_use;

		return ret;
	}

	// checks to see if we're no longer exceeding the high watermark,
	// and if we're in fact below the low watermark. If so, we need to
	// post the notification messages to the peers that are waiting for
	// more buffers to received data into
	void disk_buffer_pool::check_buffer_level(mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(l.locked());
		if (!m_exceeded_max_size || m_in_use > m_low_watermark) return;

		m_exceeded_max_size = false;

		std::vector<boost::weak_ptr<disk_observer> >* cbs
			= new std::vector<boost::weak_ptr<disk_observer> >();
		m_observers.swap(*cbs);
		l.unlock();
		m_ios.post(boost::bind(&watermark_callback, cbs));
	}

#if TORRENT_USE_ASSERTS
	bool disk_buffer_pool::is_disk_buffer(char* buffer
		, mutex::scoped_lock& l) const
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(l.locked());
		TORRENT_UNUSED(l);

#if TORRENT_HAVE_MMAP
		if (m_cache_pool)
		{
			return buffer >= m_cache_pool && buffer < m_cache_pool
				+ boost::uint64_t(m_max_use) * 0x4000;
		}
#endif

#if defined TORRENT_DEBUG
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
		mutex::scoped_lock l(m_pool_mutex);
		return is_disk_buffer(buffer, l);
	}
#endif

	char* disk_buffer_pool::allocate_buffer(char const* category)
	{
		mutex::scoped_lock l(m_pool_mutex);
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
		, boost::shared_ptr<disk_observer> o, char const* category)
	{
		mutex::scoped_lock l(m_pool_mutex);
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
	int disk_buffer_pool::allocate_iovec(file::iovec_t* iov, int iov_len)
	{
		mutex::scoped_lock l(m_pool_mutex);
		for (int i = 0; i < iov_len; ++i)
		{
			iov[i].iov_base = allocate_buffer_impl(l, "pending read");
			iov[i].iov_len = block_size();
			if (iov[i].iov_base == NULL)
			{
				// uh oh. We failed to allocate the buffer!
				// we need to roll back and free all the buffers
				// we've already allocated
				for (int j = 0; j < i; ++j)
					free_buffer_impl(static_cast<char*>(iov[j].iov_base), l);
				return -1;
			}
		}
		return 0;
	}

	void disk_buffer_pool::free_iovec(file::iovec_t* iov, int iov_len)
	{
		// TODO: perhaps we should sort the buffers here?
		mutex::scoped_lock l(m_pool_mutex);
		for (int i = 0; i < iov_len; ++i)
			free_buffer_impl(static_cast<char*>(iov[i].iov_base), l);
		check_buffer_level(l);
	}

	char* disk_buffer_pool::allocate_buffer_impl(mutex::scoped_lock& l
		, char const*)
	{
		TORRENT_ASSERT(m_settings_set);
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(l.locked());
		TORRENT_UNUSED(l);

		char* ret;
#if TORRENT_HAVE_MMAP
		if (m_cache_pool)
		{
			if (m_free_list.size() <= (m_max_use - m_low_watermark)
				/ 2 && !m_exceeded_max_size)
			{
				m_exceeded_max_size = true;
				m_trigger_cache_trim();
			}
			if (m_free_list.empty()) return 0;
			boost::uint64_t slot_index = m_free_list.back();
			m_free_list.pop_back();
			ret = m_cache_pool + (slot_index * 0x4000);
			TORRENT_ASSERT(is_disk_buffer(ret, l));
		}
		else
#endif
		{
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
					: (std::max)(m_max_use / 10, 1);
				m_pool.set_next_size(effective_block_size);
				ret = static_cast<char*>(m_pool.malloc());
			}
			else
			{
				ret = page_aligned_allocator::malloc(m_block_size);
			}
#endif
			if (ret == NULL)
			{
				m_exceeded_max_size = true;
				m_trigger_cache_trim();
				return 0;
			}
		}

#if defined TORRENT_DEBUG
		TORRENT_ASSERT(m_buffers_in_use.count(ret) == 0);
		m_buffers_in_use.insert(ret);
#endif

		++m_in_use;
		if (m_in_use >= m_low_watermark + (m_max_use - m_low_watermark)
			/ 2 && !m_exceeded_max_size)
		{
			m_exceeded_max_size = true;
			m_trigger_cache_trim();
		}

		TORRENT_ASSERT(is_disk_buffer(ret, l));
		return ret;
	}

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

		check_buffer_level(l);
	}

	void disk_buffer_pool::free_buffer(char* buf)
	{
		mutex::scoped_lock l(m_pool_mutex);
		free_buffer_impl(buf, l);
		check_buffer_level(l);
	}

	void disk_buffer_pool::set_settings(aux::session_settings const& sett
		, error_code& ec)
	{
		TORRENT_UNUSED(ec);

		mutex::scoped_lock l(m_pool_mutex);

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

#if TORRENT_HAVE_MMAP
		// if we've already allocated an mmap, we can't change
		// anything unless there are no allocations in use
		if (m_cache_pool && m_in_use > 0) return;
#endif

		// only allow changing size if we're not using mmapped
		// cache, or if we're just about to turn it off
		if (
#if TORRENT_HAVE_MMAP
			m_cache_pool == 0 ||
#endif
			sett.get_str(settings_pack::mmap_cache).empty())
		{
			int const cache_size = sett.get_int(settings_pack::cache_size);
			if (cache_size < 0)
			{
				boost::uint64_t phys_ram = total_physical_ram();
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

					boost::int64_t const gb = 1024 * 1024 * 1024;

					boost::int64_t result = 0;
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
					m_max_use = result / m_block_size;
				}

				if (sizeof(void*) == 4)
				{
					// 32 bit builds should  capped below 2 GB of memory, even
					// when more actual ram is available, because we're still
					// constrained by the 32 bit virtual address space.
					m_max_use = (std::min)(2 * 1024 * 1024 * 3 / 4 * 1024
						/ m_block_size, m_max_use);
				}
			}
			else
			{
				m_max_use = cache_size;
			}
			m_low_watermark = m_max_use - (std::max)(16, sett.get_int(settings_pack::max_queued_disk_bytes) / 0x4000);
			if (m_low_watermark < 0) m_low_watermark = 0;
			if (m_in_use >= m_max_use && !m_exceeded_max_size)
			{
				m_exceeded_max_size = true;
				m_trigger_cache_trim();
			}
			if (m_cache_buffer_chunk_size > m_max_use)
				m_cache_buffer_chunk_size = m_max_use;
		}

#if TORRENT_USE_ASSERTS
		m_settings_set = true;
#endif

#if TORRENT_HAVE_MMAP
		// #error support resizing the map
		if (m_cache_pool && sett.get_str(settings_pack::mmap_cache).empty())
		{
			TORRENT_ASSERT(m_in_use == 0);
			munmap(m_cache_pool, boost::uint64_t(m_max_use) * 0x4000);
			m_cache_pool = 0;
			// attempt to make MacOS not flush this to disk, making close()
			// block for a long time
			ftruncate(m_cache_fd, 0);
			close(m_cache_fd);
			m_cache_fd = -1;
			std::vector<int>().swap(m_free_list);
		}
		else if (m_cache_pool == 0 && !sett.get_str(settings_pack::mmap_cache).empty())
		{
			// O_TRUNC here is because we don't actually care about what's
			// in the file now, there's no need to ever read that into RAM
#ifndef O_EXLOCK
#define O_EXLOCK 0
#endif
			m_cache_fd = open(sett.get_str(settings_pack::mmap_cache).c_str(), O_RDWR | O_CREAT | O_EXLOCK | O_TRUNC, 0700);
			if (m_cache_fd < 0)
			{
				ec.assign(errno, boost::system::system_category());
			}
			else
			{
#ifndef MAP_NOCACHE
#define MAP_NOCACHE 0
#endif
				ftruncate(m_cache_fd, boost::uint64_t(m_max_use) * 0x4000);
				m_cache_pool = static_cast<char*>(mmap(0, boost::uint64_t(m_max_use) * 0x4000, PROT_READ | PROT_WRITE
					, MAP_SHARED | MAP_NOCACHE, m_cache_fd, 0));
				if (intptr_t(m_cache_pool) == -1)
				{
					ec.assign(errno, boost::system::system_category());

					m_cache_pool = 0;
					// attempt to make MacOS not flush this to disk, making close()
					// block for a long time
					ftruncate(m_cache_fd, 0);
					close(m_cache_fd);
					m_cache_fd = -1;
				}
				else
				{
					TORRENT_ASSERT((size_t(m_cache_pool) & 0xfff) == 0);
					m_free_list.reserve(m_max_use);
					for (int i = 0; i < m_max_use; ++i)
						m_free_list.push_back(i);
				}
			}
		}
#endif
	}

	void disk_buffer_pool::free_buffer_impl(char* buf, mutex::scoped_lock& l)
	{
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(m_settings_set);
		TORRENT_ASSERT(is_disk_buffer(buf, l));
		TORRENT_ASSERT(l.locked());
		TORRENT_UNUSED(l);

#if TORRENT_HAVE_MMAP
		if (m_cache_pool)
		{
			TORRENT_ASSERT(buf >= m_cache_pool);
			TORRENT_ASSERT(buf <  m_cache_pool + boost::uint64_t(m_max_use) * 0x4000);
			int slot_index = (buf - m_cache_pool) / 0x4000;
			m_free_list.push_back(slot_index);
#if defined MADV_FREE
			// tell the virtual memory system that we don't actually care
			// about the data in these pages anymore. If this block was
			// swapped out to the SSD, it (hopefully) means it won't have
			// to be read back in once we start writing our new data to it
			madvise(buf, 0x4000, MADV_FREE);
#elif defined MADV_DONTNEED && defined TORRENT_LINUX
			// rumor has it that MADV_DONTNEED is in fact destructive
			// on linux (i.e. it won't flush it to disk or re-read from disk)
			// http://kerneltrap.org/mailarchive/linux-kernel/2007/5/1/84410
			madvise(buf, 0x4000, MADV_DONTNEED);
#endif
		}
		else
#endif
		{
#if defined TORRENT_DISABLE_POOL_ALLOCATOR

		page_aligned_allocator::free(buf);

#else
		if (m_using_pool_allocator)
			m_pool.free(buf);
		else
			page_aligned_allocator::free(buf);
#endif // TORRENT_DISABLE_POOL_ALLOCATOR
		}

#if defined TORRENT_DEBUG
		std::set<char*>::iterator i = m_buffers_in_use.find(buf);
		TORRENT_ASSERT(i != m_buffers_in_use.end());
		m_buffers_in_use.erase(i);
#endif

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
		mutex::scoped_lock l(m_pool_mutex);
		if (m_using_pool_allocator)
			m_pool.release_memory();
#endif
	}

}

