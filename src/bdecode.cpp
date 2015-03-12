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
#include "libtorrent/alloca.hpp"
#include <boost/system/error_code.hpp>
#include <limits>
#include <cstring> // for memset

#ifndef BOOST_SYSTEM_NOEXCEPT
#define BOOST_SYSTEM_NOEXCEPT throw()
#endif

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

		if (digits > 20)
		{
			e = bdecode_errors::overflow;
		}

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
		: m_root_tokens(0)
		, m_buffer(NULL)
		, m_buffer_size(0)
		, m_token_idx(-1)
		, m_last_index(-1)
		, m_last_token(-1)
		, m_size(-1)
	{}

	bdecode_node::bdecode_node(bdecode_node const& n)
		: m_tokens(n.m_tokens)
		, m_root_tokens(n.m_root_tokens)
		, m_buffer(n.m_buffer)
		, m_buffer_size(n.m_buffer_size)
		, m_token_idx(n.m_token_idx)
		, m_last_index(n.m_last_index)
		, m_last_token(n.m_last_token)
		, m_size(n.m_size)
	{
		(*this) = n;
	}

	bdecode_node& bdecode_node::operator=(bdecode_node const& n)
	{
		m_tokens = n.m_tokens;
		m_root_tokens = n.m_root_tokens;
		m_buffer = n.m_buffer;
		m_buffer_size = n.m_buffer_size;
		m_token_idx = n.m_token_idx;
		m_last_index = n.m_last_index;
		m_last_token = n.m_last_token;
		m_size = n.m_size;
		if (!m_tokens.empty())
		{
			// if this is a root, make the token pointer
			// point to our storage
			m_root_tokens = &m_tokens[0];
		}
		return *this;
	}

	bdecode_node::bdecode_node(bdecode_token const* tokens, char const* buf
		, int len, int idx)
		: m_root_tokens(tokens)
		, m_buffer(buf)
		, m_buffer_size(len)
		, m_token_idx(idx)
		, m_last_index(-1)
		, m_last_token(-1)
		, m_size(-1)
	{
		TORRENT_ASSERT(tokens != NULL);
		TORRENT_ASSERT(idx >= 0);
	}

	bdecode_node bdecode_node::non_owning() const
	{
		// if we're not a root, just return a copy of ourself
		if (m_tokens.empty()) return *this;

		// otherwise, return a reference to this node, but without
		// being an owning root node
		return bdecode_node(&m_tokens[0], m_buffer, m_buffer_size, m_token_idx);
	}

	void bdecode_node::clear()
	{
		m_tokens.clear();
		m_root_tokens = NULL;
		m_token_idx = -1;
		m_size = -1;
		m_last_index = -1;
		m_last_token = -1;
	}

	void bdecode_node::switch_underlying_buffer(char const* buf)
	{
		TORRENT_ASSERT(!m_tokens.empty());
		if (m_tokens.empty()) return;

		m_buffer = buf;
	}

	bdecode_node::type_t bdecode_node::type() const
	{
		if (m_token_idx == -1) return none_t;
		return (bdecode_node::type_t)m_root_tokens[m_token_idx].type;
	}

	bdecode_node::operator bool() const
	{ return m_token_idx != -1; }

	std::pair<char const*, int> bdecode_node::data_section() const
	{
		if (m_token_idx == -1) return std::make_pair(m_buffer, 0);

		TORRENT_ASSERT(m_token_idx != -1);
		bdecode_token const& t = m_root_tokens[m_token_idx];
		bdecode_token const& next = m_root_tokens[m_token_idx + t.next_item];
		return std::make_pair(m_buffer + t.offset, next.offset - t.offset);
	}

	bdecode_node bdecode_node::list_at(int i) const
	{
		TORRENT_ASSERT(type() == list_t);
		TORRENT_ASSERT(i >= 0);

		// make sure this is a list.
		bdecode_token const* tokens = m_root_tokens;

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

			// index 'i' out of range
			TORRENT_ASSERT(tokens[token].type != bdecode_token::end);
		}

		m_last_token = token;
		m_last_index = i;

		return bdecode_node(tokens, m_buffer, m_buffer_size, token);
	}

	std::string bdecode_node::list_string_value_at(int i
		, char const* default_val)
	{
		bdecode_node n = list_at(i);
		if (n.type() != bdecode_node::string_t) return default_val;
		return n.string_value();
	}

	boost::int64_t bdecode_node::list_int_value_at(int i
		, boost::int64_t default_val)
	{
		bdecode_node n = list_at(i);
		if (n.type() != bdecode_node::int_t) return default_val;
		return n.int_value();
	}

	int bdecode_node::list_size() const
	{
		TORRENT_ASSERT(type() == list_t);

		if (m_size != -1) return m_size;

		// make sure this is a list.
		bdecode_token const* tokens = m_root_tokens;
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
		}

		m_size = ret;

		return ret;
	}

	std::pair<std::string, bdecode_node> bdecode_node::dict_at(int i) const
	{
		TORRENT_ASSERT(type() == dict_t);
		TORRENT_ASSERT(m_token_idx != -1);
	
		bdecode_token const* tokens = m_root_tokens;
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
			TORRENT_ASSERT(tokens[token].type != bdecode_token::end);

			// skip the value
			token += tokens[token].next_item;

			++item;

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

		bdecode_token const* tokens = m_root_tokens;
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

		bdecode_token const* tokens = m_root_tokens;
	
		// this is the first item
		int token = m_token_idx + 1;

		while (tokens[token].type != bdecode_token::end)
		{
			bdecode_token const& t = tokens[token];
			TORRENT_ASSERT(t.type == bdecode_token::string);
			int size = m_root_tokens[token + 1].offset - t.offset - t.start_offset();
			if (int(key.size()) == size
				&& std::equal(key.c_str(), key.c_str() + size, m_buffer
					+ t.offset + t.start_offset()))
			{
				// skip key
				token += t.next_item;
				TORRENT_ASSERT(tokens[token].type != bdecode_token::end);
			
				return bdecode_node(tokens, m_buffer, m_buffer_size, token);
			}

			// skip key
			token += t.next_item;
			TORRENT_ASSERT(tokens[token].type != bdecode_token::end);

			// skip value
			token += tokens[token].next_item;
		}

		return bdecode_node();
	}

	bdecode_node bdecode_node::dict_find_list(char const* key) const
	{
		bdecode_node ret = dict_find(key);
		if (ret.type() == bdecode_node::list_t)
			return ret;
		return bdecode_node();
	}

	bdecode_node bdecode_node::dict_find_dict(std::string key) const
	{
		bdecode_node ret = dict_find(key);
		if (ret.type() == bdecode_node::dict_t)
			return ret;
		return bdecode_node();
	}

	bdecode_node bdecode_node::dict_find_dict(char const* key) const
	{
		bdecode_node ret = dict_find(key);
		if (ret.type() == bdecode_node::dict_t)
			return ret;
		return bdecode_node();
	}

	bdecode_node bdecode_node::dict_find_string(char const* key) const
	{
		bdecode_node ret = dict_find(key);
		if (ret.type() == bdecode_node::string_t)
			return ret;
		return bdecode_node();
	}

	bdecode_node bdecode_node::dict_find_int(char const* key) const
	{
		bdecode_node ret = dict_find(key);
		if (ret.type() == bdecode_node::int_t)
			return ret;
		return bdecode_node();
	}


	bdecode_node bdecode_node::dict_find(char const* key) const
	{
		TORRENT_ASSERT(type() == dict_t);

		bdecode_token const* tokens = m_root_tokens;
	
		// this is the first item
		int token = m_token_idx + 1;

		while (tokens[token].type != bdecode_token::end)
		{
			bdecode_token const& t = tokens[token];
			TORRENT_ASSERT(t.type == bdecode_token::string);
			int size = m_root_tokens[token + 1].offset - t.offset - t.start_offset();
			if (string_equal(key, m_buffer + t.offset + t.start_offset(), size))
			{
				// skip key
				token += t.next_item;
				TORRENT_ASSERT(tokens[token].type != bdecode_token::end);
			
				return bdecode_node(tokens, m_buffer, m_buffer_size, token);
			}

			// skip key
			token += t.next_item;
			TORRENT_ASSERT(tokens[token].type != bdecode_token::end);

			// skip value
			token += tokens[token].next_item;
		}

		return bdecode_node();
	}

	std::string bdecode_node::dict_find_string_value(char const* key
		, char const* default_value) const
	{
		bdecode_node n = dict_find(key);
		if (n.type() != bdecode_node::string_t) return default_value;
		return n.string_value();
	}

	boost::int64_t bdecode_node::dict_find_int_value(char const* key
		, boost::int64_t default_val) const
	{
		bdecode_node n = dict_find(key);
		if (n.type() != bdecode_node::int_t) return default_val;
		return n.int_value();
	}

	boost::int64_t bdecode_node::int_value() const
	{
		TORRENT_ASSERT(type() == int_t);
		bdecode_token const& t = m_root_tokens[m_token_idx];
		int size = m_root_tokens[m_token_idx + 1].offset - t.offset;
		TORRENT_ASSERT(t.type == bdecode_token::integer);
	
		// +1 is to skip the 'i'
		char const* ptr = m_buffer + t.offset + 1;
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
		bdecode_token const& t = m_root_tokens[m_token_idx];
		int size = m_root_tokens[m_token_idx + 1].offset - t.offset - t.start_offset();
		TORRENT_ASSERT(t.type == bdecode_token::string);

		return std::string(m_buffer + t.offset + t.start_offset(), size);
	}

	char const* bdecode_node::string_ptr() const
	{
		TORRENT_ASSERT(type() == string_t);
		bdecode_token const& t = m_root_tokens[m_token_idx];
		TORRENT_ASSERT(t.type == bdecode_token::string);
		return m_buffer + t.offset + t.start_offset();
	}

	int bdecode_node::string_length() const
	{
		TORRENT_ASSERT(type() == string_t);
		bdecode_token const& t = m_root_tokens[m_token_idx];
		TORRENT_ASSERT(t.type == bdecode_token::string);
		return m_root_tokens[m_token_idx + 1].offset - t.offset - t.start_offset();
	}

	void bdecode_node::reserve(int tokens)
	{ m_tokens.reserve(tokens); }

	void bdecode_node::swap(bdecode_node& n)
	{
/*
		bool lhs_is_root = (m_root_tokens == &m_tokens);
		bool rhs_is_root = (n.m_root_tokens == &n.m_tokens);

		// swap is only defined between non-root nodes
		// and between root-nodes. They may not be mixed!
		// note that when swapping root nodes, all bdecode_node
		// entries that exist in those subtrees are invalidated!
		TORRENT_ASSERT(lhs_is_root == rhs_is_root);

		// if both are roots, m_root_tokens always point to
		// its own vector, and should not get swapped (the
		// underlying vectors are swapped already)
		if (!lhs_is_root && !rhs_is_root)
		{
			// if neither is a root, we just swap the pointers
			// to the token vectors, switching their roots
			std::swap(m_root_tokens, n.m_root_tokens);
		}
*/
		m_tokens.swap(n.m_tokens);
		std::swap(m_root_tokens, n.m_root_tokens);
		std::swap(m_buffer, n.m_buffer);
		std::swap(m_buffer_size, n.m_buffer_size);
		std::swap(m_token_idx, n.m_token_idx);
		std::swap(m_last_index, n.m_last_index);
		std::swap(m_last_token, n.m_last_token);
		std::swap(m_size, n.m_size);
	}

#define TORRENT_FAIL_BDECODE(code) do { \
	ec = make_error_code(code); \
	if (error_pos) *error_pos = start - orig_start; \
	goto done; \
	} while (false)

	int bdecode(char const* start, char const* end, bdecode_node& ret
		, error_code& ec, int* error_pos, int depth_limit, int token_limit)
	{
		ec.clear();
		ret.clear();

		if (end - start > bdecode_token::max_offset)
		{
			if (error_pos) *error_pos = 0;
			ec = make_error_code(bdecode_errors::limit_exceeded);
			return -1;
		}

		// this is the stack of bdecode_token indices, into m_tokens.
		// sp is the stack pointer, as index into the array, stack
		int sp = 0;
		stack_frame* stack = TORRENT_ALLOCA(stack_frame, depth_limit);

		char const* const orig_start = start;
		if (start == end) return 0;

		while (start <= end)
		{
			if (start >= end) TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);

			if (sp >= depth_limit)
				TORRENT_FAIL_BDECODE(bdecode_errors::depth_exceeded);

			--token_limit;
			if (token_limit < 0)
				TORRENT_FAIL_BDECODE(bdecode_errors::limit_exceeded);

			// look for a new token
			const char t = *start;

			const int current_frame = sp;

			// if we're currently parsing a dictionary, assert that
			// every other node is a string.
			if (current_frame > 0
				&& ret.m_tokens[stack[current_frame-1].token].type == bdecode_token::dict)
			{
				if (stack[current_frame-1].state == 0)
				{
					// the current parent is a dict and we are parsing a key.
					// only allow a digit (for a string) or 'e' to terminate
					if (!numeric(t) && t != 'e')
						TORRENT_FAIL_BDECODE(bdecode_errors::expected_digit);
				}
			}

			switch (t)
			{
				case 'd':
					stack[sp++] = ret.m_tokens.size();
					// we push it into the stack so that we know where to fill
					// in the next_node field once we pop this node off the stack.
					// i.e. get to the node following the dictionary in the buffer
					ret.m_tokens.push_back(bdecode_token(start - orig_start
						, bdecode_token::dict));
					++start;
					break;
				case 'l':
					stack[sp++] = ret.m_tokens.size();
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
					// +1 here to point to the first digit, rather than 'i'
					start = check_integer(start + 1, end, e);
					if (e)
					{
						// in order to gracefully terminate the tree,
						// make sure the end of the previous token is set correctly
						if (error_pos) *error_pos = start - orig_start;
						error_pos = NULL;
						start = int_start;
						TORRENT_FAIL_BDECODE(e);
					}
					ret.m_tokens.push_back(bdecode_token(int_start - orig_start
						, 1, bdecode_token::integer, 1));
					TORRENT_ASSERT(*start == 'e');

					// skip 'e'
					++start;
					break;
				}
				case 'e':
				{
					// this is the end of a list or dict
					if (sp == 0)
						TORRENT_FAIL_BDECODE(bdecode_errors::unexpected_eof);

					if (sp > 0
						&& ret.m_tokens[stack[sp-1].token].type == bdecode_token::dict
						&& stack[sp-1].state == 1)
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
					int top = stack[sp-1].token;
					// subtract the token's own index, since this is a relative
					// offset
					if (ret.m_tokens.size() - top > bdecode_token::max_next_item)
						TORRENT_FAIL_BDECODE(bdecode_errors::limit_exceeded);

					ret.m_tokens[top].next_item = ret.m_tokens.size() - top;

					// and pop it from the stack.
					--sp;
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

					// the bdecode_token only has 8 bits to keep the header size
					// in. If it overflows, fail!
					if (start - str_start - 2 > detail::bdecode_token::max_header)
						TORRENT_FAIL_BDECODE(bdecode_errors::limit_exceeded);

					ret.m_tokens.push_back(bdecode_token(str_start - orig_start
						, 1, bdecode_token::string, start - str_start));
					start += len;
					break;
				}
			}

			if (current_frame > 0
				&& ret.m_tokens[stack[current_frame-1].token].type == bdecode_token::dict)
			{
				// the next item we parse is the opposite
				stack[current_frame-1].state = ~stack[current_frame-1].state;
			}

			// this terminates the top level node, we're done!
			if (sp == 0) break;
		}

done:

		// if parse failed, sp will be greater than 1
		// unwind the stack by inserting terminator to make whatever we have
		// so far valid
		while (sp > 0) {
			TORRENT_ASSERT(ec);
			--sp;

			// we may need to insert a dummy token to properly terminate the tree,
			// in case we just parsed a key to a dict and failed in the value
			if (ret.m_tokens[stack[sp].token].type == bdecode_token::dict
				&& stack[sp].state == 1)
			{
				// insert an empty dictionary as the value
				ret.m_tokens.push_back(bdecode_token(start - orig_start
					, 2, bdecode_token::dict));
				ret.m_tokens.push_back(bdecode_token(start - orig_start
					, bdecode_token::end));
			}

			int top = stack[sp].token;
			TORRENT_ASSERT(ret.m_tokens.size() - top <= bdecode_token::max_next_item);
			ret.m_tokens[top].next_item = ret.m_tokens.size() - top;
			ret.m_tokens.push_back(bdecode_token(start - orig_start, 1, bdecode_token::end));
		}

		ret.m_tokens.push_back(bdecode_token(start - orig_start, 0
			, bdecode_token::end));

		ret.m_token_idx = 0;
		ret.m_buffer = orig_start;
		ret.m_buffer_size = start - orig_start;
		ret.m_root_tokens = &ret.m_tokens[0];

		return ec ? -1 : 0;
	}

	namespace {

	int line_longer_than(bdecode_node const& e, int limit)
	{
		int line_len = 0;
		switch (e.type())
		{
		case bdecode_node::list_t:
			line_len += 4;
			if (line_len > limit) return -1;
			for (int i = 0; i < e.list_size(); ++i)
			{
				int ret = line_longer_than(e.list_at(i), limit - line_len);
				if (ret == -1) return -1;
				line_len += ret + 2;
			}
			break;
		case bdecode_node::dict_t:
			line_len += 4;
			if (line_len > limit) return -1;
			for (int i = 0; i < e.dict_size(); ++i)
			{
				line_len += 4 + e.dict_at(i).first.size();
				if (line_len > limit) return -1;
				int ret = line_longer_than(e.dict_at(i).second, limit - line_len);
				if (ret == -1) return -1;
				line_len += ret + 1;
			}
			break;
		case bdecode_node::string_t:
			line_len += 3 + e.string_length();
			break;
		case bdecode_node::int_t:
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
		case bdecode_node::none_t:
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
				snprintf(tmp, sizeof(tmp), "\\x%02x", (unsigned char)str[i]);
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

}

	std::string print_entry(bdecode_node const& e
		, bool single_line, int indent)
	{
		char indent_str[200];
		using std::memset;
		memset(indent_str, ' ', 200);
		indent_str[0] = ',';
		indent_str[1] = '\n';
		indent_str[199] = 0;
		if (indent < 197 && indent >= 0) indent_str[indent+2] = 0;
		std::string ret;
		switch (e.type())
		{
			case bdecode_node::none_t: return "none";
			case bdecode_node::int_t:
			{
				char str[100];
				snprintf(str, sizeof(str), "%" PRId64, e.int_value());
				return str;
			}
			case bdecode_node::string_t:
			{
				print_string(ret, e.string_ptr(), e.string_length(), single_line);
				return ret;
			}
			case bdecode_node::list_t:
			{
				ret += '[';
				bool one_liner = line_longer_than(e, 200) != -1 || single_line;

				if (!one_liner) ret += indent_str + 1;
				for (int i = 0; i < e.list_size(); ++i)
				{
					if (i == 0 && one_liner) ret += " ";
					ret += print_entry(e.list_at(i), single_line, indent + 2);
					if (i < e.list_size() - 1) ret += (one_liner?", ":indent_str);
					else ret += (one_liner?" ":indent_str+1);
				}
				ret += "]";
				return ret;
			}
			case bdecode_node::dict_t:
			{
				ret += "{";
				bool one_liner = line_longer_than(e, 200) != -1 || single_line;

				if (!one_liner) ret += indent_str+1;
				for (int i = 0; i < e.dict_size(); ++i)
				{
					if (i == 0 && one_liner) ret += " ";
					std::pair<std::string, bdecode_node> ent = e.dict_at(i);
					print_string(ret, ent.first.c_str(), ent.first.size(), true);
					ret += ": ";
					ret += print_entry(ent.second, single_line, indent + 2);
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

