/*

Copyright (c) 2011, 2013-2022, Arvid Norberg
Copyright (c) 2016, 2018, 2020, Alden Torres
Copyright (c) 2016, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/disk_buffer_pool.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/settings_pack.hpp" // for settings_interface
#include "libtorrent/io_context.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/aux_/debug_disk_thread.hpp"
#include "libtorrent/aux_/scope_end.hpp"

#include <algorithm>
#include <array>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#ifdef TORRENT_BSD
#include <sys/sysctl.h>
#endif

#ifdef TORRENT_LINUX
#include <linux/unistd.h>
#endif

#ifdef TORRENT_ADDRESS_SANITIZER
#include <sanitizer/asan_interface.h>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {
namespace aux {

disk_buffer_pool::disk_buffer_pool(int const max_cached_buffers)
	: m_max_cached_buffers(max_cached_buffers)
{
	m_free_buffers.reserve(std::size_t(max_cached_buffers));
}

	disk_buffer_pool::~disk_buffer_pool()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#if TORRENT_USE_ASSERTS
		m_magic = 0;
#endif
		for (char* b : m_free_buffers)
			std::free(b);
	}

	char* disk_buffer_pool::allocate_buffer(char const* category)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_UNUSED(category);

		char* ret = nullptr;
		{
			std::lock_guard<std::mutex> l(m_pool_mutex);
			++m_in_use;
			if (!m_free_buffers.empty())
			{
				ret = m_free_buffers.back();
				m_free_buffers.pop_back();
				m_low_water = std::min(m_low_water, int(m_free_buffers.size()));
			}
		}

		if (ret == nullptr)
		{
			// never hold the mutex across malloc()
			ret = static_cast<char*>(std::malloc(default_block_size));
			if (ret == nullptr)
			{
				std::lock_guard<std::mutex> l(m_pool_mutex);
				--m_in_use;
				return nullptr;
			}
		}

#if TORRENT_DEBUG_BUFFER_POOL
		auto undo = scope_end([&] {
			{
				std::lock_guard<std::mutex> l(m_pool_mutex);
				--m_in_use;
			}
			std::free(ret);
		});
		{
			std::lock_guard<std::mutex> l(m_pool_mutex);
			auto const [it, added] = m_buffers_in_use.insert({ret, category});
			TORRENT_UNUSED(it);
			TORRENT_ASSERT(added);
			m_histogram[category] += 1;
			maybe_log();
		}
		undo.disarm();
#endif

		return ret;
	}

	void disk_buffer_pool::free_multiple_buffers(span<char*> bufvec)
	{
		// buffers that don't fit in the cache are compacted to the front of
		// bufvec, to be freed after the mutex is released
		int overflow = 0;
		{
			std::lock_guard<std::mutex> l(m_pool_mutex);
			for (char* buf : bufvec)
			{
				TORRENT_ASSERT(buf);
				remove_buffer_in_use(buf);
				--m_in_use;
				if (int(m_free_buffers.size()) < m_max_cached_buffers)
					m_free_buffers.push_back(buf);
				else
					bufvec[overflow++] = buf;
			}
		}

		for (char* buf : bufvec.first(overflow))
			std::free(buf);
	}

	void disk_buffer_pool::free_buffer(char* buf)
	{
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(m_magic == 0x1337);
		{
			std::lock_guard<std::mutex> l(m_pool_mutex);
			remove_buffer_in_use(buf);
			--m_in_use;
			if (int(m_free_buffers.size()) < m_max_cached_buffers)
			{
				m_free_buffers.push_back(buf);
				return;
			}
		}
		std::free(buf);
	}

	void disk_buffer_pool::expire_buffers()
	{
		int to_free = 0;
		{
			std::lock_guard<std::mutex> l(m_pool_mutex);
			int const size = int(m_free_buffers.size());

			// m_low_water buffers were not needed at all during the window,
			// release half of them, to decay gradually
			to_free = std::min(size, (m_low_water + 1) / 2);
			m_low_water = size - to_free;
		}

		// release in chunks, to not hold the mutex while calling free(). Which
		// buffers are released doesn't matter, so take them from the back to
		// avoid moving the rest
		std::array<char*, 64> chunk;
		while (to_free > 0)
		{
			int n = 0;
			{
				std::lock_guard<std::mutex> l(m_pool_mutex);
				n = std::min({int(m_free_buffers.size()), int(chunk.size()), to_free});
				std::copy_n(m_free_buffers.end() - n, n, chunk.begin());
				m_free_buffers.resize(m_free_buffers.size() - std::size_t(n));
				m_low_water = std::min(m_low_water, int(m_free_buffers.size()));
			}
			if (n == 0)
				break;
			for (char* buf : span<char*>(chunk).first(n))
				std::free(buf);
			to_free -= n;
		}
	}

	void disk_buffer_pool::remove_buffer_in_use(char* buf)
	{
		TORRENT_UNUSED(buf);
#if TORRENT_DEBUG_BUFFER_POOL
		auto i = m_buffers_in_use.find(buf);
		TORRENT_ASSERT(i != m_buffers_in_use.end());
		TORRENT_ASSERT(m_histogram[i->second] > 0);
		m_histogram[i->second] -= 1;
		m_buffers_in_use.erase(i);
		maybe_log();
#endif
	}

#if TORRENT_DEBUG_BUFFER_POOL
	void disk_buffer_pool::maybe_log()
	{
		time_t const now = std::time(nullptr);
		if (now - m_last_log > 1)
		{
			m_last_log = now;
			FILE* f = fopen("buffer_pool.log", "a");
			fprintf(f, "%ld ", now);
			for (auto it = m_histogram.begin(); it != m_histogram.end(); ++it)
			{
				fprintf(f, "%s:%d ", it->first.c_str(), it->second);
			}
			fputs("\n", f);
			fclose(f);
		}
	}

	void disk_buffer_pool::rename_buffer(char* buf, char const* category)
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);
		auto i = m_buffers_in_use.find(buf);
		TORRENT_ASSERT(i != m_buffers_in_use.end());
		TORRENT_ASSERT(m_histogram[i->second] > 0);
		m_histogram[i->second] -= 1;
		i->second = category;
		m_histogram[category] += 1;
		maybe_log();
	}
#endif
}
}
