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

#ifndef TORRENT_HETEROGENEOUS_QUEUE_HPP_INCLUDED
#define TORRENT_HETEROGENEOUS_QUEUE_HPP_INCLUDED

#include <vector>
#include <cstdint>
#include <cstdlib> // for malloc
#include <type_traits>
#include <memory>

#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/throw.hpp"

namespace libtorrent {
namespace aux {

	struct free_deleter
	{ void operator()(char* ptr) { return std::free(ptr); } };

	inline std::size_t calculate_pad_bytes(char const* inptr, std::size_t alignment)
	{
		std::uintptr_t const ptr = reinterpret_cast<std::uintptr_t>(inptr);
		std::uintptr_t const offset = ptr & (alignment - 1);
		return (alignment - offset) & (alignment - 1);
	}
}

	template <class T>
	struct heterogeneous_queue
	{
		heterogeneous_queue() : m_storage(nullptr, aux::free_deleter()) {}
		heterogeneous_queue(heterogeneous_queue const&) = delete;
		heterogeneous_queue& operator=(heterogeneous_queue const&) = delete;

		template <class U, typename... Args>
		typename std::enable_if<std::is_base_of<T, U>::value, U&>::type
		emplace_back(Args&&... args)
		{
			// make the conservative assumption that we'll need the maximum padding
			// for this object, just for purposes of growing the storage
			if (std::size_t(m_size) + sizeof(header_t) + alignof(U) + sizeof(U) > std::size_t(m_capacity))
				grow_capacity(sizeof(header_t) + alignof(U) + sizeof(U));

			char* ptr = m_storage.get() + m_size;

			std::size_t const pad_bytes = aux::calculate_pad_bytes(ptr + sizeof(header_t), alignof(U));

			// pad_bytes is only 8 bits in the header, so types that need more than
			// 256 byte alignment may not be supported
			static_assert(alignof(U) <= 256
				, "heterogeneous_queue does not support types with alignment requirements > 256");

			// if this assert triggers, the type being added to the queue has
			// alignment requirements stricter than what malloc() returns. This is
			// not supported
			TORRENT_ASSERT((reinterpret_cast<std::uintptr_t>(m_storage.get())
				& (alignof(U) - 1)) == 0);

			// make sure the current position in the storage is aligned for
			// creating a heder_t object
			TORRENT_ASSERT((reinterpret_cast<std::uintptr_t>(ptr)
				& (alignof(header_t) - 1)) == 0);

			// length prefix
			header_t* hdr = new (ptr) header_t;
			hdr->pad_bytes = static_cast<std::uint8_t>(pad_bytes);
			hdr->move = &move<U>;
			ptr += sizeof(header_t) + pad_bytes;
			hdr->len = static_cast<std::uint16_t>(sizeof(U)
				+ aux::calculate_pad_bytes(ptr + sizeof(U),  alignof(header_t)));

			// make sure ptr is correctly aligned for the object we're about to
			// create there
			TORRENT_ASSERT((reinterpret_cast<std::uintptr_t>(ptr)
				& (alignof(U) - 1)) == 0);

			// construct in-place
			U* const ret = new (ptr) U(std::forward<Args>(args)...);

			// if we constructed the object without throwing any exception
			// update counters to indicate the new item is in there
			++m_num_items;
			m_size += int(sizeof(header_t) + pad_bytes + hdr->len);
			return *ret;
		}

		void get_pointers(std::vector<T*>& out)
		{
			out.clear();

			char* ptr = m_storage.get();
			char const* const end = m_storage.get() + m_size;
			while (ptr < end)
			{
				header_t* hdr = reinterpret_cast<header_t*>(ptr);
				ptr += sizeof(header_t) + hdr->pad_bytes;
				TORRENT_ASSERT(ptr + hdr->len <= end);
				out.push_back(reinterpret_cast<T*>(ptr));
				ptr += hdr->len;
			}
		}

		void swap(heterogeneous_queue& rhs)
		{
			std::swap(m_storage, rhs.m_storage);
			std::swap(m_capacity, rhs.m_capacity);
			std::swap(m_size, rhs.m_size);
			std::swap(m_num_items, rhs.m_num_items);
		}

		int size() const { return m_num_items; }
		bool empty() const { return m_num_items == 0; }

		void clear()
		{
			char* ptr = m_storage.get();
			char const* const end = m_storage.get() + m_size;
			while (ptr < end)
			{
				header_t* hdr = reinterpret_cast<header_t*>(ptr);
				ptr += sizeof(header_t) + hdr->pad_bytes;
				TORRENT_ASSERT(ptr + hdr->len <= end);
				T* a = reinterpret_cast<T*>(ptr);
				a->~T();
				ptr += hdr->len;
				hdr->~header_t();
			}
			m_size = 0;
			m_num_items = 0;
		}

		T* front()
		{
			if (m_size == 0) return nullptr;

			TORRENT_ASSERT(m_size > 1);
			char* ptr = m_storage.get();
			header_t* hdr = reinterpret_cast<header_t*>(ptr);
			TORRENT_ASSERT(sizeof(header_t) + hdr->pad_bytes + hdr->len
				<= std::size_t(m_size));
			ptr += sizeof(header_t) + hdr->pad_bytes;
			return reinterpret_cast<T*>(ptr);
		}

		~heterogeneous_queue() { clear(); }

	private:

		// this header is put in front of every element. It tells us
		// how many bytes it's using for its allocation, and it
		// also tells us how to move this type if we need to grow our
		// allocation.
		struct header_t
		{
			// the size of the object. From the start of the object, skip this many
			// bytes to get to the next header. Meaning this includes sufficient
			// padding to have the next entry be appropriately aligned for header_t
			std::uint16_t len;

			// the number of pad bytes between the end of this
			// header and the start of the object. This supports allocating types with
			// stricter alignment requirements
			std::uint8_t pad_bytes;

			void (*move)(char* dst, char* src);
		};

		void grow_capacity(int const size)
		{
			int const amount_to_grow = (std::max)(size
				, (std::max)(m_capacity * 3 / 2, 128));

			// we use malloc() to guarantee alignment
			std::unique_ptr<char, aux::free_deleter> new_storage(
				static_cast<char*>(std::malloc(std::size_t(m_capacity + amount_to_grow)))
				, aux::free_deleter());

			if (!new_storage)
				aux::throw_ex<std::bad_alloc>();

			char* src = m_storage.get();
			char* dst = new_storage.get();
			char const* const end = m_storage.get() + m_size;
			while (src < end)
			{
				header_t* src_hdr = reinterpret_cast<header_t*>(src);
				new (dst) header_t(*src_hdr);
				src += sizeof(header_t) + src_hdr->pad_bytes;
				dst += sizeof(header_t) + src_hdr->pad_bytes;
				int const len = src_hdr->len;
				TORRENT_ASSERT(src + len <= end);
				// this is no-throw
				src_hdr->move(dst, src);
				src_hdr->~header_t();
				src += len ;
				dst += len;
			}

			m_storage.swap(new_storage);
			m_capacity += amount_to_grow;
		}

		template <class U>
		static void move(char* dst, char* src) noexcept
		{
			static_assert(std::is_nothrow_move_constructible<U>::value
				, "heterogeneous queue only supports noexcept move constructible types");
			static_assert(std::is_nothrow_destructible<U>::value
				, "heterogeneous queue only supports noexcept destructible types");
			U& rhs = *reinterpret_cast<U*>(src);

			TORRENT_ASSERT((reinterpret_cast<std::uintptr_t>(dst) & (alignof(U) - 1)) == 0);

			new (dst) U(std::move(rhs));
			rhs.~U();
		}

		std::unique_ptr<char, aux::free_deleter> m_storage;
		// number of bytes of storage allocated
		int m_capacity = 0;
		// the number of bytes used under m_storage
		int m_size = 0;
		// the number of objects allocated in m_storage
		int m_num_items = 0;
	};
}

#endif
