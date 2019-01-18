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
#define TORRENT_STACK_ALLOCATOR

#include "libtorrent/assert.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

#include <cstdarg> // for va_list
#include <cstdio> // for vsnprintf
#include <cstring>

namespace libtorrent { namespace aux {

	struct allocation_slot
	{
		allocation_slot() noexcept : m_idx(-1) {}
		allocation_slot(allocation_slot const&) noexcept = default;
		allocation_slot(allocation_slot&&) noexcept = default;
		allocation_slot& operator=(allocation_slot const&) = default;
		allocation_slot& operator=(allocation_slot&&) noexcept = default;
		bool operator==(allocation_slot const& s) const { return m_idx == s.m_idx; }
		bool operator!=(allocation_slot const& s) const { return m_idx != s.m_idx; }
		friend struct stack_allocator;
	private:
		explicit allocation_slot(int idx) noexcept : m_idx(idx) {}
		int val() const { return m_idx; }
		int m_idx;
	};

	struct TORRENT_EXTRA_EXPORT stack_allocator
	{
		stack_allocator() {}

		// non-copyable
		stack_allocator(stack_allocator const&) = delete;
		stack_allocator& operator=(stack_allocator const&) = delete;
		stack_allocator(stack_allocator&&) = default;
		stack_allocator& operator=(stack_allocator&&) = default;

		allocation_slot copy_string(string_view str);
		allocation_slot copy_string(char const* str);

		allocation_slot format_string(char const* fmt, va_list v);

		allocation_slot copy_buffer(span<char const> buf);
		allocation_slot allocate(int bytes);
		char* ptr(allocation_slot idx);
		char const* ptr(allocation_slot idx) const;
		void swap(stack_allocator& rhs);
		void reset();

	private:

		vector<char> m_storage;
	};

} }

#endif
