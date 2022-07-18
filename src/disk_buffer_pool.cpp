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

	disk_buffer_pool::disk_buffer_pool() = default;

	disk_buffer_pool::~disk_buffer_pool()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#if TORRENT_USE_ASSERTS
		m_magic = 0;
#endif
	}

	char* disk_buffer_pool::allocate_buffer(char const* category)
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);
		return allocate_buffer_impl(l, category);
	}

	char* disk_buffer_pool::allocate_buffer_impl(std::unique_lock<std::mutex>& l
		, char const* category)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(l.owns_lock());
		TORRENT_UNUSED(l);
		TORRENT_UNUSED(category);

		char* ret = static_cast<char*>(std::malloc(default_block_size));

		if (ret == nullptr)
		{
			return nullptr;
		}

		++m_in_use;

#if TORRENT_DEBUG_BUFFER_POOL
		try
		{
			auto const [it, added] = m_buffers_in_use.insert({ret, category});
			TORRENT_UNUSED(it);
			TORRENT_ASSERT(added);
		}
		catch (...)
		{
			free_buffer_impl(ret, l);
			return nullptr;
		}
		m_histogram[category] += 1;
		maybe_log();
#endif

		return ret;
	}

	void disk_buffer_pool::free_multiple_buffers(span<char*> bufvec)
	{
		// sort the pointers in order to maximize cache hits
		std::sort(bufvec.begin(), bufvec.end());

		std::unique_lock<std::mutex> l(m_pool_mutex);
		for (char* buf : bufvec)
		{
			remove_buffer_in_use(buf);
			free_buffer_impl(buf, l);
		}
	}

	void disk_buffer_pool::free_buffer(char* buf)
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);
		remove_buffer_in_use(buf);
		free_buffer_impl(buf, l);
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

	void disk_buffer_pool::free_buffer_impl(char* buf, std::unique_lock<std::mutex>& l)
	{
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(m_magic == 0x1337);
		TORRENT_ASSERT(l.owns_lock());
		TORRENT_UNUSED(l);

		std::free(buf);

		--m_in_use;
	}

}
}
