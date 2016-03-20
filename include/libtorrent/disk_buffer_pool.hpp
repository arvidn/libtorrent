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

#include "libtorrent/io_service_fwd.hpp"
#include "libtorrent/file.hpp" // for iovec_t
#include "libtorrent/span.hpp"
#include "libtorrent/bitfield.hpp"

namespace libtorrent
{
	namespace aux { struct session_settings; }
	class alert;
	struct disk_observer;

	struct TORRENT_EXTRA_EXPORT disk_buffer_pool : boost::noncopyable
	{
		disk_buffer_pool(int block_size, io_service& ios
			, std::function<void()> const& trigger_trim);
		~disk_buffer_pool();

#if TORRENT_USE_ASSERTS
		bool is_disk_buffer(char* buffer
			, std::unique_lock<std::mutex>& l) const;
		bool is_disk_buffer(char* buffer) const;
#endif

		char* allocate_buffer(char const* category);
		char* allocate_buffer(std::shared_ptr<disk_observer> o
			, char const* category);
		void free_buffer(char* buf);
		void free_multiple_buffers(span<char*> bufvec);

		int allocate_iovec(span<file::iovec_t> iov);
		void free_iovec(span<file::iovec_t const> iov);

		int block_size() const { return m_block_size; }

		void release_memory();

		int in_use() const
		{
			std::unique_lock<std::mutex> l(m_pool_mutex);
			return m_in_use;
		}
		int num_to_evict(int num_needed = 0);
		int available() const
		{
			std::unique_lock<std::mutex> l(m_pool_mutex);
			return m_max_size - m_in_use;
		}

		void set_settings(aux::session_settings const& sett, error_code& ec);

	protected:

		void free_buffer_impl(char* buf, std::unique_lock<std::mutex>& l);
		char* allocate_buffer_impl(std::unique_lock<std::mutex>& l, char const* category);

		void allocate_pool(error_code& ec);
		void free_pool();

		// number of bytes per block. The BitTorrent
		// protocol defines the block size to 16 KiB.
		const int m_block_size;

		// number of disk buffers currently allocated
		int m_in_use;

		// cache size limit
		int m_max_size;

		// if we have exceeded the limit, we won't notify peers of allowing
		// allocations again until we drop below this low watermark
		int m_low_watermark;

		// if we exceed this limit, we start telling peers we're full and that
		// they should wait for notifications of being able to allocate new blocks
		int m_high_watermark;

		// this is the highest indexed allocated block. Every now and then this is
		// recalculated by scanning m_free_blocks
		int m_highest_allocated;

		// if we exceed the max number of buffers, we start
		// adding up callbacks to this queue. Once the number
		// of buffers in use drops below the low watermark,
		// we start calling these functions back
		std::vector<std::weak_ptr<disk_observer>> m_observers;

		// callback used to tell the cache it needs to free up some blocks
		std::function<void()> m_trigger_cache_trim;

		// set to true to throttle more allocations
		bool m_exceeded_max_size;

		// this is the main thread io_service. Callbacks are
		// posted on this in order to have them execute in
		// the main thread.
		io_service& m_ios;

	private:

		void check_buffer_level(std::unique_lock<std::mutex>& l);

		mutable std::mutex m_pool_mutex;

		// the pointer to the block of virtual address space
		// making up the mmapped cache space
		char* m_cache_pool;

		// the actual size, in bytes, of the memory block at m_cache_pool
		boost::int64_t m_pool_size;

		// each block in the cache_pool has a corresponding bit in this bitfield.
		// a set bit means the block is free to be used, a cleared bit means
		// the block is in use.
		bitfield m_free_blocks;

		// this is specifically exempt from release_asserts
		// since it's a quite costly check. Only for debug
		// builds.
#if TORRENT_USE_ASSERTS
		bitfield m_buffers_in_use;
		int m_magic;
		bool m_settings_set;
#endif
	};

}

#endif // TORRENT_DISK_BUFFER_POOL_HPP
