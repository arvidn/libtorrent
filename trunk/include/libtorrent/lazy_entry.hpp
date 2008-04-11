/*

Copyright (c) 2003, Arvid Norberg
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
#include "libtorrent/assert.hpp"
#include <boost/cstdint.hpp>

namespace libtorrent
{
	struct lazy_entry;

	char* parse_int(char* start, char* end, char delimiter, boost::int64_t& val);
	// return 0 = success
	int lazy_bdecode(char* start, char* end, lazy_entry& ret, int depth_limit = 1000);

	struct lazy_entry
	{
		enum entry_type_t
		{
			none_t, dict_t, list_t, string_t, int_t
		};

		lazy_entry() : m_type(none_t) { m_data.start = 0; }

		entry_type_t type() const { return m_type; }

		// start is a null terminated string (decimal number)
		void construct_int(char* start)
		{
			TORRENT_ASSERT(m_type == none_t);
			m_type = int_t;
			m_data.start = start;
		}

		boost::int64_t int_value() const;

		// string functions
		// ================

		// start is a null terminated string
		void construct_string(char* start)
		{
			TORRENT_ASSERT(m_type == none_t);
			m_type = string_t;
			m_data.start = start;
		}

		char const* string_value() const
		{
			TORRENT_ASSERT(m_type == string_t);
			return m_data.start;
		}

		// dictionary functions
		// ====================

		void construct_dict()
		{
			TORRENT_ASSERT(m_type == none_t);
			m_type = dict_t;
			m_size = 0;
			m_capacity = 0;
		}

		lazy_entry* dict_append(char* name);
		lazy_entry* dict_find(char const* name);
		lazy_entry const* dict_find(char const* name) const
		{ return const_cast<lazy_entry*>(this)->dict_find(name); }

		int dict_size() const
		{
			TORRENT_ASSERT(m_type == dict_t);
			return m_size;
		}

		// list functions
		// ==============

		void construct_list()
		{
			TORRENT_ASSERT(m_type == none_t);
			m_type = list_t;
			m_size = 0;
			m_capacity = 0;
		}

		lazy_entry* list_append();
		lazy_entry* list_at(int i)
		{
			TORRENT_ASSERT(m_type == list_t);
			TORRENT_ASSERT(i < m_size);
			return &m_data.list[i];
		}

		int list_size() const
		{
			TORRENT_ASSERT(m_type == list_t);
			return m_size;
		}

		void clear();

		~lazy_entry()
		{ clear(); }

	private:

		entry_type_t m_type;
		union data_t
		{
			std::pair<char*, lazy_entry>* dict;
			lazy_entry* list;
			char* start;
		} m_data;
		int m_size; // if list or dictionary, the number of items
		int m_capacity; // if list or dictionary, allocated number of items
	};

};


#endif

