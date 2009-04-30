/*

Copyright (c) 2008, Arvid Norberg
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
#include <algorithm>

namespace libtorrent
{

	namespace aux { struct session_impl; }
	struct disk_buffer_pool;

	struct TORRENT_EXPORT disk_buffer_holder
	{
		disk_buffer_holder(aux::session_impl& ses, char* buf);
		disk_buffer_holder(disk_buffer_pool& disk_pool, char* buf);
		disk_buffer_holder(disk_buffer_pool& disk_pool, char* buf, int num_blocks);
		~disk_buffer_holder();
		char* release();
		char* get() const { return m_buf; }
		void reset(char* buf = 0, int num_blocks = 1);
		void swap(disk_buffer_holder& h)
		{
			TORRENT_ASSERT(&h.m_disk_pool == &m_disk_pool);
			std::swap(h.m_buf, m_buf);
		}

		typedef char* (disk_buffer_holder::*unspecified_bool_type)();
		operator unspecified_bool_type() const
		{ return m_buf == 0? 0: &disk_buffer_holder::release; }

	private:
		disk_buffer_pool& m_disk_pool;
		char* m_buf;
		int m_num_blocks;
	};

}

#endif

