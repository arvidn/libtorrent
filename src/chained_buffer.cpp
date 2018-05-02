/*

Copyright (c) 2007-2018, Arvid Norberg
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

#include "libtorrent/chained_buffer.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	void chained_buffer::pop_front(int bytes_to_pop)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(bytes_to_pop <= m_bytes);
		while (bytes_to_pop > 0 && !m_vec.empty())
		{
			buffer_t& b = m_vec.front();
			if (b.used_size > bytes_to_pop)
			{
				b.start += bytes_to_pop;
				b.used_size -= bytes_to_pop;
				m_bytes -= bytes_to_pop;
				TORRENT_ASSERT(m_bytes <= m_capacity);
				TORRENT_ASSERT(m_bytes >= 0);
				TORRENT_ASSERT(m_capacity >= 0);
				break;
			}

			b.free_fun(b.buf, b.userdata, b.ref);
			m_bytes -= b.used_size;
			m_capacity -= b.size;
			bytes_to_pop -= b.used_size;
			TORRENT_ASSERT(m_bytes >= 0);
			TORRENT_ASSERT(m_capacity >= 0);
			TORRENT_ASSERT(m_bytes <= m_capacity);
			m_vec.pop_front();
		}
	}

	void chained_buffer::append_buffer(char* buffer, int s, int used_size
		, free_buffer_fun destructor, void* userdata
		, block_cache_reference ref)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(s >= used_size);
		buffer_t b;
		b.buf = buffer;
		b.size = s;
		b.start = buffer;
		b.used_size = used_size;
		b.free_fun = destructor;
		b.userdata = userdata;
		b.ref = ref;
		m_vec.push_back(b);

		m_bytes += used_size;
		m_capacity += s;
		TORRENT_ASSERT(m_bytes <= m_capacity);
	}

	void chained_buffer::prepend_buffer(char* buffer, int s, int used_size
		, free_buffer_fun destructor, void* userdata
		, block_cache_reference ref)
	{
		TORRENT_ASSERT(s >= used_size);
		buffer_t b;
		b.buf = buffer;
		b.size = s;
		b.start = buffer;
		b.used_size = used_size;
		b.free_fun = destructor;
		b.userdata = userdata;
		b.ref = ref;
		m_vec.push_front(b);

		m_bytes += used_size;
		m_capacity += s;
		TORRENT_ASSERT(m_bytes <= m_capacity);
	}

	// returns the number of bytes available at the
	// end of the last chained buffer.
	int chained_buffer::space_in_last_buffer()
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_vec.empty()) return 0;
		buffer_t& b = m_vec.back();
		return b.size - b.used_size - (b.start - b.buf);
	}

	// tries to copy the given buffer to the end of the
	// last chained buffer. If there's not enough room
	// it returns false
	char* chained_buffer::append(char const* buf, int s)
	{
		TORRENT_ASSERT(is_single_thread());
		char* insert = allocate_appendix(s);
		if (insert == 0) return 0;
		memcpy(insert, buf, s);
		return insert;
	}

	// tries to allocate memory from the end
	// of the last buffer. If there isn't
	// enough room, returns 0
	char* chained_buffer::allocate_appendix(int s)
	{
		TORRENT_ASSERT(is_single_thread());
		if (m_vec.empty()) return 0;
		buffer_t& b = m_vec.back();
		char* insert = b.start + b.used_size;
		if (insert + s > b.buf + b.size) return 0;
		b.used_size += s;
		m_bytes += s;
		TORRENT_ASSERT(m_bytes <= m_capacity);
		return insert;
	}

	std::vector<boost::asio::const_buffer> const& chained_buffer::build_iovec(int to_send)
	{
		TORRENT_ASSERT(is_single_thread());
		m_tmp_vec.clear();
		build_vec(to_send, m_tmp_vec);
		return m_tmp_vec;
	}

	void chained_buffer::build_mutable_iovec(int bytes, std::vector<boost::asio::mutable_buffer> &vec)
	{
		build_vec(bytes, vec);
	}

	template <typename Buffer>
	void chained_buffer::build_vec(int bytes, std::vector<Buffer> &vec)
	{
		for (std::deque<buffer_t>::iterator i = m_vec.begin()
			, end(m_vec.end()); bytes > 0 && i != end; ++i)
		{
			if (i->used_size > bytes)
			{
				TORRENT_ASSERT(bytes > 0);
				vec.push_back(Buffer(i->start, bytes));
				break;
			}
			TORRENT_ASSERT(i->used_size > 0);
			vec.push_back(Buffer(i->start, i->used_size));
			bytes -= i->used_size;
		}
	}

	void chained_buffer::clear()
	{
		for (std::deque<buffer_t>::iterator i = m_vec.begin()
			, end(m_vec.end()); i != end; ++i)
		{
			i->free_fun(i->buf, i->userdata, i->ref);
		}
		m_bytes = 0;
		m_capacity = 0;
		m_vec.clear();
	}

	chained_buffer::~chained_buffer()
	{
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(!m_destructed);
		m_destructed = true;
#endif
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_bytes >= 0);
		TORRENT_ASSERT(m_capacity >= 0);
		clear();
	}

}

