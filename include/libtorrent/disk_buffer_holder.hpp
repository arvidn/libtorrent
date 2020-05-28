/*

Copyright (c) 2008-2009, 2013-2019, Arvid Norberg
Copyright (c) 2018, Alden Torres
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

#ifndef TORRENT_DISK_BUFFER_HOLDER_HPP_INCLUDED
#define TORRENT_DISK_BUFFER_HOLDER_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include <utility>

namespace libtorrent {

	// the interface for freeing disk buffers, used by the disk_buffer_holder.
	// when implementing disk_interface, this must also be implemented in order
	// to return disk buffers back to libtorrent
	struct TORRENT_EXPORT buffer_allocator_interface
	{
		virtual void free_disk_buffer(char* b) = 0;
	protected:
		~buffer_allocator_interface() = default;
	};

	// The disk buffer holder acts like a ``unique_ptr`` that frees a disk buffer
	// when it's destructed
	//
	// If this buffer holder is moved-from, default constructed or reset,
	// ``data()`` will return nullptr.
	struct TORRENT_EXPORT disk_buffer_holder
	{
		disk_buffer_holder& operator=(disk_buffer_holder&&) & noexcept;
		disk_buffer_holder(disk_buffer_holder&&) noexcept;

		disk_buffer_holder& operator=(disk_buffer_holder const&) = delete;
		disk_buffer_holder(disk_buffer_holder const&) = delete;

		// construct a buffer holder that will free the held buffer
		// using a disk buffer pool directly (there's only one
		// disk_buffer_pool per session)
		disk_buffer_holder(buffer_allocator_interface& alloc
			, char* buf, int sz) noexcept;

		// default construct a holder that does not own any buffer
		disk_buffer_holder() noexcept = default;

		// frees disk buffer held by this object
		~disk_buffer_holder();

		// return a pointer to the held buffer, if any. Otherwise returns nullptr.
		char* data() const noexcept { return m_buf; }

		// free the held disk buffer, if any, and clear the holder. This sets the
		// holder object to a default-constructed state
		void reset();

		// swap pointers of two disk buffer holders.
		void swap(disk_buffer_holder& h) noexcept
		{
			using std::swap;
			swap(h.m_allocator, m_allocator);
			swap(h.m_buf, m_buf);
			swap(h.m_size, m_size);
		}

		// if this returns true, the buffer may not be modified in place
		bool is_mutable() const noexcept { return false; }

		// implicitly convertible to true if the object is currently holding a
		// buffer
		explicit operator bool() const noexcept { return m_buf != nullptr; }

		std::ptrdiff_t size() const { return m_size; }

	private:

		buffer_allocator_interface* m_allocator = nullptr;
		char* m_buf = nullptr;
		int m_size = 0;
	};

}

#endif
