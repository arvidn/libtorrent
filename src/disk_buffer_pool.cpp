/*

Copyright (c) 2007-2018, Arvid Norberg
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
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/disk_observer.hpp"
#include "libtorrent/platform_util.hpp" // for total_physical_ram
#include "libtorrent/disk_interface.hpp" // for default_block_size

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

	disk_buffer_pool::disk_buffer_pool(io_service& ios
		, std::function<void()> const& trigger_trim)
		: m_in_use(0)
		, m_max_use(64)
		, m_low_watermark(std::max(m_max_use - 32, 0))
		, m_trigger_cache_trim(trigger_trim)
		, m_exceeded_max_size(false)
		, m_ios(ios)
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
#else
		TORRENT_UNUSED(buffer);
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
			i = { allocate_buffer_impl(l, "pending read"), std::size_t(default_block_size)};
			if (i.data() == nullptr)
			{
				// uh oh. We failed to allocate the buffer!
				// we need to roll back and free all the buffers
				// we've already allocated
				for (auto j : iov)
				{
					if (j.data() == nullptr) break;
					char* buf = j.data();
					TORRENT_ASSERT(is_disk_buffer(buf, l));
					remove_buffer_in_use(buf);
					free_buffer_impl(buf, l);
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
			char* buf = i.data();
			TORRENT_ASSERT(is_disk_buffer(buf, l));
			remove_buffer_in_use(buf);
			free_buffer_impl(buf, l);
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

		char* ret = static_cast<char*>(std::malloc(default_block_size));

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
			remove_buffer_in_use(buf);
			free_buffer_impl(buf, l);
		}

		check_buffer_level(l);
	}

	void disk_buffer_pool::free_buffer(char* buf)
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);
		TORRENT_ASSERT(is_disk_buffer(buf, l));
		remove_buffer_in_use(buf);
		free_buffer_impl(buf, l);
		check_buffer_level(l);
	}

	void disk_buffer_pool::set_settings(aux::session_settings const& sett)
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);

		int const cache_size = sett.get_int(settings_pack::cache_size);
		if (cache_size < 0)
		{
			std::int64_t phys_ram = total_physical_ram();
			if (phys_ram == 0) m_max_use = default_int_value(settings_pack::cache_size);
			else
			{
				// this is the logic to calculate the automatic disk cache size
				// based on the amount of physical RAM.
				// The more physical RAM, the smaller portion of it is allocated
				// for the cache.

				// we take a 40th of everything exceeding 4 GiB
				// a 30th of everything exceeding 1 GiB
				// and a 10th of everything below a GiB

				constexpr std::int64_t gb = 1024 * 1024 * 1024;

				std::int64_t result = 0;
				if (phys_ram > 4 * gb)
				{
					result += (phys_ram - 4 * gb) / 40;
					phys_ram = 4 * gb;
				}
				if (phys_ram > 1 * gb)
				{
					result += (phys_ram - 1 * gb) / 30;
					phys_ram = 1 * gb;
				}
				result += phys_ram / 20;
				m_max_use = int(result / default_block_size);
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
					/ default_block_size, m_max_use);
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

		std::free(buf);

		--m_in_use;
	}

}
