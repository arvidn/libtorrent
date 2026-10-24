/*

Copyright (c) 2008, 2014, 2016-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/disk_buffer_holder.hpp"
#include <cstdint>
#include <utility>

namespace libtorrent {

namespace {

// the bit marks the exclusive case, so that a holder with no allocator reads
// as non-exclusive, which is the conservative answer
constexpr std::uintptr_t exclusive_bit = 1;
static_assert(alignof(buffer_allocator_interface) > exclusive_bit,
	"no spare low bit in buffer_allocator_interface*");
}

buffer_allocator_interface* disk_buffer_holder::allocator() const noexcept
{
	return reinterpret_cast<buffer_allocator_interface*>(
		reinterpret_cast<std::uintptr_t>(m_allocator) & ~exclusive_bit);
}

bool disk_buffer_holder::is_exclusive() const noexcept
{
	return (reinterpret_cast<std::uintptr_t>(m_allocator) & exclusive_bit) != 0;
}

void disk_buffer_holder::swap(disk_buffer_holder& h) noexcept
{
	using std::swap;
	swap(h.m_allocator, m_allocator);
	swap(h.m_buf, m_buf);
}

disk_buffer_holder::disk_buffer_holder(buffer_allocator_interface& alloc, char* const buf) noexcept
	: disk_buffer_holder(alloc, buf, false)
{}

disk_buffer_holder::disk_buffer_holder(
	buffer_allocator_interface& alloc, char* const buf, bool const exclusive) noexcept
	: m_allocator(reinterpret_cast<buffer_allocator_interface*>(
		  reinterpret_cast<std::uintptr_t>(&alloc) | (exclusive ? exclusive_bit : 0)))
	, m_buf(buf)
{}

disk_buffer_holder::disk_buffer_holder(disk_buffer_holder&& h) noexcept
	: m_allocator(h.m_allocator)
	, m_buf(h.m_buf)
{
	h.m_buf = nullptr;
	// a holder with no buffer must not claim exclusive access to one
	h.m_allocator = h.allocator();
}

	disk_buffer_holder& disk_buffer_holder::operator=(disk_buffer_holder&& h) & noexcept
	{
		if (&h == this) return *this;
		disk_buffer_holder(std::move(h)).swap(*this);
		return *this;
	}

	void disk_buffer_holder::reset()
	{
		if (m_buf)
			allocator()->free_disk_buffer(m_buf);
		m_buf = nullptr;
		m_allocator = allocator();
	}

#if TORRENT_DEBUG_BUFFER_POOL
	void disk_buffer_holder::rename(char const* category)
	{
		if (m_buf != nullptr)
			allocator()->rename_buffer(m_buf, category);
	}
#endif

	disk_buffer_holder::~disk_buffer_holder() { reset(); }

	disk_buffer_ref::disk_buffer_ref(disk_buffer_holder&& h) noexcept
		: m_buf(std::exchange(h.m_buf, nullptr))
	{
		h.m_allocator = nullptr;
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
