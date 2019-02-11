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

#include <algorithm> // for copy

namespace libtorrent {

	void chained_buffer::pop_front(int bytes_to_pop)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!m_destructed);
		TORRENT_ASSERT(bytes_to_pop <= m_bytes);
		while (bytes_to_pop > 0 && !m_vec.empty())
		{
			buffer_t& b = m_vec.front();
			if (b.used_size > bytes_to_pop)
			{
				b.buf += bytes_to_pop;
				b.used_size -= bytes_to_pop;
				b.size -= bytes_to_pop;
				m_capacity -= bytes_to_pop;
				m_bytes -= bytes_to_pop;
				TORRENT_ASSERT(m_bytes <= m_capacity);
				TORRENT_ASSERT(m_bytes >= 0);
				TORRENT_ASSERT(m_capacity >= 0);
				break;
			}

			b.destruct_holder(static_cast<void*>(&b.holder));
			m_bytes -= b.used_size;
			m_capacity -= b.size;
			bytes_to_pop -= b.used_size;
			TORRENT_ASSERT(m_bytes >= 0);
			TORRENT_ASSERT(m_capacity >= 0);
			TORRENT_ASSERT(m_bytes <= m_capacity);
			m_vec.pop_front();
		}
	}

	// returns the number of bytes available at the
	// end of the last chained buffer.
	int chained_buffer::space_in_last_buffer()
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!m_destructed);
		if (m_vec.empty()) return 0;
		buffer_t& b = m_vec.back();
		TORRENT_ASSERT(b.buf != nullptr);
		return b.size - b.used_size;
	}

	// tries to copy the given buffer to the end of the
	// last chained buffer. If there's not enough room
	// it returns nullptr
	char* chained_buffer::append(span<char const> buf)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!m_destructed);
		char* const insert = allocate_appendix(static_cast<int>(buf.size()));
		if (insert == nullptr) return nullptr;
		std::copy(buf.begin(), buf.end(), insert);
		return insert;
	}

	// tries to allocate memory from the end
	// of the last buffer. If there isn't
	// enough room, returns 0
	char* chained_buffer::allocate_appendix(int const s)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!m_destructed);
		if (m_vec.empty()) return nullptr;
		buffer_t& b = m_vec.back();
		TORRENT_ASSERT(b.buf != nullptr);
		char* const insert = b.buf + b.used_size;
		if (insert + s > b.buf + b.size) return nullptr;
		b.used_size += s;
		m_bytes += s;
		TORRENT_ASSERT(m_bytes <= m_capacity);
		return insert;
	}

	span<boost::asio::const_buffer const> chained_buffer::build_iovec(int const to_send)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(!m_destructed);
		m_tmp_vec.clear();
		build_vec(to_send, m_tmp_vec);
		return m_tmp_vec;
	}

	void chained_buffer::build_mutable_iovec(int bytes, std::vector<span<char>> &vec)
	{
		TORRENT_ASSERT(!m_destructed);
		build_vec(bytes, vec);
	}

	template <typename Buffer>
	void chained_buffer::build_vec(int bytes, std::vector<Buffer>& vec)
	{
		TORRENT_ASSERT(!m_destructed);
		for (auto i = m_vec.begin(), end(m_vec.end()); bytes > 0 && i != end; ++i)
		{
			TORRENT_ASSERT(i->buf != nullptr);
			if (i->used_size > bytes)
			{
				TORRENT_ASSERT(bytes > 0);
				vec.emplace_back(i->buf, std::size_t(bytes));
				break;
			}
			TORRENT_ASSERT(i->used_size > 0);
			vec.emplace_back(i->buf, std::size_t(i->used_size));
			bytes -= i->used_size;
		}
	}

	void chained_buffer::clear()
	{
		TORRENT_ASSERT(!m_destructed);
		for (auto& b : m_vec)
			b.destruct_holder(static_cast<void*>(&b.holder));
		m_bytes = 0;
		m_capacity = 0;
		m_vec.clear();
	}

	chained_buffer::~chained_buffer()
	{
		TORRENT_ASSERT(!m_destructed);
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(m_bytes >= 0);
		TORRENT_ASSERT(m_capacity >= 0);
		clear();
#if TORRENT_USE_ASSERTS
		m_destructed = true;
#endif
	}

}
