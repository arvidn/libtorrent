/*

Copyright (c) 2008, 2014, 2016-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/disk_buffer_holder.hpp"
#include <utility>

namespace libtorrent {

	disk_buffer_holder::disk_buffer_holder(buffer_allocator_interface& alloc
		, char* const buf, int const sz) noexcept
		: m_allocator(&alloc), m_buf(buf), m_size(sz)
	{}

	disk_buffer_holder::disk_buffer_holder(disk_buffer_holder&& h) noexcept
		: m_allocator(h.m_allocator), m_buf(h.m_buf), m_size(h.m_size)
	{
		h.m_buf = nullptr;
		h.m_size = 0;
	}

	disk_buffer_holder& disk_buffer_holder::operator=(disk_buffer_holder&& h) & noexcept
	{
		if (&h == this) return *this;
		disk_buffer_holder(std::move(h)).swap(*this);
		return *this;
	}

	void disk_buffer_holder::reset()
	{
		if (m_buf) m_allocator->free_disk_buffer(m_buf);
		m_buf = nullptr;
		m_size = 0;
	}

#if TORRENT_DEBUG_BUFFER_POOL
	void disk_buffer_holder::rename(char const* category)
	{
		if (m_buf != nullptr)
			m_allocator->rename_buffer(m_buf, category);
	}
#endif

	disk_buffer_holder::~disk_buffer_holder() { reset(); }

	disk_buffer_ref::disk_buffer_ref(disk_buffer_holder&& h) noexcept
		: m_buf(std::exchange(h.m_buf, nullptr))
	{
		h.m_allocator = nullptr;
		h.m_size = 0;
	}

	disk_buffer_ref::disk_buffer_ref(disk_buffer_ref&& h) noexcept
		: m_buf(std::exchange(h.m_buf, nullptr))
	{}

	disk_buffer_ref& disk_buffer_ref::operator=(disk_buffer_ref&& h) noexcept
	{
		if (&h == this) return *this;
		TORRENT_ASSERT(m_buf == nullptr);
		m_buf = std::exchange(h.m_buf, nullptr);
		return *this;
	}

	void bulk_free_buffer::add(disk_buffer_ref r)
	{
		if (!r.m_buf) return;
		TORRENT_ASSERT(m_allocator != nullptr);
		try {
			m_bufs.push_back(r.m_buf);
		}
		catch (...) {
			// bulk-free is an optimisation; if we can't batch it, free it now
			m_allocator->free_disk_buffer(r.m_buf);
		}
		r.m_buf = nullptr;
	}

	bulk_free_buffer::~bulk_free_buffer()
	{
		if (m_allocator) m_allocator->free_multiple_buffers(m_bufs);
	}
}
