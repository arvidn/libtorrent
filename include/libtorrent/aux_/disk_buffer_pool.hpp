/*

Copyright (c) 2011, 2014-2017, 2019-2020, 2022, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
Copyright (c) 2016, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_BUFFER_POOL_HPP
#define TORRENT_DISK_BUFFER_POOL_HPP

#include "libtorrent/config.hpp"

#ifndef TORRENT_DEBUG_BUFFER_POOL
#define TORRENT_DEBUG_BUFFER_POOL 0
#endif

#if TORRENT_DEBUG_BUFFER_POOL
#include <map>
#endif
#include <chrono>
#include <vector>
#include <mutex>
#include <functional>
#include <memory>
#include <optional>

#include "libtorrent/span.hpp"
#include "libtorrent/disk_buffer_holder.hpp" // for buffer_allocator_interface

namespace libtorrent {


namespace aux {

	struct TORRENT_EXTRA_EXPORT disk_buffer_pool final
		: buffer_allocator_interface
	{
		// how often backends should call expire_buffers()
		static constexpr std::chrono::seconds expire_interval{5};

		// max_cached_buffers is the number of freed buffers kept for reuse. A
		// larger cache should only be used by callers that also call
		// expire_buffers() periodically
		explicit disk_buffer_pool(int max_cached_buffers = 64);
		~disk_buffer_pool();
		disk_buffer_pool(disk_buffer_pool const&) = delete;
		disk_buffer_pool& operator=(disk_buffer_pool const&) = delete;

		char* allocate_buffer(char const* category);
		void free_disk_buffer(char* b) override { free_buffer(b); }
		void free_buffer(char* buf);
		void free_multiple_buffers(span<char*> bufvec) override;

		// releases cached buffers that were not needed since the previous
		// call. Meant to be called periodically, every expire_interval
		void expire_buffers();

		int in_use() const
		{
			std::unique_lock<std::mutex> l(m_pool_mutex);
			return m_in_use;
		}

#if TORRENT_DEBUG_BUFFER_POOL
		void rename_buffer(char* buf, char const* category) override;
#endif
	private:
		int const m_max_cached_buffers;

		// freed buffers kept for reuse (LIFO), protected by m_pool_mutex.
		// Capacity is reserved up front so that pushing never allocates
		// while the mutex is held
		std::vector<char*> m_free_buffers;

		// the fewest cached buffers seen since the last expire_buffers().
		// Buffers below this level in the stack were not needed at all
		int m_low_water = 0;

		// number of disk buffers currently allocated
		int m_in_use = 0;

		void remove_buffer_in_use(char* buf);

		mutable std::mutex m_pool_mutex;

		// this is specifically exempt from release_asserts
		// since it's a quite costly check. Only for debug
		// builds.
#if TORRENT_DEBUG_BUFFER_POOL
		std::map<char*, char const*> m_buffers_in_use;
		std::map<std::string, int> m_histogram;
		time_t m_last_log = std::time(nullptr);

		void maybe_log();
#endif
#if TORRENT_USE_ASSERTS
		int m_magic = 0x1337;
#endif
	};

}
}

#endif // TORRENT_DISK_BUFFER_POOL_HPP
