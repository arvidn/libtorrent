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

#ifndef TORRENT_DISK_BUFFER_HOLDER_HPP_INCLUDED
#define TORRENT_DISK_BUFFER_HOLDER_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/disk_io_job.hpp" // for block_cache_reference
#include <algorithm>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent
{
	struct disk_io_thread;
	struct disk_observer;

	struct TORRENT_EXTRA_EXPORT buffer_allocator_interface
	{
		virtual char* allocate_disk_buffer(char const* category) = 0;
		virtual void free_disk_buffer(char* b) = 0;
		virtual void reclaim_block(block_cache_reference ref) = 0;
		virtual char* allocate_disk_buffer(bool& exceeded
			, boost::shared_ptr<disk_observer> o
			, char const* category) = 0;
	protected:
		~buffer_allocator_interface() {}
	};

	// The disk buffer holder acts like a ``scoped_ptr`` that frees a disk buffer
	// when it's destructed, unless it's released. ``release`` returns the disk
	// buffer and transfers ownership and responsibility to free it to the caller.
	// 
	// A disk buffer is freed by passing it to ``session_impl::free_disk_buffer()``.
	// 
	// ``get()`` returns the pointer without transferring responsibility. If
	// this buffer has been released, ``buffer()`` will return 0.
	struct TORRENT_EXPORT disk_buffer_holder
	{
		// internal
		disk_buffer_holder(buffer_allocator_interface& alloc, char* buf);

		// construct a buffer holder that will free the held buffer
		// using a disk buffer pool directly (there's only one
		// disk_buffer_pool per session)
		disk_buffer_holder(buffer_allocator_interface& alloc, disk_io_job const& j);

		// frees any unreleased disk buffer held by this object
		~disk_buffer_holder();
		
		// return the held disk buffer and clear it from the
		// holder. The responsibility to free it is passed on
		// to the caller
		char* release();

		// return a pointer to the held buffer
		char* get() const { return m_buf; }

		// set the holder object to hold the specified buffer
		// (or NULL by default). If it's already holding a
		// disk buffer, it will first be freed.
		void reset(char* buf = 0);
		void reset(disk_io_job const& j);

		// swap pointers of two disk buffer holders.
		void swap(disk_buffer_holder& h)
		{
			TORRENT_ASSERT(&h.m_allocator == &m_allocator);
			std::swap(h.m_buf, m_buf);
			std::swap(h.m_ref, m_ref);
		}

		block_cache_reference ref() const { return m_ref; }

		typedef char* (disk_buffer_holder::*unspecified_bool_type)();

		// internal
		operator unspecified_bool_type() const
		{ return m_buf == 0? 0: &disk_buffer_holder::release; }

	private:
		// non-copyable
		disk_buffer_holder& operator=(disk_buffer_holder const&);
		disk_buffer_holder(disk_buffer_holder const*);

		buffer_allocator_interface& m_allocator;
		char* m_buf;
		block_cache_reference m_ref;
	};

}

#endif

