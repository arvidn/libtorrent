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

#ifndef TORRENT_CHAINED_BUFFER_HPP_INCLUDED
#define TORRENT_CHAINED_BUFFER_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/block_cache_reference.hpp"
#include "libtorrent/aux_/aligned_storage.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/buffer.hpp"

#include <deque>
#include <vector>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/buffer.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#ifdef _MSC_VER
// visual studio requires the value in a deque to be copyable. C++11
// has looser requirements depending on which functions are actually used.
#define TORRENT_CPP98_DEQUE 1
#else
#define TORRENT_CPP98_DEQUE 0
#endif

namespace libtorrent {

	// TODO: 2 this type should probably be renamed to send_buffer
	struct TORRENT_EXTRA_EXPORT chained_buffer : private single_threaded
	{
		chained_buffer(): m_bytes(0), m_capacity(0)
		{
			thread_started();
#if TORRENT_USE_ASSERTS
			m_destructed = false;
#endif
		}

	private:

		// destructs/frees the holder object
		using destruct_holder_fun = void (*)(void*);
		using move_construct_holder_fun = void (*)(void*, void*);

		struct buffer_t
		{
			buffer_t() {}
#if TORRENT_CPP98_DEQUE
			buffer_t(buffer_t&& rhs) noexcept
			{
				destruct_holder = rhs.destruct_holder;
				move_holder = rhs.move_holder;
				buf = rhs.buf;
				size = rhs.size;
				used_size = rhs.used_size;
				move_holder(&holder, &rhs.holder);
			}
			buffer_t& operator=(buffer_t&& rhs) & noexcept
			{
				destruct_holder(&holder);
				destruct_holder = rhs.destruct_holder;
				move_holder = rhs.move_holder;
				buf = rhs.buf;
				size = rhs.size;
				used_size = rhs.used_size;
				move_holder(&holder, &rhs.holder);
				return *this;
			}
			buffer_t(buffer_t const& rhs) noexcept
				: buffer_t(std::move(const_cast<buffer_t&>(rhs))) {}
			buffer_t& operator=(buffer_t const& rhs) & noexcept
			{ return this->operator=(std::move(const_cast<buffer_t&>(rhs))); }
#else
			buffer_t(buffer_t&&) = delete;
			buffer_t& operator=(buffer_t&&) = delete;
			buffer_t(buffer_t const&) = delete;
			buffer_t& operator=(buffer_t const&) = delete;
#endif

			destruct_holder_fun destruct_holder;
#if TORRENT_CPP98_DEQUE
			move_construct_holder_fun move_holder;
#endif
			aux::aligned_storage<32>::type holder;
			char* buf = nullptr; // the first byte of the buffer
			int size = 0; // the total size of the buffer
			int used_size = 0; // this is the number of bytes to send/receive
		};

	public:

		bool empty() const { return m_bytes == 0; }
		int size() const { return m_bytes; }
		int capacity() const { return m_capacity; }

		void pop_front(int bytes_to_pop);

		template <typename Holder>
		void append_buffer(Holder buffer, int used_size)
		{
			TORRENT_ASSERT(is_single_thread());
			TORRENT_ASSERT(int(buffer.size()) >= used_size);
			m_vec.emplace_back();
			buffer_t& b = m_vec.back();
			init_buffer_entry<Holder>(b, std::move(buffer), used_size);
		}

		template <typename Holder>
		void prepend_buffer(Holder buffer, int used_size)
		{
			TORRENT_ASSERT(is_single_thread());
			TORRENT_ASSERT(int(buffer.size()) >= used_size);
			m_vec.emplace_front();
			buffer_t& b = m_vec.front();
			init_buffer_entry<Holder>(b, std::move(buffer), used_size);
		}

		// returns the number of bytes available at the
		// end of the last chained buffer.
		int space_in_last_buffer();

		// tries to copy the given buffer to the end of the
		// last chained buffer. If there's not enough room
		// it returns nullptr
		char* append(span<char const> buf);

		// tries to allocate memory from the end
		// of the last buffer. If there isn't
		// enough room, returns 0
		char* allocate_appendix(int s);

		span<boost::asio::const_buffer const> build_iovec(int to_send);

		void clear();

		void build_mutable_iovec(int bytes, std::vector<span<char>>& vec);

		~chained_buffer();

	private:

		template <typename Holder>
		void init_buffer_entry(buffer_t& b, Holder buf, int used_size)
		{
			static_assert(sizeof(Holder) <= sizeof(b.holder), "buffer holder too large");

			b.buf = buf.data();
			b.size = static_cast<int>(buf.size());
			b.used_size = used_size;

#ifdef _MSC_VER
// this appears to be a false positive msvc warning
#pragma warning(push, 1)
#pragma warning(disable : 4100)
#endif
			b.destruct_holder = [](void* holder)
			{ reinterpret_cast<Holder*>(holder)->~Holder(); };

#if TORRENT_CPP98_DEQUE
			b.move_holder = [](void* dst, void* src)
			{ new (dst) Holder(std::move(*reinterpret_cast<Holder*>(src))); };
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

			new (&b.holder) Holder(std::move(buf));

			m_bytes += used_size;
			TORRENT_ASSERT(m_capacity < (std::numeric_limits<int>::max)() - b.size);
			m_capacity += b.size;
			TORRENT_ASSERT(m_bytes <= m_capacity);
		}

		template <typename Buffer>
		void build_vec(int bytes, std::vector<Buffer>& vec);

		// this is the list of all the buffers we want to
		// send
		std::deque<buffer_t> m_vec;

		// this is the number of bytes in the send buf.
		// this will always be equal to the sum of the
		// size of all buffers in vec
		int m_bytes;

		// the total size of all buffers in the chain
		// including unused space
		int m_capacity;

		// this is the vector of buffers used when
		// invoking the async write call
		std::vector<boost::asio::const_buffer> m_tmp_vec;

#if TORRENT_USE_ASSERTS
		bool m_destructed;
#endif
	};
}

#endif
