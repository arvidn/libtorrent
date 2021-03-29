/*

Copyright (c) 2017, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_STRING_PTR_HPP_INCLUDED
#define TORRENT_STRING_PTR_HPP_INCLUDED

#include "libtorrent/string_view.hpp"

namespace lt::aux {

	struct string_ptr
	{
		explicit string_ptr(string_view str) : m_ptr(new char[str.size() + 1])
		{
			std::copy(str.begin(), str.end(), m_ptr);
			m_ptr[str.size()] = '\0';
		}
		~string_ptr()
		{
			delete[] m_ptr;
		}
		string_ptr(string_ptr&& rhs)
			: m_ptr(rhs.m_ptr)
		{
			rhs.m_ptr = nullptr;
		}
		string_ptr& operator=(string_ptr&& rhs)
		{
			if (&rhs == this) return *this;
			delete[] m_ptr;
			m_ptr = rhs.m_ptr;
			rhs.m_ptr = nullptr;
			return *this;
		}
		string_ptr(string_ptr const& rhs) = delete;
		string_ptr& operator=(string_ptr const& rhs) = delete;
		char const* operator*() const { return m_ptr; }
	private:
		char* m_ptr;
	};

}

#endif

