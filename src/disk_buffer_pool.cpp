/*

Copyright (c) 2011, 2013-2020, Arvid Norberg
Copyright (c) 2016, Steven Siloti
Copyright (c) 2016, 2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/disk_buffer_pool.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/settings_pack.hpp" // for settings_interface
#include "libtorrent/io_context.hpp"
#include "libtorrent/disk_observer.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size

#include "libtorrent/aux_/disable_warnings_push.hpp"

#ifdef TORRENT_BSD
#include <sys/sysctl.h>
#endif

#ifdef TORRENT_LINUX
#include <linux/unistd.h>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace lt::aux {

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

	disk_buffer_pool::disk_buffer_pool(io_context& ios)
		: m_in_use(0)
		, m_max_use(64)
		, m_low_watermark(std::max(m_max_use - 32, 0))
		, m_exceeded_max_size(false)
		, m_ios(ios)
	{}

	disk_buffer_pool::~disk_buffer_pool()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#if TORRENT_USE_ASSERTS
		m_magic = 0;
#endif
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
		post(m_ios, std::bind(&watermark_callback, std::move(cbs)));
	}

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
		}

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

		check_buffer_level(l);
	}

	void disk_buffer_pool::free_buffer(char* buf)
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);
		remove_buffer_in_use(buf);
		free_buffer_impl(buf, l);
		check_buffer_level(l);
	}

	void disk_buffer_pool::set_settings(settings_interface const& sett)
	{
		std::unique_lock<std::mutex> l(m_pool_mutex);

		int const pool_size = std::max(1, sett.get_int(settings_pack::max_queued_disk_bytes) / default_block_size);
		m_max_use = pool_size;
		m_low_watermark = m_max_use / 2;
		if (m_in_use >= m_max_use && !m_exceeded_max_size)
		{
			m_exceeded_max_size = true;
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
