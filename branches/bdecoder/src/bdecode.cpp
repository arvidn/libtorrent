/*

Copyright (c) 2015, Arvid Norberg
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

#include "libtorrent/bdecode.hpp"
#include <limits>

namespace libtorrent
{
	using detail::bdecode_token;

	namespace
	{
	bool numeric(char c) { return c >= '0' && c <= '9'; }

	// finds the end of an integer and verifies that it looks valid this does
	// not detect all overflows, just the ones that are an order of magnitued
	// beyond. Exact overflow checking is done when the integer value is queried
	// from a bdecode_node.
	char const* check_integer(char const* start, char const* end
		, bdecode_errors::error_code_enum& e)
	{
		if (start == end)
		{
			e = bdecode_errors::unexpected_eof;
			return start;
		}

		if (*start == '-')
		{
			++start;
			if (start == end)
			{
				e = bdecode_errors::unexpected_eof;
				return start;
			}
		}

		int digits = 0;
		do
		{
			if (digits > 20)
			{
				e = bdecode_errors::overflow;
				break;
			}
			if (!numeric(*start))
			{
				e = bdecode_errors::expected_digit;
				break;
			}
			++start;
			++digits;

			if (start == end)
			{
				e = bdecode_errors::unexpected_eof;
				break;
			}
		}
		while (*start != 'e');
		return start;
	}

	struct stack_frame
	{
		stack_frame(int t): token(t), state(0) {}
		// this is an index into m_tokens
		boost::uint32_t token:31;
		// this is used for doctionaries to indicate whether we're
		// reading a key or a vale. 0 means key 1 is value
		boost::uint32_t state:1;
	};

	int fail(std::vector<bdecode_token>& tokens
		, std::vector<stack_frame>& stack
		, boost::uint32_t offset)
	{
		// unwind the stack by inserting terminator to make whatever we have
		// so far valid
		while (!stack.empty()) {
			int top = stack.back().token;
			TORRENT_ASSERT(tokens.size() - top <= bdecode_token::max_next_item);
			tokens[top].next_item = tokens.size() - top;
			tokens.push_back(bdecode_token(offset, 1, bdecode_token::end));
			stack.pop_back();
		}
		return -1;
	}

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

	} // anonymous namespace


	// fills in 'val' with what the string between start and the
	// first occurance of the delimiter is interpreted as an int.
	// return the pointer to the delimiter, or 0 if there is a
	// parse error. val should be initialized to zero
	char const* parse_int(char const* start, char const* end, char delimiter
		, boost::int64_t& val, bdecode_errors::error_code_enum& ec)
	{
		while (start < end && *start != delimiter)
		{
			if (!numeric(*start))
			{
				ec = bdecode_errors::expected_digit;
				return start;
			}
			if (val > (std::numeric_limits<boost::int64_t>::max)() / 10)
			{
				ec = bdecode_errors::overflow;
				return start;
			}
			val *= 10;
			int digit = *start - '0';
			if (val > (std::numeric_limits<boost::int64_t>::max)() - digit)
			{
				ec = bdecode_errors::overflow;
				return start;
			}
			val += digit;
			++start;
		}
		if (*start != delimiter)
			ec = bdecode_errors::expected_colon;
		return start;
	}


	struct bdecode_error_category : boost::system::error_category
	{
		virtual const char* name() const BOOST_SYSTEM_NOEXCEPT;
		virtual std::string message(int ev) const BOOST_SYSTEM_NOEXCEPT;
		virtual boost::system::error_condition default_error_condition(
			int ev) const BOOST_SYSTEM_NOEXCEPT
		{ return boost::system::error_condition(ev, *this); }
	};

	const char* bdecode_error_category::name() const BOOST_SYSTEM_NOEXCEPT
	{
		return "bdecode error";
	}

	std::string bdecode_error_category::message(int ev) const BOOST_SYSTEM_NOEXCEPT
	{
		static char const* msgs[] =
		{
			"no error",
			"expected digit in bencoded string",
			"expected colon in bencoded string",
			"unexpected end of file in bencoded string",
			"expected value (list, dict, int or string) in bencoded string",
			"bencoded nesting depth exceeded",
			"bencoded item count limit exceeded",
			"integer overflow",
		};
		if (ev < 0 || ev >= int(sizeof(msgs)/sizeof(msgs[0])))
			return "Unknown error";
		return msgs[ev];
	}

	boost::system::error_category& get_bdecode_category()
	{
		static bdecode_error_category bdecode_category;
		return bdecode_category;
	}

	namespace bdecode_errors
	{
		boost::system::error_code make_error_code(error_code_enum e)
		{
			return boost::system::error_code(e, get_bdecode_category());
		}
	}


	bdecode_node::bdecode_node()
		: m_root_tokens(&m_tokens)
		, m_buffer(NULL)
		, m_buffer_size(0)
		, m_token_idx(-1)
		, m_last_index(-1)
		, m_last_token(-1)
		, m_size(-1)
	{}

	bdecode_node::bdecode_node(std::vector<bdecode_token> const& tokens, char const* buf
		, int len, int idx)
		: m_root_tokens(&tokens)
		, m_buffer(buf)
		, m_buffer_size(len)
		, m_token_idx(idx)
		, m_last_index(-1)
		, m_last_token(-1)
		, m_size(-1)
	{
		TORRENT_ASSERT(idx < int(m_root_tokens->size()));
		TORRENT_ASSERT(idx >= 0);
	}

	void bdecode_node::clear()
	{
		m_tokens.clear();
		m_root_tokens = &m_tokens;
		m_token_idx = -1;
	}

	bdecode_node::type_t bdecode_node::type() const
	{
		if (m_token_idx == -1) return none_t;
		return (bdecode_node::type_t)(*m_root_tokens)[m_token_idx].type;
	}

	std::pair<char const*, int> bdecode_node::data_section() const
	{
		if (m_token_idx == -1) return std::make_pair(m_buffer, 0);

		TORRENT_ASSERT(m_token_idx != -1);
		bdecode_token const& t = (*m_root_tokens)[m_token_idx];
		bdecode_token const& next = (*m_root_tokens)[m_token_idx + t.next_item];
		return std::make_pair(m_buffer + t.offset, next.offset - t.offset);
	}

	bdecode_node bdecode_node::list_at(int i) const
	{
		TORRENT_ASSERT(type() == list_t);
		TORRENT_ASSERT(i >= 0);

		// make sure this is a list.
		std::vector<bdecode_token> const& tokens = *m_root_tokens;

		// this is the first item
		int token = m_token_idx + 1;
		int item = 0;

		// do we have a lookup cached?
		if (m_last_index <= i && m_last_index != -1)
		{
			token = m_last_token;
			item = m_last_index;
		}

		while (item < i)
		{
			token += tokens[token].next_item;
			++item;
			TORRENT_ASSERT(token < int(tokens.size()));

			// index 'i' out of range
			TORRENT_ASSERT(tokens[token].type != bdecode_token::end);
		}

		m_last_token = token;
		m_last_index = i;

		return bdecode_node(tokens, m_buffer, m_buffer_size, token);
	}

	boost::int64_t bdecode_node::list_int_value_at(int i)
	{
		bdecode_node n = list_at(i);
		if (n.type() != bdecode_node::int_t) return 0;
		return n.int_value();
	}

	int bdecode_node::list_size() const
	{
		TORRENT_ASSERT(type() == list_t);

		if (m_size != -1) return m_size;

		// make sure this is a list.
		std::vector<bdecode_token> const& tokens = *m_root_tokens;
		TORRENT_ASSERT(tokens[m_token_idx].type == bdecode_token::list);

		// this is the first item
		int token = m_token_idx + 1;
		int ret = 0;
		
		// do we have a lookup cached?
		if (m_last_index != -1)
		{
			token = m_last_token;
			ret = m_last_index;
		}
		while (tokens[token].type != bdecode_token::end)
		{
			token += tokens[token].next_item;
			++ret;
			TORRENT_ASSERT(token < int(tokens.size()));
		}

		m_size = ret;

		return ret;
	}

	std::pair<std::string, bdecode_node> bdecode_node::dict_at(int i) const
	{
		TORRENT_ASSERT(type() == dict_t);
		TORRENT_ASSERT(m_token_idx != -1);
	
		std::vector<bdecode_token> const& tokens = *m_root_tokens;
		TORRENT_ASSERT(tokens[m_token_idx].type == bdecode_token::dict);

		int token = m_token_idx + 1;
		int item = 0;

		// do we have a lookup cached?
		if (m_last_index <= i && m_last_index != -1)
		{
			token = m_last_token;
			item = m_last_index;
		}

		while (item < i)
		{
			TORRENT_ASSERT(tokens[token].type == bdecode_token::string);

			// skip the key
			token += tokens[token].next_item;
			TORRENT_ASSERT(token < int(tokens.size()));
			TORRENT_ASSERT(tokens[token].type != bdecode_token::end);

			// skip the value
			token += tokens[token].next_item;

			++item;
			TORRENT_ASSERT(token < int(tokens.size()));

			// index 'i' out of range
			TORRENT_ASSERT(tokens[token].type != bdecode_token::end);
		}

		// there's no point in caching the first item
		if (i > 0)
		{
			m_last_token = token;
			m_last_index = i;
		}

		int value_token = token + tokens[token].next_item;
		TORRENT_ASSERT(token < int(tokens.size()));
		TORRENT_ASSERT(tokens[token].type != bdecode_token::end);

		return std::make_pair(
			bdecode_node(tokens, m_buffer, m_buffer_size, token).string_value()
			, bdecode_node(tokens, m_buffer, m_buffer_size, value_token));
	}

	int bdecode_node::dict_size() const
	{
		TORRENT_ASSERT(type() == dict_t);
		TORRENT_ASSERT(m_token_idx != -1);

		if (m_size != -1) return m_size;

		std::vector<bdecode_token> const& tokens = *m_root_tokens;
		TORRENT_ASSERT(tokens[m_token_idx].type == bdecode_token::dict);

		// this is the first item
		int token = m_token_idx + 1;
		int ret = 0;

		if (m_last_index != -1)
		{
			ret = m_last_index * 2;
			token = m_last_token;
		}

		while (tokens[token].type != bdecode_token::end)
		{
			token += tokens[token].next_item;
			++ret;
			TORRENT_ASSERT(token < int(tokens.size()));
		}

		// a dictionary must contain full key-value pairs. which means
		// the number of entries is divisible by 2
		TORRENT_ASSERT((ret % 2) == 0);

		// each item is one key and one value, so divide by 2
		ret /= 2;

		m_size = ret;

		return ret;
	}

	bdecode_node bdecode_node::dict_find(std::string key) const
	{
		TORRENT_ASSERT(type() == dict_t);

		std::vector<bdecode_token> const& tokens = *m_root_tokens;
	
		// this is the first item
		int token = m_token_idx + 1;

		while (tokens[token].type != bdecode_token::end)
		{
			bdecode_token const& t = tokens[token];
			TORRENT_ASSERT(t.type == bdecode_token::string);
			int size = (*m_root_tokens)[token + 1].offset - t.offset - t.header;
			if (key.size() == size
				&& std::equal(key.c_str(), key.c_str() + size, m_buffer + t.offset + t.header))
			{
				// skip key
				token += t.next_item;
				TORRENT_ASSERT(token < int(tokens.size()));
				TORRENT_ASSERT(tokens[token].type != bdecode_token::end);
			
				return bdecode_node(tokens, m_buffer, m_buffer_size, token);
			}

			// skip key
			token += t.next_item;
			TORRENT_ASSERT(token < int(tokens.size()));
			TORRENT_ASSERT(tokens[token].type != bdecode_token::end);

			// skip value
			token += tokens[token].next_item;
			TORRENT_ASSERT(token < int(tokens.size()));
			TORRENT_ASSERT(tokens[token].type != bdecode_token::end);
		}

		return bdecode_node();
	}

	bdecode_node bdecode_node::dict_find(char const* key) const
	{
		TORRENT_ASSERT(type() == dict_t);

		std::vector<bdecode_token> const& tokens = *m_root_tokens;
	
		// this is the first item
		int token = m_token_idx + 1;

		while (tokens[token].type != bdecode_token::end)
		{
			bdecode_token const& t = tokens[token];
			TORRENT_ASSERT(t.type == bdecode_token::string);
			int size = (*m_root_tokens)[token + 1].offset - t.offset - t.header;
			if (string_equal(key, m_buffer + t.offset + t.header, size))
			{
				// skip key
				token += t.next_item;
				TORRENT_ASSERT(token < int(tokens.size()));
				TORRENT_ASSERT(tokens[token].type != bdecode_token::end);
			
				return bdecode_node(tokens, m_buffer, m_buffer_size, token);
			}

			// skip key
			token += t.next_item;
			TORRENT_ASSERT(token < int(tokens.size()));
			TORRENT_ASSERT(tokens[token].type != bdecode_token::end);

			// skip value
			token += tokens[token].next_item;
			TORRENT_ASSERT(token < int(tokens.size()));
			TORRENT_ASSERT(tokens[token].type != bdecode_token::end);
		}

		return bdecode_node();
	}

	std::string bdecode_node::dict_find_string_value(char const* key) const
	{
		bdecode_node n = dict_find(key);
		if (n.type() != bdecode_node::string_t) return std::string();
		return n.string_value();
	}

	boost::int64_t bdecode_node::dict_find_int_value(char const* key) const
	{
		bdecode_node n = dict_find(key);
		if (n.type() != bdecode_node::int_t) return 0;
		return n.int_value();
	}

	boost::int64_t bdecode_node::int_value() const
	{
		TORRENT_ASSERT(type() == int_t);
		bdecode_token const& t = (*m_root_tokens)[m_token_idx];
		int size = (*m_root_tokens)[m_token_idx + 1].offset - t.offset;
		TORRENT_ASSERT(t.type == bdecode_token::integer);
	
		char const* ptr = m_buffer + t.offset + t.header;
		boost::int64_t val = 0;
		bool negative = false;
		if (*ptr == '-') negative = true;
		bdecode_errors::error_code_enum ec = bdecode_errors::no_error;
		parse_int(ptr + negative
			, ptr + size, 'e', val, ec);
		if (ec) return 0;
		if (negative) val = -val;
		return val;
	}

	std::string bdecode_node::string_value() const
	{
		TORRENT_ASSERT(type() == string_t);
		bdecode_token const& t = (*m_root_tokens)[m_token_idx];
		int size = (*m_root_tokens)[m_token_idx + 1].offset - t.offset - t.header;
		TORRENT_ASSERT(t.type == bdecode_token::string);

		return std::string(m_buffer + t.offset + t.header, size);
	}

	char const* bdecode_node::string_ptr() const
	{
		TORRENT_ASSERT(type() == string_t);
		bdecode_token const& t = (*m_root_tokens)[m_token_idx];
		TORRENT_ASSERT(t.type == bdecode_token::string);
		return m_buffer + t.offset + t.header;
	}

	int bdecode_node::string_length() const
	{
		TORRENT_ASSERT(type() == string_t);
		bdecode_token const& t = (*m_root_tokens)[m_token_idx];
		TORRENT_ASSERT(t.type == bdecode_token::string);
		return (*m_root_tokens)[m_token_idx + 1].offset - t.offset - t.header;
	}

#define TORRENT_FAIL_BDECODE(code) do { ec = make_error_code(code); \
	if (error_pos) *error_pos = start - orig_start; \
	return fail(ret.m_tokens, stack, start - orig_start); } while (false)

	int bdecode(char const* start, char const* end, bdecode_node& ret
		, error_code& ec, int* error_pos, int depth_limit, int token_limit)
	{

		if (end - start > bdecode_token::max_offset)
		{
			if (error_pos) *error_pos = 0;
			ec = make_error_code(bdecode_errors::overflow);
			return -1;
		}

		// this is the stack of bdecode_token indices, into m_tokens.
		std::vector<stack_frame> stack;

		char const* const orig_start = start;
		ret.clear();
		if (start == end) return 0;

		while (start <= end)
		{
			if (start >= end) TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);

			if (stack.size() > depth_limit)
				TORRENT_FAIL_BDECODE(bdecode_errors::depth_exceeded);

			--token_limit;
			if (token_limit < 0)
				TORRENT_FAIL_BDECODE(bdecode_errors::limit_exceeded);

			// look for a new token
			char t = *start;

			// if we're currently parsing a dictionary, assert that
			// every other node is a string.
			if (!stack.empty()
				&& ret.m_tokens[stack.back().token].type == bdecode_token::dict)
			{
				if (stack.back().state == 0)
				{
					// the current parent is a dict and we are parsing a key.
					// only allow a digit (for a string) or 'e' to terminate
					if (!numeric(t) && t != 'e')
						TORRENT_FAIL_BDECODE(bdecode_errors::expected_digit);
				}
				// the next item we parse is the opposite
				stack.back().state = ~stack.back().state;;
			}

			switch (t)
			{
				case 'd':
					stack.push_back(ret.m_tokens.size());
					// we push it into the stack so that we know where to fill
					// in the next_node field once we pop this node off the stack.
					// i.e. get to the node following the dictionary in the buffer
					ret.m_tokens.push_back(bdecode_token(start - orig_start
						, bdecode_token::dict));
					++start;
					break;
				case 'l':
					stack.push_back(ret.m_tokens.size());
					// we push it into the stack so that we know where to fill
					// in the next_node field once we pop this node off the stack.
					// i.e. get to the node following the list in the buffer
					ret.m_tokens.push_back(bdecode_token(start - orig_start
						, bdecode_token::list));
					++start;
					break;
				case 'i':
				{
					char const* int_start = start;
					bdecode_errors::error_code_enum e = bdecode_errors::no_error;
					start = check_integer(start + 1, end, e);
					if (e) TORRENT_FAIL_BDECODE(e);
					TORRENT_ASSERT(*start == 'e');

					// +1 here to point to the first digit, rather than 'i'
					ret.m_tokens.push_back(bdecode_token(int_start - orig_start
						, 1, bdecode_token::integer, 1));
					// skip 'e'
					++start;
					break;
				}
				case 'e':
				{
					// this is the end of a list or dict
					if (stack.empty())
						TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);

					if (!stack.empty()
						&& ret.m_tokens[stack.back().token].type == bdecode_token::dict
						&& stack.back().state == 0)
					{
						// this means we're parsing a dictionary and about to parse a
						// value associated with a key. Instad, we got a termination
						TORRENT_FAIL_BDECODE(bdecode_errors::expected_value);
					}

					// insert the end-of-sequence token
					ret.m_tokens.push_back(bdecode_token(start - orig_start, 1
						, bdecode_token::end));

					// and back-patch the start of this sequence with the offset
					// to the next token we'll insert
					int top = stack.back().token;
					// subtract the token's own index, since this is a relative
					// offset
					if (ret.m_tokens.size() - top > bdecode_token::max_next_item)
						TORRENT_FAIL_BDECODE(bdecode_errors::overflow);

					ret.m_tokens[top].next_item = ret.m_tokens.size() - top;

					// and pop it from the stack.
					stack.pop_back();
					++start;
					break;
				}
				default:
				{
					// this is the case for strings. The start character is any
					// numeric digit
					if (!numeric(t))
						TORRENT_FAIL_BDECODE(bdecode_errors::expected_value);

					boost::int64_t len = t - '0';
					char const* str_start = start;
					++start;
					bdecode_errors::error_code_enum e = bdecode_errors::no_error;
					start = parse_int(start, end, ':', len, e);
					if (e)
						TORRENT_FAIL_BDECODE(e);
					if (start + len + 1 > end)
						TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);
					if (len < 0)
						TORRENT_FAIL_BDECODE(bdecode_errors::overflow);

					// skip ':'
					++start;
					if (start >= end) TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);
					ret.m_tokens.push_back(bdecode_token(str_start - orig_start
						, 1, bdecode_token::string, start - str_start));
					start += len;
					break;
				}
			}
			// this terminates the top level node, we're done!
			if (stack.empty())
				break;
		}
		ret.m_tokens.push_back(bdecode_token(start - orig_start, 0
			, bdecode_token::end));

		ret.m_token_idx = 0;
		ret.m_buffer = orig_start;
		ret.m_buffer_size = start - orig_start;
		return 0;
	}
}

