/*

Copyright (c) 2008-2018, Arvid Norberg
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

#ifndef TORRENT_NO_DEPRECATE

#include "libtorrent/config.hpp"
#include "libtorrent/lazy_entry.hpp"
#include "libtorrent/bdecode.hpp" // for error codes
#include <cstring>
#include <limits> // for numeric_limits

namespace
{
	const int lazy_entry_grow_factor = 150; // percent
	const int lazy_entry_dict_init = 5;
	const int lazy_entry_list_init = 5;
}

namespace libtorrent
{

	namespace
	{
		int fail(int* error_pos
			, std::vector<lazy_entry*>& stack
			, char const* start
			, char const* orig_start)
		{
			while (!stack.empty()) {
				lazy_entry* top = stack.back();
				if (top->type() == lazy_entry::dict_t || top->type() == lazy_entry::list_t)
				{
					top->pop();
					break;
				}
				stack.pop_back();
			}
			if (error_pos) *error_pos = start - orig_start;
			return -1;
		}

#define TORRENT_FAIL_BDECODE(code) do { ec = make_error_code(code); return fail(error_pos, stack, start, orig_start); } TORRENT_WHILE_0

	bool numeric(char c) { return c >= '0' && c <= '9'; }

	char const* find_char(char const* start, char const* end, char delimiter)
	{
		while (start < end && *start != delimiter) ++start;
		return start;
	}

	} // anonymous namespace

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

		std::vector<lazy_entry*> stack;

		if (start == end)
			TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);

		stack.push_back(&ret);
		while (start <= end)
		{
			if (stack.empty()) break; // done!

			lazy_entry* top = stack.back();

			if (int(stack.size()) > depth_limit) TORRENT_FAIL_BDECODE(bdecode_errors::depth_exceeded);
			if (start >= end) TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);
			char t = *start;
			++start;
			if (start >= end && t != 'e') TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);

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
					if (!numeric(t)) TORRENT_FAIL_BDECODE(bdecode_errors::expected_digit);
					boost::int64_t len = t - '0';
					bdecode_errors::error_code_enum e = bdecode_errors::no_error;
					start = parse_int(start, end, ':', len, e);
					if (e)
						TORRENT_FAIL_BDECODE(e);
					if (start == end)
						TORRENT_FAIL_BDECODE(bdecode_errors::expected_colon);

					// remaining buffer size excluding ':'
					const ptrdiff_t buff_size = end - start - 1;
					if (len > buff_size)
						TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);

					if (len < 0)
						TORRENT_FAIL_BDECODE(bdecode_errors::overflow);

					++start;
					if (start == end) TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);
					lazy_entry* ent = top->dict_append(start);
					if (ent == 0) TORRENT_FAIL_BDECODE(boost::system::errc::not_enough_memory);
					start += len;
					if (start >= end) TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);
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
					if (ent == 0) TORRENT_FAIL_BDECODE(boost::system::errc::not_enough_memory);
					stack.push_back(ent);
					break;
				}
				case lazy_entry::int_t:
				case lazy_entry::string_t:
				case lazy_entry::none_t:
					break;
			}

			--item_limit;
			if (item_limit <= 0) TORRENT_FAIL_BDECODE(bdecode_errors::limit_exceeded);

			top = stack.back();
			switch (t)
			{
				case 'd':
					top->construct_dict(start - 1);
					break;
				case 'l':
					top->construct_list(start - 1);
					break;
				case 'i':
				{
					char const* int_start = start;
					start = find_char(start, end, 'e');
					top->construct_int(int_start, start - int_start);
					if (start == end) TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);
					TORRENT_ASSERT(*start == 'e');
					++start;
					stack.pop_back();
					break;
				}
				default:
				{
					if (!numeric(t))
						TORRENT_FAIL_BDECODE(bdecode_errors::expected_value);

					boost::int64_t len = t - '0';
					bdecode_errors::error_code_enum e = bdecode_errors::no_error;
					start = parse_int(start, end, ':', len, e);
					if (e)
						TORRENT_FAIL_BDECODE(e);
					if (start == end)
						TORRENT_FAIL_BDECODE(bdecode_errors::expected_colon);

					// remaining buffer size excluding ':'
					const ptrdiff_t buff_size = end - start - 1;
					if (len > buff_size)
						TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);
					if (len < 0)
						TORRENT_FAIL_BDECODE(bdecode_errors::overflow);

					++start;
					if (start == end) TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);
					top->construct_string(start, int(len));
					start += len;
					stack.pop_back();
					break;
				}
			}
		}
		return 0;
	}

	int lazy_entry::capacity() const
	{
		TORRENT_ASSERT(m_type == dict_t || m_type == list_t);
		if (m_data.list == NULL) return 0;
		if (m_type == dict_t)
			return m_data.dict[0].val.m_len;
		else
			return m_data.list[0].m_len;
	}

	boost::int64_t lazy_entry::int_value() const
	{
		TORRENT_ASSERT(m_type == int_t);
		boost::int64_t val = 0;
		bool negative = false;
		if (*m_data.start == '-') negative = true;
		bdecode_errors::error_code_enum ec = bdecode_errors::no_error;
		parse_int(m_data.start + negative
			, m_data.start + m_size, 'e', val, ec);
		if (ec) return 0;
		if (negative) val = -val;
		return val;
	}

	lazy_entry* lazy_entry::dict_append(char const* name)
	{
		TORRENT_ASSERT(m_type == dict_t);
		TORRENT_ASSERT(m_size <= this->capacity());
		if (m_data.dict == NULL)
		{
			int capacity = lazy_entry_dict_init;
			m_data.dict = new (std::nothrow) lazy_dict_entry[capacity+1];
			if (m_data.dict == NULL) return NULL;
			m_data.dict[0].val.m_len = capacity;
		}
		else if (m_size == this->capacity())
		{
			int capacity = this->capacity() * lazy_entry_grow_factor / 100;
			lazy_dict_entry* tmp = new (std::nothrow) lazy_dict_entry[capacity+1];
			if (tmp == NULL) return NULL;
			std::memcpy(tmp, m_data.dict, sizeof(lazy_dict_entry) * (m_size + 1));
			for (int i = 0; i < int(m_size); ++i) m_data.dict[i+1].val.release();

			delete[] m_data.dict;
			m_data.dict = tmp;
			m_data.dict[0].val.m_len = capacity;
		}

		TORRENT_ASSERT(m_size < this->capacity());
		lazy_dict_entry& ret = m_data.dict[1+m_size++];
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
		lazy_dict_entry const& e = m_data.dict[i+1];
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

	boost::int64_t lazy_entry::dict_find_int_value(char const* name
		, boost::int64_t default_val) const
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

	lazy_entry const* lazy_entry::dict_find_dict(std::string const& name) const
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
			lazy_dict_entry& e = m_data.dict[i+1];
			if (string_equal(name, e.name, e.val.m_begin - e.name))
				return &e.val;
		}
		return 0;
	}

	lazy_entry* lazy_entry::dict_find(std::string const& name)
	{
		TORRENT_ASSERT(m_type == dict_t);
		for (int i = 0; i < int(m_size); ++i)
		{
			lazy_dict_entry& e = m_data.dict[i+1];
			if (name.size() != e.val.m_begin - e.name) continue;
			if (std::equal(name.begin(), name.end(), e.name))
				return &e.val;
		}
		return 0;
	}

	lazy_entry* lazy_entry::list_append()
	{
		TORRENT_ASSERT(m_type == list_t);
		TORRENT_ASSERT(m_size <= this->capacity());
		if (m_data.start == NULL)
		{
			int capacity = lazy_entry_list_init;
			m_data.list = new (std::nothrow) lazy_entry[capacity+1];
			if (m_data.list == 0) return 0;
			m_data.list[0].m_len = capacity;
		}
		else if (m_size == this->capacity())
		{
			int capacity = this->capacity() * lazy_entry_grow_factor / 100;
			lazy_entry* tmp = new (std::nothrow) lazy_entry[capacity+1];
			if (tmp == NULL) return NULL;
			std::memcpy(tmp, m_data.list, sizeof(lazy_entry) * (m_size+1));
			for (int i = 0; i < int(m_size); ++i) m_data.list[i+1].release();

			delete[] m_data.list;
			m_data.list = tmp;
			m_data.list[0].m_len = capacity;
		}

		TORRENT_ASSERT(m_size < this->capacity());
		return &m_data.list[1 + (m_size++)];
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

	boost::int64_t lazy_entry::list_int_value_at(int i, boost::int64_t default_val) const
	{
		lazy_entry const* e = list_at(i);
		if (e == 0 || e->type() != lazy_entry::int_t) return default_val;
		return e->int_value();
	}

	void lazy_entry::clear()
	{
		switch (m_type)
		{
			case list_t:
				delete[] m_data.list;
				break;
			case dict_t:
				delete[] m_data.dict;
				break;
			default: break;
		}
		m_data.start = NULL;
		m_size = 0;
		m_type = none_t;
	}

	std::pair<char const*, int> lazy_entry::data_section() const
	{
		typedef std::pair<char const*, int> return_t;
		return return_t(m_begin, m_len);
	}

	namespace {

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
			boost::int64_t val = e.int_value();
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

	void escape_string(std::string& ret, char const* str, int len)
	{
		for (int i = 0; i < len; ++i)
		{
			if (str[i] >= 32 && str[i] < 127)
			{
				ret += str[i];
			}
			else
			{
				char tmp[5];
				snprintf(tmp, sizeof(tmp), "\\x%02x", boost::uint8_t(str[i]));
				ret += tmp;
			}
		}
	}

	void print_string(std::string& ret, char const* str, int len, bool single_line)
	{
		bool printable = true;
		for (int i = 0; i < len; ++i)
		{
			char c = str[i];
			if (c >= 32 && c < 127) continue;
			printable = false;
			break;
		}
		ret += "'";
		if (printable)
		{
			if (single_line && len > 30)
			{
				ret.append(str, 14);
				ret += "...";
				ret.append(str + len-14, 14);
			}
			else
				ret.append(str, len);
			ret += "'";
			return;
		}
		if (single_line && len > 20)
		{
			escape_string(ret, str, 9);
			ret += "...";
			escape_string(ret, str + len - 9, 9);
		}
		else
		{
			escape_string(ret, str, len);
		}
		ret += "'";
	}
	} // anonymous namespace

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
				print_string(ret, e.string_ptr(), e.string_length(), single_line);
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
					print_string(ret, ent.first.c_str(), ent.first.size(), true);
					ret += ": ";
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
}

#endif // TORRENT_NO_DEPRECATE

