/*

Copyright (c) 2003-2018, Arvid Norberg
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

#ifndef TORRENT_NO_DEPRECATE

#include <utility>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/bdecode.hpp" // for error codes

namespace libtorrent
{
	struct lazy_entry;

	// This function decodes bencoded_ data.
	// 
	// .. _bencoded: http://wiki.theory.org/index.php/BitTorrentSpecification
	// 
	// The lazy bdecoder and lazy_entry has been deprecated in favour of
	// bdecode_node and its corresponding bdecode() function.
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
	TORRENT_DEPRECATED_EXPORT int lazy_bdecode(char const* start, char const* end
		, lazy_entry& ret, error_code& ec, int* error_pos = 0
		, int depth_limit = 1000, int item_limit = 1000000);

	// for backwards compatibility, does not report error code
	// deprecated in 0.16
	TORRENT_DEPRECATED_EXPORT int lazy_bdecode(char const* start, char const* end
		, lazy_entry& ret, int depth_limit = 1000, int item_limit = 1000000);

	// this is a string that is not NULL-terminated. Instead it
	// comes with a length, specified in bytes. This is particularly
	// useful when parsing bencoded structures, because strings are
	// not NULL-terminated internally, and requiring NULL termination
	// would require copying the string.
	//
	// see lazy_entry::string_pstr().
	struct TORRENT_DEPRECATED_EXPORT pascal_string
	{
		// construct a string pointing to the characters at ``p``
		// of length ``l`` characters. No NULL termination is required.
		pascal_string(char const* p, int l): len(l), ptr(p) {}
		
		// the number of characters in the string.
		int len;

		// the pointer to the first character in the string. This is
		// not NULL terminated, but instead consult the ``len`` field
		// to know how many characters follow.
		char const* ptr;

		// lexicographical comparison of strings. Order is consisten
		// with memcmp.
		bool operator<(pascal_string const& rhs) const
		{
			return std::memcmp(ptr, rhs.ptr, (std::min)(len, rhs.len)) < 0
				|| len < rhs.len;
		}
	};

	struct lazy_dict_entry;

	// this object represent a node in a bencoded structure. It is a variant
	// type whose concrete type is one of:
	//
	// 1. dictionary (maps strings -> lazy_entry)
	// 2. list (sequence of lazy_entry, i.e. heterogenous)
	// 3. integer
	// 4. string
	//
	// There is also a ``none`` type, which is used for uninitialized
	// lazy_entries.
	struct TORRENT_DEPRECATED_EXPORT lazy_entry
	{
		// The different types a lazy_entry can have
		enum entry_type_t
		{
			none_t, dict_t, list_t, string_t, int_t
		};

		// internal
		lazy_entry() : m_begin(0), m_len(0), m_size(0), m_type(none_t)
		{ m_data.start = NULL; }

		// tells you which specific type this lazy entry has.
		// See entry_type_t. The type determines which subset of
		// member functions are valid to use.
		entry_type_t type() const { return entry_type_t(m_type); }

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

		// requires the type to be an integer. return the integer value
		boost::int64_t int_value() const;

		// internal
		void construct_string(char const* start, int length);

		// the string is not null-terminated!
		// use string_length() to determine how many bytes
		// are part of the string.
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

		// if this is a string, returns a pascal_string
		// representing the string value.
		pascal_string string_pstr() const
		{
			TORRENT_ASSERT(m_type == string_t);
			return pascal_string(m_data.start, m_size);
		}

		// if this is a string, returns the string as a std::string.
		// (which requires a copy)
		std::string string_value() const
		{
			TORRENT_ASSERT(m_type == string_t);
			return std::string(m_data.start, m_size);
		}

		// if the lazy_entry is a string, returns the
		// length of the string, in bytes.
		int string_length() const
		{ return m_size; }

		// internal
		void construct_dict(char const* begin)
		{
			TORRENT_ASSERT(m_type == none_t);
			m_type = dict_t;
			m_size = 0;
			m_begin = begin;
		}

		// internal
		lazy_entry* dict_append(char const* name);
		// internal
		void pop();

		// if this is a dictionary, look for a key ``name``, and return
		// a pointer to its value, or NULL if there is none.
		lazy_entry* dict_find(char const* name);
		lazy_entry const* dict_find(char const* name) const
		{ return const_cast<lazy_entry*>(this)->dict_find(name); }
		lazy_entry* dict_find(std::string const& name);
		lazy_entry const* dict_find(std::string const& name) const
		{ return const_cast<lazy_entry*>(this)->dict_find(name); }
		lazy_entry const* dict_find_string(char const* name) const;

		// if this is a dictionary, look for a key ``name`` whose value
		// is a string. If such key exist, return a pointer to
		// its value, otherwise NULL.
		std::string dict_find_string_value(char const* name) const;
		pascal_string dict_find_pstr(char const* name) const;

		// if this is a dictionary, look for a key ``name`` whose value
		// is an int. If such key exist, return a pointer to its value,
		// otherwise NULL.
		boost::int64_t dict_find_int_value(char const* name
			, boost::int64_t default_val = 0) const;
		lazy_entry const* dict_find_int(char const* name) const;

		// these functions require that ``this`` is a dictionary.
		// (this->type() == dict_t). They look for an element with the
		// specified name in the dictionary. ``dict_find_dict`` only
		// finds dictionaries and ``dict_find_list`` only finds lists.
		// if no key with the corresponding value of the right type is
		// found, NULL is returned.
		lazy_entry const* dict_find_dict(char const* name) const;
		lazy_entry const* dict_find_dict(std::string const& name) const;
		lazy_entry const* dict_find_list(char const* name) const;

		// if this is a dictionary, return the key value pair at
		// position ``i`` from the dictionary.
		std::pair<std::string, lazy_entry const*> dict_at(int i) const;

		// requires that ``this`` is a dictionary. return the
		// number of items in it
		int dict_size() const
		{
			TORRENT_ASSERT(m_type == dict_t);
			return m_size;
		}

		// internal
		void construct_list(char const* begin)
		{
			TORRENT_ASSERT(m_type == none_t);
			m_type = list_t;
			m_size = 0;
			m_begin = begin;
		}

		// internal
		lazy_entry* list_append();

		// requires that ``this`` is a list. return
		// the item at index ``i``.
		lazy_entry* list_at(int i)
		{
			TORRENT_ASSERT(m_type == list_t);
			TORRENT_ASSERT(i < int(m_size));
			return &m_data.list[i+1];
		}
		lazy_entry const* list_at(int i) const
		{ return const_cast<lazy_entry*>(this)->list_at(i); }

		// these functions require ``this`` to have the type list.
		// (this->type() == list_t). ``list_string_value_at`` returns
		// the string at index ``i``. ``list_pstr_at``
		// returns a pascal_string of the string value at index ``i``.
		// if the element at ``i`` is not a string, an empty string
		// is returned.
		std::string list_string_value_at(int i) const;
		pascal_string list_pstr_at(int i) const;

		// this function require ``this`` to have the type list.
		// (this->type() == list_t). returns the integer value at
		// index ``i``. If the element at ``i`` is not an integer
		// ``default_val`` is returned, which defaults to 0.
		boost::int64_t list_int_value_at(int i, boost::int64_t default_val = 0) const;

		// if this is a list, return the number of items in it.
		int list_size() const
		{
			TORRENT_ASSERT(m_type == list_t);
			return int(m_size);
		}

		// internal: end points one byte passed last byte in the source
		// buffer backing the bencoded structure.
		void set_end(char const* end)
		{
			TORRENT_ASSERT(end > m_begin);
			TORRENT_ASSERT(end - m_begin < INT_MAX);
			m_len = int(end - m_begin);
		}
		
		// internal
		void clear();

		// internal: releases ownership of any memory allocated
		void release()
		{
			m_data.start = NULL;
			m_size = 0;
			m_type = none_t;
		}

		// internal
		~lazy_entry()
		{ clear(); }

		// returns pointers into the source buffer where
		// this entry has its bencoded data
		std::pair<char const*, int> data_section() const;

		// swap values of ``this`` and ``e``.
		void swap(lazy_entry& e)
		{
			using std::swap;
			boost::uint32_t tmp = e.m_type;
			e.m_type = m_type;
			m_type = tmp;
			tmp = e.m_size;
			e.m_size = m_size;
			m_size = tmp;
			swap(m_data.start, e.m_data.start);
			swap(m_begin, e.m_begin);
			swap(m_len, e.m_len);
		}

	private:

		int capacity() const;

		union data_t
		{
			// for the dict and list arrays, the first item is not part
			// of the array. Instead its m_len member indicates the capacity
			// of the allocation
			lazy_dict_entry* dict;
			lazy_entry* list;
			char const* start;
		} m_data;

		// used for dictionaries and lists to record the range
		// in the original buffer they are based on
		char const* m_begin;

		// the number of bytes this entry extends in the
		// bencoded buffer
		boost::uint32_t m_len;

		// if list or dictionary, the number of items
		boost::uint32_t m_size:29;
		// element type (dict, list, int, string)
		boost::uint32_t m_type:3;

		// non-copyable
		lazy_entry(lazy_entry const&);
		lazy_entry const& operator=(lazy_entry const&);
	};

	struct TORRENT_DEPRECATED lazy_dict_entry
	{
		char const* name;
		lazy_entry val;
	};

	// print the bencoded structure in a human-readable format to a string
	// that's returned.
	TORRENT_DEPRECATED_EXPORT std::string print_entry(lazy_entry const& e
		, bool single_line = false, int indent = 0);

	// defined in bdecode.cpp
	TORRENT_DEPRECATED
	TORRENT_EXTRA_EXPORT char const* parse_int(char const* start
		, char const* end, char delimiter, boost::int64_t& val
		, bdecode_errors::error_code_enum& ec);

}

#endif // TORRENT_NO_DEPRECATE

#endif

