/*

Copyright (c) 2008-2009, 2013-2021, Arvid Norberg
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_BUFFER_HOLDER_HPP_INCLUDED
#define TORRENT_DISK_BUFFER_HOLDER_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/span.hpp"
#include <utility>
#include <vector>

#ifndef TORRENT_DEBUG_BUFFER_POOL
#define TORRENT_DEBUG_BUFFER_POOL 0
#endif

namespace libtorrent {

	// the interface for freeing disk buffers, used by the disk_buffer_holder.
	// when implementing disk_interface, this must also be implemented in order
	// to return disk buffers back to libtorrent
	struct TORRENT_EXPORT buffer_allocator_interface
	{
		virtual void free_disk_buffer(char* b) = 0;
		virtual void free_multiple_buffers(span<char*> bufs) = 0;
#if TORRENT_DEBUG_BUFFER_POOL
		virtual void rename_buffer(char* buf, char const* category) = 0;
#endif
	protected:
		~buffer_allocator_interface() = default;
	};

	struct bulk_free_buffer;

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

#if TORRENT_DEBUG_BUFFER_POOL
		void rename(char const* category);
#endif
	private:

		friend struct bulk_free_buffer;

		buffer_allocator_interface* m_allocator = nullptr;
		char* m_buf = nullptr;
		int m_size = 0;
	};

	// Accumulates disk buffers to be freed in a single batch call, reducing
	// allocator mutex acquisitions from one per buffer to one per batch.
	// All buffers added must share the same allocator.
	// Freeing happens in the destructor.
	struct TORRENT_EXPORT bulk_free_buffer
	{
		bulk_free_buffer() = default;
		bulk_free_buffer(bulk_free_buffer const&) = delete;
		bulk_free_buffer& operator=(bulk_free_buffer const&) = delete;

		// transfer ownership of h's buffer into this batch.
		void add(disk_buffer_holder h);
		~bulk_free_buffer();

	private:

		buffer_allocator_interface* m_allocator = nullptr;
		std::vector<char*> m_bufs;
	};

}

#endif
