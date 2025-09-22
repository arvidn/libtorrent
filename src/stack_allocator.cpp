/*

Copyright (c) 2017-2019, Arvid Norberg
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

#include "libtorrent/stack_allocator.hpp"
#include <cstdarg> // for va_list, va_copy, va_end

namespace libtorrent {
namespace aux {

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
}

