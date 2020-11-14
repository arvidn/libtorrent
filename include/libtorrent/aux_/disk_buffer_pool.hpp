/*

Copyright (c) 2011, 2014-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Steven Siloti
Copyright (c) 2016, 2020, Alden Torres
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

#include "libtorrent/config.hpp"

#if TORRENT_USE_INVARIANT_CHECKS
#include <set>
#endif
#include <vector>
#include <mutex>
#include <functional>
#include <memory>

#include "libtorrent/io_context.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/storage_utils.hpp" // for iovec_t

namespace libtorrent {

	struct settings_interface;
	struct disk_observer;

namespace aux {

	struct TORRENT_EXTRA_EXPORT disk_buffer_pool
	{
		explicit disk_buffer_pool(io_context& ios);
		~disk_buffer_pool();
		disk_buffer_pool(disk_buffer_pool const&) = delete;
		disk_buffer_pool& operator=(disk_buffer_pool const&) = delete;

		char* allocate_buffer(char const* category);
		char* allocate_buffer(bool& exceeded, std::shared_ptr<disk_observer> o
			, char const* category);
		void free_buffer(char* buf);
		void free_multiple_buffers(span<char*> bufvec);

		int in_use() const
		{
			std::unique_lock<std::mutex> l(m_pool_mutex);
			return m_in_use;
		}

		void set_settings(settings_interface const& sett);

	private:

		void free_buffer_impl(char* buf, std::unique_lock<std::mutex>& l);
		char* allocate_buffer_impl(std::unique_lock<std::mutex>& l, char const* category);

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
		std::vector<std::weak_ptr<disk_observer>> m_observers;

		// set to true to throttle more allocations
		bool m_exceeded_max_size;

		// this is the main thread io_context. Callbacks are
		// posted on this in order to have them execute in
		// the main thread.
		io_context& m_ios;

		void check_buffer_level(std::unique_lock<std::mutex>& l);
		void remove_buffer_in_use(char* buf);

		mutable std::mutex m_pool_mutex;

		// this is specifically exempt from release_asserts
		// since it's a quite costly check. Only for debug
		// builds.
#if TORRENT_USE_INVARIANT_CHECKS
		std::set<char*> m_buffers_in_use;
#endif
#if TORRENT_USE_ASSERTS
		int m_magic = 0x1337;
		bool m_settings_set = false;
#endif
	};

}
}

#endif // TORRENT_DISK_BUFFER_POOL_HPP
