/*

Copyright (c) 2012, Arvid Norberg
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


#include "json_util.hpp"
#include <string.h> // for strcmp()
#include <stdlib.h> // for strtoll()

namespace libtorrent {

// skip i. if i points to an object or an array, this function
// needs to make recursive calls to skip its members too
jsmntok_t* skip_item(jsmntok_t* i)
{
	int n = i->size;
	++i;
	// if it's a literal, just skip it, and we're done
	if (n == 0) return i;
	// if it's a container, we need to skip n items
	for (int k = 0; k < n; ++k)
		i = skip_item(i);
	return i;
}

jsmntok_t* find_key(jsmntok_t* tokens, char* buf, char const* key, int type)
{
	if (tokens[0].type != JSMN_OBJECT) return NULL;
	// size is the number of tokens at the object level.
	// half of them are keys, the other half
	int num_keys = tokens[0].size / 2;
	// we skip two items at a time, first the key then the value
	for (jsmntok_t* i = &tokens[1]; num_keys > 0; i = skip_item(skip_item(i)), --num_keys)
	{
		if (i->type != JSMN_STRING) continue;
		buf[i->end] = 0;
		if (strcmp(key, buf + i->start)) continue;
		if (i[1].type != type) continue;
		return i + 1;
	}
	return NULL;
}

char const* find_string(jsmntok_t* tokens, char* buf, char const* key, bool* found)
{
	jsmntok_t* k = find_key(tokens, buf, key, JSMN_STRING);
	if (k == NULL)
	{
		if (found) *found = false;
		return "";
	}
	if (found) *found = true;
	buf[k->end] = '\0';
	return buf + k->start;
}

boost::int64_t find_int(jsmntok_t* tokens, char* buf, char const* key, bool* found)
{
	jsmntok_t* k = find_key(tokens, buf, key, JSMN_PRIMITIVE);
	if (k == NULL)
	{
		if (found) *found = false;
		return 0;
	}
	buf[k->end] = '\0';
	if (found) *found = true;
	return strtoll(buf + k->start, NULL, 10);
}

bool find_bool(jsmntok_t* tokens, char* buf, char const* key)
{
	jsmntok_t* k = find_key(tokens, buf, key, JSMN_PRIMITIVE);
	if (k == NULL) return false;
	buf[k->end] = '\0';
	return strcmp(buf + k->start, "true") == 0;
}

}

