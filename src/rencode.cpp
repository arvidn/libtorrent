/*

Copyright (c) 2012, Arvid Norberg, Magnus Jonsson
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

#include "rencode.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/io.hpp"

namespace libtorrent {

enum renc_typecode
{
	CHR_LIST    = 59,
	CHR_DICT    = 60,
	CHR_INT     = 61,
	CHR_INT1    = 62,
	CHR_INT2    = 63,
	CHR_INT4    = 64,
	CHR_INT8    = 65,
	CHR_FLOAT32 = 66,
	CHR_FLOAT64 = 44,
	CHR_TRUE    = 67,
	CHR_FALSE   = 68,
	CHR_NONE    = 69,
	CHR_TERM    = 127,	
	// Positive integers with value embedded in typecode.
	INT_POS_FIXED_START = 0,
	INT_POS_FIXED_COUNT = 44,
	// Dictionaries with length embedded in typecode.
	DICT_FIXED_START = 102,
	DICT_FIXED_COUNT = 25,
	// Negative integers with value embedded in typecode.
	INT_NEG_FIXED_START = 70,
	INT_NEG_FIXED_COUNT = 32,
	// Strings with length embedded in typecode.
	STR_FIXED_START = 128,
	STR_FIXED_COUNT = 64,
	// Lists with length embedded in typecode.
	LIST_FIXED_START = 192,
	LIST_FIXED_COUNT = 64,
};

renc_type_t rtok_t::type() const
{
	if (m_typecode == CHR_TRUE || m_typecode == CHR_FALSE)
		return type_bool;
	if (m_typecode == CHR_FLOAT32 || m_typecode == CHR_FLOAT64)
		return type_float;
	if (m_typecode == CHR_DICT || (m_typecode >= DICT_FIXED_START
		&& m_typecode < DICT_FIXED_START+DICT_FIXED_COUNT))
		return type_dict;
	if (m_typecode == CHR_LIST || (m_typecode >= LIST_FIXED_START
		&& m_typecode < LIST_FIXED_START+LIST_FIXED_COUNT))
		return type_list;
	if (m_typecode == CHR_NONE) return type_none;
	if ((m_typecode >= '0' && m_typecode <= '9')
		|| (m_typecode >= STR_FIXED_START && m_typecode < STR_FIXED_START + STR_FIXED_COUNT))
		return type_string;
	return type_integer;
}

boost::int64_t rtok_t::integer(char const* buffer) const
{
	TORRENT_ASSERT(type() == type_integer);
	if (m_typecode >= INT_POS_FIXED_START && m_typecode < INT_POS_FIXED_START + INT_POS_FIXED_COUNT)
	{
		return m_typecode - INT_POS_FIXED_START;
	}

	if (m_typecode >= INT_NEG_FIXED_START && m_typecode < INT_NEG_FIXED_START + INT_NEG_FIXED_COUNT)
	{
		return -1 - (m_typecode - INT_NEG_FIXED_START);
	}

	char const* cursor = &buffer[m_offset + 1];

	namespace io = libtorrent::detail;

	if (m_typecode == CHR_INT1)
		return io::read_int8(cursor);

	if (m_typecode == CHR_INT2)
		return io::read_int16(cursor);

	if (m_typecode == CHR_INT4)
		return io::read_int32(cursor);

	if (m_typecode == CHR_INT8)
		return io::read_int64(cursor);

	return strtoll(cursor, NULL, 10);
}

std::string rtok_t::string(char const* buffer) const
{
	TORRENT_ASSERT(type() == type_string);
	if (m_typecode >= STR_FIXED_START && m_typecode < STR_FIXED_START + STR_FIXED_COUNT)
		return std::string(&buffer[m_offset + 1]
			, &buffer[m_offset + 1 + m_typecode - STR_FIXED_START]);

	int len = atoi(&buffer[m_offset]);
	if (len < 0) return std::string();
	
	char const* cursor = &buffer[m_offset];
	cursor = strchr(cursor, ':');
	if (cursor == NULL) return std::string();
	++cursor;
	return std::string(cursor, cursor + len);
}

bool rtok_t::boolean(char const* buffer) const
{
	TORRENT_ASSERT(type() == type_bool);
	return m_typecode == CHR_TRUE;
}

double rtok_t::floating_point(char const* buffer) const
{
	TORRENT_ASSERT(type() == type_float);

	namespace io = libtorrent::detail;

	char const* cursor = &buffer[m_offset];
	if (m_typecode == CHR_FLOAT32)
	{
		boost::uint32_t ret = io::read_uint32(cursor);
		return *reinterpret_cast<float*>(&ret);
	}

	if (m_typecode == CHR_FLOAT64)
	{
		boost::uint64_t ret = io::read_uint64(cursor);
		return *reinterpret_cast<double*>(&ret);
	}
	return 0.0;
}

// TODO: take len into account
int rdecode(rtok_t* tokens, int num_tokens, char const* buffer, int len)
{
	// number of tokens filled in
	int ret = 0;
	char const* cursor = buffer;

	ret = decode_token(buffer, cursor, tokens, num_tokens);
	if (ret <= 0) fprintf(stderr, "rdecode error: %s\n", cursor);
	return ret;
}

// return the number of slots used in the tokens array
int decode_token(char const* buffer, char const*& cursor, rtok_t* tokens, int num_tokens)
{
	namespace io = libtorrent::detail;

	if (num_tokens == 0) return -1;

	tokens->m_offset = cursor - buffer;
	tokens->m_num_items = 0;

	// cursor is progressed one byte by this call
	boost::uint8_t code = io::read_uint8(cursor);
	tokens->m_typecode = code;

	// a token should never start with a terminator
	if (code == CHR_TERM) return -1;

	if ((code >= INT_POS_FIXED_START && code < INT_POS_FIXED_START + INT_POS_FIXED_COUNT)
		|| (code >= INT_NEG_FIXED_START && code < INT_NEG_FIXED_START + INT_NEG_FIXED_COUNT)
		|| code == CHR_FALSE || code == CHR_TRUE || code == CHR_NONE)
	{
		return 1;
	}

	if (code == CHR_INT)
	{
		cursor = strchr(cursor, CHR_TERM);
		if (cursor == NULL) return -1;
		++cursor; // skip the terminator
		return 1;
	}

	if (code == CHR_INT1)
	{
		++cursor;
		return 1;
	}
	if (code == CHR_INT2)
	{
		cursor += 2;
		return 1;
	}
	if (code == CHR_INT4 || code == CHR_FLOAT32)
	{
		cursor += 4;
		return 1;
	}
	if (code == CHR_INT8 || code == CHR_FLOAT64)
	{
		cursor += 8;
		return 1;
	}
	if (code >= STR_FIXED_START && code < STR_FIXED_START + STR_FIXED_COUNT)
	{
		cursor += code - STR_FIXED_START;
		return 1;
	}

	if (code >= '0' && code <= '9')
	{
		int len = atoi(cursor-1);
		if (len < 0) return -1;
		cursor = strchr(cursor, ':');
		if (cursor == NULL) return -1;
		cursor += 1 + len;
		return 1;
	}

	if (code == CHR_DICT)
	{
		int used_tokens = 1;
		rtok_t* dict_root = tokens;
		++tokens;
		--num_tokens;
		while (*cursor != CHR_TERM)
		{
			// decode the key (must be a string)
			int ret = decode_token(buffer, cursor, tokens, num_tokens);
			if (ret == -1) return ret;
			if (tokens->type() != type_string) return -1;
			tokens += ret;
			num_tokens -= ret;
			used_tokens += ret;

			// decode the value
			ret = decode_token(buffer, cursor, tokens, num_tokens);
			if (ret == -1) return ret;
			tokens += ret;
			num_tokens -= ret;
			used_tokens += ret;
			++dict_root->m_num_items;
		}
		return used_tokens;
	}

	if (code == CHR_LIST)
	{
		int used_tokens = 1;
		rtok_t* list_root = tokens;
		++tokens;
		--num_tokens;
		while (*cursor != CHR_TERM)
		{
			int ret = decode_token(buffer, cursor, tokens, num_tokens);
			if (ret == -1) return ret;
			tokens += ret;
			num_tokens -= ret;
			used_tokens += ret;
			++list_root->m_num_items;
		}
		return used_tokens;
	}

	if (code >= DICT_FIXED_START && code < DICT_FIXED_START + DICT_FIXED_COUNT)
	{
		int used_tokens = 1;
		int size = code - DICT_FIXED_START;
		rtok_t* dict_root = tokens;
		++tokens;
		--num_tokens;
		for (int i = 0; i < size; ++i)
		{
			// decode the key (must be a string)
			int ret = decode_token(buffer, cursor, tokens, num_tokens);
			if (ret == -1) return ret;
			if (tokens->type() != type_string) return -1;
			tokens += ret;
			num_tokens -= ret;
			used_tokens += ret;

			// decode the value
			ret = decode_token(buffer, cursor, tokens, num_tokens);
			if (ret == -1) return ret;
			tokens += ret;
			num_tokens -= ret;
			used_tokens += ret;
			++dict_root->m_num_items;
		}
		return used_tokens;
	}

	if (code >= LIST_FIXED_START && code < LIST_FIXED_START + LIST_FIXED_COUNT)
	{
		int used_tokens = 1;
		int size = code - LIST_FIXED_START;
		rtok_t* list_root = tokens;
		++tokens;
		--num_tokens;
		for (int i = 0; i < size; ++i)
		{
			int ret = decode_token(buffer, cursor, tokens, num_tokens);
			if (ret == -1)
				return ret;
			tokens += ret;
			num_tokens -= ret;
			used_tokens += ret;
			++list_root->m_num_items;
		}
		return used_tokens;
	}

	TORRENT_ASSERT(false);
	return -1;
}

// returns the number of tokens that were printed
int print_rtok(rtok_t const* tokens, char const* buf)
{
	int consumed = 1;
	if (tokens->type() == type_list)
	{
		printf("[");
		int num_items = tokens->num_items();
		for (int i = 0; i < num_items; ++i)
		{
			consumed += print_rtok(tokens + consumed, buf);
			if (i < num_items - 1) printf(", ");
		}
		printf("]");
	}
	else if (tokens->type() == type_dict)
	{
		printf("{");
		int num_items = tokens->num_items();
		for (int i = 0; i < num_items * 2; i += 2)
		{
			consumed += print_rtok(tokens + consumed, buf);
			printf(": ");
			consumed += print_rtok(tokens + consumed, buf);
			if (i < (num_items-1) * 2) printf(", ");
		}
		printf("}");
	}
	else if (tokens->type() == type_integer)
	{
		printf("%" PRId64, tokens->integer(buf));
	}
	else if (tokens->type() == type_string)
	{
		printf("\"%s\"", tokens->string(buf).c_str());
	}
	else if (tokens->type() == type_float)
	{
		printf("%f", tokens->floating_point(buf));
	}
	else if (tokens->type() == type_none)
	{
		printf("None"); 
	}
	else if (tokens->type() == type_bool)
	{
		printf(tokens->boolean(buf) ? "True":"False"); 
	}
	return consumed;
}

// skip i. if i points to an object or an array, this function
// needs to make recursive calls to skip its members too
rtok_t* skip_item(rtok_t* i)
{
	int n = i->num_items();
	if (i->type() == type_dict) n *= 2;
	++i;
	// if it's a literal, just skip it, and we're done
	if (n == 0) return i;
	// if it's a container, we need to skip n items
	for (int k = 0; k < n; ++k)
		i = skip_item(i);
	return i;
}

rtok_t* find_key(rtok_t* tokens, char* buf, char const* key, int type)
{
	if (tokens->type() != type_dict) return NULL;
	int num_keys = tokens->num_items();
	// we skip two items at a time, first the key then the value
	for (rtok_t* i = &tokens[1]; num_keys > 0; i = skip_item(skip_item(i)), --num_keys)
	{
		if (i->type() != type_string) continue;
		if (i->string(buf) != key) continue;
		if (i[1].type() != type) continue;
		return i + 1;
	}
	return NULL;
}

std::string find_string(rtok_t* tokens, char* buf, char const* key, bool* found)
{
	rtok_t* k = find_key(tokens, buf, key, type_string);
	if (k == NULL)
	{
		if (found) *found = false;
		return "";
	}
	if (found) *found = true;
	return k->string(buf);
}

boost::int64_t find_int(rtok_t* tokens, char* buf, char const* key, bool* found)
{
	rtok_t* k = find_key(tokens, buf, key, type_integer);
	if (k == NULL)
	{
		if (found) *found = false;
		return 0;
	}
	if (found) *found = true;
	return k->integer(buf);
}

bool find_bool(rtok_t* tokens, char* buf, char const* key)
{
	rtok_t* k = find_key(tokens, buf, key, type_bool);
	if (k == NULL) return false;
	return k->boolean(buf);
}

// format strings can contain:
// i = integer
// f = float
// [] = list
// {} = dicts
// b = boolean
// n = none
// s = string
// example: [is[]{}] verifies the format of RPC calls
bool validate_structure(rtok_t const* tokens, char const* fmt)
{
	// TODO: the number of items in lists or dicts are not verified!
	int offset = 0;
	std::vector<int> stack;
	while (*fmt)
	{
		switch (*fmt)
		{
			case 'i':
				if (tokens[offset].type() != type_integer)
					return false;
				break;
			case 'f':
				if (tokens[offset].type() != type_float)
					return false;
				break;
			case 'b':
				if (tokens[offset].type() != type_bool)
					return false;
				break;
			case 's':
				if (tokens[offset].type() != type_string)
					return false;
				break;
			case 'n':
				if (tokens[offset].type() != type_none)
					return false;
				break;
			case '[':
				if (tokens[offset].type() != type_list)
					return false;
				stack.push_back(offset);
				break;
			case '{':
				if (tokens[offset].type() != type_dict)
					return false;
				stack.push_back(offset);
				break;
			case ']': {
				if (stack.empty() || tokens[stack.back()].type() != type_list)
					return false;
				rtok_t* t = skip_item((rtok_t*)&tokens[stack.back()]);
				stack.pop_back();
				if (t == NULL) return false;
				// offset is incremented below, the -1 is to take that
				// into account
				offset = t - tokens - 1;
				break;
			 }
			case '}': {
				if (stack.empty() || tokens[stack.back()].type() != type_dict)
					return false;
				rtok_t* t = skip_item((rtok_t*)&tokens[stack.back()]);
				stack.pop_back();
				if (t == NULL) return false;
				// offset is incremented below, the -1 is to take that
				// into account
				offset = t - tokens - 1;
				break;
			 }
			default:
				// invalid format string
				fprintf(stderr, "invalid format character: %c", *fmt);
				return false;
		};
//		fprintf(stderr, "%d: %c\n", offset, *fmt);
		++offset;
		++fmt;
	}
	return true;
}

bool rencoder::append_list(int size)
{
	if (size < 0 || size > LIST_FIXED_COUNT)
	{
		m_buffer.push_back(CHR_LIST);
		return true;
	}
	else
	{
		m_buffer.push_back(LIST_FIXED_START + size);
		return false;
	}
}

bool rencoder::append_dict(int size)
{
	if (size < 0 || size > DICT_FIXED_COUNT)
	{
		m_buffer.push_back(CHR_DICT);
		return true;
	}
	else
	{
		m_buffer.push_back(DICT_FIXED_START + size);
		return false;
	}
}

void rencoder::append_int(boost::int64_t i)
{
	if (i >= 0 && i < INT_POS_FIXED_COUNT)
	{
		m_buffer.push_back(INT_POS_FIXED_START + i);
	}
	else if (i < 0 && i > -INT_NEG_FIXED_COUNT)
	{
		m_buffer.push_back(INT_NEG_FIXED_START - i - 1);
	}
	else if (i < 0x80 && i >= -0x7f)
	{
		m_buffer.push_back(CHR_INT1);
		m_buffer.push_back(i);
	}
	else if (i < 0x8000 && i >= -0x7fff)
	{
		m_buffer.push_back(CHR_INT2);
		m_buffer.push_back(i >> 8);
		m_buffer.push_back(i & 0xff);
	}
	else if (i < 0x80000000 && i >= -0x7fffffff)
	{
		m_buffer.push_back(CHR_INT4);
		m_buffer.push_back((i >> 24) & 0xff);
		m_buffer.push_back((i >> 16) & 0xff);
		m_buffer.push_back((i >> 8) & 0xff);
		m_buffer.push_back((i >> 0) & 0xff);
	}
	else // if (i < 0x8000000000000000LL && i >= -0x7fffffffffffffffLL)
	{
		m_buffer.push_back(CHR_INT8);
		m_buffer.push_back((i >> 56) & 0xff);
		m_buffer.push_back((i >> 48) & 0xff);
		m_buffer.push_back((i >> 40) & 0xff);
		m_buffer.push_back((i >> 32) & 0xff);
		m_buffer.push_back((i >> 24) & 0xff);
		m_buffer.push_back((i >> 16) & 0xff);
		m_buffer.push_back((i >> 8) & 0xff);
		m_buffer.push_back((i >> 0) & 0xff);
	}
}

void rencoder::append_float(float f)
{
	m_buffer.push_back(CHR_FLOAT32);
	union
	{
		float in;
		boost::uint32_t out;
	};

	in = f;
	m_buffer.push_back((out >> 24) & 0xff);
	m_buffer.push_back((out >> 16) & 0xff);
	m_buffer.push_back((out >> 8) & 0xff);
	m_buffer.push_back((out >> 0) & 0xff);
}

void rencoder::append_none()
{
	m_buffer.push_back(CHR_NONE);
}

void rencoder::append_bool(bool b)
{
	m_buffer.push_back(b ? CHR_TRUE : CHR_FALSE);
}

void rencoder::append_string(std::string const& s)
{
	if (s.size() < STR_FIXED_COUNT)
	{
		m_buffer.push_back(STR_FIXED_START + s.size());
		m_buffer.insert(m_buffer.end(), s.begin(), s.end());
	}
	else
	{
		char buf[10];
		int len = snprintf(buf, sizeof(buf), "%d:", int(s.size()));
		m_buffer.insert(m_buffer.end(), buf, buf + len);
		m_buffer.insert(m_buffer.end(), s.begin(), s.end());
	}
}

void rencoder::append_term()
{
	m_buffer.push_back(CHR_TERM);
}

}

