/*

Copyright (c) 2017-2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/stack_allocator.hpp"
#include <cstdarg> // for va_list, va_copy, va_end

namespace lt::aux {

	allocation_slot stack_allocator::copy_string(string_view str)
	{
		int const ret = int(m_storage.size());
		m_storage.resize(ret + numeric_cast<int>(str.size()) + 1);
		std::memcpy(&m_storage[ret], str.data(), str.size());
		m_storage[ret + int(str.length())] = '\0';
		return allocation_slot(ret);
	}

	allocation_slot stack_allocator::copy_string(char const* str)
	{
		int const ret = int(m_storage.size());
		int const len = int(std::strlen(str));
		m_storage.resize(ret + len + 1);
		std::memcpy(&m_storage[ret], str, numeric_cast<std::size_t>(len));
		m_storage[ret + len] = '\0';
		return allocation_slot(ret);
	}

	allocation_slot stack_allocator::format_string(char const* fmt, va_list v)
	{
		int const pos = int(m_storage.size());
		int len = 512;

		for(;;)
		{
			m_storage.resize(pos + len + 1);

			va_list args;
			va_copy(args, v);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
			int const ret = std::vsnprintf(m_storage.data() + pos, static_cast<std::size_t>(len) + 1, fmt, args);
#ifdef __clang__
#pragma clang diagnostic pop
#endif

			va_end(args);

			if (ret < 0)
			{
				m_storage.resize(pos);
				return copy_string("(format error)");
			}
			if (ret > len)
			{
				// try again
				len = ret;
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
		int const ret = int(m_storage.size());
		int const size = int(buf.size());
		if (size < 1) return {};
		m_storage.resize(ret + size);
		std::memcpy(&m_storage[ret], buf.data(), numeric_cast<std::size_t>(size));
		return allocation_slot(ret);
	}

	allocation_slot stack_allocator::allocate(int const bytes)
	{
		if (bytes < 1) return {};
		int const ret = m_storage.end_index();
		m_storage.resize(ret + bytes);
		return allocation_slot(ret);
	}

	char* stack_allocator::ptr(allocation_slot const idx)
	{
		if(idx.val() < 0) return nullptr;
		TORRENT_ASSERT(idx.val() < int(m_storage.size()));
		return &m_storage[idx.val()];
	}

	char const* stack_allocator::ptr(allocation_slot const idx) const
	{
		if(idx.val() < 0) return nullptr;
		TORRENT_ASSERT(idx.val() < int(m_storage.size()));
		return &m_storage[idx.val()];
	}

	void stack_allocator::swap(stack_allocator& rhs)
	{
		m_storage.swap(rhs.m_storage);
	}

	void stack_allocator::reset()
	{
		m_storage.clear();
	}
}
