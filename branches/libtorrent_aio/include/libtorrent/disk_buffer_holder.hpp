/*

Copyright (c) 2008-2012, Arvid Norberg
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
#include <boost/shared_ptr.hpp>

namespace libtorrent
{
	struct disk_io_thread;
	struct disk_observer;

	struct buffer_allocator_interface
	{
		virtual char* allocate_disk_buffer(char const* category) = 0;
		virtual void free_disk_buffer(char* b) = 0;
		virtual void reclaim_block(block_cache_reference ref) = 0;
		virtual char* allocate_disk_buffer(bool& exceeded
			, boost::shared_ptr<disk_observer> o
			, char const* category) = 0;
	};

	struct TORRENT_EXTRA_EXPORT disk_buffer_holder
	{
		disk_buffer_holder(buffer_allocator_interface& alloc, char* buf);
		disk_buffer_holder(buffer_allocator_interface& alloc, disk_io_job const& j);
		~disk_buffer_holder();
		char* release();
		char* get() const { return m_buf; }
		void reset(disk_io_job const& j);
		void reset(char* buf = 0);
		void swap(disk_buffer_holder& h)
		{
			TORRENT_ASSERT(&h.m_allocator == &m_allocator);
			std::swap(h.m_buf, m_buf);
			std::swap(h.m_ref, m_ref);
		}

		block_cache_reference ref() const { return m_ref; }

		typedef char* (disk_buffer_holder::*unspecified_bool_type)();
		operator unspecified_bool_type() const
		{ return m_buf == 0? 0: &disk_buffer_holder::release; }

	private:
		buffer_allocator_interface& m_allocator;
		char* m_buf;
		block_cache_reference m_ref;
	};

}

#endif

