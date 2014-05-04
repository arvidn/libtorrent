/*

Copyright (c) 2012-2014, Arvid Norberg
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

#ifndef TORRENT_STRING_UTIL_HPP_INCLUDED
#define TORRENT_STRING_UTIL_HPP_INCLUDED

#include "libtorrent/config.hpp"

namespace libtorrent
{
	TORRENT_EXTRA_EXPORT bool is_alpha(char c);
	// this is used by bdecode_recursive's header file
	TORRENT_EXPORT bool is_digit(char c);
	TORRENT_EXTRA_EXPORT bool is_print(char c);
	TORRENT_EXTRA_EXPORT bool is_space(char c);
	TORRENT_EXTRA_EXPORT char to_lower(char c);

	TORRENT_EXTRA_EXPORT int split_string(char const** tags, int buf_size, char* in);
	TORRENT_EXTRA_EXPORT bool string_begins_no_case(char const* s1, char const* s2);
	TORRENT_EXTRA_EXPORT bool string_equal_no_case(char const* s1, char const* s2);

	TORRENT_EXTRA_EXPORT void url_random(char* begin, char* end);

	// strdup is not part of the C standard. Some systems
	// don't have it and it won't be available when building
	// in strict ansi mode
	char* allocate_string_copy(char const* str);

	// returns p + x such that the pointer is 8 bytes aligned
	// x cannot be greater than 7
	void* align_pointer(void* p);

	// searches for separator in the string 'last'. the pointer last points to
	// is set to point to the first character following the separator.
	// returns a pointer to a null terminated string starting at last, ending
	// at the separator (the string is mutated to replace the separator with
	// a '\0' character). If there is no separator, but the end of the string,
	// the pointer next points to is set to the last null terminator, which will
	// make the following invocation return NULL, to indicate the end of the
	// string.
	TORRENT_EXTRA_EXPORT char* string_tokenize(char* last, char sep, char** next);
}

#endif

