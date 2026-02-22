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
#include <vector>
#include <mutex>
#include <functional>
#include <memory>

#include "libtorrent/span.hpp"
#include "libtorrent/disk_buffer_holder.hpp" // for buffer_allocator_interface

namespace libtorrent {


namespace aux {

	struct TORRENT_EXTRA_EXPORT disk_buffer_pool final
		: buffer_allocator_interface
	{
		explicit disk_buffer_pool();
		~disk_buffer_pool();
		disk_buffer_pool(disk_buffer_pool const&) = delete;
		disk_buffer_pool& operator=(disk_buffer_pool const&) = delete;

		char* allocate_buffer(char const* category);
		void free_disk_buffer(char* b) override { free_buffer(b); }
		void free_buffer(char* buf);
		void free_multiple_buffers(span<char*> bufvec);

		int in_use() const
		{
			std::unique_lock<std::mutex> l(m_pool_mutex);
			return m_in_use;
		}

#if TORRENT_DEBUG_BUFFER_POOL
		void rename_buffer(char* buf, char const* category) override;
#endif
	private:

		void free_buffer_impl(char* buf, std::unique_lock<std::mutex>& l);
		char* allocate_buffer_impl(std::unique_lock<std::mutex>& l, char const* category);

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
