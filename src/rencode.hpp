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

#ifndef TORRENT_RENCODE_HPP
#define TORRENT_RENCODE_HPP

#include <boost/cstdint.hpp>
#include <string>
#include <vector>

namespace libtorrent {

enum renc_type_t
{
	type_integer = 0,
	type_string = 1,
	type_list = 2,
	type_dict = 3,
	type_float = 4,
	type_none = 5,
	type_bool = 6
};

struct rtok_t
{
	friend int rdecode(rtok_t* tokens, int num_tokens, char const* buffer, int len);
	friend int decode_token(char const* buffer, char const*& cursor, rtok_t* tokens, int num_tokens);

	renc_type_t type() const;
	// parse out the value of an integer
	boost::int64_t integer(char const* buffer) const;
	// parse out the value of a string
	std::string string(char const* buffer) const;
	bool boolean(char const* buffer) const;
	double floating_point(char const* buffer) const;
	int num_items() const { return m_num_items; }
private:
	int m_offset;
	boost::uint8_t m_typecode;
	// for dicts, this is the number of key-value pairs
	// for lists, this is the number of elements
	boost::uint16_t m_num_items;
};

int rdecode(rtok_t* tokens, int num_tokens, char const* buffer, int len);

int print_rtok(rtok_t const* tokens, char const* buf);

rtok_t* skip_item(rtok_t* i);

rtok_t* find_key(rtok_t* tokens, char* buf, char const* key, int type);
std::string find_string(rtok_t* tokens, char* buf, char const* key, bool* found);
boost::int64_t find_int(rtok_t* tokens, char* buf, char const* key, bool* found);
bool find_bool(rtok_t* tokens, char* buf, char const* key);

bool validate_structure(rtok_t const* tokens, char const* fmt);

struct rencoder
{
	bool append_list(int size = -1);
	bool append_dict(int size = -1);
	void append_int(boost::int64_t i);
	void append_float(float f);
	void append_none();
	void append_bool(bool b);
	void append_string(std::string const& s);
	void append_term();

	char const* data() const { return &m_buffer[0]; }
	int len() const { return m_buffer.size(); }

	void clear() { m_buffer.clear(); }
private:
	std::vector<char> m_buffer;
};

}

#endif

