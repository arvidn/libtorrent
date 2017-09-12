/*

Copyright (c) 2008-2016, Arvid Norberg
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

#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/storage.hpp" // for storage_interface
#include "libtorrent/aux_/block_cache_reference.hpp"

namespace libtorrent {

	disk_buffer_holder::disk_buffer_holder(buffer_allocator_interface& alloc, char* buf) noexcept
		: m_allocator(&alloc), m_buf(buf), m_ref()
	{}

	disk_buffer_holder::disk_buffer_holder(buffer_allocator_interface& alloc
		, aux::block_cache_reference const& ref, char* buf) noexcept
		: m_allocator(&alloc), m_buf(buf), m_ref(ref)
	{}

	disk_buffer_holder& disk_buffer_holder::operator=(disk_buffer_holder&& h) noexcept
	{
		disk_buffer_holder(std::move(h)).swap(*this);
		return *this;
	}

	disk_buffer_holder::disk_buffer_holder(disk_buffer_holder&& h) noexcept
		: m_allocator(h.m_allocator), m_buf(h.m_buf), m_ref(h.m_ref)
	{
		// we own this buffer now
		h.m_buf = nullptr;
		h.m_ref = aux::block_cache_reference();
	}

	void disk_buffer_holder::reset()
	{
		if (m_buf) m_allocator->free_disk_buffer(m_buf, m_ref);
		m_buf = nullptr;
		m_ref = aux::block_cache_reference();
	}

	void disk_buffer_holder::reset(char* buf, aux::block_cache_reference const& ref)
	{
		if (m_buf) m_allocator->free_disk_buffer(m_buf, m_ref);
		m_buf = buf;
		m_ref = ref;
	}

	char* disk_buffer_holder::release() noexcept
	{
		TORRENT_ASSERT(m_ref.cookie == aux::block_cache_reference::none);
		char* ret = m_buf;
		m_buf = nullptr;
		m_ref = aux::block_cache_reference();
		return ret;
	}

	disk_buffer_holder::~disk_buffer_holder() { reset(); }
}

