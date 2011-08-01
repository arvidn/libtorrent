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

#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/disk_io_thread.hpp"

namespace libtorrent
{

	disk_buffer_holder::disk_buffer_holder(aux::session_impl& ses, char* buf)
		: m_disk_pool(ses.m_disk_thread), m_buf(buf), m_num_blocks(1)
	{
		TORRENT_ASSERT(buf == 0 || m_disk_pool.is_disk_buffer(buf));
	}

	disk_buffer_holder::disk_buffer_holder(disk_buffer_pool& iothread, char* buf)
		: m_disk_pool(iothread), m_buf(buf), m_num_blocks(1)
	{
		TORRENT_ASSERT(buf == 0 || m_disk_pool.is_disk_buffer(buf));
	}

	disk_buffer_holder::disk_buffer_holder(disk_buffer_pool& iothread, char* buf, int num_blocks)
		: m_disk_pool(iothread), m_buf(buf), m_num_blocks(num_blocks)
	{
		TORRENT_ASSERT(buf == 0 || m_disk_pool.is_disk_buffer(buf));
	}

	void disk_buffer_holder::reset(char* buf, int num_blocks)
	{
		if (m_buf)
		{
			if (m_num_blocks == 1) m_disk_pool.free_buffer(m_buf);
			else m_disk_pool.free_buffers(m_buf, m_num_blocks);
		}
		m_buf = buf;
		m_num_blocks = num_blocks;
	}

	char* disk_buffer_holder::release()
	{
		char* ret = m_buf;
		m_buf = 0;
		return ret;
	}

	disk_buffer_holder::~disk_buffer_holder()
	{
		if (m_buf)
		{
			if (m_num_blocks == 1) m_disk_pool.free_buffer(m_buf);
			else m_disk_pool.free_buffers(m_buf, m_num_blocks);
		}
	}
}

