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
	LIST_FIXED_START = STR_FIXED_START+STR_FIXED_COUNT,
	LIST_FIXED_COUNT = 64,
};

renc_type_t rtok_t::type() const
{
	if (m_typecode == CHR_TRUE || m_typecode == CHR_FALSE)
		return type_bool;
	if (m_typecode == CHR_FLOAT32 || m_typecode == CHR_FLOAT64)
		return type_float;
	if (m_typecode == CHR_DICT || (m_typecode > DICT_FIXED_START
		&& m_typecode < DICT_FIXED_START+DICT_FIXED_COUNT))
		return type_dict;
	if (m_typecode == CHR_LIST || (m_typecode > LIST_FIXED_START
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

	return decode_token(buffer, cursor, tokens, num_tokens);
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
			if (ret == -1) return ret;
			if (tokens->type() != type_string) return -1;
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

}

