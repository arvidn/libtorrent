/*

Copyright (c) 2015-2016, Arvid Norberg
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
#define TORRENT_STACK_ALLOCATOR

#include "libtorrent/assert.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

#include <cstdio> // for vsnprintf
#include <cstring>

namespace lt {
LIBTORRENT_VERSION_NAMESPACE {
namespace aux {
	struct allocation_slot
	{
		allocation_slot() : m_idx(-1) {}
		allocation_slot(allocation_slot const&) = default;
		allocation_slot(allocation_slot&&) = default;
		allocation_slot& operator=(allocation_slot const&) = default;
		allocation_slot& operator=(allocation_slot&&) = default;
		bool operator==(allocation_slot const& s) const { return m_idx == s.m_idx; }
		bool operator!=(allocation_slot const& s) const { return m_idx != s.m_idx; }
		friend struct stack_allocator;
	private:
		explicit allocation_slot(int idx) : m_idx(idx) {}
		int val() const { return m_idx; }
		int m_idx;
	};

	struct stack_allocator
	{
		stack_allocator() {}

		// non-copyable
		stack_allocator(stack_allocator const&) = delete;
		stack_allocator& operator=(stack_allocator const&) = delete;

		allocation_slot copy_string(string_view str)
		{
			int const ret = int(m_storage.size());
			m_storage.resize(ret + numeric_cast<int>(str.size()) + 1);
			std::memcpy(&m_storage[ret], str.data(), str.size());
			m_storage[ret + int(str.length())] = '\0';
			return allocation_slot(ret);
		}

		allocation_slot copy_string(char const* str)
		{
			int const ret = int(m_storage.size());
			int const len = int(std::strlen(str));
			m_storage.resize(ret + len + 1);
			std::memcpy(&m_storage[ret], str, numeric_cast<std::size_t>(len));
			m_storage[ret + len] = '\0';
			return allocation_slot(ret);
		}

		allocation_slot format_string(char const* fmt, va_list v)
		{
			int const ret = int(m_storage.size());
			m_storage.resize(ret + 512);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
			int const len = std::vsnprintf(m_storage.data() + ret, 512, fmt, v);
#ifdef __clang__
#pragma clang diagnostic pop
#endif

			if (len < 0)
			{
				m_storage.resize(ret);
				return copy_string("(format error)");
			}

			// +1 is to include the 0-terminator
			m_storage.resize(ret + (len > 512 ? 512 : len) + 1);
			return allocation_slot(ret);
		}

		allocation_slot copy_buffer(span<char const> buf)
		{
			int const ret = int(m_storage.size());
			int const size = int(buf.size());
			if (size < 1) return allocation_slot();
			m_storage.resize(ret + size);
			std::memcpy(&m_storage[ret], buf.data(), numeric_cast<std::size_t>(size));
			return allocation_slot(ret);
		}

		allocation_slot allocate(int const bytes)
		{
			if (bytes < 1) return allocation_slot();
			int const ret = m_storage.end_index();
			m_storage.resize(ret + bytes);
			return allocation_slot(ret);
		}

		char* ptr(allocation_slot const idx)
		{
			if(idx.val() < 0) return nullptr;
			TORRENT_ASSERT(idx.val() < int(m_storage.size()));
			return &m_storage[idx.val()];
		}

		char const* ptr(allocation_slot const idx) const
		{
			if(idx.val() < 0) return nullptr;
			TORRENT_ASSERT(idx.val() < int(m_storage.size()));
			return &m_storage[idx.val()];
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

		vector<char> m_storage;
	};

}}}

#endif
