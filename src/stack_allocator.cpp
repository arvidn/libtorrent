/*

Copyright (c) 2017-2021, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/stack_allocator.hpp"
#include <cstdarg> // for va_list, va_copy, va_end

namespace libtorrent::aux {

	allocation_slot stack_allocator::copy_string(string_view str)
	{
		auto const ret = m_storage.size();
		if (std::numeric_limits<int>::max() - str.size() <= ret)
			return {};
		m_storage.resize(ret + str.size() + 1);
		std::memcpy(&m_storage[ret], str.data(), str.size());
		m_storage[ret + str.length()] = '\0';
		return allocation_slot(ret);
	}

	allocation_slot stack_allocator::copy_string(char const* str)
	{
		auto const ret = m_storage.size();
		auto const len = std::strlen(str);
		if (std::numeric_limits<int>::max() - len <= ret)
			return {};
		m_storage.resize(ret + len + 1);
		std::memcpy(&m_storage[ret], str, len);
		m_storage[ret + len] = '\0';
		return allocation_slot(ret);
	}

	allocation_slot stack_allocator::format_string(char const* fmt, va_list v)
	{
		auto const pos = m_storage.size();
		std::size_t len = 512;

		for(;;)
		{
			if (std::numeric_limits<int>::max() - len <= pos)
				return {};

			m_storage.resize(pos + len + 1);

			va_list args;
			va_copy(args, v);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
			int const ret = std::vsnprintf(m_storage.data() + pos, len + 1, fmt, args);
#ifdef __clang__
#pragma clang diagnostic pop
#endif

			va_end(args);

			if (ret < 0)
			{
				m_storage.resize(pos);
				return copy_string("(format error)");
			}
			if (static_cast<std::size_t>(ret) > len)
			{
				// try again
				len = numeric_cast<std::size_t>(ret);
				continue;
			}
			break;
		}

		// +1 is to include the 0-terminator
		m_storage.resize(pos + len + 1);
		return allocation_slot(pos);
	}

	allocation_slot stack_allocator::copy_buffer(span<char const> buf)
	{
		auto const ret = m_storage.size();
		auto const size = numeric_cast<std::size_t>(buf.size());
		if (size < 1) return {};

		if (std::numeric_limits<int>::max() - size <= ret)
			return {};
		m_storage.resize(ret + size);
		std::memcpy(&m_storage[ret], buf.data(), numeric_cast<std::size_t>(size));
		return allocation_slot(ret);
	}

	allocation_slot stack_allocator::allocate(int const bytes)
	{
		if (bytes < 1) return {};
		auto const ret = m_storage.size();
		if (std::numeric_limits<int>::max() - static_cast<std::size_t>(bytes) <= ret)
			return {};
		m_storage.resize(ret + numeric_cast<std::size_t>(bytes));
		return allocation_slot(ret);
	}

	char* stack_allocator::ptr(allocation_slot const idx)
	{
		static char tmp = 0;
		if(!idx.is_valid()) return &tmp;
		TORRENT_ASSERT(idx.val() < m_storage.size());
		return &m_storage[idx.val()];
	}

	char const* stack_allocator::ptr(allocation_slot const idx) const
	{
		if(!idx.is_valid()) return "";
		TORRENT_ASSERT(idx.val() < m_storage.size());
		return &m_storage[idx.val()];
	}

	void stack_allocator::swap(stack_allocator& rhs)
	{
		m_storage.swap(rhs.m_storage);
		std::swap(m_generation, rhs.m_generation);
	}

	void stack_allocator::reset(std::uint32_t const generation)
	{
		m_storage.clear();
		m_generation = generation;
	}
}
