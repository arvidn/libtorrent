/*

Copyright (c) 2012, 2015-2021, Arvid Norberg
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
#include "libtorrent/aux_/numeric_cast.hpp"

#include <cstdarg> // for va_list
#include <cstdio> // for vsnprintf
#include <cstring>
#include <vector>

namespace libtorrent::aux {

	struct allocation_slot
	{
		allocation_slot() noexcept : m_idx(-1) {}
		allocation_slot(allocation_slot const&) noexcept = default;
		allocation_slot(allocation_slot&&) noexcept = default;
		allocation_slot& operator=(allocation_slot const&) & = default;
		allocation_slot& operator=(allocation_slot&&) & noexcept = default;
		bool operator==(allocation_slot const& s) const { return m_idx == s.m_idx; }
		bool operator!=(allocation_slot const& s) const { return m_idx != s.m_idx; }
		bool is_valid() const { return m_idx >= 0; }
		friend struct stack_allocator;
	private:
		explicit allocation_slot(std::size_t const idx) noexcept : m_idx(numeric_cast<std::int32_t>(idx)) {}
		std::size_t val() const { return static_cast<std::size_t>(m_idx); }
		std::int32_t m_idx;
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
		void reset(std::uint32_t generation);
		std::uint32_t gen() const { return m_generation; }

	private:

		std::vector<char> m_storage;
		std::uint32_t m_generation = 0;
	};

	struct cached_slot
	{
		template <typename F>
		allocation_slot copy_string(stack_allocator& a, F fun)
		{
			if (m_generation != a.gen() || !m_idx.is_valid())
			{
				m_idx = a.copy_string(fun());
				m_generation = a.gen();
			}
			return m_idx;
		}

		void clear() { m_idx = aux::allocation_slot(); }

	private:

		allocation_slot m_idx;
		std::uint32_t m_generation = 0;
	};

}

#endif
