/*

Copyright (c) 2007-2016, Arvid Norberg
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

#include "libtorrent/config.hpp"

#include <boost/function/function1.hpp>
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
	struct TORRENT_EXTRA_EXPORT chained_buffer
	{
		chained_buffer(): m_bytes(0), m_capacity(0)
		{
#if TORRENT_USE_ASSERTS
			m_destructed = false;
#endif
		}

		struct buffer_t
		{
			boost::function<void(char*)> free; // destructs the buffer
			char* buf; // the first byte of the buffer
			char* start; // the first byte to send/receive in the buffer
			int size; // the total size of the buffer
			int used_size; // this is the number of bytes to send/receive
		};

		bool empty() const { return m_bytes == 0; }
		int size() const { return m_bytes; }
		int capacity() const { return m_capacity; }

		void pop_front(int bytes_to_pop);

		void append_buffer(char* buffer, int s, int used_size
			, boost::function<void(char*)> const& destructor);

		// returns the number of bytes available at the
		// end of the last chained buffer.
		int space_in_last_buffer();

		// tries to copy the given buffer to the end of the
		// last chained buffer. If there's not enough room
		// it returns false
		char* append(char const* buf, int s);

		// tries to allocate memory from the end
		// of the last buffer. If there isn't
		// enough room, returns 0
		char* allocate_appendix(int s);

		std::list<asio::const_buffer> const& build_iovec(int to_send);

		~chained_buffer();

	private:

		// this is the list of all the buffers we want to
		// send
		std::list<buffer_t> m_vec;

		// this is the vector of buffers used when
		// invoking the async write call
		std::list<asio::const_buffer> m_tmp_vec;

		// this is the number of bytes in the send buf.
		// this will always be equal to the sum of the
		// size of all buffers in vec
		int m_bytes;

		// the total size of all buffers in the chain
		// including unused space
		int m_capacity;

#if TORRENT_USE_ASSERTS
		bool m_destructed;
#endif
	};	
}

#endif

