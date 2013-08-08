/*

Copyright (c) 2003-2012, Arvid Norberg
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

#ifndef TORRENT_LAZY_ENTRY_HPP_INCLUDED
#define TORRENT_LAZY_ENTRY_HPP_INCLUDED

#include <utility>
#include <vector>
#include <string>
#include <cstring>
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/size_type.hpp"
#include "libtorrent/error_code.hpp"

#if TORRENT_USE_IOSTREAM
#include <iosfwd>
#endif

namespace libtorrent
{
	struct lazy_entry;

	// This function decodes bencoded_ data.
	// 
	// .. _bencoded: http://wiki.theory.org/index.php/BitTorrentSpecification
	// 
	// Whenever possible, ``lazy_bdecode()`` should be preferred over ``bdecode()``.
	// It is more efficient and more secure. It supports having constraints on the
	// amount of memory is consumed by the parser.
	// 
	// *lazy* refers to the fact that it doesn't copy any actual data out of the
	// bencoded buffer. It builds a tree of ``lazy_entry`` which has pointers into
	// the bencoded buffer. This makes it very fast and efficient. On top of that,
	// it is not recursive, which saves a lot of stack space when parsing deeply
	// nested trees. However, in order to protect against potential attacks, the
	// ``depth_limit`` and ``item_limit`` control how many levels deep the tree is
	// allowed to get. With recursive parser, a few thousand levels would be enough
	// to exhaust the threads stack and terminate the process. The ``item_limit``
	// protects against very large structures, not necessarily deep. Each bencoded
	// item in the structure causes the parser to allocate some amount of memory,
	// this memory is constant regardless of how much data actually is stored in
	// the item. One potential attack is to create a bencoded list of hundreds of
	// thousands empty strings, which would cause the parser to allocate a significant
	// amount of memory, perhaps more than is available on the machine, and effectively
	// provide a denial of service. The default item limit is set as a reasonable
	// upper limit for desktop computers. Very few torrents have more items in them.
	// The limit corresponds to about 25 MB, which might be a bit much for embedded
	// systems.
	// 
	// ``start`` and ``end`` defines the bencoded buffer to be decoded. ``ret`` is
	// the ``lazy_entry`` which is filled in with the whole decoded tree. ``ec``
	// is a reference to an ``error_code`` which is set to describe the error encountered
	// in case the function fails. ``error_pos`` is an optional pointer to an int,
	// which will be set to the byte offset into the buffer where an error occurred,
	// in case the function fails.
	TORRENT_EXPORT int lazy_bdecode(char const* start, char const* end
		, lazy_entry& ret, error_code& ec, int* error_pos = 0
		, int depth_limit = 1000, int item_limit = 1000000);

#ifndef TORRENT_NO_DEPRECATE
	// for backwards compatibility, does not report error code
	// deprecated in 0.16
	TORRENT_DEPRECATED_PREFIX
	TORRENT_EXPORT int lazy_bdecode(char const* start, char const* end
		, lazy_entry& ret, int depth_limit = 1000, int item_limit = 1000000) TORRENT_DEPRECATED;
#endif

	struct TORRENT_EXPORT pascal_string
	{
		pascal_string(char const* p, int l): len(l), ptr(p) {}
		int len;
		char const* ptr;
		bool operator<(pascal_string const& rhs) const
		{
			return std::memcmp(ptr, rhs.ptr, (std::min)(len, rhs.len)) < 0
				|| len < rhs.len;
		}
	};

	struct lazy_dict_entry;

	struct TORRENT_EXPORT lazy_entry
	{
		enum entry_type_t
		{
			none_t, dict_t, list_t, string_t, int_t
		};

		lazy_entry() : m_begin(0), m_len(0), m_size(0), m_capacity(0), m_type(none_t)
		{ m_data.start = 0; }

		entry_type_t type() const { return (entry_type_t)m_type; }

		// start points to the first decimal digit
		// length is the number of digits
		void construct_int(char const* start, int length)
		{
			TORRENT_ASSERT(m_type == none_t);
			m_type = int_t;
			m_data.start = start;
			m_size = length;
			m_begin = start - 1; // include 'i'
			m_len = length + 2; // include 'e'
		}

		size_type int_value() const;

		// string functions
		// ================

		void construct_string(char const* start, int length);

		// the string is not null-terminated!
		char const* string_ptr() const
		{
			TORRENT_ASSERT(m_type == string_t);
			return m_data.start;
		}

		// this will return a null terminated string
		// it will write to the source buffer!
		char const* string_cstr() const
		{
			TORRENT_ASSERT(m_type == string_t);
			const_cast<char*>(m_data.start)[m_size] = 0;
			return m_data.start;
		}

		pascal_string string_pstr() const
		{
			TORRENT_ASSERT(m_type == string_t);
			return pascal_string(m_data.start, m_size);
		}

		std::string string_value() const
		{
			TORRENT_ASSERT(m_type == string_t);
			return std::string(m_data.start, m_size);
		}

		int string_length() const
		{ return m_size; }

		// dictionary functions
		// ====================

		void construct_dict(char const* begin)
		{
			TORRENT_ASSERT(m_type == none_t);
			m_type = dict_t;
			m_size = 0;
			m_capacity = 0;
			m_begin = begin;
		}

		lazy_entry* dict_append(char const* name);
		void pop();
		lazy_entry* dict_find(char const* name);
		lazy_entry const* dict_find(char const* name) const
		{ return const_cast<lazy_entry*>(this)->dict_find(name); }

		std::string dict_find_string_value(char const* name) const;
		pascal_string dict_find_pstr(char const* name) const;
		size_type dict_find_int_value(char const* name, size_type default_val = 0) const;
		lazy_entry const* dict_find_dict(char const* name) const;
		lazy_entry const* dict_find_list(char const* name) const;
		lazy_entry const* dict_find_string(char const* name) const;
		lazy_entry const* dict_find_int(char const* name) const;

		std::pair<std::string, lazy_entry const*> dict_at(int i) const;

		int dict_size() const
		{
			TORRENT_ASSERT(m_type == dict_t);
			return m_size;
		}

		// list functions
		// ==============

		void construct_list(char const* begin)
		{
			TORRENT_ASSERT(m_type == none_t);
			m_type = list_t;
			m_size = 0;
			m_capacity = 0;
			m_begin = begin;
		}

		lazy_entry* list_append();
		lazy_entry* list_at(int i)
		{
			TORRENT_ASSERT(m_type == list_t);
			TORRENT_ASSERT(i < int(m_size));
			return &m_data.list[i];
		}
		lazy_entry const* list_at(int i) const
		{ return const_cast<lazy_entry*>(this)->list_at(i); }

		std::string list_string_value_at(int i) const;
		pascal_string list_pstr_at(int i) const;
		size_type list_int_value_at(int i, size_type default_val = 0) const;

		int list_size() const
		{
			TORRENT_ASSERT(m_type == list_t);
			return int(m_size);
		}

		// end points one byte passed last byte
		void set_end(char const* end)
		{
			TORRENT_ASSERT(end > m_begin);
			m_len = end - m_begin;
		}
		
		void clear();

		// releases ownership of any memory allocated
		void release()
		{
			m_data.start = 0;
			m_size = 0;
			m_capacity = 0;
			m_type = none_t;
		}

		~lazy_entry()
		{ clear(); }

		// returns pointers into the source buffer where
		// this entry has its bencoded data
		std::pair<char const*, int> data_section() const;

		void swap(lazy_entry& e)
		{
			using std::swap;
			boost::uint32_t tmp = e.m_type;
			e.m_type = m_type;
			m_type = tmp;
			tmp = e.m_capacity;
			e.m_capacity = m_capacity;
			m_capacity = tmp;
			swap(m_data.start, e.m_data.start);
			swap(m_size, e.m_size);
			swap(m_begin, e.m_begin);
			swap(m_len, e.m_len);
		}

	private:

		union data_t
		{
			lazy_dict_entry* dict;
			lazy_entry* list;
			char const* start;
		} m_data;

		// used for dictionaries and lists to record the range
		// in the original buffer they are based on
		char const* m_begin;
		// the number of bytes this entry extends in the
		// bencoded byffer
		boost::uint32_t m_len;

		// if list or dictionary, the number of items
		boost::uint32_t m_size;
		// if list or dictionary, allocated number of items
		boost::uint32_t m_capacity:29;
		// element type (dict, list, int, string)
		boost::uint32_t m_type:3;

		// non-copyable
		lazy_entry(lazy_entry const&);
		lazy_entry const& operator=(lazy_entry const&);
	};

	struct lazy_dict_entry
	{
		char const* name;
		lazy_entry val;
	};

	TORRENT_EXTRA_EXPORT std::string print_entry(lazy_entry const& e
		, bool single_line = false, int indent = 0);
}


#endif

