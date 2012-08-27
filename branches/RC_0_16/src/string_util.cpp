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

#include "libtorrent/config.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/random.hpp"

#include <stdlib.h> // for malloc/free
#include <string.h> // for strcpy/strlen

namespace libtorrent
{

	bool is_alpha(char c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	}

	bool is_digit(char c)
	{
		return c >= '0' && c <= '9';
	}

	bool is_print(char c)
	{
		return c >= 32 && c < 127;
	}

	bool is_space(char c)
	{
		const static char* ws = " \t\n\r\f\v";
		return strchr(ws, c) != 0;
	}

	char to_lower(char c)
	{
		return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
	}

	int split_string(char const** tags, int buf_size, char* in)
	{
		int ret = 0;
		char* i = in;
		for (;*i; ++i)
		{
			if (!is_print(*i) || is_space(*i))
			{
				*i = 0;
				if (ret == buf_size) return ret;
				continue;
			}
			if (i == in || i[-1] == 0)
			{
				tags[ret++] = i;
			}
		}
		return ret;
	}

	bool string_begins_no_case(char const* s1, char const* s2)
	{
		while (*s1 != 0)
		{
			if (to_lower(*s1) != to_lower(*s2)) return false;
			++s1;
			++s2;
		}
		return true;
	}

	bool string_equal_no_case(char const* s1, char const* s2)
	{
		while (to_lower(*s1) == to_lower(*s2))
		{
			if (*s1 == 0) return true;
			++s1;
			++s2;
		}
		return false;
	}

	// generate a url-safe random string
	void url_random(char* begin, char* end)
	{
		// http-accepted characters:
		// excluding ', since some buggy trackers don't support that
		static char const printable[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz-_.!~*()";

		// the random number
		while (begin != end)
			*begin++ = printable[random() % (sizeof(printable)-1)];
	}

	char* allocate_string_copy(char const* str)
	{
		if (str == 0) return 0;
		char* tmp = (char*)malloc(strlen(str) + 1);
		if (tmp == 0) return 0;
		strcpy(tmp, str);
		return tmp;
	}

}

