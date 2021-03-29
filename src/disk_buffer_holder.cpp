/*

Copyright (c) 2008, 2014, 2016-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/disk_buffer_holder.hpp"
#include <utility>

namespace lt {

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

	disk_buffer_holder::~disk_buffer_holder() { reset(); }
}
