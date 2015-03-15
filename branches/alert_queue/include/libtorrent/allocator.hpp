/*

Copyright (c) 2009-2014, Arvid Norberg
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

#ifndef TORRENT_ALLOCATOR_HPP_INCLUDED
#define TORRENT_ALLOCATOR_HPP_INCLUDED

#include <cstddef>
#include "libtorrent/config.hpp"

namespace libtorrent
{

	TORRENT_EXTRA_EXPORT int page_size();

	struct TORRENT_EXTRA_EXPORT page_aligned_allocator
	{
		typedef std::size_t size_type;
		typedef std::ptrdiff_t difference_type;

		static char* malloc(const size_type bytes);
		static void free(char* block);
#ifdef TORRENT_DEBUG_BUFFERS
		static bool in_use(char const* block);
#endif
	};

	struct TORRENT_EXTRA_EXPORT aligned_holder
	{
		aligned_holder(): m_buf(0) {}
		aligned_holder(int size): m_buf(page_aligned_allocator::malloc(size)) {}
		~aligned_holder() { if (m_buf) page_aligned_allocator::free(m_buf); }
		char* get() const { return m_buf; }
		void reset(char* buf = 0)
		{
			if (m_buf) page_aligned_allocator::free(m_buf);
			m_buf = buf;
		}
		void swap(aligned_holder& h)
		{
			char* tmp = m_buf;
			m_buf = h.m_buf;
			h.m_buf =  tmp;
		}
	private:
		aligned_holder(aligned_holder const&);
		aligned_holder& operator=(aligned_holder const&);
		char* m_buf;
	};

}

#endif

