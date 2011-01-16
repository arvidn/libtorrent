/*

Copyright (c) 2007, Arvid Norberg
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

#ifndef TORRENT_CHAINED_BUFFER_HPP_INCLUDED
#define TORRENT_CHAINED_BUFFER_HPP_INCLUDED

#include <boost/function.hpp>
#include <boost/version.hpp>
#if BOOST_VERSION < 103500
#include <asio/buffer.hpp>
#else
#include <boost/asio/buffer.hpp>
#endif
#include <list>
#include <string.h> // for memcpy

namespace libtorrent
{
#if BOOST_VERSION >= 103500
	namespace asio = boost::asio;
#endif
	struct chained_buffer
	{
		chained_buffer(): m_bytes(0), m_capacity(0) {}

		struct buffer_t
		{
			boost::function<void(char*)> free; // destructs the buffer
			char* buf; // the first byte of the buffer
			int size; // the total size of the buffer

			char* start; // the first byte to send/receive in the buffer
			int used_size; // this is the number of bytes to send/receive
		};

		bool empty() const { return m_bytes == 0; }
		int size() const { return m_bytes; }
		int capacity() const { return m_capacity; }

		void pop_front(int bytes_to_pop)
		{
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

				b.free(b.buf);
				m_bytes -= b.used_size;
				m_capacity -= b.size;
				bytes_to_pop -= b.used_size;
				TORRENT_ASSERT(m_bytes >= 0);
				TORRENT_ASSERT(m_capacity >= 0);
				TORRENT_ASSERT(m_bytes <= m_capacity);
				m_vec.pop_front();
			}
		}

		template <class D>
		void append_buffer(char* buffer, int s, int used_size, D const& destructor)
		{
			TORRENT_ASSERT(s >= used_size);
			buffer_t b;
			b.buf = buffer;
			b.size = s;
			b.start = buffer;
			b.used_size = used_size;
			b.free = destructor;
			m_vec.push_back(b);

			m_bytes += used_size;
			m_capacity += s;
			TORRENT_ASSERT(m_bytes <= m_capacity);
		}

		// returns the number of bytes available at the
		// end of the last chained buffer.
		int space_in_last_buffer()
		{
			if (m_vec.empty()) return 0;
			buffer_t& b = m_vec.back();
			return b.size - b.used_size - (b.start - b.buf);
		}

		// tries to copy the given buffer to the end of the
		// last chained buffer. If there's not enough room
		// it returns false
		bool append(char const* buf, int s)
		{
			char* insert = allocate_appendix(s);
			if (insert == 0) return false;
			memcpy(insert, buf, s);
			return true;
		}

		// tries to allocate memory from the end
		// of the last buffer. If there isn't
		// enough room, returns 0
		char* allocate_appendix(int s)
		{
			if (m_vec.empty()) return 0;
			buffer_t& b = m_vec.back();
			char* insert = b.start + b.used_size;
			if (insert + s > b.buf + b.size) return 0;
			b.used_size += s;
			m_bytes += s;
			TORRENT_ASSERT(m_bytes <= m_capacity);
			return insert;
		}

		std::list<asio::const_buffer> const& build_iovec(int to_send)
		{
			m_tmp_vec.clear();

			for (std::list<buffer_t>::iterator i = m_vec.begin()
				, end(m_vec.end()); to_send > 0 && i != end; ++i)
			{
				if (i->used_size > to_send)
				{
					TORRENT_ASSERT(to_send > 0);
					m_tmp_vec.push_back(asio::const_buffer(i->start, to_send));
					break;
				}
				TORRENT_ASSERT(i->used_size > 0);
				m_tmp_vec.push_back(asio::const_buffer(i->start, i->used_size));
				to_send -= i->used_size;
			}
			return m_tmp_vec;
		}

		~chained_buffer()
		{
			for (std::list<buffer_t>::iterator i = m_vec.begin()
				, end(m_vec.end()); i != end; ++i)
			{
				i->free(i->buf);
			}
		}

	private:

		// this is the list of all the buffers we want to
		// send
		std::list<buffer_t> m_vec;

		// this is the number of bytes in the send buf.
		// this will always be equal to the sum of the
		// size of all buffers in vec
		int m_bytes;

		// the total size of all buffers in the chain
		// including unused space
		int m_capacity;

		// this is the vector of buffers used when
		// invoking the async write call
		std::list<asio::const_buffer> m_tmp_vec;
	};	
}

#endif

