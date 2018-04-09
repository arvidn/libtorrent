/*

Copyright (c) 2015-2018, Arvid Norberg
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

#ifndef TORRENT_STACK_ALLOCATOR

#include "libtorrent/assert.hpp"
#include "libtorrent/buffer.hpp"

namespace libtorrent { namespace aux
{

	struct stack_allocator
	{
		stack_allocator() {}

		int copy_string(std::string const& str)
		{
			int ret = int(m_storage.size());
			m_storage.resize(ret + str.length() + 1);
			strcpy(&m_storage[ret], str.c_str());
			return ret;
		}

		int copy_string(char const* str)
		{
			int ret = int(m_storage.size());
			int len = strlen(str);
			m_storage.resize(ret + len + 1);
			strcpy(&m_storage[ret], str);
			return ret;
		}

		int copy_buffer(char const* buf, int size)
		{
			int ret = int(m_storage.size());
			if (size < 1) return -1;
			m_storage.resize(ret + size);
			memcpy(&m_storage[ret], buf, size);
			return ret;
		}

		int allocate(int bytes)
		{
			if (bytes < 1) return -1;
			int ret = int(m_storage.size());
			m_storage.resize(ret + bytes);
			return ret;
		}

		char* ptr(int idx)
		{
			if(idx < 0) return NULL;
			TORRENT_ASSERT(idx < int(m_storage.size()));
			return &m_storage[idx];
		}

		char const* ptr(int idx) const
		{
			if(idx < 0) return NULL;
			TORRENT_ASSERT(idx < int(m_storage.size()));
			return &m_storage[idx];
		}

		void swap(stack_allocator& rhs)
		{
			m_storage.swap(rhs.m_storage);
		}

		void reset()
		{
			m_storage.clear();
		}

	private:

		// non-copyable
		stack_allocator(stack_allocator const&);
		stack_allocator& operator=(stack_allocator const&);

		buffer m_storage;
	};

} }

#endif

