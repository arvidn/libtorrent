/*

Copyright (c) 2008, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include "libtorrent/lazy_entry.hpp"
#include "libtorrent/escape_string.hpp"
#include <cstring>

#if TORRENT_USE_IOSTREAM
#include <iostream>
#endif

namespace
{
	const int lazy_entry_grow_factor = 150; // percent
	const int lazy_entry_dict_init = 5;
	const int lazy_entry_list_init = 5;
}

namespace libtorrent
{

#define TORRENT_FAIL_BDECODE(code) \
	{ \
		ec = code; \
		while (!stack.empty()) { \
			top = stack.back(); \
			if (top->type() == lazy_entry::dict_t || top->type() == lazy_entry::list_t) top->pop(); \
			stack.pop_back(); \
		} \
		if (error_pos) *error_pos = start - orig_start; \
		return -1; \
	}
	// fills in 'val' with what the string between start and the
	// first occurance of the delimiter is interpreted as an int.
	// return the pointer to the delimiter, or 0 if there is a
	// parse error. val should be initialized to zero
	char const* parse_int(char const* start, char const* end, char delimiter, boost::int64_t& val)
	{
		while (start < end && *start != delimiter)
		{
			if (!is_digit(*start)) { return 0; }
			val *= 10;
			val += *start - '0';
			++start;
		}
		return start;
	}

	char const* find_char(char const* start, char const* end, char delimiter)
	{
		while (start < end && *start != delimiter) ++start;
		return start;
	}

#ifndef TORRENT_NO_DEPRECATE
	int lazy_bdecode(char const* start, char const* end
		, lazy_entry& ret, int depth_limit, int item_limit)
	{
		error_code ec;
		int pos;
		return lazy_bdecode(start, end, ret, ec, &pos, depth_limit, item_limit);
	}
#endif

	// return 0 = success
	int lazy_bdecode(char const* start, char const* end, lazy_entry& ret
		, error_code& ec, int* error_pos, int depth_limit, int item_limit)
	{
		char const* const orig_start = start;
		ret.clear();
		if (start == end) return 0;

		std::vector<lazy_entry*> stack;

		stack.push_back(&ret);
		while (start < end)
		{
			if (stack.empty()) break; // done!

			lazy_entry* top = stack.back();

			if (int(stack.size()) > depth_limit) TORRENT_FAIL_BDECODE(errors::depth_exceeded);
			if (start >= end) TORRENT_FAIL_BDECODE(errors::unexpected_eof);
			char t = *start;
			++start;
			if (start >= end && t != 'e') TORRENT_FAIL_BDECODE(errors::unexpected_eof);

			switch (top->type())
			{
				case lazy_entry::dict_t:
				{
					if (t == 'e')
					{
						top->set_end(start);
						stack.pop_back();
						continue;
					}
					if (!is_digit(t)) TORRENT_FAIL_BDECODE(errors::expected_string);
					boost::int64_t len = t - '0';
					start = parse_int(start, end, ':', len);
					if (start == 0 || start + len + 3 > end || *start != ':')
						TORRENT_FAIL_BDECODE(errors::expected_colon);
					++start;
					if (start == end) TORRENT_FAIL_BDECODE(errors::unexpected_eof);
					lazy_entry* ent = top->dict_append(start);
					if (ent == 0) TORRENT_FAIL_BDECODE(errors::no_memory);
					start += len;
					if (start >= end) TORRENT_FAIL_BDECODE(errors::unexpected_eof);
					stack.push_back(ent);
					t = *start;
					++start;
					break;
				}
				case lazy_entry::list_t:
				{
					if (t == 'e')
					{
						top->set_end(start);
						stack.pop_back();
						continue;
					}
					lazy_entry* ent = top->list_append();
					if (ent == 0) TORRENT_FAIL_BDECODE(errors::no_memory);
					stack.push_back(ent);
					break;
				}
				default: break;
			}

			--item_limit;
			if (item_limit <= 0) TORRENT_FAIL_BDECODE(errors::limit_exceeded);

			top = stack.back();
			switch (t)
			{
				case 'd':
					top->construct_dict(start - 1);
					continue;
				case 'l':
					top->construct_list(start - 1);
					continue;
				case 'i':
				{
					char const* int_start = start;
					start = find_char(start, end, 'e');
					top->construct_int(int_start, start - int_start);
					if (start == end) TORRENT_FAIL_BDECODE(errors::unexpected_eof);
					TORRENT_ASSERT(*start == 'e');
					++start;
					stack.pop_back();
					continue;
				}
				default:
				{
					if (!is_digit(t))
						TORRENT_FAIL_BDECODE(errors::expected_value);

					boost::int64_t len = t - '0';
					start = parse_int(start, end, ':', len);
					if (start == 0 || start + len + 1 > end || *start != ':')
						TORRENT_FAIL_BDECODE(errors::expected_colon);
					++start;
					top->construct_string(start, int(len));
					stack.pop_back();
					start += len;
					continue;
				}
			}
			return 0;
		}
		return 0;
	}

	size_type lazy_entry::int_value() const
	{
		TORRENT_ASSERT(m_type == int_t);
		boost::int64_t val = 0;
		bool negative = false;
		if (*m_data.start == '-') negative = true;
		parse_int(negative?m_data.start+1:m_data.start, m_data.start + m_size, 'e', val);
		if (negative) val = -val;
		return val;
	}

	lazy_entry* lazy_entry::dict_append(char const* name)
	{
		TORRENT_ASSERT(m_type == dict_t);
		TORRENT_ASSERT(m_size <= m_capacity);
		if (m_capacity == 0)
		{
			int capacity = lazy_entry_dict_init;
			m_data.dict = new (std::nothrow) lazy_dict_entry[capacity];
			if (m_data.dict == 0) return 0;
			m_capacity = capacity;
		}
		else if (m_size == m_capacity)
		{
			int capacity = m_capacity * lazy_entry_grow_factor / 100;
			lazy_dict_entry* tmp = new (std::nothrow) lazy_dict_entry[capacity];
			if (tmp == 0) return 0;
			std::memcpy(tmp, m_data.dict, sizeof(lazy_dict_entry) * m_size);
			for (int i = 0; i < int(m_size); ++i) m_data.dict[i].val.release();
			delete[] m_data.dict;
			m_data.dict = tmp;
			m_capacity = capacity;
		}

		TORRENT_ASSERT(m_size < m_capacity);
		lazy_dict_entry& ret = m_data.dict[m_size++];
		ret.name = name;
		return &ret.val;
	}

	void lazy_entry::pop()
	{
		if (m_size > 0) --m_size;
	}

	namespace
	{
		// the number of decimal digits needed
		// to represent the given value
		int num_digits(int val)
		{
			int ret = 1;
			while (val >= 10)
			{
				++ret;
				val /= 10;
			}
			return ret;
		}
	}

	void lazy_entry::construct_string(char const* start, int length)
	{
		TORRENT_ASSERT(m_type == none_t);
		m_type = string_t;
		m_data.start = start;
		m_size = length;
		m_begin = start - 1 - num_digits(length);
		m_len = start - m_begin + length;
	}

	namespace
	{
		// str1 is null-terminated
		// str2 is not, str2 is len2 chars
		bool string_equal(char const* str1, char const* str2, int len2)
		{
			while (len2 > 0)
			{
				if (*str1 != *str2) return false;
				if (*str1 == 0) return false;
				++str1;
				++str2;
				--len2;
			}
			return *str1 == 0;
		}
	}

	std::pair<std::string, lazy_entry const*> lazy_entry::dict_at(int i) const
	{
		TORRENT_ASSERT(m_type == dict_t);
		TORRENT_ASSERT(i < int(m_size));
		lazy_dict_entry const& e = m_data.dict[i];
		return std::make_pair(std::string(e.name, e.val.m_begin - e.name), &e.val);
	}

	std::string lazy_entry::dict_find_string_value(char const* name) const
	{
		lazy_entry const* e = dict_find(name);
		if (e == 0 || e->type() != lazy_entry::string_t) return std::string();
		return e->string_value();
	}

	pascal_string lazy_entry::dict_find_pstr(char const* name) const
	{
		lazy_entry const* e = dict_find(name);
		if (e == 0 || e->type() != lazy_entry::string_t) return pascal_string(0, 0);
		return e->string_pstr();
	}

	lazy_entry const* lazy_entry::dict_find_string(char const* name) const
	{
		lazy_entry const* e = dict_find(name);
		if (e == 0 || e->type() != lazy_entry::string_t) return 0;
		return e;
	}

	lazy_entry const* lazy_entry::dict_find_int(char const* name) const
	{
		lazy_entry const* e = dict_find(name);
		if (e == 0 || e->type() != lazy_entry::int_t) return 0;
		return e;
	}

	size_type lazy_entry::dict_find_int_value(char const* name, size_type default_val) const
	{
		lazy_entry const* e = dict_find(name);
		if (e == 0 || e->type() != lazy_entry::int_t) return default_val;
		return e->int_value();
	}

	lazy_entry const* lazy_entry::dict_find_dict(char const* name) const
	{
		lazy_entry const* e = dict_find(name);
		if (e == 0 || e->type() != lazy_entry::dict_t) return 0;
		return e;
	}

	lazy_entry const* lazy_entry::dict_find_list(char const* name) const
	{
		lazy_entry const* e = dict_find(name);
		if (e == 0 || e->type() != lazy_entry::list_t) return 0;
		return e;
	}

	lazy_entry* lazy_entry::dict_find(char const* name)
	{
		TORRENT_ASSERT(m_type == dict_t);
		for (int i = 0; i < int(m_size); ++i)
		{
			lazy_dict_entry& e = m_data.dict[i];
			if (string_equal(name, e.name, e.val.m_begin - e.name))
				return &e.val;
		}
		return 0;
	}

	lazy_entry* lazy_entry::list_append()
	{
		TORRENT_ASSERT(m_type == list_t);
		TORRENT_ASSERT(m_size <= m_capacity);
		if (m_capacity == 0)
		{
			int capacity = lazy_entry_list_init;
			m_data.list = new (std::nothrow) lazy_entry[capacity];
			if (m_data.list == 0) return 0;
			m_capacity = capacity;
		}
		else if (m_size == m_capacity)
		{
			int capacity = m_capacity * lazy_entry_grow_factor / 100;
			lazy_entry* tmp = new (std::nothrow) lazy_entry[capacity];
			if (tmp == 0) return 0;
			std::memcpy(tmp, m_data.list, sizeof(lazy_entry) * m_size);
			for (int i = 0; i < int(m_size); ++i) m_data.list[i].release();
			delete[] m_data.list;
			m_data.list = tmp;
			m_capacity = capacity;
		}

		TORRENT_ASSERT(m_size < m_capacity);
		return m_data.list + (m_size++);
	}

	std::string lazy_entry::list_string_value_at(int i) const
	{
		lazy_entry const* e = list_at(i);
		if (e == 0 || e->type() != lazy_entry::string_t) return std::string();
		return e->string_value();
	}

	pascal_string lazy_entry::list_pstr_at(int i) const
	{
		lazy_entry const* e = list_at(i);
		if (e == 0 || e->type() != lazy_entry::string_t) return pascal_string(0, 0);
		return e->string_pstr();
	}

	size_type lazy_entry::list_int_value_at(int i, size_type default_val) const
	{
		lazy_entry const* e = list_at(i);
		if (e == 0 || e->type() != lazy_entry::int_t) return default_val;
		return e->int_value();
	}

	void lazy_entry::clear()
	{
		switch (m_type)
		{
			case list_t: delete[] m_data.list; break;
			case dict_t: delete[] m_data.dict; break;
			default: break;
		}
		m_data.start = 0;
		m_size = 0;
		m_capacity = 0;
		m_type = none_t;
	}

	std::pair<char const*, int> lazy_entry::data_section() const
	{
		typedef std::pair<char const*, int> return_t;
		return return_t(m_begin, m_len);
	}

#if TORRENT_USE_IOSTREAM
	std::ostream& operator<<(std::ostream& os, lazy_entry const& e)
	{
		return os << print_entry(e);
	}
#endif // TORRENT_USE_IOSTREAM

	int line_longer_than(lazy_entry const& e, int limit)
	{
		int line_len = 0;
		switch (e.type())
		{
		case lazy_entry::list_t:
			line_len += 4;
			if (line_len > limit) return -1;
			for (int i = 0; i < e.list_size(); ++i)
			{
				int ret = line_longer_than(*e.list_at(i), limit - line_len);
				if (ret == -1) return -1;
				line_len += ret + 2;
			}
			break;
		case lazy_entry::dict_t:
			line_len += 4;
			if (line_len > limit) return -1;
			for (int i = 0; i < e.dict_size(); ++i)
			{
				line_len += 4 + e.dict_at(i).first.size();
				if (line_len > limit) return -1;
				int ret = line_longer_than(*e.dict_at(i).second, limit - line_len);
				if (ret == -1) return -1;
				line_len += ret + 1;
			}
			break;
		case lazy_entry::string_t:
			line_len += 3 + e.string_length();
			break;
		case lazy_entry::int_t:
		{
			size_type val = e.int_value();
			while (val > 0)
			{
				++line_len;
				val /= 10;
			}
			line_len += 2;
		}
		break;
		case lazy_entry::none_t:
			line_len += 4;
			break;
		}
	
		if (line_len > limit) return -1;
		return line_len;
	}

	std::string print_entry(lazy_entry const& e, bool single_line, int indent)
	{
		char indent_str[200];
		memset(indent_str, ' ', 200);
		indent_str[0] = ',';
		indent_str[1] = '\n';
		indent_str[199] = 0;
		if (indent < 197 && indent >= 0) indent_str[indent+2] = 0;
		std::string ret;
		switch (e.type())
		{
			case lazy_entry::none_t: return "none";
			case lazy_entry::int_t:
			{
				char str[100];
				snprintf(str, sizeof(str), "%" PRId64, e.int_value());
				return str;
			}
			case lazy_entry::string_t:
			{
				bool printable = true;
				char const* str = e.string_ptr();
				for (int i = 0; i < e.string_length(); ++i)
				{
					using namespace std;
					if (is_print((unsigned char)str[i])) continue;
					printable = false;
					break;
				}
				ret += "'";
				if (printable)
				{
					ret += e.string_value();
					ret += "'";
					return ret;
				}
				for (int i = 0; i < e.string_length(); ++i)
				{
					char tmp[5];
					snprintf(tmp, sizeof(tmp), "%02x", (unsigned char)str[i]);
					ret += tmp;
				}
				ret += "'";
				return ret;
			}
			case lazy_entry::list_t:
			{
				ret += '[';
				bool one_liner = line_longer_than(e, 200) != -1 || single_line;

				if (!one_liner) ret += indent_str + 1;
				for (int i = 0; i < e.list_size(); ++i)
				{
					if (i == 0 && one_liner) ret += " ";
					ret += print_entry(*e.list_at(i), single_line, indent + 2);
					if (i < e.list_size() - 1) ret += (one_liner?", ":indent_str);
					else ret += (one_liner?" ":indent_str+1);
				}
				ret += "]";
				return ret;
			}
			case lazy_entry::dict_t:
			{
				ret += "{";
				bool one_liner = line_longer_than(e, 200) != -1 || single_line;

				if (!one_liner) ret += indent_str+1;
				for (int i = 0; i < e.dict_size(); ++i)
				{
					if (i == 0 && one_liner) ret += " ";
					std::pair<std::string, lazy_entry const*> ent = e.dict_at(i);
					ret += "'";
					ret += ent.first;
					ret += "': ";
					ret += print_entry(*ent.second, single_line, indent + 2);
					if (i < e.dict_size() - 1) ret += (one_liner?", ":indent_str);
					else ret += (one_liner?" ":indent_str+1);
				}
				ret += "}";
				return ret;
			}
		}
		return ret;
	}
};

