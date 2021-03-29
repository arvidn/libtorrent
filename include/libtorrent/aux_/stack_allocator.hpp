/*

Copyright (c) 2012, 2015-2020, Arvid Norberg
Copyright (c) 2016-2017, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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

namespace lt::aux {

	struct allocation_slot
	{
		allocation_slot() noexcept : m_idx(-1) {}
		allocation_slot(allocation_slot const&) noexcept = default;
		allocation_slot(allocation_slot&&) noexcept = default;
		allocation_slot& operator=(allocation_slot const&) & = default;
		allocation_slot& operator=(allocation_slot&&) & noexcept = default;
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
		stack_allocator& operator=(stack_allocator&&) & = default;

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

}

#endif
